// SPDX-License-Identifier: TBD
#include "platter/imaging/imaging.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <functional>
#include <span>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "platter/core/aligned_buffer.hpp"
#include "platter/core/sha256.hpp"
#include "platter/core/time_util.hpp"

namespace platter::imaging {

namespace fs = std::filesystem;

std::string_view kind_name(JobKind k) noexcept {
    switch (k) {
        case JobKind::create: return "create";
        case JobKind::restore: return "restore";
        case JobKind::clone: return "clone";
        case JobKind::verify: return "verify";
    }
    return "?";
}

std::string_view verdict_name(Verdict v) noexcept {
    switch (v) {
        case Verdict::ok: return "OK";
        case Verdict::ok_with_unreadable: return "COMPLETED WITH UNREADABLE SECTORS";
        case Verdict::check_failed: return "VERIFICATION FAILED";
        case Verdict::incomplete: return "INCOMPLETE";
    }
    return "?";
}

namespace {

constexpr std::uint32_t kMaxBlockBytes = 64u << 20;

// Cheap 64-bit block fingerprint, used only to LOCATE differences; the
// SHA-256 over the whole stream is what decides pass or fail.
std::uint64_t fast_hash(const std::uint8_t* p, std::size_t n) {
    std::uint64_t h = 0xcbf29ce484222325ull ^ n;
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        std::uint64_t w;
        std::memcpy(&w, p + i, 8);
        h = (h ^ w) * 0x9E3779B97F4A7C15ull;
        h ^= h >> 29;
    }
    for (; i < n; ++i) h = (h ^ p[i]) * 0x100000001B3ull;
    return h ^ (h >> 32);
}

void add_lba(std::vector<LbaRange>& v, std::uint64_t lba, std::uint64_t count = 1) {
    if (!v.empty() && v.back().first + v.back().count == lba) v.back().count += count;
    else v.push_back(LbaRange{lba, count});
}

Status check_block_bytes(std::uint32_t bb, std::uint32_t ss, std::uint32_t alignment) {
    if (ss == 0) return Error{Errc::invalid_argument, "the device reports no sector size"};
    if (bb < ss || bb % ss != 0 || bb > kMaxBlockBytes)
        return Error{Errc::invalid_argument, "the block size must be a multiple of the " + std::to_string(ss) + "-byte sector size, up to 64 MiB"};
    if (alignment && bb % alignment != 0) return Error{Errc::misaligned, "the block size must be a multiple of the I/O alignment"};
    return {};
}

bool valid_sha_hex(const std::string& s) {
    return s.size() == 64 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Reads from a device; with `recover`, unreadable sectors become zeros.
struct RecoveringReader {
    IBlockDevice& dev;
    bool recover;
    std::uint32_t retries;
    std::uint64_t max_unreadable;
    std::vector<LbaRange> bad;
    std::uint64_t bad_sectors = 0;

    // Returns the number of sectors replaced by zeros in this call via `fixed`.
    Status read(std::uint64_t lba, std::uint64_t sectors, std::uint8_t* buf, std::uint64_t& fixed) {
        fixed = 0;
        const std::uint32_t ss = dev.sector_size();
        Error last{};
        for (std::uint32_t attempt = 0; attempt <= retries; ++attempt) {
            auto r = dev.read(lba * ss, buf, sectors * ss);
            if (r) return {};
            last = r.error();
            if (last.code == Errc::device_removed) return last;
        }
        if (!recover) {
            last.message += " (at LBA " + std::to_string(lba) + "; use the recovery option to continue past unreadable sectors)";
            return last;
        }
        for (std::uint64_t i = 0; i < sectors; ++i) {
            bool ok = false;
            Error e{};
            for (std::uint32_t attempt = 0; attempt <= retries && !ok; ++attempt) {
                auto r = dev.read((lba + i) * ss, buf + i * ss, ss);
                if (r) ok = true;
                else {
                    e = r.error();
                    if (e.code == Errc::device_removed) return e;
                }
            }
            if (!ok) {
                std::memset(buf + i * ss, 0, ss);
                add_lba(bad, lba + i);
                ++bad_sectors;
                ++fixed;
                if (max_unreadable && bad_sectors > max_unreadable)
                    return Error{Errc::io_error, "more than " + std::to_string(max_unreadable) + " unreadable sectors; giving up"};
            }
        }
        return {};
    }
};

std::chrono::steady_clock::time_point now() { return std::chrono::steady_clock::now(); }
std::uint64_t ms_since(std::chrono::steady_clock::time_point t) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now() - t).count());
}

