// SPDX-License-Identifier: LicenseRef-Proprietary
//
// Linux enumeration tests. They build a fake /sys, /dev/disk/by-id and
// /proc/{mounts,swaps} in a temp directory; the real /sys is never read.
#ifdef __linux__

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>

#include "platter/platform/linux_sysfs.hpp"
#include "test_support.hpp"

namespace fs = std::filesystem;
using namespace platter;
using platter::test::TempDir;

namespace {

void put(const fs::path& p, const std::string& content) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary) << content;
}

struct FakeDisk {
    std::string name;
    std::string devpath;  // relative to <root>/devices
    std::vector<std::pair<std::string, std::string>> files;
    std::vector<std::string> partitions;
};

class FakeSystem {
public:
    FakeSystem() {
        roots.sys_block = (tmp / "sys/block").string();
        roots.dev = "/dev";  // mount sources are compared as text against this prefix
        roots.mounts = (tmp / "proc/mounts").string();
        roots.swaps = (tmp / "proc/swaps").string();
        roots.disk_by_id = (tmp / "dev/disk/by-id").string();
        fs::create_directories(roots.sys_block);
        fs::create_directories(roots.disk_by_id);
    }

    fs::path add(const FakeDisk& d) {
        const auto real = tmp / "devices" / d.devpath / d.name;
        fs::create_directories(real);
        for (const auto& [f, v] : d.files) put(real / f, v);
        for (const auto& p : d.partitions) put(real / p / "partition", "1\n");
        fs::create_directory_symlink(real, fs::path(roots.sys_block) / d.name);
        return real;
    }

    void by_id(const std::string& link, const std::string& target) {
        fs::create_symlink("../../" + target, fs::path(roots.disk_by_id) / link);
    }

    TempDir tmp;
    sysfs::Roots roots;
};

std::string vpd80(const std::string& serial) {
    std::string s = {'\0', '\x80', '\0', static_cast<char>(serial.size())};
    return s + serial;
}

const DeviceInfo* find(const std::vector<DeviceInfo>& v, const std::string& name) {
    auto it = std::find_if(v.begin(), v.end(), [&](auto& d) { return d.name == name; });
    return it == v.end() ? nullptr : &*it;
}

void build_typical_box(FakeSystem& fx) {
    auto sda = fx.add({"sda",
                       "pci0000:00/0000:00:17.0/ata1/host0/target0:0:0/0:0:0:0/block",
                       {{"size", "1953525168\n"},
                        {"queue/logical_block_size", "512\n"},
                        {"queue/physical_block_size", "4096\n"},
                        {"queue/rotational", "1\n"},
                        {"queue/discard_max_bytes", "0\n"},
                        {"removable", "0\n"},
                        {"ro", "0\n"},
                        {"device/vendor", "ATA     \n"},
                        {"device/model", "WDC WD10EZEX-08W\n"},
                        {"device/rev", "1A01\n"},
                        {"device/vpd_pg80", vpd80("     WD-WCC6Y1234567")}},
                       {"sda1", "sda2"}});
    (void)sda;
    fx.add({"sdb",
            "pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/host6/target6:0:0/6:0:0:0/block",
            {{"size", "30031872\n"},
             {"queue/logical_block_size", "512\n"},
             {"queue/physical_block_size", "512\n"},
             {"queue/rotational", "1\n"},
             {"removable", "1\n"},
             {"ro", "0\n"},
             {"device/vendor", "Generic \n"},
             {"device/model", "Flash Disk\n"}},
            {"sdb1"}});
    auto nvme = fx.add({"nvme0n1",
                        "pci0000:00/0000:00:1d.0/0000:3d:00.0/nvme/nvme0",
                        {{"size", "976773168\n"},
                         {"queue/logical_block_size", "512\n"},
                         {"queue/physical_block_size", "512\n"},
                         {"queue/rotational", "0\n"},
                         {"queue/discard_max_bytes", "2199023255040\n"},
                         {"removable", "0\n"},
                         {"ro", "0\n"},
                         {"wwid", "eui.0025388b91c2a1b4\n"},
                         {"device/model", "Samsung SSD 970 EVO 500GB\n"},
                         {"device/serial", "S466NX0K123456\n"},
                         {"device/firmware_rev", "2B2QEXE7\n"}},
                        {"nvme0n1p1", "nvme0n1p2"}});
    fs::create_directories(nvme / "nvme0n1p2/holders/dm-0");
    fx.add({"dm-0", "virtual/block", {{"size", "975000000\n"}, {"dm/name", "cryptohome\n"}, {"ro", "0\n"}}, {}});
    fx.add({"loop0", "virtual/block", {{"size", "0\n"}}, {}});
    fx.add({"ram0", "virtual/block", {{"size", "131072\n"}}, {}});

    fx.by_id("wwn-0x50014ee2b5a1c3d4", "sda");
    fx.by_id("ata-WDC_WD10EZEX-08W_WD-WCC6Y1234567", "sda");
    fx.by_id("wwn-0x50014ee2b5a1c3d4-part1", "sda1");
    fx.by_id("usb-Generic_Flash_Disk-0:0", "sdb");

    put(fx.roots.mounts,
        "/dev/sda2 / ext4 rw,relatime 0 0\n"
        "proc /proc proc rw 0 0\n"
        "/dev/nvme0n1p1 /boot/efi vfat rw 0 0\n"
        "/dev/mapper/cryptohome /home ext4 rw 0 0\n"
        "/dev/sdb1 /media/My\\040Stick vfat rw 0 0\n");
    put(fx.roots.swaps,
        "Filename\t\t\t\tType\t\tSize\t\tUsed\t\tPriority\n"
        "/dev/sda1                               partition\t8388604\t\t0\t\t-2\n");
}

}  // namespace

