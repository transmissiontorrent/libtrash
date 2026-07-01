// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// macOS backend: -[NSFileManager trashItemAtURL:resultingItemURL:error:].
//
// NOTE: not compiled or exercised on the development host (Linux); reviewed
// against the Foundation API docs.

#include "libtrash/trash.hpp"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>

#include <string_view>
#include <system_error>

namespace libtrash
{

bool trash(std::string_view path, std::error_code& ec) noexcept
{
    ec.clear();
    if (path.empty() || path.find('\0') != std::string_view::npos)
    {
        ec = make_error_code(errc::invalid_argument);
        return false;
    }

    @autoreleasepool
    {
        NSString* ns_path = [[NSString alloc] initWithBytes:path.data()
                                                     length:static_cast<NSUInteger>(path.size())
                                                   encoding:NSUTF8StringEncoding];
        if (ns_path == nil)
        {
            ec = make_error_code(errc::invalid_argument);
            return false;
        }

        NSURL* url = [NSURL fileURLWithPath:ns_path];
        NSURL* resulting = nil;
        NSError* error = nil;
        BOOL const ok = [[NSFileManager defaultManager] trashItemAtURL:url
                                                      resultingItemURL:&resulting
                                                                 error:&error];
        if (ok)
        {
            return true;
        }

        if (error != nil && [error.domain isEqualToString:NSCocoaErrorDomain])
        {
            if (error.code == NSFileNoSuchFileError || error.code == NSFileReadNoSuchFileError)
            {
                ec = make_error_code(errc::not_found);
                return false;
            }
            if (error.code == NSFileWriteNoPermissionError)
            {
                ec = make_error_code(errc::permission_denied);
                return false;
            }
        }
        ec = make_error_code(errc::platform_error);
        return false;
    }
}

} // namespace libtrash

#endif // __APPLE__
