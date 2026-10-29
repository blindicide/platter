// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <fstream>
#include <set>
#include <thread>

#include "platter/erase/erase.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::erase;
using namespace std::chrono_literals;
using platter::safety::Kind;
using platter::safety::TestAccess;
using platter::test::FaultyWriteDevice;
using platter::test::TempDir;

namespace {

constexpr std::uint32_t kSector = 512;

void fill_file(const std::filesystem::path& p, std::uint64_t size, std::uint8_t v) {
    test::make_sparse_file(p, size);
    test::write_at(p, 0, std::vector<std::uint8_t>(static_cast<std::size_t>(size), v));
}

std::vector<std::uint8_t> slurp(const std::filesystem::path& p) {
    return test::read_at(p, 0, static_cast<std::size_t>(std::filesystem::file_size(p)));
}

BlockDevicePtr open_rw(const std::filesystem::path& p, std::uint32_t ss = kSector) {
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto d = open_image(p.string(), oo, ss);
    REQUIRE(d);
    return std::move(d).value();
}

EraseConfig small(Method m, VerifyMode v = VerifyMode::last_pass) {
    EraseConfig c;
    c.method = m;
    c.verify = v;
    c.block_bytes = 64 * 1024;
    return c;
}

std::unique_ptr<Erase> run(safety::AuthorizedTarget t, const EraseConfig& c) {
    auto e = std::make_unique<Erase>(std::move(t), c);
    auto v = e->validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(e->start());
    e->wait();
    return e;
}

}  // namespace

TEST_CASE("method names round-trip and passes match the descriptions", "[erase]") {
    for (int i = 0; i < kMethodCount; ++i) {
        const auto m = static_cast<Method>(i);
        auto back = parse_method(method_name(m));
        REQUIRE(back);
        CHECK(*back == m);
    }
    CHECK_FALSE(parse_method("shred"));

    CHECK(passes_for(small(Method::zero)).size() == 1);
    CHECK(passes_for(small(Method::dod_3pass)).size() == 3);
    CHECK(passes_for(small(Method::dod_7pass)).size() == 7);
    auto rn = small(Method::random_n);
    rn.random_passes = 4;
    CHECK(passes_for(rn).size() == 4);
    auto p3 = passes_for(small(Method::dod_3pass));
    CHECK(p3[0].kind == Pass::Kind::fill);
    CHECK(p3[0].fill == 0x00);
    CHECK(p3[1].fill == 0xFF);
    CHECK(p3[2].kind == Pass::Kind::random);

    CHECK(parse_verify_mode("last") == VerifyMode::last_pass);
    CHECK(parse_verify_mode("every") == VerifyMode::every_pass);
    CHECK(parse_verify_mode("none") == VerifyMode::none);
    CHECK_FALSE(parse_verify_mode("sometimes"));
}

TEST_CASE("zero fill erases the whole image and verifies", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "z.img";
    fill_file(img, 1 << 20, 0xAA);

    auto e = run(TestAccess::make(open_rw(img), Kind::erase), small(Method::zero));
    CHECK(e->state() == OperationState::completed);
    auto data = slurp(img);
    CHECK(std::all_of(data.begin(), data.end(), [](auto b) { return b == 0; }));

    auto r = e->result();
    CHECK(r.verdict == Verdict::erased_verified);
    REQUIRE(r.passes.size() == 1);
    CHECK(r.passes[0].bytes_written == (1u << 20));
    CHECK(r.passes[0].verified);
    CHECK(r.passes[0].verify_bytes == (1u << 20));
    CHECK(r.passes[0].mismatched_sectors == 0);
    CHECK(r.whole_device);
    CHECK_FALSE(r.started_utc.empty());
    CHECK_FALSE(r.finished_utc.empty());
    const auto p = e->progress();
    CHECK(p.completed_bytes == p.total_bytes);
    CHECK(p.total_bytes == 2u << 20);  // write + verify
}

TEST_CASE("one fill writes 0xFF", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "o.img";
    fill_file(img, 256 * 1024, 0x00);
    auto e = run(TestAccess::make(open_rw(img), Kind::erase), small(Method::one));
    auto data = slurp(img);
    CHECK(std::all_of(data.begin(), data.end(), [](auto b) { return b == 0xFF; }));
    CHECK(e->result().verdict == Verdict::erased_verified);
}

TEST_CASE("no verification gives an honest 'not verified' verdict", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "n.img";
    fill_file(img, 128 * 1024, 0x55);
    auto e = run(TestAccess::make(open_rw(img), Kind::erase), small(Method::zero, VerifyMode::none));
    auto r = e->result();
    CHECK(r.verdict == Verdict::erased_unverified);
    CHECK_FALSE(r.passes[0].verified);
    CHECK(e->progress().total_bytes == 128u * 1024);
}

