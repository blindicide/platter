# Architecture

```
            +-------------+     +-------------+
            | platter-gui |     | platter-cli |
            |  (Qt 6)     |     |             |
            +------+------+     +------+------+
                   |                   |
                   +---------+---------+
                             |
                    +--------v--------+
                    | platter-reports |  shared JSON schema
                    +--------+--------+
               +-------------+--------------+
               |                            |
     +---------v----------+       +---------v--------+
     | platter-partitions |       |  platter-smart   |---> smartctl (external, --json)
     +---------+----------+       +---------+--------+
               |                            |
               +-------------+--------------+
                             |
                    +--------v--------+
                    |  platter-core   |  Error/Result, range checks, DeviceInfo,
                    |                 |  IBlockDevice, process runner
                    +--------+--------+
                             |
        +--------------------+--------------------+
        |                    |                    |
  posix_file_device    linux sysfs enum     win32 file device + enum
  (images + Linux       (/sys, /proc,       (CreateFileW, DeviceIoControl,
   block devices)        /dev/disk/by-id)    zero-access PhysicalDriveN)
```

## Rules

- **Core never includes Qt.** The GUI is optional and only consumes core APIs.
- **No exceptions across library boundaries.** Functions return
  `Result<T>` / `Status` carrying an `Errc` category, a message and the raw OS
  error. The CLI maps `Errc` to exit codes; the GUI to log entries.
- **Backends behind one interface.** `IBlockDevice` (read/write/flush,
  alignment, timing per call) is implemented by `PosixFileDevice` and
  `Win32FileDevice`. Images and physical devices share the implementation, so
  image-based tests exercise the same code paths.
- **Injectable OS state.** Linux enumeration takes a `sysfs::Roots` struct,
  so tests can point it at a fake tree.
- **GUI threading.** Anything that touches a device runs on a worker
  `QThread`; results are posted back with `QMetaObject::invokeMethod` and
  matched to the device by path, then dropped if the device is gone. The
  common operation engine (states CREATED…CANCELLED, progress, cancellation)
  arrives with surface scans in v0.2 and will replace these ad-hoc workers.

## Planned modules

| Module | Milestone |
|---|---|
| operation engine (`src/core/operations`) | v0.2 |
| surface scan (`src/surface`) | v0.2 |
| benchmark (`src/benchmark`) | v0.3 |
| safety gate (`core/safety`), erase (`src/erase`), write scans / write benchmarks | v0.4 (done) |
| partition editing, filesystem creation | v0.5 |
| imaging / cloning, zstd (`src/imaging`) | v0.6 |
| hex inspector, qualification, SQLite history (`src/storage`) | v0.7 |
| privileged helper (Linux), privilege separation | v0.8 |
