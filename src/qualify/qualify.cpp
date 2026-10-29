// SPDX-License-Identifier: TBD
#include "platter/qualify/qualify.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>

#include "platter/core/aligned_buffer.hpp"
#include "platter/core/histogram.hpp"
#include "platter/core/rng.hpp"
#include "platter/core/time_util.hpp"

namespace platter::qualify {

namespace {

constexpr std::size_t kMaxListedRanges = 256;
constexpr std::uint32_t kMaxBlockBytes = 64u << 20;
constexpr std::uint32_t kSeekBytes = 4096;

bool is_fatal(Errc c) { return c == Errc::device_removed || c == Errc::not_found || c == Errc::refused; }

std::uint64_t ns_since(std::chrono::steady_clock::time_point t0) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
}

std::uint64_t ms_since(std::chrono::steady_clock::time_point t0) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
}

// Collects per-request timings for one stage.
class Recorder {
public:
    explicit Recorder(std::uint64_t slow_us) : slow_us_(slow_us) {}

    void add(std::uint64_t ns, std::uint64_t bytes) {
        const std::uint64_t us = std::max<std::uint64_t>(1, ns / 1000);
        hist_.add(us);
        if (us >= slow_us_) ++slow_;
        blocks_.push_back({static_cast<std::uint32_t>(std::min<std::uint64_t>(us, UINT32_MAX)),
                           static_cast<std::uint32_t>(std::min<std::uint64_t>(bytes, UINT32_MAX))});
        bytes_ += bytes;
    }

    void fill(StageResult& s, bool with_throughput) const {
        s.blocks = blocks_.size();
        s.slow_blocks = slow_;
        if (hist_.count()) {
            s.median_us = hist_.percentile(50);
            s.p95_us = hist_.percentile(95);
            s.p99_us = hist_.percentile(99);
            s.max_us = hist_.max();
        }
        if (!with_throughput || blocks_.empty()) return;
        auto mbps = [](std::uint64_t bytes, std::uint64_t us) {
            return us ? static_cast<double>(bytes) / static_cast<double>(us) : 0.0;  // bytes/us == MB/s
        };
        double mn = 1e300, mx = 0;
        std::uint64_t total_us = 0;
        for (const auto& b : blocks_) {
            const double v = mbps(b.bytes, b.us);
            mn = std::min(mn, v);
            mx = std::max(mx, v);
            total_us += b.us;
        }
        s.min_mbps = mn;
        s.max_mbps = mx;
        s.avg_mbps = mbps(bytes_, total_us);
        const std::size_t edge = std::max<std::size_t>(1, blocks_.size() / 10);
        std::uint64_t hb = 0, hu = 0, tb = 0, tu = 0;
        for (std::size_t i = 0; i < edge; ++i) {
            hb += blocks_[i].bytes;
            hu += blocks_[i].us;
            const auto& t = blocks_[blocks_.size() - 1 - i];
            tb += t.bytes;
            tu += t.us;
        }
        s.head_mbps = mbps(hb, hu);
        s.tail_mbps = mbps(tb, tu);
    }

private:
    struct Blk {
        std::uint32_t us;
        std::uint32_t bytes;
    };
    std::uint64_t slow_us_;
    std::uint64_t slow_ = 0, bytes_ = 0;
    LatencyHistogram hist_;
    std::vector<Blk> blocks_;
};

std::string pass_name(std::uint32_t pass) {
    return std::string(pass % 2 == 1 ? "random stream " : "complement of stream ") + std::to_string((pass + 1) / 2);
}

std::string mbps_text(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f MB/s", v);
    return b;
}

std::string ms_text(std::uint64_t us) {
    char b[32];
    if (us >= 10000) std::snprintf(b, sizeof b, "%.0f ms", static_cast<double>(us) / 1000.0);
    else std::snprintf(b, sizeof b, "%.2f ms", static_cast<double>(us) / 1000.0);
    return b;
}

}  // namespace

