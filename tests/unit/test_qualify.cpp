// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include "platter/qualify/qualify.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::qualify;
using platter::safety::Kind;
using platter::safety::TestAccess;
using platter::test::FaultyWriteDevice;
using platter::test::TempDir;

namespace {

BlockDevicePtr open_rw(const std::filesystem::path& p, std::uint32_t ss = 512) {
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto d = open_image(p.string(), oo, ss);
    REQUIRE(d);
    return std::move(d).value();
}

struct Run {
    std::unique_ptr<Qualification> op;
    QualifyResult result;
};

Run run(BlockDevicePtr dev, const QualifyConfig& c, std::vector<LbaRange> ranges = {}, Kind kind = Kind::qualification) {
    Run r;
    r.op = std::make_unique<Qualification>(TestAccess::make(std::move(dev), kind, std::move(ranges)), c);
    auto v = r.op->validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(r.op->start());
    r.op->wait();
    r.result = r.op->result();
    return r;
}

QualifyConfig small_config() {
    QualifyConfig c;
    c.block_bytes = 64 * 1024;
    c.seek_samples = 32;
    // Host scheduling noise on a loaded CI box must not look like a slow drive.
    c.slow_block_ms = 60'000;
    c.seek_slow_ms = 60'000;
    return c;
}

bool mentions(const QualifyResult& r, const std::string& needle, Finding::Severity sev) {
    for (const auto& f : r.findings)
        if (f.severity == sev && f.text.find(needle) != std::string::npos) return true;
    return false;
}

std::string fixture(const std::string& name) {
    std::ifstream in(std::string(PLATTER_FIXTURES_DIR) + "/smart/" + name, std::ios::binary);
    REQUIRE(in);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

StageResult clean_stage(StageKind k, const std::string& name) {
    StageResult s;
    s.kind = k;
    s.name = name;
    s.completed = true;
    s.blocks = 100;
    s.bytes = 100u << 20;
    s.avg_mbps = s.head_mbps = s.tail_mbps = s.max_mbps = 150;
    s.min_mbps = 120;
    return s;
}

QualifyResult clean_result() {
    QualifyResult r;
    r.state = OperationState::completed;
    r.stages = {clean_stage(StageKind::write, "Pass 1 write"), clean_stage(StageKind::verify, "Pass 1 read-back")};
    return r;
}

}  // namespace

TEST_CASE("qualification of a healthy image passes and exercises every stage", "[qualify]") {
    TempDir tmp;
    const auto img = tmp / "ok.img";
    test::make_sparse_file(img, 8ull << 20);
    auto c = small_config();
    auto r = run(open_rw(img), c);
    REQUIRE(r.result.state == OperationState::completed);
    CHECK(r.result.verdict == Verdict::pass);
    // 2 passes x (write + verify) + final read + seek
    REQUIRE(r.result.stages.size() == 6);
    for (const auto& s : r.result.stages) {
        INFO(s.name);
        CHECK(s.completed);
        CHECK(s.io_errors == 0);
        CHECK(s.mismatches == 0);
        CHECK(s.blocks > 0);
    }
    CHECK(r.result.stages[0].kind == StageKind::write);
    CHECK(r.result.stages[0].bytes == 8ull << 20);
    CHECK(r.result.stages[1].kind == StageKind::verify);
    CHECK(r.result.stages[4].kind == StageKind::read);
    CHECK(r.result.stages[5].kind == StageKind::seek);
    CHECK(r.result.stages[5].blocks == 32);
    CHECK(r.result.stages[0].avg_mbps > 0);
    CHECK(r.result.bad_ranges.empty());
    CHECK_FALSE(r.result.started_utc.empty());
    CHECK_FALSE(r.result.finished_utc.empty());
    // The image now holds the last pass (the complement stream): not zeros.
    const auto data = test::read_at(img, 0, 4096);
    CHECK(std::any_of(data.begin(), data.end(), [](std::uint8_t b) { return b != 0; }));
    // Progress ran to the end.
    CHECK(r.op->progress().completed_bytes == r.op->progress().total_bytes);
    CHECK(r.op->stage_position() == std::make_pair<std::size_t, std::size_t>(6, 6));

    const auto text = result_to_text(r.result);
    CHECK(text.find("PASS") != std::string::npos);
    CHECK(text.find("Serial:    (omitted)") != std::string::npos);
    CHECK(text.find("Random read sampling") != std::string::npos);
}

namespace {
// Records every write so a test can compare what successive passes sent.
class CaptureDevice final : public IBlockDevice {
public:
    explicit CaptureDevice(BlockDevicePtr inner) : inner_(std::move(inner)) {}
    struct W {
        std::uint64_t offset;
        std::vector<std::uint8_t> data;
    };
    std::vector<W> writes;
    const DeviceInfo& info() const noexcept override { return inner_->info(); }
    OpenMode mode() const noexcept override { return inner_->mode(); }
    std::uint32_t required_alignment() const noexcept override { return inner_->required_alignment(); }
    Result<IOResult> read(std::uint64_t o, void* b, std::size_t n) override { return inner_->read(o, b, n); }
    Result<IOResult> write(std::uint64_t o, const void* b, std::size_t n) override {
        const auto* p = static_cast<const std::uint8_t*>(b);
        writes.push_back({o, std::vector<std::uint8_t>(p, p + n)});
        return inner_->write(o, b, n);
    }
    Status flush() override { return inner_->flush(); }

private:
    BlockDevicePtr inner_;
};
}  // namespace

TEST_CASE("pass 2 writes the bitwise complement of pass 1", "[qualify]") {
    TempDir tmp;
    const auto img = tmp / "bits.img";
    test::make_sparse_file(img, 1ull << 20);
    auto cap = std::make_unique<CaptureDevice>(open_rw(img));
    auto* raw = cap.get();
    auto c = small_config();
    c.verify_each_pass = false;
    c.final_read = false;
    c.seek_samples = 0;
    c.write_passes = 4;
    auto r = run(std::move(cap), c);
    REQUIRE(r.result.state == OperationState::completed);
    const std::size_t per_pass = (1u << 20) / c.block_bytes;
    REQUIRE(raw->writes.size() == 4 * per_pass);
    bool any_nonzero = false;
    for (std::size_t i = 0; i < per_pass; ++i) {
        const auto& p1 = raw->writes[i];
        const auto& p2 = raw->writes[per_pass + i];
        const auto& p3 = raw->writes[2 * per_pass + i];
        const auto& p4 = raw->writes[3 * per_pass + i];
        REQUIRE(p1.offset == p2.offset);
        REQUIRE(p1.data.size() == p2.data.size());
        for (std::size_t k = 0; k < p1.data.size(); ++k) {
            REQUIRE(static_cast<std::uint8_t>(~p1.data[k]) == p2.data[k]);
            REQUIRE(static_cast<std::uint8_t>(~p3.data[k]) == p4.data[k]);
        }
        any_nonzero |= std::any_of(p1.data.begin(), p1.data.end(), [](std::uint8_t b) { return b != 0; });
        CHECK(p1.data != p3.data);   // a different random stream
    }
    CHECK(any_nonzero);
}

TEST_CASE("dropped writes are caught by the read-back and reported by range", "[qualify][faults]") {
    TempDir tmp;
    const auto img = tmp / "drop.img";
    test::make_sparse_file(img, 4ull << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->dropped_lbas = {100, 101, 5000};
    auto c = small_config();
    auto r = run(std::move(dev), c);
    CHECK(r.result.verdict == Verdict::fail);
    // both passes' read-back sees all three sectors
    CHECK(r.result.stages[1].mismatches == 3);
    CHECK(r.result.stages[3].mismatches == 3);
    CHECK(r.result.stages[1].first_bad_offset == 100u * 512);
    REQUIRE_FALSE(r.result.bad_ranges.empty());
    CHECK(r.result.bad_ranges.front() == LbaRange{100, 2});
    CHECK(mentions(r.result, "read back different", Finding::Severity::failure));
    CHECK(r.op->state() == OperationState::failed);   // "completed with errors" is reported as a failure status
    CHECK(result_to_text(r.result).find("FAIL") != std::string::npos);
}

TEST_CASE("unwritable and unreadable sectors fail the run with exact counts", "[qualify][faults]") {
    TempDir tmp;
    {
        const auto img = tmp / "bw.img";
        test::make_sparse_file(img, 2ull << 20);
        auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
        dev->bad_write_lbas = {40};
        auto c = small_config();
        c.write_passes = 1;
        auto r = run(std::move(dev), c);
        CHECK(r.result.verdict == Verdict::fail);
        CHECK(r.result.stages[0].io_errors == 1);
        CHECK(r.result.stages[0].first_bad_offset == 40u * 512);
        CHECK(mentions(r.result, "could not be written", Finding::Severity::failure));
    }
    {
        const auto img = tmp / "br.img";
        test::make_sparse_file(img, 2ull << 20);
        auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
        dev->bad_read_lbas = {7};
        auto c = small_config();
        c.write_passes = 1;
        auto r = run(std::move(dev), c);
        CHECK(r.result.verdict == Verdict::fail);
        CHECK(r.result.stages[1].io_errors == 1);   // read-back
        CHECK(r.result.stages[2].io_errors == 1);   // final read
        CHECK(mentions(r.result, "could not be read", Finding::Severity::failure));
    }
}

TEST_CASE("too many errors abort the run", "[qualify][faults]") {
    TempDir tmp;
    const auto img = tmp / "many.img";
    test::make_sparse_file(img, 2ull << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    for (std::uint64_t l = 0; l < 64; ++l) dev->bad_write_lbas.insert(l);
    auto c = small_config();
    c.max_errors = 4;
    auto r = run(std::move(dev), c);
    CHECK(r.op->state() == OperationState::failed);
    CHECK(r.result.verdict == Verdict::fail);
    CHECK(r.result.stages.size() == 6);
    CHECK_FALSE(r.result.stages[0].completed);
    CHECK_FALSE(r.result.stages[5].completed);
}

TEST_CASE("slow blocks are measured, warned about and can fail the run", "[qualify][latency]") {
    TempDir tmp;
    const auto img = tmp / "slow.img";
    test::make_sparse_file(img, 512 * 1024);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->write_delay = std::chrono::milliseconds(4);
    auto c = small_config();
    c.write_passes = 1;
    c.verify_each_pass = false;
    c.final_read = false;
    c.seek_samples = 0;
    c.slow_block_ms = 2;   // every delayed write counts as slow
    auto r = run(std::move(dev), c);
    REQUIRE(r.result.stages.size() == 1);
    CHECK(r.result.stages[0].slow_blocks == r.result.stages[0].blocks);
    CHECK(r.result.stages[0].p99_us >= 4000);
    CHECK(r.result.verdict == Verdict::fail);
    CHECK(mentions(r.result, "took 2 ms or longer", Finding::Severity::failure));
}

TEST_CASE("cancelling leaves an incomplete, not a failed, verdict", "[qualify][cancel]") {
    TempDir tmp;
    const auto img = tmp / "cancel.img";
    test::make_sparse_file(img, 16ull << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->write_delay = std::chrono::milliseconds(15);
    auto c = small_config();
    Qualification q(TestAccess::make(std::move(dev), Kind::qualification), c);
    REQUIRE(q.validate());
    REQUIRE(q.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    q.cancel();
    q.wait();
    const auto r = q.result();
    CHECK(q.state() == OperationState::cancelled);
    CHECK(r.verdict == Verdict::incomplete);
    CHECK_FALSE(r.stages[0].completed);
    CHECK(mentions(r, "cancelled", Finding::Severity::info));
}

TEST_CASE("only a qualification authorization runs, and only inside it", "[qualify][refusal]") {
    TempDir tmp;
    const auto img = tmp / "r.img";
    test::make_sparse_file(img, 4ull << 20);
    {
        Qualification q(TestAccess::make(open_rw(img), Kind::erase), small_config());
        auto v = q.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        auto c = small_config();
        c.write_passes = 0;
        Qualification q(TestAccess::make(open_rw(img), Kind::qualification), c);
        REQUIRE_FALSE(q.validate());
    }
    {
        auto c = small_config();
        c.write_passes = 5;
        Qualification q(TestAccess::make(open_rw(img), Kind::qualification), c);
        REQUIRE_FALSE(q.validate());
    }
    {
        auto c = small_config();
        c.block_bytes = 1000;
        Qualification q(TestAccess::make(open_rw(img), Kind::qualification), c);
        REQUIRE_FALSE(q.validate());
    }
    {
        auto c = small_config();
        c.slow_fraction_warn = 0.5;
        c.slow_fraction_fail = 0.1;
        Qualification q(TestAccess::make(open_rw(img), Kind::qualification), c);
        REQUIRE_FALSE(q.validate());
    }
    // A partial range: nothing outside it is written.
    const auto before = test::read_at(img, 0, 1ull << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    auto* raw = dev.get();
    auto c = small_config();
    auto r = run(std::move(dev), c, {LbaRange{1024, 1024}});
    CHECK(r.result.state == OperationState::completed);
    CHECK_FALSE(r.result.whole_device);
    for (const auto& w : raw->write_log()) {
        CHECK(w.offset >= 1024u * 512);
        CHECK(w.offset + w.length <= 2048u * 512);
    }
    CHECK(test::read_at(img, 0, 1024 * 512) == std::vector<std::uint8_t>(before.begin(), before.begin() + 1024 * 512));
}

TEST_CASE("evaluate: verdict rules", "[qualify][verdict]") {
    {
        auto r = clean_result();
        evaluate(r);
        CHECK(r.verdict == Verdict::pass);
    }
    {   // tail much slower than head: warning only
        auto r = clean_result();
        r.stages[0].tail_mbps = 20;
        evaluate(r);
        CHECK(r.verdict == Verdict::pass_with_warnings);
        CHECK(mentions(r, "last tenth", Finding::Severity::warning));
    }
    {   // a few slow blocks: warning; many: failure
        auto r = clean_result();
        r.stages[0].slow_blocks = 1;   // 1 of 100 = 1 % > 0.1 % warn, < 2 % fail
        evaluate(r);
        CHECK(r.verdict == Verdict::pass_with_warnings);
        r.stages[0].slow_blocks = 5;
        evaluate(r);
        CHECK(r.verdict == Verdict::fail);
    }
    {   // a single slow block among many is only information
        auto r = clean_result();
        r.stages[0].blocks = 10000;
        r.stages[0].slow_blocks = 3;
        evaluate(r);
        CHECK(r.verdict == Verdict::pass);
        CHECK(mentions(r, "within tolerance", Finding::Severity::info));
    }
    {   // unfinished stage
        auto r = clean_result();
        r.stages[1].completed = false;
        evaluate(r);
        CHECK(r.verdict == Verdict::incomplete);
    }
    {   // an error beats everything, even an unfinished run
        auto r = clean_result();
        r.stages[1].completed = false;
        r.stages[0].mismatches = 1;
        evaluate(r);
        CHECK(r.verdict == Verdict::fail);
    }
    {   // no stages at all is not a pass
        QualifyResult r;
        r.state = OperationState::completed;
        evaluate(r);
        CHECK(r.verdict == Verdict::incomplete);
    }
}

TEST_CASE("SMART snapshots and comparison", "[qualify][smart]") {
    auto rep = smart::parse_smartctl_json(fixture("ata_pending.json"));
    REQUIRE(rep);
    const auto snap = smart_snapshot(*rep);
    CHECK(snap.valid);
    REQUIRE(snap.pending_sectors);
    CHECK(*snap.pending_sectors > 0);
    REQUIRE(snap.reallocated_sectors);

    auto nv = smart::parse_smartctl_json(fixture("nvme_healthy.json"));
    REQUIRE(nv);
    const auto nsnap = smart_snapshot(*nv);
    CHECK(nsnap.valid);
    CHECK(nsnap.nvme_media_errors.has_value());

    SmartSnapshot before;
    before.valid = true;
    before.reallocated_sectors = 0;
    before.pending_sectors = 0;
    before.udma_crc_errors = 0;
    SmartSnapshot after = before;
    {
        auto f = compare_smart(before, after);
        REQUIRE(f.size() == 1);
        CHECK(f[0].severity == Finding::Severity::info);
    }
    after.reallocated_sectors = 8;
    after.udma_crc_errors = 2;
    {
        auto f = compare_smart(before, after);
        bool fail = false, warn = false;
        for (const auto& x : f) {
            fail |= x.severity == Finding::Severity::failure && x.text.find("Reallocated") != std::string::npos;
            warn |= x.severity == Finding::Severity::warning && x.text.find("CRC") != std::string::npos;
        }
        CHECK(fail);
        CHECK(warn);
    }
    CHECK(compare_smart(SmartSnapshot{}, after).empty());

    // SMART growth fails an otherwise perfect run; a missing snapshot does not.
    auto r = clean_result();
    r.smart_before = before;
    r.smart_after = after;
    evaluate(r);
    CHECK(r.verdict == Verdict::fail);
    auto r2 = clean_result();
    evaluate(r2);
    CHECK(r2.verdict == Verdict::pass);
    CHECK(mentions(r2, "SMART was not compared", Finding::Severity::info));

    // Snapshots handed to the operation are folded into its result.
    TempDir tmp;
    const auto img = tmp / "smart.img";
    test::make_sparse_file(img, 1ull << 20);
    Qualification q(TestAccess::make(open_rw(img), Kind::qualification), small_config());
    q.set_smart_before(before);
    REQUIRE(q.validate());   // validate must not discard the snapshot
    q.set_smart_after(after);
    REQUIRE(q.start());
    q.wait();
    CHECK(q.result().verdict == Verdict::fail);
}

#include "platter/reports/qualify_report.hpp"
#include "platter/storage/history.hpp"

TEST_CASE("qualification report: JSON round trip, digest and history", "[qualify][report]") {
    TempDir tmp;
    const auto img = tmp / "rep.img";
    test::make_sparse_file(img, 2ull << 20);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(img));
    dev->dropped_lbas = {9};
    auto c = small_config();
    c.write_passes = 1;
    auto run_result = run(std::move(dev), c);
    auto r = run_result.result;
    r.smart_before.valid = true;
    r.smart_before.pending_sectors = 0;
    r.smart_after = r.smart_before;
    r.smart_after.pending_sectors = 3;
    evaluate(r);

    const auto text = reports::qualify_to_json(r);
    CHECK(reports::qualify_digest_ok(text));
    CHECK(text.find("report_sha256") != std::string::npos);
    // serial numbers are not written unless asked for
    auto with_serial = r;
    with_serial.device.serial = "SECRET-123";
    CHECK(reports::qualify_to_json(with_serial).find("SECRET-123") == std::string::npos);

    auto back = reports::qualify_from_json(text);
    REQUIRE(back);
    CHECK(back->verdict == r.verdict);
    CHECK(back->stages.size() == r.stages.size());
    CHECK(back->stages[1].mismatches == r.stages[1].mismatches);
    CHECK(back->stages[1].first_bad_offset == r.stages[1].first_bad_offset);
    CHECK(back->bad_ranges == r.bad_ranges);
    CHECK(back->findings.size() == r.findings.size());
    CHECK(back->smart_after.pending_sectors == std::optional<std::uint64_t>(3));
    CHECK(back->config.block_bytes == c.block_bytes);
    // re-evaluating the loaded measurements gives the same verdict
    auto again = *back;
    evaluate(again);
    CHECK(again.verdict == r.verdict);

    auto tampered = text;
    tampered.replace(tampered.find("FAIL"), 4, "PASS");
    CHECK_FALSE(reports::qualify_digest_ok(tampered));
    CHECK_FALSE(reports::qualify_from_json("{\"kind\":\"erase-report\"}"));
    CHECK_FALSE(reports::qualify_from_json("not json"));

    auto store = storage::HistoryStore::open((tmp / "h.db").string());
    REQUIRE(store);
    auto id = (*store)->record_qualification(r);
    REQUIRE(id);
    auto rows = (*store)->list(storage::SessionKind::qualification);
    REQUIRE(rows);
    REQUIRE(rows->size() == 1);
    CHECK(rows->front().headline.find("FAIL") == 0);
    auto loaded = (*store)->load_qualification(*id);
    REQUIRE(loaded);
    CHECK(loaded->verdict == Verdict::fail);
    CHECK((*store)->remove(storage::SessionKind::qualification, *id));
}
