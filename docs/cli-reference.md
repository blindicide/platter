# platter-cli reference

`platter-cli` uses the same core libraries as the GUI. Commands are read-only
unless marked **destructive** below; destructive commands go through the
safety pipeline in [safety.md](safety.md): preview, typed confirmation and
identity re-check before the first write.

```
platter-cli [options] <command> [arguments]
```

## Commands

| Command | What it does | Needs elevation? |
|---|---|---|
| `devices list` | Lists whole-disk block devices with model, capacity, bus, media, sector sizes and usage (mounted / system / held by dm, LVM, md). | No (Linux: sysfs; Windows: zero-access handles) |
| `devices info <device>` | Identification, geometry, usage and the capability matrix for one device or image. | No |
| `partitions list <device>` | Reads and validates the MBR/GPT. Reports CRC problems, backup-GPT fallback, overlaps, out-of-bounds entries and 1 MiB alignment. | Yes for physical devices (reads sectors) |
| `surface scan <device>` | Read-only block-level surface scan: per-block latency classes, unreadable ranges, throughput and latency percentiles. Prints a text report (or JSON with `--json`); can also write CSV and a text report. Ctrl+C stops cleanly and still reports partial results. | Yes for physical devices |
| `benchmark read <device>` | Read benchmark: `--test seq` (default), `random`, `access`, `curve`, `sizes`, `qd`. Reports MB/s, MiB/s, IOPS and latency percentiles per phase, plus caveats when results may not reflect the physical device. `--chart` adds an ASCII chart. | Yes for physical devices |
| `surface scan <device> --mode write\|write-readback\|pattern` | **Destructive.** Writes a pattern (default `A5 5A`, or `--pattern HEX`) over the range, optionally reading it back and comparing. Range from `--start/--end` (default: the whole device). | Yes |
| `benchmark read <device> --test write-seq\|write-random` | **Destructive.** Write benchmark in an explicit `--start/--end` region. Reports MB/s, IOPS, latency; flush time is included. | Yes |
| `erase run <device>` | **Destructive.** Overwrite erase with `--method`, `--verify`, optional `--range`/`--start`/`--end`, `--report FILE`, `--save`. See below. | Yes |
| `erase methods` | Lists the methods and their pass counts. | No |
| `erase history` / `erase show <id>` | Saved erase sessions and their full reports. | No |
| `partitions types` | Lists the type names accepted by `--type`. | No |
| `partitions init <device> --table gpt\|mbr` | **Destructive.** Writes a new, empty partition table. | Yes |
| `partitions add\|delete\|set\|resize <device> ...` | **Destructive.** Edits the existing table. See below. | Yes |
| `format <device> --fs fat32\|exfat [--partition N]` | **Destructive.** Native quick format. See below. | Yes |
| `image create <device> <file>` | Copies a device (or `--start/--end` range) to an image: raw, or one zstd frame for `*.zst` / `--compress zstd`. Records a SHA-256 of the data and writes `<file>.sha256`. Read-only on the source. See below. | Yes for physical devices |
| `image verify <file>` | Re-hashes an image and compares it with `--sha256`, `<file>.sha256`, and (with `--against DEVICE`) the device itself. | No (Yes for physical devices with `--against`) |
| `image restore <file> <device>` | **Destructive.** Writes an image onto a device. | Yes |
| `clone <source> <target>` | **Destructive.** Copies one device onto another, sector for sector. | Yes |
| `image history` / `image show <id>` | Saved imaging sessions and their reports. | No |
| `benchmark history [<device>]` | Lists saved benchmark sessions (newest first), optionally for one device. | No |
| `benchmark show <id>` | Re-prints a saved session (text, or JSON with `--json`; `--chart` works too). | No |
| `smart show <device>` | Runs `smartctl --json=c -a` and prints attributes / NVMe health log plus a conservative assessment with every contributing observation. | Usually yes |
| `hex <device>` | Hex dump of one or more sectors (`--lba N` or `--offset BYTES`, `--sectors N` up to 256), with the structure found there decoded (MBR, GPT header/entries, FAT/exFAT/NTFS boot sectors, NTFS MFT record, ext superblock). `--as KIND` forces an interpretation, `--no-interpret` turns it off, `--json` for machine output. Read-only. | Yes for physical devices |
| `find <device>` | Searches a range for `--hex BYTES`, `--ascii TEXT` or `--utf16 TEXT` (`--ignore-case`, `--start/--end`, `--limit N` matches). Read-only. | Yes for physical devices |
| `qualify run <device>` | **Destructive.** Drive qualification (burn-in): write passes, read-back after each, a final read pass, random-read sampling and a SMART before/after comparison, ending in a PASS / PASS WITH WARNINGS / FAIL / INCOMPLETE verdict. See below. | Yes |
| `qualify history` / `qualify show <id>` | Saved qualification reports. | No |
| `history list [<device>]` | Every saved session of every kind, newest first (`--kind benchmark\|surface_scan\|smart\|erase\|imaging\|qualification`, `--limit N`). | No |
| `history show <id>` / `history delete <id>` | Re-print a saved session (`--json` for the stored document) / delete the saved record (never touches a device). | No |
| `version` | Prints the version. | No |

