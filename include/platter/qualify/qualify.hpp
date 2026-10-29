// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Drive qualification (spec §12): a destructive burn-in that decides whether a
// disk is fit for use. It runs, over the confirmed range,
//
//   1..4 write passes   pass 1 pseudo-random data, pass 2 its bitwise
//                       complement (every cell is driven both ways), then a
//                       second random stream and its complement
//   read-back           after every pass (optional)
//   final read pass     a sequential read of everything
//   seek sampling       random small reads across the range
//
// measuring throughput per block and latency per request, then compares SMART
// before and after if the front end supplied snapshots. The verdict is
// deliberately conservative and every reason behind it is listed.
//
// Qualification is a statement about what was observed during one run. It
// does not predict failure and it is not a substitute for the manufacturer's
// own diagnostic.
#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platter/core/chacha20.hpp"
#include "platter/core/operation.hpp"
#include "platter/core/range.hpp"
#include "platter/core/safety.hpp"
#include "platter/smart/smart.hpp"

namespace platter::qualify {

// ------------------------------------------------------------------ SMART

// The handful of counters that matter for a burn-in. Absent = not reported.
struct SmartSnapshot {
    bool valid = false;
    std::optional<bool> overall_passed;
    std::optional<std::uint64_t> reallocated_sectors;      // ATA 5
    std::optional<std::uint64_t> reported_uncorrectable;   // ATA 187
    std::optional<std::uint64_t> pending_sectors;          // ATA 197
    std::optional<std::uint64_t> offline_uncorrectable;    // ATA 198
    std::optional<std::uint64_t> udma_crc_errors;          // ATA 199
    std::optional<std::uint64_t> nvme_media_errors;
    std::optional<std::uint32_t> nvme_critical_warning;
    std::optional<std::uint64_t> nvme_percentage_used;
    std::optional<int> temperature_c;
    std::optional<std::uint64_t> power_on_hours;
};
SmartSnapshot smart_snapshot(const smart::Report& r);

// ------------------------------------------------------------ configuration

struct QualifyConfig {
    std::uint32_t write_passes = 2;         // 1..4
    bool verify_each_pass = true;           // read every pass back and compare
    bool final_read = true;
    std::uint32_t seek_samples = 256;       // 0 = skip the seek stage
    std::uint32_t block_bytes = 1u << 20;
    std::uint64_t max_errors = 1024;        // failed sectors tolerated before aborting

    // A write / read block taking at least this long is "slow".
    std::uint32_t slow_block_ms = 200;
    // A random read taking at least this long is "slow".
    std::uint32_t seek_slow_ms = 100;
    // Fractions of slow blocks (per stage) that draw a warning / a failure.
    double slow_fraction_warn = 0.001;
    double slow_fraction_fail = 0.02;
    // Tail-of-range throughput below this fraction of the head draws a warning.
    double tail_head_warn = 0.25;
    std::uint64_t seed = 0x5EEDull;         // seek sample positions
};

constexpr std::uint32_t kMaxWritePasses = 4;

// ------------------------------------------------------------------ results

enum class StageKind : std::uint8_t { write, verify, read, seek };
std::string_view stage_kind_name(StageKind k) noexcept;

struct StageResult {
    StageKind kind = StageKind::write;
    std::uint32_t pass = 0;                 // 1-based write/verify pass, 0 otherwise
    std::string name;                       // "Pass 1 write (random)"
    bool completed = false;
    std::uint64_t bytes = 0;
    std::uint64_t blocks = 0;               // requests (seek: samples)
    std::uint64_t io_errors = 0;            // sectors that could not be written / read
    std::uint64_t mismatches = 0;           // sectors that read back different
    std::uint64_t slow_blocks = 0;
    double min_mbps = 0, avg_mbps = 0, max_mbps = 0;
    double head_mbps = 0, tail_mbps = 0;    // first / last 10 % of the blocks
    std::uint64_t median_us = 0, p95_us = 0, p99_us = 0, max_us = 0;
    std::uint64_t duration_ms = 0;
    std::optional<std::uint64_t> first_bad_offset;
};

enum class Verdict : std::uint8_t { pass, pass_with_warnings, fail, incomplete };
std::string_view verdict_name(Verdict v) noexcept;

struct Finding {
    enum class Severity : std::uint8_t { info, warning, failure } severity = Severity::info;
    std::string text;
};
std::string_view severity_name(Finding::Severity s) noexcept;

struct QualifyResult {
    QualifyConfig config;
    DeviceInfo device;
    std::vector<LbaRange> ranges;
    std::uint64_t affected_bytes = 0;
    bool whole_device = false;
    bool direct_io = false;

    std::vector<StageResult> stages;
    std::vector<LbaRange> bad_ranges;       // merged, first few hundred
    std::vector<std::string> warnings;      // from authorization / I/O set-up

    SmartSnapshot smart_before, smart_after;

    OperationState state = OperationState::created;
    Verdict verdict = Verdict::incomplete;
    std::vector<Finding> findings;          // every reason behind the verdict
    std::string failure;
    std::string started_utc, finished_utc;
    std::uint64_t duration_ms = 0;
};

// Pure function of the measurements: fills `verdict` and `findings`.
// Exposed so reports can be re-evaluated and the rules unit-tested.
void evaluate(QualifyResult& r);

// Findings from comparing two SMART snapshots (empty when either is invalid).
std::vector<Finding> compare_smart(const SmartSnapshot& before, const SmartSnapshot& after);

std::string result_to_text(const QualifyResult& r, bool include_serial = false);

// ---------------------------------------------------------------- operation

class Qualification final : public Operation {
public:
    Qualification(safety::AuthorizedTarget target, QualifyConfig config);
    ~Qualification() override;

    const QualifyConfig& config() const noexcept { return config_; }
    const DeviceInfo& device_info() const noexcept { return info_; }

    // Optional: the front end reads SMART (read-only) before starting and
    // after the operation ends. Both are folded into the verdict.
    void set_smart_before(SmartSnapshot s);
    void set_smart_after(SmartSnapshot s);

    // 1-based index of the running stage and the number of stages.
    std::pair<std::size_t, std::size_t> stage_position() const;
    QualifyResult result() const;

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    struct StagePlan {
        StageKind kind;
        std::uint32_t pass;
    };
    Status run_write(std::size_t stage, std::uint32_t pass, OperationControl& control);
    Status run_verify(std::size_t stage, std::uint32_t pass, OperationControl& control);
    Status run_read(std::size_t stage, OperationControl& control);
    Status run_seek(std::size_t stage, OperationControl& control);
    void fill(std::uint32_t pass, std::uint64_t offset, std::uint8_t* out, std::size_t len) const;
    void note_bad(std::uint64_t first_lba, std::uint64_t count);

    safety::AuthorizedTarget target_;
    DeviceInfo info_;
    QualifyConfig config_;
    std::vector<StagePlan> plan_;
    std::vector<ChaCha20::Key> keys_;
    std::vector<ChaCha20::Nonce> nonces_;

    mutable std::mutex rm_;
    QualifyResult r_;
    std::size_t current_stage_ = 0;
};

}  // namespace platter::qualify
