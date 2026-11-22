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
#include <limits>
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
               ? "Choose an IR file, .cfg, configuration list or Convolver .vstpreset (or drop "
                 "one on this window), or load a Convolver preset from the host's preset menu."
               : "Load a Convolver preset from the host's preset menu (convolver-preset makes "
                 "one from an IR file, .cfg or configuration list).";
}
// Failures the window's Choose Filter… puts right.
bool chosen_away(HostFailureKind kind) {
    return kind != HostFailureKind::preparation && kind != HostFailureKind::activation;
}
// PLUG-44 (jrp's review of 735b0af): what becomes of the filter Convolver
// keeps after a request was refused, naming it, so the refused file and the
// kept one each have an owner: "room.cfg keeps playing." only while the host
// plays it; otherwise "room.cfg is still selected.", and when it does not suit
// the host, what it will play at ("... and will play at 48000 Hz.").
std::string kept_words(const HostFilter& filter, const HostReportFacts& facts) {
    const auto& accepted = filter.accepted();
    const auto slash = accepted.path.find_last_of("/\\");
    const std::string name =
        accepted.source == HostFilterSource::file && !accepted.path.empty()
            ? (slash == std::string::npos ? accepted.path : accepted.path.substr(slash + 1))
        : !accepted.name.empty() ? accepted.name
                                 : std::string("The previous filter");
    const auto& host = facts.host;
    const bool dry = host && dry_signal(filter, *host);
    if (filter.kept_unsuited()) {
        const auto& c = *filter.filter();
        return name + " is still selected and will play " +
               (filter.failure()->kind == HostFailureKind::sample_rate
                    ? "at " + std::to_string(c.sample_rate) + " Hz."
                    : "with " + ports(c.input_channels, c.output_channels) + ".") +
               (facts.bypassed && host ? " " + bypass_sentence(true, dry) : std::string());
    }
    if (facts.bypassed && host)
        return name + " is still selected. " + bypass_sentence(true, dry);
    return name + (host && facts.running ? " keeps playing." : " is still selected.");
}

