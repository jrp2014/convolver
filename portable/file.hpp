// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>

namespace convolver::detail {
inline void write_new_file(const std::filesystem::path& path, std::span<const std::byte> data) {
    // C exclusive creation is available without raising the C++20 baseline.
    // Keep Windows paths wide; never replace this with an exists-then-open check.
#ifdef _WIN32
    auto* opened = _wfopen(path.c_str(), L"wbx");
#else
    auto* opened = std::fopen(path.c_str(), "wbx");
#endif
    if (!opened)
        throw std::runtime_error("cannot create output file (must be a new path)");
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
