// SPDX-License-Identifier: TBD
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

#include "platter/core/sha256.hpp"
#include "platter/imaging/imaging.hpp"
#include "test_support.hpp"
#include "write_support.hpp"

using namespace platter;
using namespace platter::imaging;
using platter::safety::Kind;
using platter::safety::TestAccess;
using platter::test::FaultyWriteDevice;
using platter::test::TempDir;

namespace fs = std::filesystem;

namespace {

std::vector<std::uint8_t> pseudo_random(std::size_t n, std::uint64_t seed) {
    std::vector<std::uint8_t> v(n);
    std::uint64_t x = seed * 0x9E3779B97F4A7C15ull + 1;
    for (auto& b : v) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        b = static_cast<std::uint8_t>(x >> 24);
    }
    return v;
}

void make_data_file(const fs::path& p, std::size_t bytes, std::uint64_t seed) {
    test::make_sparse_file(p, bytes);
    test::write_at(p, 0, pseudo_random(bytes, seed));
}

std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::string sha_of(const std::vector<std::uint8_t>& v) {
    Sha256 h;
    h.update(std::span<const std::uint8_t>(v.data(), v.size()));
    return Sha256::hex(h.finish());
}

BlockDevicePtr open_ro(const fs::path& p, std::uint32_t ss = 512) {
    auto d = open_image(p.string(), {}, ss);
    REQUIRE(d);
    return std::move(d).value();
}

BlockDevicePtr open_rw(const fs::path& p, std::uint32_t ss = 512) {
    OpenOptions oo;
    oo.mode = OpenMode::read_write;
    auto d = open_image(p.string(), oo, ss);
    REQUIRE(d);
    return std::move(d).value();
}

template <class Op>
ImagingResult run(Op& op) {
    auto v = op.validate();
    INFO((v ? std::string() : v.error().describe()));
    REQUIRE(v);
    REQUIRE(op.start());
    op.wait();
    return op.result();
}

constexpr std::size_t kMiB = 1 << 20;

}  // namespace

// ----------------------------------------------------------- image files

TEST_CASE("raw image writer: partial file, rename on finish, no silent overwrite", "[imaging][file]") {
    TempDir tmp;
    const auto out = (tmp / "a.img").string();
    WriterOptions o;
    o.total_bytes = 3000;
    auto w = ImageWriter::create(out, o);
    REQUIRE(w);
    std::vector<std::uint8_t> a(1000, 1), b(2000, 2);
    REQUIRE((*w)->write(a.data(), a.size()));
    CHECK(fs::exists(out + ".partial"));
    CHECK_FALSE(fs::exists(out));
    CHECK_FALSE((*w)->finish());  // incomplete
    REQUIRE((*w)->write(b.data(), b.size()));
    CHECK_FALSE((*w)->write(b.data(), 1));  // more than announced
    REQUIRE((*w)->finish());
    CHECK(fs::exists(out));
    CHECK_FALSE(fs::exists(out + ".partial"));
    CHECK(fs::file_size(out) == 3000);

    auto again = ImageWriter::create(out, o);
    REQUIRE_FALSE(again);
    CHECK(again.error().code == Errc::invalid_argument);
    o.overwrite = true;
    CHECK(ImageWriter::create(out, o));
}

TEST_CASE("raw reader returns the bytes and the size", "[imaging][file]") {
    TempDir tmp;
    const auto f = tmp / "r.img";
    make_data_file(f, 10000, 3);
    auto r = ImageReader::open(f.string());
    REQUIRE(r);
    CHECK((*r)->compression() == Compression::none);
    CHECK(*(*r)->size() == 10000);
    std::vector<std::uint8_t> got(10000);
    auto n = (*r)->read(got.data(), got.size());
    REQUIRE(n);
    CHECK(*n == 10000);
    CHECK(got == pseudo_random(10000, 3));
    CHECK(*(*r)->read(got.data(), 1) == 0);
    CHECK_FALSE(ImageReader::open((tmp / "missing.img").string()));
}

