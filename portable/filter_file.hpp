// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PLUG-9: the one way from a filter file (a .cfg, a configuration list or a
// sound file) to the state a plug-in saves. convolver-preset and the
// plug-ins share it, so a file loaded in a plug-in's window gives the same
// saved state as the preset the tool writes for the same rate and channels
// (docs/project/plans/2026-10-24-plugin-editor.md#how-the-window-connects).
// Reads files: control or worker thread only, never audio processing.
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "sound_files.hpp"
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace convolver {
// The filter `path` holds, chosen from a list by `selection` as the CLI
// chooses, with its coefficients embedded, its sound files read by `reader`
// (detail::libsndfile_files() or, in the Audio Unit, audio_toolbox_files()).
// `raw_filters` receives the raw filters (.pcm, .raw) it read. Throws as
// load_configuration() does.
HostState filter_file_state(const std::filesystem::path& path,
                            const ConfigurationSelection& selection,
                            const detail::SoundFiles& reader,
                            std::vector<std::filesystem::path>* raw_filters = nullptr);

// Why a chosen filter file could not be used, for HostFilter::reject().
struct FilterFileFailure {
    HostFailureKind kind = HostFailureKind::filter_file;
    std::string message;
    // For a readable file made for another rate or other channels: its rate
    // and counts, without its paths, to pass to HostFilter::reject().
    std::optional<Configuration> carried;
    const Configuration* filter() const noexcept {
        return carried ? &*carried : nullptr;
    }
};
// For a plug-in: the filter in `path` for `host`'s rate and channels, or none
// with `failure` filled in. A readable file made for another rate or other
// channels is classified as such; the message names the file and explains.
std::optional<HostState> read_filter_file(const std::string& path, const HostLayout& host,
                                          const detail::SoundFiles& reader,
                                          FilterFileFailure& failure);
} // namespace convolver
