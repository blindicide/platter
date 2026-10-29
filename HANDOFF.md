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
  operations that go through the full safety pipeline (`safety::authorize()`,
  spec §9.5, §17). Never open a device read-write any other way, and never
  construct an `AuthorizedTarget` outside tests (`safety::TestAccess`).
  Destructive code runs only against temp image files / `FaultyWriteDevice`.
- Owner's disposable test hardware (for later, with permission): a clean 1 TB
  HDD and a USB stick.

## 1. What this is

Cross-platform (Windows 10/11, Linux) storage diagnostics suite: Victoria /
HD Tune / CrystalDiskInfo / GParted / ddrescue in one tool, with a deliberate
Win2000/XP-era Qt Widgets UI. Full spec: owner's "HDD Operations Suite v1.0"
document (the spec's `hos-*` names are renamed to `platter-*` throughout).

Repo: https://github.com/blindicide/platter (public, default branch `main`).

Stack: C++20, CMake ≥ 3.21, Qt 6 Widgets (GUI only), nlohmann_json, Catch2 v3
(tests). zstd and SQLite are planned for later milestones and not yet linked.

## 2. Current status

Milestone: **v0.9 Preferences / help / polish — complete in code, GUI only exercised offscreen**
(see "v0.9 so far"). Previously: **v0.8 Privilege separation (Linux helper)**, **v0.7 Inspector / qualification / history browser**, **v0.6 Imaging / cloning**. Previously: **v0.5 Partition editing + FAT32/exFAT format** and **v0.4 Erase + write tests** (same caveat). **v0.3 Benchmarking — feature-complete** (CI green incl. MSVC with
vendored SQLite; no real-hardware run yet, by the owner's choice). Previously: **v0.2 Surface Testing — feature-complete** (v0.1 + v0.2 done; CI
green on Linux, Windows MSVC and MinGW; still NO real-hardware run: the spec's
v0.2 acceptance needs "supported physical devices", i.e. the owner's
disposable HDD / USB stick, with the important drive unplugged).

### v0.9 so far
- **Preferences** (`src/gui/widgets/preferences_dialog.*`, Settings > Preferences; `Preferences` struct
  loads/saves QSettings keys `prefs/*` + existing `history/record`): history on/off, theme
  (classic Win2000 vs platform default; live switch via `apply_theme`, `remember_system_theme()` is
  called in `gui/main.cpp` before the classic theme), toolbar/status bar visibility, privileged helper
  at startup (+ custom command), append-to-file activity log, history DB path override ($PLATTER_HISTORY_DB
  still wins; applies on restart), Reset All Settings. The six "planned" Settings placeholders were
  replaced by this one dialog (no fake pages). Serial numbers are intentionally NOT persisted.
- **Help** (`help_dialog.*`, Help > Documentation, F1): offline topic browser; the HTML is embedded in
  the .cpp. Keep it in sync with docs/ when features change.
- `platter-gui --dialog-screenshot preferences|help|history|checksum=FILE` renders a dialog offscreen
  (used by CI as a smoke test; the history dialog needs a readable history DB).
- Not done: per-device settings, translations (strings use tr() but no .ts files), high-DPI tuning.

### v0.8 so far
- **Privilege separation** (POSIX only; see docs/privilege-separation.md): `platter-helper`
  (`src/helper/helper_main.cpp`, installs to libexec/platter) serves ONE client over stdin/stdout
  (a socketpair created by the client; no listening socket). Library `platter-helper-lib`:
  `wire.{hpp,cpp}` (framing, DeviceInfo/Error codec), `server.cpp` (root side),
  `client.cpp` (`HelperBroker`, `RemoteDevice : IBlockDevice`, `LazyBroker`,
  `configure_helper`, `auto_helper_command`, `find_helper_binary`). Public header
  `include/platter/helper/helper.hpp`.
- **Hook**: `include/platter/core/broker.hpp` — `set_device_broker()`; `open_device()` (now defined in
  `src/core/broker.cpp`) routes through the broker if installed, else `open_device_direct()`
  (`include/platter/platform/direct_open.hpp`; the old per-platform `open_device` bodies were renamed,
  incl. the Windows one — compiled under MinGW only). `OpenOptions::write_ranges` (byte spans) is
  filled by `safety::authorize()` from the confirmed LBA ranges; direct opens ignore it, the helper
  enforces it (refuses rw open without ranges, writes/discards outside them, rw of in-use devices,
  anything that is not a whole-disk block device under /dev).
- **CLI** `--helper auto|none|"<cmd>"` / `$PLATTER_HELPER`; **GUI** File > Use Privileged Helper and
  `--helper SPEC`. Started lazily on first device open. Images never go through the helper.
- **Tests**: `tests/unit/test_helper.cpp` runs the real helper binary against temp images
  (`--allow-regular-files` is a TEST-ONLY switch). Not tested: pkexec/sudo launch, the helper against
  a real block device, `authorize()` over the helper for a real device.
- Known limits: one serialised connection (queue-depth benchmarks are meaningless through it),
  no automatic re-launch if the helper dies, Windows has no helper (run elevated).
- `packaging/linux/org.platter.helper.policy`: polkit action for pkexec (path must match install path).

### v0.7 so far
- **Inspector** `include/platter/inspect/inspect.hpp`, `src/inspect/inspect.cpp` (lib
  `platter-inspect`): `hex_dump`, `interpret` (StructKind mbr / gpt_header / gpt_entries /
  fat_boot / exfat_boot / ntfs_boot / ntfs_mft_record / ext_superblock, auto-detected by
  content), and `search` (bytes, ASCII, UTF-16LE, case-insensitive option, matches across
  block boundaries). All read-only; tests `tests/unit/test_inspect.cpp`.
- **Qualification** `include/platter/qualify/qualify.hpp`, `src/qualify/qualify.cpp` (lib
  `platter-qualify`): `Qualification` operation (AuthorizedTarget kind `qualification`, one
  range). Stages: N write passes (seeded random, next pass = complement), read-back per pass,
  final read, random-read ("seek") sampling; SMART before/after delta supplied by the caller.
  Verdict pass / pass_with_warnings / fail / incomplete; thresholds live in `QualifyConfig`
  and are judgment calls, NOT calibrated on real drives. Report `reports/qualify_report.*`
  (schema `qualify-report`); `HistoryStore::record_qualification/load_qualification`.
  Tests `tests/unit/test_qualify.cpp` (slow limits set to 60 s there so loaded CI boxes
  don't produce timing warnings).
- **History** `HistoryStore::list_all / find / render_text`, `parse_session_kind`; CLI
  `history list|show|delete` (`src/cli/cmd_history.cpp`); GUI `HistoryDialog`
  (`src/gui/widgets/history_dialog.*`, Tools > Operation History).
- **CLI**: `hex`, `find` (`cmd_inspect.cpp`), `qualify run|history|show` (`cmd_qualify.cpp`).
- **GUI**: HEX tab (reads only on explicit Go, never merely on selection), QUALIFY tab,
  `ChecksumDialog` (Tools > Checksum Calculator), the previously "planned" menu items are
  live (Partition Manager, Clone, Create Image, Image Verification, Qualify). The GUI
  was only exercised with `QT_QPA_PLATFORM=offscreen`.
- Hardware secure erase (ATA Secure Erase / NVMe format) is deliberately NOT implemented.

### v0.6 so far
- **Image files** `include/platter/imaging/image_file.hpp`, `src/imaging/image_file.cpp`:
  `ImageWriter` (raw, optional sparse; or ONE zstd frame with the content size and a
  checksum recorded; writes `<out>.partial`, renames on `finish()`), `ImageReader` (auto
  detects zstd by magic; `size()` known for raw and for zstd frames with a content size),
  `.sha256` sidecar helpers. zstd is optional at build time (`PLATTER_HAVE_ZSTD`): system
  library if found, otherwise FetchContent v1.5.6 (cmake/PlatterDependencies.cmake;
  `-DPLATTER_FETCH_ZSTD=ON` forces it, `-DPLATTER_WITH_ZSTD=OFF` disables). Linux CI installs
  `libzstd-dev zstd`.
- **Operations** `include/platter/imaging/imaging.hpp`, `src/imaging/imaging.cpp`:
  `CreateImage` (read-only source; `--recover` = per-sector retry, zero-fill, unreadable
  ranges recorded; SHA-256 of the data; optional re-read of the finished image),
  `RestoreImage` (AuthorizedTarget kind restore_image, single range = image sectors;
  optional pre-check of `expect_sha256`; read-back by SHA-256 + per-block fingerprints to
  locate mismatches), `CloneDevice` (read-only source + AuthorizedTarget kind clone_target,
  same sector size required), `VerifyImage` (hash, expected checksum, optional sector
  comparison against a device). Result type is `ImagingResult` with `Verdict` ok /
  ok_with_unreadable / check_failed / incomplete (enum `JobKind`, not `Kind`: that name is
  `safety::Kind`). Tests: `tests/unit/test_imaging.cpp` (fault injection with
  `FaultyWriteDevice`).
- **Reports/history**: `reports/imaging_report.*` (schema `imaging-report` v1, own SHA-256
  digest), `HistoryStore::record_imaging/load_imaging` (SessionKind::imaging).
- **CLI**: `src/cli/cmd_imaging.cpp` — `image create|restore|verify|history|show`, `clone`;
  new exit code 16 (check failed). New options/flags listed in `kValueOptions` /
  `kFlagOptions` in `src/cli/main.cpp`.
- **GUI**: `src/gui/tabs/imaging_tab.{hpp,cpp}` — CLONE / IMAGE tab (create / restore /
  clone / verify). Restore and clone go through `confirm_and_authorize`. Clone's target is
  picked from the device list (`setChoices`).
- Validated by hand: `zstd -dc` of a platter image gives the same SHA-256 as the sidecar;
  restore/clone/verify round trips on temp images. NEVER run against real devices from here.
- Not done: resumable copies / rescue log, multi-frame or seekable zstd, image mounting,
  resizing of the target's partitions on clone-to-larger-disk.

### v0.5 so far
- **Partition planning/editing** `include/platter/partitions/partition_edit.hpp`,
  `src/partitions/partition_edit.cpp`: `PartitionPlan` (blank / from_table /
  from_device; add, remove, set_*, resize; problems() vs warnings(); diff();
  encode() = exact sector images; metadata_ranges() = confirmed ranges) and
  `apply_plan(AuthorizedTarget&, plan)` (writes, flushes, reads back, asks the OS
  to re-read). GPT: 128x128-byte entries, protective MBR, backup header at the
  last LBA. MBR: 4 primaries, one extended container (0x0F), EBR chain (first EBR
  at the container start, later ones at logical.first-1, link offsets relative
  to the container), boot code preserved. add/resize are rolled back if they
  introduce new problems. Tests: `tests/unit/test_partition_edit.cpp`
  (validated by hand against sfdisk/fdisk on images).
- **Native mkfs** `include/platter/mkfs/mkfs.hpp`, `src/mkfs/mkfs.cpp`
  (`src/mkfs/upcase_table.inc` = exFAT up-case table): FAT32 (32 reserved
  sectors, 2 FATs, FSInfo, backup boot at 6, cluster size from the MS table,
  needs >= 65525 clusters) and exFAT (boot region + backup with checksum sector,
  bitmap, generated up-case table, root). Quick format only; boot sector written
  last (old boot area zeroed first); metadata read back. Validated with
  `fsck.vfat` / `fsck.exfat` for 512 and 4096-byte sectors (manual; tests parse
  the fields themselves). Tests: `tests/unit/test_mkfs.cpp`.
- `read_partition_table()` now recognizes a FAT/exFAT/NTFS boot record at LBA 0
  and reports TableType::none + a warning instead of misparsing it as MBR.
- `IBlockDevice::reread_partitions()` (BLKRRPART on Linux; Windows
  IOCTL_DISK_UPDATE_PROPERTIES). Windows `Win32FileDevice` now locks+dismounts
  all volumes on the disk before write access (compile-only checked, never run).
- CLI: `partitions types|init|add|delete|set|resize`, `format`
  (`src/cli/cmd_partitions.cpp`, `cmd_format.cpp`; shared `authorize_from_cli`).
- GUI: `src/gui/widgets/partition_editor.{hpp,cpp}` — dialog opened from the
  PARTITIONS tab ("Edit Partitions..."): staged edits with live validation and
  diff, Apply and Format each go through `confirm_and_authorize`. Dev aid:
  `platter-gui --editor-screenshot FILE image.img` (offscreen).
- Not done / limits: no file system or data moving on resize; no NTFS/ext4
  creation; the GUI editor was only exercised offscreen.

### v0.4 so far
- **Safety gate** `include/platter/core/safety.hpp`, `src/core/safety.cpp`:
  `Request` -> `make_preview()`/`preview_for()` -> `Preview` (blockers, warnings,
  accepted confirmations, prompt) -> `authorize(Preview, typed)` ->
  move-only `AuthorizedTarget` (exclusive lease + O_EXCL RW handle + identity
  re-check; write/discard refused outside confirmed ranges). `preview_text()`
  renders the preview for CLI and GUI. Audit sink via `set_audit_sink()`.
  Kinds: erase, write_test, partition_edit, filesystem_create, restore_image,
  clone_target, qualification (the last four are reserved for v0.5/v0.6/v0.7).
  Tests: `tests/unit/test_safety.cpp`; test-only construction through
  `safety::TestAccess::make()` in `tests/support/write_support.hpp`, together
  with `FaultyWriteDevice` (bad_write_lbas, dropped_lbas, bad_read_lbas,
  remove_after_writes, write_delay, write_log).
- **Erase engine** `include/platter/erase/erase.hpp`, `src/erase/erase.cpp`:
  methods zero / one / random / random_n / dod3 / dod7 / pattern / discard,
  verify none / last / every, position-addressed fills (pattern phase and
  ChaCha20 keystream by absolute offset so verification regenerates the data),
  sector-wise fallback on write failure, verdicts, `Errc::refused`.
  ChaCha20 (RFC 8439) in `core/chacha20.*`. `IBlockDevice::discard()` (virtual,
  default unsupported): `fallocate(PUNCH_HOLE)` for images, `BLKDISCARD` for
  Linux block devices, nothing on Windows. Reports: `reports/erase_report.*`
  (schema `erase-report` v1, `report_sha256` of the body). History:
  `SessionKind::{erase,imaging,qualification}`, `record_erase/load_erase`,
  generic `record_document/load_document`.
- **Hardware secure erase (ATA Secure Erase / NVMe Sanitize) is deliberately NOT
  implemented** (can brick drives; untestable here). The spec asked for it;
  decide with the owner before ever adding it.
- **Write benchmarks**: `BenchKind::sequential_write` / `random_write`; the
  `Benchmark(AuthorizedTarget, BenchConfig)` constructor (kind write_test, region
  inside confirmed ranges, threads share the single authorized handle, flush
  counted in phase time). Read constructor refuses write kinds.
- **Write surface scans**: `SurfaceScan(AuthorizedTarget, ScanConfig)` with modes
  write, write_readback, custom_pattern (`ScanConfig::pattern`; default A5 5A;
  readback mismatch = `Errc::corrupt_data` block). VERIFY mode is still
  unsupported (needs a device-side verify command).
- **CLI**: shared helpers in `src/cli/common.hpp` (`authorize_from_cli()` is the
  whole destructive front half: preview, refuse, --dry-run, confirmation,
  authorize); `erase run|methods|history|show` in `src/cli/cmd_erase.cpp`;
  `surface scan --mode ...`; `benchmark read --test write-seq|write-random`.
  New exit codes 14 (refused) and 15 (erase completed with errors).
- **GUI**: `widgets/confirm_dialog.*` (`confirm_and_authorize()`: the only GUI
  way to a destructive operation; button disabled until the typed text
  matches), `tabs/erase_tab.*` (ERASE tab), SURFACE tab write modes, BENCHMARK
  tab write kinds. Status bar says "CONFIRM TO WRITE".
- **Windows**: `win32_file_device.cpp` locks + dismounts every volume of a
  physical disk before an exclusive RW open (`lock_volumes_of_disk`) and issues
  FSCTL_ALLOW_EXTENDED_DASD_IO. Compile-checked with MinGW only; never run.
- Project version 0.4.0.
- NOT done / unvalidated: any real-hardware run, Windows runtime behaviour,
  the Windows write path under MSVC beyond what CI compiles.

### v0.3 so far
- Owner chose to skip the real-hardware run for v0.2 for now and move on.
- SQLite: system library when found (`PLATTER_SYSTEM_SQLITE`, default ON),
  otherwise the vendored amalgamation in `third_party/sqlite3` (3.53.4,
  public domain; provenance + SHA-256 in third_party/sqlite3/README.md:
  extracted from npm `better-sqlite3@13.0.3` because sqlite.org is not
  reachable from the agent sandbox). Target name is `SQLite::SQLite3` either way.
- `LatencyHistogram` moved to `include/platter/core/histogram.hpp` (+ `merge()`,
  `buckets()`); surface keeps a `using` alias. Seeded RNG shared in
  `include/platter/core/rng.hpp` (splitmix64).
- `Operation::supports_pause()` (virtual, default true); benchmarks return false.
- Benchmark engine (`include/platter/benchmark/benchmark.hpp`, `platter-benchmark`):
  kinds sequential / random / access_time / speed_curve / block_sizes /
  queue_depths. Throughput = bytes / phase wall time; latency = per-request OS
  call time; QD N = N threads each with their OWN handle (Windows serializes
  I/O on one synchronous handle) issuing synchronous reads. Per-kind defaults
  (`with_defaults`): seq 1 MiB x up to 1 GiB / 30 s; random 4 KiB / 10 s;
  access = 1 sector QD1 / 5 s; curve 100 points x 16 MiB; sweeps 3 s or
  256 MiB per step (block-size steps start at different offsets so the drive
  cache can't serve them). Results: phases with Metrics (MB/s, MiB/s, IOPS,
  min/mean/median/p95/p99/max latency, errors), throughput-vs-time timeline
  (250 ms samples), throughput-vs-position samples, merged latency buckets,
  spec 7.4 caveats (image file, buffered I/O, small test size, small random
  region). Read errors counted (abort after 64); vanished device -> FAILED.
  Read-only handles enforced at validation AND in every worker.
- 14 benchmark test cases (stress-run randomized x15 and pinned to 1 CPU x8,
  no flakes). Full suite ~2.7 s.

- SHA-256 (`include/platter/core/sha256.hpp`), verified against FIPS 180-4
  vectors (incl. 1M x 'a' in odd chunks). Used to hash serials for device keys.
- Benchmark reports (`include/platter/reports/bench_report.hpp`): canonical
  compact JSON (kind `benchmark`, `bench_schema` 1; config, phases with full
  metrics, timeline, positions, latency buckets, caveats; serial omitted
  unless requested) + strict loader + text report with every setting.
- **Agents: build locally with `-DPLATTER_WERROR=ON`.** CI uses -Werror and a
  -Wshadow in test code slipped through a non-Werror local build once.

- History store (`include/platter/storage/history.hpp`, `platter-storage`):
  SQLite, schema v1 (`meta` with schema_version, `devices`, `sessions` with
  kind benchmark / surface-scan / smart, summary columns + full JSON document,
  ON DELETE CASCADE). Migrations run in a transaction; a DB with a NEWER
  schema is refused (`unsupported`), a non-database file -> `corrupt_data`.
  WAL mode, busy timeout 3 s. Default path: $PLATTER_HISTORY_DB, else
  %APPDATA%\platter\history.db / $XDG_DATA_HOME or ~/.local/share/platter/history.db.
  **Privacy:** device keys are `id:`+sha256(stable_id)[:24] / `sn:`+sha256(model|serial)
  / `image:name:capacity`; documents stored with serials omitted. A test
  greps the raw DB bytes for the serial.
- Privacy fix (all exports + CLI/GUI display): Linux by-id stable ids such as
  `ata-MODEL_SERIAL` embed the serial; `reports::redact_stable_id()` masks it
  unless serials were requested.

- CLI: `benchmark read|history|show` (all BenchConfig options, live
  progress, Ctrl+C, `--chart` ASCII charts, `--report`), `--save` /
  `--history-db` for benchmark, surface scan and SMART. Saving is opt-in in
  the CLI; a failed save only warns. `parse_size` accepts K/M/G[iB].

- GUI BENCHMARK tab (`src/gui/tabs/benchmark_tab.*`) + `Chart` widget
  (`src/gui/widgets/chart.*`: classic line / bar chart, navy series, grey
  dashed comparison series, boxed legend, optional fixed X range). Settings
  for every BenchConfig field (write tests listed but disabled "[v0.4]"),
  Start / Stop (no pause), progress, headline, chart selector per kind
  (throughput or IOPS vs time, speed vs position 0-100 %, by block size, by
  QD, latency histogram collapsed to octaves), phase metrics table, caveats,
  History sub-tab (load / delete) and Compare combo that overlays a saved
  session of the same kind. Operations > Benchmark (F6).
- GUI history: opens `HistoryStore::default_path()` (the SAME DB as the CLI,
  verified both directions). Settings > Record History (QSettings
  `history/record`, default on) records completed benchmarks, finished
  (non-cancelled) surface scans and every SMART query. If the DB can't be
  opened the GUI logs a warning and history features disable themselves.
- Dev aids: `--bench-kind N --start-bench --compare ID --shot-delay MS`.

v0.3 open items: write benchmarks (spec 7.2, need the v0.4 destructive
pipeline); SMART health-history graphs (data is now recorded; spec 5.4 graphs
belong with v0.7); real-hardware validation.

### v0.2 so far
- Operation engine (`include/platter/core/operation.hpp`): states CREATED ->
  VALIDATING -> READY -> RUNNING <-> PAUSED -> STOPPING -> COMPLETED / FAILED /
  CANCELLED with an explicit edge table (`is_valid_transition`); invalid edges
  are ignored so racing requests can't corrupt state. Worker thread per
  operation; front ends POLL `progress()` (no per-sector callbacks).
  Progress: bytes done/total, elapsed excluding pauses, 1 s-window current rate,
  average rate, ETA, errors, warnings, current LBA, message. Cooperative
  `OperationControl::checkpoint()` blocks while paused, returns false on cancel.
- `DeviceLeaseRegistry`: many `shared_read` holders OR one `exclusive` holder
  per device id; RAII leases. Surface scans hold a shared lease while running.
- Surface scan (`include/platter/surface/surface_scan.hpp`, `platter-surface`):
  READ mode only (VERIFY / WRITE* rejected at validation with `unsupported`;
  VERIFY needs a device verify command or a documented fallback, spec §6.1).
  Refuses a writable handle. Config: start/end LBA, block size (sector
  multiple, <= 64 MiB), order forward / reverse / random / sampled (splitmix64,
  deterministic per seed, identical on all platforms), retries (<= 16),
  direct I/O (auto-fallback to buffered with a recorded warning), error policy
  continue / stop-after-N, configurable latency thresholds (default 5/20/50/200 ms).
  Per-block packed `BlockResult` (12 bytes: duration, time since start, errc,
  retries, class); 1-byte class array for cheap map copies; streaming log2x16
  latency histogram (~6 % percentile error) for min/max/mean/median/p95/p99.
  A device that disappears (device_removed / not_found) stops the scan at once,
  with no retries. Durations come from `IOResult::duration_ns` on success.
- Test double `tests/support/faulty_device.hpp`: bad LBAs, flaky LBAs (fail N
  then recover), fake per-LBA latency, real delay, simulated removal.
- (v0.2 at the time: 72 test cases / 1009 assertions); operation/surface tests were
  stress-run 25x randomized and pinned to one CPU with no flakes.

- Scan export/import (`include/platter/reports/scan_report.hpp`, in
  `platter-reports`): JSON (canonical; envelope kind `surface-scan`,
  `scan_schema` 1; tested blocks as compact rows `[index, duration_us,
  time_ms, errc, retries, class]`, untested implied), CSV (one row per tested
  block with exact LBA range and MB/s), text report (config, results,
  distribution, first 100 unreadable ranges, explicit "latency is not health"
  note). `scan_from_json` validates schema, geometry, indices, classes,
  duplicates and row count and rejects anything inconsistent
  (`corrupt_data`). Serials omitted / masked unless requested.

- CLI `platter-cli surface scan <device>` (`cmd_surface_scan` in
  src/cli/main.cpp): all ScanConfig options as flags, live progress on stderr
  (\r line on a TTY, every 10 s otherwise), Ctrl+C -> `cancel()` with partial
  results still written, `--csv/--report/--json -o/--map` outputs, exit 9 =
  unreadable blocks, 13 = cancelled. Verified on images (incl. real SIGINT).

- GUI SURFACE tab (`src/gui/tabs/surface_tab.*`, `src/gui/widgets/surface_map.*`):
  settings panel (mode READ; VERIFY/WRITE listed but disabled "[v0.4]"),
  Start / Pause-Resume / Stop, Open Scan... (loads JSON for viewing or
  comparison), Export... (JSON / CSV / text by filter, serials omitted).
  `SurfaceMap`: QAbstractScrollArea painting only visible rows; 9 px cells,
  worst-class-per-cell aggregation, Fit (auto power-of-two) or fixed zoom,
  Ctrl+wheel zoom, click / arrow-key selection. Legend with per-class counts,
  statistics panel, selected-cell details (block and LBA range, size, slowest
  read, throughput, errors, retries, time). Polls the scan every 250 ms. The
  tab owns the SurfaceScan; a running scan stays bound to its device even if
  the selection changes; closing the window mid-scan asks first.
  Operations > Surface Test (F4) and a toolbar button jump to the tab.
  Dev aids: `--open-scan FILE`, `--start-scan`.

v0.2 open items: real-hardware validation; VERIFY mode (needs ATA VERIFY /
SCSI VERIFY passthrough or an explicitly documented fallback); scan history in
SQLite and side-by-side comparison of two saved maps (v0.7 per spec).

### Core (`platter-core`)
- `Error` / `Result<T>` / `Status` (no exceptions across module boundaries;
  C++20 so no `std::expected`).
- Overflow-safe range arithmetic and `validate_io()` (`core/range.hpp`).
- `DeviceInfo`, `CapabilitySet`, bus/media enums (`core/device.hpp`).
- CRC32 + little-endian helpers (`core/bytes.hpp`).
- Process runner (`core/process.hpp`): posix_spawnp / CreateProcessW, argv
  passed verbatim (no shell), stdout/stderr capture, kill on timeout.
- `running_elevated()` / `privilege_label()` (`platform/privileges.hpp`).

### Backends
- POSIX `PosixFileDevice` (`src/platform/posix/`): pread/pwrite loops with
  EINTR + short-read handling, per-call timing, O_DIRECT / O_EXCL,
  BLKGETSIZE64 / BLKSSZGET / BLKPBSZGET / BLKROGET. Used for images on all
  POSIX systems and for Linux block devices.
- Linux enumeration (`src/platform/linux/sysfs.cpp`): model/vendor/fw, serial
  (incl. SCSI VPD 0x80), stable id (wwid or best /dev/disk/by-id link, wwn-
  preferred), bus from sysfs topology, rotational/removable/ro/discard, mounts
  + swap per partition, holder chains (dm-crypt/LVM) followed recursively,
  system-disk detection (/, /boot, /boot/efi, /usr). Metadata only.
- Win32 (`src/platform/windows/`): `Win32FileDevice` (ReadFile/WriteFile with
  OVERLAPPED offsets, 1 GiB chunking, EOF -> short_transfer); PhysicalDrive
  enumeration with **zero-access handles** (StorageDeviceProperty, geometry,
  alignment, seek penalty, TRIM, IS_WRITABLE, device-id VPD -> wwn-/eui.
  stable id); volume -> disk mapping via IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS;
  system disk = disk hosting the Windows directory. **Compile-checked only**
  (MinGW, warning-free); never executed.

### Partitions (`platter-partitions`)
- Read-only MBR/GPT parser: header and entry-array CRC32, backup-GPT fallback,
  protective-MBR check, EBR chains with loop detection, overlap /
  out-of-bounds / usable-area warnings, 1 MiB alignment flag, UTF-16 names,
  type-name tables. Cross-checked against util-linux **sfdisk** (independent
  implementation): identical LBAs, names, signatures.

### SMART (`platter-smart`)
- Runs `smartctl --json=c -a` (read-only query, timeout) and parses ATA
  attributes, NVMe health log, ATA + NVMe self-test logs, error-log count,
  smartctl messages / exit bits. `assess()` is conservative: PASSED alone ->
  UNKNOWN; counts use the low 32 bits of raw (vendor-packed high bits
  ignored); CRC errors flagged as cabling/bridge; every contributing
  observation listed. SMART / SELF_TEST capabilities come only from actual
  smartctl results.

### Reports (`platter-reports`)
- Shared JSON schema for CLI `--json` and GUI "Save Report"; envelope with
  tool/version/schema/kind/timestamp; serials omitted unless requested.

### CLI (`platter-cli`)
- `devices list|info`, `partitions list`, `smart show`, `version`;
  `--json -o -v -q --show-serial --sector-size -d`; documented exit codes
  (docs/cli-reference.md). Any regular file is treated as an image. Short
  names: `sda` -> /dev/sda; `1`/`pd1` -> \\.\PhysicalDrive1. Every target is
  opened `OpenMode::read_only`. Cross-compiles to `platter-cli.exe` (MinGW).

### GUI (`platter-gui`)
- Classic theme (Qt "Windows" style + Win2000 #D4D0C8 palette, navy
  selection, Tahoma / Liberation Sans 8 pt, animations off). Full spec menu
  bar (unbuilt features present but disabled with "Planned for v0.x" tips),
  16 px toolbar with device selector, Device Summary panel, tabs INFO / SMART /
  SURFACE / BENCHMARK / PARTITIONS / ERASE / CLONE / IMAGE / HEX / LOG
  (planned ones are placeholders), Activity Log pane, status bar (device,
  READ-ONLY, privileges).
- SMART query and partition reads run on worker threads; results are matched
  back by device path and dropped if the device vanished. Refresh never
  silently re-targets a different device; cached results survive a refresh
  only if stable id + model + capacity still match.
- Tables: sortable, Copy as TSV, Export CSV, persistent column layout
  (QSettings). Save Report asks before including serials. Serials masked on
  screen unless Settings > Show Serial Numbers (never persisted).
- Dev aids: `platter-gui IMG... --tab NAME --size WxH --select N --query-smart
  --screenshot out.png` with `QT_QPA_PLATFORM=offscreen` renders headless;
  `PLATTER_SMARTCTL=<script>` swaps in a fake smartctl for UI work.

### Tests
- **43 test cases / 374 assertions passing** (Catch2): range math, CRC, image
  device (512/4096, read-only enforcement, bounds), GPT valid / corrupt
  primary / corrupt entries / both corrupt / overlaps / no protective MBR, MBR
  + logicals, EBR loops, past-end partitions, fake-sysfs enumeration, SMART
  fixtures (ATA healthy/pending/failing/packed-raw/hot, NVMe healthy/worn,
  unknown USB bridge, PASSED-only), process runner, fake-smartctl end-to-end,
  sfdisk cross-check. Release `-Werror` build is clean.

### CI / docs
- CI failures are re-emitted as `::error` annotations by `scripts/ci-run.sh`.
  This matters for agents: raw Actions logs are NOT reachable from the agent
  sandbox (blob storage blocked), but annotations are, via
  `GET /repos/blindicide/platter/check-runs/<job_id>/annotations`.
- Windows CI lets CMake choose the Visual Studio generator; the hard-coded
  "Visual Studio 17 2022" broke on the current runner image.
- `.github/workflows/ci.yml` (every step `shell: bash`): Linux GCC + Qt 6
  (-Werror build, ctest, CLI smoke on an sfdisk image, offscreen GUI
  screenshot, artifacts); Windows MSVC + Qt 6.7 via install-qt-action (build,
  ctest, CLI smoke on a blank image, offscreen GUI screenshot, windeployqt
  package); MinGW cross-compile job. **Not yet run**; expect MSVC nits.
- README.md, docs/architecture.md, docs/safety.md, docs/cli-reference.md.

### Next up (in order)
1. Owner's real-hardware runs (read-only first, then destructive on the
   disposable 1 TB HDD / USB stick with every other disk unplugged).
2. (v0.9 done) v1.0 release prep (see sections below
   as they get filled in).

## 3. Layout

```
include/platter/{core,platform,partitions,smart,reports}/   public headers
src/core/             error, range, device model, bytes/CRC, process runner
src/platform/         file_device.hpp (internal factory), privileges
  posix/              PosixFileDevice
  linux/              sysfs enumeration + open_device()
  windows/            Win32FileDevice, enumeration, win32 utils
  image/              image defaults + open_image()
src/partitions/       MBR/GPT parser
src/smart/            smartctl adapter + assessment
src/reports/          JSON schema
src/erase/            overwrite erase engine
src/cli/              platter-cli (main.cpp, cmd_erase.cpp, common.hpp)
src/gui/              platter-gui: styles/, widgets/, tabs/, mainwindow/
tests/unit/ tests/support/ tests/fixtures/smart/
cmake/                deps, warnings, MinGW toolchain
docs/                 architecture, safety, CLI reference
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

- Windows: system-disk detection only checks the volume holding the Windows
  directory; the EFI/boot partition's disk is not yet flagged (TODO v0.8).
- Windows: volume locking for exclusive disk access is implemented (v0.4) but
  has never run; first real Windows write test must be on the disposable disk.
- Windows unit tests have never run (no Wine here); CI will be first.
- GUI has never been built with MSVC or run on Windows; only Linux/offscreen.
- Toolbar icons are Qt standard icons; spec wants small monochrome 16x16
  custom icons (assets/icons) later.
- Image sector size in the GUI is 512 unless `--sector-size 4096` is passed
  on the command line; needs a choice in Open Disk Image.
- virtio disks show the PCI vendor id ("0x1af4") as model; cosmetic.
- nlohmann_json FetchContent download has no pinned URL_HASH yet.
- SMART fixtures in `tests/fixtures/smart/` are HAND-WRITTEN in smartctl 7.4's
  JSON shape, not real captures. Replace/add real `smartctl --json=c -a` output
  from the owner's drives once hardware testing is allowed (strip serials).
- `process_posix.cpp` uses `pipe2` (Linux/glibc); macOS would need pipe+fcntl.
- Health thresholds (temperature 60 °C ATA / 75 °C NVMe, endurance 90%) are
  hard-coded; spec wants them configurable via Settings later.
- Partition table type is not filled into `DeviceInfo::partition_table` during
  enumeration (that needs a read; enumeration is metadata-only by design).
  Callers that open the device can fill it via `read_partition_table()`.
- Linux `describe_device()` only accepts nodes directly under /dev (after
  resolving symlinks); /dev/mapper names resolve to dm-N, which is fine.
