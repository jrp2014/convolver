// SPDX-License-Identifier: GPL-2.0-or-later
#include "cli_files.hpp"
#include <charconv>
#include <string>
#include <string_view>
#include <system_error>
#ifndef _WIN32
#include <climits>
#include <fcntl.h>
#include <sys/stat.h>
#endif

namespace convolver::cli {
bool is_stream(const std::filesystem::path& path) {
#ifdef _WIN32
    (void)path;
    return false; // Windows has no /dev/fd; a console is a character device.
#else
    if (path == "-")
        return false;
    std::error_code ec;
    const auto type = std::filesystem::status(path, ec).type();
    return !ec && (type == std::filesystem::file_type::fifo ||
                   type == std::filesystem::file_type::socket ||
                   type == std::filesystem::file_type::character);
#endif
}
#ifndef _WIN32
// IO-16: on macOS, comparing such a path with std::filesystem::equivalent did
// not find the file the descriptor has open (CI, /dev/stdin redirected from
// the output), so callers compare the descriptor itself, with fstat().
std::optional<int> named_descriptor(const std::filesystem::path& path) {
    const std::string_view text = path.native();
    if (text == "/dev/stdin")
        return 0;
    if (text == "/dev/stdout")
        return 1;
    if (text == "/dev/stderr")
        return 2;
    constexpr std::string_view prefix = "/dev/fd/";
    if (!text.starts_with(prefix) || text.size() == prefix.size())
        return std::nullopt;
    const auto digits = text.substr(prefix.size());
    int descriptor = -1;
    const auto [end, error] =
        std::from_chars(digits.data(), digits.data() + digits.size(), descriptor);
    if (error != std::errc{} || end != digits.data() + digits.size())
        return std::nullopt;
    return descriptor;
}
#endif
// IO-17: on macOS, opening /dev/stdin or /dev/fd/N shares the descriptor's
// read position, so a regular file read through it twice (the WAV check,
// then libsndfile) is read the second time from where the first stopped; the
// file's own path starts afresh. On Linux the descriptor's path is a link,
// which a configuration's relative IR names were resolved beside (/dev), not
// beside the file. The file's own path is used only when it still leads to
// the file the descriptor has open.
std::filesystem::path readable_path(const std::filesystem::path& path) {
#ifndef _WIN32
    const auto descriptor = named_descriptor(path);
    struct stat opened{};
    if (!descriptor || fstat(*descriptor, &opened) != 0 || !S_ISREG(opened.st_mode))
        return path;
    std::filesystem::path real;
#ifdef __APPLE__
    char buffer[PATH_MAX]{};
    if (fcntl(*descriptor, F_GETPATH, buffer) == -1)
        return path;
    real = buffer;
#else
    std::error_code ec;
    real = std::filesystem::read_symlink("/proc/self/fd/" + std::to_string(*descriptor), ec);
    if (ec)
        return path;
#endif
    struct stat named{};
    if (stat(real.c_str(), &named) == 0 && named.st_dev == opened.st_dev &&
        named.st_ino == opened.st_ino)
        return real;
#endif
    return path;
}
} // namespace convolver::cli