`<device>` may be:

- a device path: `/dev/sda`, `/dev/nvme0n1`, `/dev/disk/by-id/...`, `\\.\PhysicalDrive1`
- a short name: `sda`, `nvme0n1` (Linux); `1` or `pd1` (Windows)
- a raw image file (anything that is a regular file is treated as an image)

## Options

| Option | Meaning |
|---|---|
| `--json` | JSON output (same schema as GUI exports; every report has `tool`, `version`, `schema`, `kind`, `generated_utc`). |
| `-o, --output FILE` | Write output to FILE. |
| `-v, --verbose` | More detail (e.g. raw SMART values in decimal + hex, topology path). |
| `-q, --quiet` | No normal output; use the exit code. |
| `--show-serial` | Include serial numbers. **Hidden/masked by default** (spec §4.2). |
| `--sector-size N` | Logical sector size for image files: 512 (default) or 4096. |
| `--helper SPEC` | Open devices through the privileged helper: `auto`, `none`, or a command such as `"sudo /usr/libexec/platter/platter-helper"`. Also `$PLATTER_HELPER`. See [privilege-separation.md](privilege-separation.md). |
| `-d, --device-type T` | smartctl device type, e.g. `sat`, `nvme`, `usbjmicron`, `usbcypress`. |
| `-h, --help`, `--version` | |

### Surface scan options

| Option | Meaning |
|---|---|
| `--start LBA`, `--end LBA` | Inclusive LBA range (default: whole device). |
| `--block-size SIZE` | Bytes per read request: `4096`, `64K`, `1M` (default `1M`). Must be a multiple of the sector size, at most 64 MiB. |
| `--order O` | `forward` (default), `reverse`, `random` (every block once, deterministic per seed), `sampled`. |
| `--sample PERCENT` | Share of blocks to read with `--order sampled` (default 10). Sampled blocks are read in ascending order. |
| `--seed N` | Seed for `random` / `sampled`. Same seed, same order, on every platform. |
| `--retries N` | Extra attempts for a block that fails (default 0, max 16). A device that disappears is never retried. |
| `--stop-after N` | Stop after N unreadable blocks (default: scan everything). |
| `--thresholds A,B,C,D` | Latency class bounds in milliseconds (default `5,20,50,200`). |
| `--buffered` | Read through the OS cache. By default platter uses direct I/O and falls back to buffered (with a warning) where the filesystem refuses it. |
| `--csv FILE` | Per-block CSV: block, first/last LBA, bytes, duration, MB/s, class, error, retries, time. |
| `--report FILE` | Human-readable report. |
| `--map` | ASCII latency map appended to the text output. |

With `--json` (and `-o FILE`) the canonical JSON scan file is produced: it can
be loaded back by platter for comparison. Latency classes describe how long
the device took to answer, **not** sector health.

Examples:

```sh
platter-cli surface scan disk.img --map
sudo platter-cli surface scan /dev/sdb --block-size 256K --csv sdb.csv --json -o sdb-scan.json
platter-cli surface scan 1 --order sampled --sample 5       # Windows: PhysicalDrive1, elevated prompt
```

