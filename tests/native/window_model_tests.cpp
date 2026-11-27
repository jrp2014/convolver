// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-34: what Convolver's window does on every system (portable/window_model.hpp),
// from the typed report the formats pass: the content shown, the
// Latency note, typed values, Choose… requests, the Recent menu and
// reloading when a filter's files change.
#include "comma_locale.hpp"
#include "convolver/host.hpp"
#include "host_report.hpp"
#include "window_model.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace fs = std::filesystem;
using namespace convolver;
using namespace convolver::view;

namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}

HostReport sample_report() {
    HostReport r;
    r.status.status = HostStatus::load_failed;
    r.status_name = "Load failed";
    r.title = "Convolver 0.13.6, VST3 #1 in REAPER";
    r.problem = "The file was refused.";
    r.plays = "The earlier filter still plays.";
    r.cause = "Its sample rate is 48000 Hz.";
    r.fix = "Choose a filter made for 44100 Hz.";
    r.note = "A note.";
    r.action = "choose filter";
    r.filter_name = "room.cfg";
    r.filter_note = "room-48k.wav was refused.";
    r.filter_rate = "44100 Hz";
    r.filter_channels = "2 in → 2 out";
    r.filter_length = "1.37 s";
    r.filter_taps = "65536 taps";
    r.filter_path = "/filters/room.cfg";
    r.filter_files = {"/filters/room.cfg", "/filters/left.wav"};
    r.audio = {{"Host sample rate", "44100 Hz", "", false, "The host's rate."},
               {"Host channels", "6 in, 2 out", "(the filter has 2 inputs)", true, "Help."}};
    r.sample_rate = 44100;
    r.latency_waits = true;
    r.mix_note = "No dry signal.";
    r.reference = {{"Filter file", "/filters/room.cfg"}, {"Software", "Convolver 0.13.6"}};
    r.log_path = "/logs/convolver.log";
    r.gain_played = "-7.3";
    r.gain_played_db = -7.25;
    return r;
}

