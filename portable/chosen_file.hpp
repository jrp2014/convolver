// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PLUG-26: a file chosen in a plug-in window (Choose… or a drop): a
// Convolver .vstpreset through the reader convolver-preset uses
// (presets::read_vst3_preset_file), anything else through read_filter_file().
// For the VST3 plug-in and the Audio Unit, whose windows choose files; LV2,
// which has no window, keeps read_filter_file() alone. Reads files: control
// or worker thread only, never audio processing.
#include "filter_file.hpp"
#include <optional>
#include <string>
#include <vector>

namespace convolver {
// As read_filter_file(), and for a .vstpreset (by its name's ending, in any
// letter case): its filter, with the Gain, Mix and Latency it holds in
// HostState::settings for the adapter to apply when the filter is accepted
// (none in a preset from convolver-preset or one saved at the default
// settings, so the current ones stay). Its bypass flag is cleared: the host's
// Bypass stays as it is. The preset is not checked against `host` here: the
// adapter's HostFilter::load() checks it as for a preset from the host's
// menu. `files` receives the preset alone and `wider_entry` is emptied. A
// preset that cannot be read, belongs to another plug-in, is damaged or
// holds no filter is refused with `failure` saying what to do.
std::optional<HostState> read_chosen_file(const std::string& path, const HostLayout& host,
                                          const detail::SoundFiles& reader,
                                          FilterFileFailure& failure,
                                          std::vector<std::string>* files = nullptr,
                                          std::string* wider_entry = nullptr);
} // namespace convolver
