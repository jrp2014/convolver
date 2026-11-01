// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-9: the editor report (portable/host_report.hpp), one case per kind of
// state a plug-in window shows: its status, the full explanation with what
// to change, the Audio facts and reference lines, the fields an adapter sends, the plain text and
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
const HostFact& fact(const HostReport& r, const std::string& label) {
    for (const auto& f : r.audio)
        if (f.label == label)
            return f;
    throw std::runtime_error("no Audio fact " + label);
}
bool has_fact(const HostReport& r, const std::string& label) {
    for (const auto& f : r.audio)
        if (f.label == label)
            return true;
    return false;
}
std::string reference(const HostReport& r, const std::string& label) {
    for (const auto& [name, value] : r.reference)
        if (name == label)
            return value;
    throw std::runtime_error("no reference line " + label);
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
             const auto software = reference(r, "Software");
             if (software.find("; filter files read with macOS AudioToolbox") == std::string::npos)
                 throw std::runtime_error("the Software line does not name the reader: " +
                                          software);
             return r;
         },
         HostStatus::no_filter,
         {"choose a filter file (Choose… beside Filter", "load a Convolver preset"},
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
                                "headline", "filter name", "filter note", "sample rate",
                                "latency waits", "audio", "reference", "log path", "log written"}) {
            bool found = false;
            for (const auto& [k, v] : fields)
                found = found || k == key;
            require(found, name + ": no field " + key);
        }
        std::cout << name << ": " << r.status_name << ". " << r.explanation << "\n";
    }

    // The Audio facts, in plain words, and the reference lines.
    HostFilter f(8);
    f.load(HostState{filter(44100, 2)}, stereo, [](const Configuration&) {});
    const auto r = host_report(f, facts(stereo, {-6, .25}));
    const auto check = [](const HostFact& got, const std::string& value, const std::string& note) {
        require(got.value == value && got.note == note && !got.problem && !got.help.empty(),
                got.label + ": \"" + got.value + "\" \"" + got.note + "\"");
    };
    check(fact(r, "Sample rate"), "44100 Hz", "(the filter's rate too)");
    check(fact(r, "Channels"), "2 in, 2 out", "");
    check(fact(r, "Routing"), "In 1 → Out 1, In 2 → Out 2", "");
    check(fact(r, "Filter length"), "0.1 ms", "(3 taps)");
    check(fact(r, "Added delay"), "5.8 ms", "(256 frames, reported to the host)");
    check(fact(r, "Ring-out"), "0.0 ms", "after the input stops");
    // The identity paths have peak 1.75 (1 + .5 + .25): +4.9 dB. At Gain
    // -6 dB and Mix 25%, .25 * 1.75 + .75 = 1.19 is below 2 (+6 dB).
    check(fact(r, "Loudest peak"), "+4.9 dB",
          "at most, for full-scale input; it cannot clip at this Gain and Mix");
    check(fact(r, "Host blocks"), "up to 512 frames", "(11.6 ms) per call");
    require(r.audio.front().label == "Sample rate" && r.audio.back().label == "Host blocks",
            "Audio order");
    const auto loud = host_report(f, facts(stereo));
    require(fact(loud, "Loudest peak").note ==
                "at most, for full-scale input; Gain −4.9 dB or lower rules out clipping",
            "loudest peak at 0 dB: " + fact(loud, "Loudest peak").note);
    require(r.filter_name == "From the saved project or preset" && r.filter_note.empty(),
            "filter row: " + r.filter_name + " / " + r.filter_note);
    require(reference(r, "Filter file") == "a saved project or preset, request 1",
            "filter file: " + reference(r, "Filter file"));
    require(r.settings == settings_words({-6, .25}, true), "settings words");
    require(!contains(reference(r, "Software"), "Session:"), "software line");
    require(contains(report_text(r), "Settings: " + r.settings + "\n") &&
                contains(report_text(r), "Sample rate: 44100 Hz (the filter's rate too)\n") &&
                contains(report_text(r), "Filter file: a saved project or preset, request 1\n"),
            "report text:\n" + report_text(r));
    // Playing: the status word says it, so the headline does not repeat it.
    require(r.headline == "A filter for 44100 Hz, 2 in, 2 out.", "headline: " + r.headline);

    // Before the host starts: what is known.
    const auto unstarted = host_report(HostFilter(8), facts(std::nullopt));
    require(contains(fact(unstarted, "Sample rate").value, "not known until the host starts") &&
                fact(unstarted, "Routing").value == "none" &&
                !has_fact(unstarted, "Loudest peak") && !has_fact(unstarted, "Host blocks") &&
                fact(unstarted, "Added delay").value == "256 frames",
            "facts before start");
    require(unstarted.filter_name == "None" && unstarted.headline == unstarted.explanation,
            "empty filter row");

    // A refused file: the problem shows in the fact, the file in the row.
    HostFilter refused(8);
    refused.load(HostState{filter(44100, 2)}, stereo, [](const Configuration&) {});
    try {
        refused.load(
            HostState{filter(48000, 2)}, stereo, [](const Configuration&) {},
            HostRequest{HostFilterSource::file, "/filters/room-48k.cfg"});
    } catch (const std::invalid_argument&) {
    }
    const auto rr = host_report(refused, facts(stereo));
    require(fact(rr, "Sample rate").problem &&
                fact(rr, "Sample rate").note == "the filter is for 48000 Hz",
            "refused rate: " + fact(rr, "Sample rate").note);
    require(rr.filter_note == "room-48k.cfg was refused." &&
                reference(rr, "Refused file") == "/filters/room-48k.cfg, request 2",
            "refused file: " + rr.filter_note);
    require(contains(report_text(rr), "Sample rate: 44100 Hz; the filter is for 48000 Hz\n"),
            "refused report text:\n" + report_text(rr));
    // A stereo filter the host started on a mono track: not playing.
    HostFilter idle(8);
    idle.load(HostState{filter(44100, 2)}, std::nullopt);
    static_cast<void>(idle.activate({44100, 1, 1}));
    const auto ri = host_report(idle, facts(HostLayout{44100, 1, 1}));
    require(
        fact(ri, "Channels").problem && fact(ri, "Channels").note == "the filter has 2 in, 2 out" &&
            fact(ri, "Routing").note == "(this filter is not playing)" && ri.filter_note.empty(),
        "mono track facts: " + report_text(ri));

    // PLUG-10: a Latency chosen but not yet in use (not applied yet, or it
    // could not be), and the window says so.
    require(!contains(r.explanation, "is chosen"), "a latency in use reads as waiting");
    const auto waiting = host_report(f, facts(stereo, {0, 1, 1024}));
    require(contains(waiting.explanation, "Latency 1024 frames is chosen but not in use yet, so "
                                          "the plug-in keeps 256 frames for now."),
            "waiting latency explanation: " + waiting.explanation);
    require(waiting.latency_waits && !r.latency_waits && waiting.sample_rate == 44100,
            "latency waits field");
    require(fact(waiting, "Added delay").note ==
                "(256 frames, reported to the host); 23.2 ms chosen, not in use yet",
            "waiting latency fact: " + fact(waiting, "Added delay").note);
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
    require(r.log_path == path.string() && contains(reference(r, "Log"), path.string()),
            "log line: " + reference(r, "Log"));
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
    require(contains(reference(r, "Log"), "turned off"), "log turned off: " + reference(r, "Log"));
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