void finalize(ImagingResult& r, OperationState st) {
    r.state = st;
    r.finished_utc = utc_now();
    if (st != OperationState::completed) {
        r.verdict = Verdict::incomplete;
        return;
    }
    const bool failed = r.sha_matches == std::optional<bool>(false) || r.readback_ok == std::optional<bool>(false) ||
                        !r.mismatches.empty();
    if (failed) r.verdict = Verdict::check_failed;
    else if (r.unreadable_sectors > 0) r.verdict = Verdict::ok_with_unreadable;
    else r.verdict = Verdict::ok;
}

// Streams the image through a SHA-256 (and optionally per-block fingerprints).
Status hash_image(const std::string& path, std::uint32_t block, std::string& sha, std::uint64_t& total,
                  const std::function<bool(std::uint64_t)>& progress) {
    auto rd = ImageReader::open(path);
    if (!rd) return rd.error();
    std::vector<std::uint8_t> buf(block);
    Sha256 h;
    total = 0;
    for (;;) {
        auto n = (*rd)->read(buf.data(), buf.size());
        if (!n) return n.error();
        if (*n == 0) break;
        h.update(std::span<const std::uint8_t>(buf.data(), *n));
        total += *n;
        if (progress && !progress(*n)) return Error{Errc::cancelled, "cancelled"};
    }
    sha = Sha256::hex(h.finish());
    return {};
}

}  // namespace

// ====================================================================== create

CreateImage::CreateImage(BlockDevicePtr source, ImageConfig config)
    : Operation("create-image", source ? source->info().path : std::string{}, AccessKind::shared_read),
      src_(std::move(source)),
      cfg_(std::move(config)) {
    if (src_) info_ = src_->info();
}

CreateImage::~CreateImage() {
    cancel();
    wait();
}

Status CreateImage::do_validate() {
    if (!src_) return Error{Errc::invalid_argument, "no source device"};
    if (src_->mode() != OpenMode::read_only) return Error{Errc::invalid_argument, "imaging needs a read-only source handle"};
    const auto ss = info_.logical_sector_size;
    const auto total = info_.total_lbas();
    if (total == 0) return Error{Errc::invalid_argument, "the source reports zero capacity"};
    if (auto s = check_block_bytes(cfg_.block_bytes, ss, 0); !s) return s;
    if (cfg_.output.empty()) return Error{Errc::invalid_argument, "no output file given"};
    if (cfg_.compression == Compression::zstd && !zstd_available()) return Error{Errc::unsupported, "this build has no zstd support"};
    if (cfg_.compression == Compression::zstd && (cfg_.level < 1 || cfg_.level > 19))
        return Error{Errc::invalid_argument, "the zstd level must be between 1 and 19"};
    if (cfg_.retries > 16) return Error{Errc::invalid_argument, "at most 16 retries"};
    std::error_code ec;
    if (fs::exists(cfg_.output, ec) && fs::exists(info_.path, ec) && fs::equivalent(cfg_.output, info_.path, ec))
        return Error{Errc::refused, "the output file is the source device"};
    if (fs::exists(cfg_.output, ec) && !cfg_.overwrite)
        return Error{Errc::invalid_argument, "the output file already exists: " + cfg_.output + " (use --force to replace it)"};
    const std::uint64_t end = cfg_.end_lba == UINT64_MAX ? total - 1 : cfg_.end_lba;
    auto range = lba_range_inclusive(cfg_.start_lba, end);
    if (!range) return range.error();
    if (auto s = validate_lba_range(*range, total); !s) return s;
    range_ = *range;
    set_total_bytes(range_.count * ss * (cfg_.verify_image ? 2 : 1));

    std::lock_guard lk(m_);
    r_ = ImagingResult{};
    r_.kind = JobKind::create;
    r_.source = info_;
    r_.image_path = cfg_.output;
    r_.compression = cfg_.compression;
    r_.sector_size = ss;
    r_.source_first_lba = range_.first;
    r_.sectors = range_.count;
    r_.bytes = range_.count * ss;
    r_.block_bytes = cfg_.block_bytes;
    r_.recover = cfg_.recover;
    return {};
}

ImagingResult CreateImage::result() const {
    std::lock_guard lk(m_);
    auto r = r_;
    if (!is_terminal(state())) r.state = state();
    return r;
}

