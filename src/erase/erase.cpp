// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/erase/erase.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "platter/core/aligned_buffer.hpp"
#include "platter/core/chacha20.hpp"
#include "platter/core/range.hpp"
#include "platter/core/time_util.hpp"

namespace platter::erase {

std::string_view method_name(Method m) noexcept {
    switch (m) {
        case Method::zero: return "zero";
        case Method::one: return "one";
        case Method::random: return "random";
        case Method::random_n: return "random-n";
        case Method::dod_3pass: return "dod3";
        case Method::dod_7pass: return "dod7";
        case Method::pattern: return "pattern";
        case Method::discard: return "discard";
    }
    return "?";
}

std::string_view method_label(Method m) noexcept {
    switch (m) {
        case Method::zero: return "Zero fill (1 pass)";
        case Method::one: return "One fill, 0xFF (1 pass)";
        case Method::random: return "Random data (1 pass)";
        case Method::random_n: return "Random data (N passes)";
        case Method::dod_3pass: return "3 passes: 0x00, 0xFF, random";
        case Method::dod_7pass: return "7 passes: 0x00, 0xFF, random, random, 0x00, 0xFF, random";
        case Method::pattern: return "Custom byte pattern";
        case Method::discard: return "Discard / TRIM (device dependent)";
    }
    return "?";
}

std::optional<Method> parse_method(std::string_view s) noexcept {
    for (int i = 0; i < kMethodCount; ++i)
        if (method_name(static_cast<Method>(i)) == s) return static_cast<Method>(i);
    if (s == "random_n") return Method::random_n;
    return std::nullopt;
}

std::string_view verify_mode_name(VerifyMode v) noexcept {
    switch (v) {
        case VerifyMode::none: return "none";
        case VerifyMode::last_pass: return "last";
        case VerifyMode::every_pass: return "every";
    }
    return "?";
}

std::optional<VerifyMode> parse_verify_mode(std::string_view s) noexcept {
    if (s == "none") return VerifyMode::none;
    if (s == "last" || s == "last_pass") return VerifyMode::last_pass;
    if (s == "every" || s == "every_pass" || s == "all") return VerifyMode::every_pass;
    return std::nullopt;
}

std::string_view verdict_name(Verdict v) noexcept {
    switch (v) {
        case Verdict::erased_verified: return "ERASED AND VERIFIED";
        case Verdict::erased_unverified: return "ERASED (NOT VERIFIED)";
        case Verdict::erased_with_errors: return "COMPLETED WITH ERRORS";
        case Verdict::incomplete: return "INCOMPLETE";
    }
    return "?";
}

std::string Pass::describe() const {
    char b[32];
    switch (kind) {
        case Kind::fill:
            std::snprintf(b, sizeof b, "fill 0x%02X", fill);
            return b;
        case Kind::pattern: {
            std::string s = "pattern";
            const std::size_t shown = std::min<std::size_t>(pattern.size(), 8);
            for (std::size_t i = 0; i < shown; ++i) {
                std::snprintf(b, sizeof b, " %02X", pattern[i]);
                s += b;
            }
            if (pattern.size() > shown) s += " ... (" + std::to_string(pattern.size()) + " bytes)";
            return s;
        }
        case Kind::random: return "random data";
        case Kind::discard: return "discard (TRIM)";
    }
    return "?";
}

std::vector<Pass> passes_for(const EraseConfig& c) {
    auto fill = [](std::uint8_t v) { return Pass{Pass::Kind::fill, v, {}}; };
    auto rnd = [] { return Pass{Pass::Kind::random, 0, {}}; };
    switch (c.method) {
        case Method::zero: return {fill(0x00)};
        case Method::one: return {fill(0xFF)};
        case Method::random: return {rnd()};
        case Method::random_n: {
            std::vector<Pass> v;
            for (std::uint32_t i = 0; i < c.random_passes; ++i) v.push_back(rnd());
            return v;
        }
        case Method::dod_3pass: return {fill(0x00), fill(0xFF), rnd()};
        case Method::dod_7pass: return {fill(0x00), fill(0xFF), rnd(), rnd(), fill(0x00), fill(0xFF), rnd()};
        case Method::pattern: return {Pass{Pass::Kind::pattern, 0, c.pattern}};
        case Method::discard: return {Pass{Pass::Kind::discard, 0, {}}};
    }
    return {};
}

namespace {

constexpr std::size_t kMaxListedRanges = 256;
constexpr std::uint32_t kMaxBlockBytes = 64u << 20;
constexpr std::uint64_t kDiscardChunk = 1ull << 30;

void fill_buffer(const Pass& pass, const ChaCha20* rng, std::uint64_t abs_offset, std::uint8_t* out,
                 std::size_t len) {
    switch (pass.kind) {
        case Pass::Kind::fill: std::memset(out, pass.fill, len); break;
        case Pass::Kind::pattern: {
            const std::size_t n = pass.pattern.size();
            std::size_t phase = static_cast<std::size_t>(abs_offset % n);
            for (std::size_t i = 0; i < len;) {
                const std::size_t take = std::min(n - phase, len - i);
                std::memcpy(out + i, pass.pattern.data() + phase, take);
                i += take;
                phase = 0;
            }
            break;
        }
        case Pass::Kind::random: rng->generate(abs_offset, out, len); break;
        case Pass::Kind::discard: std::memset(out, 0, len); break;
    }
}

bool is_fatal(Errc c) { return c == Errc::device_removed || c == Errc::not_found || c == Errc::refused; }

}  // namespace

Erase::Erase(safety::AuthorizedTarget target, EraseConfig config)
    : Operation("erase", target.device_id(), AccessKind::exclusive),
      target_(std::move(target)),
      info_(target_.info()),
      config_(std::move(config)) {}

Erase::~Erase() = default;

std::pair<std::size_t, std::size_t> Erase::pass_position() const {
    std::lock_guard lk(rm_);
    return {current_pass_, passes_.size()};
}

void Erase::note_ranges(std::vector<LbaRange>& into, std::uint64_t first_lba, std::uint64_t count) {
    std::lock_guard lk(rm_);
    if (!into.empty() && into.back().first + into.back().count == first_lba) {
        into.back().count += count;
    } else if (into.size() < kMaxListedRanges) {
        into.push_back(LbaRange{first_lba, count});
    }
}

Status Erase::do_validate() {
    if (target_.kind() != safety::Kind::erase)
        return Error{Errc::refused, "the authorization was not granted for an erase"};
    const auto ss = target_.sector_size();
    if (config_.block_bytes < ss || config_.block_bytes % ss != 0 || config_.block_bytes > kMaxBlockBytes)
        return Error{Errc::invalid_argument, "block size must be a multiple of the sector size, at most 64 MiB"};
    if (target_.direct_io() && config_.block_bytes % target_.required_alignment() != 0)
        return Error{Errc::misaligned, "block size must be a multiple of the I/O alignment"};
    if (config_.method == Method::pattern && (config_.pattern.empty() || config_.pattern.size() > 4096))
        return Error{Errc::invalid_argument, "the pattern must be 1 to 4096 bytes"};
    if (config_.method == Method::random_n && (config_.random_passes < 1 || config_.random_passes > 99))
        return Error{Errc::invalid_argument, "random passes must be between 1 and 99"};

    passes_ = passes_for(config_);
    if (passes_.empty()) return Error{Errc::invalid_argument, "no passes to run"};

    std::uint64_t bytes = target_.preview().affected_bytes;
    std::uint64_t total = 0;
    for (std::size_t i = 0; i < passes_.size(); ++i) {
        total += bytes;
        const bool verify = config_.verify == VerifyMode::every_pass ||
                            (config_.verify == VerifyMode::last_pass && i + 1 == passes_.size());
        if (verify) total += bytes;
    }
    set_total_bytes(total);

    // Random passes get independent keys; they are never written anywhere.
    keys_.clear();
    nonces_.clear();
    for (std::size_t i = 0; i < passes_.size(); ++i) {
        keys_.push_back(ChaCha20::random_key());
        nonces_.push_back(ChaCha20::random_nonce());
    }

    std::lock_guard lk(rm_);
    r_ = EraseResult{};
    r_.config = config_;
    r_.device = info_;
    r_.ranges = target_.ranges();
    r_.affected_bytes = bytes;
    r_.whole_device = target_.preview().whole_device;
    r_.direct_io = target_.direct_io();
    r_.warnings = target_.warnings();
    for (const auto& w : target_.preview().warnings) r_.warnings.push_back(w);
    for (const auto& p : passes_) {
        PassResult pr;
        pr.description = p.describe();
        pr.kind = p.kind;
        r_.passes.push_back(std::move(pr));
    }
    return {};
}

Status Erase::write_pass(std::size_t index, const Pass& pass, OperationControl& control) {
    const auto ss = target_.sector_size();
    const auto t0 = std::chrono::steady_clock::now();
    std::optional<ChaCha20> rng;
    if (pass.kind == Pass::Kind::random) rng.emplace(keys_[index], nonces_[index]);

    AlignedBuffer buf(config_.block_bytes, std::max<std::size_t>(4096, ss));
    std::uint64_t written = 0, bad_sectors = 0;

    for (const auto& r : target_.ranges()) {
        auto span = to_bytes(r, ss);
        if (!span) return span.error();
        const std::uint64_t end = span->offset + span->length;

        if (pass.kind == Pass::Kind::discard) {
            for (std::uint64_t off = span->offset; off < end;) {
                if (!control.checkpoint()) return Error{Errc::cancelled, "cancelled"};
                const std::uint64_t len = std::min<std::uint64_t>(kDiscardChunk, end - off);
                if (auto s = target_.discard(off, len); !s) {
                    return Error{s.error().code, "discard failed at byte offset " + std::to_string(off) + ": " +
                                                     s.error().describe()};
                }
                written += len;
                add_completed(len, off / ss);
                off += len;
            }
            continue;
        }

        for (std::uint64_t off = span->offset; off < end;) {
            if (!control.checkpoint()) return Error{Errc::cancelled, "cancelled"};
            const auto len = static_cast<std::size_t>(std::min<std::uint64_t>(config_.block_bytes, end - off));
            fill_buffer(pass, rng ? &*rng : nullptr, off, reinterpret_cast<std::uint8_t*>(buf.data()), len);
            auto w = target_.write(off, buf.data(), len);
            std::uint64_t failed_here = 0;
            if (!w) {
                if (is_fatal(w.error().code))
                    return Error{w.error().code, "write failed at byte offset " + std::to_string(off) + ": " +
                                                     w.error().describe()};
                // Isolate the damage: retry sector by sector.
                for (std::size_t s_off = 0; s_off < len; s_off += ss) {
                    auto ws = target_.write(off + s_off, buf.data() + s_off, ss);
                    if (ws) continue;
                    if (is_fatal(ws.error().code))
                        return Error{ws.error().code, "write failed at byte offset " + std::to_string(off + s_off) +
                                                          ": " + ws.error().describe()};
                    ++failed_here;
                    ++bad_sectors;
                    note_ranges(r_.unwritable, (off + s_off) / ss, 1);
                    add_error();
                    if (bad_sectors > config_.max_errors)
                        return Error{Errc::io_error, "aborted: more than " + std::to_string(config_.max_errors) +
                                                         " sectors could not be written"};
                }
            }
            written += len - failed_here * ss;
            add_completed(len, off / ss);
            off += len;
        }
    }
    if (auto f = target_.flush(); !f)
        return Error{f.error().code, "flush after pass " + std::to_string(index + 1) + " failed: " + f.error().describe()};

    std::lock_guard lk(rm_);
    auto& pr = r_.passes[index];
    pr.bytes_written = written;
    pr.unwritable_sectors = bad_sectors;
    pr.duration_ms += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    return {};
}

Status Erase::verify_pass(std::size_t index, const Pass& pass, OperationControl& control) {
    const auto ss = target_.sector_size();
    const auto t0 = std::chrono::steady_clock::now();
    std::optional<ChaCha20> rng;
    if (pass.kind == Pass::Kind::random) rng.emplace(keys_[index], nonces_[index]);

    AlignedBuffer got(config_.block_bytes, std::max<std::size_t>(4096, ss));
    AlignedBuffer want(config_.block_bytes, std::max<std::size_t>(4096, ss));
    std::uint64_t verified = 0, mismatched = 0, unreadable = 0;
    std::optional<std::uint64_t> first_bad;
    bool all_zero = true;

    auto bad_sector = [&](std::uint64_t byte_off, bool is_unreadable) {
        if (!first_bad) first_bad = byte_off;
        if (is_unreadable) ++unreadable;
        else ++mismatched;
        note_ranges(r_.bad_verify, byte_off / ss, 1);
        add_error();
    };

    for (const auto& r : target_.ranges()) {
        auto span = to_bytes(r, ss);
        if (!span) return span.error();
        const std::uint64_t end = span->offset + span->length;
        for (std::uint64_t off = span->offset; off < end;) {
            if (!control.checkpoint()) return Error{Errc::cancelled, "cancelled"};
            const auto len = static_cast<std::size_t>(std::min<std::uint64_t>(config_.block_bytes, end - off));
            const bool discard = pass.kind == Pass::Kind::discard;
            if (!discard) fill_buffer(pass, rng ? &*rng : nullptr, off, reinterpret_cast<std::uint8_t*>(want.data()), len);

            auto check_range = [&](std::uint64_t base_off, std::size_t base_idx, std::size_t n) {
                if (discard) {
                    for (std::size_t i = 0; i < n; ++i)
                        if (got.data()[base_idx + i] != std::byte{0}) { all_zero = false; break; }
                    return;
                }
                if (std::memcmp(got.data() + base_idx, want.data() + base_idx, n) == 0) return;
                for (std::size_t s = 0; s < n; s += ss)
                    if (std::memcmp(got.data() + base_idx + s, want.data() + base_idx + s, ss) != 0)
                        bad_sector(base_off + s, false);
            };

            auto rd = target_.read(off, got.data(), len);
            if (rd) {
                check_range(off, 0, len);
            } else {
                if (is_fatal(rd.error().code))
                    return Error{rd.error().code, "verify read failed at byte offset " + std::to_string(off) + ": " +
                                                      rd.error().describe()};
                for (std::size_t s = 0; s < len; s += ss) {
                    auto rs = target_.read(off + s, got.data() + s, ss);
                    if (rs) {
                        check_range(off + s, s, ss);
                    } else if (is_fatal(rs.error().code)) {
                        return Error{rs.error().code, "verify read failed at byte offset " + std::to_string(off + s) +
                                                          ": " + rs.error().describe()};
                    } else {
                        bad_sector(off + s, true);
                        if (mismatched + unreadable > config_.max_errors)
                            return Error{Errc::io_error, "aborted verification: too many unreadable or mismatching sectors"};
                    }
                }
            }
            if (mismatched + unreadable > config_.max_errors)
                return Error{Errc::io_error, "aborted verification: too many unreadable or mismatching sectors"};
            verified += len;
            add_completed(len, off / ss);
            off += len;
        }
    }

    std::lock_guard lk(rm_);
    auto& pr = r_.passes[index];
    pr.verified = true;
    pr.verify_bytes = verified;
    pr.mismatched_sectors = mismatched;
    pr.unreadable_sectors = unreadable;
    pr.first_bad_offset = first_bad;
    if (pass.kind == Pass::Kind::discard) pr.reads_back_zero = all_zero;
    pr.duration_ms += static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    return {};
}

Status Erase::do_run(OperationControl& control) {
    const auto t_start = std::chrono::steady_clock::now();
    {
        std::lock_guard lk(rm_);
        r_.started_utc = utc_now();
    }
    auto finish = [&] {
        std::lock_guard lk(rm_);
        r_.finished_utc = utc_now();
        r_.duration_ms = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t_start).count());
    };

    for (std::size_t i = 0; i < passes_.size(); ++i) {
        {
            std::lock_guard lk(rm_);
            current_pass_ = i + 1;
        }
        set_message("Pass " + std::to_string(i + 1) + " of " + std::to_string(passes_.size()) + ": writing " +
                    passes_[i].describe());
        if (auto s = write_pass(i, passes_[i], control); !s) {
            finish();
            return s;
        }
        const bool verify = config_.verify == VerifyMode::every_pass ||
                            (config_.verify == VerifyMode::last_pass && i + 1 == passes_.size());
        if (verify) {
            set_message("Pass " + std::to_string(i + 1) + " of " + std::to_string(passes_.size()) + ": verifying");
            if (auto s = verify_pass(i, passes_[i], control); !s) {
                finish();
                return s;
            }
        }
    }
    finish();

    // Any sector we could not write or read back makes the whole run a failure
    // as far as the exit status is concerned; the report holds the details.
    std::lock_guard lk(rm_);
    for (const auto& p : r_.passes)
        if (p.unwritable_sectors || p.mismatched_sectors || p.unreadable_sectors)
            return Error{Errc::io_error, "completed with errors: some sectors could not be written or did not verify"};
    return {};
}

