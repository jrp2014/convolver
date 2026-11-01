// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-9: the editor report (portable/host_report.hpp), one case per kind of
// state a plug-in window shows: its status, the full explanation with what
// to change, the details, the fields an adapter sends, the plain text and
// the Copy diagnostics text with this instance's Convolver log lines.
#include "convolver/host.hpp"
#include "host_log.hpp"
#include "host_report.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

using namespace convolver;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}
// Identity paths: a filter for `rate` with `channels` in and out.
Configuration filter(int rate, std::size_t channels) {
    Configuration c{rate, channels, channels, 0, {}, {}, {}};
    for (std::size_t i = 0; i < channels; ++i)
        c.paths.push_back({{1.f, .5f, .25f}, {{i, 1}}, {{i, 1}}});
    return c;
}
std::string detail(const HostReport& r, const std::string& label) {
    for (const auto& [name, value] : r.details)
        if (name == label)
            return value;
    throw std::runtime_error("no detail " + label);
}
HostReportFacts facts(std::optional<HostLayout> host, HostSettings settings = {}) {
    HostReportFacts f;
    f.format = "Test #7";
    f.host = host;
    f.max_frames = host ? 512 : 0;
    f.settings = settings;
    f.latency_frames = 256;
    f.tail_frames = 2;
    return f;
}

struct Case {
    const char* name;
    std::function<HostReport()> make;
    HostStatus status;
    std::vector<std::string> explained; // Parts the explanation must hold.
    std::vector<std::string> absent;    // Parts it must not.
};