Status CreateImage::do_run(OperationControl& control) {
    const auto t0 = now();
    {
        std::lock_guard lk(m_);
        r_.started_utc = utc_now();
    }
    auto fail = [&](const Error& e) -> Status {
        std::lock_guard lk(m_);
        r_.failure = e.describe();
        r_.duration_ms = ms_since(t0);
        finalize(r_, e.code == Errc::cancelled ? OperationState::cancelled : OperationState::failed);
        return e;
    };

    const std::uint32_t ss = info_.logical_sector_size;
    const std::uint64_t total_bytes = range_.count * ss;
    WriterOptions wo;
    wo.compression = cfg_.compression;
    wo.level = cfg_.level;
    wo.sparse = cfg_.sparse;
    wo.overwrite = cfg_.overwrite;
    wo.total_bytes = total_bytes;
    auto writer = ImageWriter::create(cfg_.output, wo);
    if (!writer) return fail(writer.error());

    AlignedBuffer buf(cfg_.block_bytes, 4096);
    RecoveringReader rr{*src_, cfg_.recover, cfg_.retries, cfg_.max_unreadable_sectors, {}, 0};
    Sha256 sha;
    set_message(cfg_.recover ? "Copying (recovery mode)" : "Copying");
    const std::uint64_t block_sectors = cfg_.block_bytes / ss;
    for (std::uint64_t done = 0; done < range_.count;) {
        if (!control.checkpoint()) return fail(Error{Errc::cancelled, "cancelled"});
        const std::uint64_t n = std::min(block_sectors, range_.count - done);
        const std::uint64_t lba = range_.first + done;
        std::uint64_t fixed = 0;
        if (auto s = rr.read(lba, n, reinterpret_cast<std::uint8_t*>(buf.data()), fixed); !s) return fail(s.error());
        if (fixed) add_error(fixed);
        const auto bytes = static_cast<std::size_t>(n * ss);
        sha.update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(buf.data()), bytes));
        if (auto s = (*writer)->write(buf.data(), bytes); !s) return fail(s.error());
        done += n;
        add_completed(bytes, lba + n);
    }
    if (auto s = (*writer)->finish(); !s) return fail(s.error());
    const std::string digest = Sha256::hex(sha.finish());
    {
        std::lock_guard lk(m_);
        r_.sha256 = digest;
        r_.unreadable = rr.bad;
        r_.unreadable_sectors = rr.bad_sectors;
        std::error_code ec;
        r_.image_file_bytes = static_cast<std::uint64_t>(fs::file_size(cfg_.output, ec));
    }

    if (cfg_.verify_image) {
        set_message("Verifying the image file");
        std::string again;
        std::uint64_t got = 0;
        auto s = hash_image(cfg_.output, cfg_.block_bytes, again, got, [&](std::uint64_t n) {
            add_completed(n, 0);
            return control.checkpoint();
        });
        if (!s) {
            if (s.error().code == Errc::cancelled) return fail(s.error());
            std::lock_guard lk(m_);
            r_.readback_ok = false;
            r_.warnings.push_back("The finished image could not be read back: " + s.error().describe());
        } else {
            std::lock_guard lk(m_);
            r_.readback_ok = got == total_bytes && again == digest;
            if (!*r_.readback_ok) r_.warnings.push_back("The finished image does not match the data that was read: do not rely on it.");
        }
    }

    std::lock_guard lk(m_);
    if (cfg_.write_sidecar) {
        const std::string side = cfg_.output + ".sha256";
        std::ofstream f(side, std::ios::trunc);
        f << sha256_sidecar_text(digest, cfg_.output, cfg_.compression);
        if (!f) r_.warnings.push_back("Could not write " + side);
    }
    if (r_.unreadable_sectors > 0)
        r_.warnings.push_back(std::to_string(r_.unreadable_sectors) + " unreadable sector(s) were replaced by zeros in the image.");
    r_.duration_ms = ms_since(t0);
    finalize(r_, OperationState::completed);
    return {};
}

// ===================================================================== restore

RestoreImage::RestoreImage(safety::AuthorizedTarget target, RestoreConfig config)
    : Operation("restore-image", target.device_id(), AccessKind::exclusive),
      target_(std::move(target)),
      cfg_(std::move(config)) {}

RestoreImage::~RestoreImage() {
    cancel();
    wait();
}

Result<std::uint64_t> RestoreImage::sectors_needed(const std::string& image, std::uint32_t ss) {
    if (ss == 0) return Error{Errc::invalid_argument, "sector size is zero"};
    auto rd = ImageReader::open(image);
    if (!rd) return rd.error();
    if (!(*rd)->size()) return Error{Errc::unsupported, "the compressed image does not record its size (not made by platter?)"};
    const auto bytes = *(*rd)->size();
    if (bytes == 0) return Error{Errc::invalid_argument, "the image is empty"};
    if (bytes % ss != 0)
        return Error{Errc::invalid_argument, "the image size (" + std::to_string(bytes) + " bytes) is not a multiple of the " +
                                                 std::to_string(ss) + "-byte sector size of the target"};
    return bytes / ss;
}