### Benchmark options

| Option | Meaning |
|---|---|
| `--test T` | `seq`, `random`, `access` (single-sector random reads, QD 1), `curve` (read speed at evenly spaced positions), `sizes` (sequential at several block sizes), `qd` (random at several queue depths). |
| `--block-size SIZE` | Request size. Defaults: `seq`/`curve`/`sizes` 1M, `random`/`qd` 4K, `access` one sector. |
| `--qd N` | Queue depth 1-64. Implemented as N threads, each with its own device handle, issuing synchronous reads; with direct I/O that keeps N requests outstanding at the device. |
| `--duration SEC` | Time limit per phase (defaults: seq 30, random 10, access 5, sweeps 3 per step). |
| `--size SIZE` | Byte limit per phase (`256M`, `4G`; defaults: seq 1G, sizes 256M per step). |
| `--points N`, `--point-size SIZE` | Speed curve sampling (default 100 points x 16M). |
| `--sizes LIST`, `--qds LIST` | Steps for the sweeps, e.g. `--sizes 4K,64K,1M,4M`, `--qds 1,4,16,32`. |
| `--start LBA`, `--end LBA` | Test region. |
| `--buffered` | Read through the OS cache (reported as a caveat). |
| `--chart` | ASCII chart: speed vs position, bars per step, or throughput over time. |
| `--report FILE` | Also write the text report. |

Throughput is bytes divided by wall-clock time; latency is per request. Write
benchmarks are not available yet (they need the destructive-operation
safeguards planned for v0.4).

### History

`--save` records a benchmark, surface scan or SMART query in a local SQLite
database (`--history-db FILE`, default `%APPDATA%\platter\history.db` or
`~/.local/share/platter/history.db`, or `$PLATTER_HISTORY_DB`). Nothing is saved
unless you ask. The database never contains a raw serial number: devices are
identified by hashes. A failed save prints a warning and does not change the
exit code.

```sh
sudo platter-cli benchmark read /dev/sdb --test curve --chart --save
platter-cli benchmark history sdb
platter-cli benchmark show 12 --chart
```

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Success (for `smart show`: health GOOD) |
| 1 | Generic failure |
| 2 | Usage error / invalid argument |
| 3 | Device or file not found |
| 4 | Permission denied (run elevated / as root) |
| 5 | Device busy |
| 6 | I/O error, short read, timeout or device removed |
| 7 | Operation unsupported for this target (e.g. SMART on an image) |
| 8 | smartctl missing or failed to run |
| 9 | `surface scan` / `benchmark read`: finished (or stopped) with unreadable blocks |
| 10 | `smart show`: health WARNING |
| 11 | `smart show`: health CRITICAL |
| 12 | `smart show`: health UNKNOWN |
| 13 | Cancelled by the user (Ctrl+C); partial results are still reported |
| 14 | Refused: blocked by the safety checks, no or wrong confirmation, or the device changed since the preview |
| 15 | Completed with errors: erase sectors that could not be written / verify, or imaging that replaced unreadable sectors by zeros |
| 16 | A checksum or read-back comparison failed (`image verify`, `image restore`, `clone`) |

## smartctl

platter does not send SMART commands itself; it runs smartmontools.
Lookup order: `$PLATTER_SMARTCTL`, `PATH`, then
`C:\Program Files\smartmontools\bin\smartctl.exe` (Windows) or
`/usr/sbin/smartctl` (Linux). Only the read-only `-a` query is ever issued. On Windows, `\\.\PhysicalDriveN` is passed to smartctl as `/dev/pdN`.

## Destructive commands

All of them print the preview (target, model, capacity, ranges, bytes
affected, warnings, blockers) to stderr, then need the typed confirmation:
`--confirm TEXT`, or an interactive prompt when stdin is a terminal. TEXT is
the model name or the last four characters of the serial (for an image: the
file name). `--dry-run` prints the preview and exits 0 without writing.
Mounted devices, the system disk, swap, read-only media and devices with
holders are refused (exit 14) and cannot be forced.

