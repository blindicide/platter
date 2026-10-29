// SPDX-License-Identifier: TBD
#include "platter/core/safety.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>

#include "platter/platform/enumerate.hpp"

namespace fs = std::filesystem;

namespace platter::safety {

std::string_view kind_name(Kind k) noexcept {
    switch (k) {
        case Kind::erase: return "erase";
        case Kind::write_test: return "write-test";
        case Kind::partition_edit: return "partition-edit";
        case Kind::filesystem_create: return "filesystem-create";
        case Kind::restore_image: return "restore-image";
        case Kind::clone_target: return "clone-target";
        case Kind::qualification: return "qualification";
    }
    return "?";
}

// ------------------------------------------------------------ fingerprint

Fingerprint Fingerprint::of(const DeviceInfo& d) {
    Fingerprint f;
    f.stable_id = d.stable_id;
    f.serial = d.serial;
    f.vendor = d.vendor;
    f.model = d.model;
    f.capacity_bytes = d.capacity_bytes;
    f.logical_sector_size = d.logical_sector_size;
    f.physical_sector_size = d.physical_sector_size;
    f.bus = d.bus;
    return f;
}

bool Fingerprint::matches(const Fingerprint& o) const noexcept {
    return stable_id == o.stable_id && serial == o.serial && vendor == o.vendor && model == o.model &&
           capacity_bytes == o.capacity_bytes && logical_sector_size == o.logical_sector_size &&
           physical_sector_size == o.physical_sector_size && bus == o.bus;
}

std::string Fingerprint::describe() const {
    std::string s = model.empty() ? "(unknown model)" : model;
    s += ", " + format_capacity(capacity_bytes);
    s += ", " + std::to_string(logical_sector_size) + "/" + std::to_string(physical_sector_size) + " B sectors";
    s += ", " + std::string(bus_type_name(bus));
    return s;
}

// ------------------------------------------------------------------ audit

namespace {

std::mutex g_sink_mutex;
AuditSink g_sink;

void emit(AuditRecord r) {
    AuditSink sink;
    {
        std::lock_guard lk(g_sink_mutex);
        sink = g_sink;
    }
    r.time = std::chrono::system_clock::now();
    if (sink) sink(r);
}

std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::vector<std::string> confirmation_tokens(const DeviceInfo& d) {
    std::vector<std::string> t;
    if (d.bus == BusType::image) {
        t.push_back(d.name);
        return t;
    }
    const auto model = trim(d.model);
    if (!model.empty()) t.push_back(model);
    const auto serial = trim(d.serial);
    if (serial.size() >= 4) t.push_back(serial.substr(serial.size() - 4));
    if (t.empty()) t.push_back(d.name);
    return t;
}

}  // namespace

void set_audit_sink(AuditSink sink) {
    std::lock_guard lk(g_sink_mutex);
    g_sink = std::move(sink);
}

bool Preview::confirmation_ok(std::string_view typed) const {
    const auto t = trim(typed);
    if (t.empty()) return false;
    return std::any_of(accepted_confirmations.begin(), accepted_confirmations.end(),
                       [&](const std::string& a) { return iequals(a, t); });
}

// ---------------------------------------------------------------- preview

Result<DeviceInfo> describe_target(const Target& t) {
    if (t.is_image) {
        std::error_code ec;
        if (!fs::is_regular_file(t.path, ec)) return Error{Errc::not_found, "image file not found: " + t.path};
        const auto canon = fs::weakly_canonical(t.path, ec);
        const std::string path = ec ? t.path : canon.string();
        auto dev = open_image(path, {}, t.image_sector_size);
        if (!dev) return dev.error();
        DeviceInfo info = (*dev)->info();
        info.path = path;
        return info;
    }
    return describe_device(t.path);
}

std::string preview_text(const Preview& p, bool show_serial) {
    const auto& d = p.device;
    std::string o;
    o += "Target:      " + d.path + "\n";
    o += "Device:      " + (d.model.empty() ? std::string("(unknown model)") : d.model) + ", " + format_capacity(d.capacity_bytes) +
         ", " + std::string(bus_type_name(d.bus)) + "\n";
    if (show_serial && !d.serial.empty()) o += "Serial:      " + d.serial + "\n";
    o += "Sectors:     " + std::to_string(d.logical_sector_size) + " B logical / " + std::to_string(d.physical_sector_size) +
         " B physical\n";
    o += "Operation:   " + p.request.summary + "\n";
    if (p.whole_device) {
        o += "Range:       WHOLE DEVICE (" + std::to_string(d.total_lbas()) + " sectors, " + format_capacity(p.affected_bytes) + ")\n";
    } else {
        for (const auto& r : p.ranges)
            o += "Range:       LBA " + std::to_string(r.first) + " - " + std::to_string(r.last()) + " (" + std::to_string(r.count) +
                 " sectors)\n";
        o += "Affected:    " + format_capacity(p.affected_bytes) + "\n";
    }
    for (const auto& w : p.warnings) o += "Warning:     " + w + "\n";
    for (const auto& b : p.blockers) o += "REFUSED:     " + b + "\n";
    return o;
}

Result<Preview> make_preview(const Request& req) {
    auto described = describe_target(req.target);
    if (!described) return described.error();
    return preview_for(*described, req);
}

Preview preview_for(const DeviceInfo& device, const Request& req) {
    Preview p;
    p.request = req;
    p.device = device;
    const DeviceInfo& d = p.device;
    p.fingerprint = Fingerprint::of(d);

    const auto total = d.total_lbas();
    if (req.ranges.empty()) {
        p.ranges.push_back(LbaRange{0, total});
        p.whole_device = true;
    } else {
        p.ranges = req.ranges;
    }

    std::uint64_t bytes = 0;
    for (const auto& r : p.ranges) {
        if (r.empty()) {
            p.blockers.push_back("an empty LBA range was requested");
            continue;
        }
        if (auto s = validate_lba_range(r, total); !s) {
            p.blockers.push_back("range " + std::to_string(r.first) + "+" + std::to_string(r.count) +
                                 " lies outside the device (" + std::to_string(total) + " sectors)");
            continue;
        }
        auto span = to_bytes(r, d.logical_sector_size);
        auto sum = span ? checked_add(bytes, span->length) : std::nullopt;
        if (!sum) {
            p.blockers.push_back("range size overflows");
            continue;
        }
        bytes = *sum;
    }
    p.affected_bytes = bytes;
    if (!p.whole_device && p.ranges.size() == 1 && p.ranges[0].first == 0 && p.ranges[0].count == total)
        p.whole_device = true;

    // ---- blockers: any of these makes authorize() refuse
    if (d.capacity_bytes == 0) p.blockers.push_back("the device reports no capacity");
    if (d.bus != BusType::image) {
        if (d.read_only == Tri::yes) p.blockers.push_back("the device is read-only (hardware switch or OS flag)");
        if (!d.capabilities.has(Capability::write))
            p.blockers.push_back("writing is not available for this device (permissions or read-only media)");
        if (d.is_system_disk) p.blockers.push_back("this is the system disk");
        for (const auto& m : d.mountpoints) p.blockers.push_back("a volume is mounted at " + m);
        for (const auto& h : d.holders) p.blockers.push_back("in use by " + h + " (volume manager / encryption / RAID)");
    }

    // ---- warnings: shown, never blocking
    if (d.bus == BusType::image) {
        p.warnings.push_back("Target is an image file, not a physical device.");
    } else {
        p.warnings.push_back("Data in the selected range of " + p.fingerprint.describe() +
                             " will be destroyed and cannot be recovered.");
        if (d.media == MediaKind::solid_state || d.bus == BusType::nvme || d.bus == BusType::mmc ||
            d.bus == BusType::usb)
            p.warnings.push_back(
                "Flash storage remaps and over-provisions blocks, so overwriting cannot reach every cell. "
                "For sensitive data use the drive's secure-erase / sanitize command or encryption.");
        if (d.removable == Tri::yes) p.warnings.push_back("This is a removable device: check it is the one you mean.");
        if (p.fingerprint.weak())
            p.warnings.push_back(
                "The device exposes neither a serial number nor a stable id; an identical drive could not be told apart.");
    }

    p.accepted_confirmations = confirmation_tokens(d);
    if (d.bus == BusType::image) {
        p.prompt = "To confirm, type the image file name: " + p.accepted_confirmations.front();
    } else {
        p.prompt = "To confirm, type the model name";
        if (!trim(d.model).empty()) p.prompt += " (" + trim(d.model) + ")";
        const auto serial = trim(d.serial);
        if (serial.size() >= 4) p.prompt += " or the last 4 characters of the serial number (" + serial.substr(serial.size() - 4) + ")";
        if (trim(d.model).empty() && serial.size() < 4) p.prompt = "To confirm, type the device name: " + d.name;
    }
    return p;
}

// -------------------------------------------------------------- authorize

AuthorizedTarget::AuthorizedTarget(BlockDevicePtr dev, DeviceLeaseRegistry::Lease lease, Preview preview,
                                   bool direct, std::vector<std::string> warnings)
    : dev_(std::move(dev)),
      lease_(std::move(lease)),
      preview_(std::move(preview)),
      kind_(preview_.request.kind),
      direct_(direct),
      warnings_(std::move(warnings)) {}

AuthorizedTarget::AuthorizedTarget(AuthorizedTarget&&) noexcept = default;
AuthorizedTarget& AuthorizedTarget::operator=(AuthorizedTarget&&) noexcept = default;
AuthorizedTarget::~AuthorizedTarget() = default;

bool AuthorizedTarget::allows(std::uint64_t offset, std::uint64_t length) const noexcept {
    const auto end = checked_add(offset, length);
    if (!end || length == 0) return false;
    for (const auto& r : preview_.ranges) {
        auto span = to_bytes(r, dev_->sector_size());
        if (!span) continue;
        const auto span_end = checked_add(span->offset, span->length);
        if (span_end && offset >= span->offset && *end <= *span_end) return true;
    }
    return false;
}

Result<IOResult> AuthorizedTarget::write(std::uint64_t offset, const void* buffer, std::size_t length) {
    if (!allows(offset, length))
        return Error{Errc::refused, "write at byte offset " + std::to_string(offset) + " (" + std::to_string(length) +
                                        " bytes) is outside the confirmed range"};
    return dev_->write(offset, buffer, length);
}

Status AuthorizedTarget::discard(std::uint64_t offset, std::uint64_t length) {
    if (!allows(offset, length))
        return Error{Errc::refused, "discard at byte offset " + std::to_string(offset) + " is outside the confirmed range"};
    return dev_->discard(offset, length);
}

Result<AuthorizedTarget> authorize(const Preview& shown, std::string_view typed_confirmation,
                                   const AuthorizeOptions& opts) {
    auto refuse = [&](const std::string& why) -> Error {
        AuditRecord r;
        r.event = "refused";
        r.kind = shown.request.kind;
        r.device_path = shown.device.path;
        r.model = shown.device.model;
        r.capacity_bytes = shown.device.capacity_bytes;
        r.affected_bytes = shown.affected_bytes;
        r.detail = why;
        emit(std::move(r));
        return Error{Errc::refused, why};
    };

    if (!shown.allowed()) return refuse("the operation is blocked: " + shown.blockers.front());
    if (!shown.confirmation_ok(typed_confirmation)) return refuse("the confirmation text does not match");

    // Everything is re-observed from scratch: the preview may be stale.
    auto fresh = make_preview(shown.request);
    if (!fresh) return refuse("the device could not be re-examined: " + fresh.error().describe());
    if (!fresh->allowed()) return refuse("the operation is now blocked: " + fresh->blockers.front());
    if (!fresh->fingerprint.matches(shown.fingerprint))
        return refuse("the device is not the one that was previewed (identity changed: now " +
                      fresh->fingerprint.describe() + ")");
    if (fresh->ranges != shown.ranges) return refuse("the confirmed range no longer matches the device");

    DeviceLeaseRegistry& registry = opts.registry ? *opts.registry : DeviceLeaseRegistry::global();
    auto lease = registry.acquire(fresh->device.path, AccessKind::exclusive);
    if (!lease) return refuse(lease.error().describe());

    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    oo.exclusive = true;
    {
        // The confirmed spans, in bytes, for a privileged helper to enforce.
        const std::uint64_t ss = fresh->device.logical_sector_size ? fresh->device.logical_sector_size : 512;
        for (const auto& r : fresh->ranges) {
            if (r.count == 0) continue;
            oo.write_ranges.push_back(ByteSpan{r.first * ss, r.count * ss});
        }
    }
    std::vector<std::string> warnings;
    auto open = [&](bool direct) {
        oo.direct_io = direct;
        return shown.request.target.is_image
                   ? open_image(fresh->device.path, oo, shown.request.target.image_sector_size)
                   : open_device(fresh->device.path, oo);
    };
    bool direct = opts.direct_io;
    auto dev = open(direct);
    if (!dev && direct && dev.error().code == Errc::invalid_argument) {
        direct = false;
        warnings.push_back("Direct I/O is unavailable here; writes and verification go through the OS cache.");
        dev = open(false);
    }
    if (!dev) {
        auto e = dev.error();
        AuditRecord r;
        r.event = "refused";
        r.kind = shown.request.kind;
        r.device_path = fresh->device.path;
        r.model = fresh->device.model;
        r.capacity_bytes = fresh->device.capacity_bytes;
        r.detail = "could not open exclusively for writing: " + e.describe();
        emit(std::move(r));
        return e;
    }

    // Last check, on the handle that will do the writing.
    if (!Fingerprint::of((*dev)->info()).matches(shown.fingerprint))
        return refuse("the opened device does not match the previewed identity (" +
                      Fingerprint::of((*dev)->info()).describe() + ")");

    AuditRecord r;
    r.event = "authorized";
    r.kind = shown.request.kind;
    r.device_path = fresh->device.path;
    r.model = fresh->device.model;
    r.capacity_bytes = fresh->device.capacity_bytes;
    r.affected_bytes = fresh->affected_bytes;
    r.detail = shown.request.summary;
    emit(std::move(r));

    return AuthorizedTarget(std::move(dev).value(), std::move(lease).value(), shown, direct, std::move(warnings));
}

}  // namespace platter::safety
