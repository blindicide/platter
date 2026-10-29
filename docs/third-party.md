# Third-party components

Platter's proprietary license applies only to its own code.

- Qt 6: LGPL v3, dynamically linked. License texts accompany the distribution.
  Source and build instructions: https://www.qt.io/download-open-source
  Users may replace the Qt shared libraries with compatible modified versions;
  reverse engineering for debugging such modifications is permitted by the LGPL.
- nlohmann/json: MIT, https://github.com/nlohmann/json (license in its source).
- SQLite: public domain, https://sqlite.org/copyright.html
- zstd: BSD license, https://github.com/facebook/zstd/blob/dev/LICENSE
- Catch2: BSL-1.0, used only for tests, https://github.com/catchorg/Catch2
- smartmontools: GPL v2 or later, separate executable. See smartmontools/README.txt
  and COPYING.txt in the Windows archive. Source: https://www.smartmontools.org/

Linux builds require compatible system Qt 6 and zstd shared libraries; they are
not bundled. Windows Qt DLLs remain separate replaceable files.
