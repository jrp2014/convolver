// SPDX-License-Identifier: GPL-2.0-or-later
#include "filter_file.hpp"
#include "advice.hpp"
#include "config_parse.hpp"
#include <algorithm>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace convolver {
namespace {
// DIAG-2: the reader's message, always naming the chosen file by its full
// path, so that the log says which folder's file was refused (the window
// shows only the file name; host_report.cpp).
Advice refusal(const std::string& path, const char* what) {
    auto a = detail::parse_advice(what);
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
            "Set the host's project or audio device to " + made + ".",
            {}};
}
} // namespace

HostState filter_file_state(const std::filesystem::path& path,
                            const ConfigurationSelection& selection,
                            const detail::SoundFiles& reader,
                            std::vector<std::filesystem::path>* raw_filters, bool plugin,
                            bool options, std::vector<std::filesystem::path>* sources) {
    HostState state;
    state.configuration = detail::load_configuration(path, selection, reader, nullptr, raw_filters,
                                                     plugin, options, sources);
    return state;
}

std::optional<HostState> read_filter_file(const std::string& path, const HostLayout& host,
                                          const detail::SoundFiles& reader,
                                          FilterFileFailure& failure,
                                          std::vector<std::string>* files) {
    failure.kind = HostFailureKind::filter_file;
    failure.carried.reset();
    if (files)
        files->clear();
    try {
        // Explained in the host's terms, not the command line's (jrp's
        // GarageBand run: the window advised omitting --output-channels).
        std::vector<std::filesystem::path> sources;
        auto state = filter_file_state(path, {host.sample_rate, host.inputs, host.outputs}, reader,
                                       nullptr, true, false, &sources);
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
            for (const auto& source : sources) {
                const auto text = source.u8string();
                named.emplace_back(text.begin(), text.end());
            }
            *files = std::move(named);
        }
        return state;
    } catch (const detail::NoMatch& e) {
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
        if (!e.list && checked.size() == 1) {
            Configuration only;
            only.sample_rate = checked.front().sample_rate;
            only.input_channels = checked.front().input_channels;
            only.output_channels = checked.front().output_channels;
            failure.carried = std::move(only);
        }
        failure.message = refusal(path, e.what());
        // VST3-9: the selection looked for the whole part of a fractional
        // rate, which no filter can match; say so instead.
        if (host.fractional_rate != 0 && !checked.empty()) {
            std::vector<int> rates;
            for (const auto& c : checked)
                rates.push_back(c.sample_rate);
            failure.message =
                fractional_rate_refusal(path, host, std::move(rates), !failure.carried);
        }
    } catch (const std::exception& e) {
        failure.message = refusal(path, e.what());
    }
    return std::nullopt;
}
} // namespace convolver