std::string_view stage_kind_name(StageKind k) noexcept {
    switch (k) {
        case StageKind::write: return "write";
        case StageKind::verify: return "verify";
        case StageKind::read: return "read";
        case StageKind::seek: return "seek";
    }
    return "?";
}

std::string_view verdict_name(Verdict v) noexcept {
    switch (v) {
        case Verdict::pass: return "PASS";
        case Verdict::pass_with_warnings: return "PASS WITH WARNINGS";
        case Verdict::fail: return "FAIL";
        case Verdict::incomplete: return "INCOMPLETE";
    }
    return "?";
}

std::string_view severity_name(Finding::Severity s) noexcept {
    switch (s) {
        case Finding::Severity::info: return "info";
        case Finding::Severity::warning: return "warning";
        case Finding::Severity::failure: return "failure";
    }
    return "?";
}

// ===================================================================== SMART

SmartSnapshot smart_snapshot(const smart::Report& r) {
    SmartSnapshot s;
    s.valid = r.has_health_data() || r.overall_passed.has_value();
    s.overall_passed = r.overall_passed;
    s.temperature_c = r.temperature_c;
    s.power_on_hours = r.power_on_hours;
    auto raw = [&](int id) -> std::optional<std::uint64_t> {
        if (const auto* a = r.attribute(id)) return a->raw_value & 0xFFFFFFFFFFFFull;  // 48-bit raw field
        return std::nullopt;
    };
    s.reallocated_sectors = raw(5);
    s.reported_uncorrectable = raw(187);
    s.pending_sectors = raw(197);
    s.offline_uncorrectable = raw(198);
    s.udma_crc_errors = raw(199);
    if (r.nvme) {
        s.nvme_media_errors = r.nvme->media_errors;
        s.nvme_critical_warning = r.nvme->critical_warning;
        if (r.nvme->percentage_used) s.nvme_percentage_used = static_cast<std::uint64_t>(*r.nvme->percentage_used);
    }
    return s;
}

std::vector<Finding> compare_smart(const SmartSnapshot& b, const SmartSnapshot& a) {
    std::vector<Finding> out;
    using Sev = Finding::Severity;
    if (!b.valid || !a.valid) return out;
    auto grew = [&](const std::optional<std::uint64_t>& x, const std::optional<std::uint64_t>& y, const char* what, Sev sev,
                    const char* meaning) {
        if (x && y && *y > *x)
            out.push_back({sev, std::string(what) + " rose from " + std::to_string(*x) + " to " + std::to_string(*y) + " " + meaning});
    };
    grew(b.reallocated_sectors, a.reallocated_sectors, "Reallocated sector count", Sev::failure,
         "(the drive remapped sectors during the run)");
    grew(b.pending_sectors, a.pending_sectors, "Current pending sector count", Sev::failure,
         "(sectors the drive could not read reliably)");
    grew(b.offline_uncorrectable, a.offline_uncorrectable, "Offline uncorrectable count", Sev::failure, "");
    grew(b.reported_uncorrectable, a.reported_uncorrectable, "Reported uncorrectable errors", Sev::failure, "");
    grew(b.nvme_media_errors, a.nvme_media_errors, "NVMe media errors", Sev::failure, "");
    grew(b.udma_crc_errors, a.udma_crc_errors, "UDMA CRC error count", Sev::warning,
         "(usually the cable or connector, not the media)");
    if (a.overall_passed && !*a.overall_passed)
        out.push_back({Sev::failure, "SMART overall health reports FAILED after the run"});
    if (a.nvme_critical_warning && *a.nvme_critical_warning != 0 && !(b.nvme_critical_warning && *b.nvme_critical_warning == *a.nvme_critical_warning))
        out.push_back({Sev::failure, "NVMe critical warning bits are set after the run (0x" + [&] {
                           char t[16];
                           std::snprintf(t, sizeof t, "%02X", *a.nvme_critical_warning);
                           return std::string(t);
                       }() + ")"});
    if (a.temperature_c && *a.temperature_c >= 60)
        out.push_back({Sev::warning, "The drive reported " + std::to_string(*a.temperature_c) + " °C after the run"});
    if (out.empty()) out.push_back({Sev::info, "SMART counters did not change during the run"});
    return out;
}