Status RestoreImage::do_validate() {
    if (target_.kind() != safety::Kind::restore_image)
        return Error{Errc::refused, "the authorization was not granted for restoring an image"};
    if (target_.ranges().size() != 1) return Error{Errc::refused, "a restore needs exactly one confirmed range"};
    const auto& range = target_.ranges()[0];
    const std::uint32_t ss = target_.sector_size();
    if (auto s = check_block_bytes(cfg_.block_bytes, ss, target_.direct_io() ? target_.required_alignment() : 0); !s) return s;
    if (!cfg_.expect_sha256.empty()) {
        if (!valid_sha_hex(cfg_.expect_sha256)) return Error{Errc::invalid_argument, "the expected checksum must be 64 hex digits"};
        cfg_.expect_sha256 = lower(cfg_.expect_sha256);
    }
    auto sectors = sectors_needed(cfg_.input, ss);
    if (!sectors) return sectors.error();
    if (*sectors > range.count)
        return Error{Errc::refused, "the image needs " + std::to_string(*sectors) + " sectors but only " + std::to_string(range.count) +
                                        " are confirmed for writing"};
    image_bytes_ = *sectors * ss;
    std::error_code ec;
    if (fs::exists(cfg_.input, ec) && fs::exists(target_.info().path, ec) && fs::equivalent(cfg_.input, target_.info().path, ec))
        return Error{Errc::refused, "the image file is the target"};
    set_total_bytes(image_bytes_ * (1 + (cfg_.verify ? 1 : 0) + (cfg_.expect_sha256.empty() ? 0 : 1)));

    auto rd = ImageReader::open(cfg_.input);
    std::lock_guard lk(m_);
    r_ = ImagingResult{};
    r_.kind = JobKind::restore;
    r_.target = target_.info();
    r_.image_path = cfg_.input;
    if (rd) {
        r_.compression = (*rd)->compression();
        r_.image_file_bytes = (*rd)->file_bytes();
    }
    r_.sector_size = ss;
    r_.target_first_lba = range.first;
    r_.sectors = *sectors;
    r_.bytes = image_bytes_;
    r_.block_bytes = cfg_.block_bytes;
    r_.expected_sha256 = cfg_.expect_sha256;
    r_.warnings = target_.warnings();
    if (*sectors < range.count) r_.warnings.push_back("The confirmed range is larger than the image; the extra sectors are not written.");
    return {};
}

ImagingResult RestoreImage::result() const {
    std::lock_guard lk(m_);
    auto r = r_;
    if (!is_terminal(state())) r.state = state();
    return r;
}

Status RestoreImage::do_run(OperationControl& control) {
    const auto t0 = now();
    {
        std::lock_guard lk(m_);
        r_.started_utc = utc_now();
    }
    auto fail = [&](const Error& e) -> Status {
        std::lock_guard lk(m_);
        r_.failure = e.describe();
        r_.duration_ms = ms_since(t0);
        finalize(r_, e.code == Errc::cancelled ? OperationState::cancelled : OperationState::failed);
        return e;
    };
    const std::uint32_t ss = target_.sector_size();
    const auto first = target_.ranges()[0].first;

    // 1. Optional: prove the image is what the user expects BEFORE writing anything.
    if (!cfg_.expect_sha256.empty()) {
        set_message("Checking the image checksum");
        std::string sha;
        std::uint64_t got = 0;
        auto s = hash_image(cfg_.input, cfg_.block_bytes, sha, got, [&](std::uint64_t n) {
            add_completed(n, 0);
            return control.checkpoint();
        });
        if (!s) return fail(s.error());
        const bool match = sha == cfg_.expect_sha256;
        {
            std::lock_guard lk(m_);
            r_.sha_matches = match;
        }
        if (!match) return fail(Error{Errc::corrupt_data, "the image checksum is " + sha + ", not the expected " + cfg_.expect_sha256 + "; nothing was written"});
    }

    // 2. Write.
    auto rd = ImageReader::open(cfg_.input);
    if (!rd) return fail(rd.error());
    AlignedBuffer buf(cfg_.block_bytes, std::max<std::size_t>(4096, target_.required_alignment()));
    std::vector<std::uint64_t> block_hash;
    Sha256 sha;
    set_message("Writing");
    std::uint64_t done = 0;
    while (done < image_bytes_) {
        if (!control.checkpoint()) return fail(Error{Errc::cancelled, "cancelled"});
        const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(cfg_.block_bytes, image_bytes_ - done));
        auto n = (*rd)->read(buf.data(), want);
        if (!n) return fail(n.error());
        if (*n != want) return fail(Error{Errc::corrupt_data, "the image ended after " + std::to_string(done + *n) + " of " + std::to_string(image_bytes_) + " bytes"});
        const auto* p = reinterpret_cast<const std::uint8_t*>(buf.data());
        sha.update(std::span<const std::uint8_t>(p, want));
        block_hash.push_back(fast_hash(p, want));
        auto w = target_.write(first * ss + done, buf.data(), want);
        if (!w) return fail(w.error());
        done += want;
        add_completed(want, first + done / ss);
    }
    const std::string digest = Sha256::hex(sha.finish());
    {
        std::lock_guard lk(m_);
        r_.sha256 = digest;
        if (!r_.expected_sha256.empty()) r_.sha_matches = digest == r_.expected_sha256;
    }
    set_message("Flushing");
    if (auto f = target_.flush(); !f) return fail(f.error());

    // 3. Read back.
    if (cfg_.verify) {
        set_message("Verifying");
        Sha256 back;
        std::vector<LbaRange> bad;
        std::uint64_t pos = 0, idx = 0;
        while (pos < image_bytes_) {
            if (!control.checkpoint()) return fail(Error{Errc::cancelled, "cancelled"});
            const auto want = static_cast<std::size_t>(std::min<std::uint64_t>(cfg_.block_bytes, image_bytes_ - pos));
            auto r = target_.read(first * ss + pos, buf.data(), want);
            if (!r) return fail(Error{r.error().code, "cannot read back at byte offset " + std::to_string(pos) + ": " + r.error().describe()});
            const auto* p = reinterpret_cast<const std::uint8_t*>(buf.data());
            back.update(std::span<const std::uint8_t>(p, want));
            if (fast_hash(p, want) != block_hash[idx]) add_lba(bad, first + pos / ss, want / ss);
            pos += want;
            ++idx;
            add_completed(want, first + pos / ss);
        }
        const bool same = Sha256::hex(back.finish()) == digest;
        std::lock_guard lk(m_);
        r_.readback_ok = same && bad.empty();
        r_.mismatches = bad;
        if (!same) r_.warnings.push_back("The data read back from the target differs from the image (mismatches are reported per block).");
    }

    // 4. Let the OS notice a new partition table. Not fatal.
    if (auto s = target_.reread_partition_table(); !s && s.error().code != Errc::unsupported) {
        std::lock_guard lk(m_);
        r_.warnings.push_back("The OS did not re-read the partition table: " + s.error().describe());
    }
    std::lock_guard lk(m_);
    r_.duration_ms = ms_since(t0);
    finalize(r_, OperationState::completed);
    return {};
}

