// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Common execution framework for long-running device operations (spec §3.3).
//
// An Operation runs on its own worker thread. Front ends never receive
// per-sector callbacks; they poll progress() at their own rate (the GUI uses
// ~250 ms), which keeps disk I/O and rendering decoupled (spec §18).
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

#include "platter/core/error.hpp"

namespace platter {

enum class OperationState : std::uint8_t {
    created,
    validating,
    ready,
    running,
    paused,
    stopping,
    completed,
    failed,
    cancelled,
};

std::string_view operation_state_name(OperationState s) noexcept;
bool is_terminal(OperationState s) noexcept;
// The state machine's allowed edges. Everything else is a programming error.
bool is_valid_transition(OperationState from, OperationState to) noexcept;

enum class AccessKind : std::uint8_t {
    shared_read,  // may run alongside other shared_read operations
    exclusive,    // destructive: nothing else may touch the device
};

struct OperationProgress {
    std::uint64_t operation_id = 0;
    std::string device_id;
    std::string type;
    OperationState state = OperationState::created;

    std::chrono::system_clock::time_point start_time{};
    std::chrono::nanoseconds elapsed{0};  // excludes time spent paused

    std::uint64_t completed_bytes = 0;
    std::uint64_t total_bytes = 0;
    double current_bytes_per_sec = 0;     // recent window
    double average_bytes_per_sec = 0;     // completed_bytes / elapsed
    std::optional<std::chrono::seconds> eta;

    std::uint64_t errors = 0;
    std::uint64_t warnings = 0;
    std::uint64_t current_lba = 0;

    std::string message;                  // last status / failure description

    double percent() const noexcept {
        return total_bytes ? 100.0 * static_cast<double>(completed_bytes) / static_cast<double>(total_bytes) : 0.0;
    }
};

// Cooperative cancellation and pause. Workers call checkpoint() between I/O
// requests: it blocks while paused and returns false once cancellation has
// been requested. Commands already executing inside device firmware cannot be
// interrupted, so cancellation takes effect at the next checkpoint.
class OperationControl {
public:
    void request_cancel();
    void request_pause();
    void request_resume();

    bool cancel_requested() const noexcept { return cancel_.load(std::memory_order_acquire); }
    bool pause_requested() const noexcept { return pause_.load(std::memory_order_acquire); }

    // Blocks while paused; `on_pause(true/false)` is called when entering and
    // leaving the paused state. Returns false if the operation must stop.
    template <typename OnPause>
    bool checkpoint(OnPause&& on_pause) {
        if (cancel_requested()) return false;
        if (!pause_requested()) return true;
        std::unique_lock lk(m_);
        on_pause(true);
        cv_.wait(lk, [&] { return !pause_requested() || cancel_requested(); });
        on_pause(false);
        return !cancel_requested();
    }
    bool checkpoint() {
        return checkpoint([](bool) {});
    }

private:
    std::atomic<bool> cancel_{false};
    std::atomic<bool> pause_{false};
    std::mutex m_;
    std::condition_variable cv_;
};

// Base class for every long-running operation.
class Operation {
public:
    Operation(std::string type, std::string device_id, AccessKind access);
    virtual ~Operation();

    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;

    // CREATED -> VALIDATING -> READY (or FAILED). Synchronous and cheap.
    Status validate();
    // READY -> RUNNING on a worker thread. Returns immediately.
    Status start();
    // Ignored for operations whose results pausing would invalidate (benchmarks).
    void pause();
    void resume();
    virtual bool supports_pause() const noexcept { return true; }
    void cancel();
    // Blocks until the worker has finished (terminal state).
    void wait();

    OperationState state() const;
    OperationProgress progress() const;
    std::uint64_t id() const noexcept { return id_; }
    AccessKind access() const noexcept { return access_; }
    const std::string& device_id() const noexcept { return device_id_; }

protected:
    // Implemented by subclasses. Called on the calling thread during validate().
    virtual Status do_validate() = 0;
    // Called on the worker thread. Return ok for COMPLETED, Errc::cancelled
    // for CANCELLED (the base also infers that from the control), anything
    // else for FAILED.
    virtual Status do_run(OperationControl& control) = 0;

    // Progress reporting for subclasses (thread-safe, cheap).
    void set_total_bytes(std::uint64_t total);
    void add_completed(std::uint64_t bytes, std::uint64_t current_lba);
    void add_error(std::uint64_t n = 1);
    void add_warning(std::uint64_t n = 1);
    void set_message(std::string msg);

    OperationControl& control() noexcept { return control_; }

private:
    void transition(OperationState to);
    void worker();
    std::chrono::nanoseconds active_elapsed_locked() const;

    const std::uint64_t id_;
    const std::string type_;
    const std::string device_id_;
    const AccessKind access_;

    mutable std::mutex m_;
    OperationState state_ = OperationState::created;
    OperationProgress p_;
    std::chrono::steady_clock::time_point started_{};
    std::chrono::steady_clock::time_point paused_at_{};
    std::chrono::nanoseconds paused_total_{0};

    // Sliding window for "current throughput".
    std::chrono::steady_clock::time_point window_start_{};
    std::uint64_t window_bytes_ = 0;
    double window_rate_ = 0;

    OperationControl control_;
    std::thread worker_;
    std::condition_variable done_cv_;
};

// Tracks which operations hold which devices so incompatible ones cannot run
// together (spec §3.3): many shared readers, or exactly one exclusive holder.
class DeviceLeaseRegistry {
public:
    class Lease {
    public:
        Lease() = default;
        Lease(Lease&& o) noexcept;
        Lease& operator=(Lease&& o) noexcept;
        ~Lease();
        explicit operator bool() const noexcept { return reg_ != nullptr; }
        void release();

    private:
        friend class DeviceLeaseRegistry;
        Lease(DeviceLeaseRegistry* r, std::string id, AccessKind k) : reg_(r), id_(std::move(id)), kind_(k) {}
        DeviceLeaseRegistry* reg_ = nullptr;
        std::string id_;
        AccessKind kind_ = AccessKind::shared_read;
    };

    // Fails with Errc::device_busy if the request conflicts with current holders.
    Result<Lease> acquire(const std::string& device_id, AccessKind kind);

    std::size_t holders(const std::string& device_id) const;

    static DeviceLeaseRegistry& global();

private:
    void release(const std::string& id, AccessKind kind);
    struct Entry {
        int readers = 0;
        bool exclusive = false;
    };
    mutable std::mutex m_;
    std::unordered_map<std::string, Entry> entries_;
};

}  // namespace platter
