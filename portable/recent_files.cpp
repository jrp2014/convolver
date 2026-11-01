// SPDX-License-Identifier: GPL-2.0-or-later
#include "recent_files.hpp"
#include "host_log.hpp"
#include <algorithm>
#include <fstream>
#include <random>
#include <system_error>

namespace convolver {
namespace {
// Writes `files` as the whole list, through a file beside it renamed over it.
bool save(const std::vector<std::string>& files) {
    const auto path = recent_files_path();
    if (path.empty())
        return false;
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    auto temporary = path;
    temporary += "." + std::to_string(std::random_device{}()) + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& file : files)
            out << file << '\n';
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, ec);
            return false;
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}
} // namespace

std::filesystem::path recent_files_path() {
    const auto log = host_log_path();
    return log.empty() ? log : log.parent_path() / "recent-filters.txt";
}

std::vector<std::string> recent_files() noexcept {
    try {
        std::vector<std::string> files;
        const auto path = recent_files_path();
        if (path.empty())
            return files;
        std::ifstream in(path, std::ios::binary);
        std::string line;
        while (files.size() < recent_files_kept && std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') // Edited on Windows.
                line.pop_back();
            if (!line.empty() && std::find(files.begin(), files.end(), line) == files.end())
                files.push_back(line);
        }
        return files;
    } catch (...) { // Out of memory, or a path that cannot be formed.
        return {};
    }
}

bool remember_recent_file(const std::string& path) noexcept {
    if (path.empty() || path.find_first_of("\r\n") != std::string::npos)
        return false;
    try {
        auto files = recent_files();
        files.erase(std::remove(files.begin(), files.end(), path), files.end());
        files.insert(files.begin(), path);
        if (files.size() > recent_files_kept)
            files.resize(recent_files_kept);
        return save(files);
    } catch (...) {
        return false;
    }
}

bool forget_recent_files() noexcept {
    try {
        return save({});
    } catch (...) {
        return false;
    }
}
} // namespace convolver