TEST_CASE("sparse raw images keep their size and read back as zeros", "[imaging][file]") {
    TempDir tmp;
    const auto out = (tmp / "s.img").string();
    WriterOptions o;
    o.sparse = true;
    o.total_bytes = 4 * 4096;
    auto w = ImageWriter::create(out, o);
    REQUIRE(w);
    std::vector<std::uint8_t> zero(4096, 0), data(4096, 7);
    REQUIRE((*w)->write(data.data(), data.size()));
    REQUIRE((*w)->write(zero.data(), zero.size()));
    REQUIRE((*w)->write(data.data(), data.size()));
    REQUIRE((*w)->write(zero.data(), zero.size()));  // trailing hole
    REQUIRE((*w)->finish());
    const auto bytes = slurp(out);
    REQUIRE(bytes.size() == 4 * 4096);
    CHECK(bytes[0] == 7);
    CHECK(bytes[4096] == 0);
    CHECK(bytes[2 * 4096] == 7);
    CHECK(bytes[4 * 4096 - 1] == 0);
}

TEST_CASE("zstd images round-trip, are smaller for compressible data and detect damage", "[imaging][file][zstd]") {
    if (!zstd_available()) { SUCCEED("built without zstd"); return; }
    TempDir tmp;
    const auto out = (tmp / "z.img.zst").string();
    WriterOptions o;
    o.compression = Compression::zstd;
    o.total_bytes = 2 * kMiB;
    auto w = ImageWriter::create(out, o);
    REQUIRE(w);
    std::vector<std::uint8_t> data(kMiB, 0x42);
    REQUIRE((*w)->write(data.data(), data.size()));
    REQUIRE((*w)->write(data.data(), data.size()));
    REQUIRE((*w)->finish());
    CHECK(fs::file_size(out) < 100 * 1024);

    auto r = ImageReader::open(out);
    REQUIRE(r);
    CHECK((*r)->compression() == Compression::zstd);
    REQUIRE((*r)->size());
    CHECK(*(*r)->size() == 2 * kMiB);
    std::vector<std::uint8_t> got(2 * kMiB);
    auto n = (*r)->read(got.data(), got.size());
    REQUIRE(n);
    CHECK(*n == got.size());
    CHECK(got[12345] == 0x42);
    CHECK(*(*r)->read(got.data(), 1) == 0);

    // Truncated file
    const auto cut = (tmp / "cut.zst").string();
    fs::copy_file(out, cut);
    fs::resize_file(cut, fs::file_size(cut) / 2);
    auto rc = ImageReader::open(cut);
    REQUIRE(rc);
    auto bad = (*rc)->read(got.data(), got.size());
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == Errc::corrupt_data);

    // Flipped bytes in the middle
    const auto flip = (tmp / "flip.zst").string();
    auto bytes = slurp(out);
    for (std::size_t i = bytes.size() / 2; i < bytes.size() / 2 + 8; ++i) bytes[i] ^= 0xFF;
    std::ofstream(flip, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    auto rf = ImageReader::open(flip);
    REQUIRE(rf);
    auto bad2 = (*rf)->read(got.data(), got.size());
    CHECK((!bad2 || *bad2 != got.size() || got != std::vector<std::uint8_t>(got.size(), 0x42)));
}

// ----------------------------------------------------------------- create

TEST_CASE("create image: raw copy is byte-exact with checksum, sidecar and read-back", "[imaging][create]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, 3 * kMiB + 512 * 5, 11);
    ImageConfig c;
    c.output = (tmp / "out.img").string();
    CreateImage op(open_ro(src), c);
    const auto r = run(op);
    CHECK(r.state == OperationState::completed);
    CHECK(r.verdict == Verdict::ok);
    CHECK(slurp(c.output) == slurp(src));
    CHECK(r.sha256 == sha_of(slurp(src)));
    CHECK(r.readback_ok == std::optional<bool>(true));
    CHECK(read_sha256_sidecar(c.output + ".sha256") == r.sha256);
    CHECK(r.bytes == fs::file_size(src));
    CHECK_FALSE(fs::exists(c.output + ".partial"));
}