void content_tests() {
    const auto fields = sample_report();
    const auto c = window_content(&fields, true);
    require(c.available && c.status == static_cast<int>(HostStatus::load_failed), "status");
    require(c.status_name == "Load failed" && c.tone == StatusTone::failure &&
                c.symbol == StatusSymbol::failed,
            "status look");
    require(c.title == "Convolver 0.13.6, VST3 #1 in REAPER", "title");
    require(c.problem == "The file was refused." && c.plays == "The earlier filter still plays." &&
                c.cause == "Its sample rate is 48000 Hz." &&
                c.fix == "Choose a filter made for 44100 Hz." && c.note == "A note.",
            "message parts");
    require(c.boxed() && c.message_shown(), "a problem is boxed");
    require(c.action == WindowAction::choose_filter, "Choose is the fix's action");
    require(c.filter_name == "room.cfg", "filter name");
    // PLUG-40: after a refusal the Fix says what to do; no drop hint.
    require(c.filter_note == "room-48k.wav was refused.",
            "filter note after a refusal: " + c.filter_note);
    require(c.facts_shown && c.filter_rate == "44100 Hz" && c.filter_channels == "2 in → 2 out" &&
                c.filter_length == "1.37 s" && c.filter_taps == "65536 taps" &&
                c.filter_speakers.empty(),
            "the filter's facts");
    require(c.audio.size() == 2 && c.audio[0].label == "Host sample rate" &&
                c.audio[0].value == "44100 Hz" && c.audio[0].note.empty() && !c.audio[0].problem &&
                c.audio[0].help == "The host's rate." && c.audio[1].problem &&
                c.audio[1].note == "(the filter has 2 inputs)",
            "audio facts");
    require(c.reference.size() == 2 && c.reference[0].label == "Filter file" &&
                c.reference[0].value == "/filters/room.cfg" && c.reference[1].label == "Software",
            "reference lines");
    require(c.mix_note == "No dry signal." && c.sample_rate == 44100 && c.latency_waits,
            "settings facts");
    // PLUG-55: the dimmed Gain slider is placed from the number, not its text.
    require(c.gain_played == "-7.3" && c.gain_played_db == -7.25, "the gain played");
    require(c.log_path == "/logs/convolver.log" && c.filter_path == "/filters/room.cfg" &&
                c.filter_files == "/filters/room.cfg\n/filters/left.wav\n",
            "paths");
    // The same report shows the same content: no redraw.
    require(window_content(&fields, true) == c, "equal content for an equal report");

    // A window that cannot load files gives no Choose action.
    const auto fixed = window_content(&fields, false);
    require(fixed.filter_note == "room-48k.wav was refused." && fixed.action == WindowAction::none,
            "a window without loading");

    // Copy Diagnostics stays as an action either way.
    auto report = sample_report();
    report.action = "copy diagnostics";
    report.filter_note.clear();
    const auto& copy = report;
    const auto c2 = window_content(&copy, false);
    require(c2.action == WindowAction::copy_diagnostics && c2.filter_note.empty(),
            "copy diagnostics");
    // PLUG-44: no drop hint under the name; the Fix and Choose…'s tooltip
    // name dropping a file.
    require(window_content(&copy, true).filter_note.empty(), "a drop hint under the name");

    // Active, nothing to say: no message, no box.
    HostReport active;
    active.status.status = HostStatus::active;
    active.status_name = "Active";
    const auto& quiet = active;
    const auto c3 = window_content(&quiet, true);
    require(c3.tone == StatusTone::positive && c3.symbol == StatusSymbol::active &&
                !c3.message_shown() && !c3.boxed() && !c3.facts_shown && c3.audio.empty(),
            "active and quiet");
    for (const auto& [status, tone, symbol] :
         {std::tuple{HostStatus::no_filter, StatusTone::plain, StatusSymbol::empty},
          std::tuple{HostStatus::unchecked, StatusTone::plain, StatusSymbol::waiting},
          std::tuple{HostStatus::fault, StatusTone::warning, StatusSymbol::fault},
          std::tuple{HostStatus::bypassed, StatusTone::plain, StatusSymbol::bypassed}}) {
        active.status.status = status;
        const auto c4 = window_content(&active, true);
        require(c4.tone == tone && c4.symbol == symbol,
                "status look for " + std::to_string(static_cast<int>(status)));
    }
    // PLUG-52: each status word's tooltip names the word it explains.
    for (const auto& [status, word] :
         {std::pair{HostStatus::active, "Active:"}, std::pair{HostStatus::no_filter, "No filter:"},
          std::pair{HostStatus::load_failed, "Load failed:"},
          std::pair{HostStatus::unchecked, "Waiting:"}, std::pair{HostStatus::fault, "Fault:"},
          std::pair{HostStatus::bypassed, "Bypassed:"}}) {
        active.status.status = status;
        const auto help = window_content(&active, true).status_help;
        require(help.rfind(word, 0) == 0, "status help: " + help);
    }
    require(window_content(nullptr, true).status_help.empty(), "status help without a report");
    // The sliders' tooltips name the click that resets them, and nothing for
    // a setting that is not a slider.
    require(slider_help("Gain", "Ctrl-click").find("Ctrl-click to put back 0 dB.") !=
                    std::string::npos &&
                slider_help("Mix", "Option-click").find("Option-click to put back 100%.") !=
                    std::string::npos &&
                slider_help("Latency", "Option-click").empty(),
            "slider help: " + slider_help("Gain", "Ctrl-click"));

    // PLUG-52: Waiting with a problem (a kept filter that does not suit the
    // host plays silence) is drawn in the warning colour, keeping its clock
    // and its word.
    {
        HostReport kept;
        kept.status.status = HostStatus::unchecked;
        kept.status_name = "Waiting";
        kept.problem = "The saved filter is for 44100 Hz, and the host runs at 48000 Hz.";
        const auto& warned = kept;
        const auto c5 = window_content(&warned, true);
        require(c5.tone == StatusTone::warning && c5.symbol == StatusSymbol::waiting &&
                    c5.status_name == "Waiting" && c5.boxed(),
                "a silent kept filter is a warning");
    }

    // No report: the window says so and how to recover.
    const auto none = window_content(nullptr, true);
    require(!none.available && none.status == -1 && none.status_name == "Not available" &&
                none.title == "Convolver" && none.boxed() && !none.fix.empty() &&
                none.filter_note.empty(),
            "no report");
}

