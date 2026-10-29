// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Test double: wraps a real (image) device and injects errors, fake
// latencies, real delays, or a disappearing device. Used to exercise the
// surface-scan engine without any physical hardware.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "platter/core/block_device.hpp"

namespace platter::test {

class FaultyDevice final : public IBlockDevice {
public:
    explicit FaultyDevice(BlockDevicePtr inner) : inner_(std::move(inner)) {}

    // Reads overlapping any of these LBAs fail with io_error...
    std::set<std::uint64_t> bad_lbas;
    // ...unless they appear here: fail only the first N attempts, then succeed.
    std::map<std::uint64_t, int> flaky_lbas;
    // Fake duration reported for successful reads whose range contains the key LBA.
    std::map<std::uint64_t, std::uint64_t> latency_ns_at;
    std::uint64_t default_latency_ns = 1'000'000;  // 1 ms
    // Real sleep per read (for pause/cancel tests).
    std::chrono::microseconds real_delay{0};
    // After this many reads, every read fails with device_removed (0 = never).
    std::uint64_t remove_after_reads = 0;
    // Called after every read with the running read count.
    std::function<void(std::uint64_t)> after_read;
    // Called before every read with (offset, length). Must be thread-safe.
    std::function<void(std::uint64_t, std::size_t)> on_read;

    std::uint64_t reads() const noexcept { return reads_.load(); }

    const DeviceInfo& info() const noexcept override { return inner_->info(); }
    OpenMode mode() const noexcept override { return inner_->mode(); }
    std::uint32_t required_alignment() const noexcept override { return inner_->required_alignment(); }
    Status flush() override { return inner_->flush(); }
    Result<IOResult> write(std::uint64_t, const void*, std::size_t) override {
        return Error{Errc::read_only, "FaultyDevice is read-only"};
    }

    Result<IOResult> read(std::uint64_t offset, void* buffer, std::size_t length) override {
        const auto n = ++reads_;
        if (on_read) on_read(offset, length);
        if (real_delay.count() > 0) std::this_thread::sleep_for(real_delay);
        auto finish = [&](Result<IOResult> r) {
            if (after_read) after_read(n);
            return r;
        };
        if (remove_after_reads && n > remove_after_reads)
            return finish(Error{Errc::device_removed, "simulated device removal"});

        const auto ss = info().logical_sector_size;
        const std::uint64_t first = offset / ss, last = (offset + length - 1) / ss;
        {
            std::lock_guard lk(m_);
            for (auto it = flaky_lbas.lower_bound(first); it != flaky_lbas.end() && it->first <= last; ++it) {
                if (it->second > 0) {
                    --it->second;
                    return finish(Error{Errc::io_error, "simulated transient error at LBA " + std::to_string(it->first)});
                }
            }
        }
        if (auto it = bad_lbas.lower_bound(first); it != bad_lbas.end() && *it <= last)
            return finish(Error{Errc::io_error, "simulated unreadable LBA " + std::to_string(*it)});

        auto r = inner_->read(offset, buffer, length);
        if (!r) return finish(r);
        r->duration_ns = default_latency_ns;
        if (auto it = latency_ns_at.lower_bound(first); it != latency_ns_at.end() && it->first <= last)
            r->duration_ns = it->second;
        return finish(r);
    }

private:
    BlockDevicePtr inner_;
    std::atomic<std::uint64_t> reads_{0};
    std::mutex m_;
};

}  // namespace platter::test