// ====================================================================== clone

CloneDevice::CloneDevice(BlockDevicePtr source, safety::AuthorizedTarget target, CloneConfig config)
    : Operation("clone-device", target.device_id(), AccessKind::exclusive),
      src_(std::move(source)),
      target_(std::move(target)),
      cfg_(config) {}

CloneDevice::~CloneDevice() {
    cancel();
    wait();
}

Status CloneDevice::do_validate() {
    if (!src_) return Error{Errc::invalid_argument, "no source device"};
    if (src_->mode() != OpenMode::read_only) return Error{Errc::invalid_argument, "cloning needs a read-only source handle"};
    if (target_.kind() != safety::Kind::clone_target) return Error{Errc::refused, "the authorization was not granted for a clone"};
    if (target_.ranges().size() != 1) return Error{Errc::refused, "a clone needs exactly one confirmed range"};
    if (src_->info().path == target_.info().path) return Error{Errc::refused, "source and target are the same device"};
    std::error_code ec;
    if (fs::exists(src_->info().path, ec) && fs::exists(target_.info().path, ec) &&
        fs::equivalent(src_->info().path, target_.info().path, ec))
        return Error{Errc::refused, "source and target are the same device"};
    const std::uint32_t ss = target_.sector_size();
    if (src_->sector_size() != ss)
        return Error{Errc::invalid_argument, "source and target have different logical sector sizes (" + std::to_string(src_->sector_size()) +
                                                 " vs " + std::to_string(ss) + "); a sector-for-sector clone is not possible"};
    if (auto s = check_block_bytes(cfg_.block_bytes, ss, target_.direct_io() ? target_.required_alignment() : 0); !s) return s;
    if (cfg_.retries > 16) return Error{Errc::invalid_argument, "at most 16 retries"};
    const auto& range = target_.ranges()[0];
    if (auto s = validate_lba_range(LbaRange{cfg_.source_first_lba, range.count}, src_->info().total_lbas()); !s)
        return Error{Errc::out_of_range, "the source is smaller than the region to copy: " + s.error().message};
    set_total_bytes(range.count * ss * (cfg_.verify ? 2 : 1));

    std::lock_guard lk(m_);
    r_ = ImagingResult{};
    r_.kind = JobKind::clone;
    r_.source = src_->info();
    r_.target = target_.info();
    r_.sector_size = ss;
    r_.source_first_lba = cfg_.source_first_lba;
    r_.target_first_lba = range.first;
    r_.sectors = range.count;
    r_.bytes = range.count * ss;
    r_.block_bytes = cfg_.block_bytes;
    r_.recover = cfg_.recover;
    r_.warnings = target_.warnings();
    if (target_.info().capacity_bytes > r_.bytes + range.first * ss)
        r_.warnings.push_back("The target is larger than the copied region; the rest of it was not touched (partitions and file systems are not resized).");
    return {};
}

