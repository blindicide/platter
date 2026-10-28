// SPDX-License-Identifier: TBD
#include "platter/core/operation.hpp"

namespace platter {

std::string_view operation_state_name(OperationState s) noexcept {
    switch (s) {
        case OperationState::created: return "CREATED";
        case OperationState::validating: return "VALIDATING";
        case OperationState::ready: return "READY";
        case OperationState::running: return "RUNNING";
        case OperationState::paused: return "PAUSED";
        case OperationState::stopping: return "STOPPING";
        case OperationState::completed: return "COMPLETED";
        case OperationState::failed: return "FAILED";
        case OperationState::cancelled: return "CANCELLED";
    }
    return "?";
}

bool is_terminal(OperationState s) noexcept {
    return s == OperationState::completed || s == OperationState::failed || s == OperationState::cancelled;
}

bool is_valid_transition(OperationState from, OperationState to) noexcept {
    using S = OperationState;
    switch (from) {
        case S::created: return to == S::validating || to == S::cancelled;
        case S::validating: return to == S::ready || to == S::failed;
        case S::ready: return to == S::running || to == S::cancelled;
        case S::running: return to == S::paused || to == S::stopping || to == S::completed || to == S::failed;
        case S::paused: return to == S::running || to == S::stopping || to == S::failed;
        case S::stopping: return to == S::cancelled || to == S::completed || to == S::failed;
        default: return false;  // terminal states are final
    }
}

// --------------------------------------------------------------- control

void OperationControl::request_cancel() {
    {
        std::lock_guard lk(m_);
        cancel_.store(true, std::memory_order_release);
    }
    cv_.notify_all();
}

void OperationControl::request_pause() {
    std::lock_guard lk(m_);
    pause_.store(true, std::memory_order_release);
}

void OperationControl::request_resume() {
    {
        std::lock_guard lk(m_);
        pause_.store(false, std::memory_order_release);
    }
    cv_.notify_all();
}

// --------------------------------------------------------------- operation

namespace {
std::atomic<std::uint64_t> g_next_id{1};
}

Operation::Operation(std::string type, std::string device_id, AccessKind access)
    : id_(g_next_id.fetch_add(1)), type_(std::move(type)), device_id_(std::move(device_id)), access_(access) {
    p_.operation_id = id_;
    p_.type = type_;
    p_.device_id = device_id_;
}

Operation::~Operation() {
    control_.request_cancel();
    control_.request_resume();
    if (worker_.joinable()) worker_.join();
}

void Operation::transition(OperationState to) {
    // Caller holds m_. Invalid edges are ignored so racing requests (e.g. a
    // pause arriving just as the scan completes) cannot corrupt the state.
    if (!is_valid_transition(state_, to)) return;
    const auto now = std::chrono::steady_clock::now();
    if (to == OperationState::paused) paused_at_ = now;
    if (state_ == OperationState::paused && to != OperationState::paused) paused_total_ += now - paused_at_;
    state_ = to;
    p_.state = to;
}

Status Operation::validate() {
    {
        std::lock_guard lk(m_);
        if (state_ != OperationState::created) return Error{Errc::invalid_argument, "operation already validated"};
        transition(OperationState::validating);
    }
    auto s = do_validate();
    std::lock_guard lk(m_);
    if (!s) {
        p_.message = s.error().describe();
        transition(OperationState::failed);
        return s;
    }
    transition(OperationState::ready);
    return {};
}

Status Operation::start() {
    std::lock_guard lk(m_);
    if (state_ != OperationState::ready)
        return Error{Errc::invalid_argument,
                     "cannot start an operation in state " + std::string(operation_state_name(state_))};
    started_ = std::chrono::steady_clock::now();
    window_start_ = started_;
    p_.start_time = std::chrono::system_clock::now();
    transition(OperationState::running);
    worker_ = std::thread([this] { worker(); });
    return {};
}

void Operation::worker() {
    Status result = do_run(control_);
    std::lock_guard lk(m_);
    p_.elapsed = active_elapsed_locked();
    const bool cancelled = control_.cancel_requested() || (!result && result.error().code == Errc::cancelled);
    if (cancelled) {
        transition(OperationState::stopping);
        transition(OperationState::cancelled);
        if (p_.message.empty()) p_.message = "cancelled by user";
    } else if (!result) {
        p_.message = result.error().describe();
        transition(OperationState::stopping);
        transition(OperationState::failed);
    } else {
        transition(OperationState::stopping);
        transition(OperationState::completed);
    }
    p_.eta.reset();
    done_cv_.notify_all();
}

void Operation::pause() {
    std::lock_guard lk(m_);
    if (state_ != OperationState::running) return;
    control_.request_pause();
    transition(OperationState::paused);
}

void Operation::resume() {
    std::lock_guard lk(m_);
    if (state_ != OperationState::paused) return;
    transition(OperationState::running);
    control_.request_resume();
}

void Operation::cancel() {
    {
        std::lock_guard lk(m_);
        if (state_ == OperationState::created || state_ == OperationState::ready) {
            transition(OperationState::cancelled);
            p_.message = "cancelled before start";
            done_cv_.notify_all();
            return;
        }
        if (state_ == OperationState::running || state_ == OperationState::paused) transition(OperationState::stopping);
    }
    control_.request_cancel();
    control_.request_resume();  // wake a paused worker so it can observe the cancel
}

void Operation::wait() {
    {
        std::unique_lock lk(m_);
        done_cv_.wait(lk, [&] { return is_terminal(state_); });
    }
    if (worker_.joinable() && worker_.get_id() != std::this_thread::get_id()) worker_.join();
}

OperationState Operation::state() const {
    std::lock_guard lk(m_);
    return state_;
}

std::chrono::nanoseconds Operation::active_elapsed_locked() const {
    if (started_ == std::chrono::steady_clock::time_point{}) return std::chrono::nanoseconds{0};
    auto end = std::chrono::steady_clock::now();
    if (state_ == OperationState::paused) end = paused_at_;
    if (is_terminal(state_)) return p_.elapsed;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(end - started_ - paused_total_);
}

OperationProgress Operation::progress() const {
    std::lock_guard lk(m_);
    OperationProgress p = p_;
    p.elapsed = active_elapsed_locked();
    const double secs = std::chrono::duration<double>(p.elapsed).count();
    p.average_bytes_per_sec = secs > 0 ? static_cast<double>(p.completed_bytes) / secs : 0;
    p.current_bytes_per_sec = window_rate_ > 0 ? window_rate_ : p.average_bytes_per_sec;
    if (state_ == OperationState::paused) p.current_bytes_per_sec = 0;
    if (!is_terminal(state_) && p.average_bytes_per_sec > 0 && p.total_bytes > p.completed_bytes)
        p.eta = std::chrono::seconds(static_cast<std::int64_t>(
            static_cast<double>(p.total_bytes - p.completed_bytes) / p.average_bytes_per_sec));
    return p;
}

void Operation::set_total_bytes(std::uint64_t total) {
    std::lock_guard lk(m_);
    p_.total_bytes = total;
}

void Operation::add_completed(std::uint64_t bytes, std::uint64_t current_lba) {
    std::lock_guard lk(m_);
    p_.completed_bytes += bytes;
    p_.current_lba = current_lba;
    window_bytes_ += bytes;
    const auto now = std::chrono::steady_clock::now();
    const auto dt = std::chrono::duration<double>(now - window_start_).count();
    if (dt >= 1.0) {
        window_rate_ = static_cast<double>(window_bytes_) / dt;
        window_bytes_ = 0;
        window_start_ = now;
    }
}

void Operation::add_error(std::uint64_t n) {
    std::lock_guard lk(m_);
    p_.errors += n;
}

void Operation::add_warning(std::uint64_t n) {
    std::lock_guard lk(m_);
    p_.warnings += n;
}

void Operation::set_message(std::string msg) {
    std::lock_guard lk(m_);
    p_.message = std::move(msg);
}

// --------------------------------------------------------------- leases

DeviceLeaseRegistry::Lease::Lease(Lease&& o) noexcept : reg_(o.reg_), id_(std::move(o.id_)), kind_(o.kind_) {
    o.reg_ = nullptr;
}

DeviceLeaseRegistry::Lease& DeviceLeaseRegistry::Lease::operator=(Lease&& o) noexcept {
    if (this != &o) {
        release();
        reg_ = o.reg_;
        id_ = std::move(o.id_);
        kind_ = o.kind_;
        o.reg_ = nullptr;
    }
    return *this;
}

DeviceLeaseRegistry::Lease::~Lease() { release(); }

void DeviceLeaseRegistry::Lease::release() {
    if (reg_) reg_->release(id_, kind_);
    reg_ = nullptr;
}

Result<DeviceLeaseRegistry::Lease> DeviceLeaseRegistry::acquire(const std::string& device_id, AccessKind kind) {
    std::lock_guard lk(m_);
    auto& e = entries_[device_id];
    if (e.exclusive)
        return Error{Errc::device_busy, device_id + " is held exclusively by a destructive operation"};
    if (kind == AccessKind::exclusive && e.readers > 0)
        return Error{Errc::device_busy, device_id + " is in use by " + std::to_string(e.readers) + " other operation(s)"};
    if (kind == AccessKind::exclusive) e.exclusive = true;
    else ++e.readers;
    return Lease(this, device_id, kind);
}

void DeviceLeaseRegistry::release(const std::string& id, AccessKind kind) {
    std::lock_guard lk(m_);
    auto it = entries_.find(id);
    if (it == entries_.end()) return;
    if (kind == AccessKind::exclusive) it->second.exclusive = false;
    else if (it->second.readers > 0) --it->second.readers;
    if (!it->second.exclusive && it->second.readers == 0) entries_.erase(it);
}

std::size_t DeviceLeaseRegistry::holders(const std::string& device_id) const {
    std::lock_guard lk(m_);
    auto it = entries_.find(device_id);
    if (it == entries_.end()) return 0;
    return static_cast<std::size_t>(it->second.readers) + (it->second.exclusive ? 1 : 0);
}

DeviceLeaseRegistry& DeviceLeaseRegistry::global() {
    static DeviceLeaseRegistry r;
    return r;
}

}  // namespace platter
