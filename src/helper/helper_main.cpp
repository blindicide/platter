// SPDX-License-Identifier: LicenseRef-Proprietary
//
// platter-helper: the privileged half of platter. Started by the user through
// pkexec / sudo (or from a root shell) with --stdio; stdin/stdout are a
// connection to exactly one client. See docs/privilege-separation.md.
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "platter/helper/helper.hpp"
#include "platter/version.hpp"

namespace {
void usage(FILE* f) {
    std::fprintf(f,
                 "platter-helper %s\n"
                 "Privileged helper for platter. It is started by the platter GUI/CLI, not by hand:\n"
                 "  platter-helper --stdio [--read-only]\n"
                 "    --stdio      serve one client on stdin/stdout (required)\n"
                 "    --read-only  refuse every read-write open\n"
                 "    --allow-regular-files   TESTING ONLY: also serve image files\n",
                 platter::kVersion);
}
}  // namespace

int main(int argc, char** argv) {
    bool stdio = false;
    platter::helper::ServerPolicy policy;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--stdio") stdio = true;
        else if (a == "--read-only") policy.read_only = true;
        else if (a == "--allow-regular-files") policy.allow_regular_files = true;
        else if (a == "--help" || a == "-h") { usage(stdout); return 0; }
        else if (a == "--version") { std::puts(platter::kVersion); return 0; }
        else { std::fprintf(stderr, "platter-helper: unknown option '%s'\n", a.c_str()); usage(stderr); return 2; }
    }
    if (!stdio) {
        usage(stderr);
        return 2;
    }
    ::signal(SIGPIPE, SIG_IGN);
    ::umask(077);
#ifdef __linux__
    ::prctl(PR_SET_DUMPABLE, 0);
    ::prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
#endif
    const rlimit no_core{0, 0};
    ::setrlimit(RLIMIT_CORE, &no_core);
    return platter::helper::serve(0, policy);
}
