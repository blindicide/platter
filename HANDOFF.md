# HANDOFF — platter

Living notes for whoever (human or agent) picks this repo up next. Update this
file after every significant change. Newest status at the top of each section.

## 0. Read this first: safety rules for agents

- **Never run platter, smartctl, dd, or any other disk tool against real hardware
  unless the owner explicitly says so in that session.** The owner sometimes has
  an important drive connected. "Explicitly" means a message naming the device.
- All automated tests use disposable image files created in a temp directory.
  Nothing in the test suite may open `/dev/*` or `\\.\PhysicalDrive*`.
- Linux sysfs enumeration is tested against a fake /sys + /dev/disk/by-id +
  /proc/{mounts,swaps} tree that `tests/unit/test_sysfs.cpp` builds in a temp
  dir (`sysfs::Roots` makes every path injectable). Never test against the
  build machine's real `/sys`.
- Read-write opening (`OpenMode::read_write`) is reserved for destructive
  operations that go through the full safety pipeline (spec §9.5, §17). As of
  v0.1 nothing opens a physical device read-write.
- Owner's disposable test hardware (for later, with permission): a clean 1 TB
  HDD and a USB stick.

## 1. What this is

Cross-platform (Windows 10/11, Linux) storage diagnostics suite: Victoria /
HD Tune / CrystalDiskInfo / GParted / ddrescue in one tool, with a deliberate
Win2000/XP-era Qt Widgets UI. Full spec: owner's "HDD Operations Suite v1.0"
document (the spec's `hos-*` names are renamed to `platter-*` throughout).

Stack: C++20, CMake ≥ 3.21, Qt 6 Widgets (GUI only), nlohmann_json, Catch2 v3
(tests). zstd and SQLite are planned for later milestones and not yet linked.

## 2. Current status

Milestone: **v0.1 Foundation — in progress.**

Done:
- Repo skeleton and directory layout.
- `platter::Error` / `Result<T>` / `Status` (no exceptions across module
  boundaries; C++20 so no `std::expected`).
- Overflow-safe range arithmetic and `validate_io()` (`include/platter/core/range.hpp`).
- `DeviceInfo`, `CapabilitySet`, bus/media enums (`include/platter/core/device.hpp`).
- `IBlockDevice` interface + `open_image()` / `open_device()` declarations.
- POSIX file/block-device backend (`src/platform/posix/posix_file_device.cpp`):
  pread/pwrite loops with EINTR + short-read handling, per-call timing,
  O_DIRECT and O_EXCL support, BLKGETSIZE64/BLKSSZGET/BLKPBSZGET/BLKROGET.

- CMake build (`platter-core`, `platter-partitions`, `platter-tests`), deps via
  find_package with FetchContent fallback (`cmake/PlatterDependencies.cmake`).
- Linux sysfs enumeration (`src/platform/linux/sysfs.cpp`): model/vendor/fw,
  serial (incl. SCSI VPD 0x80), stable id (wwid or best /dev/disk/by-id link,
  wwn- preferred), bus from sysfs topology, rotational/removable/ro/discard,
  mounts + swap per partition, holder chains (dm-crypt/LVM) followed
  recursively, system-disk detection (/, /boot, /boot/efi, /usr).
- Read-only MBR/GPT parser (`src/partitions/partition_table.cpp`): header and
  entry-array CRC32, backup-GPT fallback, protective-MBR check, EBR chains with
  loop detection, overlap / out-of-bounds / usable-area warnings, 1 MiB
  alignment flag, UTF-16 names, type-name tables.
- CRC32 + little-endian helpers (`include/platter/core/bytes.hpp`).
- **29 test cases / 239 assertions passing** (range math, CRC, image device,
  GPT 512/4096, corrupted primary/entries/both, MBR+logicals, EBR loops, sysfs).

Next up (in order):
1. Win32 backend (compile-checked here with MinGW; MSVC in CI).
2. smartctl JSON adapter + conservative health assessment.
3. `platter-cli`: `devices list|info`, `partitions list`, `smart show`, `--json`.
4. Qt GUI skeleton with classic theme.
5. GitHub Actions CI, docs (architecture, safety, cli-reference).

## 3. Layout

```
include/platter/<module>/   public headers
src/core/                   error, range, device model (no Qt, no OS calls)
src/platform/file_device.hpp  internal factory shared by backends
src/platform/posix/         POSIX IBlockDevice (images everywhere, block devs on Linux)
src/platform/image/         image-specific defaults + open_image()
src/platform/linux/         sysfs enumeration (next)
src/platform/windows/       Win32 backend (next)
src/partitions/ src/smart/ src/cli/ src/gui/   (next)
tests/unit/ tests/fixtures/
```

## 4. Conventions and decisions

- Namespace `platter`; internals in `platter::detail`.
- Core never depends on Qt. GUI is optional (`PLATTER_BUILD_GUI`).
- Every device read/write path calls `validate_io()` first. Alignment is 1 for
  buffered I/O and the logical sector size when `direct_io` is set.
- `IOResult::duration_ns` is measured around the OS call only; surface scans
  rely on it, so don't add logging or allocation inside that window.
- Serial numbers are collected but must not appear in exported reports unless
  the user asks (spec §4.2).
- Device paths are not identity (spec §17.2); use `stable_id` + serial + model
  + capacity + geometry.
- License: TBD by owner; files carry `SPDX-License-Identifier: TBD`.

## 5. Dev environment notes

- Agent sandbox: Ubuntu 24.04, GCC 13, Clang, CMake 3.28, Ninja, Qt 6.4.2,
  nlohmann-json 3.11, Catch2 3.4, MinGW-w64 (for Windows compile checks).
- Owner's machine: Windows with WSL available. Owner prefers to avoid
  PowerShell; CI uses `shell: bash` on Windows runners.
- Real-hardware Linux testing later can use WSL2 `wsl --mount <disk> --bare`
  (from elevated cmd), but SMART passthrough is unreliable there.

## 6. Build & test

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
./build/tests/platter-tests          # or: ctest --test-dir build
```
Options: `PLATTER_BUILD_GUI`, `PLATTER_BUILD_CLI`, `PLATTER_BUILD_TESTS`, `PLATTER_WERROR`.
Test helpers in `tests/support/` can synthesize GPT (`write_gpt`) and MBR+EBR
(`write_mbr`) images into sparse temp files.

## 7. Known gaps / TODO

- `open_device()` exists on Linux only; Windows backend pending.
- nlohmann_json FetchContent download has no pinned URL_HASH yet.
- Partition table type is not filled into `DeviceInfo::partition_table` during
  enumeration (that needs a read; enumeration is metadata-only by design).
  Callers that open the device can fill it via `read_partition_table()`.
- Linux `describe_device()` only accepts nodes directly under /dev (after
  resolving symlinks); /dev/mapper names resolve to dm-N, which is fine.
