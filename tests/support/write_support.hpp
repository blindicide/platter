// SPDX-License-Identifier: TBD
//
// Test doubles for the destructive code paths. Everything wraps an image
// file in a temp directory; nothing here can touch a real device.
#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include "platter/core/block_device.hpp"
#include "platter/core/safety.hpp"

namespace platter::test {

// A read-write image wrapper that records every write and can misbehave.
class FaultyWriteDevice final : public IBlockDevice {
public:
    explicit FaultyWriteDevice(BlockDevicePtr inner) : inner_(std::move(inner)) {}

    // Writes touching these LBAs fail with io_error.
    std::set<std::uint64_t> bad_write_lbas;
    // Writes touching these LBAs report success but store nothing.
    std::set<std::uint64_t> dropped_lbas;
    // Reads touching these LBAs fail with io_error.
    std::set<std::uint64_t> bad_read_lbas;
    // After this many writes every write fails with device_removed (0 = never).
    std::uint64_t remove_after_writes = 0;
    std::chrono::microseconds write_delay{0};
    bool discard_ok = true;

    struct WriteRecord {
        std::uint64_t offset;
        std::size_t length;
    };
    std::vector<WriteRecord> write_log() const {
        std::lock_guard lk(m_);
        return log_;
    }
    std::uint64_t writes() const noexcept { return writes_.load(); }

    const DeviceInfo& info() const noexcept override { return inner_->info(); }
    OpenMode mode() const noexcept override { return inner_->mode(); }
    std::uint32_t required_alignment() const noexcept override { return inner_->required_alignment(); }
    Status flush() override { return inner_->flush(); }

    Result<IOResult> read(std::uint64_t offset, void* buffer, std::size_t length) override {
        if (touches(bad_read_lbas, offset, length)) return Error{Errc::io_error, "simulated unreadable sector"};
        return inner_->read(offset, buffer, length);
    }

    Result<IOResult> write(std::uint64_t offset, const void* buffer, std::size_t length) override {
        const auto n = ++writes_;
        {
            std::lock_guard lk(m_);
            log_.push_back({offset, length});
        }
        if (write_delay.count() > 0) std::this_thread::sleep_for(write_delay);
        if (remove_after_writes && n > remove_after_writes)
            return Error{Errc::device_removed, "simulated device removal"};
        if (touches(bad_write_lbas, offset, length)) return Error{Errc::io_error, "simulated write failure"};
        if (touches(dropped_lbas, offset, length)) {
            // Store everything except the dropped sectors.
            const auto ss = info().logical_sector_size;
            const auto* src = static_cast<const std::uint8_t*>(buffer);
            for (std::size_t s = 0; s < length; s += ss) {
                const std::uint64_t lba = (offset + s) / ss;
                if (dropped_lbas.count(lba)) continue;
                auto r = inner_->write(offset + s, src + s, ss);
                if (!r) return r.error();
            }
            return IOResult{length, 0};
        }
        return inner_->write(offset, buffer, length);
    }

    Status discard(std::uint64_t offset, std::uint64_t length) override {
        if (!discard_ok) return Error{Errc::unsupported, "no discard here"};
        return inner_->discard(offset, length);
    }

private:
    bool touches(const std::set<std::uint64_t>& lbas, std::uint64_t offset, std::size_t length) const {
        if (lbas.empty() || length == 0) return false;
        const auto ss = info().logical_sector_size;
        const std::uint64_t first = offset / ss, last = (offset + length - 1) / ss;
        auto it = lbas.lower_bound(first);
        return it != lbas.end() && *it <= last;
    }

    BlockDevicePtr inner_;
    std::atomic<std::uint64_t> writes_{0};
    mutable std::mutex m_;
    std::vector<WriteRecord> log_;
};

}  // namespace platter::test

// The one place that can build an AuthorizedTarget without going through
// authorize(): defined by the test suite only, so production code cannot use it.
namespace platter::safety {

struct TestAccess {
    static AuthorizedTarget make(BlockDevicePtr dev, Kind kind, std::vector<LbaRange> ranges = {},
                                 bool direct = false) {
        Preview p;
        p.request.kind = kind;
        p.request.target.path = dev->info().path;
        p.request.target.is_image = true;
        p.device = dev->info();
        p.fingerprint = Fingerprint::of(p.device);
        const auto total = p.device.total_lbas();
        if (ranges.empty()) {
            ranges.push_back(LbaRange{0, total});
            p.whole_device = true;
        }
        p.ranges = ranges;
        for (const auto& r : ranges) p.affected_bytes += r.count * p.device.logical_sector_size;
        auto lease = DeviceLeaseRegistry::global().acquire(p.device.path, AccessKind::exclusive);
        return AuthorizedTarget(std::move(dev), std::move(lease).value(), std::move(p), direct, {});
    }
};

}  // namespace platter::safety