### `erase run <device>`

| Option | Meaning |
|---|---|
| `--method M` | `zero` (default), `one`, `random`, `random_n`, `dod3`, `dod7`, `pattern`, `discard`; see `erase methods` |
| `--passes N` | Random passes (1-99); turns `random` into `random_n` |
| `--pattern HEX` | Byte pattern for `pattern` (1-4096 bytes) |
| `--verify V` | `none`, `last` (default) or `every` |
| `--range F:C[,F:C]` or `--start/--end` | LBA ranges (default: whole device) |
| `--block-size SIZE` | Request size (default 1M) |
| `--confirm TEXT`, `--dry-run` | As above |
| `--report FILE`, `--save`, `--history-db FILE`, `--json` | Output and history |

The verdict is one of `ERASED AND VERIFIED`, `ERASED (NOT VERIFIED)`,
`COMPLETED WITH ERRORS` (exit 15) or `INCOMPLETE` (cancelled / failed).
The report ends with a SHA-256 digest of its body and a plain statement of the
limits of software overwrite on remapped blocks.

Discard (`--method discard`) issues TRIM / hole-punching (`BLKDISCARD` on
Linux block devices, `fallocate(PUNCH_HOLE)` on images); unsupported on
Windows. Hardware secure erase is not offered.

### Write scans and write benchmarks

`surface scan --mode write|write-readback|pattern` and
`benchmark read --test write-seq|write-random` (the subcommand keeps its name)
use the same options as their read counterparts, plus `--confirm`, `--dry-run`
and `--pattern`. Write benchmarks require an explicit `--start` and `--end`.

### Partition editing

Every edit rewrites the whole table (all copies, all EBRs) from an in-memory
plan, then flushes, reads each region back and compares. The confirmed ranges
are exactly the table areas (e.g. LBA 0-33 and the last 33 LBAs for GPT with
512-byte sectors), so nothing else on the disk can be touched. The planned
diff is printed before the confirmation.

| Command | Meaning |
|---|---|
| `partitions init <device> --table gpt\|mbr` | New empty table; existing partitions are forgotten (data is not erased) |
| `partitions add <device> [--size 20G\|max] [--offset 1M] [--type NAME\|GUID] [--name N] [--bootable] [--logical] [--extended] [--attributes N]` | Adds a partition in the first free 1 MiB-aligned space (`max` = fill the free span) |
| `partitions delete <device> N` | Removes entry N |
| `partitions set <device> N [--type T] [--name N] [--bootable\|--no-bootable] [--attributes N]` | Changes metadata only |
| `partitions resize <device> N --start LBA --end LBA` | New bounds. **The file system and its data are not moved or resized.** |

Edits that would make the table invalid (overlap, out of bounds, too many
primaries, ...) are refused before anything is written. After writing, platter
asks the OS to re-read the table (`BLKRRPART` / `IOCTL_DISK_UPDATE_PROPERTIES`);
if a partition is still in use that is reported as a note, not an error.
GPT partition number = table slot; MBR logicals are numbered from 5.

### `format <device> --fs fat32|exfat`

| Option | Meaning |
|---|---|
| `--fs fat32\|exfat` | Required. ext4, NTFS etc. are not implemented natively; use the platform tools. |
| `--partition N` | Partition to format. Without it only a device with no partition table (or an image) is accepted. |
| `--label TEXT` | FAT32: up to 11 characters (uppercased); exFAT: up to 11 characters |
| `--cluster-size SIZE` | Power of two (`4K`...`32M`); default is chosen from the volume size |
| `--confirm TEXT`, `--dry-run` | As above |

This is a **quick format**: only boot sectors, FATs / allocation bitmap, up-case
table and the root directory are written, inside the partition. The data area
is not erased, so old files stay recoverable until overwritten (use `erase run`
first if that matters). The boot sector is written last, a half-finished format
is therefore not recognizable as a volume, and the metadata is read back and
compared afterwards. FAT32 volumes need at least 65525 clusters (about 33 MiB
with 512-byte clusters).

### Imaging and cloning