TEST_CASE("random pass leaves unpredictable data and verifies against the same stream", "[erase]") {
    TempDir tmp;
    const auto a = tmp / "a.img";
    const auto b = tmp / "b.img";
    fill_file(a, 512 * 1024, 0);
    fill_file(b, 512 * 1024, 0);

    auto ea = run(TestAccess::make(open_rw(a), Kind::erase), small(Method::random));
    auto eb = run(TestAccess::make(open_rw(b), Kind::erase), small(Method::random));
    CHECK(ea->result().verdict == Verdict::erased_verified);
    CHECK(eb->result().verdict == Verdict::erased_verified);

    auto da = slurp(a), db = slurp(b);
    CHECK(da != db);  // independent keys
    std::set<std::uint8_t> distinct(da.begin(), da.end());
    CHECK(distinct.size() > 250);
    // ~50% of bits set
    std::size_t ones = 0;
    for (auto v : da) ones += static_cast<std::size_t>(std::popcount(static_cast<unsigned>(v)));
    const double frac = static_cast<double>(ones) / (static_cast<double>(da.size()) * 8.0);
    CHECK(frac > 0.49);
    CHECK(frac < 0.51);
}

TEST_CASE("3-pass leaves the random pass; verify every pass checks all of them", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    fill_file(img, 256 * 1024, 0x11);
    auto e = run(TestAccess::make(open_rw(img), Kind::erase), small(Method::dod_3pass, VerifyMode::every_pass));
    auto r = e->result();
    REQUIRE(r.passes.size() == 3);
    for (const auto& p : r.passes) {
        CHECK(p.bytes_written == 256u * 1024);
        CHECK(p.verified);
        CHECK(p.mismatched_sectors == 0);
    }
    CHECK(r.verdict == Verdict::erased_verified);
    CHECK(e->progress().total_bytes == 6u * 256 * 1024);
    auto data = slurp(img);
    std::set<std::uint8_t> distinct(data.begin(), data.end());
    CHECK(distinct.size() > 200);
}

TEST_CASE("7-pass runs seven passes", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "d7.img";
    fill_file(img, 64 * 1024, 0x77);
    auto e = run(TestAccess::make(open_rw(img), Kind::erase), small(Method::dod_7pass));
    CHECK(e->result().passes.size() == 7);
    CHECK(e->result().verdict == Verdict::erased_verified);
    CHECK(e->state() == OperationState::completed);
}

TEST_CASE("custom pattern is position addressed across block boundaries", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "p.img";
    fill_file(img, 256 * 1024, 0xFF);
    auto c = small(Method::pattern);
    c.pattern = {0xDE, 0xAD, 0xBE};  // odd length: phase must carry across 64 KiB blocks
    auto e = run(TestAccess::make(open_rw(img), Kind::erase), c);
    CHECK(e->result().verdict == Verdict::erased_verified);
    auto data = slurp(img);
    for (std::size_t i = 0; i < data.size(); ++i) {
        REQUIRE(data[i] == c.pattern[i % 3]);
    }
}

TEST_CASE("only the confirmed ranges are erased", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "r.img";
    fill_file(img, 256 * 1024, 0xAA);  // 512 sectors
    const auto before = slurp(img);

    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    auto* raw = dev.get();
    auto e = run(TestAccess::make(std::move(dev), Kind::erase, {LbaRange{10, 20}, LbaRange{300, 5}}),
                 small(Method::zero));
    CHECK(e->state() == OperationState::completed);
    CHECK_FALSE(e->result().whole_device);

    auto after = slurp(img);
    for (std::size_t lba = 0; lba < 512; ++lba) {
        const bool inside = (lba >= 10 && lba < 30) || (lba >= 300 && lba < 305);
        for (std::size_t i = 0; i < kSector; ++i) {
            const auto idx = lba * kSector + i;
            REQUIRE(after[idx] == (inside ? 0 : before[idx]));
        }
    }
    // The low-level write log agrees: nothing outside was even attempted.
    for (const auto& w : raw->write_log()) {
        const auto first = w.offset / kSector, last = (w.offset + w.length - 1) / kSector;
        const bool in1 = first >= 10 && last < 30, in2 = first >= 300 && last < 305;
        CHECK((in1 || in2));
    }
    CHECK(e->progress().total_bytes == 2u * 25 * kSector);
}

