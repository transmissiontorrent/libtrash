// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// librecycle - cross-platform "move a file/directory to the trash" library.
//
// Single public entry point: librecycle::recycle(). Works for files and
// directories. No glib/Qt/GTK dependency.
//
//   - Linux/*BSD : FreeDesktop.org Trash specification v1.0
//   - Windows    : IFileOperation (FOFX_RECYCLEONDELETE)
//   - macOS      : -[NSFileManager trashItemAtURL:resultingItemURL:error:]
//
// Error handling mirrors <filesystem>: a throwing overload and a non-throwing
// overload taking std::error_code&. Failures use the librecycle::errc enum,
// which plugs into std::error_code via the standard make_error_code
// customization.

#ifndef LIBRECYCLE_RECYCLE_HPP
#define LIBRECYCLE_RECYCLE_HPP

#include <string_view>
#include <system_error>

namespace librecycle
{

// Portable failure reasons. Values are stable; 0 is reserved for "no error".
enum class errc
{
    invalid_argument = 1, // empty path, embedded NUL, or invalid encoding
    not_found, // the path does not exist
    permission_denied, // could not create trash dirs or move the item
    cross_device, // no usable trash directory on the item's filesystem
    platform_error, // underlying OS API failure
    unsupported, // no backend for this platform
};

// The error_category for librecycle::errc.
std::error_category const& error_category() noexcept;

// Enables std::error_code{librecycle::errc::...} and ec == errc::... checks.
std::error_code make_error_code(errc e) noexcept;

// Move a file or directory (and its contents) to the OS trash / recycle bin.
// `utf8_path` must be UTF-8 encoded. Returns true on success; on failure returns
// false and sets `ec`.
//
// Trashing is inherently recursive at the OS level (a directory and all of its
// contents move as a single unit), so there is no separate "recycle_all": one
// recycle() handles both files and directories.
//
// NOTE: do not change the process working directory concurrently while calling
// this in a multithreaded application.
bool recycle(std::string_view utf8_path, std::error_code& ec) noexcept;

// Throwing overload. Throws std::filesystem::filesystem_error on failure, with
// the offending path and a librecycle::errc error_code attached.
void recycle(std::string_view utf8_path);

} // namespace librecycle

template <>
struct std::is_error_code_enum<librecycle::errc> : std::true_type
{
};

#endif // LIBRECYCLE_RECYCLE_HPP
