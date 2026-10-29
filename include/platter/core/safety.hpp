// SPDX-License-Identifier: LicenseRef-Proprietary
//
// The destructive-operation gate (docs/safety.md, spec §17).
//
// Nothing in platter may write to a device except through an
// AuthorizedTarget, and the only way to get one is authorize():
//
//   Request --make_preview()--> Preview (shown to the user, with blockers)
//   Preview + typed confirmation --authorize()--> AuthorizedTarget
//
// authorize() re-describes the device from scratch, refuses if anything the
// user was shown has changed, takes an exclusive lease, opens the device
// read-write and exclusively, and re-checks the identity of the handle that
// will actually do the writing. The resulting object can only write inside
// the ranges that were confirmed.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/core/operation.hpp"
#include "platter/core/range.hpp"

namespace platter::safety {

enum class Kind : std::uint8_t {
    erase,
    write_test,         // surface-scan write modes, write benchmarks
    partition_edit,
    filesystem_create,
    restore_image,
    clone_target,
    qualification,
};
std::string_view kind_name(Kind k) noexcept;

// What the user picked. For images `path` is the file; for devices the node.
struct Target {
    std::string path;
    bool is_image = false;
    std::uint32_t image_sector_size = 512;
};

// Identity of a device (spec §17.2). A path alone is never identity.
struct Fingerprint {
    std::string stable_id;
    std::string serial;
    std::string vendor;
    std::string model;
    std::uint64_t capacity_bytes = 0;
    std::uint32_t logical_sector_size = 0;
    std::uint32_t physical_sector_size = 0;
    BusType bus = BusType::unknown;

    static Fingerprint of(const DeviceInfo& d);
    bool matches(const Fingerprint& other) const noexcept;
    // True when neither a stable id nor a serial is available, i.e. an
    // identical device could not be told apart.
    bool weak() const noexcept { return stable_id.empty() && serial.empty() && bus != BusType::image; }
    std::string describe() const;  // never includes the serial
};

#ifndef PLATTER_BACKEND_DISMOUNTS_VOLUMES
#ifdef _WIN32
#define PLATTER_BACKEND_DISMOUNTS_VOLUMES true
#else
#define PLATTER_BACKEND_DISMOUNTS_VOLUMES false
#endif
#endif

struct Request {
    Kind kind = Kind::erase;
    Target target;
    // Byte-addressed work is expressed in LBAs of the device's logical sector
    // size. Empty = the whole device.
    std::vector<LbaRange> ranges;
    // One line shown in the preview: "Overwrite with zeros (1 pass), verify".
    std::string summary;
    // True when this platform's backend locks and dismounts the volumes of a
    // disk itself when it opens it for writing (Windows does; see
    // lock_volumes_of_disk). Mounted volumes then need an explicit
    // acknowledgment instead of being a hard refusal. Where nothing dismounts
    // them (Linux), a mounted volume stays a blocker.
    bool backend_dismounts_volumes = PLATTER_BACKEND_DISMOUNTS_VOLUMES;
};

struct Preview {
    Request request;
    DeviceInfo device;                 // as observed when the preview was made
    Fingerprint fingerprint;
    std::vector<LbaRange> ranges;      // resolved: never empty
    std::uint64_t affected_bytes = 0;
    bool whole_device = false;

    // Any one of these, typed exactly (ignoring case and surrounding spaces),
    // confirms the operation.
    std::vector<std::string> accepted_confirmations;
    std::string prompt;                // human-readable instruction

    std::vector<std::string> blockers; // non-empty => authorize() will refuse
    std::vector<std::string> warnings;
    // Mounted volumes that the backend will lock and dismount. Not blockers,
    // but authorize() refuses unless AuthorizeOptions::acknowledge_dismount is
    // set, which a front end may do only after showing them to the user.
    std::vector<std::string> mounted;

