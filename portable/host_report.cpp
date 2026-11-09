// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_report.hpp"
#include "advice.hpp"
#include "convolver_version.hpp"
#include "host_log.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace convolver {
namespace {
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

// PLUG-10: a Latency chosen but not yet in use: the adapter has not applied
// it yet, or could not (docs/develop/plugin-contract.md#settings).
bool latency_waits(const HostReportFacts& facts) {
    return facts.settings.latency != facts.latency_frames;
}

// DIAG-2: what the window shows above the form, from the most important
// thing to know: a load failure, then a fault, then a speaker notice, then
// what plays (docs/use/plugins.md#convolvers-window).
struct Message {
    std::string problem, plays, cause, fix, note, action;
};
void add_note(Message& m, const std::string& sentence) {
    if (!sentence.empty())
        m.note += (m.note.empty() ? "" : " ") + sentence;
}
// DIAG-2: a refused file's problem starts with its full path, for the log;
// the window names it by its file name (the reference lines keep the path).
std::string short_problem(std::string problem, const HostFailure& failure) {
    const auto& path = failure.request.path;
    if (failure.request.source != HostFilterSource::file || path.empty() ||
        !problem.starts_with(path))
        return problem;
    const auto name = detail::utf8_string(detail::utf8_path(path).filename());
    if (!name.empty())
        problem.replace(0, path.size(), name);
    return problem;
}
// What the window says to do with no filter loaded.
std::string load_fix(bool chooses_files) {
    return chooses_files
               ? "Choose an IR file, .cfg or configuration list (or drop one on this window), or "
                 "load a Convolver preset from the host's preset menu."
               : "Load a Convolver preset from the host's preset menu (convolver-preset makes "
                 "one from an IR file, .cfg or configuration list).";
}
// Failures the window's Choose Filter… puts right.
bool chosen_away(HostFailureKind kind) {
    return kind != HostFailureKind::preparation && kind != HostFailureKind::activation;
}
Message message(const HostFilter& filter, const HostReportFacts& facts) {
    const auto& host = facts.host;
    const auto* layout = host ? &*host : nullptr;
    const auto& failure = filter.failure();
    const auto choose = facts.chooses_files ? "choose filter" : "";
    Message m;
    const auto take = [&](const detail::Advice& a) {
        m.problem = detail::as_sentence(a.problem);
        m.cause = a.cause;
        m.fix = a.fix;
        m.note = a.note;
    };
    if (failure) {
        auto a = failure_message(*failure);
        a.problem = short_problem(std::move(a.problem), *failure);
        take(a);
        m.plays = failure_consequence(*failure, layout, facts.settings,
                                      layout && dry_signal(filter, *layout));
        if (chosen_away(failure->kind))
            m.action = choose;
        // Faults the load failure hides from the status.
        if (filter.faults().any())
            add_note(m, detail::as_sentence("While audio played: " +
                                            fault_advice(filter.faults(), true).problem));
    } else if (filter.faults().any() && !filter.failed()) {
        take(fault_advice(filter.faults(), false));
        if (filter.filter()) {
            m.plays = "The filter keeps playing.";
        } else {
            // Faults are counted with no filter too (the host's samples).
            m.plays = host ? playing_words(filter, *host, facts.settings)
                           : std::string("No filter is loaded.");
            add_note(m, load_fix(facts.chooses_files));
            m.action = choose;
        }
        if (filter.faults().processing_failure)
            m.action = "copy diagnostics";
    } else if (facts.bypassed && filter.filter()) {
        // PLUG-22: as HostProcessor bypasses, the input when there is a dry
        // signal, otherwise silence (dry_signal_rule(), PLUG-23).
        // Nothing is wrong, so turning it off is part of the sentence, not a fix.
        const auto& c = *filter.filter();
        m.plays =
            (!host ? std::string("No filter is applied while bypassed.")
             : dry_signal(filter, *host)
                 ? std::string("The input plays unfiltered.")
                 : "The outputs are silent: with " + ports(c.input_channels, c.output_channels) +
                       ", the filter has no unfiltered signal to play.") +
            " Turn bypass off to hear the filter.";
    } else if (filter.filter()) {
        // PLUG-22: while it plays, the window's heading names the filter and
        // the Audio section describes it, so nothing is said here.
        if (!host)
            m.plays = "Waiting for audio settings from the host. The filter then plays if it fits "
                      "the host's sample rate and channels.";
        if (host && !filter.notice().empty())
            take(filter.notice());
    } else if (filter.failed()) {
        m.plays = host ? playing_words(filter, *host, facts.settings)
                       : std::string("Silent after a failed load.");
        m.fix = "Load a filter made for the host's sample rate and channels.";
        m.action = choose;
    } else {
        m.plays = host ? playing_words(filter, *host, facts.settings)
                       : std::string("No filter is loaded.");
        m.fix = load_fix(facts.chooses_files);
        m.action = choose;
    }
    if (!host && !(filter.filter() && !failure))
        m.plays += " The host has not sent its audio settings yet.";
    if (latency_waits(facts))
        add_note(m, "Latency " + std::to_string(facts.settings.latency) +
                        " frames is chosen but not in use yet, so the plug-in keeps " +
                        std::to_string(facts.latency_frames) +
                        " frames for now. If it could not be applied, the Convolver log says "
                        "why.");
    return m;
}

// The message as text: the problem and what plays, then the labelled parts.
std::string explanation(const Message& m) {
    auto text =
        m.problem.empty() || m.plays.empty() ? m.problem + m.plays : m.problem + " " + m.plays;
    const std::string* parts[] = {&m.cause, &m.fix, &m.note};
    for (std::size_t i = 0; i < 3; ++i)
        if (!parts[i]->empty())
            text += "\n" + std::string(detail::advice_labels[i]) + *parts[i];
    return text;
}

std::size_t longest_taps(const Configuration& c) {
    std::size_t longest = 0;
    for (const auto& path : c.paths)
        longest = std::max(longest, path.impulse.size());
    return longest;
}

// Whose rate a problem note names: the filter that cannot play, or a
// refused file while the previous filter keeps playing.
std::string refused_words(const HostFailure& failure) {
    return failure.silent ? "the filter is for " : "the refused file is for ";
}

// A time for reading, without brackets: "5.3 ms", "1.37 s"; empty without a rate.
std::string duration(std::size_t frames, int rate) {
    auto text = seconds(frames, rate);
    return text.size() > 3 ? text.substr(2, text.size() - 3) : std::string();
}

std::string decibels(double value) {
    char text[32] = {};
    // Anything that rounds to 0.0 reads "+0.0 dB", never "−0.0 dB".
    std::snprintf(text, sizeof text, "%+.1f dB", std::abs(value) < 0.05 ? 0.0 : value);
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

// The Audio section: the host's values and what plays, each a short value
// with a short note; a mismatch is a problem note naming the filter's
// value (jrp, 7 November 2026: "Host sample rate" and the like rather than
// prose). The filter's own values are the facts under its name.
// (docs/use/plugins.md#convolvers-window).
std::vector<HostFact> audio_facts(const HostFilter& filter, const HostReportFacts& facts) {
    std::vector<HostFact> audio;
    const auto& host = facts.host;
    const auto& c = filter.filter();
    const auto& failure = filter.failure();
    const int rate = host ? host->sample_rate : 0;
    constexpr const char* not_started = "not known until the host sends its audio settings";

    HostFact sample_rate{"Host sample rate",
                         host ? host_rate_text(*host) : std::string(not_started),
                         {},
                         false,
                         "The rate the host runs the plug-in at. A filter must be made for the "
                         "same rate: Convolver does not resample."};
    if (failure && failure->kind == HostFailureKind::sample_rate && failure->sample_rate > 0) {
        sample_rate.note = refused_words(*failure) + std::to_string(failure->sample_rate) + " Hz";
        sample_rate.problem = true;
    }
    audio.push_back(std::move(sample_rate));

    HostFact channels{"Host channels",
                      host ? ports(host->inputs, host->outputs) : not_started,
                      {},
                      false,
                      "The inputs and outputs the host gives the plug-in."};
    if (failure && failure->kind == HostFailureKind::channels &&
        (failure->inputs || failure->outputs)) {
        channels.note = (failure->silent ? std::string("the filter has ")
                                         : std::string("the refused file has ")) +
                        ports(failure->inputs, failure->outputs);
        channels.problem = true;
    } else if (c && host) {
        // PLUG-23: a host that gives more inputs than the filter reads.
        if (const auto ignored = ignored_inputs(*c, *host); !ignored.empty())
            channels.note = "(" + ignored + " ignored)";
    }
    audio.push_back(std::move(channels));

    if (c) {
        // A filter the host could not start stays accepted (it is saved),
        // but it does not play.
        const bool idle = failure && failure->silent;
        audio.push_back({"Routing", routing_words(*c),
                         idle          ? "(not playing)"
                         : delayed(*c) ? "(with input or output delays)"
                                       : "",
                         false,
                         "Which input feeds which output through the filter; ×0.5 is a gain "
                         "on that route."});
    } else {
        audio.push_back({"Routing", "none", "(no filter)", false,
                         "Which input feeds which output through the filter."});
    }

    // PLUG-22: the latency is not a fact here; the Latency setting shows the
    // time it adds, and the note says when a choice is not in use yet.
    const auto frames = [](std::size_t n) { return std::to_string(n) + " frames"; };
    audio.push_back(
        {"Ring-out",
         facts.tail_frames == 0 ? std::string("none")
         // Under 0.05 ms reads as frames, not "0.0 ms".
         : rate && facts.tail_frames * 20000.0 >= rate ? duration(facts.tail_frames, rate)
                                                       : frames(facts.tail_frames),
         "", false, "How long the output goes on after the input stops (the filter's tail)."});

    if (c && filter.filter_peak() > 0) {
        // As HostProcessor::process() applies them: Gain on the filtered
        // path only, then Mix adds the dry input at 1 - m (inputs and
        // outputs equal; otherwise Mix is 1). The bound is
        // m * g * peak + (1 - m), so for any m > 0 the Gain that rules out
        // clipping is 1 / peak whatever the Mix, and at m = 0 nothing can.
        const double peak = filter.filter_peak();
        const bool equal = c->input_channels == c->output_channels;
        const double mix = !equal || std::isnan(facts.settings.mix)
                               ? 1.0
                               : std::clamp(facts.settings.mix, 0.0, 1.0);
        const double db = std::isnan(facts.settings.gain_db)
                              ? 0.0
                              : std::clamp(facts.settings.gain_db, min_gain_db, max_gain_db);
        const double bound = mix * std::pow(10.0, db / 20) * peak + (1 - mix);
        audio.push_back(
            {"Loudest peak", decibels(20 * std::log10(peak)),
             mix == 0 || bound <= 1 + 1e-9
                 ? "(cannot clip at this Gain and Mix)"
                 : "(Gain " + decibels(-20 * std::log10(peak)) + " or lower rules out clipping)",
             false,
             "The loudest output any full-scale input could make through the filter, at most. "
             "Music is usually quieter: this is a guarantee, not an estimate."});
    }

    if (facts.max_frames)
        audio.push_back({"Host block size", "up to " + frames(facts.max_frames),
                         rate ? "(" + duration(facts.max_frames, rate) + ")" : "", false,
                         "The largest block of audio the host sends at a time."});
    return audio;
}

std::string request_reference(const HostRequest& r) {
    return (r.source == HostFilterSource::file && !r.path.empty()
                ? r.path
                : std::string("a saved project or preset")) +
           (r.number ? ", request " + std::to_string(r.number) : std::string());
}
} // namespace

std::string filter_identity_text(std::uint64_t identity) {
    char text[17] = {};
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(identity));
    return text;
}

