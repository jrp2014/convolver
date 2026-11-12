// SPDX-License-Identifier: GPL-2.0-or-later
#include "window_model.hpp"
#include "convolver/host.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <locale>
#include <sstream>
#include <system_error>

namespace convolver::view {
namespace {
std::string field(const ReportFields& report, std::string_view key) {
    const auto found = report.find(key);
    return found == report.end() ? std::string() : found->second;
}

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

// The report's "audio" field: one fact per line, label, value, note,
// "problem" or empty, and help, between tabs.
std::vector<AudioFact> audio_facts(const std::string& text) {
    std::vector<AudioFact> facts;
    for (const auto& line : lines(text)) {
        std::vector<std::string> parts;
        std::size_t start = 0;
        while (true) {
            const auto tab = line.find('\t', start);
            parts.push_back(line.substr(start, tab == std::string::npos ? tab : tab - start));
            if (tab == std::string::npos)
                break;
            start = tab + 1;
        }
        if (parts.size() < 5)
            continue;
        facts.push_back({parts[0], parts[1], parts[2], parts[4], parts[3] == "problem"});
    }
    return facts;
}

// The report's "reference" field: "Label: value" per line; a line without
// ": " is all value.
std::vector<ReferenceLine> reference_lines(const std::string& text) {
    std::vector<ReferenceLine> result;
    for (const auto& line : lines(text)) {
        const auto colon = line.find(": ");
        if (colon == std::string::npos)
            result.push_back({"", line});
        else
            result.push_back({line.substr(0, colon), line.substr(colon + 2)});
    }
    return result;
}

void replace_all(std::string& text, std::string_view from, std::string_view to) {
    for (auto at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size()))
        text.replace(at, from.size(), to);
}
} // namespace

ReportFields report_map(const std::vector<std::pair<std::string, std::string>>& fields) {
    ReportFields report;
    for (const auto& [key, value] : fields)
        report.insert_or_assign(key, value);
    return report;
}

WindowContent window_content(const ReportFields* report, bool can_load) {
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
        const auto status = field(r, "status");
        c.status = status.empty() ? -1 : std::atoi(status.c_str());
        c.title = field(r, "title");
        c.status_name = field(r, "status name");
        c.tone = tone_of(c.status);
        c.symbol = symbol_of(c.status);
        c.problem = field(r, "problem");
        c.plays = field(r, "plays");
        c.cause = field(r, "cause");
        c.fix = field(r, "fix");
        c.note = field(r, "note");
        const auto action = field(r, "action");
        if (action == "choose filter" && can_load)
            c.action = WindowAction::choose_filter;
        else if (action == "copy diagnostics")
            c.action = WindowAction::copy_diagnostics;
        c.filter_name = field(r, "filter name");
        c.filter_note = field(r, "filter note");
        c.filter_rate = field(r, "filter rate");
        c.filter_channels = field(r, "filter channels");
        c.filter_length = field(r, "filter length");
        c.filter_taps = field(r, "filter taps");
        c.filter_speakers = field(r, "filter speakers");
        c.facts_shown = !c.filter_rate.empty();
        c.audio = audio_facts(field(r, "audio"));
        c.reference = reference_lines(field(r, "reference"));
        c.mix_note = field(r, "mix note");
        c.gain_note = field(r, "gain note");
        c.sample_rate = std::atoi(field(r, "sample rate").c_str());
        c.latency_waits = field(r, "latency waits") == "yes";
        c.log_path = field(r, "log path");
        c.filter_path = field(r, "filter path");
        c.filter_files = field(r, "filter files");
    }
    if (can_load)
        c.filter_note = c.filter_note.empty()
                            ? "Or drop an IR file, .cfg or .vstpreset on this window."
                            : c.filter_note + " Or drop a file on this window.";
    return c;
}

std::string latency_note(double frames, int sample_rate, bool waits) {
    std::string time;
    if (sample_rate > 0 && frames > 0) {
        char text[48];
        std::snprintf(text, sizeof text, "adds %.1f ms", 1000 * frames / sample_rate);
        time = text;
    }
    if (!waits)
        return time;
    return time.empty() ? "Not in use yet" : time + ", not in use yet";
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