TEST_CASE("create image: a sub-range and zstd", "[imaging][create][zstd]") {
    if (!zstd_available()) { SUCCEED("built without zstd"); return; }
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, 2 * kMiB, 12);
    ImageConfig c;
    c.output = (tmp / "part.img.zst").string();
    c.compression = Compression::zstd;
    c.start_lba = 100;
    c.end_lba = 1123;  // 1024 sectors
    CreateImage op(open_ro(src), c);
    const auto r = run(op);
    CHECK(r.verdict == Verdict::ok);
    CHECK(r.sectors == 1024);
    const auto all = slurp(src);
    const std::vector<std::uint8_t> expect(all.begin() + 100 * 512, all.begin() + 1124 * 512);
    CHECK(r.sha256 == sha_of(expect));

    auto rd = ImageReader::open(c.output);
    REQUIRE(rd);
    std::vector<std::uint8_t> got(expect.size());
    REQUIRE(*(*rd)->read(got.data(), got.size()) == got.size());
    CHECK(got == expect);
    CHECK(read_sha256_sidecar(c.output + ".sha256") == r.sha256);
}

TEST_CASE("create image: refuses writable sources, existing outputs and the source itself", "[imaging][create]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, kMiB, 13);
    {
        ImageConfig c;
        c.output = (tmp / "o1.img").string();
        CreateImage op(open_rw(src), c);
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::invalid_argument);
    }
    {
        ImageConfig c;
        c.output = src.string();
        c.overwrite = true;
        CreateImage op(open_ro(src), c);
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        const auto existing = tmp / "exists.img";
        make_data_file(existing, 100, 1);
        ImageConfig c;
        c.output = existing.string();
        CreateImage op(open_ro(src), c);
        REQUIRE_FALSE(op.validate());
    }
    {
        ImageConfig c;
        c.output = (tmp / "o2.img").string();
        c.block_bytes = 1000;  // not a sector multiple
        CreateImage op(open_ro(src), c);
        REQUIRE_FALSE(op.validate());
    }
}

TEST_CASE("create image: unreadable sectors stop a normal copy but not a recovery copy", "[imaging][create][recover]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, 2 * kMiB, 14);
    auto make = [&] {
        auto d = std::make_unique<FaultyWriteDevice>(open_ro(src));
        d->bad_read_lbas = {1000, 1001, 1002, 3000};
        return d;
    };
    {
        ImageConfig c;
        c.output = (tmp / "strict.img").string();
        CreateImage op(make(), c);
        REQUIRE(op.validate());
        REQUIRE(op.start());
        op.wait();
        CHECK(op.state() == OperationState::failed);
        CHECK(op.result().verdict == Verdict::incomplete);
        CHECK_FALSE(fs::exists(c.output));          // never presented as a finished image
        CHECK(fs::exists(c.output + ".partial"));
    }
    {
        ImageConfig c;
        c.output = (tmp / "recover.img").string();
        c.recover = true;
        c.retries = 1;
        CreateImage op(make(), c);
        const auto r = run(op);
        CHECK(r.state == OperationState::completed);
        CHECK(r.verdict == Verdict::ok_with_unreadable);
        CHECK(r.unreadable_sectors == 4);
        REQUIRE(r.unreadable.size() == 2);
        CHECK(r.unreadable[0] == LbaRange{1000, 3});
        CHECK(r.unreadable[1] == LbaRange{3000, 1});
        auto expect = slurp(src);
        for (std::uint64_t lba : {1000u, 1001u, 1002u, 3000u}) std::fill_n(expect.begin() + static_cast<std::ptrdiff_t>(lba * 512), 512, std::uint8_t{0});
        CHECK(slurp(c.output) == expect);
        CHECK(r.sha256 == sha_of(expect));
    }
    {
        ImageConfig c;
        c.output = (tmp / "limit.img").string();
        c.recover = true;
        c.max_unreadable_sectors = 2;
        CreateImage op(make(), c);
        REQUIRE(op.validate());
        REQUIRE(op.start());
        op.wait();
        CHECK(op.state() == OperationState::failed);
    }
}