// ==================================================================== verdict

void evaluate(QualifyResult& r) {
    using Sev = Finding::Severity;
    r.findings.clear();
    auto add = [&](Sev s, std::string t) { r.findings.push_back({s, std::move(t)}); };

    bool any_failure = false, any_warning = false, all_done = !r.stages.empty();
    const auto& c = r.config;
    for (const auto& s : r.stages) {
        if (!s.completed) all_done = false;
        if (s.io_errors) {
            any_failure = true;
            add(Sev::failure, s.name + ": " + std::to_string(s.io_errors) + " sector(s) could not be " +
                                  (s.kind == StageKind::write ? "written" : "read"));
        }
        if (s.mismatches) {
            any_failure = true;
            add(Sev::failure, s.name + ": " + std::to_string(s.mismatches) + " sector(s) read back different from what was written");
        }
        if (s.blocks) {
            const double frac = static_cast<double>(s.slow_blocks) / static_cast<double>(s.blocks);
            const std::string what = s.kind == StageKind::seek ? "random reads" : "blocks";
            const std::string limit = s.kind == StageKind::seek ? std::to_string(c.seek_slow_ms) : std::to_string(c.slow_block_ms);
            if (s.slow_blocks && frac >= c.slow_fraction_fail) {
                any_failure = true;
                add(Sev::failure, s.name + ": " + std::to_string(s.slow_blocks) + " of " + std::to_string(s.blocks) + " " + what +
                                      " took " + limit + " ms or longer");
            } else if (s.slow_blocks && frac > c.slow_fraction_warn) {
                any_warning = true;
                add(Sev::warning, s.name + ": " + std::to_string(s.slow_blocks) + " of " + std::to_string(s.blocks) + " " + what +
                                      " took " + limit + " ms or longer");
            } else if (s.slow_blocks) {
                add(Sev::info, s.name + ": " + std::to_string(s.slow_blocks) + " " + what + " took " + limit + " ms or longer (within tolerance)");
            }
        }
        if ((s.kind == StageKind::write || s.kind == StageKind::read) && s.blocks >= 20 && s.head_mbps > 0 &&
            s.tail_mbps < s.head_mbps * c.tail_head_warn) {
            any_warning = true;
            add(Sev::warning, s.name + ": the last tenth ran at " + mbps_text(s.tail_mbps) + " against " + mbps_text(s.head_mbps) +
                                  " for the first tenth");
        }
        if ((s.kind == StageKind::write || s.kind == StageKind::read) && s.blocks >= 100 && s.avg_mbps > 0 &&
            s.min_mbps < s.avg_mbps * 0.05)
            add(Sev::info, s.name + ": at least one block ran at " + mbps_text(s.min_mbps) + " against an average of " +
                               mbps_text(s.avg_mbps));
    }

    for (auto& f : compare_smart(r.smart_before, r.smart_after)) {
        if (f.severity == Sev::failure) any_failure = true;
        if (f.severity == Sev::warning) any_warning = true;
        r.findings.push_back(std::move(f));
    }
    if (!r.smart_before.valid || !r.smart_after.valid)
        add(Sev::info, "SMART was not compared (no readable snapshot before and after); only what the run itself measured counts");

    if (any_failure) {
        r.verdict = Verdict::fail;
    } else if (r.state != OperationState::completed || !all_done) {
        r.verdict = Verdict::incomplete;
        if (r.state == OperationState::cancelled) add(Sev::info, "The run was cancelled before every stage finished");
        else if (r.state == OperationState::failed && !r.failure.empty()) add(Sev::info, "The run stopped early: " + r.failure);
    } else {
        r.verdict = any_warning ? Verdict::pass_with_warnings : Verdict::pass;
    }
}

// ================================================================= operation

Qualification::Qualification(safety::AuthorizedTarget target, QualifyConfig config)
    : Operation("qualification", target.device_id(), AccessKind::exclusive),
      target_(std::move(target)),
      info_(target_.info()),
      config_(std::move(config)) {}

