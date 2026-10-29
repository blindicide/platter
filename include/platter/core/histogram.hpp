// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Streaming latency histogram: log2 octaves split into 16 linear sub-buckets
// (an HDR-style layout). Percentiles are upper-bound estimates within ~6 %,
// memory is fixed (8 KiB) no matter how many samples are added.
#pragma once

#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace platter {

class LatencyHistogram {
public:
    void add(std::uint64_t us);
    void merge(const LatencyHistogram& other);
    std::uint64_t count() const noexcept { return count_; }
    std::uint64_t min() const noexcept { return count_ ? min_ : 0; }
    std::uint64_t max() const noexcept { return max_; }
    double mean() const noexcept { return count_ ? static_cast<double>(sum_) / static_cast<double>(count_) : 0; }
    // p in [0,100]. Returns an upper-bound estimate within one bucket.
    std::uint64_t percentile(double p) const;
    // Non-empty buckets as (inclusive upper bound in us, count), ascending.
    std::vector<std::pair<std::uint64_t, std::uint64_t>> buckets() const;

private:
    static constexpr int kSub = 16;
    static constexpr int kBuckets = 64 * kSub;
    static int bucket_of(std::uint64_t us);
    static std::uint64_t bucket_upper(int b);
    std::array<std::uint64_t, kBuckets> b_{};
    std::uint64_t count_ = 0, sum_ = 0, min_ = UINT64_MAX, max_ = 0;
};

}  // namespace platter
