# libtrash

A small, dependency-free C++17 library for moving files and directories to the
operating system's trash / recycle bin instead of permanently deleting them.

- Linux & BSD: implements the [FreeDesktop.org Trash specification v1.0](https://specifications.freedesktop.org/trash-spec/trashspec-1.0.html) directly (no glib, Qt, or GTK).
- Windows: uses `IFileOperation` with `FOFX_RECYCLEONDELETE`.
- macOS: uses `-[NSFileManager trashItemAtURL:resultingItemURL:error:]`.

No third-party runtime dependencies; only the platform SDK.

## API

The public surface is a single function with two overloads that mirror
`<filesystem>` error-handling conventions:

```cpp
#include <libtrash/trash.hpp>

// Non-throwing: returns false and sets `ec` on failure.
std::error_code ec;
if (!libtrash::trash("/path/to/file or dir", ec))
{
    if (ec == libtrash::errc::not_found) { /* ... */ }
    std::fprintf(stderr, "%s\n", ec.message().c_str());
}

// Throwing: throws std::filesystem::filesystem_error on failure.
libtrash::trash("/path/to/file");
```

Paths are UTF-8. Failure reasons are reported via the `libtrash::errc` enum,
which integrates with `std::error_code` (`libtrash::error_category()`).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Use as a submodule

```cmake
add_subdirectory(third-party/libtrash)   # tests/example are skipped automatically
target_link_libraries(your_app PRIVATE libtrash::trash)
```

On macOS the Foundation framework is linked automatically; on Windows `ole32`
and `shell32` are linked automatically.

## Status & references

Alpha. The Linux backend is the most thoroughly tested. The Windows and macOS
backends are implemented against their respective platform docs but are
validated only in CI. Correctness reference for the FDO spec:
[send2trash](https://github.com/arsenetar/send2trash) and
[trash-cli](https://github.com/andreafrancia/trash-cli).

## License

MIT. See [LICENSE](LICENSE).
