// SPDX-License-Identifier: GPL-2.0-or-later
// Equalizer APO export: Convolver configurations as Equalizer APO commands.
// See docs/equalizer-apo.md for the mapping and its limits.
#pragma once
#include "convolver/engine.hpp"
#include <cstddef>
#include <string>
#include <vector>

namespace convolver::equalizer_apo {
// One configuration, in the input's order: as in a list, the first match wins.
struct Source {
    Configuration configuration;
    std::string label; // Shown in comments, for example the .cfg file name.
};
// A mono 32-bit float WAV that the text names, relative to the text file.
struct ImpulseFile {
    std::string name;
    int sample_rate = 0;
    std::vector<float> samples;
};
struct Filter {
    std::size_t source = 0; // Index into the sources.
    std::size_t device_channels = 0;
    // The largest device it applies to, at its rate: devices with more
    // channels than it has go to the largest filter that fits.
    std::size_t largest_device = 0;
};
struct Export {
    std::string text; // UTF-8 configuration file for Equalizer APO's Include command.
    std::vector<ImpulseFile> impulses;
    std::vector<Filter> filters;      // Exported sources, in the order they are tried.
    std::vector<std::string> skipped; // Why a source was left out.
};
// The text file's name inside the exported folder.
inline constexpr const char* text_file_name = "convolver.txt";
// Largest device a filter applies to when the device has more channels than
// the filter. The export silences the extra channels one by one, so it needs
// a bound; larger devices are silenced entirely.
inline constexpr std::size_t max_device_channels = 32;
// Device channels a configuration occupies: its inputs and outputs share them.
std::size_t device_channels(const Configuration&) noexcept;
// header: comment lines (without '#') placed at the top of the text.
Export make_export(const std::vector<Source>&, const std::vector<std::string>& header);
} // namespace convolver::equalizer_apo
