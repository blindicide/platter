# platter

A native, cross-platform (Windows 10/11, Linux) storage diagnostics, maintenance
and management utility: device identification, SMART health, partition
inspection, and, in later milestones, surface scans, benchmarks, erasure,
imaging and cloning. It has a deliberately old-school, information-dense
Win2000-style Qt Widgets interface.

> **Status: v0.7 (sector inspector, qualification, history browser).** Destructive operations exist and go
> through a typed-confirmation safety gate, but have only ever been run against
> image files and test doubles, never real hardware. See [HANDOFF.md](HANDOFF.md) for the detailed state of
> the code and [docs/safety.md](docs/safety.md) for the safety model.

## What works today

- Hex sector viewer and searcher (read-only) that decodes MBR, GPT, FAT/exFAT/NTFS
  boot sectors, NTFS MFT records and ext superblocks; byte/ASCII/UTF-16 search
- Drive qualification (burn-in): write passes, read-back, final read, random
  reads and a SMART before/after comparison with a PASS / WARNINGS / FAIL verdict
- History browser over every saved benchmark, scan, SMART, erase, imaging and
  qualification report (GUI and `platter-cli history`)

- Disk imaging (raw or zstd, with SHA-256 and verification), error-tolerant
  recovery copies with an unreadable-sector map, image restore, device-to-device
  clone, image verify / compare

- Partition table editing (GPT and MBR incl. extended/logical) and native
  quick FAT32 / exFAT formatting, in the CLI and in the GUI partition manager

- Device discovery on Linux (sysfs, /proc) and Windows (zero-access
  `\\.\PhysicalDriveN` queries), with mount / swap / dm-crypt / LVM /
  system-disk detection
- Raw image files as first-class devices (512 or 4096-byte sectors)
- MBR and GPT inspection with CRC validation, backup-GPT fallback, EBR chains,
  overlap / bounds / alignment checks
- SMART and NVMe health through smartmontools (`smartctl --json`), with a
  conservative GOOD / WARNING / CRITICAL / UNKNOWN assessment that lists every
  contributing observation
- Read-only surface scans: LBA ranges, block sizes, forward / reverse /
  random / sampled order, retries, direct I/O, configurable latency classes,
  per-block results, latency percentiles, pause / resume / cancel, and
  JSON / CSV / text export (saved scans can be reopened)
- Read benchmarks: sequential, random (IOPS), access time, full-disk speed
  curve, block-size and queue-depth sweeps, with latency percentiles, charts
  and explicit caveats when results may reflect caches rather than the device
- Overwrite erase (zero / one / random / multi-pass / DoD / custom pattern /
  discard) with read-back verification, honest reports and a typed-confirmation
  safety gate; write surface scans and write benchmarks behind the same gate
- Local history in SQLite (benchmarks, scans, SMART snapshots), shared by
  CLI and GUI, with no raw serial numbers stored
- `platter-cli` with JSON output and documented exit codes
- `platter-gui` with INFO, SMART, SURFACE (Victoria-style latency map),
  BENCHMARK (HD Tune-style charts, history, run comparison), PARTITIONS and
  LOG tabs

## Building

Requirements: CMake ≥ 3.21, a C++20 compiler (GCC 13, Clang 16, MSVC 2022),
Qt 6.2+ (Widgets) for the GUI. nlohmann_json and Catch2 are used from the
system when present and fetched automatically otherwise.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
```

On Windows, any shell works (cmd, Git Bash); PowerShell is not required:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:\Qt\6.7.2\msvc2019_64
cmake --build build --config Release
```

Options: `PLATTER_BUILD_GUI`, `PLATTER_BUILD_CLI`, `PLATTER_BUILD_TESTS`,
`PLATTER_WERROR`.

For SMART data, install [smartmontools](https://www.smartmontools.org/).

## Usage

```sh
platter-cli devices list
platter-cli devices info sda                # or: devices info 1   (Windows: PhysicalDrive1)
sudo platter-cli partitions list /dev/sdb
sudo platter-cli smart show /dev/sda --json -o sda-smart.json
sudo platter-cli surface scan /dev/sdb --csv sdb.csv --map
sudo platter-cli benchmark read /dev/sdb --test curve --chart --save
platter-cli partitions list disk.img        # images need no privileges
```

See [docs/cli-reference.md](docs/cli-reference.md).

## Layout

```
include/platter/   public headers (core, platform, partitions, smart, reports)
src/core           errors, range validation, device model, process runner
src/platform       POSIX / Linux / Win32 backends, image files
src/partitions     MBR / GPT parser
src/smart          smartctl adapter and health assessment
src/reports        shared JSON report schema
src/cli            platter-cli
src/gui            platter-gui (Qt 6 Widgets)
tests/             Catch2 unit tests, fixtures, test helpers
docs/              architecture, safety model, CLI reference
```

## License

To be decided by the author. Third-party: Qt 6 (LGPL v3), nlohmann/json (MIT),
SQLite (public domain; vendored copy in third_party/), Catch2 (BSL-1.0, tests only); smartmontools (GPL v2) is invoked as an external
program and not linked.
