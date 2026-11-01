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
inline constexpr std::array<const char*, 5> host_status_names{"Playing", "No filter", "Load failed",
                                                              "Not checked yet", "Fault"};
static_assert(static_cast<std::size_t>(HostStatus::fault) == host_status_names.size() - 1);

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
    // The format's window can load a filter file (the Audio Unit's Choose
    // Filter), so an empty instance is told about it as well as presets.
    bool chooses_files = false;
    // How that window reads sound files, for the Software detail, such as
    // "macOS AudioToolbox"; empty when the format reads none.
    std::string file_reader;
};

struct HostReport {
    HostStatusLine status;   // What the Status parameter shows.
    std::string status_name; // Its value's name, such as "Load failed".
    // "Convolver 0.6.0, Audio Unit #2 in REAPER": which instance this is, in the
    // program that runs it (a host's separate plug-in process where it has one).
    std::string title;
    // In full: what plays, why, and what to change. Several sentences.
    std::string explanation;
    // Labelled facts for support, in order: host, filter, settings, latency
    // and tail, software, log.
    std::vector<std::pair<std::string, std::string>> details;
    std::string log_path; // UTF-8; empty when there is none.
    bool log_written = true;
};

HostReport host_report(const HostFilter&, const HostReportFacts&);

// The report as named text values, for a format's transport (an Audio Unit
// property dictionary): status (the HostStatus number), status name,
// summary (the status line), title, explanation, details (one "Label: value"
// line each), log path and log written ("yes" or "no").
std::vector<std::pair<std::string, std::string>> report_fields(const HostReport&);

// The report as plain text: title, status, explanation and details.
std::string report_text(const HostReport&);

// For Copy diagnostics: report_text() and this instance's recent lines from
// the Convolver log (host_log_recent), ready to paste into a report. Reads
// the log file, so it is built only when asked for, never on the audio thread.
std::string diagnostics_text(const HostReport&, const std::string& format);
} // namespace convolver