Qualification::~Qualification() = default;

void Qualification::set_smart_before(SmartSnapshot s) {
    std::lock_guard lk(rm_);
    r_.smart_before = std::move(s);
}
void Qualification::set_smart_after(SmartSnapshot s) {
    std::lock_guard lk(rm_);
    r_.smart_after = std::move(s);
}

std::pair<std::size_t, std::size_t> Qualification::stage_position() const {
    std::lock_guard lk(rm_);
    return {current_stage_, plan_.size()};
}

void Qualification::note_bad(std::uint64_t first_lba, std::uint64_t count) {
    std::lock_guard lk(rm_);
    auto& v = r_.bad_ranges;
    if (!v.empty() && v.back().first + v.back().count == first_lba) v.back().count += count;
    else if (v.size() < kMaxListedRanges) v.push_back(LbaRange{first_lba, count});
}

Status Qualification::do_validate() {
    if (target_.kind() != safety::Kind::qualification)
        return Error{Errc::refused, "the authorization was not granted for a qualification run"};
    const auto ss = target_.sector_size();
    if (config_.block_bytes < ss || config_.block_bytes % ss != 0 || config_.block_bytes > kMaxBlockBytes)
        return Error{Errc::invalid_argument, "block size must be a multiple of the sector size, at most 64 MiB"};
    if (target_.direct_io() && config_.block_bytes % target_.required_alignment() != 0)
        return Error{Errc::misaligned, "block size must be a multiple of the I/O alignment"};
    if (config_.write_passes < 1 || config_.write_passes > kMaxWritePasses)
        return Error{Errc::invalid_argument, "write passes must be between 1 and 4"};
    if (config_.slow_block_ms == 0 || config_.seek_slow_ms == 0)
        return Error{Errc::invalid_argument, "the slow-request limits must be positive"};
    if (!(config_.slow_fraction_warn >= 0 && config_.slow_fraction_fail > config_.slow_fraction_warn && config_.slow_fraction_fail <= 1))
        return Error{Errc::invalid_argument, "slow fractions must satisfy 0 <= warn < fail <= 1"};

    plan_.clear();
    for (std::uint32_t p = 1; p <= config_.write_passes; ++p) {
        plan_.push_back({StageKind::write, p});
        if (config_.verify_each_pass) plan_.push_back({StageKind::verify, p});
    }
    if (config_.final_read) plan_.push_back({StageKind::read, 0});
    if (config_.seek_samples) plan_.push_back({StageKind::seek, 0});

    const std::uint64_t bytes = target_.preview().affected_bytes;
    std::uint64_t total = 0;
    for (const auto& s : plan_) total += s.kind == StageKind::seek ? std::uint64_t{config_.seek_samples} * kSeekBytes : bytes;
    set_total_bytes(total);

    keys_.clear();
    nonces_.clear();
    for (std::uint32_t i = 0; i < (config_.write_passes + 1) / 2; ++i) {
        keys_.push_back(ChaCha20::random_key());
        nonces_.push_back(ChaCha20::random_nonce());
    }

    std::lock_guard lk(rm_);
    auto smart_before = std::move(r_.smart_before);
    auto smart_after = std::move(r_.smart_after);
    r_ = QualifyResult{};
    r_.smart_before = std::move(smart_before);
    r_.smart_after = std::move(smart_after);
    r_.config = config_;
    r_.device = info_;
    r_.ranges = target_.ranges();
    r_.affected_bytes = bytes;
    r_.whole_device = target_.preview().whole_device;
    r_.direct_io = target_.direct_io();
    r_.warnings = target_.warnings();
    for (const auto& w : target_.preview().warnings) r_.warnings.push_back(w);
    for (const auto& s : plan_) {
        StageResult sr;
        sr.kind = s.kind;
        sr.pass = s.pass;
        switch (s.kind) {
            case StageKind::write: sr.name = "Pass " + std::to_string(s.pass) + " write (" + pass_name(s.pass) + ")"; break;
            case StageKind::verify: sr.name = "Pass " + std::to_string(s.pass) + " read-back"; break;
            case StageKind::read: sr.name = "Final sequential read"; break;
            case StageKind::seek: sr.name = "Random read sampling"; break;
        }
        r_.stages.push_back(std::move(sr));
    }
    return {};
}