TEST_CASE("create image can be cancelled and leaves only a partial file", "[imaging][create][cancel]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, 8 * kMiB, 15);
    ImageConfig c;
    c.output = (tmp / "c.img").string();
    c.block_bytes = 4096;
    CreateImage op(open_ro(src), c);
    REQUIRE(op.validate());
    REQUIRE(op.start());
    op.cancel();
    op.wait();
    CHECK((op.state() == OperationState::cancelled || op.state() == OperationState::completed));
    if (op.state() == OperationState::cancelled) {
        CHECK_FALSE(fs::exists(c.output));
        CHECK(op.result().verdict == Verdict::incomplete);
    }
}

// ---------------------------------------------------------------- restore

TEST_CASE("restore: writes the image at the confirmed place and nowhere else, verified", "[imaging][restore]") {
    for (bool z : {false, true}) {
        if (z && !zstd_available()) continue;
        INFO(std::string(z ? "zstd" : "raw"));
        TempDir tmp;
        const auto src = tmp / "src.img";
        make_data_file(src, 2 * kMiB, 21);
        ImageConfig c;
        c.output = (tmp / (z ? "i.img.zst" : "i.img")).string();
        c.compression = z ? Compression::zstd : Compression::none;
        CreateImage mk(open_ro(src), c);
        REQUIRE(run(mk).verdict == Verdict::ok);

        const auto dst = tmp / "dst.img";
        make_data_file(dst, 8 * kMiB, 22);
        const auto before = slurp(dst);
        const std::uint64_t first = 2048, sectors = 2 * kMiB / 512;
        RestoreConfig rc;
        rc.input = c.output;
        RestoreImage op(TestAccess::make(open_rw(dst), Kind::restore_image, {LbaRange{first, sectors}}), rc);
        const auto r = run(op);
        CHECK(r.state == OperationState::completed);
        CHECK(r.verdict == Verdict::ok);
        CHECK(r.readback_ok == std::optional<bool>(true));
        CHECK(r.sha256 == sha_of(slurp(src)));

        const auto after = slurp(dst);
        const auto img = slurp(src);
        CHECK(std::equal(img.begin(), img.end(), after.begin() + static_cast<std::ptrdiff_t>(first * 512)));
        CHECK(std::equal(before.begin(), before.begin() + static_cast<std::ptrdiff_t>(first * 512), after.begin()));
        CHECK(std::equal(before.begin() + static_cast<std::ptrdiff_t>((first + sectors) * 512), before.end(),
                         after.begin() + static_cast<std::ptrdiff_t>((first + sectors) * 512)));
    }
}

TEST_CASE("restore: a wrong expected checksum stops before anything is written", "[imaging][restore]") {
    TempDir tmp;
    const auto img = tmp / "i.img";
    make_data_file(img, kMiB, 23);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, 2 * kMiB, 24);
    const auto before = slurp(dst);
    RestoreConfig rc;
    rc.input = img.string();
    rc.expect_sha256 = std::string(64, 'a');
    {
        RestoreImage op(TestAccess::make(open_rw(dst), Kind::restore_image, {LbaRange{0, kMiB / 512}}), rc);
        REQUIRE(op.validate());
        REQUIRE(op.start());
        op.wait();
        CHECK(op.state() == OperationState::failed);
        CHECK(slurp(dst) == before);
        CHECK(op.result().sha_matches == std::optional<bool>(false));
    }  // releases the exclusive lease

    // The right checksum goes through.
    const auto good = sha_of(slurp(img));
    RestoreConfig ok;
    ok.input = img.string();
    ok.expect_sha256 = good;
    RestoreImage op2(TestAccess::make(open_rw(dst), Kind::restore_image, {LbaRange{0, kMiB / 512}}), ok);
    const auto r = run(op2);
    CHECK(r.verdict == Verdict::ok);
    CHECK(r.sha_matches == std::optional<bool>(true));
}

