// SPDX-License-Identifier: GPL-2.0-or-later
#include "filter_file.hpp"
#include <exception>
#include <utility>

namespace convolver {
HostState filter_file_state(const std::filesystem::path& path,
                            const ConfigurationSelection& selection,
                            const detail::SoundFiles& reader,
                            std::vector<std::filesystem::path>* raw_filters) {
    HostState state;
    state.configuration = detail::load_configuration(path, selection, reader, nullptr, raw_filters);
    return state;
}

std::optional<HostState> read_filter_file(const std::string& path, const HostLayout& host,
                                          const detail::SoundFiles& reader,
                                          FilterFileFailure& failure) {
    try {
        // Explained in the host's terms, not the command line's (jrp's
        // GarageBand run: the window advised omitting --output-channels).
        HostState state;
        state.configuration = detail::load_configuration(
            path, {host.sample_rate, host.inputs, host.outputs}, reader, nullptr, nullptr, true);
        return state;
    } catch (const std::exception& e) {
        // Readable, but made for another rate or other channels? Then the
        // record's kind says so; the message already explains. (A file that
        // fails for another reason as well is classified by rate or channels.)
        failure.kind = HostFailureKind::filter_file;
        failure.carried.reset();
        try {
            auto any = detail::load_configuration(path, {}, reader);
            failure.kind = any.sample_rate != host.sample_rate ? HostFailureKind::sample_rate
                                                               : HostFailureKind::channels;
            any.paths.clear(); // Only the rate and counts are kept.
            failure.carried = std::move(any);
        } catch (...) {
        }
        failure.message = "The filter file \"" + path + "\" could not be used: " + e.what();
    }
    return std::nullopt;
}
} // namespace convolver
