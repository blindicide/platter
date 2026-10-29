// SPDX-License-Identifier: TBD
//
// Privilege separation. The helper is started as the current user against
// temp image files (--allow-regular-files); nothing here opens a real device.
#include <catch2/catch_test_macros.hpp>

#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <thread>

#include "platter/core/block_device.hpp"
#include "platter/core/broker.hpp"
#include "platter/helper/helper.hpp"
#include "test_support.hpp"
#include "wire.hpp"

using namespace platter;
using namespace platter::helper;
using platter::test::TempDir;

namespace {

std::shared_ptr<HelperBroker> start(std::vector<std::string> extra = {"--allow-regular-files"}) {
    std::vector<std::string> argv{PLATTER_HELPER_PATH, "--stdio"};
    argv.insert(argv.end(), extra.begin(), extra.end());
    auto b = HelperBroker::launch(argv);
    REQUIRE(b);
    return *b;
}

OpenOptions rw(std::vector<ByteSpan> ranges) {
    OpenOptions o;
    o.mode = OpenMode::read_write;
    o.write_ranges = std::move(ranges);
    return o;
}

std::vector<std::uint8_t> pattern(std::size_t n, std::uint8_t seed) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(seed + i * 7);
    return v;
}

}  // namespace

TEST_CASE("the helper starts, identifies itself and shuts down", "[helper]") {
    auto b = start();
    CHECK(b->alive());
    CHECK(b->helper_pid() > 0);
    CHECK(b->describe().find("platter-helper pid") != std::string::npos);
}

TEST_CASE("a helper that cannot be started is reported, not fatal", "[helper]") {
    auto b = HelperBroker::launch({"/nonexistent/platter-helper", "--stdio"});
    REQUIRE_FALSE(b);
    CHECK(b.error().code == Errc::not_found);
}

TEST_CASE("reads through the helper match the file, including multi-chunk reads", "[helper]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    const std::uint64_t size = 40ull << 20;  // larger than one 16 MiB message
    test::make_sparse_file(img, size);
    const auto a = pattern(4096, 3);
    const auto z = pattern(4096, 99);
    test::write_at(img, 0, a);
    test::write_at(img, size - 4096, z);

    auto b = start();
    auto dev = b->open(img.string(), {});
    REQUIRE(dev);
    CHECK((*dev)->capacity() == size);
    CHECK((*dev)->mode() == OpenMode::read_only);

    std::vector<std::uint8_t> head(4096), tail(4096);
    REQUIRE((*dev)->read(0, head.data(), head.size()));
    REQUIRE((*dev)->read(size - 4096, tail.data(), tail.size()));
    CHECK(head == a);
    CHECK(tail == z);

    std::vector<std::uint8_t> big(static_cast<std::size_t>(size));
    auto r = (*dev)->read(0, big.data(), big.size());
    REQUIRE(r);
    CHECK(r->bytes == big.size());
    CHECK(std::equal(a.begin(), a.end(), big.begin()));
    CHECK(std::equal(z.begin(), z.end(), big.end() - 4096));
    // Past the end is the device's own range error, passed through.
    CHECK_FALSE((*dev)->read(size, head.data(), head.size()));
}

TEST_CASE("a read-only handle cannot write", "[helper]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 1 << 20);
    auto b = start();
    auto dev = b->open(img.string(), {});
    REQUIRE(dev);
    const auto data = pattern(512, 1);
    auto w = (*dev)->write(0, data.data(), data.size());
    REQUIRE_FALSE(w);
    CHECK(w.error().code == Errc::read_only);
    CHECK(test::read_at(img, 0, 512) == std::vector<std::uint8_t>(512, 0));
}

