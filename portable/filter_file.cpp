// SPDX-License-Identifier: GPL-2.0-or-later
#include "filter_file.hpp"
#include <exception>
#include <utility>

namespace convolver {
HostState filter_file_state(const std::filesystem::path& path,
                            const ConfigurationSelection& selection,
                            std::vector<std::filesystem::path>* raw_filters) {
    HostState state;
    state.configuration = load_configuration(path, selection, nullptr, raw_filters);
    return state;
}

std::optional<HostState> read_filter_file(const std::string& path, const HostLayout& host,
                                          FilterFileFailure& failure) {
    try {
        return filter_file_state(path, {host.sample_rate, host.inputs, host.outputs});
    } catch (const std::exception& e) {
        // Readable, but made for another rate or other channels? Then the
        // record's kind says so; the message already explains. (A file that
        // fails for another reason as well is classified by rate or channels.)
        failure.kind = HostFailureKind::filter_file;
        try {
            const auto any = load_configuration(path);
            failure.kind = any.sample_rate != host.sample_rate ? HostFailureKind::sample_rate
                                                               : HostFailureKind::channels;
        } catch (...) {
        }
        failure.message = "The filter file \"" + path + "\" could not be used: " + e.what();
    }
    return std::nullopt;
}
} // namespace convolver
