// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Windows backend: IFileOperation with FOFX_RECYCLEONDELETE.
//
// NOTE: not compiled or exercised on the development host (Linux); reviewed
// against the Windows Shell API docs and robertguetzkow/libtrashcan.

#include "libtrash/trash.hpp"

#if defined(_WIN32)

#include "trash_detail.hpp"

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shobjidl_core.h>
// clang-format on

#include <filesystem>
#include <string_view>
#include <system_error>

namespace libtrash
{

bool trash(std::string_view path, std::error_code& ec) noexcept
{
    ec.clear();
    try
    {
        // Validate + resolve to an absolute native path (this also handles
        // relative paths and forward-slash separators, which the shell parsing
        // API below does not accept on its own).
        std::filesystem::path resolved;
        if (!detail::resolve_input(path, resolved, ec))
        {
            return false;
        }

        HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        // RPC_E_CHANGED_MODE: COM is already initialized on this thread with another
        // model. That is fine; we just must not call CoUninitialize ourselves.
        bool const need_uninit = SUCCEEDED(hr);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
        {
            ec = make_error_code(errc::platform_error);
            return false;
        }

        auto const fail = [&](errc code) {
            ec = make_error_code(code);
            if (need_uninit)
            {
                ::CoUninitialize();
            }
            return false;
        };

        IFileOperation* op = nullptr;
        hr = ::CoCreateInstance(CLSID_FileOperation, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&op));
        if (FAILED(hr) || op == nullptr)
        {
            return fail(errc::platform_error);
        }

        // FOFX_RECYCLEONDELETE recycles instead of permanently deleting.
        // FOF_WANTNUKEWARNING is critical: without it, an item that cannot be
        // recycled (too large for the bin, recycling disabled, or a
        // network/removable drive) would be silently *permanently deleted*.
        // With it, such an item is refused instead, which we detect via
        // GetAnyOperationsAborted below and report as a failure -- so trash()
        // never destroys data behind the caller's back.
        hr = op->SetOperationFlags(
            FOFX_RECYCLEONDELETE | FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT | FOF_WANTNUKEWARNING);
        if (FAILED(hr))
        {
            op->Release();
            return fail(errc::platform_error);
        }

        IShellItem* item = nullptr;
        hr = ::SHCreateItemFromParsingName(resolved.c_str(), nullptr, IID_PPV_ARGS(&item));
        if (FAILED(hr) || item == nullptr)
        {
            op->Release();
            if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
            {
                return fail(errc::not_found);
            }
            return fail(errc::platform_error);
        }

        hr = op->DeleteItem(item, nullptr);
        item->Release();
        if (FAILED(hr))
        {
            op->Release();
            return fail(errc::platform_error);
        }

        hr = op->PerformOperations();
        if (FAILED(hr))
        {
            op->Release();
            if (hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
            {
                return fail(errc::permission_denied);
            }
            return fail(errc::platform_error);
        }

        BOOL aborted = FALSE;
        op->GetAnyOperationsAborted(&aborted);
        op->Release();
        if (aborted)
        {
            // The operation was refused -- e.g. the item could not be recycled
            // and FOF_WANTNUKEWARNING blocked the permanent delete. Report
            // failure rather than pretend it was trashed.
            return fail(errc::platform_error);
        }

        if (need_uninit)
        {
            ::CoUninitialize();
        }
        return true;
    }
    catch (...)
    {
        ec = make_error_code(errc::platform_error);
        return false;
    }
}

} // namespace libtrash

#endif // _WIN32
