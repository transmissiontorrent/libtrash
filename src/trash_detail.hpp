// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Internal shared helpers (NOT part of the public API). Input validation and
// path resolution live here so that every platform backend accepts the same
// inputs and resolves them identically.

#ifndef LIBTRASH_TRASH_DETAIL_HPP
#define LIBTRASH_TRASH_DETAIL_HPP

#include "libtrash/trash.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace libtrash
{
namespace detail
{

// Strict UTF-8 validator. Rejects stray continuation bytes, truncated
// sequences, overlong encodings, UTF-16 surrogates, and code points above
// U+10FFFF. An empty string is considered valid here; the empty-path case is
// rejected separately by resolve_input().
inline bool is_valid_utf8(std::string_view s) noexcept
{
    std::size_t i = 0;
    std::size_t const n = s.size();
    while (i < n)
    {
        unsigned char const c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0; // number of continuation bytes expected
        unsigned int cp = 0;
        unsigned int lower = 0; // smallest non-overlong code point for this length

        if (c < 0x80)
        {
            ++i;
            continue;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            len = 1;
            cp = c & 0x1Fu;
            lower = 0x80;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            len = 2;
            cp = c & 0x0Fu;
            lower = 0x800;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            len = 3;
            cp = c & 0x07u;
            lower = 0x10000;
        }
        else
        {
            return false; // stray continuation byte (0x80-0xBF) or invalid lead (0xF8-0xFF)
        }

        if (i + len >= n)
        {
            return false; // truncated multibyte sequence
        }
        for (std::size_t k = 1; k <= len; ++k)
        {
            unsigned char const cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80)
            {
                return false; // expected continuation byte
            }
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if (cp < lower || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu))
        {
            return false; // overlong, out of range, or surrogate
        }
        i += len + 1;
    }
    return true;
}

// Validate `utf8_path` and resolve it to an absolute path suitable for the OS
// backends. On success fills `out` and returns true; on failure sets `ec` and
// returns false.
//
// Resolution rule (identical on every platform):
//   * The input is interpreted as UTF-8 (on Windows a plain std::string would
//     otherwise be decoded with the ANSI code page).
//   * The path is made absolute, then symlinks and any '.'/'..' components in
//     the DIRECTORY part are resolved via std::filesystem::canonical.
//   * The FINAL component is left untouched, so a symlink is trashed as the
//     link itself rather than its target -- matching the FreeDesktop spec,
//     macOS -[NSFileManager trashItemAtURL:], and Windows IFileOperation.
//
// `ec` values: invalid_argument (empty, embedded NUL, or invalid UTF-8),
// not_found (the containing directory does not exist), permission_denied
// (a directory component is inaccessible), platform_error (anything else).
inline bool resolve_input(std::string_view utf8_path, std::filesystem::path& out, std::error_code& ec) noexcept
{
    namespace fs = std::filesystem;
    try
    {
        if (utf8_path.empty() || utf8_path.find('\0') != std::string_view::npos || !is_valid_utf8(utf8_path))
        {
            ec = make_error_code(errc::invalid_argument);
            return false;
        }

        // u8path interprets the bytes as UTF-8 on every platform (C++17).
        fs::path abs = fs::absolute(fs::u8path(std::string(utf8_path)), ec);
        if (ec)
        {
            ec = make_error_code(errc::platform_error);
            return false;
        }

        // A trailing separator yields an empty filename; treat "/a/b/" as "/a/b"
        // so the final component is a real name we can keep un-followed.
        if (!abs.has_filename() && abs.has_relative_path())
        {
            abs = abs.parent_path();
        }

        std::error_code fsec;
        fs::path const parent = fs::canonical(abs.parent_path(), fsec);
        if (fsec)
        {
            // The containing directory is missing or unreadable, so the item
            // cannot be there either.
            ec = make_error_code(fsec == std::errc::permission_denied ? errc::permission_denied : errc::not_found);
            return false;
        }

        out = (parent / abs.filename()).lexically_normal();

        // lexically_normal leaves a trailing separator when the final component
        // was '.'/'..'; drop it (but keep a bare root path).
        if (!out.has_filename() && out.has_relative_path())
        {
            out = out.parent_path();
        }

        ec.clear();
        return true;
    }
    catch (...)
    {
        ec = make_error_code(errc::platform_error);
        return false;
    }
}

} // namespace detail
} // namespace libtrash

#endif // LIBTRASH_TRASH_DETAIL_HPP