void cases() {
    const HostLayout stereo{44100, 2, 2}, mono{44100, 1, 1};
    const std::vector<Case> all{
        {"empty, not started",
         [] {
             HostFilter f(8);
             return host_report(f, facts(std::nullopt));
         },
         HostStatus::no_filter,
         {"No filter is loaded.", "has not started", "load a Convolver preset"},
         {"Silent"}},
        // The Audio Unit's window has Choose Filter.
        {"empty, the window chooses files",
         [&] {
             HostFilter f(8);
             auto x = facts(stereo);
             x.chooses_files = true;
             x.file_reader = "macOS AudioToolbox";
             auto r = host_report(f, x);
             for (const auto& [name, value] : r.details)
                 if (name == "Software" &&
                     value.find("; filter files read with macOS AudioToolbox") == std::string::npos)
                     throw std::runtime_error("the Software detail does not name the reader: " +
                                              value);
             return r;
         },
         HostStatus::no_filter,
         {"choose a filter file (Choose Filter", "load a Convolver preset"},
         {"Silent"}},
        {"empty, Gain -20 dB",
         [&] {
             HostFilter f(8);
             return host_report(f, facts(stereo, {-20, 1}));
         },
         HostStatus::no_filter,
         {"audio passes at -20", "load a Convolver preset"},
         {"unchanged"}},
        // jrp's GarageBand run: a stereo preset on a mono track plays silence.
        {"stereo filter on a mono track",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, std::nullopt);
             static_cast<void>(f.activate(mono));
             return host_report(f, facts(mono));
         },
         HostStatus::load_failed,
         {"does not suit the host", "plays silence", "Give the plug-in 2 inputs and 2 outputs",
          "load a filter made for 1 input and 1 output", "put Convolver on a stereo track"},
         {"load a Convolver preset"}},
        // The independent review: a stereo track cannot fix a 6-channel filter.
        {"6-channel filter on a mono track",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 6)}, std::nullopt);
             static_cast<void>(f.activate(mono));
             return host_report(f, facts(mono));
         },
         HostStatus::load_failed,
         {"Give the plug-in 6 inputs and 6 outputs"},
         {"stereo track"}},
        {"wrong rate",
         [] {
             HostFilter f(8);
             f.load(HostState{filter(48000, 2)}, std::nullopt);
             static_cast<void>(f.activate({44100, 2, 2}));
             return host_report(f, facts(HostLayout{44100, 2, 2}));
         },
         HostStatus::load_failed,
         {"made for 48000 Hz", "load a filter made for 44100 Hz, or set the host to 48000 Hz"},
         {"stereo track"}},
        // jrp's review of 0.5.5: a rejected first load with Mix 50% is not silent.
        {"failed first load, Mix 50%",
         [&] {
             HostFilter f(8);
             f.reject(HostFailureKind::state_unreadable, "The saved state could not be read.");
             return host_report(f, facts(stereo, {0, .5}));
         },
         HostStatus::load_failed,
         {"could not be read", "the dry signal still plays"},
         {"load a Convolver preset"}},
        {"refused replacement",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, stereo, [](const Configuration&) {});
             try {
                 f.load(HostState{filter(48000, 2)}, stereo, [](const Configuration&) {});
             } catch (const std::invalid_argument&) {
             }
             return host_report(f, facts(stereo));
         },
         HostStatus::load_failed,
         {"previous filter is kept", "made for 48000 Hz", "Playing a filter for 44100 Hz"},
         {}},
        {"playing",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, stereo, [](const Configuration&) {});
             return host_report(f, facts(stereo));
         },
         HostStatus::playing,
         {"Playing a filter for 44100 Hz, 2 in, 2 out."},
         {"preset menu"}},
        {"not checked yet",
         [] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, std::nullopt);
             return host_report(f, facts(std::nullopt));
         },
         HostStatus::unchecked,
         {"is loaded", "checked against the host's sample rate and channels", "has not started"},
         {}},
        {"fault",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, stereo, [](const Configuration&) {});
             HostFaults totals;
             totals.rejected_input = 3;
             static_cast<void>(f.observe(totals));
             return host_report(f, facts(stereo));
         },
         HostStatus::fault,
         {"Playing a filter", "invalid samples", "3 times"},
         {}},
    };
    for (const auto& c : all) {
        const auto r = c.make();
        const std::string name = c.name;
        require(r.status.status == c.status, name + ": status " + r.status_name);
        require(r.status_name == host_status_names[static_cast<std::size_t>(c.status)],
                name + ": status name");
        for (const auto& part : c.explained)
            require(contains(r.explanation, part),
                    name + ": explanation lacks \"" + part + "\": " + r.explanation);
        for (const auto& part : c.absent)
            require(!contains(r.explanation, part),
                    name + ": explanation has \"" + part + "\": " + r.explanation);
        // The window shows the same status line as the Status parameter.
        require(!r.status.text.empty() && r.status.text.size() <= max_status_text,
                name + ": status line");
        require(contains(r.title, "Convolver ") && contains(r.title, "Test #7"), name + ": title");
        const auto text = report_text(r);
        require(contains(text, r.title) && contains(text, r.explanation) &&
                    contains(text, "Status: " + r.status_name + ". " + r.status.text) &&
                    contains(text, "Software: Convolver "),
                name + ": report text");
        const auto fields = report_fields(r);
        for (const char* key : {"status", "status name", "summary", "title", "explanation",
                                "details", "log path", "log written"}) {
            bool found = false;
            for (const auto& [k, v] : fields)
                found = found || k == key;
            require(found, name + ": no field " + key);
        }
        std::cout << name << ": " << r.status_name << ". " << r.explanation << "\n";
    }

    // Details.
    HostFilter f(8);
    f.load(HostState{filter(44100, 2)}, stereo, [](const Configuration&) {});
    const auto r = host_report(f, facts(stereo, {-6, .25}));
    require(detail(r, "Host") == "44100 Hz, 2 in, 2 out, blocks of up to 512 frames",
            "host detail");
    require(contains(detail(r, "Filter"), "44100 Hz, 2 in, 2 out, 2 paths, longest 3 taps") &&
                contains(detail(r, "Filter"), "request 1, from a saved project or preset"),
            "filter detail: " + detail(r, "Filter"));
    require(detail(r, "Settings") == settings_words({-6, .25}, true), "settings detail");
    require(contains(detail(r, "Latency"), "256 frames (5.8 ms), tail 2 frames"),
            "latency detail: " + detail(r, "Latency"));
    require(!contains(detail(r, "Software"), "Session:"), "software detail");
    require(detail(host_report(HostFilter(8), facts(std::nullopt)), "Host") == "not started yet",
            "host detail before start");
    require(!contains(r.explanation, "is chosen") && !contains(detail(r, "Latency"), "chosen"),
            "a running latency is reported as waiting");

    // PLUG-10: a Latency chosen but not yet in use (not applied yet, or it
    // could not be), and the window says so.
    const auto waiting = host_report(f, facts(stereo, {0, 1, 1024}));
    require(contains(waiting.explanation, "Latency 1024 frames is chosen but not in use yet, so "
                                          "the plug-in keeps 256 frames for now."),
            "waiting latency explanation: " + waiting.explanation);
    require(detail(waiting, "Latency") ==
                "256 frames (5.8 ms), tail 2 frames (0.0 ms); 1024 frames chosen, not in use yet",
            "waiting latency detail: " + detail(waiting, "Latency"));
}

