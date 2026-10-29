// SPDX-License-Identifier: LicenseRef-Proprietary
#include <catch2/catch_test_macros.hpp>

#include "platter/reports/erase_report.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::erase;
using namespace platter::reports;
using platter::test::TempDir;

namespace {

EraseResult sample(bool with_errors = false) {
    TempDir tmp;
    const auto img = tmp / "rep.img";
    test::make_sparse_file(img, 256 * 1024);
    test::write_at(img, 0, std::vector<std::uint8_t>(256 * 1024, 0xAA));
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto dev = open_image(img.string(), oo);
    REQUIRE(dev);
    auto faulty = std::make_unique<test::FaultyWriteDevice>(std::move(dev).value());
    if (with_errors) faulty->dropped_lbas = {7};
    EraseConfig c;
    c.method = Method::dod_3pass;
    c.block_bytes = 64 * 1024;
    Erase e(safety::TestAccess::make(std::move(faulty), safety::Kind::erase), c);
    REQUIRE(e.validate());
    REQUIRE(e.start());
    e.wait();
    return e.result();
}

}  // namespace

TEST_CASE("erase report JSON round-trips", "[erase][report]") {
    const auto r = sample();
    const auto text = erase_to_json(r);
    CHECK(erase_digest_ok(text));
    auto back = erase_from_json(text);
    REQUIRE(back);
    CHECK(back->config.method == Method::dod_3pass);
    CHECK(back->verdict == r.verdict);
    CHECK(back->state == r.state);
    REQUIRE(back->passes.size() == 3);
    CHECK(back->passes[2].verified);
    CHECK(back->passes[0].bytes_written == r.passes[0].bytes_written);
    CHECK(back->affected_bytes == r.affected_bytes);
    CHECK(back->device.model == "Raw disk image");
    CHECK(back->whole_device);
}

TEST_CASE("erase report records failures", "[erase][report]") {
    const auto r = sample(true);
    auto back = erase_from_json(erase_to_json(r));
    REQUIRE(back);
    CHECK(back->verdict == Verdict::erased_with_errors);
    REQUIRE(back->passes.back().first_bad_offset);
    CHECK(*back->passes.back().first_bad_offset == 7 * 512);
    REQUIRE_FALSE(back->bad_verify.empty());
    CHECK(back->bad_verify[0].first == 7);
}

TEST_CASE("tampering with an erase report breaks its digest", "[erase][report]") {
    auto text = erase_to_json(sample());
    REQUIRE(erase_digest_ok(text));
    const auto pos = text.find("ERASED AND VERIFIED");
    REQUIRE(pos != std::string::npos);
    text.replace(pos, 3, "NOT");
    CHECK_FALSE(erase_digest_ok(text));
    CHECK_FALSE(erase_digest_ok("not json at all"));
}

TEST_CASE("erase report rejects other documents", "[erase][report]") {
    CHECK_FALSE(erase_from_json("{}"));
    CHECK_FALSE(erase_from_json("[1,2"));
    CHECK_FALSE(erase_from_json(R"({"kind":"benchmark"})"));
}

TEST_CASE("erase report omits the serial unless asked", "[erase][report]") {
    auto r = sample();
    r.device.serial = "SECRETSERIAL123";
    r.device.stable_id = "ata-ACME_SECRETSERIAL123";
    const auto hidden = erase_to_json(r);
    CHECK(hidden.find("SECRETSERIAL123") == std::string::npos);
    CHECK(erase_to_text(r).find("SECRETSERIAL123") == std::string::npos);
    Privacy p;
    p.include_serials = true;
    CHECK(erase_to_json(r, p).find("SECRETSERIAL123") != std::string::npos);
}

TEST_CASE("text report states verdict, passes and limits", "[erase][report]") {
    const auto text = erase_to_text(sample());
    CHECK(text.find("ERASED AND VERIFIED") != std::string::npos);
    CHECK(text.find("fill 0x00") != std::string::npos);
    CHECK(text.find("random data") != std::string::npos);
    CHECK(text.find("not a cryptographic") != std::string::npos);
    const auto bad = erase_to_text(sample(true));
    CHECK(bad.find("COMPLETED WITH ERRORS") != std::string::npos);
    CHECK(bad.find("did not verify") != std::string::npos);
}
