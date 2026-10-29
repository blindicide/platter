// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Windows device discovery. Every handle opened here uses ZERO access rights
// (dwDesiredAccess = 0): enough for metadata IOCTLs, unable to read or write
// a single sector. Requires no elevation for basic information.
#include <windows.h>
#include <winioctl.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <optional>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/platform/enumerate.hpp"
#include "platter/platform/direct_open.hpp"
#include "../file_device.hpp"
#include "win32_util.hpp"

namespace platter {
namespace {

using win32::Handle;

constexpr int kMaxPhysicalDrives = 64;

std::string drive_path(int n) { return "\\\\.\\PhysicalDrive" + std::to_string(n); }

Handle open_meta(const std::wstring& path) {
    return Handle(CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

std::string descriptor_string(const std::vector<BYTE>& buf, DWORD offset) {
    if (offset == 0 || offset >= buf.size()) return {};
    const char* p = reinterpret_cast<const char*>(buf.data() + offset);
    const std::size_t max = buf.size() - offset;
    return trim(std::string(p, strnlen(p, max)));
}

template <typename T>
bool query_property(HANDLE h, STORAGE_PROPERTY_ID id, T& out) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = id;
    q.QueryType = PropertyStandardQuery;
    DWORD ret = 0;
    return DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, &out, sizeof out, &ret, nullptr) &&
           ret >= sizeof(DWORD) * 2;
}

std::vector<BYTE> query_property_blob(HANDLE h, STORAGE_PROPERTY_ID id) {
    STORAGE_PROPERTY_QUERY q{};
    q.PropertyId = id;
    q.QueryType = PropertyStandardQuery;
    STORAGE_DESCRIPTOR_HEADER hdr{};
    DWORD ret = 0;
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, &hdr, sizeof hdr, &ret, nullptr) ||
        hdr.Size == 0 || hdr.Size > 64 * 1024)
        return {};
    std::vector<BYTE> buf(hdr.Size);
    if (!DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY, &q, sizeof q, buf.data(), static_cast<DWORD>(buf.size()),
                         &ret, nullptr))
        return {};
    buf.resize(ret);
    return buf;
}

BusType map_bus(STORAGE_BUS_TYPE b) {
    switch (static_cast<int>(b)) {
        case BusTypeAta:
        case BusTypeSata: return BusType::ata;
        case BusTypeScsi:
        case BusTypeSas:
        case BusTypeiScsi:
        case BusTypeFibre: return BusType::scsi;
        case 17 /* BusTypeNvme */: return BusType::nvme;
        case BusTypeUsb: return BusType::usb;
        case BusTypeSd:
        case BusTypeMmc: return BusType::mmc;
        case BusTypeVirtual:
        case BusTypeFileBackedVirtual: return BusType::loop;
        case BusTypeRAID:
        case 16 /* BusTypeSpaces */: return BusType::raid;
        default: return BusType::unknown;
    }
}

// Device identification VPD (page 0x83) via StorageDeviceIdProperty: prefer a
// NAA/EUI-64 identifier for a stable id that survives drive-number changes.
std::string stable_id_of(HANDLE h) {
    auto blob = query_property_blob(h, StorageDeviceIdProperty);
    if (blob.size() < sizeof(STORAGE_DEVICE_ID_DESCRIPTOR)) return {};
    const auto* d = reinterpret_cast<const STORAGE_DEVICE_ID_DESCRIPTOR*>(blob.data());
    const BYTE* p = d->Identifiers;
    const BYTE* end = blob.data() + blob.size();
    for (DWORD i = 0; i < d->NumberOfIdentifiers; ++i) {
        if (p + sizeof(STORAGE_IDENTIFIER) > end) break;
        const auto* id = reinterpret_cast<const STORAGE_IDENTIFIER*>(p);
        const BYTE* data = id->Identifier;
        if (data + id->IdentifierSize > end) break;
        if (id->Association == StorageIdAssocDevice &&
            (id->Type == StorageIdTypeFCPHName || id->Type == StorageIdTypeEUI64) && id->IdentifierSize >= 8 &&
            id->CodeSet == StorageIdCodeSetBinary) {
            std::string s = id->Type == StorageIdTypeFCPHName ? "wwn-0x" : "eui.";
            char hex[3];
            for (WORD k = 0; k < id->IdentifierSize; ++k) {
                std::snprintf(hex, sizeof hex, "%02x", data[k]);
                s += hex;
            }
            return s;
        }
        if (id->NextOffset == 0) break;
        p += id->NextOffset;
    }
    return {};
}

