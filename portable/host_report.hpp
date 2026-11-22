// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// PLUG-9: what a plug-in window and its Copy diagnostics button show about one
// instance (docs/archive/plans/2026-10-24-plugin-editor.md#how-the-window-connects).
// It is built on the control side from the same functions as the Status
// parameter (host_status) and the Convolver log (host_log.hpp), so the
// window, a copied report and the log never disagree. Internal to the
// adapters and their views, which always ship together: the field names are
// not a stable interface.
namespace convolver {
// The Status parameter's value names, in HostStatus order.
inline constexpr std::array<const char*, 6> host_status_names{"Active",  "No filter", "Load failed",
                                                              "Waiting", "Fault",     "Bypassed"};
static_assert(static_cast<std::size_t>(HostStatus::bypassed) == host_status_names.size() - 1);

// What the adapter knows besides its HostFilter, under its control lock.
// PLUG-46: a tail and the rate it counts at (0 for the host's), which the
// adapters publish as one value in a lock-free std::atomic, so a host
// reading it on another thread never pairs one tail with another's rate.
struct HostTail {
    std::uint32_t frames = 0;
    std::int32_t rate = 0;
};

struct HostReportFacts {
    std::string format;             // The log tag, such as "Audio Unit #2".
    std::optional<HostLayout> host; // As for host_status(): nullopt while not known.
    std::size_t max_frames = 0;     // The host's largest block; 0 if not known.
    HostSettings settings;
    // The latency the processor runs at (settings.latency is the one chosen,
    // not yet in use when they differ), and the tail.
    std::size_t latency_frames = 0, tail_frames = 0;
    // The rate tail_frames counts at when it is not the host's: the accepted
    // filter's while no processor plays (its own tail); 0 for the host's.
    int tail_rate = 0;
    bool log_written = true; // host_log_written()
    // PLUG-22: the instance is bypassed (the host's switch or the window's).
    bool bypassed = false;
    // A processor plays now: the host has started the plug-in and not
    // stopped it. `host` can stay known after a stop (HostSettling), so it
    // does not say this (jrp's review of PR #236, 21 November 2026).
    bool running = false;
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
    bool operator==(const HostFact&) const = default;
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
    // extension (for saved state, the name saved beside it, PLUG-22;
    // "Restored filter" when none was, PLUG-31; "None"), and a note about
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
    // PLUG-22: which filter this is, as 16 hex digits, so a name saved
    // beside it (VST3's controller state) is shown only while that filter
    // plays: HostFilter::filter_identity(), a hash of every stored value of
    // the filter, found once when it was accepted. Empty without a filter.
    std::string filter_identity;
    // PLUG-22, PLUG-39 step 4: the name to save beside the playing filter's
    // state: the file's name, or the name its state brought or was offered
    // (HostFilter::accepted()); empty when it has none or nothing plays.
    // VST3's controller state saves it.
    std::string filter_saved_name;
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
    // and whether the chosen Latency is not in use yet (latency_waits()) or,
    // with no processor (the host has the plug-in stopped), selected for the
    // next start (AU-19, jrp's review of 22 November 2026).
    int sample_rate = 0;
    bool latency_waits = false;
    bool latency_next_start = false;
    // PLUG-23: for the window's Mix row, empty when Mix applies: why there
    // is no dry signal (the filter's inputs and outputs differ, or with no
    // filter the host's), so Mix has no effect and bypass plays silence.
    std::string mix_note;
    // PLUG-10: beside the window's Gain mode menu, empty in Set mode: the
    // gain the mode plays (gain_note()).
    std::string gain_note;
    // Reference material for problem reports, smaller and folded away in
    // the window: the filter file and request, software, log.
    std::vector<std::pair<std::string, std::string>> reference;
    std::string log_path; // UTF-8; empty when there is none.
    bool log_written = true;
    bool operator==(const HostReport&) const = default;
};

HostReport host_report(const HostFilter&, const HostReportFacts&);
// PLUG-22: HostReport::filter_identity's text for a filter identity
// (HostFilter::filter_identity(), configuration_identity()): 16 lowercase
// hex digits. convolver-preset writes the same text into VST3 presets
// (PLUG-31), so the two must stay one function.
std::string filter_identity_text(std::uint64_t identity);
// The identity filter_identity_text() wrote: exactly 16 lowercase hex
// digits, or none for anything else (VST3's controller state, PLUG-22).
std::optional<std::uint64_t> parse_filter_identity_text(std::string_view text) noexcept;

// PLUG-39 step 5: the report as bytes, for a format's transport (the VST3
// processor's status message, the Audio Unit's report property), and the
// report back from them: every field of HostReport, typed, encoded once,
// so a window or controller never parses text lines back (a negative
// sample rate, which host_report() never gives, is sent as 0). decode_report()
// gives nothing for bytes it cannot read whole (another encoding version,
// cut short, an out-of-range status or anything left over); it may throw
// std::bad_alloc.
std::string encode_report(const HostReport&);
std::optional<HostReport> decode_report(std::string_view bytes);

// The report as plain text: title, status, explanation, the Audio facts
// ("Label: value note"), the settings and the reference lines.
std::string report_text(const HostReport&);

// For Copy diagnostics: report_text() and this instance's recent lines from
// the Convolver log (host_log_recent), ready to paste into a report. Reads
// the log file, so it is built only when asked for, never on the audio thread.
std::string diagnostics_text(const HostReport&, const std::string& format);
} // namespace convolver
