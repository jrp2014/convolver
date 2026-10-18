// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

namespace convolver::detail {
inline void write_new_file(const std::filesystem::path& path, std::span<const std::byte> data) {
    // Never replace exclusive creation with an exists-then-open check.
#ifdef _WIN32
    // _wfopen("wbx") follows dangling symlinks on Windows. Open the entry
    // itself so CREATE_NEW rejects all existing entries, including those links.
    const auto handle =
        CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                    CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("cannot create output file (must be a new path)");
    const int fd = _open_osfhandle(reinterpret_cast<std::intptr_t>(handle), _O_WRONLY | _O_BINARY);
    if (fd < 0) {
        CloseHandle(handle);
        throw std::runtime_error(
            "cannot open output descriptor; remove incomplete file before retrying");
    }
    // On success the descriptor owns the handle; then the stream owns both.
    auto* opened = _fdopen(fd, "wb");
    if (!opened) {
        _close(fd);
        throw std::runtime_error(
            "cannot open output stream; remove incomplete file before retrying");
    }
#else
    auto* opened = std::fopen(path.c_str(), "wbx");
    if (!opened)
        throw std::runtime_error("cannot create output file (must be a new path)");
#endif
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(opened, &std::fclose);
    const bool written =
        data.empty() || std::fwrite(data.data(), 1, data.size(), file.get()) == data.size();
    // Closing flushes buffered writes and can fail even after fwrite succeeds.
    const bool closed = std::fclose(file.release()) == 0;
    if (!written || !closed)
        throw std::runtime_error(
            "cannot finish output file; remove incomplete file before retrying");
}
} // namespace convolver::detail
