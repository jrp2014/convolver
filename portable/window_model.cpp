// SPDX-License-Identifier: GPL-2.0-or-later
#include "window_model.hpp"
#include "convolver/host.hpp"
#include "decimal_text.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <locale>
#include <sstream>
#include <system_error>
#include <utility>

namespace convolver::view {
namespace {
constexpr int status_number(HostStatus status) {
    return static_cast<int>(status);
}

StatusTone tone_of(int status) {
    switch (status) {
    case status_number(HostStatus::active):
        return StatusTone::positive;
    case status_number(HostStatus::load_failed):
        return StatusTone::failure;
    case status_number(HostStatus::fault):
        return StatusTone::warning;
    default:
        return StatusTone::plain;
    }
}

StatusSymbol symbol_of(int status) {
    switch (status) {
    case status_number(HostStatus::active):
        return StatusSymbol::active;
    case status_number(HostStatus::load_failed):
        return StatusSymbol::failed;
    case status_number(HostStatus::unchecked):
        return StatusSymbol::waiting;
    case status_number(HostStatus::fault):
        return StatusSymbol::fault;
    case status_number(HostStatus::bypassed):
        return StatusSymbol::bypassed;
    default:
        return StatusSymbol::empty;
    }
}

// The lines of a field, without empty ones.
std::vector<std::string> lines(const std::string& text) {
    std::vector<std::string> result;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find('\n', start), text.size());
        if (end > start)
            result.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return result;
}

// PLUG-52: what each status word means and when it clears, in the order of
// HostStatus.
std::string status_help_of(int status) {
    switch (status) {
    case status_number(HostStatus::active):
        return "Active: the filter plays whenever the host sends audio through this track.";
    case status_number(HostStatus::no_filter):
        return "No filter: none is loaded, so the input passes through at the Gain and Mix "
               "settings, unchanged at their defaults (silence when the track's inputs and "
               "outputs differ in number). Choose a filter, or drop its file on this window.";
    case status_number(HostStatus::load_failed):
        return "Load failed: the newest filter could not be used, or the filter could not "
               "start. The message says what plays and how to put it right; this clears when "
               "a filter loads and plays.";
    case status_number(HostStatus::unchecked):
        return "Waiting: the filter is loaded but does not play yet, until the host's audio "
               "settings suit it. It plays as soon as they do.";
    case status_number(HostStatus::fault):
        return "Fault: the audio met a problem while the filter played. The message says what "
               "plays; Copy Diagnostics gives the details for a problem report.";
    case status_number(HostStatus::bypassed):
        return "Bypassed: the input plays unfiltered (silence where the filter has no dry "
               "signal) until bypass is turned off.";
    default:
        return {};
    }
}

void replace_all(std::string& text, std::string_view from, std::string_view to) {
    std::string out;
    std::size_t start = 0;
    for (auto at = text.find(from); at != std::string::npos; at = text.find(from, start)) {
        out.append(text, start, at - start);
        out += to;
        start = at + from.size();
    }
    out.append(text, start);
    text = std::move(out);
}
} // namespace