ImagingResult CloneDevice::result() const {
    std::lock_guard lk(m_);
    auto r = r_;
    if (!is_terminal(state())) r.state = state();
    return r;
}

Status CloneDevice::do_run(OperationControl& control) {
    const auto t0 = now();
    {
        std::lock_guard lk(m_);
        r_.started_utc = utc_now();
    }
    auto fail = [&](const Error& e) -> Status {
        std::lock_guard lk(m_);
        r_.failure = e.describe();
        r_.duration_ms = ms_since(t0);
        finalize(r_, e.code == Errc::cancelled ? OperationState::cancelled : OperationState::failed);
        return e;
    };
    const std::uint32_t ss = target_.sector_size();
    const auto tfirst = target_.ranges()[0].first;
    const std::uint64_t sectors = target_.ranges()[0].count;
    const std::uint64_t block_sectors = cfg_.block_bytes / ss;
    AlignedBuffer buf(cfg_.block_bytes, std::max<std::size_t>(4096, target_.required_alignment()));
    RecoveringReader rr{*src_, cfg_.recover, cfg_.retries, cfg_.max_unreadable_sectors, {}, 0};
    std::vector<std::uint64_t> block_hash;
    Sha256 sha;
    set_message(cfg_.recover ? "Cloning (recovery mode)" : "Cloning");
    for (std::uint64_t done = 0; done < sectors;) {
        if (!control.checkpoint()) return fail(Error{Errc::cancelled, "cancelled"});
        const std::uint64_t n = std::min(block_sectors, sectors - done);
        std::uint64_t fixed = 0;
        auto* p = reinterpret_cast<std::uint8_t*>(buf.data());
        if (auto s = rr.read(cfg_.source_first_lba + done, n, p, fixed); !s) return fail(s.error());
        if (fixed) add_error(fixed);
        const auto bytes = static_cast<std::size_t>(n * ss);
        sha.update(std::span<const std::uint8_t>(p, bytes));
        block_hash.push_back(fast_hash(p, bytes));
        auto w = target_.write((tfirst + done) * ss, buf.data(), bytes);
        if (!w) return fail(w.error());
        done += n;
        add_completed(bytes, tfirst + done);
    }
    const std::string digest = Sha256::hex(sha.finish());
    set_message("Flushing");
    if (auto f = target_.flush(); !f) return fail(f.error());
    {
        std::lock_guard lk(m_);
        r_.sha256 = digest;
        r_.unreadable = rr.bad;
        r_.unreadable_sectors = rr.bad_sectors;
    }
    if (cfg_.verify) {
        set_message("Verifying");
        Sha256 back;
        std::vector<LbaRange> bad;
        std::uint64_t idx = 0;
        for (std::uint64_t done = 0; done < sectors;) {
            if (!control.checkpoint()) return fail(Error{Errc::cancelled, "cancelled"});
            const std::uint64_t n = std::min(block_sectors, sectors - done);
            const auto bytes = static_cast<std::size_t>(n * ss);
            auto r = target_.read((tfirst + done) * ss, buf.data(), bytes);
            if (!r) return fail(Error{r.error().code, "cannot read back at LBA " + std::to_string(tfirst + done) + ": " + r.error().describe()});
            const auto* p = reinterpret_cast<const std::uint8_t*>(buf.data());
            back.update(std::span<const std::uint8_t>(p, bytes));
            if (fast_hash(p, bytes) != block_hash[idx]) add_lba(bad, tfirst + done, n);
            done += n;
            ++idx;
            add_completed(bytes, tfirst + done);
        }
        const bool same = Sha256::hex(back.finish()) == digest;
        std::lock_guard lk(m_);
        r_.readback_ok = same && bad.empty();
        r_.mismatches = bad;
        if (!same) r_.warnings.push_back("The target does not read back as the source did (mismatches are reported per block).");
    }
    if (auto s = target_.reread_partition_table(); !s && s.error().code != Errc::unsupported) {
        std::lock_guard lk(m_);
        r_.warnings.push_back("The OS did not re-read the partition table: " + s.error().describe());
    }
    std::lock_guard lk(m_);
    if (r_.unreadable_sectors > 0)
        r_.warnings.push_back(std::to_string(r_.unreadable_sectors) + " unreadable source sector(s) were written to the target as zeros.");
    r_.duration_ms = ms_since(t0);
    finalize(r_, OperationState::completed);
    return {};
}

// ====================================================================== verify

VerifyImage::VerifyImage(VerifyConfig config)
    : Operation("verify-image", config.compare_to ? config.compare_to->info().path : config.input, AccessKind::shared_read),
      cfg_(std::move(config)) {}

VerifyImage::~VerifyImage() {
    cancel();
    wait();
}

