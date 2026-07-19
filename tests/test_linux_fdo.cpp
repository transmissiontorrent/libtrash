// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Linux/*BSD FDO trash tests. Self-contained (no GoogleTest dependency).
//
// The whole point of the FDO spec is that it is testable without touching the
// real desktop trash: we redirect HOME and XDG_DATA_HOME at a temp sandbox and
// inspect Trash/files and Trash/info directly.

#include "libtrash/trash.hpp"

#include "../src/trash_nix_detail.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#include <sys/stat.h>
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

    // 1b) Non-ASCII (UTF-8) name is percent-encoded byte-for-byte, independent
    //     of the process locale (é == 0xC3 0xA9 -> %C3%A9).
    fs::path const uni_victim = sandbox / "caf\xC3\xA9.txt";
    {
        std::ofstream(uni_victim) << "u";
    }
    std::error_code ec_uni;
    bool const ok_uni = libtrash::trash(uni_victim.string(), ec_uni);
    CHECK(ok_uni && !ec_uni, "trashing a UTF-8-named file succeeds");
    CHECK(fs::exists(files_dir / "caf\xC3\xA9.txt"), "UTF-8-named file moved into Trash/files");
    {
        std::string const uni_info = read_all(info_dir / "caf\xC3\xA9.txt.trashinfo");
        CHECK(uni_info.find("caf%C3%A9.txt") != std::string::npos, "Path encodes high bytes as %C3%A9");
    }

    // 2) Collision: trashing a second file of the same name must not clobber.
    {
        std::ofstream(victim) << "data2";
    }
    std::error_code ec2;
    bool const ok2 = libtrash::trash(victim.string(), ec2);
    CHECK(ok2 && !ec2, "second trash of same name returns success");
    CHECK(fs::exists(files_dir / "hello world.txt"), "first collision entry still present");
    CHECK(fs::exists(files_dir / "hello world_2.txt"), "second entry uses name_2.ext form");
    CHECK(read_all(files_dir / "hello world.txt") == "data", "first entry's contents intact");
    CHECK(read_all(files_dir / "hello world_2.txt") == "data2", "second entry's contents not clobbered");
    CHECK(fs::exists(info_dir / "hello world.txt.trashinfo"), "first .trashinfo present");
    CHECK(fs::exists(info_dir / "hello world_2.txt.trashinfo"), "second .trashinfo present");

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

    // 3b) Trashing a symlink moves the link itself, not its target.
    fs::path const link_target = sandbox / "link_target.txt";
    {
        std::ofstream(link_target) << "keep me";
    }
    fs::path const the_link = sandbox / "the_link";
    fs::create_symlink(link_target, the_link);
    std::error_code ec_link;
    bool const ok_link = libtrash::trash(the_link.string(), ec_link);
    CHECK(ok_link && !ec_link, "trashing a symlink succeeds");
    CHECK(!fs::exists(fs::symlink_status(the_link)), "the symlink itself is gone from its original location");
    CHECK(fs::exists(link_target), "the symlink's target is left untouched");
    CHECK(
        fs::is_symlink(fs::symlink_status(files_dir / "the_link")),
        "the trashed entry is the symlink itself, not a copy of its target");

    // 3c) A ".." after a symlinked directory must resolve THROUGH the symlink to
    // the real location, not collapse lexically to a different (wrong) file.
    fs::create_directories(sandbox / "real_sub" / "inner");
    {
        std::ofstream(sandbox / "real_sub" / "victim2.txt") << "the real one";
    }
    {
        std::ofstream(sandbox / "victim2.txt") << "decoy - must survive";
    }
    fs::create_directory_symlink(sandbox / "real_sub" / "inner", sandbox / "inner_link");
    std::error_code ec_dd;
    // "inner_link/../victim2.txt" == real_sub/victim2.txt (via the symlink),
    // NOT the lexical sandbox/victim2.txt.
    bool const ok_dd = libtrash::trash((sandbox / "inner_link" / ".." / "victim2.txt").string(), ec_dd);
    CHECK(ok_dd && !ec_dd, "trashing via symlink+.. succeeds");
    CHECK(!fs::exists(sandbox / "real_sub" / "victim2.txt"), "the intended (real_sub) file was trashed");
    CHECK(fs::exists(sandbox / "victim2.txt"), "the lexical-collision decoy was NOT touched");

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

    // 6) Security guard: the per-user top-dir trash must not follow a planted
    //    symlink or reuse another user's directory (shared-mount hijack).
    {
        fs::path const guard_root = sandbox / "guard";
        fs::create_directories(guard_root);
        uid_t const me = ::getuid();

        // (a) absent -> created as a private (0700) directory we own.
        std::string const fresh = (guard_root / ".Trash-me").string();
        CHECK(libtrash::detail::make_or_verify_owned_dir(fresh, me), "guard: creates a missing trash dir");
        struct stat st = {};
        CHECK(
            ::lstat(fresh.c_str(), &st) == 0 && S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode),
            "guard: created entry is a real directory");
        CHECK((st.st_mode & 0777) == 0700, "guard: created dir is private (0700)");

        // (b) idempotent on our own existing dir.
        CHECK(libtrash::detail::make_or_verify_owned_dir(fresh, me), "guard: accepts our own existing dir");

        // (c) a planted symlink is refused and NOT followed.
        fs::path const attacker = guard_root / "attacker_target";
        fs::create_directories(attacker);
        std::string const planted = (guard_root / ".Trash-evil").string();
        fs::create_symlink(attacker, planted);
        CHECK(!libtrash::detail::make_or_verify_owned_dir(planted, me), "guard: refuses a planted symlink");
        CHECK(fs::is_empty(attacker), "guard: nothing was written through the symlink");
    }

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
