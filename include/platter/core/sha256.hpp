// SPDX-License-Identifier: TBD
// SHA-256 (FIPS 180-4). Streaming; used for device-key hashing and, later,
// image verification.
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace platter {

class Sha256 {
public:
    Sha256() { reset(); }
    void reset();
    void update(std::span<const std::uint8_t> data);
    void update(std::string_view s) {
        update(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
    }
    std::array<std::uint8_t, 32> finish();
    static std::string hex(const std::array<std::uint8_t, 32>& d);
    static std::string hex_of(std::string_view s) {
        Sha256 h;
        h.update(s);
        return hex(h.finish());
    }

private:
    void block(const std::uint8_t* p);
    std::array<std::uint32_t, 8> h_{};
    std::array<std::uint8_t, 64> buf_{};
    std::size_t buf_len_ = 0;
    std::uint64_t total_ = 0;
};

}  // namespace platter
