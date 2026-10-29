// SPDX-License-Identifier: TBD
#include "platter/benchmark/benchmark.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <optional>
#include <thread>

#include "platter/core/aligned_buffer.hpp"
#include "platter/core/chacha20.hpp"
#include "platter/core/range.hpp"
#include "platter/core/rng.hpp"
#include "platter/core/time_util.hpp"

namespace platter::bench {

std::string_view bench_kind_name(BenchKind k) noexcept {
    switch (k) {
        case BenchKind::sequential: return "sequential";
        case BenchKind::random: return "random";
        case BenchKind::access_time: return "access_time";
        case BenchKind::speed_curve: return "speed_curve";
        case BenchKind::block_sizes: return "block_sizes";
        case BenchKind::queue_depths: return "queue_depths";
        case BenchKind::sequential_write: return "sequential_write";
        case BenchKind::random_write: return "random_write";
    }
    return "?";
}

std::string_view bench_kind_title(BenchKind k) noexcept {
    switch (k) {
        case BenchKind::sequential: return "Sequential read";
        case BenchKind::random: return "Random read";
        case BenchKind::access_time: return "Access time";
        case BenchKind::speed_curve: return "Read speed curve";
        case BenchKind::block_sizes: return "Block size sweep";
        case BenchKind::queue_depths: return "Queue depth sweep";
        case BenchKind::sequential_write: return "Sequential write";
        case BenchKind::random_write: return "Random write";
    }
    return "?";
}

namespace {

constexpr std::uint32_t kMaxBlock = 64u << 20;
constexpr std::uint32_t kMaxQD = 64;
constexpr std::uint64_t kMaxErrors = 64;
constexpr auto kSampleEvery = std::chrono::milliseconds(250);

std::uint32_t round_up_to(std::uint32_t v, std::uint32_t m) { return m ? ((v + m - 1) / m) * m : v; }

std::string size_label(std::uint32_t b) {
    char buf[32];
    if (b >= (1u << 20) && b % (1u << 20) == 0) std::snprintf(buf, sizeof buf, "%u MiB", b >> 20);
    else if (b >= 1024 && b % 1024 == 0) std::snprintf(buf, sizeof buf, "%u KiB", b >> 10);
    else std::snprintf(buf, sizeof buf, "%u B", b);
    return buf;
}



}  // namespace

BenchConfig with_defaults(BenchConfig c, std::uint32_t ss) {
    if (ss == 0) ss = 512;
    if (c.block_bytes == 0) {
        switch (c.kind) {
            case BenchKind::random:
            case BenchKind::random_write:
            case BenchKind::queue_depths: c.block_bytes = round_up_to(4096, ss); break;
            case BenchKind::access_time: c.block_bytes = ss; break;
            default: c.block_bytes = 1u << 20; break;
        }
    }
    if (c.kind == BenchKind::access_time) c.queue_depth = 1;
    if (c.max_bytes == 0) {
        if (c.kind == BenchKind::sequential || c.kind == BenchKind::sequential_write) c.max_bytes = 1ull << 30;
        if (c.kind == BenchKind::block_sizes) c.max_bytes = 256ull << 20;
    }
    if (c.max_seconds <= 0) {
        switch (c.kind) {
            case BenchKind::sequential:
            case BenchKind::sequential_write: c.max_seconds = 30; break;
            case BenchKind::random:
            case BenchKind::random_write: c.max_seconds = 10; break;
            case BenchKind::access_time: c.max_seconds = 5; break;
            case BenchKind::speed_curve: c.max_seconds = 5; break;  // per point, safety cap
            case BenchKind::block_sizes:
            case BenchKind::queue_depths: c.max_seconds = 3; break;
        }
    }
    return c;
}

Metrics compute_metrics(const LatencyHistogram& h, std::uint64_t ops, std::uint64_t bytes, std::uint64_t errors,
                        std::uint64_t wall_ns) {
    Metrics m;
    m.ops = ops;
    m.bytes = bytes;
    m.errors = errors;
    m.wall_ns = wall_ns;
    const double secs = static_cast<double>(wall_ns) / 1e9;
    if (secs > 0) {
        m.mb_per_s = static_cast<double>(bytes) / secs / 1e6;
        m.mib_per_s = static_cast<double>(bytes) / secs / 1048576.0;
        m.iops = static_cast<double>(ops) / secs;
    }
    m.lat_mean_us = h.mean();
    m.lat_min_us = h.min();
    m.lat_median_us = h.percentile(50);
    m.lat_p95_us = h.percentile(95);
    m.lat_p99_us = h.percentile(99);
    m.lat_max_us = h.max();
    return m;
}

std::string BenchResult::headline() const {
    char b[96];
    auto best = [&](auto proj) {
        double v = 0;
        for (const auto& p : phases) v = std::max(v, proj(p.metrics));
        return v;
    };
    switch (config.kind) {
        case BenchKind::sequential:
        case BenchKind::sequential_write:
            std::snprintf(b, sizeof b, "%.1f MB/s", phases.empty() ? 0.0 : phases.front().metrics.mb_per_s);
            break;
        case BenchKind::random:
        case BenchKind::random_write:
            std::snprintf(b, sizeof b, "%.0f IOPS @ QD%u (%s)", phases.empty() ? 0.0 : phases.front().metrics.iops,
                          config.queue_depth, size_label(config.block_bytes).c_str());
            break;
        case BenchKind::access_time:
            std::snprintf(b, sizeof b, "%.2f ms", phases.empty() ? 0.0 : phases.front().metrics.lat_mean_us / 1000.0);
            break;
        case BenchKind::speed_curve: {
            double lo = 0, hi = 0, sum = 0;
            for (std::size_t i = 0; i < positions.size(); ++i) {
                const double v = positions[i].bytes_per_sec / 1e6;
                lo = i ? std::min(lo, v) : v;
                hi = std::max(hi, v);
                sum += v;
            }
            std::snprintf(b, sizeof b, "min %.1f / avg %.1f / max %.1f MB/s", lo,
                          positions.empty() ? 0.0 : sum / static_cast<double>(positions.size()), hi);
            break;
        }
        case BenchKind::block_sizes:
            std::snprintf(b, sizeof b, "peak %.1f MB/s", best([](const Metrics& m) { return m.mb_per_s; }));
            break;
        case BenchKind::queue_depths:
            std::snprintf(b, sizeof b, "peak %.0f IOPS", best([](const Metrics& m) { return m.iops; }));
            break;
    }
    return b;
}

// ------------------------------------------------------------------ Benchmark

Benchmark::Benchmark(DeviceOpener opener, BenchConfig config, std::string device_id, bool is_image)
    : Operation("benchmark", std::move(device_id), AccessKind::shared_read),
      opener_(std::move(opener)),
      cfg_(std::move(config)),
      is_image_(is_image) {}

Benchmark::Benchmark(safety::AuthorizedTarget target, BenchConfig config)
    : Operation("benchmark", target.device_id(), AccessKind::exclusive),
      auth_(std::make_unique<safety::AuthorizedTarget>(std::move(target))),
      cfg_(std::move(config)),
      is_image_(auth_->info().bus == BusType::image) {}

Benchmark::~Benchmark() {
    cancel();
    wait();
}

void Benchmark::note(const std::string& w) {
    std::lock_guard lk(m_);
    if (std::find(res_.warnings.begin(), res_.warnings.end(), w) == res_.warnings.end()) res_.warnings.push_back(w);
}

Status Benchmark::do_validate() {
    if (auth_) {
        if (auth_->kind() != safety::Kind::write_test)
            return Error{Errc::refused, "the authorization was not granted for a write test"};
        if (!is_write_kind(cfg_.kind))
            return Error{Errc::invalid_argument, "an authorized target can only run write benchmarks"};
        info_ = auth_->info();
    } else {
        if (is_write_kind(cfg_.kind))
            return Error{Errc::refused, "write benchmarks need an authorized target (typed confirmation)"};
        if (!opener_) return Error{Errc::invalid_argument, "no device opener"};
        auto dev = opener_();
        if (!dev) return dev.error();
        // Read benchmarks only read. Refuse anything that could write.
        if ((*dev)->mode() != OpenMode::read_only)
            return Error{Errc::invalid_argument, "read benchmarks require a read-only device handle"};
        info_ = (*dev)->info();
    }
    const auto ss = info_.logical_sector_size;
    const auto total = info_.total_lbas();
    if (ss == 0 || total == 0) return Error{Errc::invalid_argument, "device reports zero capacity"};

    cfg_ = with_defaults(cfg_, ss);
    first_lba_ = cfg_.start_lba;
    last_lba_ = cfg_.end_lba == UINT64_MAX ? total - 1 : cfg_.end_lba;
    auto range = lba_range_inclusive(first_lba_, last_lba_);
    if (!range) return range.error();
    if (auto s = validate_lba_range(*range, total); !s) return s;
    const std::uint64_t region_lbas = range->count;
    const std::uint64_t region_bytes = region_lbas * ss;
    if (auth_ && !auth_->allows(first_lba_ * ss, region_bytes))
        return Error{Errc::refused, "the test region is not inside the range that was confirmed for writing"};

    auto check_block = [&](std::uint32_t b) -> Status {
        if (b < ss || b % ss != 0 || b > kMaxBlock)
            return Error{Errc::invalid_argument, "block size " + std::to_string(b) + " must be a multiple of the " +
                                                     std::to_string(ss) + "-byte sector, up to 64 MiB"};
        if (b > region_bytes) return Error{Errc::invalid_argument, "block size is larger than the test region"};
        return {};
    };
    if (cfg_.queue_depth < 1 || cfg_.queue_depth > kMaxQD)
        return Error{Errc::invalid_argument, "queue depth must be between 1 and 64"};

    plan_.clear();
    switch (cfg_.kind) {
        case BenchKind::sequential:
            if (auto s = check_block(cfg_.block_bytes); !s) return s;
            plan_.push_back({"Sequential", cfg_.block_bytes, cfg_.queue_depth, false, first_lba_, region_lbas,
                             cfg_.max_bytes, cfg_.max_seconds, true});
            break;
        case BenchKind::random:
        case BenchKind::access_time:
            if (auto s = check_block(cfg_.block_bytes); !s) return s;
            plan_.push_back({cfg_.kind == BenchKind::random ? "Random" : "Access time", cfg_.block_bytes,
                             cfg_.queue_depth, true, first_lba_, region_lbas, 0, cfg_.max_seconds, false});
            break;
        case BenchKind::sequential_write:
            if (auto s = check_block(cfg_.block_bytes); !s) return s;
            plan_.push_back({"Sequential write", cfg_.block_bytes, cfg_.queue_depth, false, first_lba_, region_lbas,
                             cfg_.max_bytes, cfg_.max_seconds, true, true});
            break;
        case BenchKind::random_write:
            if (auto s = check_block(cfg_.block_bytes); !s) return s;
            plan_.push_back({"Random write", cfg_.block_bytes, cfg_.queue_depth, true, first_lba_, region_lbas, 0,
                             cfg_.max_seconds, false, true});
            break;
        case BenchKind::speed_curve: {
            if (auto s = check_block(cfg_.block_bytes); !s) return s;
            if (cfg_.curve_points < 2 || cfg_.curve_points > 10000)
                return Error{Errc::invalid_argument, "curve points must be between 2 and 10000"};
            std::uint64_t per_point = std::max<std::uint64_t>(cfg_.curve_bytes_per_point, cfg_.block_bytes);
            per_point = (per_point / cfg_.block_bytes) * cfg_.block_bytes;
            const std::uint64_t point_lbas = std::min<std::uint64_t>(per_point / ss, region_lbas);
            const std::uint64_t block_lbas = cfg_.block_bytes / ss;
            const std::uint64_t span = region_lbas - point_lbas;
            for (std::uint32_t i = 0; i < cfg_.curve_points; ++i) {
                std::uint64_t at = first_lba_ + span * i / (cfg_.curve_points - 1);
                at -= (at - first_lba_) % block_lbas;  // block-aligned within the region
                plan_.push_back({"Speed curve", cfg_.block_bytes, cfg_.queue_depth, false, at, point_lbas,
                                 point_lbas * ss, cfg_.max_seconds, false});
            }
            break;
        }
        case BenchKind::block_sizes: {
            if (cfg_.sweep_block_sizes.empty()) return Error{Errc::invalid_argument, "no block sizes to test"};
            std::uint64_t step = 0;
            for (auto b : cfg_.sweep_block_sizes) {
                if (!check_block(b)) {
                    note("Skipped block size " + std::to_string(b) +
                                     ": not a sector multiple, too large, or larger than the region");
                    continue;
                }
                // Each step starts at a different offset so the drive's cache
                // cannot serve a step from data the previous step just read.
                const std::uint64_t b_lbas = b / ss;
                std::uint64_t offset_lbas = (step++ * (cfg_.max_bytes / ss)) % std::max<std::uint64_t>(region_lbas, 1);
                offset_lbas -= offset_lbas % b_lbas;
                const std::uint64_t remain = region_lbas - offset_lbas;
                plan_.push_back({size_label(b), b, cfg_.queue_depth, false, first_lba_ + offset_lbas, remain,
                                 cfg_.max_bytes, cfg_.max_seconds, false});
            }
            if (plan_.empty()) return Error{Errc::invalid_argument, "none of the block sizes fit this device"};
            break;
        }
        case BenchKind::queue_depths:
            if (auto s = check_block(cfg_.block_bytes); !s) return s;
            if (cfg_.sweep_queue_depths.empty()) return Error{Errc::invalid_argument, "no queue depths to test"};
            for (auto q : cfg_.sweep_queue_depths) {
                if (q < 1 || q > kMaxQD) return Error{Errc::invalid_argument, "queue depths must be between 1 and 64"};
                plan_.push_back({"QD " + std::to_string(q), cfg_.block_bytes, q, true, first_lba_, region_lbas, 0,
                                 cfg_.max_seconds, false});
            }
            break;
    }

    // Spec §7.4: say when results may not represent the physical device.
    if (is_image_)
        note("Image file: results reflect the host filesystem and OS cache, not a physical device.");
    if (!cfg_.direct_io) note("Buffered I/O: reads may be served from the OS page cache.");
    if (auth_) {
        note("DESTRUCTIVE: data in LBA " + std::to_string(first_lba_) + " - " + std::to_string(last_lba_) +
             " was overwritten with random data.");
        if (!auth_->direct_io())
            note("Writes went through the OS cache; the data was flushed at the end of each phase.");
        note("The drive's volatile write cache can inflate short write tests; each phase ends with a flush.");
        if (cfg_.queue_depth > 1)
            note("Write queue depth above 1 uses several threads on one exclusive handle (Windows serializes these).");
    }
    if (cfg_.kind == BenchKind::sequential && std::min(cfg_.max_bytes, region_bytes) < (512ull << 20))
        note("Small test size: a large part of it may be served from the drive's own cache.");
    const bool random_kind = cfg_.kind == BenchKind::random || cfg_.kind == BenchKind::access_time ||
                             cfg_.kind == BenchKind::queue_depths;
    if (random_kind && region_bytes < (2ull << 30))
        note("Random reads confined to a region under 2 GiB may be served from the drive's cache.");

    std::lock_guard lk(m_);
    res_.config = cfg_;
    res_.device = info_;
    return {};
}

Status Benchmark::run_phase(const PhaseSpec& spec, OperationControl& control, PositionPoint* single_point,
                            Metrics* out) {
    const std::uint32_t ss = info_.logical_sector_size;
    const std::uint64_t lbas_per = spec.block_bytes / ss;
    const std::uint64_t blocks_in_region = spec.region_lbas / lbas_per;
    if (blocks_in_region == 0) return Error{Errc::invalid_argument, "test region holds no complete block"};
    std::uint64_t seq_limit = blocks_in_region;
    if (spec.max_bytes) seq_limit = std::min<std::uint64_t>(seq_limit, std::max<std::uint64_t>(1, spec.max_bytes / spec.block_bytes));

    std::atomic<std::uint64_t> next{0}, bytes{0}, ops{0}, errors{0}, last_lba{spec.region_first_lba};
    std::atomic<bool> stop{false};
    std::mutex fm;
    std::condition_variable fcv;
    std::uint32_t finished = 0;
    std::optional<Error> fatal;
    std::vector<LatencyHistogram> hists(spec.queue_depth);
    const std::size_t align = std::max<std::size_t>(4096, ss);
    const std::uint64_t phase_seed = cfg_.seed ^ (0x9E37ull * (phase_index_ + 1));

    auto worker = [&](std::uint32_t t) {
        auto finish = [&] {
            std::lock_guard lk(fm);
            ++finished;
            fcv.notify_all();
        };
        Result<BlockDevicePtr> dev = Error{Errc::internal, "unused"};
        if (!spec.write) {
            dev = opener_();
            if (!dev || (*dev)->mode() != OpenMode::read_only) {
                std::lock_guard lk(fm);
                if (!fatal) fatal = dev ? Error{Errc::invalid_argument, "worker handle is not read-only"} : dev.error();
                stop = true;
                ++finished;
                fcv.notify_all();
                return;
            }
        } else if (!auth_) {
            std::lock_guard lk(fm);
            if (!fatal) fatal = Error{Errc::refused, "write phase without an authorized target"};
            stop = true;
            ++finished;
            fcv.notify_all();
            return;
        }
        AlignedBuffer buf(spec.block_bytes, align);
        if (spec.write) {
            // One block of incompressible data, generated before the clock starts
            // and re-used (first 8 bytes vary) so generation never limits the rate.
            ChaCha20 gen(ChaCha20::random_key(), ChaCha20::random_nonce());
            gen.generate(0, reinterpret_cast<std::uint8_t*>(buf.data()), spec.block_bytes);
        }
        std::uint64_t write_counter = 0;
        SplitMix64 rng{phase_seed + 0x1000ull * (t + 1)};
        auto& hist = hists[t];
        while (!stop.load(std::memory_order_relaxed) && !control.cancel_requested()) {
            std::uint64_t idx;
            if (spec.random) {
                idx = rng.below(blocks_in_region);
            } else {
                idx = next.fetch_add(1);
                if (idx >= seq_limit) break;
            }
            const std::uint64_t lba = spec.region_first_lba + idx * lbas_per;
            Result<IOResult> r = Error{Errc::internal, "unused"};
            if (spec.write) {
                const std::uint64_t tag = ++write_counter ^ (static_cast<std::uint64_t>(t) << 56);
                std::memcpy(buf.data(), &tag, sizeof tag);
                r = auth_->write(lba * ss, buf.data(), spec.block_bytes);
            } else {
                r = (*dev)->read(lba * ss, buf.data(), spec.block_bytes);
            }
            if (r) {
                hist.add(r->duration_ns / 1000);
                bytes.fetch_add(spec.block_bytes, std::memory_order_relaxed);
                ops.fetch_add(1, std::memory_order_relaxed);
                last_lba.store(lba + lbas_per - 1, std::memory_order_relaxed);
                continue;
            }
            const auto code = r.error().code;
            if (code == Errc::device_removed || code == Errc::not_found || code == Errc::refused) {
                std::lock_guard lk(fm);
                if (!fatal) fatal = Error{code, "device disappeared during the benchmark: " + r.error().describe()};
                stop = true;
                break;
            }
            if (errors.fetch_add(1) + 1 > kMaxErrors) {
                std::lock_guard lk(fm);
                if (!fatal)
                    fatal = Error{Errc::io_error, "more than " + std::to_string(kMaxErrors) +
                                                      (spec.write ? " write errors; the device is not healthy enough to benchmark"
                                                                  : " read errors; run a surface scan before benchmarking")};
                stop = true;
                break;
            }
        }
        finish();
    };

    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(spec.queue_depth);
    for (std::uint32_t t = 0; t < spec.queue_depth; ++t) threads.emplace_back(worker, t);

    auto last_sample = start;
    std::uint64_t last_bytes = 0;
    auto sample = [&](std::chrono::steady_clock::time_point now) {
        const double dt = std::chrono::duration<double>(now - last_sample).count();
        const std::uint64_t b = bytes.load();
        if (dt <= 0) return;
        const double rate = static_cast<double>(b - last_bytes) / dt;
        add_completed(b - last_bytes, last_lba.load());
        std::lock_guard lk(m_);
        if (!single_point) {
            res_.timeline.push_back({std::chrono::duration<double>(now - t0_).count(), rate});
            if (spec.record_positions) res_.positions.push_back({last_lba.load(), rate});
        }
        last_sample = now;
        last_bytes = b;
    };

    {
        std::unique_lock lk(fm);
        while (finished < spec.queue_depth) {
            fcv.wait_for(lk, std::chrono::milliseconds(20));
            const auto now = std::chrono::steady_clock::now();
            const double elapsed = std::chrono::duration<double>(now - start).count();
            if (spec.max_seconds > 0 && elapsed >= spec.max_seconds) stop = true;
            if (control.cancel_requested()) stop = true;
            double frac = spec.max_seconds > 0 ? elapsed / spec.max_seconds : 0;
            if (!spec.random) frac = std::max(frac, static_cast<double>(next.load()) / static_cast<double>(seq_limit));
            {
                std::lock_guard rl(m_);
                phase_fraction_ = std::min(frac, 1.0);
            }
            if (now - last_sample >= kSampleEvery) {
                lk.unlock();
                sample(now);
                lk.lock();
            }
        }
    }
    for (auto& t : threads) t.join();
    if (spec.write && auth_) {
        // The flush belongs to the measurement: data that is only in a cache has not been written.
        if (auto f = auth_->flush(); !f) {
            std::lock_guard lk(fm);
            if (!fatal) fatal = Error{f.error().code, "flush after the write phase failed: " + f.error().describe()};
        }
    }
    const auto end = std::chrono::steady_clock::now();
    if (end - last_sample >= std::chrono::milliseconds(20) || res_.timeline.empty()) sample(end);
    else add_completed(bytes.load() - last_bytes, last_lba.load());

    LatencyHistogram merged;
    for (const auto& h : hists) merged.merge(h);
    const auto wall_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
    const auto m = compute_metrics(merged, ops.load(), bytes.load(), errors.load(), wall_ns);
    if (errors.load()) add_error(errors.load());
    if (out) *out = m;

    {
        std::lock_guard lk(m_);
        all_.merge(merged);
        if (single_point) {
            single_point->lba = spec.region_first_lba;
            single_point->bytes_per_sec = wall_ns ? static_cast<double>(m.bytes) * 1e9 / static_cast<double>(wall_ns) : 0;
        } else {
            res_.phases.push_back({spec.label, spec.block_bytes, spec.queue_depth, m});
        }
        res_.latency_buckets = all_.buckets();
    }
    if (fatal) return *fatal;
    if (control.cancel_requested()) return Error{Errc::cancelled, "cancelled"};
    return {};
}

Status Benchmark::do_run(OperationControl& control) {
    // Write benchmarks already hold the exclusive lease through their authorization.
    DeviceLeaseRegistry::Lease lease;
    if (!auth_) {
        auto l = DeviceLeaseRegistry::global().acquire(device_id(), access());
        if (!l) return l.error();
        lease = std::move(l).value();
    }
    t0_ = std::chrono::steady_clock::now();
    {
        std::lock_guard lk(m_);
        res_.started_utc = utc_now();
    }

    if (cfg_.kind == BenchKind::speed_curve) {
        // One aggregate phase; each point contributes a position sample.
        Metrics total;
        auto finish = [&] {
            std::lock_guard lk(m_);
            res_.phases.push_back({"Speed curve", cfg_.block_bytes, cfg_.queue_depth,
                                   compute_metrics(all_, total.ops, total.bytes, total.errors, total.wall_ns)});
        };
        for (std::size_t i = 0; i < plan_.size(); ++i) {
            {
                std::lock_guard lk(m_);
                phase_index_ = i;
                phase_fraction_ = 0;
            }
            PositionPoint pt;
            Metrics m;
            auto s = run_phase(plan_[i], control, &pt, &m);
            total.ops += m.ops;
            total.bytes += m.bytes;
            total.errors += m.errors;
            total.wall_ns += m.wall_ns;
            {
                std::lock_guard lk(m_);
                res_.positions.push_back(pt);
                res_.timeline.push_back(
                    {std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count(), pt.bytes_per_sec});
            }
            if (!s) {
                finish();
                return s;
            }
        }
        finish();
        std::lock_guard lk(m_);
        phase_index_ = plan_.size();
        return {};
    }

    for (std::size_t i = 0; i < plan_.size(); ++i) {
        {
            std::lock_guard lk(m_);
            phase_index_ = i;
            phase_fraction_ = 0;
        }
        if (auto s = run_phase(plan_[i], control, nullptr, nullptr); !s) return s;
    }
    std::lock_guard lk(m_);
    phase_index_ = plan_.size();
    return {};
}

BenchResult Benchmark::result() const {
    const auto p = progress();
    std::lock_guard lk(m_);
    BenchResult r = res_;
    r.state = p.state;
    r.message = p.message;
    return r;
}

double Benchmark::fraction() const {
    std::lock_guard lk(m_);
    if (plan_.empty()) return 0;
    return std::min(1.0, (static_cast<double>(phase_index_) + (phase_index_ < plan_.size() ? phase_fraction_ : 0.0)) /
                             static_cast<double>(plan_.size()));
}

Result<std::unique_ptr<Benchmark>> create_benchmark(const std::string& path, bool is_image, const BenchConfig& config,
                                                     std::uint32_t image_sector_size) {
    OpenOptions oo;
    oo.mode = OpenMode::read_only;  // never anything else for a read benchmark
    oo.direct_io = config.direct_io;
    auto open_with = [path, is_image, image_sector_size](OpenOptions o) -> Result<BlockDevicePtr> {
        return is_image ? open_image(path, o, image_sector_size) : open_device(path, o);
    };
    auto probe = open_with(oo);
    bool fell_back = false;
    if (!probe && oo.direct_io && probe.error().code == Errc::invalid_argument) {
        oo.direct_io = false;
        probe = open_with(oo);
        fell_back = true;
    }
    if (!probe) return probe.error();
    BenchConfig effective = config;
    effective.direct_io = oo.direct_io;
    auto b = std::make_unique<Benchmark>([open_with, oo] { return open_with(oo); }, effective, path, is_image);
    if (fell_back) b->note("Direct I/O is not supported for this target; fell back to buffered I/O.");
    return b;
}

}  // namespace platter::bench
