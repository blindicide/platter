# platter

A native, cross-platform (Windows 10/11, Linux) storage diagnostics, maintenance
and management utility: device identification, SMART health, partition
inspection, and, in later milestones, surface scans, benchmarks, erasure,
imaging and cloning. It has a deliberately old-school, information-dense
Win2000-style Qt Widgets interface.

> **Status: v0.1 (foundation), read-only.** Nothing in this version opens a
> device for writing. See [HANDOFF.md](HANDOFF.md) for the detailed state of
> the code and [docs/safety.md](docs/safety.md) for the safety model.

## What works today

- Device discovery on Linux (sysfs, /proc) and Windows (zero-access
  `\\.\PhysicalDriveN` queries), with mount / swap / dm-crypt / LVM /
  system-disk detection
- Raw image files as first-class devices (512 or 4096-byte sectors)
- MBR and GPT inspection with CRC validation, backup-GPT fallback, EBR chains,
  overlap / bounds / alignment checks
- SMART and NVMe health through smartmontools (`smartctl --json`), with a
  conservative GOOD / WARNING / CRITICAL / UNKNOWN assessment that lists every
  contributing observation
- `platter-cli` with JSON output and documented exit codes
- `platter-gui` with INFO, SMART, PARTITIONS and LOG tabs

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
Catch2 (BSL-1.0, tests only); smartmontools (GPL v2) is invoked as an external
program and not linked.