TEST_CASE("a read-write open needs confirmed ranges", "[helper][safety]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 1 << 20);
    auto b = start();
    auto none = b->open(img.string(), rw({}));
    REQUIRE_FALSE(none);
    CHECK(none.error().code == Errc::refused);
    auto empty = b->open(img.string(), rw({{4096, 0}}));
    REQUIRE_FALSE(empty);
    CHECK(empty.error().code == Errc::refused);
    auto beyond = b->open(img.string(), rw({{(1 << 20) - 512, 4096}}));
    REQUIRE_FALSE(beyond);
    CHECK(beyond.error().code == Errc::refused);
    auto overflow = b->open(img.string(), rw({{~0ull - 10, 100}}));
    REQUIRE_FALSE(overflow);
    CHECK(overflow.error().code == Errc::refused);
}

TEST_CASE("the helper enforces the confirmed write ranges itself", "[helper][safety]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 1 << 20);
    auto b = start();
    // Only [64 KiB, 128 KiB) and [512 KiB, 516 KiB) may be written.
    auto dev = b->open(img.string(), rw({{64 << 10, 64 << 10}, {512 << 10, 4096}}));
    REQUIRE(dev);
    CHECK((*dev)->mode() == OpenMode::read_write);
    const auto data = pattern(4096, 5);

    REQUIRE((*dev)->write(64 << 10, data.data(), data.size()));
    REQUIRE((*dev)->write(512 << 10, data.data(), data.size()));
    REQUIRE((*dev)->flush());
    CHECK(test::read_at(img, 64 << 10, 4096) == data);

    // Outside, before, after, and straddling the edge: all refused, nothing written.
    for (std::uint64_t off : {std::uint64_t{0}, std::uint64_t{(64 << 10) - 4096}, std::uint64_t{128 << 10},
                              std::uint64_t{(128 << 10) - 2048}, std::uint64_t{(512 << 10) + 4096}}) {
        INFO("offset " << off);
        auto w = (*dev)->write(off, data.data(), data.size());
        REQUIRE_FALSE(w);
        CHECK(w.error().code == Errc::refused);
    }
    CHECK(test::read_at(img, 0, 4096) == std::vector<std::uint8_t>(4096, 0));
    CHECK(test::read_at(img, (64 << 10) - 4096, 4096) == std::vector<std::uint8_t>(4096, 0));
    CHECK(test::read_at(img, 128 << 10, 4096) == std::vector<std::uint8_t>(4096, 0));
    // The straddling write must not have landed partly inside the range either.
    CHECK(test::read_at(img, (128 << 10) - 2048, 2048) == std::vector<std::uint8_t>(2048, 0));

    auto d = (*dev)->discard(0, 4096);
    REQUIRE_FALSE(d);
    CHECK(d.error().code == Errc::refused);
    // Reads are unrestricted.
    std::vector<std::uint8_t> back(4096);
    CHECK((*dev)->read(0, back.data(), back.size()));
}

TEST_CASE("the helper refuses what is not a whole-disk block device", "[helper][safety]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 1 << 20);

    SECTION("regular files, unless the test switch is on") {
        auto b = start({});
        auto dev = b->open(img.string(), {});
        REQUIRE_FALSE(dev);
        CHECK(dev.error().code == Errc::refused);
    }
    SECTION("character devices and other nodes") {
        auto b = start();
        auto dev = b->open("/dev/null", {});
        REQUIRE_FALSE(dev);
        CHECK(dev.error().code == Errc::refused);
    }
    SECTION("paths that do not exist") {
        auto b = start();
        auto dev = b->open((tmp / "missing.img").string(), {});
        REQUIRE_FALSE(dev);
        CHECK(dev.error().code == Errc::not_found);
    }
    SECTION("a read-only helper refuses every read-write open") {
        auto b = start({"--allow-regular-files", "--read-only"});
        auto dev = b->open(img.string(), rw({{0, 4096}}));
        REQUIRE_FALSE(dev);
        CHECK(dev.error().code == Errc::refused);
        CHECK(b->open(img.string(), {}));  // reading still works
    }
}