HostReport host_report(const HostFilter& filter, const HostReportFacts& facts) {
    HostReport r;
    r.status = host_status(filter, facts.host, facts.log_written, facts.settings, facts.bypassed);
    r.status_name = host_status_names.at(static_cast<std::size_t>(r.status.status));
    const auto program = host_program_name();
    // Out of process (GarageBand, Logic) the program is Apple's hosting
    // service, which names no host; the Software detail still gives it.
    const bool service = program.rfind("AUHostingService", 0) == 0;
    r.title = "Convolver " CONVOLVER_VERSION_STRING ", " + facts.format +
              (program.empty() ? ""
               : service       ? std::string(" in the host's plug-in process")
                               : " in " + program);
    const auto m = message(filter, facts);
    r.problem = m.problem;
    r.plays = m.plays;
    r.cause = m.cause;
    r.fix = m.fix;
    r.note = m.note;
    r.action = m.action;
    r.explanation = explanation(m);

    const auto& c = filter.filter();
    const auto& failure = filter.failure();
    const auto& accepted = filter.accepted();
    if (c)
        r.filter_name = accepted.source == HostFilterSource::file && !accepted.path.empty()
                            ? file_name(accepted.path)
                        : !accepted.name.empty() ? accepted.name
                                                 : "Restored filter";
    else
        r.filter_name = "None";
    if (c) {
        r.filter_rate = std::to_string(c->sample_rate) + " Hz";
        r.filter_channels = std::to_string(c->input_channels) + " in → " +
                            std::to_string(c->output_channels) + " out";
        const auto taps = longest_taps(*c);
        const auto length = duration(taps, c->sample_rate);
        r.filter_taps = std::to_string(taps) + " taps";
        r.filter_length = length.empty() ? r.filter_taps : length;
        r.filter_identity = filter_identity_text(filter.filter_identity());
        // A list on its own, so it starts with a capital: "Front left, front right".
        r.filter_speakers = speaker_names(c->channel_mask, c->output_channels);
        if (!r.filter_speakers.empty())
            r.filter_speakers[0] =
                static_cast<char>(std::toupper(static_cast<unsigned char>(r.filter_speakers[0])));
    }
    if (c && accepted.source == HostFilterSource::file) {
        r.filter_path = accepted.path;
        r.filter_files = accepted.files;
    }
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
    // Before the host starts the instance only the filter's counts are known,
    // and a failed load with no filter kept has no dry signal whatever the
    // host (jrp, 7 November 2026), as Status says.
    const auto dry = host ? dry_signal_rule(filter, *host) : dry_signal_before_host(filter);
    r.settings = settings_words(facts.settings, dry);
    if (dry != HostDrySignal::present) { // PLUG-23 (dry_signal_rule()).
        r.mix_note = std::string(no_dry_reason(dry)) +
                     ", so there is no dry signal: Mix has no effect, and bypass plays silence.";
        r.mix_note[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r.mix_note[0])));
    }
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
    r.log_path = detail::utf8_string(host_log_path());
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
    std::string audio, reference, files;
    for (const auto& file : r.filter_files)
        files += file + "\n";
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
            {"problem", r.problem},
            {"plays", r.plays},
            {"cause", r.cause},
            {"fix", r.fix},
            {"note", r.note},
            {"action", r.action},
            {"filter name", r.filter_name},
            {"filter rate", r.filter_rate},
            {"filter channels", r.filter_channels},
            {"filter length", r.filter_length},
            {"filter taps", r.filter_taps},
            {"filter identity", r.filter_identity},
            {"filter speakers", r.filter_speakers},
            {"filter note", r.filter_note},
            {"filter path", r.filter_path},
            {"filter files", files},
            {"sample rate", r.sample_rate ? std::to_string(r.sample_rate) : std::string()},
            {"latency waits", r.latency_waits ? "yes" : "no"},
            {"mix note", r.mix_note},
            {"audio", audio},
            {"reference", reference},
            {"log path", r.log_path},
            {"log written", r.log_written ? "yes" : "no"}};
}

