# libtrash

A small, dependency-free C++17 library for moving files and directories to the
operating system's trash / recycle bin instead of permanently deleting them.

```cpp
#include <libtrash/trash.hpp>

std::error_code ec;
if (!libtrash::trash("path/to/file-or-dir", ec))
    std::fprintf(stderr, "%s\n", ec.message().c_str());

// ...or the throwing overload:
libtrash::trash("path/to/file-or-dir");   // throws std::filesystem::filesystem_error
```

- **One function**, two overloads (non-throwing + throwing) mirroring `<filesystem>`.
- **Files and directories**: a directory and its contents move as a single unit.
- **Never permanently deletes**: if an item can't be trashed, the call fails and leaves it in place.
- **No third-party dependencies**: only the platform SDK.

Backends: Linux/BSD implement the
[FreeDesktop.org Trash specification v1.0](https://specifications.freedesktop.org/trash-spec/trashspec-1.0.html)
directly (no glib/Qt/GTK); Windows uses `IFileOperation`; macOS uses
`-[NSFileManager trashItemAtURL:...]`.

## Error handling

Failures are reported through `libtrash::errc`, which integrates with
`std::error_code` (`libtrash::error_category()`):

| `libtrash::errc` | Meaning |
| --- | --- |
| `invalid_argument` | empty path, embedded NUL, or invalid UTF-8 |
| `not_found` | the path does not exist |
| `permission_denied` | could not create the trash directory or move the item |
| `cross_device` | the item's filesystem has no usable trash |
| `platform_error` | underlying OS API failure |
| `unsupported` | no backend for this platform |

```cpp
if (ec == libtrash::errc::not_found) { /* ... */ }
```

`not_found`, `invalid_argument`, and `permission_denied` behave the same on every
platform; the others are advisory and may vary by OS, so prefer the
success/failure result for control flow.

## Behavior details

- **Encoding.** Paths are UTF-8 on every platform. On Windows a
  `std::filesystem::path`'s narrow `.string()` is the local code page, *not*
  UTF-8:  convert accordingly before calling.
- **Relative paths** are resolved against the current working directory. Don't
  change the CWD from another thread during a call.
- **Symlinks** are trashed as the link itself, never their target; symlinks and
  `.`/`..` in the *directory* part of the path are resolved.

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Add `-DLIBTRASH_SHARED=ON` for a shared library (static by default).

## Use as a submodule

```cmake
add_subdirectory(third-party/libtrash)   # tests/example are skipped automatically
target_link_libraries(your_app PRIVATE libtrash::trash)
```

The Foundation framework (macOS) and `ole32` / `shell32` (Windows) are linked
automatically.

## License

MIT. See [LICENSE](LICENSE).
