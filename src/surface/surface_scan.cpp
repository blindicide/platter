// SPDX-License-Identifier: TBD
#include "platter/surface/surface_scan.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>

#include "platter/core/range.hpp"
#include "platter/core/rng.hpp"

namespace platter::surface {

std::string_view scan_mode_name(ScanMode m) noexcept {
    switch (m) {
        case ScanMode::read: return "READ";
        case ScanMode::verify: return "VERIFY";
        case ScanMode::write: return "WRITE";
        case ScanMode::write_readback: return "WRITE+READBACK";
        case ScanMode::custom_pattern: return "CUSTOM PATTERN";
    }
    return "?";
}

std::string_view scan_order_name(ScanOrder o) noexcept {
    switch (o) {
        case ScanOrder::forward: return "forward";
        case ScanOrder::reverse: return "reverse";
        case ScanOrder::random: return "random";
        case ScanOrder::sampled: return "sampled";
    }
    return "?";
}

std::string_view latency_class_name(LatencyClass c) noexcept {
    switch (c) {
        case LatencyClass::untested: return "untested";
        case LatencyClass::fast: return "fast";
        case LatencyClass::good: return "good";
        case LatencyClass::medium: return "medium";
        case LatencyClass::slow: return "slow";
        case LatencyClass::very_slow: return "very_slow";
        case LatencyClass::error: return "error";
    }
    return "?";
}

LatencyClass classify(std::uint64_t duration_ns, const Thresholds& t) noexcept {
    const std::uint64_t us = duration_ns / 1000;
    if (us < t.us[0]) return LatencyClass::fast;
    if (us < t.us[1]) return LatencyClass::good;
    if (us < t.us[2]) return LatencyClass::medium;
    if (us < t.us[3]) return LatencyClass::slow;
    return LatencyClass::very_slow;
}

// ------------------------------------------------------------ order

std::vector<std::uint64_t> make_order(std::uint64_t n, ScanOrder order, double sample_percent, std::uint64_t seed) {
    std::vector<std::uint64_t> v(static_cast<std::size_t>(n));
    std::iota(v.begin(), v.end(), std::uint64_t{0});
    SplitMix64 rng{seed};
    switch (order) {
        case ScanOrder::forward: break;
        case ScanOrder::reverse: std::reverse(v.begin(), v.end()); break;
        case ScanOrder::random:
            for (std::uint64_t i = n; i > 1; --i) std::swap(v[i - 1], v[rng.below(i)]);
            break;
        case ScanOrder::sampled: {
            const double pct = std::clamp(sample_percent, 0.0, 100.0);
            auto k = static_cast<std::uint64_t>(std::llround(static_cast<double>(n) * pct / 100.0));
            k = std::clamp<std::uint64_t>(k, n ? 1 : 0, n);
            // Partial Fisher-Yates: the first k slots become a uniform sample.
            for (std::uint64_t i = 0; i < k; ++i) std::swap(v[i], v[i + rng.below(n - i)]);
            v.resize(static_cast<std::size_t>(k));
            std::sort(v.begin(), v.end());  // visit in ascending order: kinder to HDD heads
            break;
        }
    }
    return v;
}

// ------------------------------------------------------------ scan

namespace {
constexpr std::uint64_t kMaxBlocks = 256ull * 1024 * 1024;  // ~3 GiB of state: refuse beyond this
constexpr std::uint32_t kMaxBlockBytes = 64u << 20;

struct AlignedBuffer {
    std::unique_ptr<std::byte[]> raw;
    std::byte* data = nullptr;
    AlignedBuffer(std::size_t size, std::size_t align) : raw(new std::byte[size + align]) {
        void* p = raw.get();
        std::size_t space = size + align;
        data = static_cast<std::byte*>(std::align(align, size, p, space));
    }
};
}  // namespace

SurfaceScan::SurfaceScan(BlockDevicePtr device, ScanConfig config)
    : Operation("surface-scan", device ? device->info().path : std::string{}, AccessKind::shared_read),
      dev_(std::move(device)),
      config_(config) {
    if (dev_) info_ = dev_->info();
}

SurfaceScan::SurfaceScan(safety::AuthorizedTarget target, ScanConfig config)
    : Operation("surface-scan", target.device_id(), AccessKind::exclusive),
      auth_(std::make_unique<safety::AuthorizedTarget>(std::move(target))),
      info_(auth_->info()),
      config_(std::move(config)) {
    for (const auto& w : auth_->warnings()) warnings_.push_back(w);
}

SurfaceScan::~SurfaceScan() {
    cancel();
    wait();
}

Status SurfaceScan::do_validate() {
    if (!dev_ && !auth_) return Error{Errc::invalid_argument, "no device"};
    if (config_.mode == ScanMode::verify)
        return Error{Errc::unsupported, "VERIFY scans need a device-side verify command, which this build does not issue"};
    if (is_write_mode(config_.mode)) {
        if (!auth_) return Error{Errc::refused, std::string(scan_mode_name(config_.mode)) +
                                                   " scans destroy data and need a confirmed authorization"};
        if (auth_->kind() != safety::Kind::write_test)
            return Error{Errc::refused, "the authorization was not granted for a write test"};
        if (config_.pattern.size() > 4096) return Error{Errc::invalid_argument, "the pattern must be at most 4096 bytes"};
        if (config_.mode == ScanMode::custom_pattern && config_.pattern.empty())
            return Error{Errc::invalid_argument, "custom pattern mode needs a pattern"};
        if (auth_->direct_io() && config_.block_bytes % auth_->required_alignment() != 0)
            return Error{Errc::misaligned, "block size must be a multiple of the I/O alignment"};
    } else {
        if (auth_) return Error{Errc::invalid_argument, "READ scans never use a writable authorization"};
        // A surface scan must never hold a writable handle (spec §6.1, §17).
        if (dev_->mode() != OpenMode::read_only)
            return Error{Errc::invalid_argument, "surface scan requires a read-only device handle"};
    }

    const auto ss = info_.logical_sector_size;
    const auto total = info_.total_lbas();
    if (ss == 0 || total == 0) return Error{Errc::invalid_argument, "device reports zero capacity"};
    if (config_.block_bytes < ss || config_.block_bytes % ss != 0 || config_.block_bytes > kMaxBlockBytes)
        return Error{Errc::invalid_argument, "block size must be a multiple of the " + std::to_string(ss) +
                                                 "-byte sector size, up to 64 MiB"};
    if (!config_.thresholds.valid()) return Error{Errc::invalid_argument, "latency thresholds must be increasing"};
    if (config_.order == ScanOrder::sampled && !(config_.sample_percent > 0 && config_.sample_percent <= 100))
        return Error{Errc::invalid_argument, "sample percentage must be in (0, 100]"};
    if (config_.error_policy == ErrorPolicy::stop_after_n && config_.max_errors == 0)
        return Error{Errc::invalid_argument, "max_errors must be at least 1 with the stop-after-N policy"};
    if (config_.retries > 16) return Error{Errc::invalid_argument, "at most 16 retries"};

    const std::uint64_t end = config_.end_lba == UINT64_MAX ? total - 1 : config_.end_lba;
    auto range = lba_range_inclusive(config_.start_lba, end);
    if (!range) return range.error();
    if (auto s = validate_lba_range(*range, total); !s) return s;

    layout_.start_lba = config_.start_lba;
    layout_.end_lba = end;
    layout_.sector_size = ss;
    layout_.lbas_per_block = config_.block_bytes / ss;
    layout_.block_count = (range->count + layout_.lbas_per_block - 1) / layout_.lbas_per_block;
    if (layout_.block_count > kMaxBlocks)
        return Error{Errc::invalid_argument, "range has " + std::to_string(layout_.block_count) +
                                                 " blocks; use a larger block size"};

    results_.assign(static_cast<std::size_t>(layout_.block_count), BlockResult{});
    classes_.assign(static_cast<std::size_t>(layout_.block_count), 0);
    stats_ = {};
    stats_.device_bytes = info_.capacity_bytes;
    order_ = make_order(layout_.block_count, config_.order, config_.sample_percent, config_.seed);
    std::uint64_t bytes = 0;
    for (auto i : order_) {
        bytes += layout_.lba_count(i) * ss;
        if (auth_ && !auth_->allows(layout_.first_lba(i) * ss, layout_.lba_count(i) * ss))
            return Error{Errc::refused, "the scan range extends outside the range that was confirmed"};
    }
    stats_.total_bytes = bytes;
    set_total_bytes(bytes);
    return {};
}

void SurfaceScan::record(std::uint64_t index, const BlockResult& r, std::uint64_t bytes, std::uint64_t latency_us) {
    std::lock_guard lk(rm_);
    results_[static_cast<std::size_t>(index)] = r;
    classes_[static_cast<std::size_t>(index)] = static_cast<std::uint8_t>(r.cls);
    ++stats_.requests;
    stats_.retries += r.retries;
    ++stats_.per_class[static_cast<std::size_t>(r.cls)];
    if (r.cls == LatencyClass::error) {
        ++stats_.failed;
    } else {
        ++stats_.ok;
        stats_.tested_bytes += bytes;
        hist_.add(latency_us);
        if (latency_us >= config_.thresholds.slow_us()) ++stats_.slow;
    }
}

Status SurfaceScan::do_run(OperationControl& control) {
    // An authorized target already owns the exclusive lease.
    DeviceLeaseRegistry::Lease lease;
    if (!auth_) {
        auto l = DeviceLeaseRegistry::global().acquire(device_id(), access());
        if (!l) return l.error();
        lease = std::move(l).value();
    }
    const bool writing = is_write_mode(config_.mode);
    const bool readback = reads_back(config_.mode);
    std::vector<std::uint8_t> pattern = config_.pattern;
    if (pattern.empty()) pattern = {0xA5, 0x5A};
    AlignedBuffer rbuf(readback ? config_.block_bytes : 1, std::max<std::size_t>(4096, info_.logical_sector_size));
    if (writing && !auth_->direct_io() && readback)
        note_warning("direct I/O is not in use; read-back may be served from the OS cache and hide bad sectors");

    const std::size_t align = std::max<std::size_t>(4096, info_.logical_sector_size);
    AlignedBuffer buf(config_.block_bytes, align);
    const auto t_start = std::chrono::steady_clock::now();
    std::uint64_t failed = 0;

    for (const auto idx : order_) {
        if (!control.checkpoint()) return Error{Errc::cancelled, "cancelled"};

        const std::uint64_t first = layout_.first_lba(idx);
        const std::uint64_t count = layout_.lba_count(idx);
        const std::uint64_t offset = first * layout_.sector_size;
        const auto len = static_cast<std::size_t>(count * layout_.sector_size);

        BlockResult br;
        std::uint64_t dur_ns = 0;
        Errc last = Errc::ok;
        if (writing) {
            auto* out = reinterpret_cast<std::uint8_t*>(buf.data);
            const std::size_t plen = pattern.size();
            for (std::size_t i = 0; i < len; ++i) out[i] = pattern[(offset + i) % plen];
        }
        for (std::uint32_t attempt = 0; attempt <= config_.retries; ++attempt) {
            br.retries = static_cast<std::uint8_t>(attempt);  // attempts beyond the first, success or not
            const auto t0 = std::chrono::steady_clock::now();
            auto r = writing ? auth_->write(offset, buf.data, len) : dev_->read(offset, buf.data, len);
            if (r && readback) {
                // Write succeeded; read it back and compare.
                const auto dur_w = r->duration_ns;
                auto rr = auth_->read(offset, rbuf.data, len);
                if (rr) {
                    if (std::memcmp(buf.data, rbuf.data, len) != 0) {
                        dur_ns = dur_w + rr->duration_ns;
                        last = Errc::corrupt_data;
                        continue;  // retry the write+read pair
                    }
                    r = IOResult{r->bytes, dur_w + rr->duration_ns};
                } else {
                    r = rr.error();
                }
            }
            if (r) {
                dur_ns = r->duration_ns;
                last = Errc::ok;
                break;
            }
            dur_ns = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
            last = r.error().code;
            // A vanished device is fatal: never keep going against whatever
            // might appear under the same path.
            if (last == Errc::device_removed || last == Errc::not_found)
                return Error{last, "device disappeared at LBA " + std::to_string(first) + ": " +
                                       r.error().describe()};
            if (control.cancel_requested()) break;
        }

        const std::uint64_t us = dur_ns / 1000;
        br.duration_us = static_cast<std::uint32_t>(std::min<std::uint64_t>(us, UINT32_MAX));
        br.time_ms = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                    std::chrono::steady_clock::now() - t_start)
                                                    .count());
        br.errc = static_cast<std::uint16_t>(last);
        br.cls = last == Errc::ok ? classify(dur_ns, config_.thresholds) : LatencyClass::error;
        record(idx, br, len, us);
        add_completed(len, first + count - 1);
        if (last != Errc::ok) {
            add_error();
            ++failed;
            if (config_.error_policy == ErrorPolicy::stop_after_n && failed >= config_.max_errors)
                return Error{Errc::io_error, "stopped after " + std::to_string(failed) + " failed block(s)"};
        }
    }
    if (auth_) {
        if (auto f = auth_->flush(); !f) return f.error();
    }
    return {};
}

