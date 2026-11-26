// SPDX-License-Identifier: GPL-2.0-or-later
#include "filter_file.hpp"
#include "advice.hpp"
#include "config_parse.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace convolver {
namespace {
// DIAG-2: the reader's message, always naming the chosen file by its full
// path, so that the log says which folder's file was refused (the window
// shows only the file name; host_report.cpp).
Advice refusal(const std::string& path, const std::exception& e) {
    auto a = detail::advice_of(e);
    if (a.problem.find(path) == std::string::npos)
        a.problem = path + ": " + a.problem;
    return a;
}
// VST3-9: a host rate that is not a whole number of hertz, which no filter
// can match: `rates` are those of the filter, or of a list's entries.
Advice fractional_rate_refusal(const std::string& path, const HostLayout& host,
                               std::vector<int> rates, bool list) {
    std::sort(rates.begin(), rates.end());
    rates.erase(std::unique(rates.begin(), rates.end()), rates.end());
    std::string made;
    for (std::size_t i = 0; i < rates.size(); ++i)
        made += (i == 0                  ? ""
                 : i + 1 == rates.size() ? " or "
                                         : ", ") +
                std::to_string(rates[i]) + " Hz";
    const auto runs = host_rate_text(host);
    return {path + (list ? ": no filter in the list is for the host's " + runs
                         : ": the filter is for " + made + ", and the host runs at " + runs),
            "Convolver never resamples, and filters are made for a whole number of hertz, so none "
            "plays at the host's rate.",
            "Set the host's project or audio device to " + made + ". " +
                std::string(detail::host_playback_rate_fix),
            {}};
}
// PLUG-33: on a host with more inputs than any candidate for its rate and
// outputs, the input count to read with: the most inputs below the host's
// among the candidates for the host's rate and exact outputs, so a list's
// entry that reads the most of the host's inputs plays (its first such entry
// in list order, as the selection takes it), or 0 when none fits.
std::size_t wider_bus_inputs(const std::vector<detail::SelectionCandidate>& candidates,
                             const HostLayout& host) {
    std::size_t inputs = 0;
    for (const auto& c : candidates)
        if (c.sample_rate == host.sample_rate && c.output_channels == host.outputs &&
            c.input_channels > 0 && c.input_channels < host.inputs)
            inputs = std::max(inputs, c.input_channels);
    return inputs;
}
// PLUG-27: whether `path` is a configuration list, read as text alone (no
// sound file is decoded). Anything that cannot be read or parsed is not:
// the read that follows explains it.
bool configuration_list(const std::string& path) noexcept {
    try {
        const auto file = detail::utf8_path(path);
        const auto text = detail::read_configuration_text(file);
        return !detail::not_configuration_text(text) &&
               detail::parse_configuration_text(text, file).kind ==
                   detail::ParsedDocument::Kind::list;
    } catch (...) {
        return false;
    }
}
// PLUG-27: jrp's words (20 November 2026) for a list chosen before the host
// has played audio, which the window shows as for any refused file.
Advice list_waits_refusal(const std::string& path) {
    return {path + ": no entry selected",
            "A configuration list's entry is chosen for the host's sample rate and channel "
            "counts, and the host has not played audio through the plug-in yet, so they are not "
            "known.",
            "Play audio once so Convolver can check the host's sample rate and channel counts, "
            "then choose the configuration list again.",
            {}};
}
} // namespace

HostState filter_file_state(const std::filesystem::path& path,
                            const ConfigurationSelection& selection,
                            const detail::SoundFiles& reader,
                            std::vector<std::filesystem::path>* raw_filters, bool plugin,
                            bool options, std::vector<std::filesystem::path>* sources,
                            bool default_layouts) {
    HostState state;
    state.configuration =
        detail::load_configuration(path, selection, reader, nullptr, raw_filters, plugin, options,
                                   sources, nullptr, default_layouts);
    return state;
}

