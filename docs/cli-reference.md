# platter-cli reference

`platter-cli` uses the same core libraries as the GUI. As of v0.1 **every
command is read-only**: nothing opens a device for writing.

```
platter-cli [options] <command> [arguments]
```

## Commands

| Command | What it does | Needs elevation? |
|---|---|---|
| `devices list` | Lists whole-disk block devices with model, capacity, bus, media, sector sizes and usage (mounted / system / held by dm, LVM, md). | No (Linux: sysfs; Windows: zero-access handles) |
| `devices info <device>` | Identification, geometry, usage and the capability matrix for one device or image. | No |
| `partitions list <device>` | Reads and validates the MBR/GPT. Reports CRC problems, backup-GPT fallback, overlaps, out-of-bounds entries and 1 MiB alignment. | Yes for physical devices (reads sectors) |
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
| 10 | `smart show`: health WARNING |
| 11 | `smart show`: health CRITICAL |
| 12 | `smart show`: health UNKNOWN |

## smartctl

platter does not send SMART commands itself; it runs smartmontools.
Lookup order: `$PLATTER_SMARTCTL`, `PATH`, then
`C:\Program Files\smartmontools\bin\smartctl.exe` (Windows) or
`/usr/sbin/smartctl` (Linux). Only the read-only `-a` query is ever issued in
v0.1. On Windows, `\\.\PhysicalDriveN` is passed to smartctl as `/dev/pdN`.

## Destructive commands (future)

From v0.4 on, destructive commands will require explicit target flags (e.g.
`--target <stable-id>`), re-validate device identity immediately before I/O,
and run the same safety checks as the GUI (see `docs/safety.md`).
