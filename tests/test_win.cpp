// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Windows smoke test. NOTE: there is no clean way to redirect the recycle bin,
// so this touches the *real* recycle bin and is labelled "touches_real_trash"
// in CTest. It verifies that trashing succeeds and the source path is gone; it
// deliberately does not try to locate/restore the item from the bin.

#include "librecycle/recycle.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

int main()
{
    int failures = 0;
    auto check = [&](bool cond, char const* what) {
        std::fprintf(stderr, "%s - %s\n", cond ? "ok  " : "FAIL", what);
        if (!cond)
        {
            ++failures;
        }
    };

    fs::path const victim = fs::temp_directory_path() / "librecycle_win_smoke.txt";
    {
        std::ofstream(victim) << "data";
    }

    std::error_code ec;
    bool const ok = librecycle::recycle(victim.string(), ec);
    check(ok && !ec, "recycle returns success");
    check(!fs::exists(victim), "original file is gone");

    std::error_code missing;
    check(!librecycle::recycle((fs::temp_directory_path() / "nope-xyz").string(), missing), "missing path fails");
    check(missing == librecycle::errc::not_found, "missing path -> errc::not_found");

    std::error_code empty;
    check(
        !librecycle::recycle("", empty) && empty == librecycle::errc::invalid_argument,
        "empty path -> errc::invalid_argument");

    return failures == 0 ? 0 : 1;
}