Status VerifyImage::do_validate() {
    auto rd = ImageReader::open(cfg_.input);
    if (!rd) return rd.error();
    if (!cfg_.expect_sha256.empty()) {
        if (!valid_sha_hex(cfg_.expect_sha256)) return Error{Errc::invalid_argument, "the expected checksum must be 64 hex digits"};
        cfg_.expect_sha256 = lower(cfg_.expect_sha256);
    }
    std::uint32_t ss = 512;
    if (cfg_.compare_to) {
        if (cfg_.compare_to->mode() != OpenMode::read_only) return Error{Errc::invalid_argument, "comparison needs a read-only handle"};
        ss = cfg_.compare_to->sector_size();
        if (auto s = check_block_bytes(cfg_.block_bytes, ss, 0); !s) return s;
    } else if (cfg_.block_bytes == 0 || cfg_.block_bytes > kMaxBlockBytes) {
        return Error{Errc::invalid_argument, "the block size must be between 1 byte and 64 MiB"};
    }
    // Unknown (size-less zstd) images are streamed; the total is then a guess.
    image_bytes_ = (*rd)->size().value_or(0);
    set_total_bytes(image_bytes_ ? image_bytes_ : (*rd)->file_bytes());

    std::lock_guard lk(m_);
    r_ = ImagingResult{};
    r_.kind = JobKind::verify;
    r_.image_path = cfg_.input;
    r_.compression = (*rd)->compression();
    r_.image_file_bytes = (*rd)->file_bytes();
    r_.sector_size = ss;
    r_.block_bytes = cfg_.block_bytes;
    r_.expected_sha256 = cfg_.expect_sha256;
    if (cfg_.compare_to) {
        r_.target = cfg_.compare_to->info();
        r_.target_first_lba = cfg_.compare_first_lba;
    }
    return {};
}

ImagingResult VerifyImage::result() const {
    std::lock_guard lk(m_);
    auto r = r_;
    if (!is_terminal(state())) r.state = state();
    return r;
}

Status VerifyImage::do_run(OperationControl& control) {
    const auto t0 = now();
    {
        std::lock_guard lk(m_);
        r_.started_utc = utc_now();
    }
    auto fail = [&](const Error& e) -> Status {
        std::lock_guard lk(m_);
        r_.failure = e.describe();
        r_.duration_ms = ms_since(t0);
        finalize(r_, e.code == Errc::cancelled ? OperationState::cancelled : OperationState::failed);
        return e;
    };
    auto rd = ImageReader::open(cfg_.input);
    if (!rd) return fail(rd.error());
    const std::uint32_t ss = r_.sector_size;
    AlignedBuffer img(cfg_.block_bytes, 4096), dev(cfg_.block_bytes + ss, 4096);
    Sha256 sha;
    std::uint64_t total = 0;
    std::vector<LbaRange> bad;
    bool compare_failed_io = false;
    set_message(cfg_.compare_to ? "Hashing and comparing" : "Hashing");
    for (;;) {
        if (!control.checkpoint()) return fail(Error{Errc::cancelled, "cancelled"});
        auto n = (*rd)->read(img.data(), cfg_.block_bytes);
        if (!n) return fail(n.error());
        if (*n == 0) break;
        const auto* ip = reinterpret_cast<const std::uint8_t*>(img.data());
        sha.update(std::span<const std::uint8_t>(ip, *n));
        if (cfg_.compare_to) {
            const std::uint64_t lba = cfg_.compare_first_lba + total / ss;
            const std::uint64_t padded = (*n + ss - 1) / ss * ss;
            const auto cap = cfg_.compare_to->info().capacity_bytes;
            if (lba * ss + padded > cap) {
                compare_failed_io = true;
                add_lba(bad, lba, padded / ss);
            } else {
                auto r = cfg_.compare_to->read(lba * ss, dev.data(), padded);
                if (!r) {
                    // Compare sector by sector so one bad sector does not hide the rest.
                    for (std::uint64_t s = 0; s < padded / ss; ++s) {
                        auto one = cfg_.compare_to->read((lba + s) * ss, dev.data() + s * ss, ss);
                        const std::size_t off = static_cast<std::size_t>(s * ss);
                        const std::size_t len = std::min<std::size_t>(ss, *n - std::min(*n, off));
                        if (!one || (len > 0 && std::memcmp(ip + off, dev.data() + s * ss, len) != 0)) add_lba(bad, lba + s);
                    }
                } else {
                    const auto* dp = reinterpret_cast<const std::uint8_t*>(dev.data());
                    for (std::uint64_t s = 0; s < padded / ss; ++s) {
                        const std::size_t off = static_cast<std::size_t>(s * ss);
                        const std::size_t len = std::min<std::size_t>(ss, *n - std::min(*n, off));
                        if (len > 0 && std::memcmp(ip + off, dp + off, len) != 0) add_lba(bad, lba + s);
                    }
                }
            }
        }
        total += *n;
        add_completed(*n, total / ss);
    }
    const std::string digest = Sha256::hex(sha.finish());
    std::lock_guard lk(m_);
    r_.sha256 = digest;
    r_.bytes = total;
    r_.sectors = (total + ss - 1) / ss;
    if (!r_.expected_sha256.empty()) r_.sha_matches = digest == r_.expected_sha256;
    if (cfg_.compare_to) {
        r_.mismatches = bad;
        r_.readback_ok = bad.empty();
        if (compare_failed_io) r_.warnings.push_back("The image extends past the end of the compared device.");
    }
    if (image_bytes_ && total != image_bytes_) {
        r_.sha_matches = false;
        r_.warnings.push_back("The image holds " + std::to_string(total) + " bytes but records " + std::to_string(image_bytes_) + ".");
    }
    r_.duration_ms = ms_since(t0);
    finalize(r_, OperationState::completed);
    return {};
}

