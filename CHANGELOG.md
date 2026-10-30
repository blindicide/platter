# Changelog

platter follows semantic versioning from 1.0. Before 1.0 each milestone was a minor version.

## 1.1.3

- Refresh project status and handoff documentation to reflect shipped features, dependencies and the license decision.
- Derive the Windows release archive version from CMake instead of hard-coding it in CI.
- Keep the Windows portable package tidy: GUI and CLI launchers at the top level, runtime files in `app/` and documentation in `docs/`.
- Exclude dependency development headers and libraries from release archives.
- Add a disk-and-head application logo, Windows executable icons and version information, and a Linux desktop icon.
- Show the author's copyright notice in English and Russian in About and License Information.
- Load Qt's bundled Russian catalogue so standard dialogs and buttons translate on clean Windows installations.
- Fix an invalid XML comment in the installed Linux polkit policy.
- Include the GUI completion-signal connection fix made after the 1.1.2 release preparation.

## 1.1.2

- Reject overflowing erase LBA values instead of interpreting failed conversions as zero.
- Resynchronize Erase, Surface and Benchmark targets after an operation finishes, and reset
  LBA ranges when switching devices or sector sizes.
- Add a SMART device-type selector in the GUI (Auto, SAT variants, SCSI, JMicron).
- Bound Windows process output collection so inherited output handles cannot defeat the timeout.
- Platter's own code is proprietary, all rights reserved; third-party licenses remain unchanged.
- Hardware validation has mostly succeeded according to the owner. The reported Windows USB 3.0
  to SATA SMART hang still needs confirmation with this build; adapter compatibility is not guaranteed.

## 1.1.0

- Mounted volumes on Windows no longer hard-refuse a destructive operation: the confirmation dialog
  shows them and needs an extra tick ("I understand ... will be locked and dismounted"); the CLI needs
  `--dismount-volumes`. The Windows backend already locks and dismounts the disk's volumes and aborts
  if one is in use. On Linux, and for the system disk and encryption/RAID holders, it stays a refusal.
- Erase, Surface and Benchmark tabs have a "Partition..." button that fills Start/End LBA from a
  partition (or the whole device). It uses the partition table already read in the Partitions tab
  and reads nothing from the device itself.
- The Windows package bundles smartctl (smartmontools, GPL-2.0+, unmodified, run as a separate process)
  in `smartmontools\`; platter looks there before PATH and the default install directory.

- **Russian locale.** The GUI is English by default; pick Russian in Settings > Preferences > Language
  (applies after a restart) or per run with `platter-gui --lang ru`. There is no system-locale
  auto-detection. All GUI text, the confirmation dialog, help topics and the common messages produced
  by the core libraries are translated (`translations/platter_ru.ts`).
- The CLI, JSON/CSV exports and text reports stay English on purpose (stable, scriptable output).
  Text report panels, the plan diff text, stored history headlines, raw OS / smartctl messages and
  CHS notation are not translated.
- The Russian translation has not been proofread by a native speaker.
- Build: optional Qt LinguistTools (`qt6-tools-dev`, `qt6-l10n-tools`) compiles the `.ts` into the
  executable; without it the GUI builds English-only. New ctest cases: `translations`, `platter-i18n-tests`.
- Windows packaging deploys Qt's own Russian translations (`windeployqt --translations ru`).

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
