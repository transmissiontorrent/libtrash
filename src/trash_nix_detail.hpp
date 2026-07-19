// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Internal Linux/*BSD-only helpers (NOT part of the public API). Kept in a
// header so they can be unit-tested directly.

#ifndef LIBTRASH_TRASH_NIX_DETAIL_HPP
#define LIBTRASH_TRASH_NIX_DETAIL_HPP

#include <cerrno>
#include <string>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace libtrash
{
namespace detail
{

// Create `path` as a private directory (mode, default 0700) owned by `uid`, or,
// if it already exists, verify it is a real directory (NOT a symlink) owned by
// `uid`. Never follows a symlink at `path`.
//
// This guards the top-directory trash ($topdir/.Trash-$uid and
// $topdir/.Trash/$uid) on a writable shared mount: a co-user who pre-plants a
// symlink (or a directory they own) there to redirect our trashed files is
// rejected, because mkdir fails with EEXIST and the follow-up lstat sees the
// symlink / foreign owner. Returns true only when we can safely use `path`.
inline bool make_or_verify_owned_dir(std::string const& path, uid_t uid, mode_t mode = 0700) noexcept
{
    if (::mkdir(path.c_str(), mode) == 0)
    {
        return true; // freshly created by us
    }
    if (errno != EEXIST)
    {
        return false;
    }
    struct stat st = {};
    if (::lstat(path.c_str(), &st) != 0)
    {
        return false;
    }
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && st.st_uid == uid;
}

} // namespace detail
} // namespace libtrash

#endif // LIBTRASH_TRASH_NIX_DETAIL_HPP
