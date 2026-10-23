// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include <cstddef>
#include <filesystem>
#include <string>

// DIAG-1: the Convolver log, where plug-ins without a host log interface (VST3,
// Audio Unit) write what they load and start with, and why a filter was refused
// or why they play silence, in full, so that a user can send the file with a
// report (docs/use/plugins.md#status-and-the-convolver-log). Control thread
// only: never from audio processing.
namespace convolver {
// Where the log goes: CONVOLVER_LOG when set (empty turns the file off, except
// on Windows, where setting a variable empty removes it);
// otherwise ~/Library/Logs/Convolver/Convolver.log on macOS,
// %LOCALAPPDATA%\Convolver\Convolver.log on Windows, and
// $XDG_STATE_HOME/convolver/convolver.log (default ~/.local/state) elsewhere.
// Empty when there is nowhere to write.
std::filesystem::path host_log_path();
// Appends one dated line, "<time> Convolver <version> <format> in <program>:
// <message>" (the host's program name, where the platform gives it),
// creating the folder if needed. When the file has grown past 1 MB it is first renamed
// with ".old" added (replacing an older one). On macOS the message also goes
// to the unified log (Console, search for "Convolver"), as an error when
// `problem`, otherwise as information. Never throws; a
// message that cannot be written is dropped.
void host_log(const char* format, const std::string& message, bool problem = true) noexcept;
// The lines every adapter logs, so that the log reads the same for each
// format. After an accepted load: what was loaded and, for a saved failed
// load, why it plays silence; `host_known` says whether it was checked
// against the host yet.
std::string loaded_line(const HostFilter&, bool host_known);
// After the host starts (activates or initializes) the instance: the rate,
// channels and block size, then what plays and why (the failure or speaker
// notice in full).
std::string started_line(const HostFilter&, const HostLayout&, std::size_t max_frames);
} // namespace convolver