void Qualification::fill(std::uint32_t pass, std::uint64_t offset, std::uint8_t* out, std::size_t len) const {
    const ChaCha20 rng(keys_[(pass - 1) / 2], nonces_[(pass - 1) / 2]);
    rng.generate(offset, out, len);
    if (pass % 2 == 0)
        for (std::size_t i = 0; i < len; ++i) out[i] = static_cast<std::uint8_t>(~out[i]);
}

Status Qualification::run_write(std::size_t stage, std::uint32_t pass, OperationControl& control) {
    const auto ss = target_.sector_size();
    const auto t0 = std::chrono::steady_clock::now();
    Recorder rec(std::uint64_t{config_.slow_block_ms} * 1000);
    AlignedBuffer buf(config_.block_bytes, std::max<std::size_t>(4096, ss));
    std::uint64_t written = 0, bad = 0;
    std::optional<std::uint64_t> first_bad;

    auto store = [&](bool done) {
        std::lock_guard lk(rm_);
        auto& s = r_.stages[stage];
        rec.fill(s, true);
        s.bytes = written;
        s.io_errors = bad;
        s.first_bad_offset = first_bad;
        s.duration_ms = ms_since(t0);
        s.completed = done;
    };

    for (const auto& r : target_.ranges()) {
        auto span = to_bytes(r, ss);
        if (!span) return span.error();
        const std::uint64_t end = span->offset + span->length;
        for (std::uint64_t off = span->offset; off < end;) {
            if (!control.checkpoint()) {
                store(false);
                return Error{Errc::cancelled, "cancelled"};
            }
            const auto len = static_cast<std::size_t>(std::min<std::uint64_t>(config_.block_bytes, end - off));
            fill(pass, off, reinterpret_cast<std::uint8_t*>(buf.data()), len);
            const auto tw = std::chrono::steady_clock::now();
            auto w = target_.write(off, buf.data(), len);
            std::uint64_t failed_here = 0;
            if (w) {
                // Wall-clock around the whole call, so queueing in the stack counts too.
                rec.add(ns_since(tw), len);
            } else {
                if (is_fatal(w.error().code)) {
                    store(false);
                    return Error{w.error().code, "write failed at byte offset " + std::to_string(off) + ": " + w.error().describe()};
                }
                const auto tf = tw;
                for (std::size_t s_off = 0; s_off < len; s_off += ss) {
                    auto ws = target_.write(off + s_off, buf.data() + s_off, ss);
                    if (ws) continue;
                    if (is_fatal(ws.error().code)) {
                        store(false);
                        return Error{ws.error().code, "write failed at byte offset " + std::to_string(off + s_off) + ": " + ws.error().describe()};
                    }
                    ++failed_here;
                    ++bad;
                    if (!first_bad) first_bad = off + s_off;
                    note_bad((off + s_off) / ss, 1);
                    add_error();
                    if (bad > config_.max_errors) {
                        store(false);
                        return Error{Errc::io_error, "aborted: more than " + std::to_string(config_.max_errors) + " sectors could not be written"};
                    }
                }
                rec.add(ns_since(tf), len);
            }
            written += len - failed_here * ss;
            add_completed(len, off / ss);
            off += len;
        }
    }
    if (auto f = target_.flush(); !f) {
        store(false);
        return Error{f.error().code, "flush after write pass " + std::to_string(pass) + " failed: " + f.error().describe()};
    }
    store(true);
    return {};
}

