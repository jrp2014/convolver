// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// VST3-11, AU-6: the filter files recently loaded in a Convolver window, kept
// per user in a small text file beside the Convolver log, so every host and
// instance shares them and no project holds them (jrp, 25 October 2026;
// docs/project/plans/2026-10-24-plugin-editor.md#what-the-window-does). One UTF-8
// path per line, newest first. Control thread only (a window's main
// thread): never from audio processing.
#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace convolver {
// How many files the list keeps.
inline constexpr std::size_t recent_files_kept = 10;
// The list's file: recent-filters.txt in the Convolver log's folder
// (host_log_path()); empty when there is no log, for example with
// CONVOLVER_LOG set empty.
std::filesystem::path recent_files_path();
// The files, newest first; empty when there is no list or it cannot be read.
std::vector<std::string> recent_files() noexcept;
// Puts `path` (UTF-8) first, removing an older entry for it and keeping
// recent_files_kept. The file is replaced whole (written beside it, then
// renamed over it), so a reader never sees half a list; two windows saving
// at once can lose one of the two new entries. A path holding a line break
// is not kept. Returns whether the list was saved.
bool remember_recent_file(const std::string& path) noexcept;
// Empties the list (a menu's Clear Menu). Returns whether it was saved.
bool forget_recent_files() noexcept;
} // namespace convolver
