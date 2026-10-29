// SPDX-License-Identifier: TBD
#include "wire.hpp"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>

namespace platter::helper::wire {
namespace {

Status write_all(int fd, const std::uint8_t* p, std::size_t n) {
    while (n > 0) {
        // sudo and friends may hand the helper pipes instead of a socket.
        ssize_t w = ::send(fd, p, n, MSG_NOSIGNAL);
        if (w < 0 && errno == ENOTSOCK) w = ::write(fd, p, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return Error{Errc::io_error, std::string("helper connection write failed: ") + std::strerror(errno), errno};
        }
        p += w;
        n -= static_cast<std::size_t>(w);
    }
    return {};
}

// Returns bytes read: n on success, 0 on EOF at the first byte, error otherwise.
Result<std::size_t> read_all(int fd, std::uint8_t* p, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        const ssize_t r = ::read(fd, p + got, n - got);
        if (r < 0) {
            if (errno == EINTR) continue;
            return Error{Errc::io_error, std::string("helper connection read failed: ") + std::strerror(errno), errno};
        }
        if (r == 0) {
            if (got == 0) return std::size_t{0};
            return Error{Errc::io_error, "helper connection closed in the middle of a message"};
        }
        got += static_cast<std::size_t>(r);
    }
    return got;
}

}  // namespace

Status write_frame(int fd, const std::vector<std::uint8_t>& payload) {
    if (payload.size() > kMaxFrame) return Error{Errc::overflow, "message too large for the helper protocol"};
    std::uint8_t hdr[4];
    const auto len = static_cast<std::uint32_t>(payload.size());
    for (int i = 0; i < 4; ++i) hdr[i] = static_cast<std::uint8_t>((len >> (8 * i)) & 0xff);
    if (auto s = write_all(fd, hdr, 4); !s) return s;
    return write_all(fd, payload.data(), payload.size());
}

Result<std::vector<std::uint8_t>> read_frame(int fd) {
    std::uint8_t hdr[4];
    auto r = read_all(fd, hdr, 4);
    if (!r) return r.error();
    if (*r == 0) return Error{Errc::not_found, "helper connection closed"};
    std::uint32_t len = 0;
    for (int i = 0; i < 4; ++i) len |= static_cast<std::uint32_t>(hdr[i]) << (8 * i);
    if (len > kMaxFrame) return Error{Errc::corrupt_data, "oversized helper message"};
    std::vector<std::uint8_t> payload(len);
    if (len) {
        auto b = read_all(fd, payload.data(), len);
        if (!b) return b.error();
        if (*b == 0) return Error{Errc::io_error, "helper connection closed in the middle of a message"};
    }
    return payload;
}

void put_info(Writer& w, const DeviceInfo& d) {
    w.str(d.path); w.str(d.name); w.str(d.vendor); w.str(d.model); w.str(d.serial); w.str(d.firmware); w.str(d.stable_id);
    w.u64(d.capacity_bytes); w.u32(d.logical_sector_size); w.u32(d.physical_sector_size);
    w.u8(static_cast<std::uint8_t>(d.bus)); w.u8(static_cast<std::uint8_t>(d.media));
    w.u8(static_cast<std::uint8_t>(d.removable)); w.u8(static_cast<std::uint8_t>(d.read_only));
    w.u8(static_cast<std::uint8_t>(d.trim_supported));
    w.u32(static_cast<std::uint32_t>(d.mountpoints.size()));
    for (const auto& m : d.mountpoints) w.str(m);
    w.u8(d.is_system_disk ? 1 : 0);
    w.u32(static_cast<std::uint32_t>(d.holders.size()));
    for (const auto& h : d.holders) w.str(h);
    w.str(d.partition_table); w.str(d.topology);
    w.u32(d.capabilities.bits());
}

DeviceInfo get_info(Reader& r) {
    DeviceInfo d;
    d.path = r.str(); d.name = r.str(); d.vendor = r.str(); d.model = r.str(); d.serial = r.str();
    d.firmware = r.str(); d.stable_id = r.str();
    d.capacity_bytes = r.u64(); d.logical_sector_size = r.u32(); d.physical_sector_size = r.u32();
    d.bus = static_cast<BusType>(r.u8()); d.media = static_cast<MediaKind>(r.u8());
    d.removable = static_cast<Tri>(r.u8()); d.read_only = static_cast<Tri>(r.u8());
    d.trim_supported = static_cast<Tri>(r.u8());
    for (auto n = r.u32(); n > 0 && r.ok(); --n) d.mountpoints.push_back(r.str());
    d.is_system_disk = r.u8() != 0;
    for (auto n = r.u32(); n > 0 && r.ok(); --n) d.holders.push_back(r.str());
    d.partition_table = r.str(); d.topology = r.str();
    const auto caps = r.u32();
    for (auto c : all_capabilities()) d.capabilities.set(c, (caps & static_cast<std::uint32_t>(c)) != 0);
    return d;
}

void put_error(Writer& w, const Error& e) {
    w.u16(static_cast<std::uint16_t>(e.code));
    w.u64(static_cast<std::uint64_t>(e.os_error));
    w.str(e.message);
}

Error get_error(Reader& r) {
    Error e;
    e.code = static_cast<Errc>(r.u16());
    e.os_error = static_cast<std::int64_t>(r.u64());
    e.message = r.str();
    return e;
}

}  // namespace platter::helper::wire