WindowContent window_content(const HostReport* report, bool can_load) {
    WindowContent c;
    if (!report) {
        c.title = "Convolver";
        c.status_name = "Not available";
        c.problem = "Convolver could not read its report from the plug-in.";
        c.fix = "Close this window and open it again. If it keeps happening, the Convolver log "
                "has the details.";
    } else {
        const auto& r = *report;
        c.available = true;
        c.status = status_number(r.status.status);
        c.title = r.title;
        c.status_name = r.status_name;
        c.status_help = status_help_of(c.status);
        c.tone = tone_of(c.status);
        c.symbol = symbol_of(c.status);
        c.problem = r.problem;
        // PLUG-52: Waiting with a problem (a kept filter that does not suit
        // the host) plays silence, so it is drawn in the warning colour; the
        // word stays Waiting, and Waiting before the host starts stays plain.
        if (c.status == status_number(HostStatus::unchecked) && !c.problem.empty())
            c.tone = StatusTone::warning;
        c.plays = r.plays;
        c.cause = r.cause;
        c.fix = r.fix;
        c.note = r.note;
        if (r.action == "choose filter" && can_load)
            c.action = WindowAction::choose_filter;
        else if (r.action == "copy diagnostics")
            c.action = WindowAction::copy_diagnostics;
        c.filter_name = r.filter_name;
        c.filter_note = r.filter_note;
        c.filter_rate = r.filter_rate;
        c.filter_channels = r.filter_channels;
        c.filter_length = r.filter_length;
        c.filter_taps = r.filter_taps;
        c.filter_speakers = r.filter_speakers;
        c.facts_shown = !c.filter_rate.empty();
        for (const auto& f : r.audio)
            c.audio.push_back({f.label, f.value, f.note, f.help, f.problem});
        for (const auto& [label, value] : r.reference)
            c.reference.push_back({label, value});
        c.mix_note = r.mix_note;
        c.gain_note = r.gain_note;
        c.gain_played = r.gain_played;
        c.gain_played_db = r.gain_played_db;
        c.sample_rate = r.sample_rate;
        c.latency_waits = r.latency_waits;
        c.latency_next_start = r.latency_next_start;
        c.log_path = r.log_path;
        c.filter_path = r.filter_path;
        for (const auto& file : r.filter_files)
            c.filter_files += file + "\n";
    }
    // PLUG-44: no drop hint under the name. With no filter the Fix names
    // dropping a file; otherwise Choose Filter's tooltip does.
    return c;
}

std::string latency_note(double frames, int sample_rate, bool waits, bool next_start) {
    std::string time;
    // PLUG-52 (jrp, 26 November 2026): said even for none, so the host's
    // block size is not read as delay Convolver adds.
    if (frames <= 0)
        time = "adds no delay";
    else if (sample_rate > 0)
        time = "adds " + detail::fixed_text(1000 * frames / sample_rate, 1) + " ms";
    if (waits)
        return time.empty() ? "Not in use yet" : time + ", not in use yet";
    if (next_start)
        return time.empty() ? "Selected for the next start"
                            : time + ", selected for the next start";
    return time;
}

namespace {
// PLUG-52: the Gain slider's taper, as (dB, position) corners joined by
// straight lines: most of its travel between -30 and +12 dB.
constexpr std::array<std::pair<double, double>, 4> gain_taper{
    {{min_gain_db, 0.0}, {-30.0, 0.15}, {12.0, 0.92}, {max_gain_db, 1.0}}};
} // namespace

double gain_position(double db) {
    if (!(db > gain_taper.front().first)) // NaN reads as the bottom.
        return 0;
    for (std::size_t i = 1; i < gain_taper.size(); ++i) {
        const auto [db0, at0] = gain_taper[i - 1];
        const auto [db1, at1] = gain_taper[i];
        if (db <= db1)
            return at0 + (db - db0) * (at1 - at0) / (db1 - db0);
    }
    return 1;
}

double gain_at_position(double position) {
    if (!(position > 0))
        return gain_taper.front().first;
    for (std::size_t i = 1; i < gain_taper.size(); ++i) {
        const auto [db0, at0] = gain_taper[i - 1];
        const auto [db1, at1] = gain_taper[i];
        if (position <= at1)
            return db0 + (position - at0) * (db1 - db0) / (at1 - at0);
    }
    return gain_taper.back().first;
}

std::string slider_help(std::string_view name, std::string_view reset_click) {
    const std::string reset(reset_click);
    if (name == "Gain")
        return "The level of the filtered sound, in decibels; the dry sound Mix lets through "
               "is not changed. Plays in Gain mode Manual. " +
               reset + " to put back 0 dB.";
    if (name == "Mix")
        return "The filtered (wet) sound against the input (dry): 100% is the filter alone, 0% "
               "the input alone. " +
               reset + " to put back 100%.";
    return {};
}