void SurfaceScan::copy_classes(std::vector<std::uint8_t>& out) const {
    std::lock_guard lk(rm_);
    out = classes_;
}

BlockResult SurfaceScan::block(std::uint64_t index) const {
    std::lock_guard lk(rm_);
    return index < results_.size() ? results_[static_cast<std::size_t>(index)] : BlockResult{};
}

ScanStats SurfaceScan::stats() const {
    ScanStats s;
    {
        std::lock_guard lk(rm_);
        s = stats_;
        s.min_us = hist_.min();
        s.max_us = hist_.max();
        s.mean_us = hist_.mean();
        s.median_us = hist_.percentile(50);
        s.p95_us = hist_.percentile(95);
        s.p99_us = hist_.percentile(99);
    }
    s.average_bytes_per_sec = progress().average_bytes_per_sec;
    return s;
}

std::vector<std::string> SurfaceScan::warnings() const {
    std::lock_guard lk(rm_);
    return warnings_;
}

void SurfaceScan::note_warning(std::string text) {
    {
        std::lock_guard lk(rm_);
        warnings_.push_back(text);
    }
    add_warning();
    set_message(std::move(text));
}

ScanSnapshot SurfaceScan::snapshot() const {
    ScanSnapshot snap;
    snap.config = config_;
    snap.layout = layout_;
    snap.device = info_;
    snap.stats = stats();
    snap.progress = progress();
    std::lock_guard lk(rm_);
    snap.blocks = results_;
    snap.warnings = warnings_;
    return snap;
}

Result<std::unique_ptr<SurfaceScan>> create_scan(const std::string& path, bool is_image, const ScanConfig& config,
                                                 std::uint32_t image_sector_size) {
    OpenOptions oo;
    oo.mode = OpenMode::read_only;  // never anything else for a surface scan
    oo.direct_io = config.direct_io;
    auto open = [&]() { return is_image ? open_image(path, oo, image_sector_size) : open_device(path, oo); };
    auto dev = open();
    bool fell_back = false;
    if (!dev && oo.direct_io && dev.error().code == Errc::invalid_argument) {
        // Some filesystems (tmpfs, some FUSE) refuse O_DIRECT; retry buffered.
        oo.direct_io = false;
        dev = open();
        fell_back = true;
    }
    if (!dev) return dev.error();
    ScanConfig effective = config;
    effective.direct_io = oo.direct_io;
    auto scan = std::make_unique<SurfaceScan>(std::move(dev).value(), effective);
    if (fell_back) scan->note_warning("direct I/O is not supported here; using buffered I/O (results may reflect the OS cache)");
    return scan;
}

}  // namespace platter::surface
