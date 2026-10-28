# Safety model

platter will eventually be able to destroy data. These rules exist so it only
ever destroys the data the user meant, and they apply from v0.1 on, even
though v0.1 itself never writes.

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

## Destructive pipeline (v0.4+, design)

Before any write to a physical device:

1. Explicit target selection by the user (GUI) or `--target <stable-id>` (CLI).
2. Capability check: the operation must be in the device's capability set.
3. Preview: model, capacity, serial (unmasked for this purpose), range, method.
4. In-use check: mounted / swap / holders / system disk ⇒ refuse.
5. Typed confirmation for whole-device operations (type the model or the
   last 4 characters of the serial).
6. Exclusive open (`O_EXCL` on Linux; lock + dismount every volume on
   Windows).
7. **Re-validate identity immediately before the first write**, using the
   handle that will do the writing.
8. Bounds-checked I/O with cooperative cancellation; report exactly what was
   written and verified.

No erase may start automatically at startup, from a saved session, or from
anything but a fresh, explicit user action.

## Testing rules

- Unit and integration tests use sparse image files in temp directories.
  They must never open `/dev/*` or `\\.\PhysicalDrive*`.
- Linux enumeration is tested against a fake sysfs tree built per test.
- Destructive features are not production-ready until validated on
  dedicated disposable hardware (spec §24), with every other disk unplugged.
