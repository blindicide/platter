# Release checklist

platter 1.1.0 is feature-complete, but a public release should wait for the items below. Everything
destructive has so far been exercised only against image files, fault-injecting test doubles and
temporary files.

## Must decide

- [ ] **License.** Still "to be decided by the author". Source files carry `SPDX-License-Identifier: TBD`.
  Pick one, add `LICENSE`, replace the SPDX tags, update the README and the About > License dialog.

## Hardware validation (disposable drives only, everything else unplugged)

Read-only first, on any drive:
- [ ] `devices list/info`, `partitions list`, `smart show` match `lsblk` / `smartctl` / Disk Management.
- [ ] Surface scan and benchmark on a real HDD, an SSD and a USB stick; sanity-check speeds against `hdparm -t` / `fio`.
- [ ] Hex viewer: sector 0 of a real disk matches `xxd`/`dd` output.

Destructive, on a disposable HDD and a USB stick:
- [ ] Erase (zero, random, multi-pass) with verification; confirm with an independent tool (`cmp` against `/dev/zero`).
- [ ] Partition edit + FAT32/exFAT format; the result mounts and passes `fsck.vfat` / `fsck.exfat` and, on Windows, `chkdsk`.
- [ ] Image create / restore / clone round trip with SHA-256 comparison.
- [ ] Write scan / write benchmark / qualification; check the qualification thresholds against a drive you know is healthy and one you know is not.
- [ ] The privileged helper: enable it with pkexec, run a read scan and one destructive operation on a USB stick,
  check the helper's stderr shows the granted range and no refusals.
- [ ] Removal during an operation (unplug the USB stick) is reported cleanly.

Windows (nothing below the compile and CI-smoke level has been run):
- [ ] Everything above on Windows 10 and 11, as Administrator: enumeration, SMART (smartctl.exe), sector reads,
  locking/dismounting volumes before writes, `\\.\PhysicalDriveN` direct I/O alignment.
- [ ] Windows installer / zip contents and a clean-machine start (Qt DLLs via `windeployqt`).

## Housekeeping

- [ ] CI green on the release commit (Linux, Windows MSVC, MinGW cross).
- [ ] `CHANGELOG.md` and the version in `CMakeLists.txt` agree.
- [ ] `cmake --install` into a staging directory and a CPack package: helper at `libexec/platter/platter-helper`, policy path matches.
- [ ] Tag `vX.Y.Z` only after the above; attach the CI artifacts.
