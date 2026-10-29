# Safety model

platter can destroy data (since v0.4). These rules exist so it only ever
destroys the data the user meant. They applied from v0.1 on, before any
write path existed.

## Layers

1. **Read-only by default.** `OpenOptions::mode` defaults to `read_only`. A
   device opened read-only returns `Errc::read_only` from `write()`, and that
   is enforced in the backend, not the UI.
2. **Every I/O is bounds-checked.** `validate_io()` rejects requests whose
   buffer is too small, whose `offset + length` overflows 64 bits, that
   extend past capacity, or that are misaligned for direct I/O. LBA math uses
   `checked_add` / `checked_mul`.
3. **Discovery is metadata-only.** Linux enumeration reads sysfs and /proc
   only. Windows enumeration opens drives with `dwDesiredAccess = 0`, which
   permits property queries and nothing else.
4. **Identity is not a path.** `/dev/sdb` and `PhysicalDrive2` can point at a
   different disk after a replug. Identity = stable id (WWN / EUI-64 /
   by-id) + serial + model + capacity + sector geometry (spec §17.2). The GUI
   only carries cached results across a refresh when all of these match.
5. **Never silently re-target.** If the selected device disappears, the
   selection becomes empty; it never falls through to whatever now occupies
   the same slot. Worker results for a vanished device are discarded.
6. **In-use detection.** `DeviceInfo::in_use()` is true for mounted
   filesystems (including via dm-crypt / LVM / md holders), active swap, and
   the system disk. Destructive operations will refuse these.
7. **Conservative health.** SMART PASSED alone is reported as UNKNOWN, never
   GOOD. No failure-probability percentages. Every observation that
   contributed to the verdict is shown.
8. **No shell, no arbitrary commands.** External tools run through
   `run_process()` with argv passed verbatim. The SMART adapter only issues
   the read-only `-a` query.
9. **Privacy.** Serial numbers are masked on screen and omitted from exported
   reports unless the user explicitly opts in for that report.

## Destructive pipeline (v0.4, implemented in `core/safety.hpp`)

Nothing may write to a device except through a `safety::AuthorizedTarget`, and
the only way to obtain one is `safety::authorize()`:

```
Request --make_preview()--> Preview  --(user types the confirmation)--> authorize() --> AuthorizedTarget
```

1. **Request**: kind (`erase`, `write_test`, ...), target (device path or
   image file), LBA ranges (empty = whole device) and a one-line summary.
2. **Preview** (`make_preview`, pure given the described device): model,
   capacity, sector sizes, ranges, bytes affected, warnings, **blockers** and
   the exact confirmation the user must type. Blockers: mounted filesystem,
   dm/LVM/md holders, swap, system disk, read-only media, no write capability,
   zero capacity, invalid or overlapping ranges.
3. **Typed confirmation**: the model name or the last four characters of the
   serial for devices; the file name for images. Case-insensitive, whitespace
   trimmed. The GUI dialog never pre-fills it and keeps the button disabled
   until it matches; the CLI takes `--confirm TEXT` or prompts on a terminal,
   and refuses (exit 14) when neither is available.
4. **`authorize()`** re-describes the device from scratch, refuses on any
   blocker or a wrong confirmation, compares the fingerprint (stable id,
   serial, vendor, model, capacity, sector sizes, bus) with the one the user
   was shown, takes an **exclusive lease** (`DeviceLeaseRegistry`), opens the
   device read-write and exclusively (`O_EXCL` on Linux; on Windows every
   volume on the disk is locked and dismounted, and the open fails with
   "device busy" if any is in use), and **re-checks the identity of the handle
   that will do the writing**.
5. **`AuthorizedTarget`** is move-only. `write()` / `discard()` are refused
   (`Errc::refused`) outside the confirmed ranges; reads are allowed anywhere
   (verification, partition tables). It owns the lease until destroyed.
6. **Audit**: every authorization and refusal is reported to the sink
   installed with `set_audit_sink()`; records never contain serials.

Further rules for the engines on top (erase, write benchmarks, write scans):

- They accept an `AuthorizedTarget` and nothing else. The read-only
  constructors refuse write modes with `Errc::refused`.
- Write scans and write benchmarks validate that their whole region lies inside
  the confirmed ranges before they start.
- The CLI never offers a "yes to all" flag; `--dry-run` shows the preview and
  exits without writing.
- Reports state what was written and what was verified, and the limits of
  overwrite erasure (remapped / over-provisioned blocks on flash, SMR zones,
  reallocated sectors).
- **Hardware secure erase (ATA Secure Erase, NVMe Sanitize) is deliberately not
  implemented.** A bad implementation can brick a drive and cannot be tested
  without sacrificing hardware.

No destructive operation may start automatically at startup, from a saved
session, or from anything but a fresh, explicit user action.

## Partition edits and formats (v0.5)

`Kind::partition_edit` is confirmed for exactly `PartitionPlan::metadata_ranges()`
(the table areas, never partition data); `Kind::filesystem_create` for exactly
one range, the volume. `apply_plan()` and `mkfs::Format` refuse any other kind,
geometry mismatch or extra range, and the `AuthorizedTarget` itself rejects
writes outside the confirmed ranges, so a bug in the planner cannot reach other
sectors. Formatting only ever writes metadata; it never claims to erase data.
On Windows the device first locks and dismounts every volume that lives on the
disk (`FSCTL_LOCK_VOLUME`, `FSCTL_DISMOUNT_VOLUME`) and fails if one is busy.
The GUI partition manager stages edits in memory and goes through the same
typed-confirmation dialog as erase.

## Imaging and cloning (v0.6)

The *source* of an image or clone is only ever opened read-only (the operations reject a writable
handle). Restore uses `Kind::restore_image` and clone `Kind::clone_target`, each confirmed for
exactly one range: the sectors that will be written. `RestoreImage` and `CloneDevice` refuse any
other kind or range count, and the `AuthorizedTarget` rejects writes outside that range.
Both can verify by reading the target back; a mismatch is reported as `VERIFICATION FAILED`
(exit 16) with the differing ranges. A recovery copy never hides lost data: unreadable sectors
are zero-filled, counted, listed and flagged in the verdict. Image files are written to
`<name>.partial` and renamed on success.

## Qualification and inspection (v0.7)

Drive qualification is destructive and uses `Kind::qualification`, confirmed for exactly one
range (the span that will be written). `Qualification` refuses any other kind or range count.
Hex viewing and searching are read-only: they open devices `read_only`, and the GUI HEX tab
only reads when you press *Go* (selecting a device in the list never reads it). The history
browser and `history delete` only touch the local SQLite database.

## Testing rules

- Unit and integration tests use sparse image files in temp directories.
  They must never open `/dev/*` or `\\.\PhysicalDrive*`.
- Linux enumeration is tested against a fake sysfs tree built per test.
- Destructive code is tested only against temp image files and the
  `FaultyWriteDevice` double (`tests/support/write_support.hpp`), which injects
  write errors, dropped writes and device removal. Test code builds an
  `AuthorizedTarget` through `safety::TestAccess`, which is defined only in the
  test suite.
- Destructive features are not production-ready until validated on
  dedicated disposable hardware (spec §24), with every other disk unplugged.
  As of v0.4 that has not happened.
