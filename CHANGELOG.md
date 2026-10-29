# Changelog

platter follows semantic versioning from 1.0. Before 1.0 each milestone was a minor version.

## 1.0.0

Feature-complete release candidate. **Not yet validated on physical hardware** (see
`docs/release-checklist.md`); every destructive feature has only run against image files and test doubles.

- Packaging: `cmake --install` and CPack (tar.gz / zip), polkit policy and desktop entry for Linux,
  documentation installed to `share/doc/platter`.
- No functional changes since 0.9.0.

## 0.9.0

- Settings > Preferences (history, look, toolbar/status bar, helper at startup, log file, history
  database path, reset); built-in offline help (F1); live switch between the classic and platform look.

## 0.8.0

- Privilege separation on Linux: `platter-helper` serves one unprivileged client over a socket pair
  and enforces the confirmed write ranges. CLI `--helper`, GUI File > Use Privileged Helper.

## 0.7.0

- Hex sector viewer and searcher with structure decoding (MBR, GPT, FAT, exFAT, NTFS, ext).
- Drive qualification (burn-in) with verdict, report and history.
- History browser (GUI and `platter-cli history`), checksum calculator.

## 0.6.0

- Disk imaging (raw / zstd, SHA-256), recovery copies, image restore, device cloning, image verification.

## 0.5.0

- GPT/MBR partition editing and native FAT32 / exFAT quick format.

## 0.4.0

- Safety gate (preview, typed confirmation, identity re-check), overwrite erase with verification,
  write surface scans and write benchmarks.

## 0.3.0

- Benchmarks (sequential, random, access time, speed curve, block-size and queue-depth sweeps) and
  the SQLite history database.

## 0.2.0

- Surface scans with latency maps and exports.

## 0.1.0

- Device discovery (Linux, Windows), image files, MBR/GPT inspection, SMART via smartmontools, CLI and GUI shell.
