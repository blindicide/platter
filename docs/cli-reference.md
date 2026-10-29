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
| `benchmark history [<device>]` | Lists saved benchmark sessions (newest first), optionally for one device. | No |
| `benchmark show <id>` | Re-prints a saved session (text, or JSON with `--json`; `--chart` works too). | No |
| `smart show <device>` | Runs `smartctl --json=c -a` and prints attributes / NVMe health log plus a conservative assessment with every contributing observation. | Usually yes |
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
| 15 | Erase completed with errors (sectors that could not be written or did not verify) |

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
