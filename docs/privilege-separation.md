# Privilege separation (v0.8, Linux/POSIX)

Reading raw sectors of a physical disk normally needs root. Running the whole GUI as root is a
large attack surface and a bad habit, so platter can instead run a **small helper** as root and
keep everything else (Qt, report parsing, SQLite, JSON, zstd, …) unprivileged:

```
platter-gui / platter-cli  (you)  --socketpair-->  platter-helper  (root, via pkexec or sudo)
```

Windows keeps the single-process model: start platter elevated (devices are opened through
`\\.\PhysicalDriveN`, which requires an elevated token). The helper is POSIX-only.

## Using it

| Where | How |
|---|---|
| GUI | File > **Use Privileged Helper** (greyed out when already root), or `platter-gui --helper auto` |
| CLI | `platter-cli --helper auto …`, or set `PLATTER_HELPER=auto` |
| Custom | `--helper "sudo /opt/platter/platter-helper"` (`--stdio` is appended) |

`auto` finds `platter-helper` next to the running program (or under `/usr/libexec/platter`,
`/usr/lib/platter`, `/usr/local/bin`, …) and starts it with `pkexec` (preferred) or `sudo`. When
platter already runs as root `auto` does nothing. The helper is started **lazily**, on the first
device open, so a command that only touches image files never asks for a password.

While the helper is on, every `open_device()` goes through it: surface scans, benchmarks, SMART
sector reads, the hex viewer, erase, partition edits, imaging. Enumeration and `smartctl` still run
as you (sysfs is world-readable; `smartctl` may need its own elevation, as before).

## What the helper will and will not do

The helper is the security boundary, so it does not trust the client:

- **Only whole-disk block devices under `/dev`** (after resolving symlinks). Regular files, partitions,
  character devices, and anything else are refused.
- **No read-write open without confirmed ranges.** The client's safety pipeline
  (`docs/safety.md`) passes the byte ranges you confirmed; the helper keeps them and refuses any
  write, or discard, that is not completely inside one of them. A compromised GUI therefore cannot
  scribble over an arbitrary part of a disk, only over the span that was confirmed for it.
- **No read-write open of a device that is in use** (mounted, held by dm/LVM/md, or the system disk),
  re-derived by the helper from sysfs, independent of what the client claims.
- A range beyond the end of the device, an empty range, or an overflowing range is refused.
- Requests are bounded (16 MiB per I/O, 64 open devices, 4096 ranges per open), every message is
  length-checked, and a malformed request ends the session.
- `--read-only` makes the helper refuse every read-write open (useful for a diagnostics-only setup).
- It sets `PR_SET_NO_NEW_PRIVS`, `PR_SET_DUMPABLE=0`, disables core dumps, `umask 077`, and logs every
  read-write open and every refused write to stderr.

There is **no listening socket**: the helper serves exactly one client over the socket pair that
started it, so no other process can connect to it. When the client exits or hangs up, the helper exits.

What it deliberately does **not** do: it does not authenticate *you* (pkexec/sudo does that), and it
cannot check that the typed confirmation really happened, because that happens in the unprivileged
process. The separation limits the damage of a bug or compromise in the large unprivileged program
to "can write inside the range that was confirmed for it", which is a lot less than "can write
anywhere". Treat it as defence in depth, not as a replacement for the confirmation dialog.

## Performance and limits

All I/O of one session shares a single connection and is serialised. Sequential transfers are
fine (a copy through a Unix socket is far faster than a disk), but benchmarks that deliberately
issue several requests at once (queue-depth sweeps) are **not meaningful through the helper**: the
requests are queued one after another. Run those as root directly. Latency figures exclude the
socket round trip (the helper times the actual I/O), but cached/OS-level effects are the helper's.

## Installing

`platter-helper` installs to `libexec/platter/`. For a proper pkexec prompt install
`packaging/linux/org.platter.helper.policy` to `/usr/share/polkit-1/actions/` (the
`org.freedesktop.policykit.exec.path` annotation must match the installed path). Without the policy,
pkexec still works and shows a generic prompt.

## Testing

`tests/unit/test_helper.cpp` starts the real `platter-helper` binary as the current user against
temporary image files (`--allow-regular-files`, a switch that exists only for tests) and checks
reads, chunking, range enforcement, refusal rules, concurrent clients, a killed helper, and hostile
messages. Nothing in the suite opens a real device. The `authorize()` → helper path for a real
block device (including the byte ranges it passes) has not been exercised: that needs a disposable
disk, see HANDOFF.md.