std::string choice_help(std::string_view name) {
    if (name == "Gain mode")
        return "Which gain plays. Manual: the Gain setting. Estimated: the filter's own "
               "estimate, from noise played through it, as convolver --gain-db auto finds. "
               "Clip-safe: the gain at which no input within full scale can clip, as convolver "
               "--gain-db no-clip; it is not a limiter, so input above full scale still can. "
               "Each filter plays at its own figure; the Gain setting is kept for Manual.";
    if (name == "Latency")
        return "The delay Convolver adds, in frames. Smaller is less delay and more processor "
               "load. A new choice plays at once, after a gap in the sound; if it cannot be "
               "prepared, the previous one keeps playing and this row says the choice is not "
               "in use yet. The host is told the delay, so it can delay other tracks to keep "
               "them in time.";
    return {};
}

const char* const value_box_help = "Type a value and press Return to set it exactly; a value "
                                   "beyond the range stops at its end.";
const char* const facts_help = "The filter's own sample rate, channels and length. The Audio "
                               "section gives the host's, which need not be the same.";

double message_slot(double text, double tallest, double minimum, double most) {
    return std::min(std::max({text, tallest, minimum}), most);
}

std::string watch_note(const std::string& path, bool watching) {
    if (!watching || path.empty())
        return {};
    const auto name = detail::utf8_string(detail::utf8_path(path).filename());
    return "Reloads when " + (name.empty() ? path : name) + " changes.";
}

std::optional<double> typed_value(std::string_view text, double minimum, double maximum) {
    std::string typed(text);
    replace_all(typed, "−", "-"); // The minus sign.
    std::replace(typed.begin(), typed.end(), ',', '.');
    // Read in the classic locale, so a host's LC_NUMERIC cannot change it.
    std::istringstream stream(typed);
    stream.imbue(std::locale::classic());
    double number = 0;
    stream >> std::ws >> number;
    if (stream.fail() || !std::isfinite(number))
        return std::nullopt;
    return std::clamp(number, minimum, maximum);
}

std::vector<RecentEntry> recent_entries(const std::vector<std::string>& files) {
    std::vector<RecentEntry> entries;
    for (const auto& file : files) {
        const auto path = detail::utf8_path(file);
        std::error_code error;
        if (file.empty() || !std::filesystem::exists(path, error))
            continue;
        const auto name = detail::utf8_string(path.filename());
        const auto folder = detail::utf8_string(path.parent_path().filename());
        entries.push_back({folder.empty() ? name : name + " — " + folder, file});
    }
    return entries;
}

void FileWatch::follow(std::string path, std::string files) {
    path_ = std::move(path);
    if (files == text_)
        return;
    text_ = std::move(files);
    watched_.clear();
    for (const auto& line : lines(text_))
        watched_.push_back(detail::utf8_path(line));
    settle();
}

void FileWatch::settle() {
    loaded_ = stamps();
    changed_.reset();
}

bool FileWatch::due() {
    if (path_.empty() || watched_.empty())
        return false;
    auto now = stamps();
    if (now == loaded_) {
        changed_.reset();
        return false;
    }
    if (!changed_ || now != *changed_) {
        changed_ = std::move(now); // Changed: due once it has stayed so for one check.
        return false;
    }
    loaded_ = std::move(*changed_);
    changed_.reset();
    return std::all_of(loaded_.begin(), loaded_.end(), [](const auto& s) { return s.has_value(); });
}

FileWatch::Stamps FileWatch::stamps() const {
    Stamps now;
    now.reserve(watched_.size());
    for (const auto& file : watched_) {
        std::error_code time_error, size_error;
        const auto modified = std::filesystem::last_write_time(file, time_error);
        const auto size = std::filesystem::file_size(file, size_error);
        if (time_error || size_error)
            now.emplace_back();
        else
            now.emplace_back(Stamp{modified, size});
    }
    return now;
}
} // namespace convolver::view
