// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-9: the editor report (portable/host_report.hpp), one case per kind of
// state a plug-in window shows: its status, the full explanation with what
// to change, the Audio facts and reference lines, the fields an adapter sends, the plain text and
// the Copy diagnostics text with this instance's Convolver log lines.
#include "cases.hpp"
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
    f.latency_frames = settings.latency; // In use, as chosen.
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
         {"No filter is loaded.", "has not sent its audio settings",
          "Fix: Load a Convolver preset"},
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
         {"Fix: Choose an IR file, .cfg, configuration list or Convolver .vstpreset",
          "load a Convolver preset"},
         {"Silent"}},
        {"empty, Gain -20 dB",
         [&] {
             HostFilter f(8);
             return host_report(f, facts(stereo, {-20, 1}));
         },
         HostStatus::no_filter,
         {"audio passes at -20", "Load a Convolver preset"},
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
         {"The saved filter has 2 inputs and 2 outputs, and the host gives the plug-in 1 input "
          "and 1 output. Convolver plays silence.",
          "Fix: Give the plug-in 2 inputs and 2 outputs",
          "load a filter made for 1 input and 1 output",
          "Note: Hosts run an effect with one channel on a mono track",
          "put Convolver on a stereo track"},
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
         {"The saved filter is for 48000 Hz, and the host runs at 44100 Hz.",
          "Cause: Convolver never resamples",
          "Fix: Load a filter made for 44100 Hz, or set the host's project or audio device to "
          "48000 Hz."},
         {"stereo track"}},
        // jrp's review of 0.5.5: the report says what a rejected first load at
        // Mix 50% plays. PLUG-23 (jrp, 7 November 2026): with no filter kept
        // there is no dry signal, so that is silence.
        {"failed first load, Mix 50%",
         [&] {
             HostFilter f(8);
             f.reject(HostFailureKind::state_unreadable, {"The saved state could not be read."});
             return host_report(f, facts(stereo, {0, .5}));
         },
         HostStatus::load_failed,
         {"The saved state could not be read. Convolver plays silence."},
         {"load a Convolver preset", "only the dry signal"}},
        // A kept filter with equal counts that does not suit the host still
        // passes the dry signal at Mix 50%.
        {"wrong rate, Mix 50%",
         [] {
             HostFilter f(8);
             f.load(HostState{filter(48000, 2)}, std::nullopt);
             static_cast<void>(f.activate({44100, 2, 2}));
             return host_report(f, facts(HostLayout{44100, 2, 2}, {0, .5}));
         },
         HostStatus::load_failed,
         {"With Mix at 50% wet, only the dry signal plays: the input at -6.0 dB."},
         {"no dry signal"}},
        {"refused replacement",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
             try {
                 f.load(HostState{filter(48000, 2)}, stereo, [](const HostPlayback&) {});
             } catch (const std::invalid_argument&) {
             }
             return host_report(f, facts(stereo));
         },
         HostStatus::load_failed,
         {"The filter is for 48000 Hz, and the host runs at 44100 Hz. The previous filter keeps "
          "playing."},
         {}},
        // PLUG-22: the window's heading names the filter and the Audio
        // section describes it, so nothing is said under the status.
        {"active",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
             return host_report(f, facts(stereo));
         },
         HostStatus::active,
         {},
         {"A filter for", "preset menu", "Fix:"}},
        {"bypassed",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
             auto bypassed = facts(stereo);
             bypassed.bypassed = true;
             return host_report(f, bypassed);
         },
         HostStatus::bypassed,
         {"The input plays unfiltered. Turn bypass off to hear the filter."},
         {"silent", "Fix:"}},
        {"bypassed, unequal counts",
         [&] {
             const HostLayout two_to_four{44100, 2, 4, 0, 0};
             Configuration c{44100, 2, 4, 0, {}, {}, {}};
             for (std::size_t i = 0; i < 4; ++i)
                 c.paths.push_back({{1.f}, {{i % 2, 1}}, {{i, 1}}});
             HostFilter f(8);
             f.load(HostState{c}, two_to_four, [](const HostPlayback&) {});
             auto bypassed = facts(two_to_four);
             bypassed.bypassed = true;
             return host_report(f, bypassed);
         },
         HostStatus::bypassed,
         {"The outputs are silent: with 2 in, 4 out, the filter has no unfiltered signal"},
         {"plays unfiltered"}},
        {"bypassed without a filter",
         [&] {
             auto bypassed = facts(stereo);
             bypassed.bypassed = true;
             return host_report(HostFilter(8), bypassed);
         },
         HostStatus::no_filter,
         {"No filter"},
         {"bypass"}},
        {"waiting",
         [] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, std::nullopt);
             return host_report(f, facts(std::nullopt));
         },
         HostStatus::unchecked,
         {"Waiting for audio settings from the host", "fits the host's sample rate and channels"},
         {"has not sent"}},
        {"fault",
         [&] {
             HostFilter f(8);
             f.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
             HostFaults totals;
             totals.rejected_input = 3;
             static_cast<void>(f.observe(totals));
             return host_report(f, facts(stereo));
         },
         HostStatus::fault,
         {"The host sent invalid samples (3 times). The filter keeps playing.",
          "Fix: Bypass the plug-ins before Convolver", "Note: The status shows Fault until"},
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
        for (const char* key :
             {"status",      "status name",   "summary",     "title",       "explanation",
              "problem",     "plays",         "cause",       "fix",         "note",
              "action",      "filter name",   "filter note", "filter path", "filter files",
              "sample rate", "latency waits", "mix note",    "audio",       "reference",
              "log path",    "log written"}) {
            bool found = false;
            for (const auto& [k, v] : fields)
                found = found || k == key;
            require(found, name + ": no field " + key);
        }
        std::cout << name << ": " << r.status_name << ". " << r.explanation << "\n";
    }

    // The Audio facts, in plain words, and the reference lines.
    HostFilter f(8);
    f.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
    const auto r = host_report(f, facts(stereo, {-6, .25}));
    const auto check = [](const HostFact& got, const std::string& value, const std::string& note) {
        require(got.value == value && got.note == note && !got.problem && !got.help.empty(),
                got.label + ": \"" + got.value + "\" \"" + got.note + "\"");
    };
    // jrp, 7 November 2026: short labelled values ("Host sample rate"), not
    // prose; the filter's own values are under its name, so the Audio
    // section neither repeats its rate and speakers nor lists its length.
    check(fact(r, "Host sample rate"), "44100 Hz", "");
    check(fact(r, "Host channels"), "2 in, 2 out", "");
    check(fact(r, "Routing"), "In 1 → Out 1, In 2 → Out 2", "");
    require(!has_fact(r, "Filter length"), "the filter's length repeated as a fact");
    // PLUG-22: the Latency setting shows the time it adds, so the Audio
    // section does not repeat it.
    require(!has_fact(r, "Added delay"), "the latency repeated as a fact");
    check(fact(r, "Ring-out"), "2 frames", "");
    // The identity paths have peak 1.75 (1 + .5 + .25): +4.9 dB. Gain
    // scales only the filtered path, which Mix then adds to the dry input:
    // at Gain -6 dB and Mix 25%, .25 * .501 * 1.75 + .75 = .97.
    check(fact(r, "Loudest peak"), "+4.9 dB", "(cannot clip at this Gain and Mix)");
    check(fact(r, "Host block size"), "up to 512 frames", "(11.6 ms)");
    // jrp, 7 November 2026: the filter's own facts, under its name in the
    // window; the Audio section gives the host's.
    const auto fields = report_fields(r);
    const auto field = [&](const std::string& name) {
        for (const auto& [key, value] : fields)
            if (key == name)
                return value;
        throw std::runtime_error("no report field " + name);
    };
    require(r.filter_rate == "44100 Hz" && r.filter_channels == "2 in → 2 out" &&
                r.filter_length == "0.1 ms" && r.filter_taps == "3 taps" &&
                r.filter_speakers.empty() && field("filter taps") == "3 taps" &&
                field("filter rate") == "44100 Hz" && field("filter channels") == "2 in → 2 out" &&
                field("filter length") == "0.1 ms" && field("filter speakers").empty(),
            "filter facts: " + r.filter_rate + " | " + r.filter_channels + " | " + r.filter_length +
                " | " + r.filter_speakers);
    {
        // Known before the host starts, and with the speakers it names.
        auto labelled = filter(48000, 2);
        labelled.channel_mask = 0x3;
        HostFilter waiting(8);
        waiting.load(HostState{labelled}, std::nullopt);
        const auto w = host_report(waiting, facts(std::nullopt));
        require(
            w.filter_rate == "48000 Hz" && w.filter_channels == "2 in → 2 out" &&
                w.filter_speakers == "Front left, front right" &&
                contains(fact(w, "Host sample rate").value,
                         "not known until the host sends its audio settings") &&
                fact(w, "Host sample rate").note.empty() && fact(w, "Host channels").note.empty(),
            "filter facts before the host starts: " + w.filter_rate + " | " + w.filter_speakers);
        // PLUG-22: the identity a saved name is matched against: 16 hex
        // digits, the same for the same filter, different for another.
        require(r.filter_identity.size() == 16 &&
                    r.filter_identity.find_first_not_of("0123456789abcdef") == std::string::npos &&
                    field("filter identity") == r.filter_identity &&
                    host_report(f, facts(stereo)).filter_identity == r.filter_identity &&
                    w.filter_identity.size() == 16 && w.filter_identity != r.filter_identity,
                "filter identity: " + r.filter_identity + " | " + w.filter_identity);
        auto quieter = labelled;
        quieter.paths.at(0).impulse.at(0) *= .5f;
        HostFilter other(8);
        other.load(HostState{quieter}, std::nullopt);
        require(host_report(other, facts(std::nullopt)).filter_identity != w.filter_identity,
                "a changed sample keeps the identity");
        // jrp's review of c496cba: two 1024-tap filters that differ only in
        // the middle tap had the same identity when only the ends were
        // hashed. Every sample counts now.
        Configuration long_one{48000, 1, 1, 0, {}, {}, {}};
        long_one.paths.push_back({std::vector<float>(1024, .001f), {{0, 1}}, {{0, 1}}});
        auto middle = long_one;
        middle.paths[0].impulse[512] = .002f;
        require(configuration_identity(long_one) != configuration_identity(middle) &&
                    configuration_identity(long_one) ==
                        configuration_identity(Configuration(long_one)),
                "a middle tap does not change the identity");
        HostFilter cached(8);
        cached.load(HostState{middle}, std::nullopt);
        require(cached.filter_identity() == configuration_identity(middle) &&
                    HostFilter(8).filter_identity() == 0,
                "the accepted filter's identity is not cached");
        const auto none = host_report(HostFilter(8), facts(std::nullopt));
        require(none.filter_rate.empty() && none.filter_channels.empty() &&
                    none.filter_length.empty() && none.filter_speakers.empty() &&
                    none.filter_identity.empty(),
                "filter facts without a filter");
    }
    require(r.audio.front().label == "Host sample rate" &&
                r.audio.back().label == "Host block size",
            "Audio order");
    const auto loud = host_report(f, facts(stereo));
    require(fact(loud, "Loudest peak").note == "(Gain −4.9 dB or lower rules out clipping)",
            "loudest peak at 0 dB: " + fact(loud, "Loudest peak").note);
    // The Gain that rules out clipping does not depend on Mix (above 0):
    // at Mix 50% and Gain -3 dB, .5 * .708 * 1.75 + .5 = 1.12 can clip, and
    // at Mix 25% it still takes -4.9 dB. At Mix 0 nothing can clip.
    for (const HostSettings s : {HostSettings{-3, .5}, HostSettings{0, .25}})
        require(fact(host_report(f, facts(stereo, s)), "Loudest peak").note ==
                    "(Gain −4.9 dB or lower rules out clipping)",
                "loudest peak at gain " + std::to_string(s.gain_db) + ", mix " +
                    std::to_string(s.mix));
    require(fact(host_report(f, facts(stereo, {24, 0})), "Loudest peak").note ==
                "(cannot clip at this Gain and Mix)",
            "loudest peak at mix 0");
    // PLUG-10: the note follows the gain that plays, not the Gain setting:
    // No clipping cannot clip at any Gain; Set at -30 dB cannot either, but
    // Estimate plays the filter's own figure (its bound here: f has no
    // estimate), so the note names the mode.
    HostSettings moded{-30, 1, 0, GainMode::no_clipping};
    require(fact(host_report(f, facts(stereo, moded)), "Loudest peak").note ==
                "(cannot clip in this Gain mode and Mix)",
            "loudest peak in No clipping: " +
                fact(host_report(f, facts(stereo, moded)), "Loudest peak").note);
    moded.gain_mode = GainMode::set;
    require(fact(host_report(f, facts(stereo, moded)), "Loudest peak").note ==
                "(cannot clip at this Gain and Mix)",
            "loudest peak at -30 dB in Set");
    HostFilter estimated(8);
    estimated.set_estimating(true);
    estimated.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
    moded.gain_mode = GainMode::estimate;
    const auto estimate_note = fact(host_report(estimated, facts(stereo, moded)), "Loudest peak");
    require(estimated.gains().estimate_db &&
                *estimated.gains().estimate_db > *estimated.gains().no_clipping_db &&
                estimate_note.note == "(Gain mode No clipping rules out clipping)",
            "loudest peak in Estimate: " + estimate_note.note);
    require(r.filter_name == "Restored filter" && r.filter_note.empty(),
            "filter row: " + r.filter_name + " / " + r.filter_note);
    require(reference(r, "Filter file") == "a saved project or preset, request 1",
            "filter file: " + reference(r, "Filter file"));
    require(r.settings == settings_words({-6, .25}, HostDrySignal::present), "settings words");
    require(!contains(reference(r, "Software"), "Session:"), "software line");
    require(
        contains(report_text(r), "Settings: " + r.settings + "\n") &&
            contains(report_text(r), "Host sample rate: 44100 Hz\n") &&
            contains(report_text(r), "Filter values: 44100 Hz, 2 in → 2 out, 0.1 ms (3 taps)\n") &&
            contains(report_text(r), "Filter file: a saved project or preset, request 1\n"),
        "report text:\n" + report_text(r));
    // Active: the status word says it; nothing is wrong, so there is no
    // problem, fix or action, and the heading and Audio section say what
    // plays (PLUG-22).
    require(r.plays.empty() && r.problem.empty() && r.fix.empty() && r.action.empty() &&
                r.explanation.empty(),
            "active message: " + r.explanation);

    // Before the host starts: what is known.
    const auto unstarted = host_report(HostFilter(8), facts(std::nullopt));
    require(contains(fact(unstarted, "Host sample rate").value,
                     "not known until the host sends its audio settings") &&
                fact(unstarted, "Routing").value == "none" &&
                !has_fact(unstarted, "Loudest peak") && !has_fact(unstarted, "Host block size") &&
                !contains(report_text(unstarted), "Filter values:"),
            "facts before start");
    require(unstarted.filter_name == "None" && unstarted.problem.empty() &&
                unstarted.plays ==
                    "No filter is loaded. The host has not sent its audio settings yet.",
            "empty filter row");

    // A refused file while the previous filter plays: the problem shows in
    // the fact, naming the refused file, and the file in the row.
    HostFilter refused(8);
    refused.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
    try {
        refused.load(
            HostState{filter(48000, 2)}, stereo, [](const HostPlayback&) {},
            HostRequest{HostFilterSource::file, "/filters/room-48k.cfg"});
    } catch (const std::invalid_argument&) {
    }
    const auto rr = host_report(refused, facts(stereo));
    require(fact(rr, "Host sample rate").problem &&
                fact(rr, "Host sample rate").note == "the refused file is for 48000 Hz",
            "refused rate: " + fact(rr, "Host sample rate").note);
    require(rr.filter_note == "room-48k.cfg was refused." &&
                reference(rr, "Refused file") == "/filters/room-48k.cfg, request 2",
            "refused file: " + rr.filter_note);
    require(
        contains(report_text(rr), "Host sample rate: 44100 Hz; the refused file is for 48000 Hz\n"),
        "refused report text:\n" + report_text(rr));
    // VST3-11: the window's Reload and its watch on the files take the
    // accepted file's path and files, never a refused one's or saved state's.
    require(rr.filter_path.empty() && rr.filter_files.empty(), "path from saved state");
    HostFilter chosen(8);
    HostRequest from_file{HostFilterSource::file, "/filters/room.cfg"};
    from_file.files = {"/filters/room.cfg", "/filters/left.wav", "/filters/right.wav"};
    chosen.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {}, from_file);
    try {
        chosen.load(
            HostState{filter(48000, 2)}, stereo, [](const HostPlayback&) {},
            HostRequest{HostFilterSource::file, "/filters/room-48k.cfg"});
    } catch (const std::invalid_argument&) {
    }
    const auto rc = host_report(chosen, facts(stereo));
    require(rc.filter_path == "/filters/room.cfg" && rc.filter_files == from_file.files,
            "accepted file's path and files: " + rc.filter_path);
    // PLUG-22: a file's name keeps its extension; saved state shows the
    // name saved beside it, and the generic words only without one.
    require(rc.filter_name == "room.cfg", "file name: " + rc.filter_name);
    HostFilter named(8);
    HostRequest saved_name;
    saved_name.name = "Living room 48k.cfg";
    named.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {}, saved_name);
    require(host_report(named, facts(stereo)).filter_name == "Living room 48k.cfg" &&
                host_report(named, facts(stereo)).filter_path.empty(),
            "saved name: " + host_report(named, facts(stereo)).filter_name);
    HostFilter unnamed(8);
    unnamed.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
    require(host_report(unnamed, facts(stereo)).filter_name == "Restored filter",
            "no saved name: " + host_report(unnamed, facts(stereo)).filter_name);
    std::string files;
    for (const auto& [key, value] : report_fields(rc))
        if (key == "filter files")
            files = value;
    require(files == "/filters/room.cfg\n/filters/left.wav\n/filters/right.wav\n",
            "filter files field: " + files);
    // A stereo filter the host started on a mono track: not playing.
    HostFilter idle(8);
    idle.load(HostState{filter(44100, 2)}, std::nullopt);
    static_cast<void>(idle.activate({44100, 1, 1}));
    const auto ri = host_report(idle, facts(HostLayout{44100, 1, 1}));
    require(fact(ri, "Host channels").problem &&
                fact(ri, "Host channels").note == "the filter has 2 in, 2 out" &&
                fact(ri, "Routing").note == "(not playing)" && ri.filter_note.empty(),
            "mono track facts: " + report_text(ri));

    // PLUG-10: a Latency chosen but not yet in use (not applied yet, or it
    // could not be), and the window says so.
    require(!contains(r.explanation, "is chosen"), "a latency in use reads as waiting");
    auto waiting_facts = facts(stereo, {0, 1, 1024});
    waiting_facts.latency_frames = 256;
    const auto waiting = host_report(f, waiting_facts);
    require(waiting.note.starts_with("Latency 1024 frames is chosen but not in use yet, so "
                                     "the plug-in keeps 256 frames for now.") &&
                contains(waiting.explanation, "\nNote: Latency 1024 frames"),
            "waiting latency explanation: " + waiting.explanation);
    require(waiting.latency_waits && !r.latency_waits && waiting.sample_rate == 44100,
            "latency waits field");
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
// DIAG-2: the window's action button for each kind of message: Choose
// Filter… where a new filter puts it right and the window can choose one,
// Copy Diagnostics for a Convolver bug, none otherwise.
void actions() {
    const HostLayout stereo{44100, 2, 2};
    auto choosing = facts(stereo);
    choosing.chooses_files = true;
    HostFilter empty(8);
    require(host_report(empty, choosing).action == "choose filter" &&
                host_report(empty, facts(stereo)).action.empty(),
            "no filter: Choose Filter… only where the window chooses files");
    HostFilter refused(8);
    refused.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
    try {
        refused.load(HostState{filter(48000, 2)}, stereo, [](const HostPlayback&) {});
    } catch (const std::invalid_argument&) {
    }
    require(host_report(refused, choosing).action == "choose filter", "refused file");
    HostFilter unprepared(8);
    try {
        unprepared.load(HostState{filter(44100, 2)}, stereo,
                        [](const HostPlayback&) { throw std::bad_alloc(); });
    } catch (const std::invalid_argument&) {
    }
    const auto prepared = host_report(unprepared, choosing);
    require(prepared.action.empty() && contains(prepared.problem, "could not be prepared") &&
                contains(prepared.fix, "Close other projects or programs"),
            "preparation failure: " + prepared.explanation);
    HostFilter bug(8);
    bug.load(HostState{filter(44100, 2)}, stereo, [](const HostPlayback&) {});
    HostFaults totals;
    totals.processing_failure = 2;
    totals.rejected_input = 1;
    static_cast<void>(bug.observe(totals));
    const auto faulted = host_report(bug, choosing);
    require(faulted.action == "copy diagnostics" &&
                faulted.problem == "The filter failed while processing (2 times)." &&
                contains(faulted.cause, "fault in Convolver") &&
                faulted.note.starts_with("Also: the host sent invalid samples (once). The status "
                                         "shows Fault until"),
            "processing failure: " + faulted.explanation);
    // Faults are counted with no filter too: the window then says no filter
    // plays and still offers to choose one.
    HostFilter unfiltered(8);
    static_cast<void>(unfiltered.activate(stereo));
    HostFaults input;
    input.rejected_input = 3;
    static_cast<void>(unfiltered.observe(input));
    const auto bare = host_report(unfiltered, choosing);
    require(bare.status.status == HostStatus::fault && bare.action == "choose filter" &&
                !contains(bare.explanation, "filter keeps playing") &&
                contains(bare.plays, "No filter") && contains(bare.note, "Choose an IR file"),
            "fault with no filter: " + bare.explanation);
    // A refused file's message names its full path, for the log; the window's
    // problem names only the file.
    const std::string path = "/Users/me/Filters/room.cfg";
    HostFilter named(8);
    named.reject(HostFailureKind::filter_file,
                 {path + ":2:1: the delay line has 3 values", "A cause.", "A fix.", {}},
                 {HostFilterSource::file, path, 1});
    const auto shown = host_report(named, choosing);
    require(shown.problem == "room.cfg:2:1: the delay line has 3 values." &&
                contains(rejected_line(named, true), path + ":2:1:"),
            "refused file's name: " + shown.problem);
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

// PLUG-23: a 2-in, 6-out crossover on REAPER's 6 inputs and 6 outputs: the
// Host channels says which inputs are ignored, and Mix has its own note
// (no dry signal), for the window to show by the Mix row.
void wider_inputs() {
    const HostLayout six{48000, 6, 6};
    Configuration x{48000, 2, 6, 0, {}, {}, {}};
    x.paths.push_back({{1.f}, {{0, 1}}, {{0, 1}}});
    x.paths.push_back({{1.f}, {{1, 1}}, {{5, 1}}});
    HostFilter f(8);
    f.load(HostState{x}, six);
    static_cast<void>(f.activate(six));
    const auto r = host_report(f, facts(six));
    require(r.status.status == HostStatus::active && r.problem.empty(),
            "a crossover on a wider bus is reported as a problem: " + r.explanation);
    const auto& channels = fact(r, "Host channels");
    require(channels.value == "6 in, 6 out" && channels.note == "(inputs 3–6 ignored)" &&
                !channels.problem,
            "Channels: " + channels.value + " " + channels.note);
    require(r.mix_note == "The filter's inputs and outputs differ, so there is no dry signal: Mix "
                          "has no effect, and bypass plays silence.",
            "mix note: " + r.mix_note);
    // An empty instance on the same track has a dry signal; a stereo filter
    // on a stereo track has no Mix note.
    HostFilter empty(8);
    require(host_report(empty, facts(six)).mix_note.empty(), "an empty instance has a mix note");
    HostFilter stereo(8);
    stereo.load(HostState{filter(48000, 2)}, HostLayout{48000, 2, 2});
    const auto rs = host_report(stereo, facts(HostLayout{48000, 2, 2}));
    require(rs.mix_note.empty() &&
                fact(rs, "Host channels").note.find("ignored") == std::string::npos,
            "a stereo filter has a mix note or ignored inputs");
    // Without a filter and with unequal counts, the host's counts decide.
    HostFilter none(8);
    require(contains(host_report(none, facts(HostLayout{48000, 2, 6})).mix_note,
                     "Inputs and outputs differ"),
            "no mix note for an empty instance with unequal counts");
    // jrp, 7 November 2026: the crossover kept at the wrong rate on the
    // six-channel track, and a failed load with no filter kept, have no dry
    // signal either.
    HostFilter kept(8);
    kept.load(HostState{x}, std::nullopt);
    const HostLayout slower{44100, 6, 6};
    static_cast<void>(kept.activate(slower));
    const auto rk = host_report(kept, facts(slower, {0, .5}));
    require(rk.mix_note == r.mix_note && contains(rk.explanation, "Convolver plays silence.") &&
                contains(rk.settings, "mix 100% wet (the filter's inputs and outputs differ"),
            "a kept crossover that does not suit the host: " + rk.mix_note + " | " +
                rk.explanation + " | " + rk.settings);
    HostFilter failed(8);
    failed.reject(HostFailureKind::filter_file, {"The filter file could not be read."});
    const auto rf = host_report(failed, facts(six, {0, .5}));
    require(rf.mix_note == "A load failed and no filter is kept, so there is no dry signal: Mix "
                           "has no effect, and bypass plays silence." &&
                contains(rf.settings, "mix 100% wet (a load failed and no filter is kept"),
            "a failed load with no filter kept: " + rf.mix_note + " | " + rf.settings);
    // The same before the host has started the instance, and on a mono
    // instance (jrp, 7 November 2026).
    for (const auto& host : {std::optional<HostLayout>{}, std::optional{HostLayout{48000, 1, 1}}})
        require(host_report(failed, facts(host, {0, .5})).mix_note == rf.mix_note,
                "a failed load with no filter kept, host " +
                    std::string(host ? "mono" : "unknown") + ": " +
                    host_report(failed, facts(host, {0, .5})).mix_note);
    // An empty instance the adapter could not start, its host no longer
    // known: the report agrees with Status that nothing plays.
    HostFilter unstarted(8);
    unstarted.fail_activation("no memory");
    const auto ru = host_report(unstarted, facts(std::nullopt, {0, .5}));
    require(!ru.mix_note.empty() && contains(ru.settings, "mix 100% wet") &&
                contains(host_status(unstarted, std::nullopt, true, {0, .5}).text, "Silent: "),
            "an unstarted empty instance: " + ru.mix_note + " | " + ru.settings);
}

int main() {
    if (convolver::test::run_cases({
            {"cases", cases},
            {"actions", actions},
            {"log_agrees", log_agrees},
            {"diagnostics", diagnostics},
            {"wider_inputs", wider_inputs},
        }))
        return 1;
    std::cout << "host report tests passed\n";
    return 0;
}