Message message(const HostFilter& filter, const HostReportFacts& facts) {
    const auto& host = facts.host;
    const auto* layout = host ? &*host : nullptr;
    const auto& failure = filter.shown_failure();
    // PLUG-44 (jrp's review of 735b0af): a request refused after the filter
    // Convolver keeps is the user's latest action, so it is explained first,
    // even while the kept filter's own wait is the failure Status shows.
    const auto& refusal = filter.rejection();
    const bool kept_after_refusal =
        filter.filter() && refusal && refusal->request.number != filter.accepted().number;
    const auto& explained = kept_after_refusal ? refusal : failure;
    const auto choose = facts.chooses_files ? "choose filter" : "";
    // PLUG-36 (jrp, 20 November 2026), PLUG-39 step 4: with bypass on, what
    // plays is what bypass plays, as host_status() says; playing_words() and
    // failure_consequence() say it.
    const auto no_filter_plays = [&] {
        // PLUG-44: before the host's settings, the heading's "None" and the
        // status say there is no filter; what follows says why nothing is
        // known yet.
        return !host ? std::string() : playing_words(filter, *host, facts.settings, facts.bypassed);
    };
    Message m;
    const auto take = [&](const detail::Advice& a) {
        m.problem = detail::as_sentence(a.problem);
        m.cause = a.cause;
        m.fix = a.fix;
        m.note = a.note;
    };
    // Which state the report explains, in the order Status gives it
    // (host_status_value()).
    const auto status = host_status_value(filter, host.has_value(), facts.bypassed);
    // PLUG-44: a filter that waits for settings that suit it shows Waiting,
    // and is explained as a filter that does not suit the host.
    if ((status == HostStatus::load_failed || filter.waits_for_settings()) && explained) {
        auto a = shown_failure_message(*explained, filter.played_with(host));
        a.problem = short_problem(std::move(a.problem), *explained);
        take(a);
        if (kept_after_refusal)
            m.plays = kept_words(filter, facts);
        else {
            m.plays = failure_consequence(*explained, layout, facts.settings,
                                          layout && dry_signal(filter, *layout), facts.bypassed);
            // PLUG-42 (jrp, 21 November 2026): a filter kept although it
            // does not suit the host plays at the next start that suits it.
            if (filter.kept_unsuited())
                m.plays += " The filter is kept and waits for audio settings that suit it: it "
                           "plays when the host next starts the plug-in with them.";
        }
        if (chosen_away(explained->kind))
            m.action = choose;
        // Faults the load failure hides from the status.
        if (filter.faults().any())
            add_note(m, detail::as_sentence("While audio played: " +
                                            fault_advice(filter.faults(), true).problem));
    } else if (status == HostStatus::fault) {
        take(fault_advice(filter.faults(), false));
        if (filter.filter()) {
            // With no processor running (stopped, or not started yet), even
            // with the host's layout still known after a stop, nothing
            // plays: say what happens when it starts.
            m.plays = facts.bypassed
                          ? bypass_sentence(host.has_value(), host && dry_signal(filter, *host))
                      : facts.running ? std::string("The filter keeps playing.")
                                      : std::string("The filter is kept and plays when the host "
                                                    "starts again.");
        } else {
            // Faults are counted with no filter too (the host's samples).
            m.plays = no_filter_plays();
            add_note(m, load_fix(facts.chooses_files));
            m.action = choose;
        }
        if (filter.faults().processing_failure)
            m.action = "copy diagnostics";
    } else if (status == HostStatus::bypassed) {
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
    } else if (status == HostStatus::active || status == HostStatus::unchecked) {
        // PLUG-22: while it plays, the window's heading names the filter and
        // the Audio section describes it, so nothing is said here.
        if (!host && filter.awaiting()) // PLUG-27, jrp's words (20 November 2026).
            m.plays = "Filter selected. Waiting for the host's audio settings. Convolver checks "
                      "the filter when the host plays audio through the plug-in.";
        else if (!host)
            m.plays = "Waiting for audio settings from the host. The filter then plays if it fits "
                      "the host's sample rate and channels.";
        if (host && !filter.notice().empty())
            take(filter.notice());
    } else if (status == HostStatus::load_failed) {
        m.plays = host ? playing_words(filter, *host, facts.settings, facts.bypassed)
                       : std::string("Silent after a failed load.");
        m.fix = "Load a filter made for the host's sample rate and channels.";
        m.action = choose;
    } else {
        m.plays = no_filter_plays();
        m.fix = load_fix(facts.chooses_files);
        m.action = choose;
    }
    if (!host && !(filter.filter() && !failure))
        m.plays += std::string(m.plays.empty() ? "" : " ") +
                   "The host has not sent its audio settings yet.";
    // PLUG-44: a Latency not in use yet is said by the window's Latency row
    // and the Status line, not again in the message.
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

// PLUG-44: channel counts as the facts under the filter's name write them,
// "2 in → 2 out", so the window uses one form (Status keeps ASCII ports()).
std::string arrow_ports(std::size_t inputs, std::size_t outputs) {
    return std::to_string(inputs) + " in → " + std::to_string(outputs) + " out";
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
    const auto& failure = filter.shown_failure();
    const int rate = host ? host->sample_rate : 0;
    if (!host) {
        // PLUG-44: one line, not two with the message's words again.
        audio.push_back({"Host",
                         "not known yet",
                         {},
                         false,
                         "The sample rate and channels the host runs the plug-in with, known "
                         "once it sends its audio settings."});
    } else if (c &&
               !(failure && (failure->kind == HostFailureKind::sample_rate ||
                             failure->kind == HostFailureKind::channels)) &&
               host->fractional_rate == 0 && host->sample_rate == c->sample_rate &&
               host->inputs == c->input_channels && host->outputs == c->output_channels) {
        // PLUG-44: what the host gives matches the facts under the filter's
        // name, so one line says so rather than repeating them (also after
        // a refusal for another reason, an unreadable file say).
        audio.push_back({"Host",
                         host_rate_text(*host) + ", " + arrow_ports(host->inputs, host->outputs),
                         "✓ suits the filter", false,
                         "The sample rate and channels the host runs the plug-in with: the "
                         "filter's own."});
    } else {
        HostFact sample_rate{"Host sample rate",
                             host_rate_text(*host),
                             {},
                             false,
                             "The rate the host runs the plug-in at. A filter must be made for "
                             "the same rate: Convolver does not resample."};
        if (failure && failure->kind == HostFailureKind::sample_rate && failure->sample_rate > 0) {
            sample_rate.note =
                refused_words(*failure) + std::to_string(failure->sample_rate) + " Hz";
            sample_rate.problem = true;
        }
        audio.push_back(std::move(sample_rate));

        HostFact channels{"Host channels",
                          arrow_ports(host->inputs, host->outputs),
                          {},
                          false,
                          "The inputs and outputs the host gives the plug-in."};
        if (failure && failure->kind == HostFailureKind::channels &&
            (failure->inputs || failure->outputs)) {
            channels.note = (failure->silent ? std::string("the filter has ")
                                             : std::string("the refused file has ")) +
                            arrow_ports(failure->inputs, failure->outputs);
            channels.problem = true;
        } else if (c) {
            // PLUG-23: a host that gives more inputs than the filter reads.
            if (const auto ignored = ignored_inputs(*c, *host); !ignored.empty())
                channels.note = "(" + ignored + " ignored)";
        }
        audio.push_back(std::move(channels));
    }

    // PLUG-44: with no filter there is no routing to show.
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
    }

    // PLUG-22: the latency is not a fact here; the Latency setting shows the
    // time it adds, and the note says when a choice is not in use yet.
    // PLUG-44: Ring-out shows only when it says more than the filter's
    // Length: a filter of n taps rings out for n - 1 frames at its own rate,
    // so the line shows for delays, a Latency, another rate, or with no
    // filter a tail that is not zero. Compared in frames, as the two round
    // differently as times.
    const auto frames = [](std::size_t n) { return std::to_string(n) + " frames"; };
    // PLUG-46: a stopped filter's tail counts at its own rate, so it is
    // shown and compared at that rate.
    const int tail_rate = facts.tail_rate > 0 ? facts.tail_rate : rate;
    const auto ring_out = facts.tail_frames == 0 ? std::string("none")
                          // Under 0.05 ms reads as frames, not "0.0 ms".
                          : tail_rate && facts.tail_frames * 20000.0 >= tail_rate
                              ? duration(facts.tail_frames, tail_rate)
                              : frames(facts.tail_frames);
    const bool says_more =
        c ? facts.tail_frames + 1 != longest_taps(*c) || (tail_rate && tail_rate != c->sample_rate)
          : facts.tail_frames != 0;
    if (says_more)
        audio.push_back({"Ring-out", ring_out, "", false,
                         "How long the output goes on after the input stops (the filter's tail)."});

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
        // PLUG-10: the gain that plays in the Gain mode (silence without a
        // figure, which cannot clip either).
        const auto played = applied_gain_db(facts.settings, filter.gains());
        const double bound =
            played ? mix * std::pow(10.0, *played / 20) * peak + (1 - mix) : 1 - mix;
        const bool set = facts.settings.gain_mode == GainMode::set;
        audio.push_back(
            {"Worst-case peak", decibels(20 * std::log10(peak)),
             mix == 0 || bound <= 1 + 1e-9 ? (set ? "(cannot clip at this Gain and Mix)"
                                                  : "(cannot clip in this Gain mode and Mix)")
             : set ? "(Gain " + decibels(-20 * std::log10(peak)) + " or lower rules out clipping)"
                   : std::string("(Gain mode No clipping rules out clipping)"),
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

std::string latency_value_text(std::size_t frames, int sample_rate) {
    auto text = std::to_string(frames) + " frames";
    if (frames == 0)
        return text + " (none)";
    if (sample_rate > 0) {
        char time[32] = {};
        std::snprintf(time, sizeof time, " (%.1f ms)",
                      1000.0 * static_cast<double>(frames) / sample_rate);
        text += time;
    }
    return text;
}

std::string filter_identity_text(std::uint64_t identity) {
    char text[17] = {};
    std::snprintf(text, sizeof text, "%016llx", static_cast<unsigned long long>(identity));
    return text;
}

std::optional<std::uint64_t> parse_filter_identity_text(std::string_view text) noexcept {
    if (text.size() != 16)
        return std::nullopt;
    std::uint64_t value = 0;
    for (const char ch : text) {
        const int digit = ch >= '0' && ch <= '9'   ? ch - '0'
                          : ch >= 'a' && ch <= 'f' ? ch - 'a' + 10
                                                   : -1;
        if (digit < 0)
            return std::nullopt;
        value = value << 4 | static_cast<std::uint64_t>(digit);
    }
    return value;
}

HostReport host_report(const HostFilter& filter, const HostReportFacts& given) {
    // PLUG-27: a chosen filter awaiting validation is not judged against a
    // start that may have been at a placeholder rate (HostFilter::waiting()).
    HostReportFacts facts = given;
    if (filter.waiting())
        facts.host.reset();
    HostReport r;
    r.status = host_status(filter, facts.host, facts.log_written, facts.settings, facts.bypassed,
                           facts.chooses_files, facts.latency_frames);
    r.status_name = host_status_names.at(static_cast<std::size_t>(r.status.status));
    const auto program = host_program_name();
    // Out of process (GarageBand, Logic) the program is Apple's hosting
    // service, which names no host; the Software detail still gives it.
    const bool service = program.rfind("AUHostingService", 0) == 0;
    // PLUG-44: the release version; the Software line keeps the build's
    // full identity ("0.18.5+gb3b45578b60f").
    std::string version = CONVOLVER_VERSION_STRING;
    version = version.substr(0, version.find('+'));
    r.title = "Convolver " + version + ", " + facts.format +
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
        r.filter_channels = arrow_ports(c->input_channels, c->output_channels);
        const auto taps = longest_taps(*c);
        const auto length = duration(taps, c->sample_rate);
        r.filter_taps = std::to_string(taps) + " taps";
        r.filter_length = length.empty() ? r.filter_taps : length;
        r.filter_identity = filter_identity_text(filter.filter_identity());
        r.filter_saved_name = accepted.source == HostFilterSource::file && !accepted.path.empty()
                                  ? r.filter_name
                                  : accepted.name;
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
    // accepted filter, which names no new file). Named even while a kept
    // filter's wait is the failure shown, so a file refused then is not
    // only in the log (jrp, 21 November 2026, PLUG-44).
    const auto& refusal = filter.rejection();
    const bool refused_request = refusal && refusal->request.number != accepted.number;
    if (refused_request)
        // PLUG-44: the message says what becomes of the filter named in
        // the heading (kept_words()), so the note names only the refusal.
        r.filter_note =
            refusal->request.source == HostFilterSource::file && !refusal->request.path.empty()
                ? file_name(refusal->request.path) + " was refused."
                : std::string("The last filter loaded was refused.");

    r.audio = audio_facts(filter, facts);
    const auto& host = facts.host;
    // Before the host starts the instance only the filter's counts are known,
    // and a failed load with no filter kept has no dry signal whatever the
    // host (jrp, 7 November 2026), as Status says.
    const auto dry = host ? dry_signal_rule(filter, *host) : dry_signal_before_host(filter);
    const auto* gains = filter.filter() ? &filter.gains() : nullptr;
    r.settings = settings_words(facts.settings, dry, gains);
    r.gain_note = gain_note(facts.settings, filter.gains(), gains != nullptr);
    if (dry != HostDrySignal::present) { // PLUG-23 (dry_signal_rule()).
        r.mix_note = std::string(no_dry_reason(dry)) +
                     ", so there is no dry signal: Mix has no effect, and bypass plays silence.";
        r.mix_note[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(r.mix_note[0])));
    }
    r.sample_rate = host ? host->sample_rate : 0;
    r.latency_waits = latency_waits(facts);
    r.latency_next_start = !facts.running && !r.latency_waits;

    if (c)
        r.reference.emplace_back("Filter file", request_reference(accepted));
    // PLUG-33: which list entry a wider input bus chose, and why. Said of
    // the moment of choosing, not the host's present inputs, which may have
    // changed since without the file being read again (independent review
    // of 43aaa07); the load's log line gives the count.
    if (c && !accepted.wider_entry.empty())
        r.reference.emplace_back("List entry",
                                 accepted.wider_entry +
                                     " (when the list was chosen, no entry had the host's input "
                                     "count; this one had the most inputs below it)");
    if (refused_request)
        r.reference.emplace_back("Refused file", request_reference(refusal->request));
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

namespace {
// PLUG-39 step 5: one list of HostReport's fields, read and written in the
// same order by the two classes below, so the encoding cannot drift from
// the decoding. Numbers are 8 bytes, least significant first; a text is its
// length then its bytes; a list is its count then its items.
constexpr std::uint64_t report_encoding = 1; // The first number; bump on any change.

class ReportWriter {
  public:
    bool number(std::uint64_t n) {
        for (int i = 0; i < 8; ++i)
            bytes += static_cast<char>((n >> (8 * i)) & 0xff);
        return true;
    }
    bool text(const std::string& t) {
        number(t.size());
        bytes += t;
        return true;
    }
    bool flag(bool b) {
        return number(b ? 1 : 0);
    }
    bool status(HostStatus s) {
        return number(static_cast<std::uint64_t>(s));
    }
    bool rate(int r) {
        return number(static_cast<std::uint64_t>(std::max(r, 0)));
    }
    bool texts(const std::vector<std::string>& list) {
        number(list.size());
        for (const auto& t : list)
            text(t);
        return true;
    }
    bool facts(const std::vector<HostFact>& list) {
        number(list.size());
        for (const auto& f : list) {
            text(f.label);
            text(f.value);
            text(f.note);
            flag(f.problem);
            text(f.help);
        }
        return true;
    }
    bool pairs(const std::vector<std::pair<std::string, std::string>>& list) {
        number(list.size());
        for (const auto& [label, value] : list) {
            text(label);
            text(value);
        }
        return true;
    }
    std::string bytes;
};

class ReportReader {
  public:
    explicit ReportReader(std::string_view bytes) : rest_(bytes) {}
    bool number(std::uint64_t& n) {
        if (rest_.size() < 8)
            return false;
        n = 0;
        for (int i = 0; i < 8; ++i)
            n |= static_cast<std::uint64_t>(static_cast<unsigned char>(rest_[i])) << (8 * i);
        rest_.remove_prefix(8);
        return true;
    }
    bool text(std::string& t) {
        std::uint64_t n = 0;
        if (!number(n) || n > rest_.size())
            return false;
        t.assign(rest_.substr(0, static_cast<std::size_t>(n)));
        rest_.remove_prefix(static_cast<std::size_t>(n));
        return true;
    }
    bool flag(bool& b) {
        std::uint64_t n = 0;
        if (!number(n) || n > 1)
            return false;
        b = n == 1;
        return true;
    }
    bool status(HostStatus& s) {
        std::uint64_t n = 0;
        if (!number(n) || n >= host_status_names.size())
            return false;
        s = static_cast<HostStatus>(n);
        return true;
    }
    bool rate(int& r) {
        std::uint64_t n = 0;
        if (!number(n) || n > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return false;
        r = static_cast<int>(n);
        return true;
    }
    bool texts(std::vector<std::string>& list) {
        std::uint64_t n = 0;
        if (!count(n))
            return false;
        list.assign(static_cast<std::size_t>(n), {});
        return std::all_of(list.begin(), list.end(), [&](std::string& t) { return text(t); });
    }
    bool facts(std::vector<HostFact>& list) {
        std::uint64_t n = 0;
        if (!count(n))
            return false;
        list.assign(static_cast<std::size_t>(n), {});
        return std::all_of(list.begin(), list.end(), [&](HostFact& f) {
            return text(f.label) && text(f.value) && text(f.note) && flag(f.problem) &&
                   text(f.help);
        });
    }
    bool pairs(std::vector<std::pair<std::string, std::string>>& list) {
        std::uint64_t n = 0;
        if (!count(n))
            return false;
        list.assign(static_cast<std::size_t>(n), {});
        return std::all_of(list.begin(), list.end(),
                           [&](auto& p) { return text(p.first) && text(p.second); });
    }
    bool done() const noexcept {
        return rest_.empty();
    }

  private:
    // A count no larger than the bytes left could hold (every item takes at
    // least 8), so a damaged count cannot ask for a huge allocation.
    bool count(std::uint64_t& n) {
        return number(n) && n <= rest_.size() / 8;
    }
    std::string_view rest_;
};

// Every field, in one order, for either class. Report is HostReport or
// const HostReport.
template <class Io, class Report> bool report_io(Io& io, Report& r) {
    return io.status(r.status.status) && io.text(r.status.text) && io.text(r.status_name) &&
           io.text(r.title) && io.text(r.problem) && io.text(r.plays) && io.text(r.cause) &&
           io.text(r.fix) && io.text(r.note) && io.text(r.action) && io.text(r.explanation) &&
           io.text(r.filter_name) && io.text(r.filter_note) && io.text(r.filter_rate) &&
           io.text(r.filter_channels) && io.text(r.filter_length) && io.text(r.filter_speakers) &&
           io.text(r.filter_taps) && io.text(r.filter_identity) && io.text(r.filter_saved_name) &&
           io.text(r.filter_path) && io.texts(r.filter_files) && io.facts(r.audio) &&
           io.text(r.settings) && io.rate(r.sample_rate) && io.flag(r.latency_waits) &&
           io.flag(r.latency_next_start) && io.text(r.mix_note) && io.text(r.gain_note) &&
           io.pairs(r.reference) && io.text(r.log_path) && io.flag(r.log_written);
}
} // namespace

std::string encode_report(const HostReport& r) {
    ReportWriter w;
    w.number(report_encoding);
    report_io(w, r);
    return std::move(w.bytes);
}

std::optional<HostReport> decode_report(std::string_view bytes) {
    ReportReader reader(bytes);
    std::uint64_t encoding = 0;
    HostReport r;
    if (!reader.number(encoding) || encoding != report_encoding || !report_io(reader, r) ||
        !reader.done())
        return std::nullopt;
    return r;
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
    auto text = report_text(r) + "\nRecent Convolver log lines for " + format + ":\n";
    const auto recent = host_log_recent(format);
    return text + (recent.empty() ? "(none found)\n" : recent);
}
} // namespace convolver
