// SPDX-License-Identifier: TBD
//
// splitmix64: tiny, fast, and bit-identical on every platform (unlike the
// std:: distributions, whose output is implementation-defined). Used wherever
// a scan or benchmark must be reproducible from a seed.
#pragma once

#include <cstdint>

namespace platter {

struct SplitMix64 {
    std::uint64_t s;
    std::uint64_t next() noexcept {
        std::uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // Unbiased value in [0, bound); bound must be > 0.
    std::uint64_t below(std::uint64_t bound) noexcept {
        const std::uint64_t limit = UINT64_MAX - UINT64_MAX % bound;
        std::uint64_t v;
        do v = next();
        while (v >= limit);
        return v % bound;
    }
};

}  // namespace platter
