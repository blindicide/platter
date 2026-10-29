// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Block-level surface scanning (spec §6). v0.2 implements READ only; VERIFY
// and the destructive WRITE modes are rejected at validation time.
//
// Colour classes describe observed I/O latency, not physical sector health
// (spec §6.3). Every block corresponds to an exact, documented LBA range.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/core/histogram.hpp"
#include "platter/core/operation.hpp"
#include "platter/core/safety.hpp"

namespace platter::surface {

enum class ScanMode : std::uint8_t { read, verify, write, write_readback, custom_pattern };
std::string_view scan_mode_name(ScanMode m) noexcept;

enum class ScanOrder : std::uint8_t { forward, reverse, random, sampled };
std::string_view scan_order_name(ScanOrder o) noexcept;

// Latency classes, in map order. Values are stored per block, so keep < 256.
enum class LatencyClass : std::uint8_t {
    untested = 0,
    fast,      // green        < t0
    good,      // light green  < t1
    medium,    // yellow       < t2
    slow,      // orange       < t3
    very_slow, // red          >= t3
    error,     // dark red     I/O error
};
inline constexpr int kLatencyClassCount = 7;
std::string_view latency_class_name(LatencyClass c) noexcept;

struct Thresholds {
    // Upper bounds in microseconds for fast / good / medium / slow.
    std::array<std::uint32_t, 4> us = {5'000, 20'000, 50'000, 200'000};
    // Requests at or above this are counted as "slow" in the statistics.
    std::uint32_t slow_us() const noexcept { return us[3]; }
    bool valid() const noexcept { return us[0] < us[1] && us[1] < us[2] && us[2] < us[3]; }
};

LatencyClass classify(std::uint64_t duration_ns, const Thresholds& t) noexcept;

enum class ErrorPolicy : std::uint8_t {
    continue_scan,   // record and move on
    stop_after_n,    // stop after max_errors failed blocks
};

struct ScanConfig {
    ScanMode mode = ScanMode::read;
    std::uint64_t start_lba = 0;
    // Inclusive. UINT64_MAX means "last LBA of the device".
    std::uint64_t end_lba = UINT64_MAX;
    std::uint32_t block_bytes = 1u << 20;  // transfer size per request
    ScanOrder order = ScanOrder::forward;
    double sample_percent = 10.0;          // for ScanOrder::sampled
    std::uint64_t seed = 0x5EED;           // random / sampled order
    std::uint32_t retries = 0;             // extra attempts after a failed read
    bool direct_io = true;                 // bypass the OS cache when possible
    ErrorPolicy error_policy = ErrorPolicy::continue_scan;
    std::uint64_t max_errors = 0;
    Thresholds thresholds;
    // Write modes only: the repeating byte pattern (1 to 4096 bytes), phase
    // locked to the absolute byte offset. Empty = 0xA5 0x5A, except for
    // custom_pattern where it is required.
    std::vector<std::uint8_t> pattern;
};

constexpr bool is_write_mode(ScanMode m) noexcept {
    return m == ScanMode::write || m == ScanMode::write_readback || m == ScanMode::custom_pattern;
}
constexpr bool reads_back(ScanMode m) noexcept { return m == ScanMode::write_readback || m == ScanMode::custom_pattern; }

// One scanned block. Packed to keep a 1 TB / 1 MiB scan (~1M blocks) small.
struct BlockResult {
    std::uint32_t duration_us = 0;      // last attempt; saturates at UINT32_MAX
    std::uint32_t time_ms = 0;          // completion time since scan start
    std::uint16_t errc = 0;             // platter::Errc of the final attempt
    std::uint8_t retries = 0;           // attempts beyond the first
    LatencyClass cls = LatencyClass::untested;
};

// Shared with benchmarks; see platter/core/histogram.hpp.
using LatencyHistogram = platter::LatencyHistogram;

struct ScanStats {
    std::uint64_t total_bytes = 0;       // bytes in the configured range (or sample)
    std::uint64_t tested_bytes = 0;
    std::uint64_t device_bytes = 0;
    std::uint64_t requests = 0;          // completed blocks
    std::uint64_t ok = 0;
    std::uint64_t failed = 0;
    std::uint64_t slow = 0;              // >= thresholds.slow_us()
    std::uint64_t retries = 0;
    std::array<std::uint64_t, kLatencyClassCount> per_class{};
    std::uint64_t min_us = 0, max_us = 0, median_us = 0, p95_us = 0, p99_us = 0;
    double mean_us = 0;
    double average_bytes_per_sec = 0;
};

// The scan geometry: block i covers LBAs [first_lba(i), first_lba(i)+lba_count(i)).
struct ScanLayout {
    std::uint64_t start_lba = 0;
    std::uint64_t end_lba = 0;           // inclusive
    std::uint32_t sector_size = 512;
    std::uint64_t lbas_per_block = 0;
    std::uint64_t block_count = 0;

