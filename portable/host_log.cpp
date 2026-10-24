// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_log.hpp"
#include "convolver_version.hpp"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <system_error>
#ifdef __APPLE__
#include <os/log.h>
#include <stdlib.h> // getprogname
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h> // program_invocation_short_name (glibc)
#endif

namespace convolver {
namespace {
constexpr std::uintmax_t largest_log = 1024 * 1024;

std::filesystem::path from_environment(const char* name) {
#ifdef _WIN32
    // _wgetenv: user profile folders may hold characters outside the ANSI code page.
    std::wstring wide(name, name + std::char_traits<char>::length(name));
    if (const wchar_t* value = _wgetenv(wide.c_str()); value && *value)
        return std::filesystem::path(value);
#else
    if (const char* value = std::getenv(name); value && *value)
        return std::filesystem::path(value);
#endif
    return {};
}

std::string now() {
    const auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char text[32];
    return std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local) ? text : "";
}
// The host's program name, so lines from several hosts can be told apart.
std::string program() {
#ifdef __APPLE__
    const char* name = getprogname();
    return name ? name : "";
#elif defined(_WIN32)
    std::wstring module(MAX_PATH, L'\0');
    const auto size = GetModuleFileNameW(nullptr, module.data(), static_cast<DWORD>(module.size()));
    if (!size || size >= module.size())
        return {};
    module.resize(size);
    const auto name = std::filesystem::path(module).stem().u8string();
    return std::string(name.begin(), name.end());
#elif defined(__GLIBC__)
    return program_invocation_short_name ? program_invocation_short_name : "";
#else
    return {};
#endif
}
std::string ports(std::size_t inputs, std::size_t outputs) {
    return std::to_string(inputs) + " in, " + std::to_string(outputs) + " out";
}
std::string filter_words(const Configuration& c) {
    return "a filter for " + std::to_string(c.sample_rate) + " Hz, " +
           ports(c.input_channels, c.output_channels);
}
} // namespace

std::string loaded_line(const HostFilter& filter, bool host_known) {
    if (const auto& f = filter.failure(); f && f->kind == HostFailureKind::saved_failure)
        return "Loaded saved state that records a failed load. " + f->message;
    if (const auto& c = filter.filter())
        return "Loaded " + filter_words(*c) +
               (!host_known               ? "; it is checked when the host starts the plug-in."
                : filter.notice().empty() ? "."
                                          : ". " + filter.notice());
    return "Loaded state with no filter.";
}

std::string started_line(const HostFilter& filter, const HostLayout& host, std::size_t max_frames) {
    auto line = "Started at " + std::to_string(host.sample_rate) + " Hz, " +
                ports(host.inputs, host.outputs) + ", blocks of up to " +
                std::to_string(max_frames) + " frames. ";
    if (const auto& f = filter.failure(); f && f->silent)
        return line + f->message;
    if (const auto& c = filter.filter()) {
        line += "Playing " + filter_words(*c) + ".";
        if (!filter.notice().empty())
            line += " " + filter.notice();
        return line;
    }
    if (filter.failed())
        return line + "Playing silence after a failed load.";
    return line + (host.inputs == host.outputs
                       ? "No filter, so audio passes unchanged."
                       : "No filter, and inputs and outputs differ, so Convolver plays silence.");
}

std::filesystem::path host_log_path() {
#ifdef _WIN32
    if (const wchar_t* chosen = _wgetenv(L"CONVOLVER_LOG"))
        return std::filesystem::path(chosen);
    const auto base = from_environment("LOCALAPPDATA");
    return base.empty() ? base : base / "Convolver" / "Convolver.log";
#else
    if (const char* chosen = std::getenv("CONVOLVER_LOG"))
        return std::filesystem::path(chosen);
#ifdef __APPLE__
    const auto home = from_environment("HOME");
    return home.empty() ? home : home / "Library" / "Logs" / "Convolver" / "Convolver.log";
#else
    if (auto state = from_environment("XDG_STATE_HOME"); !state.empty())
        return state / "convolver" / "convolver.log";
    const auto home = from_environment("HOME");
    return home.empty() ? home : home / ".local" / "state" / "convolver" / "convolver.log";
#endif
#endif
}

void host_log(const char* format, const std::string& message, bool problem) noexcept {
    if (message.empty())
        return;
#ifndef __APPLE__
    static_cast<void>(problem); // Only the unified log has levels.
#else
    if (problem)
        os_log_error(OS_LOG_DEFAULT, "Convolver %{public}s: %{public}s", format, message.c_str());
    else
        os_log(OS_LOG_DEFAULT, "Convolver %{public}s: %{public}s", format, message.c_str());
#endif
    try {
        // One writer at a time within the process; each line is one write.
        static std::mutex writing;
        std::lock_guard lock(writing);
        const auto path = host_log_path();
        if (path.empty())
            return;
        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (std::filesystem::file_size(path, ec) > largest_log && !ec) {
            auto old = path;
            old += ".old";
            std::filesystem::rename(path, old, ec);
        }
        const auto host = program();
        const auto line = now() + " Convolver " CONVOLVER_VERSION_STRING " " + format +
                          (host.empty() ? "" : " in " + host) + ": " + message + "\n";
        std::ofstream out(path, std::ios::app | std::ios::binary);
        out.write(line.data(), static_cast<std::streamsize>(line.size()));
    } catch (...) { // Out of memory, or no place to write: the message is dropped.
    }
}
} // namespace convolver