    bool allowed() const noexcept { return blockers.empty(); }
    bool needs_dismount_ack() const noexcept { return !mounted.empty(); }
    bool confirmation_ok(std::string_view typed) const;
};

Result<DeviceInfo> describe_target(const Target& t);

// Pure function of its inputs: derives blockers, warnings and the confirmation
// prompt for an already-described device. make_preview() = describe + this.
Preview preview_for(const DeviceInfo& device, const Request& req);

Result<Preview> make_preview(const Request& req);

// Multi-line, human-readable rendering of everything the user is asked to
// confirm (shared by the CLI and the GUI). Serials only if show_serial.
std::string preview_text(const Preview& p, bool show_serial);

struct AuthorizeOptions {
    // Try O_DIRECT / NO_BUFFERING; falls back to buffered with a warning.
    bool direct_io = true;
    // The user has seen Preview::mounted and agreed that those volumes are
    // locked and dismounted (open files in them are lost).
    bool acknowledge_dismount = false;
    DeviceLeaseRegistry* registry = nullptr;  // null = DeviceLeaseRegistry::global()
};

class AuthorizedTarget;
Result<AuthorizedTarget> authorize(const Preview& shown, std::string_view typed_confirmation,
                                   const AuthorizeOptions& opts = {});

struct TestAccess;  // defined only by the test suite

// A read-write device that has passed every check. Move-only; it owns the
// exclusive lease for as long as it lives.
class AuthorizedTarget {
public:
    AuthorizedTarget(AuthorizedTarget&&) noexcept;
    AuthorizedTarget& operator=(AuthorizedTarget&&) noexcept;
    AuthorizedTarget(const AuthorizedTarget&) = delete;
    AuthorizedTarget& operator=(const AuthorizedTarget&) = delete;
    ~AuthorizedTarget();

    Kind kind() const noexcept { return kind_; }
    const DeviceInfo& info() const noexcept { return dev_->info(); }
    const Preview& preview() const noexcept { return preview_; }
    const std::vector<LbaRange>& ranges() const noexcept { return preview_.ranges; }
    const std::vector<std::string>& warnings() const noexcept { return warnings_; }
    std::uint32_t sector_size() const noexcept { return dev_->sector_size(); }
    std::uint32_t required_alignment() const noexcept { return dev_->required_alignment(); }
    bool direct_io() const noexcept { return direct_; }
    // Lease / operation key: the device path.
    const std::string& device_id() const noexcept { return dev_->info().path; }

    // True if [offset, offset+length) lies entirely inside one confirmed range.
    bool allows(std::uint64_t offset, std::uint64_t length) const noexcept;

    // Writes are refused (Errc::refused) outside the confirmed ranges.
    Result<IOResult> write(std::uint64_t offset, const void* buffer, std::size_t length);
    Status discard(std::uint64_t offset, std::uint64_t length);
    Status flush() { return dev_->flush(); }
    // Best-effort: ask the OS to re-read the partition table.
    Status reread_partition_table() { return dev_->reread_partitions(); }
    // Reads are allowed anywhere (partition tables, verification).
    Result<IOResult> read(std::uint64_t offset, void* buffer, std::size_t length) {
        return dev_->read(offset, buffer, length);
    }

private:
    friend Result<AuthorizedTarget> authorize(const Preview&, std::string_view, const AuthorizeOptions&);
    friend struct TestAccess;
    AuthorizedTarget(BlockDevicePtr dev, DeviceLeaseRegistry::Lease lease, Preview preview, bool direct,
                     std::vector<std::string> warnings);

    BlockDevicePtr dev_;
    DeviceLeaseRegistry::Lease lease_;
    Preview preview_;
    Kind kind_;
    bool direct_ = false;
    std::vector<std::string> warnings_;
};

// ------------------------------------------------------------------ audit

// Every authorization (and refusal) is reported to the audit sink so front
// ends can write a durable log. The record never contains a serial number.
struct AuditRecord {
    std::chrono::system_clock::time_point time;
    std::string event;       // "authorized" | "refused"
    Kind kind = Kind::erase;
    std::string device_path;
    std::string model;
    std::uint64_t capacity_bytes = 0;
    std::uint64_t affected_bytes = 0;
    std::string detail;      // the request summary, or the refusal reason
};
using AuditSink = std::function<void(const AuditRecord&)>;
void set_audit_sink(AuditSink sink);

}  // namespace platter::safety