// ======================================================================= text

std::string result_to_text(const ImagingResult& r, bool show_serial) {
    std::ostringstream o;
    auto dev_line = [&](const char* label, const DeviceInfo& d) {
        o << label << (d.model.empty() ? "(unknown model)" : d.model) << ", " << format_capacity(d.capacity_bytes) << ", " << bus_type_name(d.bus)
          << ", " << d.logical_sector_size << " B sectors";
        if (show_serial && !d.serial.empty()) o << ", serial " << d.serial;
        o << "\n";
    };
    const char* title = r.kind == JobKind::create ? "image creation" : r.kind == JobKind::restore ? "image restore"
                        : r.kind == JobKind::clone ? "clone" : "image verification";
    o << "platter " << title << " report\n==========================\n\n";
    o << "Result:      " << verdict_name(r.verdict) << "  (operation " << operation_state_name(r.state) << ")\n";
    if (!r.failure.empty()) o << "Failure:     " << r.failure << "\n";
    if (r.source) dev_line("Source:      ", *r.source);
    if (r.target) dev_line(r.kind == JobKind::verify ? "Compared to: " : "Target:      ", *r.target);
    if (!r.image_path.empty()) {
        o << "Image:       " << r.image_path << " (" << compression_name(r.compression) << ", " << format_capacity(r.image_file_bytes) << " on disk)\n";
    }
    if (r.sectors) {
        o << "Region:      ";
        if (r.kind == JobKind::create || r.kind == JobKind::clone) o << "source LBA " << r.source_first_lba << " + " << r.sectors << " sectors";
        if (r.kind == JobKind::clone) o << " -> target LBA " << r.target_first_lba;
        if (r.kind == JobKind::restore || r.kind == JobKind::verify) o << r.sectors << " sectors at target LBA " << r.target_first_lba;
        o << " (" << format_capacity(r.bytes) << ")\n";
    }
    if (r.recover) o << "Mode:        recovery copy (unreadable sectors replaced by zeros)\n";
    if (!r.sha256.empty()) o << "SHA-256:     " << r.sha256 << "  (of the data, not the file)\n";
    if (!r.expected_sha256.empty())
        o << "Expected:    " << r.expected_sha256 << "  -> " << (r.sha_matches.value_or(false) ? "MATCH" : "MISMATCH") << "\n";
    if (r.readback_ok) o << "Read-back:   " << (*r.readback_ok ? "verified, identical" : "FAILED") << "\n";
    if (r.unreadable_sectors) {
        o << "Unreadable:  " << r.unreadable_sectors << " sector(s) in " << r.unreadable.size() << " range(s) (source LBAs)\n";
        const std::size_t show = std::min<std::size_t>(r.unreadable.size(), 20);
        for (std::size_t i = 0; i < show; ++i)
            o << "             " << r.unreadable[i].first << " - " << r.unreadable[i].last() << "\n";
        if (r.unreadable.size() > show) o << "             ... and " << (r.unreadable.size() - show) << " more range(s)\n";
    }
    if (!r.mismatches.empty()) {
        o << "Differences: " << r.mismatches.size() << " range(s) on the device (LBAs)\n";
        const std::size_t show = std::min<std::size_t>(r.mismatches.size(), 20);
        for (std::size_t i = 0; i < show; ++i) o << "             " << r.mismatches[i].first << " - " << r.mismatches[i].last() << "\n";
        if (r.mismatches.size() > show) o << "             ... and " << (r.mismatches.size() - show) << " more range(s)\n";
    }
    if (!r.started_utc.empty()) o << "Started:     " << r.started_utc << "  (" << static_cast<double>(r.duration_ms) / 1000.0 << " s)\n";
    for (const auto& w : r.warnings) o << "Note:        " << w << "\n";
    return o.str();
}

}  // namespace platter::imaging
