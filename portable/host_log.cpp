// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_log.hpp"
#include "convolver_version.hpp"
#include <atomic>
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
#include <unistd.h> // getpid
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <errno.h>  // program_invocation_short_name (glibc)
#include <unistd.h> // getpid
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
unsigned long process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return static_cast<unsigned long>(getpid());
#endif
}
std::string ports(std::size_t inputs, std::size_t outputs) {
    return std::to_string(inputs) + " in, " + std::to_string(outputs) + " out";
}
std::atomic<bool> last_written{true};
// The line, and once per run of failures why the file could not be written,
// where a user or developer can still find them.
bool explained = false; // Under host_log's lock: why was written, since the last good line.
void fall_back(const std::string& line, const std::string& why) noexcept {
    try {
        std::string text;
        if (!explained)
            text = "Convolver: " + why + " Convolver log lines go to standard error instead.\n";
        explained = true;
        text += line;
        std::fwrite(text.data(), 1, text.size(), stderr);
        std::fflush(stderr);
#ifdef _WIN32
        OutputDebugStringA(text.c_str());
#endif
    } catch (...) {
    }
}
std::string filter_words(const Configuration& c) {
    return "a filter for " + std::to_string(c.sample_rate) + " Hz, " +
           ports(c.input_channels, c.output_channels);
}
} // namespace

std::string execution_line() {
    const auto host = program();
    return "Session: Convolver " CONVOLVER_VERSION_STRING ", FFT backend " +
           std::string(fft_backend_name()) + " (" + fft_backend_version() + "), CPU kernels " +
           cpu_kernels_name() + ", process " + std::to_string(process_id()) +
           (host.empty() ? "" : ", host program " + host) + ".";
}

bool host_log_written() noexcept {
    return last_written.load(std::memory_order_relaxed);
}

namespace {
std::string request_words(const HostRequest& r) {
    return "Request " + std::to_string(r.number) + ": ";
}
} // namespace

std::string loaded_line(const HostFilter& filter, bool host_known) {
    const auto request = request_words(filter.accepted());
    if (const auto& f = filter.failure(); f && f->kind == HostFailureKind::saved_failure)
        return request + "Loaded saved state that records a failed load. " + f->message;
    if (const auto& c = filter.filter())
        return request + "Loaded " + filter_words(*c) +
               (!host_known               ? "; it is checked when the host starts the plug-in."
                : filter.notice().empty() ? "."
                                          : ". " + filter.notice());
    return request + "Loaded state with no filter.";
}

std::string rejected_line(const HostFilter& filter) {
    const auto& r = filter.rejection();
    return r ? request_words(r->request) + r->message : std::string();
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
        // The execution line, once per process in the file and once on the
        // fall-back route, so the file still gets it after a failed start.
        static bool session_in_file = false, session_in_fall_back = false;
        const auto host = program();
        const auto prefix = now() + " Convolver " CONVOLVER_VERSION_STRING " " + format +
                            (host.empty() ? "" : " in " + host) + " (process " +
                            std::to_string(process_id()) + "): ";
        const auto line = prefix + message + "\n";
        const auto session = prefix + execution_line() + "\n";
        const auto fall_back_line = [&]() {
            auto text = session_in_fall_back ? line : session + line;
            session_in_fall_back = true;
            return text;
        };
#ifndef _WIN32
        // Set empty on purpose: the file is off, which is not a failure.
        if (const char* chosen = std::getenv("CONVOLVER_LOG"); chosen && !*chosen) {
            last_written.store(true, std::memory_order_relaxed);
            return;
        }
#endif
        const auto path = host_log_path();
        if (path.empty()) {
            last_written.store(false, std::memory_order_relaxed);
            fall_back(fall_back_line(), "No Convolver log file can be chosen (there is no home "
                                        "folder in the environment).");
            return;
        }
        std::error_code ec;
        if (path.has_parent_path())
            std::filesystem::create_directories(path.parent_path(), ec);
        if (std::filesystem::file_size(path, ec) > largest_log && !ec) {
            auto old = path;
            old += ".old";
            std::filesystem::rename(path, old, ec);
        }
        const auto text = session_in_file ? line : session + line;
        std::ofstream out(path, std::ios::app | std::ios::binary);
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.close();
        const bool written = !out.fail();
        last_written.store(written, std::memory_order_relaxed);
        if (written) {
            session_in_file = true;
            explained = false; // A later failure explains itself again.
        } else {
            const auto where = path.u8string();
            fall_back(fall_back_line(),
                      "The Convolver log file " + std::string(where.begin(), where.end()) +
                          " could not be written (check that its folder exists and can "
                          "be written to).");
        }
    } catch (...) { // Out of memory: the message is dropped.
        last_written.store(false, std::memory_order_relaxed);
    }
}
} // namespace convolver