namespace {
std::optional<HostState> read_for(const std::string& path, const std::optional<HostLayout>& known,
                                  const detail::SoundFiles& reader, FilterFileFailure& failure,
                                  std::vector<std::string>* files, std::string* wider_entry,
                                  bool settled) {
    failure.kind = HostFailureKind::filter_file;
    failure.carried.reset();
    if (files)
        files->clear();
    if (wider_entry)
        wider_entry->clear();
    // PLUG-27: while the host is not playing, a single filter is read as it
    // is, to be kept and checked when the host starts; a list's entry is
    // chosen for the configuration the host last started with, if any.
    const bool list = !settled || !known ? configuration_list(path) : false;
    if (!known && list) {
        failure.kind = HostFailureKind::settings_unknown;
        failure.message = list_waits_refusal(path);
        return std::nullopt;
    }
    if (!known || (!settled && !list)) {
        try {
            std::vector<std::filesystem::path> sources;
            // EDGE-3: a four-channel IR file plays as true stereo only when
            // read for two channels, so with the channels the host last
            // started with, a file is read for them where they fit, the rate
            // left open; otherwise as it is, as before.
            std::optional<HostState> fitted;
            if (known)
                try {
                    fitted = filter_file_state(detail::utf8_path(path),
                                               {0, known->inputs, known->outputs}, reader, nullptr,
                                               true, false, &sources);
                } catch (const detail::NoMatch& e) {
                    // A B-format file on two channels is refused with its
                    // reason, not kept as four channels to wait for a start
                    // (jrp's review of a404df0).
                    if (std::any_of(
                            e.candidates->begin(), e.candidates->end(),
                            [](const detail::SelectionCandidate& c) { return c.ambisonic; })) {
                        failure.kind = HostFailureKind::channels;
                        throw;
                    }
                    sources.clear();
                }
            auto state = fitted ? std::move(*fitted)
                                : filter_file_state(detail::utf8_path(path), {}, reader, nullptr,
                                                    true, false, &sources);
            if (files) {
                std::vector<std::string> named;
                for (const auto& source : sources)
                    named.push_back(detail::utf8_string(source));
                *files = std::move(named);
            }
            return state;
        } catch (const std::exception& e) {
            failure.message = refusal(path, e);
        }
        return std::nullopt;
    }
    const auto& host = *known;
    try {
        // Explained in the host's terms, not the command line's (jrp's
        // GarageBand run: the window advised omitting --output-channels).
        std::vector<std::filesystem::path> sources;
        const auto read = [&](std::size_t inputs, bool default_layouts) {
            sources.clear();
            return filter_file_state(detail::utf8_path(path),
                                     {host.sample_rate, inputs, host.outputs}, reader, nullptr,
                                     true, false, &sources, default_layouts);
        };
        HostState state;
        try {
            // EDGE-3: an exact match first, then PLUG-33's fewer inputs,
            // then the default routings, so a list that played an entry
            // for fewer inputs before plays it still.
            state = read(host.inputs, false);
        } catch (const detail::NoMatch& e) {
            // PLUG-33: a chosen file follows the rule a preset follows
            // (PLUG-23, HostFilter::mismatch()): the host's outputs exactly
            // and at least the filter's inputs. Nothing has the host's
            // exact input count, so read again for the fewer inputs that
            // fit (wider_bus_inputs()), or refuse as before.
            const auto inputs =
                host.fractional_rate == 0 ? wider_bus_inputs(*e.candidates, host) : 0;
            if (inputs == 0)
                state = read(host.inputs, true); // Or refused, explaining all.
            else
                state = read(inputs, false);
            // A list's entry is named, so the log and the window can say
            // which one plays (jrp: the choice must be clear to the user).
            // The selection takes the first such entry in list order.
            if (wider_entry && e.list)
                for (const auto& c : *e.candidates)
                    if (c.sample_rate == host.sample_rate && c.output_channels == host.outputs &&
                        c.input_channels == inputs) {
                        *wider_entry = detail::utf8_string(c.path);
                        break;
                    }
        }
        // VST3-9: the selection used the whole part of a fractional rate;
        // a filter made for that whole part does not suit it either.
        if (host.fractional_rate != 0 && state.configuration) {
            const auto& c = *state.configuration;
            failure.kind = HostFailureKind::sample_rate;
            Configuration only;
            only.sample_rate = c.sample_rate;
            only.input_channels = c.input_channels;
            only.output_channels = c.output_channels;
            failure.carried = std::move(only);
            failure.message = fractional_rate_refusal(path, host, {c.sample_rate}, false);
            return std::nullopt;
        }
        if (files) {
            // As UTF-8, which every window's file APIs take.
            std::vector<std::string> named;
            for (const auto& source : sources)
                named.push_back(detail::utf8_string(source));
            *files = std::move(named);
        }
        return state;
    } catch (const detail::NoMatch& e) {
        if (wider_entry)
            wider_entry->clear();
        // Readable, but made for another rate or other channels. A list is
        // a rate refusal only when no entry has the host's rate.
        const auto& checked = *e.candidates;
        if (!checked.empty())
            failure.kind = host.fractional_rate == 0 &&
                                   std::any_of(checked.begin(), checked.end(),
                                               [&](const detail::SelectionCandidate& c) {
                                                   return c.sample_rate == host.sample_rate;
                                               })
                               ? HostFailureKind::channels
                               : HostFailureKind::sample_rate;
        // Only a single filter's rate and counts describe "the filter"; a
        // list's entries differ, and its message lists them.
        // A B-format file refused on two channels carries none, so PLUG-42
        // does not keep it as four channels (jrp's review of a404df0).
        if (!e.list && checked.size() == 1 && !checked.front().ambisonic) {
            Configuration only;
            only.sample_rate = checked.front().sample_rate;
            only.input_channels = checked.front().input_channels;
            only.output_channels = checked.front().output_channels;
            failure.carried = std::move(only);
        }
        failure.message = refusal(path, e);
        // VST3-9: the selection looked for the whole part of a fractional
        // rate, which no filter can match; say so instead.
        if (host.fractional_rate != 0 && !checked.empty()) {
            std::vector<int> rates;
            for (const auto& c : checked)
                rates.push_back(c.sample_rate);
            failure.message =
                fractional_rate_refusal(path, host, std::move(rates), !failure.carried);
        }
        // PLUG-27 (jrp, 21 November 2026): the host is not playing, so the
        // configuration the list was checked against may be a placeholder
        // (Audacity's 44.1 kHz before it first plays). The message keeps
        // naming it, and the fix says what to do if it is one. settled is
        // false whenever the host is not playing at this moment, so this is
        // added after a real start too, and for channels as for rates;
        // following it, the list is chosen while the host plays and gets
        // the normal fix alone, so the advice cannot send anyone round in
        // circles.
        if (e.list && !settled)
            failure.message.fix +=
                std::string(failure.message.fix.empty() ? "" : " ") +
                "If the host has not played audio at your project's rate yet, play audio once so "
                "Convolver can check the host's sample rate and channel counts, then choose the "
                "configuration list again.";
    } catch (const std::exception& e) {
        if (wider_entry) // Refused after the entry was named (out of memory).
            wider_entry->clear();
        failure.message = refusal(path, e);
    }
    return std::nullopt;
}
} // namespace

std::optional<HostState> read_filter_file(const std::string& path,
                                          const std::optional<HostLayout>& known,
                                          const detail::SoundFiles& reader,
                                          FilterFileFailure& failure,
                                          std::vector<std::string>* files, std::string* wider_entry,
                                          bool settled, bool keep_unsuited) {
    auto state = read_for(path, known, reader, failure, files, wider_entry, settled);
    // PLUG-42: a single filter refused only for the host's rate or channels
    // (`carried` holds its own) is read as it is, for HostFilter::load() to
    // keep until a start suits it.
    if (state || !keep_unsuited || !settled || !failure.carried)
        return state;
    return read_for(path, known, reader, failure, files, wider_entry, false);
}
} // namespace convolver