void latency_tests() {
    require(latency_note(256, 48000, false, false) == "adds 5.3 ms",
            latency_note(256, 48000, false, false));
    require(latency_note(256, 48000, true, false) == "adds 5.3 ms, not in use yet", "waiting");
    require(latency_note(256, 0, true, false) == "Not in use yet", "waiting without a rate");
    require(latency_note(256, 0, false, false).empty(), "nothing to say");
    // PLUG-52: none is said too, beside the host's block size.
    require(latency_note(0, 48000, false, false) == "adds no delay" &&
                latency_note(0, 0, false, false) == "adds no delay" &&
                latency_note(0, 0, true, false) == "adds no delay, not in use yet",
            latency_note(0, 0, true, false));
    // AU-19 (jrp's review, 22 November 2026): with the plug-in stopped, the
    // choice is selected for the next start.
    require(latency_note(256, 48000, false, true) == "adds 5.3 ms, selected for the next start",
            latency_note(256, 48000, false, true));
    require(latency_note(256, 0, false, true) == "Selected for the next start",
            "stopped without a rate");
}

void typed_tests() {
    const auto is = [](std::optional<double> got, double want) {
        return got && std::abs(*got - want) < 1e-12;
    };
    require(is(typed_value("-6", -100, 24), -6), "-6");
    require(is(typed_value(" -6,5 ", -100, 24), -6.5), "a decimal comma");
    require(is(typed_value("−3.5", -100, 24), -3.5), "a minus sign");
    require(is(typed_value("-6 dB", -100, 24), -6), "a unit after the number");
    require(is(typed_value("50%", 0, 100), 50), "a percent sign");
    require(is(typed_value("200", -100, 24), 24) && is(typed_value("-1e9", -100, 24), -100),
            "clamped to the range");
    require(!typed_value("", 0, 1) && !typed_value("dB", 0, 1) && !typed_value("nan", 0, 1) &&
                !typed_value("1e999", 0, 1),
            "not a finite number");
}

// PLUG-52: the Gain slider's taper keeps its ends at the parameter's range,
// gives -30 to +12 dB most of the travel, and turns back into the same gain.
void taper_tests() {
    require(gain_position(min_gain_db) == 0 && gain_position(max_gain_db) == 1 &&
                gain_position(-500) == 0 && gain_position(500) == 1 &&
                gain_position(std::nan("")) == 0,
            "taper ends");
    require(gain_position(12) - gain_position(-30) > 0.75, "taper's middle");
    double last = -1;
    for (double db = min_gain_db; db <= max_gain_db; db += 0.5) {
        const double at = gain_position(db);
        require(at > last && std::abs(gain_at_position(at) - db) < 1e-9,
                "taper round trip at " + std::to_string(db));
        last = at;
    }
    require(gain_at_position(0) == min_gain_db && gain_at_position(1) == max_gain_db &&
                gain_at_position(-1) == min_gain_db && gain_at_position(2) == max_gain_db,
            "taper positions beyond the ends");
}

// PLUG-52: the message keeps its height once taller text has shown, up to
// the most it shows before scrolling, and never goes below the minimum.
void slot_tests() {
    require(message_slot(0, 0, 34, 200) == 34 && message_slot(17, 0, 34, 200) == 34,
            "a short message keeps the minimum");
    require(message_slot(120, 34, 34, 200) == 120 && message_slot(0, 120, 34, 200) == 120,
            "the tallest message so far stays");
    require(message_slot(500, 0, 34, 200) == 200 && message_slot(0, 500, 34, 200) == 200,
            "the most before scrolling");
}

// PLUG-52: the note that says a changed file reloads.
void watch_note_tests() {
    require(watch_note((fs::path("filters") / "room.cfg").string(), true) ==
                "Reloads when room.cfg changes.",
            "watch note");
    require(watch_note("/filters/room.cfg", false).empty() && watch_note("", true).empty(),
            "no watch note");
}

