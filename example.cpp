// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Minimal CLI: move each argument to the trash.
//   trash_example <path> [<path> ...]

#include "libtrash/trash.hpp"

#include <cstdio>
#include <system_error>

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <path> [<path> ...]\n", argv[0]);
        return 2;
    }

    int rc = 0;
    for (int i = 1; i < argc; ++i)
    {
        std::error_code ec;
        if (libtrash::trash(argv[i], ec))
        {
            std::printf("trashed: %s\n", argv[i]);
        }
        else
        {
            std::fprintf(stderr, "error: %s: %s\n", argv[i], ec.message().c_str());
            rc = 1;
        }
    }
    return rc;
}