TEST_CASE("restore: refuses wrong kinds, too-small ranges and unaligned images", "[imaging][restore]") {
    TempDir tmp;
    const auto img = tmp / "i.img";
    make_data_file(img, kMiB, 25);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, 2 * kMiB, 26);
    RestoreConfig rc;
    rc.input = img.string();
    {
        RestoreImage op(TestAccess::make(open_rw(dst), Kind::erase, {LbaRange{0, kMiB / 512}}), rc);
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        RestoreImage op(TestAccess::make(open_rw(dst), Kind::restore_image, {LbaRange{0, 100}}), rc);
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        RestoreImage op(TestAccess::make(open_rw(dst), Kind::restore_image, {LbaRange{0, 10}, LbaRange{100, 5000}}), rc);
        REQUIRE_FALSE(op.validate());
    }
    {
        const auto odd = tmp / "odd.img";
        make_data_file(odd, 1000, 27);
        RestoreConfig r2;
        r2.input = odd.string();
        RestoreImage op(TestAccess::make(open_rw(dst), Kind::restore_image, {LbaRange{0, 10}}), r2);
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::invalid_argument);
    }
    CHECK(slurp(dst).size() == 2 * kMiB);
}

TEST_CASE("restore: a write the device silently drops is caught by the read-back", "[imaging][restore][fault]") {
    TempDir tmp;
    const auto img = tmp / "i.img";
    make_data_file(img, kMiB, 28);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, 2 * kMiB, 29);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(dst));
    dev->dropped_lbas = {500};
    RestoreConfig rc;
    rc.input = img.string();
    rc.block_bytes = 64 * 1024;
    RestoreImage op(TestAccess::make(std::move(dev), Kind::restore_image, {LbaRange{0, kMiB / 512}}), rc);
    const auto r = run(op);
    CHECK(r.state == OperationState::completed);
    CHECK(r.verdict == Verdict::check_failed);
    CHECK(r.readback_ok == std::optional<bool>(false));
    REQUIRE_FALSE(r.mismatches.empty());
    CHECK(r.mismatches[0].contains(500));
}

TEST_CASE("restore: a failing write fails the operation", "[imaging][restore][fault]") {
    TempDir tmp;
    const auto img = tmp / "i.img";
    make_data_file(img, kMiB, 30);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, kMiB, 31);
    auto dev = std::make_unique<FaultyWriteDevice>(open_rw(dst));
    dev->bad_write_lbas = {700};
    RestoreConfig rc;
    rc.input = img.string();
    RestoreImage op(TestAccess::make(std::move(dev), Kind::restore_image, {LbaRange{0, kMiB / 512}}), rc);
    REQUIRE(op.validate());
    REQUIRE(op.start());
    op.wait();
    CHECK(op.state() == OperationState::failed);
    CHECK(op.result().verdict == Verdict::incomplete);
}

// ------------------------------------------------------------------ clone

TEST_CASE("clone: copies the region sector for sector and verifies", "[imaging][clone]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, 2 * kMiB, 41);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, 4 * kMiB, 42);
    const auto before = slurp(dst);
    CloneConfig cc;
    cc.source_first_lba = 0;
    CloneDevice op(open_ro(src), TestAccess::make(open_rw(dst), Kind::clone_target, {LbaRange{1024, 2 * kMiB / 512}}), cc);
    const auto r = run(op);
    CHECK(r.verdict == Verdict::ok);
    CHECK(r.readback_ok == std::optional<bool>(true));
    const auto after = slurp(dst);
    const auto s = slurp(src);
    CHECK(std::equal(s.begin(), s.end(), after.begin() + 1024 * 512));
    CHECK(std::equal(before.begin(), before.begin() + 1024 * 512, after.begin()));
    CHECK(std::equal(before.begin() + static_cast<std::ptrdiff_t>(1024 * 512 + s.size()), before.end(), after.begin() + static_cast<std::ptrdiff_t>(1024 * 512 + s.size())));
    CHECK(r.sha256 == sha_of(s));
}

