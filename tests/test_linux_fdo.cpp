// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Linux/*BSD FDO trash tests. Self-contained (no GoogleTest dependency).
//
// The whole point of the FDO spec is that it is testable without touching the
// real desktop trash: we redirect HOME and XDG_DATA_HOME at a temp sandbox and
// inspect Trash/files and Trash/info directly.

#include "libtrash/trash.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <unistd.h>

namespace fs = std::filesystem;

namespace
{
int g_failures = 0;

void check(bool cond, char const* what, char const* file, int line)
{
    if (cond)
    {
        std::fprintf(stderr, "ok   - %s\n", what);
    }
    else
    {
        std::fprintf(stderr, "FAIL - %s (%s:%d)\n", what, file, line);
        ++g_failures;
    }
}

#define CHECK(cond, what) check((cond), (what), __FILE__, __LINE__)

std::string read_all(fs::path const& p)
{
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

std::size_t count_entries(fs::path const& dir)
{
    std::size_t n = 0;
    std::error_code ec;
    for (auto const& e : fs::directory_iterator(dir, ec))
    {
        (void)e;
        ++n;
    }
    return n;
}
} // namespace

int main()
{
    fs::path const sandbox = fs::temp_directory_path() / ("libtrash_test_" + std::to_string(::getpid()));
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);

    fs::path const xdg = sandbox / "xdg";
    fs::create_directories(xdg);
    ::setenv("HOME", sandbox.string().c_str(), 1);
    ::setenv("XDG_DATA_HOME", xdg.string().c_str(), 1);

    fs::path const files_dir = xdg / "Trash" / "files";
    fs::path const info_dir = xdg / "Trash" / "info";

    // 1) Basic file trashing (name has a space -> exercises percent-encoding).
    fs::path const victim = sandbox / "hello world.txt";
    {
        std::ofstream(victim) << "data";
    }
    std::error_code ec1;
    bool const ok1 = libtrash::trash(victim.string(), ec1);
    CHECK(ok1 && !ec1, "trash returns success");
    CHECK(!fs::exists(victim), "original file is gone");
    CHECK(fs::exists(files_dir / "hello world.txt"), "file moved into Trash/files");

    fs::path const info_file = info_dir / "hello world.txt.trashinfo";
    CHECK(fs::exists(info_file), ".trashinfo created");

    std::string const info = read_all(info_file);
    CHECK(info.rfind("[Trash Info]\n", 0) == 0, ".trashinfo starts with [Trash Info]");
    CHECK(info.find("/hello%20world.txt") != std::string::npos, "Path is percent-encoded (space -> %20)");
    CHECK(info.find("\nDeletionDate=") != std::string::npos, ".trashinfo has DeletionDate");

    // 2) Collision: trashing a second file of the same name must not clobber.
    {
        std::ofstream(victim) << "data2";
    }
    std::error_code ec2;
    bool const ok2 = libtrash::trash(victim.string(), ec2);
    CHECK(ok2 && !ec2, "second trash of same name returns success");
    CHECK(count_entries(files_dir) == 2, "two distinct entries after collision");
    CHECK(count_entries(info_dir) == 2, "two distinct .trashinfo files after collision");

    // 3) Trashing a directory (whole subtree moves in one rename).
    fs::path const dir_victim = sandbox / "subdir";
    fs::create_directories(dir_victim / "nested");
    {
        std::ofstream(dir_victim / "nested" / "f.txt") << "x";
    }
    std::error_code ec3;
    bool const ok3 = libtrash::trash(dir_victim.string(), ec3);
    CHECK(ok3 && !ec3, "directory trashing returns success");
    CHECK(!fs::exists(dir_victim), "original directory is gone");
    CHECK(fs::exists(files_dir / "subdir" / "nested" / "f.txt"), "directory contents preserved in trash");

    // 4) Error paths and std::error_code interop.
    std::error_code ec4;
    bool const ok4 = libtrash::trash((sandbox / "does-not-exist").string(), ec4);
    CHECK(!ok4 && ec4 == libtrash::errc::not_found, "missing path -> errc::not_found");
    CHECK(ec4.category() == libtrash::error_category(), "error uses libtrash category");
    CHECK(!ec4.message().empty(), "error has a message");

    std::error_code ec5;
    CHECK(
        !libtrash::trash("", ec5) && ec5 == libtrash::errc::invalid_argument,
        "empty path -> errc::invalid_argument");

    // 5) Throwing overload throws on failure, succeeds silently otherwise.
    bool threw = false;
    try
    {
        libtrash::trash((sandbox / "also-missing").string());
    }
    catch (fs::filesystem_error const&)
    {
        threw = true;
    }
    CHECK(threw, "throwing overload throws filesystem_error on failure");

    std::error_code rmec;
    fs::remove_all(sandbox, rmec);

    if (g_failures != 0)
    {
        std::fprintf(stderr, "\n%d check(s) failed\n", g_failures);
        return 1;
    }
    std::fprintf(stderr, "\nall checks passed\n");
    return 0;
}
