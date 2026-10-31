// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_report.hpp"
#include "convolver_version.hpp"
#include "host_log.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace convolver {
namespace {
std::string ports(std::size_t inputs, std::size_t outputs) {
    return std::to_string(inputs) + " in, " + std::to_string(outputs) + " out";
}
std::string filter_words(const Configuration& c) {
    return std::to_string(c.sample_rate) + " Hz, " + ports(c.input_channels, c.output_channels);
}
std::string seconds(std::size_t frames, int rate) {
    if (rate <= 0)
        return {};
    char text[48] = {};
    const double ms = 1000.0 * static_cast<double>(frames) / rate;
    if (ms < 1000)
        std::snprintf(text, sizeof text, " (%.1f ms)", ms);
    else
        std::snprintf(text, sizeof text, " (%.2f s)", ms / 1000);
    return text;
}
std::string request_words(const HostRequest& r) {
    if (r.number == 0)
        return {};
    return "; request " + std::to_string(r.number) +
           (r.source == HostFilterSource::file && !r.path.empty()
                ? ", from " + r.path
                : ", from a saved project or preset");
}

// PLUG-10: a Latency chosen while the processor runs at another block size
// waits for the next start (docs/develop/plugin-contract.md#settings).
bool latency_waits(const HostReportFacts& facts) {
    return facts.latency_frames && facts.settings.latency != facts.latency_frames;
}

// What plays, why, and what to change, in full.
std::string explanation(const HostFilter& filter, const HostReportFacts& facts) {
    const auto& host = facts.host;
    const auto& failure = filter.failure();
    std::string text;
    if (failure && !failure->silent) {
        // A refused replacement: the previous filter still plays.
        text = failure->message;
        if (host)
            text += " " + playing_words(filter, *host, facts.settings);
    } else if (host) {
        text = playing_words(filter, *host, facts.settings);
    } else {
        if (failure)
            text = failure->message;
        else if (const auto& c = filter.filter())
            text = "A filter for " + filter_words(*c) +
                   " is loaded. It is checked against the host's sample rate and channels when "
                   "the host starts the plug-in.";
        else if (filter.failed())
            text = "Silent after a failed load.";
        else
            text = "No filter is loaded.";
        text += " The host has not started the plug-in yet.";
    }
    // playing_words() gives the mono-track hint for a silent failure; a
    // refused replacement gets it here.
    if (failure && !failure->silent && host)
        text += mono_track_hint(*failure, *host);
    if (filter.faults().any())
        text += " " + fault_message(HostFaultReport{filter.faults(), {}, filter.failure_shown()});
    if (latency_waits(facts))
        text += " Latency " + std::to_string(facts.settings.latency) +
                " frames is chosen; it takes effect when the host next starts the plug-in (for "
                "example when the project is reopened or the sample rate changes). Until then "
                "the plug-in keeps " +
                std::to_string(facts.latency_frames) + " frames.";
    if (!filter.filter() && !failure && !filter.failed())
        text += facts.chooses_files
                    ? " To filter the audio, choose a filter file (Choose Filter, or drop a .cfg "
                      "or sound file on this window), or load a Convolver preset (made with "
                      "convolver-preset) from the host's preset menu."
                    : " To filter the audio, load a Convolver preset (made with convolver-preset) "
                      "from the host's preset menu.";
    return text;
}

std::string longest_words(const Configuration& c) {
    std::size_t longest = 0;
    for (const auto& path : c.paths)
        longest = std::max(longest, path.impulse.size());
    return std::to_string(c.paths.size()) + (c.paths.size() == 1 ? " path" : " paths") +
           ", longest " + std::to_string(longest) + " taps" + seconds(longest, c.sample_rate);
}
} // namespace

