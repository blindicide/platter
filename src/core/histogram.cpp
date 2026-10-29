// SPDX-License-Identifier: LicenseRef-Proprietary
#include "platter/core/histogram.hpp"

#include <algorithm>
#include <bit>
#include <cmath>

namespace platter {

int LatencyHistogram::bucket_of(std::uint64_t us) {
    if (us < kSub) return static_cast<int>(us);
    const int msb = 63 - std::countl_zero(us);
    const int shift = msb - 4;
    const int sub = static_cast<int>((us >> shift) & (kSub - 1));
    return (msb - 3) * kSub + sub;
}

std::uint64_t LatencyHistogram::bucket_upper(int b) {
    if (b < kSub) return static_cast<std::uint64_t>(b);
    const int group = b / kSub, sub = b % kSub;
    const int shift = group - 1;
    const std::uint64_t lower = (static_cast<std::uint64_t>(kSub + sub)) << shift;
    return lower + ((std::uint64_t{1} << shift) - 1);
}

void LatencyHistogram::add(std::uint64_t us) {
    ++b_[static_cast<std::size_t>(bucket_of(us))];
    ++count_;
    sum_ += us;
    min_ = std::min(min_, us);
    max_ = std::max(max_, us);
}

void LatencyHistogram::merge(const LatencyHistogram& o) {
    for (std::size_t i = 0; i < b_.size(); ++i) b_[i] += o.b_[i];
    count_ += o.count_;
    sum_ += o.sum_;
    min_ = std::min(min_, o.min_);
    max_ = std::max(max_, o.max_);
}

std::uint64_t LatencyHistogram::percentile(double p) const {
    if (count_ == 0) return 0;
    p = std::clamp(p, 0.0, 100.0);
    auto target = static_cast<std::uint64_t>(std::ceil(p / 100.0 * static_cast<double>(count_)));
    target = std::max<std::uint64_t>(target, 1);
    std::uint64_t seen = 0;
    for (int i = 0; i < kBuckets; ++i) {
        seen += b_[static_cast<std::size_t>(i)];
        if (seen >= target) return std::clamp(bucket_upper(i), min(), max_);
    }
    return max_;
}

std::vector<std::pair<std::uint64_t, std::uint64_t>> LatencyHistogram::buckets() const {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> out;
    for (int i = 0; i < kBuckets; ++i)
        if (b_[static_cast<std::size_t>(i)]) out.emplace_back(bucket_upper(i), b_[static_cast<std::size_t>(i)]);
    return out;
}

}  // namespace platter