std::optional<std::string> log_variable() {
    if (const char* value = std::getenv("CONVOLVER_LOG"))
        return std::string(value);
    return std::nullopt;
}
void set_log_variable(const std::optional<std::string>& value) {
#ifdef _WIN32
    _putenv_s("CONVOLVER_LOG", value ? value->c_str() : "");
#else
    if (value)
        setenv("CONVOLVER_LOG", value->c_str(), 1);
    else
        unsetenv("CONVOLVER_LOG");
#endif
}

// Copy diagnostics: the report and this instance's own lines from the log.
void diagnostics() {
    const auto folder = std::filesystem::temp_directory_path() / "convolver-host-report-test";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto path = folder / "Convolver.log";
    const auto earlier = log_variable();
    set_log_variable(path.string());
    {
        // Lines from another process and another instance, which are left out.
        std::ofstream other(path, std::ios::binary);
        other << "2026-10-25 10:00:00 Convolver 0.6.0 Test #7 in other (process 1): not mine\n";
    }
    host_log("Test #7", "first of mine");
    host_log("Test #70", "another instance");
    host_log("Test #7", "second of mine", false);
    HostFilter f(8);
    auto r = host_report(f, facts(std::nullopt));
    const auto text = diagnostics_text(r, "Test #7");
    require(contains(text, report_text(r)), "diagnostics lack the report");
    require(contains(text, ": first of mine\n") && contains(text, ": second of mine\n") &&
                contains(text, "Session: Convolver "),
            "diagnostics lack this instance's lines:\n" + text);
    require(!contains(text, "not mine") && !contains(text, "another instance"),
            "diagnostics hold other lines:\n" + text);
    require(contains(text, "first of mine") &&
                text.find("first of mine") < text.find("second of mine"),
            "log lines out of order");
    require(r.log_path == path.string() && contains(detail(r, "Log"), path.string()),
            "log detail: " + detail(r, "Log"));
    // At most the last lines asked for.
    for (int i = 0; i < 50; ++i)
        host_log("Test #7", "line " + std::to_string(i));
    const auto recent = host_log_recent("Test #7", 5);
    require(contains(recent, ": line 49\n") && contains(recent, ": line 45\n") &&
                !contains(recent, ": line 44\n"),
            "recent lines:\n" + recent);
#ifndef _WIN32
    // The log turned off on purpose.
    set_log_variable(std::string());
    r = host_report(f, facts(std::nullopt));
    require(contains(detail(r, "Log"), "turned off"), "log turned off: " + detail(r, "Log"));
    require(contains(diagnostics_text(r, "Test #7"), "(none found)"), "no lines without a log");
#endif
    set_log_variable(earlier);
    std::filesystem::remove_all(folder);
}
// The log's start line gives the same corrective action as the window.
void log_agrees() {
    const HostLayout mono{44100, 1, 1};
    HostFilter f(8);
    f.load(HostState{filter(44100, 2)}, std::nullopt);
    static_cast<void>(f.activate(mono));
    const auto line = started_line(f, mono, 512);
    require(
        contains(line, "put Convolver on a stereo track") &&
            contains(host_report(f, facts(mono)).explanation, "put Convolver on a stereo track"),
        "log line lacks the mono-track hint: " + line);
    const auto hint = std::string("put Convolver on a stereo track");
    const auto& explained = host_report(f, facts(mono)).explanation;
    require(explained.find(hint) == explained.rfind(hint), "hint given twice: " + explained);
}
} // namespace

int main() {
    try {
        cases();
        log_agrees();
        diagnostics();
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << "\n";
        return 1;
    }
    std::cout << "host report tests passed\n";
    return 0;
}