// Shared by the read-back and the final read: compare against pass data when
// `pass` is non-zero, plain read otherwise.
Status Qualification::run_verify(std::size_t stage, std::uint32_t pass, OperationControl& control) {
    const auto ss = target_.sector_size();
    const auto t0 = std::chrono::steady_clock::now();
    Recorder rec(std::uint64_t{config_.slow_block_ms} * 1000);
    AlignedBuffer got(config_.block_bytes, std::max<std::size_t>(4096, ss));
    AlignedBuffer want(config_.block_bytes, std::max<std::size_t>(4096, ss));
    std::uint64_t verified = 0, mismatched = 0, unreadable = 0;
    std::optional<std::uint64_t> first_bad;

    auto store = [&](bool done) {
        std::lock_guard lk(rm_);
        auto& s = r_.stages[stage];
        rec.fill(s, true);
        s.bytes = verified;
        s.io_errors = unreadable;
        s.mismatches = mismatched;
        s.first_bad_offset = first_bad;
        s.duration_ms = ms_since(t0);
        s.completed = done;
    };
    auto bad_sector = [&](std::uint64_t byte_off, bool is_unreadable) {
        if (!first_bad) first_bad = byte_off;
        if (is_unreadable) ++unreadable;
        else ++mismatched;
        note_bad(byte_off / ss, 1);
        add_error();
    };
    auto too_many = [&] { return mismatched + unreadable > config_.max_errors; };

    for (const auto& r : target_.ranges()) {
        auto span = to_bytes(r, ss);
        if (!span) return span.error();
        const std::uint64_t end = span->offset + span->length;
        for (std::uint64_t off = span->offset; off < end;) {
            if (!control.checkpoint()) {
                store(false);
                return Error{Errc::cancelled, "cancelled"};
            }
            const auto len = static_cast<std::size_t>(std::min<std::uint64_t>(config_.block_bytes, end - off));
            if (pass) fill(pass, off, reinterpret_cast<std::uint8_t*>(want.data()), len);
            auto compare = [&](std::uint64_t base_off, std::size_t idx, std::size_t n) {
                if (!pass || std::memcmp(got.data() + idx, want.data() + idx, n) == 0) return;
                for (std::size_t s = 0; s < n; s += ss)
                    if (std::memcmp(got.data() + idx + s, want.data() + idx + s, ss) != 0) bad_sector(base_off + s, false);
            };
            const auto tr = std::chrono::steady_clock::now();
            auto rd = target_.read(off, got.data(), len);
            if (rd) {
                rec.add(ns_since(tr), len);
                compare(off, 0, len);
            } else {
                if (is_fatal(rd.error().code)) {
                    store(false);
                    return Error{rd.error().code, "read failed at byte offset " + std::to_string(off) + ": " + rd.error().describe()};
                }
                const auto tf = tr;
                for (std::size_t s = 0; s < len; s += ss) {
                    auto rs = target_.read(off + s, got.data() + s, ss);
                    if (rs) {
                        compare(off + s, s, ss);
                    } else if (is_fatal(rs.error().code)) {
                        store(false);
                        return Error{rs.error().code, "read failed at byte offset " + std::to_string(off + s) + ": " + rs.error().describe()};
                    } else {
                        bad_sector(off + s, true);
                        if (too_many()) break;
                    }
                }
                rec.add(ns_since(tf), len);
            }
            if (too_many()) {
                store(false);
                return Error{Errc::io_error, "aborted: too many unreadable or mismatching sectors"};
            }
            verified += len;
            add_completed(len, off / ss);
            off += len;
        }
    }
    store(true);
    return {};
}

Status Qualification::run_read(std::size_t stage, OperationControl& control) { return run_verify(stage, 0, control); }

