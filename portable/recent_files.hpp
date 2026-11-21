// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// VST3-11, AU-6: the filter files recently loaded in a Convolver window, kept
// per user in a small text file beside the Convolver log, so every host and
// instance shares them and no project holds them (jrp, 25 October 2026;
// docs/archive/plans/2026-10-24-plugin-editor.md#what-the-window-does). One UTF-8
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

// PLUG-47 (jrp's review, 21 November 2026): a file a window chose that was
// refused, but that the plug-in then loads by itself (PLUG-40's
// configuration list, read again once the host plays), joins the list when
// the window first shows it playing, as it would have when chosen. One per
// window, on its main thread.
class RefusedRecentFile {
  public:
    // A chosen file was refused while `playing` (the accepted file's path,
    // empty for none) plays. The same file as the one playing is not
    // waited for: it is in the list already.
    void refused(const std::string& path, const std::string& playing);
    // A chosen file was accepted: none waits any longer.
    void accepted() noexcept {
        path_.clear();
    }
    // The window shows `playing` as the accepted file: remembers the refused
    // file when it is that one. Returns whether it did.
    bool shown(const std::string& playing) noexcept;

  private:
    std::string path_;
};
} // namespace convolver