std::string report_text(const HostReport& r) {
    auto text = "Convolver diagnostics\n" + r.title + "\nStatus: " + r.status_name + ". " +
                r.status.text + "\n\n" + r.explanation + "\n\nFilter: " + r.filter_name +
                (r.filter_note.empty() ? "" : " " + r.filter_note) + "\n";
    // The filter's own values, as the window shows them under its name.
    if (!r.filter_rate.empty())
        text += "Filter values: " + r.filter_rate + ", " + r.filter_channels + ", " +
                r.filter_length + " (" + r.filter_taps + ")" +
                (r.filter_speakers.empty() ? "" : ", " + r.filter_speakers) + "\n";
    for (const auto& f : r.audio)
        text += f.label + ": " + fact_text(f) + "\n";
    text += "Settings: " + r.settings + "\n";
    for (const auto& [label, value] : r.reference)
        text += label + ": " + value + "\n";
    return text;
}

std::string diagnostics_text(const HostReport& r, const std::string& format) {
    return diagnostics_text(report_text(r), format);
}
std::string diagnostics_text(const std::string& report_text, const std::string& format) {
    auto text = report_text + "\nRecent Convolver log lines for " + format + ":\n";
    const auto recent = host_log_recent(format);
    return text + (recent.empty() ? "(none found)\n" : recent);
}
} // namespace convolver
