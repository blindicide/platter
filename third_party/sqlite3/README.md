# Vendored SQLite

- Version: **3.53.4** (`SQLITE_SOURCE_ID` 2026-07-24 19:02:57 bf7c7f30…)
- Files: `sqlite3.c`, `sqlite3.h` — the official single-file amalgamation.
- License: public domain (https://sqlite.org/copyright.html).

## Provenance

The SQLite project's download site was not reachable from the environment that
added these files, so the amalgamation was taken from the npm package
`better-sqlite3@13.0.3` (`deps/sqlite3/`). That package generates the file from
upstream SQLite source with SQLite's own `make sqlite3.c` (see its
`deps/download.sh`) and applies one patch (`deps/patches/1208.patch`), which
only replaces the expressions `30.0*86400.0` and `365.0*86400.0` with the
equivalent literals `2592000.0` and `31536000.0` in the date-modifier table
(a compiler workaround; values are identical).

SHA-256:

```
6a2805f8c1ef020a7086e62258519323cf98b219e9fe87a42065d916cfebaefc  sqlite3.c
919e7f2e8ed1d8f56ac17b412b8971c76aa5d1a879752cc6058f75e7d5910e1d  sqlite3.h
```

To update: replace both files with a newer amalgamation from
https://sqlite.org/download.html, update this note, and rerun the tests.

platter builds this copy only when `PLATTER_SYSTEM_SQLITE` is OFF or no system
SQLite 3 is found (Windows and MinGW builds always use it).
