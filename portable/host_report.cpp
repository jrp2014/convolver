// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_report.hpp"
#include "convolver_version.hpp"
#include "host_log.hpp"
#include <algorithm>
#include <cmath>
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
                    ? " To filter the audio, choose a filter file (Choose… beside Filter, or drop "
                      "a .cfg or sound file on this window), or load a Convolver preset (made with "
                      "convolver-preset) from the host's preset menu."
                    : " To filter the audio, load a Convolver preset (made with convolver-preset) "
                      "from the host's preset menu.";
    return text;
}

std::size_t longest_taps(const Configuration& c) {
    std::size_t longest = 0;
    for (const auto& path : c.paths)
        longest = std::max(longest, path.impulse.size());
    return longest;
}

// A time for reading, without brackets: "5.3 ms", "1.37 s"; empty without a rate.
std::string duration(std::size_t frames, int rate) {
    auto text = seconds(frames, rate);
    return text.size() > 3 ? text.substr(2, text.size() - 3) : std::string();
}

std::string decibels(double value) {
    char text[32] = {};
    std::snprintf(text, sizeof text, "%+.1f dB", value);
    std::string out = text;
    if (out.rfind("-", 0) == 0)
        out.replace(0, 1, "−"); // A minus sign, as the controls write it.
    return out;
}

// "In 1 + In 2 x0.5": a route's channels, from 1, with any gain that is not 1.
std::string route_side(const std::vector<Route>& routes, const char* side) {
    std::string text;
    for (const auto& route : routes) {
        if (!text.empty())
            text += " + ";
        text += std::string(side) + " " + std::to_string(route.channel + 1);
        if (route.gain != 1) {
            char gain[32] = {};
            std::snprintf(gain, sizeof gain, " ×%.3g", static_cast<double>(route.gain));
            text += gain;
        }
    }
    return text;
}

// Which input feeds which output: "In 1 -> Out 1, In 2 -> Out 2", the first
// few paths, then how many more (Copy diagnostics has the log for the rest).
std::string routing_words(const Configuration& c) {
    constexpr std::size_t shown = 4;
    std::string text;
    for (std::size_t i = 0; i < c.paths.size() && i < shown; ++i)
        text += (text.empty() ? "" : ", ") + route_side(c.paths[i].inputs, "In") + " → " +
                route_side(c.paths[i].outputs, "Out");
    if (c.paths.size() > shown)
        text += ", and " + std::to_string(c.paths.size() - shown) + " more";
    return text;
}

bool delayed(const Configuration& c) {
    const auto any = [](const std::vector<std::size_t>& delays) {
        return std::any_of(delays.begin(), delays.end(), [](std::size_t d) { return d != 0; });
    };
    return any(c.input_delays) || any(c.output_delays);
}

