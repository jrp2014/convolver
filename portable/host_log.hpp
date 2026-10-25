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
// `problem`, otherwise as information. The first line a process writes is
// preceded by its execution_line(), and every line names the process ID, so
// lines from several hosts or sessions can be told apart. Never throws. A
// line that cannot be written to the file goes to standard error instead
// (and on Windows to the debugger output, OutputDebugString), preceded once
// by why the file could not be written.
void host_log(const char* format, const std::string& message, bool problem = true) noexcept;
// DIAG-1: false once a line could not be written to the file (no folder,
// no permission, no home folder), until a later line is. Status lines then
// say so instead of pointing to the log. CONVOLVER_LOG set empty turns the
// file off on purpose: lines then go only to the macOS system log, and this
// stays true.
bool host_log_written() noexcept;
// What ran, for support: Convolver's version, the FFT backend and its
// version, the CPU kernels, the plug-in SDK it was built with (when the
// adapter has said), the process ID and the host program.
std::string execution_line();
// The format SDK the adapter was built with, such as "VST3 SDK
// 3.8.1_build_84, commit 3cdf9ca5d1f5" (CONVOLVER_SDK_DESCRIPTION from the
// build, CI-6), for execution_line(). Call it before the first host_log();
// `description` must live as long as the process (a string literal).
void set_host_log_sdk(const char* description) noexcept;
// The lines every adapter logs, so that the log reads the same for each
// format. After an accepted load: what was loaded and, for a saved failed
// load, why it plays silence; `host_known` says whether it was checked
// against the host yet. Both are numbered with the request ("Request 3: "),
// so a sequence of loads can be followed.
std::string loaded_line(const HostFilter&, bool host_known);
// After a rejected load: HostFilter::rejection()'s message, numbered as
// loaded_line() numbers accepted ones ("Request 3: "); empty if there is none.
std::string rejected_line(const HostFilter&);
// PLUG-10: what Gain and Mix let through while no filter is heard, with
// equal input and output counts (otherwise Mix does not apply and there is
// silence). `identity` true: no filter is loaded, so the filtered path is the
// input itself; false: a load failed, so it is silence and only Mix's dry
// share plays. `level` is the output's level against the input, such as
// "-20.0 dB", empty when the sound is `silent` or `unchanged`.
struct UnfilteredSound {
    bool silent = false, unchanged = false;
    std::string level;
};
UnfilteredSound unfiltered_sound(const HostSettings&, bool identity);
// PLUG-10: the settings in words, such as "gain 0 dB, mix 100% wet".
// `equal_counts` false says the mix does not apply.
std::string settings_words(const HostSettings&, bool equal_counts);
// After the host starts (activates or initializes) the instance: the rate,
// channels and block size, then what plays and why (the failure or speaker
// notice in full), and the settings in use when the adapter has them
// (settings_words(); PLUG-10).
std::string started_line(const HostFilter&, const HostLayout&, std::size_t max_frames,
                         const HostSettings* settings = nullptr);
} // namespace convolver
