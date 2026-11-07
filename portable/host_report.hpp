// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// PLUG-9: what a plug-in window and its Copy diagnostics button show about one
// instance (docs/project/plans/2026-10-24-plugin-editor.md#how-the-window-connects).
// It is built on the control side from the same functions as the Status
// parameter (host_status) and the Convolver log (host_log.hpp), so the
// window, a copied report and the log never disagree. Internal to the
// adapters and their views, which always ship together: the field names are
// not a stable interface.
namespace convolver {
// The Status parameter's value names, in HostStatus order.
inline constexpr std::array<const char*, 6> host_status_names{
    "Active", "No filter", "Load failed", "Not checked yet", "Fault", "Bypassed"};
static_assert(static_cast<std::size_t>(HostStatus::bypassed) == host_status_names.size() - 1);

// What the adapter knows besides its HostFilter, under its control lock.
struct HostReportFacts {
    std::string format;             // The log tag, such as "Audio Unit #2".
    std::optional<HostLayout> host; // As for host_status(): nullopt while not known.
    std::size_t max_frames = 0;     // The host's largest block; 0 if not known.
    HostSettings settings;
    // The latency the processor runs at (settings.latency is the one chosen,
    // not yet in use when they differ), and the tail.
    std::size_t latency_frames = 0, tail_frames = 0;
    bool log_written = true; // host_log_written()
    // PLUG-22: the instance is bypassed (the host's switch or the window's).
    bool bypassed = false;
    // The format's window can load a filter file (the Audio Unit's Choose
    // Filter), so an empty instance is told about it as well as presets.
    bool chooses_files = false;
    // How that window reads sound files, for the Software detail, such as
    // "macOS AudioToolbox"; empty when the format reads none.
    std::string file_reader;
};

// One line of the window's Audio section: a fact about what plays, in plain
// words, with the technical term in `note` where it helps ("1.37 s" and
// "(65536 taps)"), and `help` for a tooltip. `problem` marks a note that says
// what does not match (a refused filter's rate), which the window shows as a
// problem, not by colour alone.
struct HostFact {
    std::string label, value, note;
    bool problem = false;
    std::string help;
};

struct HostReport {
    HostStatusLine status;   // What the Status parameter shows.
    std::string status_name; // Its value's name, such as "Load failed".
    // "Convolver 0.6.0, Audio Unit #2 in REAPER": which instance this is, in the
    // program that runs it (a host's separate plug-in process where it has one).
    std::string title;
    // DIAG-2: what the window shows under the status word, each part on its
    // own: the problem as a sentence (empty when nothing is wrong), what
    // plays, then the cause, the fix and a note (each may be empty), and the
    // window's action for the fix: "choose filter" (Choose Filter…), "copy
    // diagnostics" (Copy Diagnostics) or empty.
    std::string problem, plays, cause, fix, note, action;
    // All of it as text: the problem and what plays, then "Cause: ", "Fix: "
    // and "Note: " lines (advice.hpp), for the text report.
    std::string explanation;
    // The window's heading: the accepted filter's file name, with its
    // extension (for saved state, the name saved beside it, PLUG-22; "From
    // the saved project or preset" when none was; "None"), and a note about
    // the newest request when it was refused.
    std::string filter_name, filter_note;
    // Under the name, the accepted filter's own values (jrp, 7 November
    // 2026), which the Audio section's host values need not match: its rate
    // ("48000 Hz"), channels ("2 in → 2 out"), length ("1.37 s") and the
    // speakers its outputs are labelled for ("Front left, front right";
    // empty when it names none).
    // All empty without a filter.
    std::string filter_rate, filter_channels, filter_length, filter_speakers;
    std::string filter_taps; // "65536 taps": the longest path, for the length's tooltip.
    // VST3-11: the accepted filter's file as chosen (UTF-8; empty when it came
    // from saved state), which the window's Reload reads again, and every
    // file it was read from (HostRequest::files), which the window watches
    // to reload when one changes.
    std::string filter_path;
    std::vector<std::string> filter_files;
    // The Audio section, in the order the window lists it: host sample
    // rate, host channels, routing, ring-out, loudest peak, host block size
    // (each only when known).
    std::vector<HostFact> audio;
    // The settings in words (settings_words()), for the text report; the
    // window shows them as its controls.
    std::string settings;
    // For the window's Latency row: the host's rate (0 before it starts;
    // the whole part of a rate that is not whole hertz, VST3-9),
    // and whether the chosen Latency is not in use yet (latency_waits()).
    int sample_rate = 0;
    bool latency_waits = false;
    // Reference material for problem reports, smaller and folded away in
    // the window: the filter file and request, software, log.
    std::vector<std::pair<std::string, std::string>> reference;
    std::string log_path; // UTF-8; empty when there is none.
    bool log_written = true;
};

HostReport host_report(const HostFilter&, const HostReportFacts&);

// The report as named text values, for a format's transport (an Audio Unit
// property dictionary): status (the HostStatus number), status name,
// summary (the status line), title, explanation, problem, plays, cause, fix,
// note, action, filter name,
// filter note, filter path, filter files (one per line), sample rate (digits, or empty), latency
// waits ("yes" or "no"), audio (one line per fact: label, value, note, "problem" or empty, and
// help, separated by tabs), reference (one "Label: value" line each), log path and log written
// ("yes" or "no").
std::vector<std::pair<std::string, std::string>> report_fields(const HostReport&);

// The report as plain text: title, status, explanation, the Audio facts
// ("Label: value note"), the settings and the reference lines.
std::string report_text(const HostReport&);

// For Copy diagnostics: report_text() and this instance's recent lines from
// the Convolver log (host_log_recent), ready to paste into a report. Reads
// the log file, so it is built only when asked for, never on the audio thread.
std::string diagnostics_text(const HostReport&, const std::string& format);
// The same from report_text() already made, for a format whose window has
// only the text (the VST3 controller, which the processor sends it).
std::string diagnostics_text(const std::string& report_text, const std::string& format);
} // namespace convolver
