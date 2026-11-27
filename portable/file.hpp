// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "advice.hpp"
#include "utf8.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace convolver::detail {
// IO-13: the error for a file or folder that could not be created: one that
// gives the path's length when that is why (windows_path_length_problem()),
// otherwise `problem` alone. The first names the path itself.
[[noreturn]] inline void creation_failed(const std::filesystem::path& path, bool folder,
                                         const std::string& problem) {
    if (auto too_long = windows_path_length_problem(path, folder); !too_long.empty())
        throw AdvisedError<std::runtime_error>({"cannot create " + utf8_string(path),
                                                std::move(too_long),
                                                std::string(windows_path_length_fix),
                                                {}});
    throw std::runtime_error(problem);
}
// Creates a new, empty folder. Returns false, with `error` set, when it could
// not, except when the reason is the path's length on Windows, which it
// throws for with that advice (IO-13).
inline bool create_new_folder(const std::filesystem::path& path, std::error_code& error) {
    if (std::filesystem::create_directory(path, error) && !error)
        return true;
    if (!windows_path_length_problem(path, true).empty())
        creation_failed(path, true, {});
    return false;
}
// IO-15: the file write_new_file() created, removed because it could not be
// finished, so that a retry finds the path free. The error says so, or names
// the file when it could not be removed.
[[noreturn]] inline void unfinished(const std::filesystem::path& path, const std::string& problem) {
    std::error_code error;
    std::filesystem::remove(path, error);
    if (!error)
        throw std::runtime_error(problem + "; the incomplete file was removed");
    throw std::runtime_error(problem + "; remove the incomplete file " + utf8_string(path) +
                             " before retrying");
}
// Writes `data` to a new file at `path`. It never replaces an existing entry,
// and when it cannot finish the file it removes it (unfinished()).
inline void write_new_file(const std::filesystem::path& path, std::span<const std::byte> data) {
    // Never replace exclusive creation with an exists-then-open check.
#ifdef _WIN32
    // _wfopen("wbx") follows dangling symlinks on Windows. Open the entry
    // itself so CREATE_NEW rejects all existing entries, including those links.
    const auto handle =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        creation_failed(path, false, "cannot create output file (must be a new path)");
    const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(handle);
        unfinished(path, "cannot open output descriptor");
    }
    // On success the descriptor owns the handle; then the stream owns both.
    auto* opened = _fdopen(fd, "wb");
    if (!opened) {
        _close(fd);
        unfinished(path, "cannot open output stream");
    }
#else
    auto* opened = std::fopen(path.c_str(), "wbx");
    if (!opened)
        creation_failed(path, false, "cannot create output file (must be a new path)");
#endif
    // A deleter type rather than decltype(&std::fclose), whose attributes
    // GCC drops from the template argument with a warning.
    struct Close {
        void operator()(std::FILE* stream) const noexcept {
            std::fclose(stream);
        }
    };
    std::unique_ptr<std::FILE, Close> file(opened);
    const bool written =
        data.empty() || std::fwrite(data.data(), 1, data.size(), file.get()) == data.size();
    // Closing flushes buffered writes and can fail even after fwrite succeeds.
    const bool closed = std::fclose(file.release()) == 0;
    if (!written || !closed)
        unfinished(path, "cannot finish writing the output file");
}
} // namespace convolver::detail