std::string file_name(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// The Audio section: what plays, in plain words
// (docs/use/plugins.md#the-audio-units-window).
std::vector<HostFact> audio_facts(const HostFilter& filter, const HostReportFacts& facts) {
    std::vector<HostFact> audio;
    const auto& host = facts.host;
    const auto& c = filter.filter();
    const auto& failure = filter.failure();
    const int rate = host ? host->sample_rate : 0;
    constexpr const char* not_started = "not known until the host starts the plug-in";

    HostFact sample_rate{"Sample rate",
                         host ? std::to_string(host->sample_rate) + " Hz"
                              : std::string(not_started),
                         {},
                         false,
                         "The rate the host runs the plug-in at. A filter must be made for the "
                         "same rate: Convolver does not resample."};
    if (failure && failure->kind == HostFailureKind::sample_rate && failure->sample_rate > 0) {
        sample_rate.note = "the filter is for " + std::to_string(failure->sample_rate) + " Hz";
        sample_rate.problem = true;
    } else if (c) {
        sample_rate.note = host ? "(the filter's rate too)"
                                : "(the filter is for " + std::to_string(c->sample_rate) + " Hz)";
    }
    audio.push_back(std::move(sample_rate));

    HostFact channels{"Channels",
                      host ? ports(host->inputs, host->outputs) : not_started,
                      {},
                      false,
                      "The inputs and outputs the host gives the plug-in, and the speakers the "
                      "filter's outputs are labelled for."};
    if (failure && failure->kind == HostFailureKind::channels &&
        (failure->inputs || failure->outputs)) {
        channels.note = "the filter has " + ports(failure->inputs, failure->outputs);
        channels.problem = true;
    } else if (c) {
        const auto speakers = speaker_names(c->channel_mask, c->output_channels);
        if (!host)
            channels.note = "(the filter has " + ports(c->input_channels, c->output_channels) +
                            (speakers.empty() ? "" : ": " + speakers) + ")";
        else if (!speakers.empty())
            channels.note = "(" + speakers + ")";
    }
    audio.push_back(std::move(channels));

    if (c) {
        // A filter the host could not start stays accepted (it is saved),
        // but it does not play.
        const bool idle = failure && failure->silent;
        audio.push_back({"Routing", routing_words(*c),
                         idle          ? "(this filter is not playing)"
                         : delayed(*c) ? "(with input or output delays)"
                                       : "",
                         false,
                         "Which input feeds which output through the filter; ×0.5 is a gain "
                         "on that route."});
        const auto taps = longest_taps(*c);
        const auto length = duration(taps, c->sample_rate);
        audio.push_back({"Filter length", length.empty() ? std::to_string(taps) + " taps" : length,
                         length.empty() ? "" : "(" + std::to_string(taps) + " taps)", false,
                         "The longest filter, in time and in taps (samples)."});
    } else {
        audio.push_back({"Routing", "none", "(no filter is loaded)", false,
                         "Which input feeds which output through the filter."});
    }

    const auto frames = [](std::size_t n) { return std::to_string(n) + " frames"; };
    HostFact delay{"Added delay",
                   rate ? duration(facts.latency_frames, rate) : frames(facts.latency_frames),
                   rate ? "(" + frames(facts.latency_frames) + ", reported to the host)"
                        : "(reported to the host)",
                   false,
                   "Convolver's latency, set by the Latency menu. It is reported to the host, "
                   "which can delay other tracks to keep them in time."};
    if (latency_waits(facts))
        delay.note +=
            "; " +
            (rate ? duration(facts.settings.latency, rate) : frames(facts.settings.latency)) +
            " from the host's next start";
    audio.push_back(std::move(delay));

    audio.push_back({"Ring-out",
                     facts.tail_frames == 0 ? std::string("none")
                     : rate                 ? duration(facts.tail_frames, rate)
                                            : frames(facts.tail_frames),
                     facts.tail_frames ? "after the input stops" : "", false,
                     "How long the output goes on after the input stops (the filter's tail)."});

    if (c && filter.filter_peak() > 0) {
        // Through Mix, the dry share adds at most its own level (inputs and
        // outputs equal): the bound is m * peak + (1 - m), before Gain.
        const double peak = filter.filter_peak();
        const bool equal = c->input_channels == c->output_channels;
        const double mix = equal ? std::clamp(facts.settings.mix, 0.0, 1.0) : 1.0;
        const double bound = mix * peak + (1 - mix);
        const double gain = std::pow(10.0, facts.settings.gain_db / 20);
        const double safe_db = -20 * std::log10(bound);
        audio.push_back(
            {"Loudest peak", decibels(20 * std::log10(peak)),
             gain * bound <= 1 + 1e-9
                 ? "at most, for full-scale input; it cannot clip at this Gain and Mix"
                 : "at most, for full-scale input; Gain " + decibels(safe_db) +
                       " or lower rules out clipping",
             false,
             "The loudest output any full-scale input could make through the filter. Music is "
             "usually quieter: this is a guarantee, not an estimate."});
    }

    if (facts.max_frames)
        audio.push_back({"Host blocks", "up to " + frames(facts.max_frames),
                         rate ? "(" + duration(facts.max_frames, rate) + ") per call" : "per call",
                         false, "The largest block of audio the host sends at a time."});
    return audio;
}

std::string request_reference(const HostRequest& r) {
    return (r.source == HostFilterSource::file && !r.path.empty()
                ? r.path
                : std::string("a saved project or preset")) +
           (r.number ? ", request " + std::to_string(r.number) : std::string());
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
    r.headline = r.explanation;
    if (r.status.status == HostStatus::playing && r.headline.rfind("Playing ", 0) == 0) {
        r.headline.erase(0, 8);
        if (!r.headline.empty() && r.headline[0] >= 'a' && r.headline[0] <= 'z')
            r.headline[0] = static_cast<char>(r.headline[0] - 'a' + 'A');
    }

    const auto& c = filter.filter();
    const auto& failure = filter.failure();
    const auto& accepted = filter.accepted();
    if (c)
        r.filter_name = accepted.source == HostFilterSource::file && !accepted.path.empty()
                            ? file_name(accepted.path)
                            : "From the saved project or preset";
    else
        r.filter_name = "None";
    // A refused request (not a failure found when the host started the
    // accepted filter, which names no new file).
    const bool refused_request = failure && failure->request.number != accepted.number;
    if (refused_request)
        r.filter_note =
            failure->request.source == HostFilterSource::file && !failure->request.path.empty()
                ? file_name(failure->request.path) + " was refused."
                : "The last filter loaded was refused.";

    r.audio = audio_facts(filter, facts);
    const auto& host = facts.host;
    const bool equal =
        host ? host->inputs == host->outputs : !c || c->input_channels == c->output_channels;
    r.settings = settings_words(facts.settings, equal);
    r.sample_rate = host ? host->sample_rate : 0;
    r.latency_waits = latency_waits(facts);

    if (c)
        r.reference.emplace_back("Filter file", request_reference(accepted));
    if (refused_request)
        r.reference.emplace_back("Refused file", request_reference(failure->request));
    auto software = execution_line();
    if (software.starts_with("Session: "))
        software.erase(0, 9);
    if (!facts.file_reader.empty())
        software += "; filter files read with " + facts.file_reader;
    r.reference.emplace_back("Software", software);

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
        r.reference.emplace_back("Log", "turned off (CONVOLVER_LOG is set empty)");
    else if (r.log_path.empty())
        r.reference.emplace_back("Log", "none (there is no home folder in the environment)");
    else
        r.reference.emplace_back("Log",
                                 r.log_path + (facts.log_written ? "" : " (could not be written)"));
    return r;
}

namespace {
// "48000 Hz (the filter's rate too)", "48000 Hz; the filter is for 44100 Hz".
std::string fact_text(const HostFact& f) {
    if (f.note.empty())
        return f.value;
    return f.value + (f.problem ? "; " : " ") + f.note;
}
} // namespace

std::vector<std::pair<std::string, std::string>> report_fields(const HostReport& r) {
    std::string audio, reference;
    for (const auto& f : r.audio)
        audio += f.label + "\t" + f.value + "\t" + f.note + "\t" + (f.problem ? "problem" : "") +
                 "\t" + f.help + "\n";
    for (const auto& [label, value] : r.reference)
        reference += label + ": " + value + "\n";
    return {{"status", std::to_string(static_cast<int>(r.status.status))},
            {"status name", r.status_name},
            {"summary", r.status.text},
            {"title", r.title},
            {"explanation", r.explanation},
            {"headline", r.headline},
            {"filter name", r.filter_name},
            {"filter note", r.filter_note},
            {"sample rate", r.sample_rate ? std::to_string(r.sample_rate) : std::string()},
            {"latency waits", r.latency_waits ? "yes" : "no"},
            {"audio", audio},
            {"reference", reference},
            {"log path", r.log_path},
            {"log written", r.log_written ? "yes" : "no"}};
}

std::string report_text(const HostReport& r) {
    auto text = "Convolver diagnostics\n" + r.title + "\nStatus: " + r.status_name + ". " +
                r.status.text + "\n\n" + r.explanation + "\n\nFilter: " + r.filter_name +
                (r.filter_note.empty() ? "" : " " + r.filter_note) + "\n";
    for (const auto& f : r.audio)
        text += f.label + ": " + fact_text(f) + "\n";
    text += "Settings: " + r.settings + "\n";
    for (const auto& [label, value] : r.reference)
        text += label + ": " + value + "\n";
    return text;
}

std::string diagnostics_text(const HostReport& r, const std::string& format) {
    auto text = report_text(r) + "\nRecent Convolver log lines for " + format + ":\n";
    const auto recent = host_log_recent(format);
    return text + (recent.empty() ? "(none found)\n" : recent);
}
} // namespace convolver
