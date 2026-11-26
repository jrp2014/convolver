// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PLUG-26: a file chosen in a plug-in window (Choose File… or a drop): a
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
// (none in a preset from convolver-preset or, until PLUG-35, one saved at
// the default settings, so the current ones stay). Its bypass flag is
// cleared: the host's Bypass stays as it is. `host`, `settled` and
// `keep_unsuited` as for read_filter_file() (PLUG-27, PLUG-42). The preset is not checked
// against `host` here: the adapter's HostFilter::load() checks it as for a
// preset from the host's menu. `files` receives the preset alone and `wider_entry` is emptied. A
// preset that cannot be read, belongs to another plug-in, is damaged or
// holds no filter is refused with `failure` saying what to do. PLUG-53:
// the preset's own name stays in HostState::preset_name, and `filter_name`
// receives the filter file's name its controller state keeps (empty when it
// keeps none, and for any file that is not a .vstpreset), for the window to
// show beside the preset's name.
std::optional<HostState>
read_chosen_file(const std::string& path, const std::optional<HostLayout>& host,
                 const detail::SoundFiles& reader, FilterFileFailure& failure,
                 std::vector<std::string>* files = nullptr, std::string* wider_entry = nullptr,
                 bool settled = true, bool keep_unsuited = false,
                 std::string* filter_name = nullptr);
} // namespace convolver
