// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PLUG-34: what Convolver's window does, apart from how a system draws it
// (docs/project/plans/2026-11-10-windows-linux-plugin-windows.md#what-comes-first).
// Each system's window (AppKit on macOS, Win32 on Windows) reads the
// report's fields (report_fields() in host_report.hpp), asks this file what
// to show and what a user's action means, and keeps only its controls and
// layout, so every window behaves the same. Control thread only (a window's
// main thread): never from audio processing.
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace convolver::view {
// The report's fields by name, as report_fields() gives them.
using ReportFields = std::map<std::string, std::string, std::less<>>;
ReportFields report_map(const std::vector<std::pair<std::string, std::string>>& fields);

// How the status is drawn beside its word, which is always written too, so
// the colour and symbol are never the only signal.
enum class StatusTone {
    plain,    // No filter, Waiting, Bypassed, or no report.
    positive, // Active.
    failure,  // Load failed.
    warning,  // Fault.
};
enum class StatusSymbol { none, active, failed, waiting, fault, bypassed, empty };

// The button that carries out the fix, highlighted; never the default button,
// so Return still goes to the host.
enum class WindowAction { none, choose_filter, copy_diagnostics };

// One Audio fact: "Host sample rate", "44100 Hz", a note after the value
// (in red after a warning sign when it states a problem) and its help.
struct AudioFact {
    std::string label, value, note, help;
    bool problem = false;
    bool operator==(const AudioFact&) const = default;
};
// One "Label: value" line of the folded file, software and log section.
struct ReferenceLine {
    std::string label, value;
    bool operator==(const ReferenceLine&) const = default;
};

// Everything the window shows, from one report. Equal contents need no
// redraw, so a refresh keeps a selection in the text.
struct WindowContent {
    bool available = false; // False: the plug-in gave no report.
    int status = -1;        // HostStatus as a number; -1 without a report.
    std::string title, status_name;
    StatusTone tone = StatusTone::plain;
    StatusSymbol symbol = StatusSymbol::none;
    // DIAG-2's message: the problem (in a box tinted with the status colour
    // when there is one), what plays, its cause and fix; then a note.
    std::string problem, plays, cause, fix, note;
    WindowAction action = WindowAction::none;
    // The heading: the filter's file name, and under it a refused file or the
    // drop hint.
    std::string filter_name, filter_note;
    // The filter's own facts under the name; shown only with a filter.
    bool facts_shown = false;
    std::string filter_rate, filter_channels, filter_length, filter_taps, filter_speakers;
    std::vector<AudioFact> audio;
    std::vector<ReferenceLine> reference;
    // PLUG-23: why Mix has no effect; empty while it applies.
    std::string mix_note;
    int sample_rate = 0; // The host's, for the Latency row; 0 before it starts.
    bool latency_waits = false;
    std::string log_path;
    // The playing filter's file and the files it was read from, one per
    // line, for Reload and reloading when they change.
    std::string filter_path, filter_files;

    bool message_shown() const {
        return !problem.empty() || !plays.empty() || !cause.empty() || !fix.empty() ||
               !note.empty();
    }
    bool boxed() const {
        return !problem.empty();
    }
    bool operator==(const WindowContent&) const = default;
};

// What to show for `report` (nullptr: the plug-in could not give one).
// `can_load` says whether this window can load a file (Choose…, a drop): it
// adds the drop hint and keeps the Choose action.
WindowContent window_content(const ReportFields* report, bool can_load);

// The time a Latency choice of `frames` adds at the host's rate ("adds 5.3
// ms"), and "not in use yet" while `waits`; empty when there is nothing to
// say (no rate yet and not waiting).
std::string latency_note(double frames, int sample_rate, bool waits);

// A value typed in a setting's box: a decimal comma reads as a point and a
// minus sign as a hyphen, whatever the host's locale; a unit after the
// number ("-6 dB") is ignored. Clamped to [minimum, maximum]; nullopt when
// the text does not start with a finite number.
std::optional<double> typed_value(std::string_view text, double minimum, double maximum);

// Choose… in two steps: start() records a request (forgetting any earlier
// one) for the file dialog; finish() says whether its answer is still
// wanted. Closing the window, or the view leaving it, calls stop(): the host
// may then dispose of the plug-in while the dialog is still open.
class ChooseRequests {
  public:
    std::uint64_t start() {
        pending_ = ++last_;
        return pending_;
    }
    // Whether `request` is the pending one; it is then no longer pending.
    bool finish(std::uint64_t request) {
        if (request == 0 || request != pending_)
            return false;
        pending_ = 0;
        return true;
    }
    void stop() {
        pending_ = 0;
    }
    bool pending() const {
        return pending_ != 0;
    }

  private:
    std::uint64_t pending_ = 0, last_ = 0;
};

// One entry of the Recent menu: "room.cfg — Filters" (the file's name and
// its folder's, since several folders often hold a filter.cfg), and the
// path it loads, also its tooltip.
struct RecentEntry {
    std::string title, path;
    bool operator==(const RecentEntry&) const = default;
};
// The menu's entries for `files` (recent_files(), UTF-8 paths, newest
// first), leaving out files that are no longer there.
std::vector<RecentEntry> recent_entries(const std::vector<std::string>& files);

// Reload When the File Changes: follows the filter that plays, as the report
// names it, with its files' modification times and sizes when it was
// loaded. Once a second the window asks due(): the files changed and the
// change has stayed the same for one check, so a file still being written
// is not read half way. The window keeps the timer, the checkbox and the
// loading.
class FileWatch {
  public:
    // The playing filter as the report gives it now (empty strings when
    // there is none). A new list of files (one UTF-8 path per line) starts
    // watching them as they are now; the same list keeps what was recorded.
    void follow(std::string path, std::string files);
    const std::string& path() const {
        return path_;
    }
    const std::string& files() const {
        return text_;
    }
    // Takes the times and sizes as they are now as what plays: after a load
    // (what was read is what plays) or when watching is turned on (changes
    // made while it was off are not reloaded).
    void settle();
    // One check: whether the filter's files changed since it was loaded and
    // the change has stayed the same since the last check, with every file
    // still there. A moved or deleted file is not reloaded (the filter that
    // plays stays), and its change is then taken as seen.
    bool due();

  private:
    struct Stamp {
        std::filesystem::file_time_type modified{};
        std::uintmax_t size = 0;
        bool operator==(const Stamp&) const = default;
    };
    // Each watched file's stamp, or nullopt when it is not there.
    using Stamps = std::vector<std::optional<Stamp>>;
    Stamps stamps() const;
    std::string path_, text_;
    std::vector<std::filesystem::path> watched_;
    Stamps loaded_;                 // When it was loaded.
    std::optional<Stamps> changed_; // A change waiting to settle.
};
} // namespace convolver::view