void choose_tests() {
    ChooseRequests choose;
    require(!choose.pending() && !choose.finish(0), "nothing pending");
    const auto first = choose.start();
    const auto second = choose.start(); // A newer dialog replaces the first.
    require(!choose.finish(first) && choose.pending(), "an older request is forgotten");
    require(choose.finish(second) && !choose.pending(), "the pending request");
    require(!choose.finish(second), "an answer is taken once");
    const auto third = choose.start();
    choose.stop(); // The window closed.
    require(!choose.finish(third), "a closed window forgets its request");
}

void recent_tests(const fs::path& dir) {
    fs::create_directories(dir / "Filters");
    std::ofstream(dir / "Filters" / "room.cfg") << "x";
    std::ofstream(dir / "near.wav") << "x";
    const auto room = (dir / "Filters" / "room.cfg").string();
    const auto near = (dir / "near.wav").string();
    const auto gone = (dir / "gone.cfg").string();
    const auto entries = recent_entries({room, gone, near});
    require(entries.size() == 2, "missing files are left out");
    require(entries[0].title == "room.cfg — Filters" && entries[0].path == room,
            "name and folder: " + entries[0].title);
    require(entries[1].path == near && entries[1].title.rfind("near.wav — ", 0) == 0,
            "second entry: " + entries[1].title);
}

void touch(const fs::path& file, const std::string& text, fs::file_time_type when) {
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
    fs::last_write_time(file, when);
}

void watch_tests(const fs::path& dir) {
    const auto cfg = dir / "watch.cfg";
    const auto wav = dir / "watch.wav";
    const auto start = fs::file_time_type::clock::now() - std::chrono::hours(1);
    touch(cfg, "a", start);
    touch(wav, "b", start);
    FileWatch watch;
    require(!watch.due(), "nothing followed");
    watch.follow(cfg.string(), cfg.string() + "\n" + wav.string() + "\n");
    require(watch.path() == cfg.string() && !watch.due(), "unchanged files");

    // A change is due once it has stayed the same for one check.
    touch(wav, "bb", start + std::chrono::minutes(1));
    require(!watch.due(), "a change waits one check");
    require(watch.due(), "a settled change is due");
    require(!watch.due(), "then taken as what plays");

    // A file still being written is not read half way.
    touch(wav, "bbb", start + std::chrono::minutes(2));
    require(!watch.due(), "first change");
    touch(wav, "bbbb", start + std::chrono::minutes(3));
    require(!watch.due(), "still changing");
    require(watch.due(), "settled");

    // settle() takes the files as they are: a change made while watching
    // was off is not reloaded.
    touch(cfg, "aa", start + std::chrono::minutes(4));
    watch.settle();
    require(!watch.due() && !watch.due(), "settled by hand");

    // The same list keeps what was recorded; a new list starts again.
    touch(cfg, "aaa", start + std::chrono::minutes(5));
    watch.follow(cfg.string(), cfg.string() + "\n" + wav.string() + "\n");
    require(!watch.due() && watch.due(), "the same list keeps its record");
    watch.follow(wav.string(), wav.string() + "\n");
    require(watch.files() == wav.string() + "\n" && !watch.due() && !watch.due(),
            "a new list starts from now");

    // A deleted file is not reloaded, and its change is then taken as seen.
    fs::remove(wav);
    require(!watch.due() && !watch.due() && !watch.due(), "a deleted file");

    // No filter: nothing is due.
    watch.follow("", "");
    require(!watch.due(), "no filter");
}
} // namespace

int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-window-model-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int status = 0;
    try {
        fs::create_directories(dir);
        content_tests();
        latency_tests();
        convolver::test::in_comma_locale("window content", content_tests);
        convolver::test::in_comma_locale("latency notes", latency_tests);
        typed_tests();
        taper_tests();
        slot_tests();
        watch_note_tests();
        choose_tests();
        recent_tests(dir);
        watch_tests(dir);
        std::cout << "window model checks passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        status = 1;
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
    return status;
}