// Maps PhysicalDrive number -> drive letters ("C:\") of volumes that live on it.
std::map<DWORD, std::vector<std::string>> volume_map() {
    std::map<DWORD, std::vector<std::string>> out;
    wchar_t drives[512];
    const DWORD n = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(drives)), drives);
    if (n == 0 || n > std::size(drives)) return out;
    for (const wchar_t* d = drives; *d; d += wcslen(d) + 1) {
        const UINT type = GetDriveTypeW(d);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        std::wstring vol = L"\\\\.\\";
        vol += d[0];
        vol += L':';
        Handle h = open_meta(vol);
        if (!h.valid()) continue;
        alignas(8) BYTE buf[sizeof(VOLUME_DISK_EXTENTS) + 16 * sizeof(DISK_EXTENT)];
        DWORD ret = 0;
        if (!DeviceIoControl(h.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0, buf, sizeof buf, &ret, nullptr))
            continue;
        const auto* ext = reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buf);
        for (DWORD i = 0; i < ext->NumberOfDiskExtents && i < 17; ++i)
            out[ext->Extents[i].DiskNumber].push_back(win32::narrow(std::wstring(d)));
    }
    return out;
}

std::string system_drive_root() {
    wchar_t dir[MAX_PATH];
    const UINT n = GetSystemWindowsDirectoryW(dir, MAX_PATH);
    if (n < 3 || n >= MAX_PATH) return {};
    return win32::narrow(std::wstring(dir, 3));  // "C:\"
}

Result<DeviceInfo> describe_number(int n, const std::map<DWORD, std::vector<std::string>>& vols,
                                   const std::string& sysroot) {
    const auto path = drive_path(n);
    Handle h = open_meta(win32::widen(path));
    if (!h.valid()) return win32::last_error("open " + path);

    DeviceInfo d;
    d.path = path;
    d.name = "PhysicalDrive" + std::to_string(n);

    auto desc = query_property_blob(h.get(), StorageDeviceProperty);
    if (desc.size() >= sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        const auto* sd = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR*>(desc.data());
        d.vendor = descriptor_string(desc, sd->VendorIdOffset);
        d.model = descriptor_string(desc, sd->ProductIdOffset);
        d.firmware = descriptor_string(desc, sd->ProductRevisionOffset);
        d.serial = descriptor_string(desc, sd->SerialNumberOffset);
        d.removable = sd->RemovableMedia ? Tri::yes : Tri::no;
        d.bus = map_bus(sd->BusType);
    }

    alignas(8) BYTE geo_buf[sizeof(DISK_GEOMETRY_EX) + 256]{};
    DWORD ret = 0;
    if (DeviceIoControl(h.get(), IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, nullptr, 0, geo_buf, sizeof geo_buf, &ret, nullptr)) {
        const auto* g = reinterpret_cast<const DISK_GEOMETRY_EX*>(geo_buf);
        d.capacity_bytes = static_cast<std::uint64_t>(g->DiskSize.QuadPart);
        if (g->Geometry.BytesPerSector) d.logical_sector_size = g->Geometry.BytesPerSector;
    }
    d.physical_sector_size = d.logical_sector_size;
    STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR align{};
    if (query_property(h.get(), StorageAccessAlignmentProperty, align) && align.BytesPerPhysicalSector)
        d.physical_sector_size = align.BytesPerPhysicalSector;

    DEVICE_SEEK_PENALTY_DESCRIPTOR seek{};
    if (query_property(h.get(), StorageDeviceSeekPenaltyProperty, seek))
        d.media = seek.IncursSeekPenalty ? MediaKind::rotational : MediaKind::solid_state;
    DEVICE_TRIM_DESCRIPTOR trim_desc{};
    if (query_property(h.get(), StorageDeviceTrimProperty, trim_desc))
        d.trim_supported = trim_desc.TrimEnabled ? Tri::yes : Tri::no;

    // IOCTL_DISK_IS_WRITABLE: success => writable, ERROR_WRITE_PROTECT => read-only.
    if (DeviceIoControl(h.get(), IOCTL_DISK_IS_WRITABLE, nullptr, 0, nullptr, 0, &ret, nullptr))
        d.read_only = Tri::no;
    else if (GetLastError() == ERROR_WRITE_PROTECT)
        d.read_only = Tri::yes;

    d.stable_id = stable_id_of(h.get());

    if (auto it = vols.find(static_cast<DWORD>(n)); it != vols.end()) {
        d.mountpoints = it->second;
        std::sort(d.mountpoints.begin(), d.mountpoints.end());
        d.mountpoints.erase(std::unique(d.mountpoints.begin(), d.mountpoints.end()), d.mountpoints.end());
        for (const auto& mp : d.mountpoints)
            if (!sysroot.empty() && _stricmp(mp.c_str(), sysroot.c_str()) == 0) d.is_system_disk = true;
    }
    // TODO(v0.8): also flag the disk holding the EFI System Partition / boot
    // volume (IOCTL_DISK_GET_DRIVE_LAYOUT_EX + GetFirmwareEnvironmentVariable).

    d.capabilities.set(Capability::read);
    d.capabilities.set(Capability::benchmark);
    d.capabilities.set(Capability::raw_sector_access);
    if (d.read_only != Tri::yes) {
        d.capabilities.set(Capability::write);
        d.capabilities.set(Capability::partition_edit);
    }
    if (d.trim_supported == Tri::yes) d.capabilities.set(Capability::trim);
    return d;
}

