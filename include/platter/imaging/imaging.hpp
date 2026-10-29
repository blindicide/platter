// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Imaging and cloning (spec §11):
//   CreateImage   device  -> image file (raw or zstd), SHA-256 of the data,
//                 optional error-tolerant "recovery copy" with an unreadable map
//   RestoreImage  image   -> device     (destructive, authorized)
//   CloneDevice   device  -> device     (destructive, authorized)
//   VerifyImage   image   -> checksum, optionally compared against a device
//
// The source of a copy is only ever opened read-only. A recovery copy
// replaces sectors that cannot be read by zeros and lists them; it is not a
// substitute for a specialised data-recovery tool (no reverse passes, no
// resumable log).
#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/core/operation.hpp"
#include "platter/core/range.hpp"
#include "platter/core/safety.hpp"
#include "platter/imaging/image_file.hpp"

namespace platter::imaging {

enum class JobKind : std::uint8_t { create, restore, clone, verify };
std::string_view kind_name(JobKind k) noexcept;

enum class Verdict : std::uint8_t {
    ok,                  // finished, nothing unreadable, every requested check passed
    ok_with_unreadable,  // finished, but some source sectors were replaced by zeros
    check_failed,        // finished, but a checksum or read-back comparison failed
    incomplete,          // cancelled or failed
};
std::string_view verdict_name(Verdict v) noexcept;

struct ImagingResult {
    JobKind kind = JobKind::create;
    Verdict verdict = Verdict::incomplete;
    OperationState state = OperationState::created;
    std::string failure;

    std::optional<DeviceInfo> source;  // create / clone: what was read
    std::optional<DeviceInfo> target;  // restore / clone: what was written; verify: what was compared
    std::string image_path;
    Compression compression = Compression::none;
    std::uint64_t image_file_bytes = 0;  // size of the image file

    std::uint32_t sector_size = 512;
    std::uint64_t source_first_lba = 0;
    std::uint64_t target_first_lba = 0;
    std::uint64_t sectors = 0;
    std::uint64_t bytes = 0;
    std::uint32_t block_bytes = 0;
    bool recover = false;

    std::string sha256;                   // of the data stream (zeros for unreadable sectors)
    std::string expected_sha256;          // supplied by the user / sidecar, if any
    std::optional<bool> sha_matches;      // sha256 == expected_sha256
    std::optional<bool> readback_ok;      // written data read back and compared (or image re-read)
    std::vector<LbaRange> unreadable;     // source LBAs replaced by zeros
    std::uint64_t unreadable_sectors = 0;
    std::vector<LbaRange> mismatches;     // absolute LBAs on the target / compared device that differ

    std::string started_utc, finished_utc;
    std::uint64_t duration_ms = 0;
    std::vector<std::string> warnings;
};

constexpr std::uint32_t kDefaultBlockBytes = 1u << 20;

// ------------------------------------------------------------------ create
struct ImageConfig {
    std::string output;
    Compression compression = Compression::none;
    int level = 3;
    bool sparse = false;
    bool overwrite = false;
    bool write_sidecar = true;       // "<output>.sha256"
    bool verify_image = true;        // re-read the finished image and compare its checksum
    std::uint32_t block_bytes = kDefaultBlockBytes;
    std::uint64_t start_lba = 0;
    std::uint64_t end_lba = UINT64_MAX;  // inclusive; UINT64_MAX = last sector
    bool recover = false;                // continue past unreadable sectors
    std::uint32_t retries = 2;           // extra attempts per failing block / sector
    std::uint64_t max_unreadable_sectors = 0;  // 0 = no limit
};

class CreateImage final : public Operation {
public:
    // `source` must be a read-only handle.
    CreateImage(BlockDevicePtr source, ImageConfig config);
    ~CreateImage() override;
    ImagingResult result() const;

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    BlockDevicePtr src_;
    ImageConfig cfg_;
    DeviceInfo info_;
    LbaRange range_;
    mutable std::mutex m_;
    ImagingResult r_;
};

// ----------------------------------------------------------------- restore
struct RestoreConfig {
    std::string input;
    std::uint32_t block_bytes = kDefaultBlockBytes;
    bool verify = true;                 // read the written data back and compare
    std::string expect_sha256;          // if set, the image is hashed first and must match
};

class RestoreImage final : public Operation {
public:
    // The authorization must be kind restore_image with exactly one range:
    // [first, ceil(image size / sector size)).
    RestoreImage(safety::AuthorizedTarget target, RestoreConfig config);
    ~RestoreImage() override;
    ImagingResult result() const;
    // Sector count the authorization has to cover (reads the image header).
    static Result<std::uint64_t> sectors_needed(const std::string& image, std::uint32_t sector_size);

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    safety::AuthorizedTarget target_;
    RestoreConfig cfg_;
    std::uint64_t image_bytes_ = 0;
    mutable std::mutex m_;
    ImagingResult r_;
};

// ------------------------------------------------------------------- clone
struct CloneConfig {
    std::uint32_t block_bytes = kDefaultBlockBytes;
    std::uint64_t source_first_lba = 0;
    bool recover = false;
    std::uint32_t retries = 2;
    std::uint64_t max_unreadable_sectors = 0;
    bool verify = true;
};

class CloneDevice final : public Operation {
public:
    // `source` is read-only. The authorization (kind clone_target) has exactly
    // one range: where on the target the copy goes; its length is the number of
    // sectors copied from `source_first_lba`.
    CloneDevice(BlockDevicePtr source, safety::AuthorizedTarget target, CloneConfig config);
    ~CloneDevice() override;
    ImagingResult result() const;

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    BlockDevicePtr src_;
    safety::AuthorizedTarget target_;
    CloneConfig cfg_;
    mutable std::mutex m_;
    ImagingResult r_;
};

// ------------------------------------------------------------------ verify
struct VerifyConfig {
    std::string input;
    std::string expect_sha256;            // optional
    BlockDevicePtr compare_to;            // optional, read-only
    std::uint64_t compare_first_lba = 0;  // where the image starts on that device
    std::uint32_t block_bytes = kDefaultBlockBytes;
};

class VerifyImage final : public Operation {
public:
    explicit VerifyImage(VerifyConfig config);
    ~VerifyImage() override;
    ImagingResult result() const;

protected:
    Status do_validate() override;
    Status do_run(OperationControl& control) override;

private:
    VerifyConfig cfg_;
    std::uint64_t image_bytes_ = 0;
    mutable std::mutex m_;
    ImagingResult r_;
};

// ------------------------------------------------------------------- text
std::string result_to_text(const ImagingResult& r, bool show_serial = false);

}  // namespace platter::imaging
