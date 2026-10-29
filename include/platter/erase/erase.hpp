// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Secure erase by overwrite (spec §9): zero / one / random / multi-pass /
// custom pattern / discard, with read-back verification and a report that
// states exactly what was written and what was verified.
//
// Software overwrite is honest about its limits: it cannot reach blocks the
// device has remapped or over-provisioned (flash, SMR zones, bad-sector
// reallocation). Reports say so; hardware sanitize commands are deliberately
// not part of this release (docs/safety.md).
#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platter/core/operation.hpp"
#include "platter/core/safety.hpp"

namespace platter::erase {

enum class Method : std::uint8_t {
    zero,        // 1 pass of 0x00
    one,         // 1 pass of 0xFF
    random,      // 1 pass of cryptographic-quality random data
    random_n,    // N passes of random data (EraseConfig::random_passes)
    dod_3pass,   // 0x00, 0xFF, random   (the "DoD short" pattern)
    dod_7pass,   // 0x00, 0xFF, random, random, 0x00, 0xFF, random
    pattern,     // repeat a custom byte pattern (EraseConfig::pattern)
    discard,     // TRIM / UNMAP (device decides whether data reads back as zeros)
};
inline constexpr int kMethodCount = 8;
std::string_view method_name(Method m) noexcept;
std::string_view method_label(Method m) noexcept;
std::optional<Method> parse_method(std::string_view s) noexcept;

struct Pass {
    enum class Kind : std::uint8_t { fill, pattern, random, discard };
    Kind kind = Kind::fill;
    std::uint8_t fill = 0;
    std::vector<std::uint8_t> pattern;
    std::string describe() const;
};

enum class VerifyMode : std::uint8_t { none, last_pass, every_pass };
std::string_view verify_mode_name(VerifyMode v) noexcept;
std::optional<VerifyMode> parse_verify_mode(std::string_view s) noexcept;

struct EraseConfig {
    Method method = Method::zero;
    std::uint32_t random_passes = 1;           // Method::random_n
    std::vector<std::uint8_t> pattern;         // Method::pattern (1..4096 bytes)
    VerifyMode verify = VerifyMode::last_pass;
    std::uint32_t block_bytes = 1u << 20;      // per request; multiple of the sector size
    std::uint64_t max_errors = 1024;           // failed sectors tolerated before aborting
};

std::vector<Pass> passes_for(const EraseConfig& c);

struct PassResult {
    std::string description;
    Pass::Kind kind = Pass::Kind::fill;
    std::uint64_t bytes_written = 0;
    std::uint64_t unwritable_sectors = 0;
    bool verified = false;                     // a verify read of this pass completed
    std::uint64_t verify_bytes = 0;
    std::uint64_t mismatched_sectors = 0;
    std::uint64_t unreadable_sectors = 0;
    std::optional<std::uint64_t> first_bad_offset;   // first mismatching / unreadable byte offset
    // discard passes: did the verify read see zeros everywhere? (informational)
    std::optional<bool> reads_back_zero;
    std::uint64_t duration_ms = 0;
};

enum class Verdict : std::uint8_t {
    erased_verified,     // every pass written, verification requested and clean
    erased_unverified,   // written without errors, no verification requested
    erased_with_errors,  // some sectors could not be written / verified
    incomplete,          // cancelled or failed before all passes ran
};
std::string_view verdict_name(Verdict v) noexcept;

struct EraseResult {
    EraseConfig config;
    DeviceInfo device;
    std::vector<LbaRange> ranges;
    std::uint64_t affected_bytes = 0;
    bool whole_device = false;
    std::vector<PassResult> passes;
    std::vector<LbaRange> unwritable;          // merged, first few hundred
    std::vector<LbaRange> bad_verify;          // merged, first few hundred
    std::vector<std::string> warnings;
    OperationState state = OperationState::created;
    Verdict verdict = Verdict::incomplete;
    std::string failure;                       // reason when state == failed
    std::string started_utc, finished_utc;
    std::uint64_t duration_ms = 0;
    bool direct_io = false;
};

class Erase final : public Operation {
public:
    Erase(safety::AuthorizedTarget target, EraseConfig config);
    ~Erase() override;

    const EraseConfig& config() const noexcept { return config_; }
    const DeviceInfo& device_info() const noexcept { return info_; }
    // Which pass is running (1-based) and how many there are in total.
    std::pair<std::size_t, std::size_t> pass_position() const;
    EraseResult result() const;

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    Status write_pass(std::size_t index, const Pass& pass, OperationControl& control);
    Status verify_pass(std::size_t index, const Pass& pass, OperationControl& control);
    void note_ranges(std::vector<LbaRange>& into, std::uint64_t first_lba, std::uint64_t count);

    safety::AuthorizedTarget target_;
    DeviceInfo info_;
    EraseConfig config_;
    std::vector<Pass> passes_;

    mutable std::mutex rm_;
    EraseResult r_;
    std::size_t current_pass_ = 0;
    std::vector<std::array<std::uint8_t, 32>> keys_;      // per random pass
    std::vector<std::array<std::uint8_t, 12>> nonces_;
};

}  // namespace platter::erase
