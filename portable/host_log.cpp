// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_log.hpp"
#include "convolver_version.hpp"
#include <algorithm>
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
#include <vector>
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

namespace {
std::atomic<const char*> sdk_description{nullptr};
} // namespace

void set_host_log_sdk(const char* description) noexcept {
    sdk_description.store(description, std::memory_order_relaxed);
}

std::string execution_line() {
    const auto host = program();
    const char* sdk = sdk_description.load(std::memory_order_relaxed);
    return "Session: Convolver " CONVOLVER_VERSION_STRING ", FFT backend " +
           std::string(fft_backend_name()) + " (" + fft_backend_version() + "), CPU kernels " +
           cpu_kernels_name() + (sdk ? std::string(", built with ") + sdk : std::string()) +
           ", process " + std::to_string(process_id()) +
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

std::string settings_words(const HostSettings& s, bool equal_counts) {
    char gain[32] = {};
    std::snprintf(gain, sizeof gain, "gain %g dB", s.gain_db);
    if (!equal_counts)
        return std::string(gain) +
               ", mix 100% wet (inputs and outputs differ, so there is no dry signal)";
    char mix[32] = {};
    std::snprintf(mix, sizeof mix, ", mix %g%% wet", 100 * s.mix);
    return std::string(gain) + mix;
}

std::string mono_track_hint(const HostFailure& failure, const HostLayout& host) {
    // GarageBand runs effects with one channel on a mono track (jrp's first
    // GarageBand run, 25 October 2026). Only a stereo filter is fixed by a
    // stereo track.
    if (failure.kind != HostFailureKind::channels || host.inputs != 1 || host.outputs != 1 ||
        failure.inputs != 2 || failure.outputs != 2)
        return {};
    return " Hosts run an effect with one channel on a mono track (GarageBand does): put "
           "Convolver on a stereo track instead.";
}

std::string playing_words(const HostFilter& filter, const HostLayout& host,
                          const HostSettings& used) {
    // PLUG-10: with equal counts, Mix's dry share plays even when no filter
    // does, and Gain changes the input that passes with no filter loaded.
    const bool equal = host.inputs == host.outputs;
    const auto sound = [&](bool identity) {
        return equal ? unfiltered_sound(used, identity) : UnfilteredSound{true, false, {}};
    };
    const auto dry_words = [](const UnfilteredSound& s) {
        return s.unchanged ? std::string("unchanged") : "at " + s.level;
    };
    if (const auto& f = filter.failure(); f && f->silent) {
        auto line = f->message + mono_track_hint(*f, host);
        if (const auto s = sound(false); !s.silent) {
            char mix[32] = {};
            std::snprintf(mix, sizeof mix, "%g%%", 100 * std::clamp(used.mix, 0.0, 1.0));
            line += std::string(" With Mix at ") + mix +
                    " wet, though, the dry signal still plays instead of silence: the input " +
                    dry_words(s) + ".";
        }
        return line;
    }
    if (const auto& c = filter.filter()) {
        auto line = "Playing " + filter_words(*c) + ".";
        if (!filter.notice().empty())
            line += " " + filter.notice();
        return line;
    }
    if (filter.failed()) {
        const auto s = sound(false);
        return s.silent ? "Playing silence after a failed load."
                        : "Playing only the dry signal after a failed load: the input " +
                              dry_words(s) + ".";
    }
    if (!equal)
        return "No filter, and inputs and outputs differ, so Convolver plays silence.";
    const auto s = sound(true);
    return s.unchanged ? std::string("No filter, so audio passes unchanged.")
                       : "No filter, so audio passes at " + s.level + ", set by Gain and Mix.";
}

std::string started_line(const HostFilter& filter, const HostLayout& host, std::size_t max_frames,
                         const HostSettings* settings) {
    auto line = "Started at " + std::to_string(host.sample_rate) + " Hz, " +
                ports(host.inputs, host.outputs) + ", blocks of up to " +
                std::to_string(max_frames) + " frames. " +
                playing_words(filter, host, settings ? *settings : HostSettings{});
    if (settings)
        line += " Settings: " + settings_words(*settings, host.inputs == host.outputs) + ".";
    return line;
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
std::string host_program_name() {
    return program();
}

std::string host_log_recent(const std::string& format, std::size_t max_lines) {
    try {
        const auto path = host_log_path();
        if (path.empty() || max_lines == 0)
            return {};
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return {};
        constexpr std::streamoff largest_read = 64 * 1024;
        in.seekg(0, std::ios::end);
        const auto size = static_cast<std::streamoff>(in.tellg());
        const auto start = size > largest_read ? size - largest_read : 0;
        in.seekg(start);
        std::string text(static_cast<std::size_t>(size - start), '\0');
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(in.gcount()));
        // Every line names its format and process: "... Convolver <version>
        // <format> in <program> (process <id>): <message>", or without " in
        // <program>" where there is none.
        const auto process = " (process " + std::to_string(process_id()) + "): ";
        const auto mine = [&](const std::string& line) {
            const auto at = line.find(process);
            if (at == std::string::npos)
                return false;
            if (line.compare(at + process.size(), 9, "Session: ") == 0)
                return true;
            const auto named = " " + format;
            const auto tag = line.find(named + " in ");
            return (tag != std::string::npos && tag < at) ||
                   (at >= named.size() &&
                    line.compare(at - named.size(), named.size(), named) == 0);
        };
        std::vector<std::string> lines;
        std::size_t begin = 0;
        if (start > 0) { // Skip the partial first line.
            const auto newline = text.find('\n');
            begin = newline == std::string::npos ? text.size() : newline + 1;
        }
        while (begin < text.size()) {
            auto end = text.find('\n', begin);
            if (end == std::string::npos)
                end = text.size();
            auto line = text.substr(begin, end - begin);
            if (mine(line))
                lines.push_back(std::move(line));
            begin = end + 1;
        }
        const auto first = lines.size() > max_lines ? lines.size() - max_lines : 0;
        std::string recent;
        for (auto i = first; i < lines.size(); ++i)
            recent += lines[i] + "\n";
        return recent;
    } catch (...) { // Out of memory, or the file changed while it was read.
        return {};
    }
}
} // namespace convolver