std::optional<int> parse_drive_number(const std::string& path) {
    static const std::string prefixes[] = {"\\\\.\\PhysicalDrive", "\\\\?\\PhysicalDrive", "PhysicalDrive"};
    for (const auto& pre : prefixes) {
        if (path.size() <= pre.size() || _strnicmp(path.c_str(), pre.c_str(), pre.size()) != 0) continue;
        const auto digits = path.substr(pre.size());
        if (digits.empty() || digits.size() > 3 ||
            !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
            return std::nullopt;
        return std::stoi(digits);
    }
    return std::nullopt;
}

}  // namespace

Result<std::vector<DeviceInfo>> enumerate_devices() {
    const auto vols = volume_map();
    const auto sysroot = system_drive_root();
    std::vector<DeviceInfo> out;
    for (int n = 0; n < kMaxPhysicalDrives; ++n) {
        auto d = describe_number(n, vols, sysroot);
        if (d) {
            out.push_back(std::move(d).value());
        } else if (d.error().code != Errc::not_found) {
            // Keep a placeholder so the user sees that drive N exists but was unreadable.
            DeviceInfo stub;
            stub.path = drive_path(n);
            stub.name = "PhysicalDrive" + std::to_string(n);
            stub.model = "(" + d.error().describe() + ")";
            out.push_back(std::move(stub));
        }
    }
    return out;
}

Result<DeviceInfo> describe_device(const std::string& path) {
    auto n = parse_drive_number(path);
    if (!n) return Error{Errc::invalid_argument, "'" + path + "' is not a \\\\.\\PhysicalDriveN path"};
    return describe_number(*n, volume_map(), system_drive_root());
}

Result<BlockDevicePtr> open_device_direct(const std::string& path, const OpenOptions& opts) {
    auto info = describe_device(path);
    if (!info) return info.error();
    auto d = std::move(info).value();
    if (opts.mode == OpenMode::read_write && d.read_only == Tri::yes)
        return Error{Errc::read_only, d.path + " is write-protected"};
    const auto p = d.path;
    return detail::open_file_device(p, opts, std::move(d), /*is_image=*/false);
}

}  // namespace platter
