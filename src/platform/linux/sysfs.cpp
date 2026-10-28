// SPDX-License-Identifier: TBD
//
// Linux block-device discovery from sysfs + /proc. Strictly metadata: no
// device node is opened here.
#include "platter/platform/linux_sysfs.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace platter::sysfs {
namespace {

std::string trim(std::string s) {
    auto ws = [](unsigned char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\0'; };
    while (!s.empty() && ws(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && ws(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::optional<std::string> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string read_str(const fs::path& p) {
    auto s = read_file(p);
    return s ? trim(*s) : std::string{};
}

std::optional<std::uint64_t> read_u64(const fs::path& p) {
    auto s = read_file(p);
    if (!s) return std::nullopt;
    try {
        auto t = trim(*s);
        if (t.empty()) return std::nullopt;
        std::size_t used = 0;
        auto v = std::stoull(t, &used, 10);
        if (used != t.size()) return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

Tri read_bool(const fs::path& p) {
    auto v = read_u64(p);
    if (!v) return Tri::unknown;
    return *v ? Tri::yes : Tri::no;
}

// SCSI VPD page 0x80 (unit serial number): 4-byte header, byte 3 = length.
std::string serial_from_vpd80(const fs::path& p) {
    auto raw = read_file(p);
    if (!raw || raw->size() < 4) return {};
    const auto len = static_cast<unsigned char>((*raw)[3]);
    if (raw->size() < 4u + len) return {};
    return trim(raw->substr(4, len));
}

std::string decode_octal_escapes(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '\\' && i + 3 < in.size() &&
            std::isdigit(static_cast<unsigned char>(in[i + 1])) &&
            std::isdigit(static_cast<unsigned char>(in[i + 2])) &&
            std::isdigit(static_cast<unsigned char>(in[i + 3]))) {
            const int v = (in[i + 1] - '0') * 64 + (in[i + 2] - '0') * 8 + (in[i + 3] - '0');
            out.push_back(static_cast<char>(v));
            i += 3;
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

BusType classify_bus(const std::string& name, const std::string& devpath) {
    auto starts = [&](const char* p) { return name.rfind(p, 0) == 0; };
    if (starts("loop")) return BusType::loop;
    if (starts("dm-") || starts("md")) return BusType::raid;
    if (starts("nvme")) return BusType::nvme;
    if (starts("mmcblk")) return BusType::mmc;
    if (starts("vd")) return BusType::virtio;
    if (devpath.find("/usb") != std::string::npos) return BusType::usb;
    if (devpath.find("/ata") != std::string::npos) return BusType::ata;
    if (starts("sd") || starts("sr")) return BusType::scsi;
    return BusType::unknown;
}

std::vector<std::string> list_dir_names(const fs::path& p) {
    std::vector<std::string> out;
    std::error_code ec;
    for (auto it = fs::directory_iterator(p, ec); !ec && it != fs::directory_iterator(); it.increment(ec))
        out.push_back(it->path().filename().string());
    std::sort(out.begin(), out.end());
    return out;
}

// Partitions of `name` are subdirectories that contain a "partition" file.
std::vector<std::string> list_partitions(const fs::path& blockdir) {
    std::vector<std::string> out;
    for (const auto& n : list_dir_names(blockdir)) {
        std::error_code ec;
        if (fs::exists(blockdir / n / "partition", ec)) out.push_back(n);
    }
    return out;
}

std::string find_stable_id(const Roots& roots, const fs::path& blockdir, const std::string& name) {
    for (const char* f : {"wwid", "device/wwid"}) {
        auto v = read_str(blockdir / f);
        if (!v.empty()) return v;
    }
    // Fall back to /dev/disk/by-id; prefer wwn-, then nvme-eui., then anything.
    std::string best;
    int best_rank = 100;
    std::error_code ec;
    for (auto it = fs::directory_iterator(roots.disk_by_id, ec); !ec && it != fs::directory_iterator();
         it.increment(ec)) {
        const auto link = it->path().filename().string();
        if (link.find("-part") != std::string::npos) continue;
        std::error_code ec2;
        auto target = fs::read_symlink(it->path(), ec2);
        if (ec2 || target.filename().string() != name) continue;
        int rank = link.rfind("wwn-", 0) == 0 ? 0 : link.rfind("nvme-eui.", 0) == 0 ? 1 : 2;
        if (rank < best_rank || (rank == best_rank && link < best)) {
            best = link;
            best_rank = rank;
        }
    }
    return best;
}

struct UsageContext {
    std::vector<MountEntry> mounts;
    std::set<std::string> swaps;
};

// Collects mountpoints for a /dev node, including dm names under /dev/mapper.
// `node_dir` is the node's sysfs directory: /sys/block/<disk> for whole disks
// and stacked devices, /sys/block/<disk>/<part> for partitions.
void collect_usage(const Roots& roots, const UsageContext& ctx, const std::string& node_name,
                   const fs::path& node_dir, std::vector<std::string>& mountpoints, int depth) {
    if (depth > 8) return;  // defensive: storage stacks are never this deep
    std::vector<std::string> paths = {roots.dev + "/" + node_name};
    const fs::path& blockdir = node_dir;
    if (auto dmname = read_str(blockdir / "dm" / "name"); !dmname.empty())
        paths.push_back(roots.dev + "/mapper/" + dmname);

    for (const auto& p : paths) {
        for (const auto& m : ctx.mounts)
            if (m.source == p) mountpoints.push_back(m.mountpoint);
        if (ctx.swaps.count(p)) mountpoints.push_back("[SWAP]");
    }
    for (const auto& h : list_dir_names(blockdir / "holders"))
        collect_usage(roots, ctx, h, fs::path(roots.sys_block) / h, mountpoints, depth + 1);
}

DeviceInfo describe_with(const Roots& roots, const UsageContext& ctx, const std::string& name) {
    const fs::path dir = fs::path(roots.sys_block) / name;
    DeviceInfo d;
    d.name = name;
    d.path = roots.dev + "/" + name;

    if (auto sectors = read_u64(dir / "size")) d.capacity_bytes = *sectors * 512ull;  // always 512-byte units
    if (auto v = read_u64(dir / "queue/logical_block_size"); v && *v) d.logical_sector_size = static_cast<std::uint32_t>(*v);
    if (auto v = read_u64(dir / "queue/physical_block_size"); v && *v) d.physical_sector_size = static_cast<std::uint32_t>(*v);

    switch (read_bool(dir / "queue/rotational")) {
        case Tri::yes: d.media = MediaKind::rotational; break;
        case Tri::no: d.media = MediaKind::solid_state; break;
        default: break;
    }
    d.removable = read_bool(dir / "removable");
    d.read_only = read_bool(dir / "ro");
    if (auto dmax = read_u64(dir / "queue/discard_max_bytes")) d.trim_supported = *dmax > 0 ? Tri::yes : Tri::no;

    d.vendor = read_str(dir / "device/vendor");
    d.model = read_str(dir / "device/model");
    if (d.model.empty()) d.model = read_str(dir / "device/name");  // mmc
    d.firmware = read_str(dir / "device/firmware_rev");
    if (d.firmware.empty()) d.firmware = read_str(dir / "device/rev");
    d.serial = read_str(dir / "device/serial");
    if (d.serial.empty()) d.serial = serial_from_vpd80(dir / "device/vpd_pg80");
    d.stable_id = find_stable_id(roots, dir, name);

    std::error_code ec;
    auto canon = fs::canonical(dir, ec);
    if (!ec) d.topology = canon.string();
    d.bus = classify_bus(name, d.topology);

    // Usage: the disk itself, each partition, and anything stacked on top.
    collect_usage(roots, ctx, name, dir, d.mountpoints, 0);
    for (const auto& part : list_partitions(dir)) {
        collect_usage(roots, ctx, part, dir / part, d.mountpoints, 0);
        for (const auto& h : list_dir_names(dir / part / "holders")) d.holders.push_back(h);
    }
    for (const auto& h : list_dir_names(dir / "holders")) d.holders.push_back(h);
    std::sort(d.mountpoints.begin(), d.mountpoints.end());
    d.mountpoints.erase(std::unique(d.mountpoints.begin(), d.mountpoints.end()), d.mountpoints.end());

    for (const auto& mp : d.mountpoints)
        if (mp == "/" || mp == "/boot" || mp == "/boot/efi" || mp == "/usr") d.is_system_disk = true;

    // Capabilities we can actually justify from sysfs alone. SMART, self-test,
    // sanitize and secure erase are added later by the SMART adapter after
    // probing the device; they are never guessed from the bus type.
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

UsageContext load_usage(const Roots& roots) {
    UsageContext ctx;
    if (auto m = read_file(roots.mounts)) ctx.mounts = parse_mounts(*m);
    if (auto s = read_file(roots.swaps))
        for (auto& p : parse_swaps(*s)) ctx.swaps.insert(p);
    return ctx;
}

}  // namespace

std::vector<MountEntry> parse_mounts(const std::string& text) {
    std::vector<MountEntry> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        MountEntry e;
        if (!(ls >> e.source >> e.mountpoint >> e.fstype)) continue;
        e.source = decode_octal_escapes(e.source);
        e.mountpoint = decode_octal_escapes(e.mountpoint);
        out.push_back(std::move(e));
    }
    return out;
}

std::vector<std::string> parse_swaps(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in(text);
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) {
            header = false;
            if (line.rfind("Filename", 0) == 0) continue;
        }
        std::istringstream ls(line);
        std::string path;
        if (ls >> path) out.push_back(decode_octal_escapes(path));
    }
    return out;
}

bool should_skip(const std::string& name, std::uint64_t size_bytes) {
    if (name.rfind("ram", 0) == 0 || name.rfind("zram", 0) == 0) return true;
    // Size 0: empty card readers, unbound loops, ejected optical drives.
    return size_bytes == 0;
}

Result<std::vector<DeviceInfo>> enumerate(const Roots& roots) {
    std::error_code ec;
    if (!fs::is_directory(roots.sys_block, ec))
        return Error{Errc::not_found, roots.sys_block + " is not a directory"};
    const auto ctx = load_usage(roots);
    std::vector<DeviceInfo> out;
    for (const auto& name : list_dir_names(roots.sys_block)) {
        const auto sectors = read_u64(fs::path(roots.sys_block) / name / "size").value_or(0);
        if (should_skip(name, sectors * 512ull)) continue;
        out.push_back(describe_with(roots, ctx, name));
    }
    return out;
}

Result<DeviceInfo> describe(const Roots& roots, const std::string& name) {
    std::error_code ec;
    if (name.empty() || name.find('/') != std::string::npos || name == "." || name == "..")
        return Error{Errc::invalid_argument, "invalid block device name '" + name + "'"};
    if (!fs::is_directory(fs::path(roots.sys_block) / name, ec))
        return Error{Errc::not_found, "no block device named '" + name + "'"};
    return describe_with(roots, load_usage(roots), name);
}

}  // namespace platter::sysfs