HostReport host_report(const HostFilter& filter, const HostReportFacts& facts) {
    HostReport r;
    r.status = host_status(filter, facts.host, facts.log_written, facts.settings);
    r.status_name = host_status_names.at(static_cast<std::size_t>(r.status.status));
    const auto program = host_program_name();
    // Out of process (GarageBand, Logic) the program is Apple's hosting
    // service, which names no host; the Software detail still gives it.
    const bool service = program.rfind("AUHostingService", 0) == 0;
    r.title = "Convolver " CONVOLVER_VERSION_STRING ", " + facts.format +
              (program.empty() ? ""
               : service       ? std::string(" in the host's plug-in process")
                               : " in " + program);
    r.explanation = explanation(filter, facts);

    const auto& host = facts.host;
    if (host)
        r.details.emplace_back(
            "Host", std::to_string(host->sample_rate) + " Hz, " +
                        ports(host->inputs, host->outputs) +
                        (facts.max_frames
                             ? ", blocks of up to " + std::to_string(facts.max_frames) + " frames"
                             : std::string()));
    else
        r.details.emplace_back("Host", "not started yet");
    if (const auto& c = filter.filter())
        r.details.emplace_back("Filter", filter_words(*c) + ", " + longest_words(*c) +
                                             request_words(filter.accepted()));
    else
        r.details.emplace_back("Filter",
                               filter.failed() ? "none, after a failed load" : "none loaded");
    const bool equal = host ? host->inputs == host->outputs
                            : !filter.filter() || filter.filter()->input_channels ==
                                                      filter.filter()->output_channels;
    r.details.emplace_back("Settings", settings_words(facts.settings, equal));
    const int rate = host ? host->sample_rate : 0;
    r.details.emplace_back("Latency", std::to_string(facts.latency_frames) + " frames" +
                                          seconds(facts.latency_frames, rate) + ", tail " +
                                          std::to_string(facts.tail_frames) + " frames" +
                                          seconds(facts.tail_frames, rate) +
                                          (latency_waits(facts)
                                               ? "; " + std::to_string(facts.settings.latency) +
                                                     " frames chosen, from the next start"
                                               : std::string()));
    auto software = execution_line();
    if (software.starts_with("Session: "))
        software.erase(0, 9);
    if (!facts.file_reader.empty())
        software += "; filter files read with " + facts.file_reader;
    r.details.emplace_back("Software", software);

    r.log_written = facts.log_written;
    const auto path = host_log_path().u8string();
    r.log_path.assign(path.begin(), path.end());
#ifdef _WIN32
    const bool turned_off = false;
#else
    const char* chosen = std::getenv("CONVOLVER_LOG");
    const bool turned_off = chosen && !*chosen;
#endif
    if (turned_off)
        r.details.emplace_back("Log", "turned off (CONVOLVER_LOG is set empty)");
    else if (r.log_path.empty())
        r.details.emplace_back("Log", "none (there is no home folder in the environment)");
    else
        r.details.emplace_back("Log",
                               r.log_path + (facts.log_written ? "" : " (could not be written)"));
    return r;
}

std::vector<std::pair<std::string, std::string>> report_fields(const HostReport& r) {
    std::string details;
    for (const auto& [label, value] : r.details)
        details += label + ": " + value + "\n";
    return {{"status", std::to_string(static_cast<int>(r.status.status))},
            {"status name", r.status_name},
            {"summary", r.status.text},
            {"title", r.title},
            {"explanation", r.explanation},
            {"details", details},
            {"log path", r.log_path},
            {"log written", r.log_written ? "yes" : "no"}};
}

std::string report_text(const HostReport& r) {
    auto text = "Convolver diagnostics\n" + r.title + "\nStatus: " + r.status_name + ". " +
                r.status.text + "\n\n" + r.explanation + "\n\n";
    for (const auto& [label, value] : r.details)
        text += label + ": " + value + "\n";
    return text;
}

std::string diagnostics_text(const HostReport& r, const std::string& format) {
    auto text = report_text(r) + "\nRecent Convolver log lines for " + format + ":\n";
    const auto recent = host_log_recent(format);
    return text + (recent.empty() ? "(none found)\n" : recent);
}
} // namespace convolver