Status Qualification::run_seek(std::size_t stage, OperationControl& control) {
    const auto ss = target_.sector_size();
    const auto t0 = std::chrono::steady_clock::now();
    Recorder rec(std::uint64_t{config_.seek_slow_ms} * 1000);
    const std::size_t len = std::max<std::size_t>(kSeekBytes, ss);
    AlignedBuffer buf(len, std::max<std::size_t>(4096, ss));
    const std::uint64_t len_sectors = len / ss;

    // Candidate start sectors: every sector that leaves room for `len` bytes.
    struct Seg {
        std::uint64_t first, slots;
    };
    std::vector<Seg> segs;
    std::uint64_t total_slots = 0;
    for (const auto& r : target_.ranges()) {
        if (r.count >= len_sectors) {
            segs.push_back({r.first, r.count - len_sectors + 1});
            total_slots += r.count - len_sectors + 1;
        } else if (r.count > 0) {
            // tiny range: single-sector reads only
            segs.push_back({r.first, r.count});
            total_slots += r.count;
        }
    }
    SplitMix64 rng{config_.seed};
    std::uint64_t reads = 0, bad = 0, bytes = 0;
    static_cast<void>(reads);
    std::optional<std::uint64_t> first_bad;
    auto store = [&](bool done) {
        std::lock_guard lk(rm_);
        auto& s = r_.stages[stage];
        rec.fill(s, false);
        s.bytes = bytes;
        s.io_errors = bad;
        s.first_bad_offset = first_bad;
        s.duration_ms = ms_since(t0);
        s.completed = done;
    };
    for (std::uint32_t i = 0; i < config_.seek_samples && total_slots; ++i) {
        if (!control.checkpoint()) {
            store(false);
            return Error{Errc::cancelled, "cancelled"};
        }
        std::uint64_t pick = rng.below(total_slots), lba = 0, n = len_sectors;
        for (const auto& s : segs) {
            if (pick < s.slots) {
                lba = s.first + pick;
                break;
            }
            pick -= s.slots;
        }
        // Do not run past a tiny range.
        for (const auto& r : target_.ranges())
            if (r.contains(lba)) n = std::min<std::uint64_t>(n, r.first + r.count - lba);
        const auto ts = std::chrono::steady_clock::now();
        const auto rd = target_.read(lba * ss, buf.data(), static_cast<std::size_t>(n * ss));
        ++reads;
        if (rd) {
            rec.add(ns_since(ts), n * ss);
            bytes += n * ss;
        } else {
            if (is_fatal(rd.error().code)) {
                store(false);
                return Error{rd.error().code, "read failed at LBA " + std::to_string(lba) + ": " + rd.error().describe()};
            }
            ++bad;
            if (!first_bad) first_bad = lba * ss;
            note_bad(lba, n);
            add_error();
            rec.add(std::uint64_t{config_.seek_slow_ms} * 1'000'000ull, n * ss);
        }
        add_completed(kSeekBytes, lba);
    }
    store(true);
    return {};
}

Status Qualification::do_run(OperationControl& control) {
    const auto t_start = std::chrono::steady_clock::now();
    {
        std::lock_guard lk(rm_);
        r_.started_utc = utc_now();
    }
    auto finish = [&] {
        std::lock_guard lk(rm_);
        r_.finished_utc = utc_now();
        r_.duration_ms = ms_since(t_start);
    };
    for (std::size_t i = 0; i < plan_.size(); ++i) {
        {
            std::lock_guard lk(rm_);
            current_stage_ = i + 1;
        }
        std::string msg;
        {
            std::lock_guard lk(rm_);
            msg = "Stage " + std::to_string(i + 1) + " of " + std::to_string(plan_.size()) + ": " + r_.stages[i].name;
        }
        set_message(msg);
        Status s;
        switch (plan_[i].kind) {
            case StageKind::write: s = run_write(i, plan_[i].pass, control); break;
            case StageKind::verify: s = run_verify(i, plan_[i].pass, control); break;
            case StageKind::read: s = run_read(i, control); break;
            case StageKind::seek: s = run_seek(i, control); break;
        }
        if (!s) {
            finish();
            return s;
        }
    }
    finish();
    // Errors do not abort the run (the report holds them) but they make the
    // process exit status say so.
    std::lock_guard lk(rm_);
    for (const auto& s : r_.stages)
        if (s.io_errors || s.mismatches) return Error{Errc::io_error, "completed with errors: see the report"};
    return {};
}

