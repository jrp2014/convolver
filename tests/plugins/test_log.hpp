// SPDX-License-Identifier: GPL-2.0-or-later
// The Convolver log of a plug-in test process (CI-27, CI-28), shared by the
// Audio Unit and VST3 tests.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace convolver::test_log {
// The file CTest names in the build folder, or `name` in the current folder
// when run by hand. Call it once in main(), before any plug-in instance
// exists: a setenv while an instance's worker reads the variable is a data
// race, which ASan caught when a test changed it per case (CI-22). The file is
// never copied or removed, so it and the ".old" file host_log rotates it to
// past 1 MiB are evidence even after a crash, a sanitizer abort or a forced
// end. On Windows the variable is read and set wide, as host_log_path() reads
// it, since build folders may hold characters outside the ANSI code page.
inline std::filesystem::path use_test_log(const char* name) {
#ifdef _WIN32
    if (const wchar_t* configured = _wgetenv(L"CONVOLVER_LOG"); configured && *configured)
        return configured;
    const auto path = std::filesystem::absolute(name);
    _wputenv_s(L"CONVOLVER_LOG", path.wstring().c_str());
#else
    if (const char* configured = std::getenv("CONVOLVER_LOG"); configured && *configured)
        return configured;
    const auto path = std::filesystem::absolute(name);
    setenv("CONVOLVER_LOG", path.c_str(), 1);
#endif
    return path;
}

// What a log gained while this object exists: earlier runs' lines stay in
// the file. If host_log moved it to ".old" meanwhile and began a new one, the
// rest of the old one and all of the new.
class LogSince {
  public:
    explicit LogSince(std::filesystem::path path)
        : path_(std::move(path)), start_(size_now()), old_time_(old_time()) {}
    std::string text() const {
        auto all = read(path_);
        if (all.size() < start_ || old_time() != old_time_) {
            const auto before = read(old());
            return (start_ < before.size() ? before.substr(start_) : std::string()) + all;
        }
        return all.substr(start_);
    }
    const std::filesystem::path& path() const {
        return path_;
    }

  private:
    static std::string read(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    }
    std::filesystem::path old() const {
        auto old = path_;
        old += ".old";
        return old;
    }
    std::filesystem::file_time_type old_time() const {
        std::error_code missing;
        return std::filesystem::last_write_time(old(), missing);
    }
    std::size_t size_now() const {
        std::error_code missing;
        const auto size = std::filesystem::file_size(path_, missing);
        return missing ? 0 : static_cast<std::size_t>(size);
    }
    std::filesystem::path path_;
    std::size_t start_;
    std::filesystem::file_time_type old_time_;
};
} // namespace convolver::test_log