EraseResult Erase::result() const {
    EraseResult out;
    {
        std::lock_guard lk(rm_);
        out = r_;
    }
    const auto p = progress();
    out.state = p.state;
    if (p.state == OperationState::failed) out.failure = p.message;
    if (out.finished_utc.empty() && is_terminal(p.state)) out.finished_utc = utc_now();

    bool any_bad = false, any_unrun = false;
    for (const auto& ps : out.passes) {
        if (ps.unwritable_sectors || ps.mismatched_sectors || ps.unreadable_sectors) any_bad = true;
        if (ps.bytes_written == 0 && out.affected_bytes > 0) any_unrun = true;
        if (ps.reads_back_zero && !*ps.reads_back_zero) any_bad = true;
    }
    const bool finished_clean = p.state == OperationState::completed;
    if (finished_clean && !any_bad && !any_unrun) {
        const bool last_verified = !out.passes.empty() && out.passes.back().verified;
        out.verdict = last_verified ? Verdict::erased_verified : Verdict::erased_unverified;
    } else if (any_bad && (finished_clean || p.state == OperationState::failed) && !any_unrun) {
        out.verdict = Verdict::erased_with_errors;
    } else {
        out.verdict = Verdict::incomplete;
    }
    return out;
}

}  // namespace platter::erase
