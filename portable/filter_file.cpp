// SPDX-License-Identifier: GPL-2.0-or-later
#include "filter_file.hpp"
#include "config_parse.hpp"
#include <algorithm>
#include <exception>
#include <utility>

namespace convolver {
HostState filter_file_state(const std::filesystem::path& path,
                            const ConfigurationSelection& selection,
                            const detail::SoundFiles& reader,
                            std::vector<std::filesystem::path>* raw_filters, bool plugin) {
    HostState state;
    state.configuration =
        detail::load_configuration(path, selection, reader, nullptr, raw_filters, plugin);
    return state;
}

std::optional<HostState> read_filter_file(const std::string& path, const HostLayout& host,
                                          const detail::SoundFiles& reader,
                                          FilterFileFailure& failure) {
    failure.kind = HostFailureKind::filter_file;
    failure.carried.reset();
    try {
        // Explained in the host's terms, not the command line's (jrp's
        // GarageBand run: the window advised omitting --output-channels).
        return filter_file_state(path, {host.sample_rate, host.inputs, host.outputs}, reader,
                                 nullptr, true);
    } catch (const detail::NoMatch& e) {
        // Readable, but made for another rate or other channels. A list is
        // a rate refusal only when no entry has the host's rate.
        const auto& checked = *e.candidates;
        if (!checked.empty())
            failure.kind = std::any_of(checked.begin(), checked.end(),
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
        failure.message = "The filter file \"" + path + "\" could not be used: " + e.what();
    } catch (const std::exception& e) {
        failure.message = "The filter file \"" + path + "\" could not be used: " + e.what();
    }
    return std::nullopt;
}
} // namespace convolver