TEST_CASE("verification catches a sector that silently did not store", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "drop.img";
    fill_file(img, 256 * 1024, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->dropped_lbas = {37};
    auto e = run(TestAccess::make(std::move(dev), Kind::erase), small(Method::zero));

    CHECK(e->state() == OperationState::failed);
    auto r = e->result();
    CHECK(r.verdict == Verdict::erased_with_errors);
    REQUIRE(r.passes.size() == 1);
    CHECK(r.passes[0].mismatched_sectors == 1);
    REQUIRE(r.passes[0].first_bad_offset);
    CHECK(*r.passes[0].first_bad_offset == 37 * kSector);
    REQUIRE(r.bad_verify.size() == 1);
    CHECK(r.bad_verify[0].first == 37);
    CHECK(r.bad_verify[0].count == 1);
}

TEST_CASE("a sector that cannot be written is isolated, not fatal", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "bad.img";
    fill_file(img, 256 * 1024, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->bad_write_lbas = {5, 6, 200};
    auto e = run(TestAccess::make(std::move(dev), Kind::erase), small(Method::zero, VerifyMode::none));

    CHECK(e->state() == OperationState::failed);  // completed with errors
    auto r = e->result();
    CHECK(r.verdict == Verdict::erased_with_errors);
    CHECK(r.passes[0].unwritable_sectors == 3);
    REQUIRE(r.unwritable.size() == 2);
    CHECK(r.unwritable[0].first == 5);
    CHECK(r.unwritable[0].count == 2);  // merged
    CHECK(r.unwritable[1].first == 200);
    CHECK(r.passes[0].bytes_written == 256u * 1024 - 3 * kSector);

    auto data = slurp(img);
    for (std::size_t lba = 0; lba < 512; ++lba) {
        const bool bad = lba == 5 || lba == 6 || lba == 200;
        CHECK(data[lba * kSector] == (bad ? 0xAA : 0x00));
    }
    CHECK(e->progress().errors == 3);
}

TEST_CASE("too many bad sectors aborts the erase", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "many.img";
    fill_file(img, 256 * 1024, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    for (std::uint64_t l = 0; l < 100; ++l) dev->bad_write_lbas.insert(l);
    auto c = small(Method::zero);
    c.max_errors = 10;
    auto e = run(TestAccess::make(std::move(dev), Kind::erase), c);
    CHECK(e->state() == OperationState::failed);
    CHECK(e->result().verdict == Verdict::incomplete);
    CHECK(e->progress().message.find("more than 10") != std::string::npos);
}

TEST_CASE("an unreadable sector during verification is reported", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "ur.img";
    fill_file(img, 128 * 1024, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->bad_read_lbas = {12};
    auto e = run(TestAccess::make(std::move(dev), Kind::erase), small(Method::zero));
    auto r = e->result();
    CHECK(r.verdict == Verdict::erased_with_errors);
    CHECK(r.passes[0].unreadable_sectors == 1);
    CHECK(r.passes[0].mismatched_sectors == 0);
    REQUIRE(r.passes[0].first_bad_offset);
    CHECK(*r.passes[0].first_bad_offset == 12 * kSector);
}

TEST_CASE("a vanishing device stops the erase at once", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "gone.img";
    fill_file(img, 1 << 20, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    auto* raw = dev.get();
    dev->remove_after_writes = 3;
    auto e = run(TestAccess::make(std::move(dev), Kind::erase), small(Method::zero));
    CHECK(e->state() == OperationState::failed);
    CHECK(e->result().verdict == Verdict::incomplete);
    CHECK(raw->writes() == 4);  // no sector-by-sector retry storm against a dead device
}

TEST_CASE("cancel stops an erase between requests", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "c.img";
    fill_file(img, 4 << 20, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->write_delay = 5ms;
    auto c = small(Method::zero);
    c.block_bytes = 4096;
    auto e = std::make_unique<Erase>(TestAccess::make(std::move(dev), Kind::erase), c);
    REQUIRE(e->validate());
    REQUIRE(e->start());
    std::this_thread::sleep_for(60ms);
    e->cancel();
    e->wait();
    CHECK(e->state() == OperationState::cancelled);
    CHECK(e->result().verdict == Verdict::incomplete);
    CHECK(e->progress().completed_bytes < e->progress().total_bytes);
}

TEST_CASE("discard on an image punches holes that read back as zeros", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "t.img";
    fill_file(img, 1 << 20, 0xAA);
    auto e = std::make_unique<Erase>(TestAccess::make(open_rw(img), Kind::erase), small(Method::discard));
    REQUIRE(e->validate());
    REQUIRE(e->start());
    e->wait();
    const auto why = e->progress().message + " " + e->result().failure;
    if (e->state() == OperationState::failed &&
        (why.find("fallocate") != std::string::npos || why.find("not supported") != std::string::npos))
        { SUCCEED("this platform or filesystem cannot discard / punch holes"); return; }
    CHECK(e->state() == OperationState::completed);
    auto r = e->result();
    REQUIRE(r.passes.size() == 1);
    REQUIRE(r.passes[0].reads_back_zero);
    CHECK(*r.passes[0].reads_back_zero);
    CHECK(r.verdict == Verdict::erased_verified);
}

TEST_CASE("discard that leaves data readable is reported as such", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "t2.img";
    fill_file(img, 256 * 1024, 0xAA);
    class NoOpDiscard final : public IBlockDevice {
    public:
        explicit NoOpDiscard(BlockDevicePtr d) : d_(std::move(d)) {}
        const DeviceInfo& info() const noexcept override { return d_->info(); }
        OpenMode mode() const noexcept override { return d_->mode(); }
        std::uint32_t required_alignment() const noexcept override { return d_->required_alignment(); }
        Result<IOResult> read(std::uint64_t o, void* b, std::size_t l) override { return d_->read(o, b, l); }
        Result<IOResult> write(std::uint64_t o, const void* b, std::size_t l) override { return d_->write(o, b, l); }
        Status flush() override { return d_->flush(); }
        Status discard(std::uint64_t, std::uint64_t) override { return {}; }  // claims success, does nothing
    private:
        BlockDevicePtr d_;
    };
    auto e = run(TestAccess::make(std::make_unique<NoOpDiscard>(open_rw(img)), Kind::erase), small(Method::discard));
    auto r = e->result();
    REQUIRE(r.passes[0].reads_back_zero);
    CHECK_FALSE(*r.passes[0].reads_back_zero);
    CHECK(r.verdict == Verdict::erased_with_errors);
}

TEST_CASE("discard without backend support fails cleanly", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "t3.img";
    fill_file(img, 256 * 1024, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->discard_ok = false;
    auto e = run(TestAccess::make(std::move(dev), Kind::erase), small(Method::discard));
    CHECK(e->state() == OperationState::failed);
    CHECK(e->result().verdict == Verdict::incomplete);
}

TEST_CASE("invalid configurations are rejected at validation", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "v.img";
    fill_file(img, 128 * 1024, 0);

    auto try_cfg = [&](EraseConfig c) {
        Erase e(TestAccess::make(open_rw(img), Kind::erase), c);
        return e.validate();
    };
    auto c = small(Method::zero);
    c.block_bytes = 1000;  // not a sector multiple
    CHECK_FALSE(try_cfg(c));
    c.block_bytes = 128u << 20;
    CHECK_FALSE(try_cfg(c));
    c = small(Method::pattern);
    CHECK_FALSE(try_cfg(c));  // empty pattern
    c.pattern.assign(5000, 1);
    CHECK_FALSE(try_cfg(c));
    c = small(Method::random_n);
    c.random_passes = 0;
    CHECK_FALSE(try_cfg(c));
    c.random_passes = 100;
    CHECK_FALSE(try_cfg(c));
    CHECK(try_cfg(small(Method::zero)));
}

TEST_CASE("an authorization for another kind of operation cannot erase", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "k.img";
    fill_file(img, 128 * 1024, 0xAA);
    Erase e(TestAccess::make(open_rw(img), Kind::partition_edit), small(Method::zero));
    auto v = e.validate();
    REQUIRE_FALSE(v);
    CHECK(v.error().code == Errc::refused);
    CHECK(slurp(img)[0] == 0xAA);
}

TEST_CASE("erase through the full pipeline: preview, confirm, authorize, run", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "pipeline.img";
    fill_file(img, 1 << 20, 0xAA);

    safety::Request req;
    req.kind = Kind::erase;
    req.target.path = img.string();
    req.target.is_image = true;
    req.summary = "zero fill + verify";
    auto preview = safety::make_preview(req);
    REQUIRE(preview);
    auto target = safety::authorize(*preview, "pipeline.img");
    REQUIRE(target);
    auto e = run(std::move(target).value(), small(Method::zero));
    CHECK(e->result().verdict == Verdict::erased_verified);
    auto data = slurp(img);
    CHECK(std::all_of(data.begin(), data.end(), [](auto b) { return b == 0; }));
}

TEST_CASE("4096-byte sector images erase and verify", "[erase]") {
    TempDir tmp;
    const auto img = tmp / "s4k.img";
    fill_file(img, 1 << 20, 0xAA);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img, 4096));
    dev->dropped_lbas = {3};
    auto e = run(TestAccess::make(std::move(dev), Kind::erase), small(Method::zero));
    auto r = e->result();
    REQUIRE(r.passes[0].mismatched_sectors == 1);
    CHECK(*r.passes[0].first_bad_offset == 3 * 4096);
}