TEST_CASE("clone: refuses mismatched sector sizes, itself and an oversized region", "[imaging][clone]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, kMiB, 43);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, 4 * kMiB, 44);
    {
        CloneDevice op(open_ro(src, 4096), TestAccess::make(open_rw(dst, 512), Kind::clone_target, {LbaRange{0, 256}}), {});
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::invalid_argument);
    }
    {
        CloneDevice op(open_ro(dst), TestAccess::make(open_rw(dst), Kind::clone_target, {LbaRange{0, 256}}), {});
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
    {
        CloneDevice op(open_ro(src), TestAccess::make(open_rw(dst), Kind::clone_target, {LbaRange{0, 2 * kMiB / 512}}), {});
        REQUIRE_FALSE(op.validate());  // source holds only 1 MiB
    }
    {
        CloneDevice op(open_ro(src), TestAccess::make(open_rw(dst), Kind::erase, {LbaRange{0, 256}}), {});
        auto v = op.validate();
        REQUIRE_FALSE(v);
        CHECK(v.error().code == Errc::refused);
    }
}

TEST_CASE("clone: recovery mode zero-fills unreadable sectors on the target", "[imaging][clone][recover]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, kMiB, 45);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, kMiB, 46);
    auto sd = std::make_unique<FaultyWriteDevice>(open_ro(src));
    sd->bad_read_lbas = {10, 11};
    CloneConfig cc;
    cc.recover = true;
    cc.retries = 0;
    CloneDevice op(std::move(sd), TestAccess::make(open_rw(dst), Kind::clone_target, {LbaRange{0, kMiB / 512}}), cc);
    const auto r = run(op);
    CHECK(r.verdict == Verdict::ok_with_unreadable);
    REQUIRE(r.unreadable.size() == 1);
    CHECK(r.unreadable[0] == LbaRange{10, 2});
    const auto after = slurp(dst);
    CHECK(std::all_of(after.begin() + 10 * 512, after.begin() + 12 * 512, [](auto b) { return b == 0; }));
    const auto s = slurp(src);
    CHECK(std::equal(s.begin(), s.begin() + 10 * 512, after.begin()));
}

TEST_CASE("clone: a silently dropped write is reported as a verification failure", "[imaging][clone][fault]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, kMiB, 47);
    const auto dst = tmp / "dst.img";
    make_data_file(dst, kMiB, 48);
    auto td = std::make_unique<FaultyWriteDevice>(open_rw(dst));
    td->dropped_lbas = {1500};
    CloneConfig cc;
    cc.block_bytes = 64 * 1024;
    CloneDevice op(open_ro(src), TestAccess::make(std::move(td), Kind::clone_target, {LbaRange{0, kMiB / 512}}), cc);
    const auto r = run(op);
    CHECK(r.verdict == Verdict::check_failed);
    REQUIRE_FALSE(r.mismatches.empty());
    CHECK(r.mismatches[0].contains(1500));
}

// ----------------------------------------------------------------- verify

TEST_CASE("verify image: checksum comparison and device comparison", "[imaging][verify]") {
    TempDir tmp;
    const auto src = tmp / "src.img";
    make_data_file(src, 2 * kMiB, 51);
    ImageConfig c;
    c.output = (tmp / "i.img").string();
    CreateImage mk(open_ro(src), c);
    const auto made = run(mk);

    {
        VerifyConfig vc;
        vc.input = c.output;
        vc.expect_sha256 = made.sha256;
        VerifyImage op(std::move(vc));
        const auto r = run(op);
        CHECK(r.verdict == Verdict::ok);
        CHECK(r.sha_matches == std::optional<bool>(true));
    }
    {
        VerifyConfig vc;
        vc.input = c.output;
        vc.expect_sha256 = std::string(64, '0');
        VerifyImage op(std::move(vc));
        const auto r = run(op);
        CHECK(r.verdict == Verdict::check_failed);
        CHECK(r.sha_matches == std::optional<bool>(false));
    }
    {
        VerifyConfig vc;
        vc.input = c.output;
        vc.compare_to = open_ro(src);
        VerifyImage op(std::move(vc));
        const auto r = run(op);
        CHECK(r.verdict == Verdict::ok);
        CHECK(r.readback_ok == std::optional<bool>(true));
    }
    {
        // Change two sectors of the device.
        test::write_at(src, 100 * 512 + 3, {0xFF});
        test::write_at(src, 101 * 512 + 3, {0xFF});
        test::write_at(src, 2000 * 512, {0xAB});
        VerifyConfig vc;
        vc.input = c.output;
        vc.compare_to = open_ro(src);
        VerifyImage op(std::move(vc));
        const auto r = run(op);
        CHECK(r.verdict == Verdict::check_failed);
        REQUIRE(r.mismatches.size() == 2);
        CHECK(r.mismatches[0] == LbaRange{100, 2});
        CHECK(r.mismatches[1] == LbaRange{2000, 1});
    }
    {
        VerifyConfig vc;
        vc.input = (tmp / "none.img").string();
        VerifyImage op(std::move(vc));
        CHECK_FALSE(op.validate());
    }
}

