// SPDX-License-Identifier: TBD
//
// Wire format between the unprivileged client and platter-helper. Internal.
// Little-endian, length-prefixed frames over a stream socket; strictly
// request/response. The peers are always the same build, so the protocol
// version is only checked for equality.
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "platter/core/device.hpp"
#include "platter/core/error.hpp"

namespace platter::helper::wire {

constexpr std::uint32_t kProtocol = 1;
constexpr std::uint32_t kMaxFrame = 24u << 20;   // header + payload
constexpr std::uint32_t kMaxIo = 16u << 20;      // one read/write request

enum class Op : std::uint8_t {
    hello = 1,
    open = 2,
    read = 3,
    write = 4,
    flush = 5,
    discard = 6,
    reread = 7,
    close = 8,
    quit = 9,
};

class Writer {
public:
    void u8(std::uint8_t v) { buf_.push_back(v); }
    void u16(std::uint16_t v) { put(v); }
    void u32(std::uint32_t v) { put(v); }
    void u64(std::uint64_t v) { put(v); }
    void str(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        buf_.insert(buf_.end(), s.begin(), s.end());
    }
    void bytes(const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf_.insert(buf_.end(), b, b + n);
    }
    std::vector<std::uint8_t>& data() { return buf_; }
    const std::vector<std::uint8_t>& data_view() const { return buf_; }

private:
    template <class T>
    void put(T v) {
        for (std::size_t i = 0; i < sizeof(T); ++i) buf_.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(v) >> (8 * i)) & 0xff));
    }
    std::vector<std::uint8_t> buf_;
};

class Reader {
public:
    Reader(const std::uint8_t* p, std::size_t n) : p_(p), n_(n) {}
    std::uint8_t u8() { return static_cast<std::uint8_t>(get(1)); }
    std::uint16_t u16() { return static_cast<std::uint16_t>(get(2)); }
    std::uint32_t u32() { return static_cast<std::uint32_t>(get(4)); }
    std::uint64_t u64() { return get(8); }
    std::string str() {
        const auto len = u32();
        if (!need(len)) return {};
        std::string s(reinterpret_cast<const char*>(p_ + pos_), len);
        pos_ += len;
        return s;
    }
    // Pointer to `n` bytes in the frame, or nullptr (and the reader fails).
    const std::uint8_t* bytes(std::size_t n) {
        if (!need(n)) return nullptr;
        const auto* r = p_ + pos_;
        pos_ += n;
        return r;
    }
    std::size_t remaining() const { return ok_ ? n_ - pos_ : 0; }
    bool ok() const { return ok_; }

private:
    bool need(std::size_t n) {
        if (!ok_ || n > n_ - pos_) ok_ = false;
        return ok_;
    }
    std::uint64_t get(std::size_t n) {
        if (!need(n)) return 0;
        std::uint64_t v = 0;
        for (std::size_t i = 0; i < n; ++i) v |= static_cast<std::uint64_t>(p_[pos_ + i]) << (8 * i);
        pos_ += n;
        return v;
    }
    const std::uint8_t* p_;
    std::size_t n_;
    std::size_t pos_ = 0;
    bool ok_ = true;
};

void put_info(Writer& w, const DeviceInfo& d);
DeviceInfo get_info(Reader& r);
void put_error(Writer& w, const Error& e);
Error get_error(Reader& r);

// Frame I/O on a stream fd. `send_flags` is MSG_NOSIGNAL for sockets.
Status write_frame(int fd, const std::vector<std::uint8_t>& payload);
// Blocks. Returns not_found on clean EOF before the first byte.
Result<std::vector<std::uint8_t>> read_frame(int fd);

}  // namespace platter::helper::wire
