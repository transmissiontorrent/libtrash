// SPDX-License-Identifier: MIT
// This file Copyright © Mnemosaic LLC.
//
// Linux / *BSD backend: FreeDesktop.org Trash specification v1.0.
// https://specifications.freedesktop.org/trash-spec/trashspec-1.0.html
//
// Correctness reference: arsenetar/send2trash (plat_other.py) and
// andreafrancia/trash-cli.

#include "libtrash/trash.hpp"

#if defined(__linux__) || defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__)

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace libtrash
{
namespace
{

std::string get_home()
{
    if (char const* h = std::getenv("HOME"); h != nullptr && *h != '\0')
    {
        return h;
    }
    if (passwd const* pw = getpwuid(getuid()); pw != nullptr && pw->pw_dir != nullptr)
    {
        return pw->pw_dir;
    }
    return {};
}

// $XDG_DATA_HOME if set to an absolute path, else ~/.local/share. Empty on failure.
std::string xdg_data_home()
{
    if (char const* x = std::getenv("XDG_DATA_HOME"); x != nullptr && x[0] == '/')
    {
        return x;
    }
    std::string const home = get_home();
    if (home.empty())
    {
        return {};
    }
    return home + "/.local/share";
}

bool lstat_dev(std::string const& path, dev_t& out_dev)
{
    struct stat st = {};
    if (::lstat(path.c_str(), &st) != 0)
    {
        return false;
    }
    out_dev = st.st_dev;
    return true;
}

// Device of the nearest existing ancestor of `path` (used for not-yet-created
// trash directories).
bool nearest_existing_dev(std::string path, dev_t& out_dev)
{
    while (!path.empty())
    {
        struct stat st = {};
        if (::stat(path.c_str(), &st) == 0)
        {
            out_dev = st.st_dev;
            return true;
        }
        auto parent = fs::path(path).parent_path();
        if (parent.empty() || parent == fs::path(path))
        {
            break;
        }
        path = parent.string();
    }
    return false;
}

// The mount point containing `abs_path`: walk up while the parent stays on the
// same device.
std::string find_mount_point(std::string const& abs_path)
{
    dev_t dev = 0;
    if (!lstat_dev(abs_path, dev))
    {
        return {};
    }
    fs::path cur(abs_path);
    for (;;)
    {
        fs::path parent = cur.parent_path();
        if (parent.empty() || parent == cur)
        {
            break; // reached "/"
        }
        dev_t pdev = 0;
        if (!lstat_dev(parent.string(), pdev) || pdev != dev)
        {
            break; // parent is on another device -> cur is the mount point
        }
        cur = parent;
    }
    return cur.string();
}

// mkdir -p with the given mode applied to directories we create.
bool ensure_dir(std::string const& path, mode_t mode)
{
    struct stat st = {};
    if (::stat(path.c_str(), &st) == 0)
    {
        return S_ISDIR(st.st_mode);
    }
    fs::path const p(path);
    fs::path const parent = p.parent_path();
    if (!parent.empty() && parent != p && !ensure_dir(parent.string(), mode))
    {
        return false;
    }
    return ::mkdir(path.c_str(), mode) == 0 || errno == EEXIST;
}

// A shared "administrator" trash ($topdir/.Trash) is only safe if it is a real
// directory (not a symlink) with the sticky bit set. This guards against a
// hostile user planting a symlink on a shared mount.
bool is_safe_admin_trash(std::string const& path)
{
    struct stat st = {};
    if (::lstat(path.c_str(), &st) != 0)
    {
        return false;
    }
    return S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode) && (st.st_mode & S_ISVTX) != 0;
}

bool is_unreserved(unsigned char c)
{
    // Matches Python's urllib.parse.quote default never-quote set, which
    // send2trash uses with safe='/': ALWAYS_SAFE + '/'.
    return (std::isalnum(c) != 0) || c == '_' || c == '.' || c == '-' || c == '~';
}

std::string percent_encode(std::string_view s)
{
    static char const* const kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char const c : s)
    {
        if (is_unreserved(c) || c == '/')
        {
            out.push_back(static_cast<char>(c));
        }
        else
        {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

std::string deletion_date_now()
{
    std::time_t const t = std::time(nullptr);
    struct tm tm = {};
    ::localtime_r(&t, &tm);
    char buf[32] = {};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm); // local time, no TZ
    return buf;
}

bool path_exists(std::string const& p)
{
    struct stat st = {};
    return ::lstat(p.c_str(), &st) == 0;
}

struct TrashName
{
    std::string files_path;
    std::string info_path;
};

// Find a name that is free in both files/ and info/, inserting a counter before
// the extension on collision.
TrashName pick_unique_name(std::string const& files_dir, std::string const& info_dir, std::string const& base)
{
    auto make = [&](std::string const& name) {
        return TrashName{ files_dir + "/" + name, info_dir + "/" + name + ".trashinfo" };
    };

    TrashName candidate = make(base);
    int counter = 1;
    while (path_exists(candidate.files_path) || path_exists(candidate.info_path))
    {
        ++counter;
        std::string name;
        auto const dot = base.rfind('.');
        if (dot != std::string::npos && dot != 0)
        {
            name = base.substr(0, dot) + "_" + std::to_string(counter) + base.substr(dot);
        }
        else
        {
            name = base + "_" + std::to_string(counter);
        }
        candidate = make(name);
    }
    return candidate;
}

// Write info/NAME.trashinfo, claiming the slot atomically with O_EXCL.
bool write_trashinfo(std::string const& info_path, std::string const& encoded_path, std::string const& date)
{
    int const fd = ::open(info_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
    {
        return false;
    }

    std::string const content = "[Trash Info]\nPath=" + encoded_path + "\nDeletionDate=" + date + "\n";
    char const* data = content.data();
    auto remaining = static_cast<ssize_t>(content.size());
    while (remaining > 0)
    {
        ssize_t const w = ::write(fd, data, static_cast<size_t>(remaining));
        if (w < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            ::close(fd);
            ::unlink(info_path.c_str());
            return false;
        }
        data += w;
        remaining -= w;
    }
    ::close(fd);
    return true;
}

} // namespace

bool trash(std::string_view path_sv, std::error_code& ec) noexcept
{
    ec.clear();
    try
    {
        if (path_sv.empty() || path_sv.find('\0') != std::string_view::npos)
        {
            ec = make_error_code(errc::invalid_argument);
            return false;
        }

        std::error_code fsec;
        fs::path const abs = fs::absolute(fs::path(std::string(path_sv)), fsec);
        if (fsec)
        {
            ec = make_error_code(errc::platform_error);
            return false;
        }
        std::string abs_path = abs.lexically_normal().string();
        if (abs_path.size() > 1 && abs_path.back() == '/')
        {
            abs_path.pop_back();
        }

        if (!path_exists(abs_path))
        {
            ec = make_error_code(errc::not_found);
            return false;
        }

        dev_t file_dev = 0;
        (void)lstat_dev(abs_path, file_dev);

        std::string home_trash = xdg_data_home();
        if (home_trash.empty())
        {
            ec = make_error_code(errc::platform_error);
            return false;
        }
        home_trash += "/Trash";

        dev_t home_dev = 0;
        bool const have_home_dev = nearest_existing_dev(home_trash, home_dev);

        std::string trash_dir;
        std::string original_for_info; // absolute (home trash) or relative (topdir trash)

        if (have_home_dev && file_dev == home_dev)
        {
            trash_dir = home_trash;
            original_for_info = abs_path;
        }
        else
        {
            std::string const topdir = find_mount_point(abs_path);
            if (topdir.empty())
            {
                ec = make_error_code(errc::cross_device);
                return false;
            }
            auto const uid = static_cast<unsigned long>(::getuid());
            std::string const admin = topdir + "/.Trash";
            if (is_safe_admin_trash(admin))
            {
                trash_dir = admin + "/" + std::to_string(uid);
            }
            else
            {
                trash_dir = topdir + "/.Trash-" + std::to_string(uid);
            }

            // Path is recorded relative to the top directory for topdir trashes.
            std::string rel = abs_path;
            if (rel.rfind(topdir, 0) == 0)
            {
                rel.erase(0, topdir.size());
                while (!rel.empty() && rel.front() == '/')
                {
                    rel.erase(0, 1);
                }
            }
            original_for_info = rel;
        }

        std::string const files_dir = trash_dir + "/files";
        std::string const info_dir = trash_dir + "/info";
        if (!ensure_dir(files_dir, 0700) || !ensure_dir(info_dir, 0700))
        {
            ec = make_error_code(errc::permission_denied);
            return false;
        }

        std::string base = fs::path(abs_path).filename().string();
        if (base.empty())
        {
            base = "file";
        }
        TrashName const target = pick_unique_name(files_dir, info_dir, base);

        if (!write_trashinfo(target.info_path, percent_encode(original_for_info), deletion_date_now()))
        {
            ec = make_error_code(errc::permission_denied);
            return false;
        }

        if (::rename(abs_path.c_str(), target.files_path.c_str()) != 0)
        {
            int const e = errno;
            ::unlink(target.info_path.c_str()); // release the claimed slot
            if (e == EXDEV)
            {
                ec = make_error_code(errc::cross_device);
            }
            else if (e == EACCES || e == EPERM)
            {
                ec = make_error_code(errc::permission_denied);
            }
            else
            {
                ec = make_error_code(errc::platform_error);
            }
            return false;
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

#endif // platform