TEST_CASE("verify image: an image bigger than the device is a mismatch", "[imaging][verify]") {
    TempDir tmp;
    const auto big = tmp / "big.img";
    make_data_file(big, 2 * kMiB, 52);
    const auto small = tmp / "small.img";
    make_data_file(small, kMiB, 53);
    VerifyConfig vc;
    vc.input = big.string();
    vc.compare_to = open_ro(small);
    VerifyImage op(std::move(vc));
    const auto r = run(op);
    CHECK(r.verdict == Verdict::check_failed);
    CHECK_FALSE(r.mismatches.empty());
}

TEST_CASE("imaging text report states result, checksum and unreadable ranges", "[imaging][report]") {
    ImagingResult r;
    r.kind = JobKind::create;
    r.verdict = Verdict::ok_with_unreadable;
    r.state = OperationState::completed;
    r.sha256 = std::string(64, 'c');
    r.unreadable = {LbaRange{5, 3}};
    r.unreadable_sectors = 3;
    r.sectors = 100;
    r.bytes = 51200;
    const auto t = result_to_text(r);
    CHECK(t.find("COMPLETED WITH UNREADABLE SECTORS") != std::string::npos);
    CHECK(t.find(std::string(64, 'c')) != std::string::npos);
    CHECK(t.find("5 - 7") != std::string::npos);
}

// ----------------------------------------------------------------- report

#include "platter/reports/imaging_report.hpp"

TEST_CASE("imaging report JSON round-trips and detects tampering", "[imaging][report]") {
    ImagingResult r;
    r.kind = JobKind::clone;
    r.verdict = Verdict::ok_with_unreadable;
    r.state = OperationState::completed;
    DeviceInfo d;
    d.path = "/dev/x";
    d.model = "Test Disk";
    d.serial = "SECRET123";
    d.capacity_bytes = 1 << 20;
    r.source = d;
    r.target = d;
    r.image_path = "a.img.zst";
    r.compression = Compression::zstd;
    r.sector_size = 4096;
    r.sectors = 77;
    r.bytes = 77 * 4096;
    r.sha256 = std::string(64, 'a');
    r.expected_sha256 = std::string(64, 'a');
    r.sha_matches = true;
    r.readback_ok = true;
    r.unreadable = {LbaRange{3, 2}};
    r.unreadable_sectors = 2;
    r.mismatches = {LbaRange{9, 1}};
    r.warnings = {"w"};
    const auto json = reports::imaging_to_json(r);
    CHECK(reports::imaging_digest_ok(json));
    CHECK(json.find("SECRET123") == std::string::npos);  // serials omitted by default
    auto back = reports::imaging_from_json(json);
    REQUIRE(back);
    CHECK(back->kind == JobKind::clone);
    CHECK(back->verdict == Verdict::ok_with_unreadable);
    CHECK(back->compression == Compression::zstd);
    CHECK(back->sectors == 77);
    CHECK(back->sha_matches == std::optional<bool>(true));
    CHECK(back->readback_ok == std::optional<bool>(true));
    REQUIRE(back->unreadable.size() == 1);
    CHECK(back->unreadable[0] == LbaRange{3, 2});
    CHECK(back->mismatches[0] == LbaRange{9, 1});
    CHECK(back->source->model == "Test Disk");

    auto tampered = json;
    tampered.replace(tampered.find("\"sectors\":77"), 12, "\"sectors\":78");
    CHECK_FALSE(reports::imaging_digest_ok(tampered));
    CHECK_FALSE(reports::imaging_from_json("{\"kind\":\"erase-report\"}"));
    CHECK_FALSE(reports::imaging_from_json("nonsense"));
}