```sh
platter-cli image create /dev/sdb backup.img.zst --save          # zstd, verified, backup.img.zst.sha256
platter-cli image create /dev/sdb rescue.img --recover --retries 3
platter-cli image verify backup.img.zst --against /dev/sdb
platter-cli image restore backup.img.zst /dev/sdc --expect-sha256 <hex>
platter-cli clone /dev/sdb /dev/sdc --dry-run
```

| Option | Meaning |
|---|---|
| `--compress none\|zstd`, `--level N` | Image compression (default: zstd when the name ends in `.zst`); level 1-19 |
| `--sparse` | Raw images only: all-zero blocks become holes |
| `--force` | Replace an existing output file (otherwise refused) |
| `--recover` | Continue past unreadable sectors: they are replaced by zeros and listed in the report (`image create`, `clone`) |
| `--retries N` | Extra read attempts per failing block or sector (default 2, max 16) |
| `--start`, `--end` | Source LBA range (`create`, `clone`); `verify --against`: where the image starts on the device |
| `--target-offset LBA` | `restore` / `clone`: first LBA written on the target (default 0) |
| `--no-verify` | Skip the read-back pass (`create`: re-reading the finished image; `restore` / `clone`: reading the target back) |
| `--expect-sha256 HEX`, `--use-sidecar` | `restore`: hash the image first and refuse to write anything unless it matches |
| `--sha256 HEX` | `verify`: expected checksum (default: `<file>.sha256` when present) |
| `--no-sidecar` | `create`: do not write `<file>.sha256` |
| `--block-size SIZE` | Copy block (default 1M) |
| `--report FILE`, `--save`, `--json` | Output and history (reports carry a SHA-256 of their own body) |

* The **checksum** is of the data stream (unreadable sectors count as zeros), not of the file. For a
  `.zst` image `zstd -dc image.zst | sha256sum` gives the same value; platter's images are ordinary
  single-frame zstd files that the `zstd` tool can read. Compressed images record their size, which
  restore and verify rely on.
* Images are written to `<file>.partial` and renamed only when complete; a cancelled or failed run
  never leaves something that looks like a finished image.
* A restore or clone refuses images/ranges that do not fit the confirmed range, devices with
  different sector sizes (clone), and a target that is the source. The confirmed range is exactly
  the sectors written: `restore` needs `ceil(image size / sector size)` sectors starting at
  `--target-offset`; `clone` needs the copied sector count starting at `--target-offset`.
* Exit codes: 15 when unreadable sectors were replaced by zeros, 16 when a checksum or read-back
  comparison failed.
* Not included: resuming an interrupted copy, reverse/trimming passes and a rescue log as in
  `ddrescue`, partition/file-system resizing when the target is larger.

### Inspecting sectors

```sh
platter-cli hex disk.img                      # sector 0, MBR decoded
platter-cli hex disk.img --lba 1 --as gpt-header
platter-cli find disk.img --ascii "NTFS" --limit 5
platter-cli find /dev/sdb --utf16 "Documents and Settings" --ignore-case   # read-only
```

Both commands only ever open the device read-only. A read error on a sector is shown as
an error for that sector and the command carries on.

### Qualification (`qualify run <device>`)

**Destructive**: it overwrites the *whole* device (or `--start/--end`) several times, so it
goes through the full typed-confirmation pipeline ([safety.md](safety.md)). Stages: N write
passes (`--passes 1..4`, default 2; random data from a seeded stream, the next pass writes
the complement), a read-back after each pass (`--no-verify` skips), a final read
(`--no-final-read` skips), `--samples N` random single-block reads (0 skips) and a SMART
comparison before/after (`--no-smart` skips; images have no SMART). `--block-size`,
`--slow-ms` tune the block size and what counts as a slow request. `--report FILE`, `--json`
and `--save` work as for erase. Exit codes: 0 pass (including with warnings), 15 completed
with errors, 16 verdict FAIL, 13 cancelled.

The verdict thresholds (slow-request fractions, tail-vs-head throughput, SMART deltas) are
engineering judgment, not a standard; the report lists every observation that contributed.
