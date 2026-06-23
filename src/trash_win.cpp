// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Windows backend: IFileOperation with FOFX_RECYCLEONDELETE.
//
// NOTE: not compiled or exercised on the development host (Linux); reviewed
// against the Windows Shell API docs and robertguetzkow/libtrashcan.

#include "librecycle/recycle.hpp"

#if defined(_WIN32)

// clang-format off
#include <windows.h>
#include <objbase.h>
#include <shobjidl.h>
#include <shobjidl_core.h>
// clang-format on

#include <string>
#include <string_view>
#include <system_error>

namespace librecycle
{
namespace
{

std::wstring utf8_to_wide(std::string_view s)
{
    if (s.empty())
    {
        return {};
    }
    int const n = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
    {
        return {};
    }
    std::wstring w(static_cast<size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

} // namespace

bool recycle(std::string_view path, std::error_code& ec) noexcept
{
    ec.clear();
    if (path.empty() || path.find('\0') != std::string_view::npos)
    {
        ec = make_error_code(errc::invalid_argument);
        return false;
    }

    std::wstring const wide = utf8_to_wide(path);
    if (wide.empty())
    {
        ec = make_error_code(errc::invalid_argument);
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

    hr = op->SetOperationFlags(FOFX_RECYCLEONDELETE | FOF_NO_UI | FOF_NOCONFIRMATION | FOF_SILENT);
    if (FAILED(hr))
    {
        op->Release();
        return fail(errc::platform_error);
    }

    IShellItem* item = nullptr;
    hr = ::SHCreateItemFromParsingName(wide.c_str(), nullptr, IID_PPV_ARGS(&item));
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
        return fail(errc::platform_error);
    }

    if (need_uninit)
    {
        ::CoUninitialize();
    }
    return true;
}

} // namespace librecycle

#endif // _WIN32