TEST_CASE("handles are independent and closing one leaves the others working", "[helper]") {
    TempDir tmp;
    const auto a = tmp / "a.img", c = tmp / "c.img";
    test::make_sparse_file(a, 1 << 20);
    test::make_sparse_file(c, 2 << 20);
    auto b = start();
    auto d1 = b->open(a.string(), {});
    auto d2 = b->open(c.string(), {});
    REQUIRE(d1);
    REQUIRE(d2);
    CHECK((*d1)->capacity() == (1u << 20));
    CHECK((*d2)->capacity() == (2u << 20));
    d1->reset();
    std::vector<std::uint8_t> buf(512);
    CHECK((*d2)->read(0, buf.data(), buf.size()));
}

TEST_CASE("many threads can share one helper", "[helper]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 8 << 20);
    for (int i = 0; i < 8; ++i) test::write_at(img, static_cast<std::uint64_t>(i) * (1 << 20), pattern(4096, static_cast<std::uint8_t>(i)));
    auto b = start();
    std::atomic<int> bad{0};
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&, t] {
            auto dev = b->open(img.string(), {});
            if (!dev) { ++bad; return; }
            std::vector<std::uint8_t> buf(4096);
            for (int round = 0; round < 50; ++round) {
                const int i = (t + round) % 8;
                if (!(*dev)->read(static_cast<std::uint64_t>(i) * (1 << 20), buf.data(), buf.size()) ||
                    buf != pattern(4096, static_cast<std::uint8_t>(i)))
                    ++bad;
            }
        });
    for (auto& t : ts) t.join();
    CHECK(bad == 0);
}

TEST_CASE("open_device is routed through an installed broker", "[helper]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 1 << 20);
    auto b = start();
    set_device_broker(b);
    auto routed = open_device(img.string());     // direct open would reject a non-/dev path
    set_device_broker(nullptr);
    REQUIRE(routed);
    CHECK((*routed)->capacity() == (1u << 20));
    CHECK_FALSE(open_device(img.string()));      // without the broker it is refused
}

TEST_CASE("a helper that dies is reported as an error, never a crash", "[helper]") {
    TempDir tmp;
    const auto img = tmp / "d.img";
    test::make_sparse_file(img, 1 << 20);
    auto b = start();
    auto dev = b->open(img.string(), {});
    REQUIRE(dev);
    REQUIRE(::kill(static_cast<pid_t>(b->helper_pid()), SIGKILL) == 0);
    std::vector<std::uint8_t> buf(512);
    for (int i = 0; i < 100 && (*dev)->read(0, buf.data(), buf.size()); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto r = (*dev)->read(0, buf.data(), buf.size());
    REQUIRE_FALSE(r);
    CHECK(r.error().code == Errc::io_error);
    CHECK_FALSE(b->alive());
    CHECK_FALSE(b->open(img.string(), {}));
}

TEST_CASE("the server survives hostile requests", "[helper][safety]") {
    int sv[2];
    REQUIRE(::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    ServerPolicy policy;
    policy.allow_regular_files = true;
    int rc = -1;
    std::thread server([&] { rc = serve(sv[1], policy); ::close(sv[1]); });

    auto roundtrip = [&](wire::Writer& w) {
        REQUIRE(wire::write_frame(sv[0], w.data()));
        auto f = wire::read_frame(sv[0]);
        REQUIRE(f);
        return *f;
    };
    {   // unknown handle
        wire::Writer w;
        w.u8(static_cast<std::uint8_t>(wire::Op::read));
        w.u32(12345); w.u64(0); w.u32(512);
        auto f = roundtrip(w);
        CHECK(f[0] == 1);
    }
    {   // truncated open
        wire::Writer w;
        w.u8(static_cast<std::uint8_t>(wire::Op::open));
        w.u32(1000);  // string length with no bytes behind it
        auto f = roundtrip(w);
        CHECK(f[0] == 1);
    }
    {   // wrong protocol version
        wire::Writer w;
        w.u8(static_cast<std::uint8_t>(wire::Op::hello));
        w.u32(999);
        auto f = roundtrip(w);
        CHECK(f[0] == 1);
    }
    {   // unknown op ends the session
        wire::Writer w;
        w.u8(200);
        auto f = roundtrip(w);
        CHECK(f[0] == 1);
    }
    ::close(sv[0]);
    server.join();
    CHECK(rc == 1);
}