    std::uint64_t first_lba(std::uint64_t i) const noexcept { return start_lba + i * lbas_per_block; }
    std::uint64_t lba_count(std::uint64_t i) const noexcept {
        const auto first = first_lba(i);
        return std::min(lbas_per_block, end_lba - first + 1);
    }
    std::uint64_t block_of_lba(std::uint64_t lba) const noexcept { return (lba - start_lba) / lbas_per_block; }
};

// Builds the visiting order of block indices. Forward / reverse / random visit
// every block exactly once; sampled visits round(count * percent / 100) blocks
// (at least one), spread uniformly at random.
std::vector<std::uint64_t> make_order(std::uint64_t block_count, ScanOrder order, double sample_percent,
                                      std::uint64_t seed);

// Snapshot of everything a front end or exporter needs.
struct ScanSnapshot {
    ScanConfig config;
    ScanLayout layout;
    DeviceInfo device;
    std::vector<BlockResult> blocks;
    ScanStats stats;
    OperationProgress progress;
    std::vector<std::string> warnings;   // e.g. "direct I/O unavailable, buffered"
};

class SurfaceScan final : public Operation {
public:
    // Takes ownership of an already-opened, READ-ONLY device (READ mode only).
    SurfaceScan(BlockDevicePtr device, ScanConfig config);
    // Destructive write modes: runs only through a confirmed authorization
    // (kind write_test) and only inside the confirmed ranges.
    SurfaceScan(safety::AuthorizedTarget target, ScanConfig config);
    ~SurfaceScan() override;

    const ScanConfig& config() const noexcept { return config_; }
    const ScanLayout& layout() const noexcept { return layout_; }
    const DeviceInfo& device_info() const noexcept { return info_; }

    // Cheap: 1 byte per block. For map rendering at GUI refresh rate.
    void copy_classes(std::vector<std::uint8_t>& out) const;
    BlockResult block(std::uint64_t index) const;
    ScanStats stats() const;
    ScanSnapshot snapshot() const;
    std::vector<std::string> warnings() const;

    // Records a non-fatal condition (shown in reports and the GUI log).
    void note_warning(std::string text);

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    void record(std::uint64_t index, const BlockResult& r, std::uint64_t bytes, std::uint64_t latency_us);

    BlockDevicePtr dev_;
    std::unique_ptr<safety::AuthorizedTarget> auth_;
    DeviceInfo info_;
    ScanConfig config_;
    ScanLayout layout_;

    mutable std::mutex rm_;
    std::vector<std::uint64_t> order_;
    std::vector<BlockResult> results_;
    std::vector<std::uint8_t> classes_;
    std::vector<std::string> warnings_;
    LatencyHistogram hist_;
    ScanStats stats_;
};

// Opens `path` read-only (image or physical device) and builds a scan.
Result<std::unique_ptr<SurfaceScan>> create_scan(const std::string& path, bool is_image, const ScanConfig& config,
                                                 std::uint32_t image_sector_size = 512);

}  // namespace platter::surface
