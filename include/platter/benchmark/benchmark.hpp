// SPDX-License-Identifier: TBD
//
// Read benchmarks (spec §7.1, §7.3, §7.4). Write benchmarks (§7.2) are not
// part of this module yet: they need the destructive-operation pipeline.
//
// Measurement model
// -----------------
// * Throughput = bytes completed / wall-clock time of the phase.
// * Latency    = duration of each individual read, timed around the OS call.
// * Queue depth N = N worker threads, each with its OWN device handle,
//   issuing synchronous reads (fio's psync+numjobs model). With direct I/O
//   this keeps N requests outstanding at the device. Separate handles matter:
//   Windows serializes I/O on one synchronous handle.
// * Benchmarks cannot be paused: a pause would distort the timing.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/core/histogram.hpp"
#include "platter/core/operation.hpp"

namespace platter::bench {

enum class BenchKind : std::uint8_t {
    sequential,    // sequential read throughput
    random,        // random read IOPS / latency
    access_time,   // random single-sector reads across the region, QD 1
    speed_curve,   // throughput sampled at evenly spaced positions (HD Tune style)
    block_sizes,   // sequential read at several block sizes
    queue_depths,  // random read at several queue depths
};
inline constexpr int kBenchKindCount = 6;
std::string_view bench_kind_name(BenchKind k) noexcept;   // "sequential", ...
std::string_view bench_kind_title(BenchKind k) noexcept;  // "Sequential read", ...

struct BenchConfig {
    BenchKind kind = BenchKind::sequential;
    std::uint64_t start_lba = 0;
    std::uint64_t end_lba = UINT64_MAX;  // inclusive; UINT64_MAX = last LBA
    std::uint32_t block_bytes = 0;       // 0 = default for the kind
    std::uint32_t queue_depth = 1;       // 1..64 (ignored by access_time and queue_depths)
    std::uint64_t max_bytes = 0;         // per phase; 0 = default for the kind
    double max_seconds = 0;              // per phase; 0 = default for the kind
    std::uint32_t curve_points = 100;
    std::uint64_t curve_bytes_per_point = 16ull << 20;
    std::vector<std::uint32_t> sweep_block_sizes = {4096, 16384, 65536, 262144, 1u << 20, 4u << 20};
    std::vector<std::uint32_t> sweep_queue_depths = {1, 2, 4, 8, 16, 32};
    std::uint64_t seed = 0xBE7C;
    bool direct_io = true;
};

// Fills zero fields with the defaults for the kind and sector size.
BenchConfig with_defaults(BenchConfig c, std::uint32_t sector_size);

struct Metrics {
    std::uint64_t ops = 0;
    std::uint64_t bytes = 0;
    std::uint64_t errors = 0;
    std::uint64_t wall_ns = 0;
    double mb_per_s = 0;   // 10^6 bytes per second
    double mib_per_s = 0;  // 2^20 bytes per second
    double iops = 0;
    double lat_mean_us = 0;
    std::uint64_t lat_min_us = 0, lat_median_us = 0, lat_p95_us = 0, lat_p99_us = 0, lat_max_us = 0;
};

Metrics compute_metrics(const LatencyHistogram& h, std::uint64_t ops, std::uint64_t bytes, std::uint64_t errors,
                        std::uint64_t wall_ns);

struct Phase {
    std::string label;              // "1 MiB", "QD 8", "Sequential"...
    std::uint32_t block_bytes = 0;
    std::uint32_t queue_depth = 1;
    Metrics metrics;
};

struct TimePoint {
    double t_s = 0;                 // seconds since benchmark start
    double bytes_per_sec = 0;
};

struct PositionPoint {
    std::uint64_t lba = 0;          // position of the measurement
    double bytes_per_sec = 0;
};

struct BenchResult {
    BenchConfig config;             // effective (defaults filled in)
    DeviceInfo device;
    std::string started_utc;        // ISO-8601
    OperationState state = OperationState::created;
    std::string message;
    std::vector<Phase> phases;
    std::vector<TimePoint> timeline;        // throughput vs time
    std::vector<PositionPoint> positions;   // throughput vs disk position
    std::vector<std::pair<std::uint64_t, std::uint64_t>> latency_buckets;  // all phases merged
    std::vector<std::string> warnings;      // cache / filesystem caveats (spec §7.4)

    // The headline number for the kind: MB/s, IOPS or access time.
    std::string headline() const;
};

using DeviceOpener = std::function<Result<BlockDevicePtr>()>;

class Benchmark final : public Operation {
public:
    // `opener` is called once per worker thread; every handle must be read-only.
    Benchmark(DeviceOpener opener, BenchConfig config, std::string device_id, bool is_image);
    ~Benchmark() override;

    bool supports_pause() const noexcept override { return false; }
    const BenchConfig& config() const noexcept { return cfg_; }

    // Safe to call while running: everything measured so far.
    BenchResult result() const;
    // 0..1, by phases completed plus progress within the current phase.
    double fraction() const;
    // Adds a caveat to the result (deduplicated).
    void note(const std::string& warning);

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    struct PhaseSpec {
        std::string label;
        std::uint32_t block_bytes;
        std::uint32_t queue_depth;
        bool random;
        std::uint64_t region_first_lba;
        std::uint64_t region_lbas;
        std::uint64_t max_bytes;   // 0 = unlimited
        double max_seconds;        // 0 = unlimited
        bool record_positions;
    };
    // Runs one measured phase. With `single_point` (speed curve) the result is
    // reported as a position sample instead of a Phase; `out` receives the
    // phase metrics either way.
    Status run_phase(const PhaseSpec& spec, OperationControl& control, PositionPoint* single_point, Metrics* out);

    DeviceOpener opener_;
    BenchConfig cfg_;
    bool is_image_;
    DeviceInfo info_;
    std::vector<PhaseSpec> plan_;
    std::uint64_t first_lba_ = 0, last_lba_ = 0;

    mutable std::mutex m_;
    BenchResult res_;
    LatencyHistogram all_;
    std::size_t phase_index_ = 0;
    double phase_fraction_ = 0;
    std::chrono::steady_clock::time_point t0_{};
};

// Opens `path` read-only (image or physical device) and builds a benchmark.
// Falls back to buffered I/O (with a warning) where direct I/O is refused.
Result<std::unique_ptr<Benchmark>> create_benchmark(const std::string& path, bool is_image, const BenchConfig& config,
                                                     std::uint32_t image_sector_size = 512);

}  // namespace platter::bench