QualifyResult Qualification::result() const {
    QualifyResult out;
    {
        std::lock_guard lk(rm_);
        out = r_;
    }
    const auto p = progress();
    out.state = p.state;
    if (p.state == OperationState::failed) out.failure = p.message;
    if (out.finished_utc.empty() && is_terminal(p.state)) out.finished_utc = utc_now();
    // A run that ended because the operation reported "completed with errors"
    // is still a finished run: the stages carry the evidence.
    evaluate(out);
    return out;
}

// ====================================================================== text

std::string result_to_text(const QualifyResult& r, bool include_serial) {
    std::ostringstream o;
    const auto& d = r.device;
    o << "platter drive qualification report\n==================================\n\n";
    o << "Verdict:   " << verdict_name(r.verdict) << "  (operation " << operation_state_name(r.state) << ")\n";
    if (!r.failure.empty()) o << "Stopped:   " << r.failure << "\n";
    o << "Device:    " << (d.model.empty() ? "(unknown model)" : d.model) << ", " << format_capacity(d.capacity_bytes) << ", "
      << bus_type_name(d.bus) << ", " << d.logical_sector_size << "/" << d.physical_sector_size << " B sectors\n";
    o << "Path:      " << d.path << "\n";
    o << "Serial:    " << (include_serial ? d.serial : std::string("(omitted)")) << "\n";
    if (r.whole_device) o << "Range:     whole device (" << d.total_lbas() << " sectors)\n";
    else {
        o << "Range:    ";
        for (const auto& rg : r.ranges) o << " LBA " << rg.first << ".." << rg.last();
        o << "  (" << format_capacity(r.affected_bytes) << ")\n";
    }
    o << "Plan:      " << r.config.write_passes << " write pass(es)" << (r.config.verify_each_pass ? " with read-back" : "")
      << (r.config.final_read ? ", final read" : "") << (r.config.seek_samples ? ", " + std::to_string(r.config.seek_samples) + " random reads" : "")
      << "; " << r.config.block_bytes / 1024 << " KiB requests, " << (r.direct_io ? "direct I/O" : "buffered I/O") << "\n";
    o << "Started:   " << r.started_utc << "\nFinished:  " << r.finished_utc << "\nDuration:  " << r.duration_ms / 1000 << " s\n\n";

    o << "Stage                                   Done  Avg          Min          Max          Slow   Errors  Mismatch\n";
    for (const auto& s : r.stages) {
        char line[300];
        std::string avg = "-", mn = "-", mx = "-";
        if (s.kind == StageKind::seek) {
            avg = "p50 " + ms_text(s.median_us);
            mn = "p99 " + ms_text(s.p99_us);
            mx = "max " + ms_text(s.max_us);
        } else if (s.avg_mbps > 0) {
            avg = mbps_text(s.avg_mbps);
            mn = mbps_text(s.min_mbps);
            mx = mbps_text(s.max_mbps);
        }
        std::snprintf(line, sizeof line, "%-38s  %-4s  %-11s  %-11s  %-11s  %-5llu  %-6llu  %llu\n", s.name.c_str(),
                      s.completed ? "yes" : "no", avg.c_str(), mn.c_str(), mx.c_str(), static_cast<unsigned long long>(s.slow_blocks),
                      static_cast<unsigned long long>(s.io_errors), static_cast<unsigned long long>(s.mismatches));
        o << line;
    }
    if (!r.bad_ranges.empty()) {
        o << "\nSectors with errors (first ranges, LBA first+count):\n";
        for (const auto& rg : r.bad_ranges) o << "  " << rg.first << " +" << rg.count << "\n";
    }
    o << "\nWhy this verdict:\n";
    if (r.findings.empty()) o << "  (nothing noteworthy)\n";
    for (const auto& f : r.findings) o << "  [" << severity_name(f.severity) << "] " << f.text << "\n";
    if (!r.warnings.empty()) {
        o << "\nNotes:\n";
        for (const auto& w : r.warnings) o << "  - " << w << "\n";
    }
    o << "\nThis report describes what platter observed during one run. It does not predict failure, "
         "and a passing run is not a guarantee.\n";
    return o.str();
}

}  // namespace platter::qualify