TEST_CASE("mount and swap parsing", "[sysfs]") {
    auto m = sysfs::parse_mounts("/dev/sdb1 /media/a\\040b vfat rw 0 0\nbroken\n");
    REQUIRE(m.size() == 1);
    CHECK(m[0].mountpoint == "/media/a b");
    CHECK(m[0].fstype == "vfat");
    auto s = sysfs::parse_swaps("Filename Type Size Used Priority\n/dev/zram0 partition 1 0 100\n");
    REQUIRE(s.size() == 1);
    CHECK(s[0] == "/dev/zram0");
    CHECK(sysfs::should_skip("ram0", 1 << 20));
    CHECK(sysfs::should_skip("zram0", 1 << 20));
    CHECK(sysfs::should_skip("loop3", 0));
    CHECK_FALSE(sysfs::should_skip("sda", 1 << 20));
}

TEST_CASE("enumeration of a typical machine", "[sysfs]") {
    FakeSystem fx;
    build_typical_box(fx);
    auto devs = sysfs::enumerate(fx.roots);
    REQUIRE(devs);
    REQUIRE(devs->size() == 4);  // dm-0, nvme0n1, sda, sdb (loop0 empty, ram0 skipped)

    SECTION("SATA HDD hosting / and swap") {
        auto* d = find(*devs, "sda");
        REQUIRE(d);
        CHECK(d->path == "/dev/sda");
        CHECK(d->bus == BusType::ata);
        CHECK(d->media == MediaKind::rotational);
        CHECK(d->vendor == "ATA");
        CHECK(d->model == "WDC WD10EZEX-08W");
        CHECK(d->firmware == "1A01");
        CHECK(d->serial == "WD-WCC6Y1234567");
        CHECK(d->stable_id == "wwn-0x50014ee2b5a1c3d4");
        CHECK(d->capacity_bytes == 1953525168ull * 512);
        CHECK(d->logical_sector_size == 512);
        CHECK(d->physical_sector_size == 4096);
        CHECK(d->trim_supported == Tri::no);
        CHECK(d->is_system_disk);
        CHECK(d->in_use());
        CHECK(d->mountpoints == std::vector<std::string>{"/", "[SWAP]"});
        CHECK_FALSE(d->capabilities.has(Capability::trim));
        CHECK_FALSE(d->capabilities.has(Capability::smart));  // never guessed from bus type
    }
    SECTION("USB stick") {
        auto* d = find(*devs, "sdb");
        REQUIRE(d);
        CHECK(d->bus == BusType::usb);
        CHECK(d->removable == Tri::yes);
        CHECK(d->mountpoints == std::vector<std::string>{"/media/My Stick"});
        CHECK_FALSE(d->is_system_disk);
        CHECK(d->stable_id == "usb-Generic_Flash_Disk-0:0");
    }
    SECTION("NVMe SSD with an encrypted /home on top") {
        auto* d = find(*devs, "nvme0n1");
        REQUIRE(d);
        CHECK(d->bus == BusType::nvme);
        CHECK(d->media == MediaKind::solid_state);
        CHECK(d->trim_supported == Tri::yes);
        CHECK(d->capabilities.has(Capability::trim));
        CHECK(d->stable_id == "eui.0025388b91c2a1b4");
        CHECK(d->serial == "S466NX0K123456");
        CHECK(d->holders == std::vector<std::string>{"dm-0"});
        CHECK(d->mountpoints == std::vector<std::string>{"/boot/efi", "/home"});
        CHECK(d->is_system_disk);
    }
    SECTION("device-mapper node") {
        auto* d = find(*devs, "dm-0");
        REQUIRE(d);
        CHECK(d->bus == BusType::raid);
        CHECK(d->mountpoints == std::vector<std::string>{"/home"});
    }
}

TEST_CASE("describe validates names", "[sysfs][safety]") {
    FakeSystem fx;
    build_typical_box(fx);
    CHECK(sysfs::describe(fx.roots, "../etc").error().code == Errc::invalid_argument);
    CHECK(sysfs::describe(fx.roots, "").error().code == Errc::invalid_argument);
    CHECK(sysfs::describe(fx.roots, "sdz").error().code == Errc::not_found);
    auto d = sysfs::describe(fx.roots, "sdb");
    REQUIRE(d);
    CHECK(d->model == "Flash Disk");
}

TEST_CASE("missing sysfs root is an error, not a crash", "[sysfs]") {
    sysfs::Roots r;
    r.sys_block = "/nonexistent/platter/sys/block";
    CHECK(sysfs::enumerate(r).error().code == Errc::not_found);
}

#endif
