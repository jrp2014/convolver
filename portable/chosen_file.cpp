// SPDX-License-Identifier: GPL-2.0-or-later
#include "chosen_file.hpp"
#include "advice.hpp"
#include "presets.hpp"
#include "utf8.hpp"
#include <exception>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace convolver {
namespace {
constexpr const char* use_instead =
    "Choose a .vstpreset that a VST3 host saved from Convolver or that convolver-preset made, "
    "or choose the filter itself (an IR file, .cfg or configuration list).";
} // namespace

std::optional<HostState> read_chosen_file(const std::string& path, const HostLayout& host,
                                          const detail::SoundFiles& reader,
                                          FilterFileFailure& failure,
                                          std::vector<std::string>* files,
                                          std::string* wider_entry) {
    std::filesystem::path file;
    try {
        file = detail::utf8_path(path);
    } catch (const std::exception&) {
        // Not a name this system can use (a damaged Recent entry):
        // read_filter_file() refuses it with its explanation.
        return read_filter_file(path, host, reader, failure, files, wider_entry);
    }
    if (presets::format_for(file) != presets::Format::vst3)
        return read_filter_file(path, host, reader, failure, files, wider_entry);
    if (files)
        files->clear();
    if (wider_entry)
        wider_entry->clear();
    failure = {};
    HostState state;
    try {
        state = presets::read_vst3_preset_file(file).state;
    } catch (const NewerHostState& e) {
        failure.kind = HostFailureKind::state_newer;
        failure.message = detail::parse_advice(e.what());
        failure.message.problem = path + ": " + failure.message.problem;
        return std::nullopt;
    } catch (const std::invalid_argument& e) {
        // Another plug-in's preset, or a damaged one.
        failure.kind = HostFailureKind::state_unreadable;
        failure.message = {path + ": " + detail::advice_problem(e.what()),
                           "Only a .vstpreset that holds a Convolver filter can be loaded here.",
                           use_instead,
                           {}};
        return std::nullopt;
    } catch (const std::exception& e) { // Cannot be opened, too large, or out of memory.
        failure.kind = HostFailureKind::filter_file;
        failure.message = detail::parse_advice(e.what());
        // Too large, or out of memory: name the file and say what to do.
        if (failure.message.fix.empty()) {
            if (failure.message.problem.find(path) == std::string::npos)
                failure.message.problem = path + ": " + failure.message.problem;
            failure.message.fix = use_instead;
        }
        return std::nullopt;
    }
    if (!state.configuration) {
        failure.kind = HostFailureKind::state_unreadable;
        failure.message = {path + " holds no filter",
                           "It was saved from Convolver while no filter was loaded, or after a "
                           "load had failed.",
                           use_instead,
                           {}};
        return std::nullopt;
    }
    state.bypass = false;
    if (files)
        files->push_back(path);
    return state;
}
} // namespace convolver
