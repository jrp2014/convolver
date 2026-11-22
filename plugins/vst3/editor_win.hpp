// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-12: what a test (tests/vst3/editor_win_tests.cpp) needs to find in
// Convolver's window on Windows (editor_win.cpp): the child window's class
// and each control's identifier, for GetDlgItem, and the timers it runs.
#pragma once

namespace convolver::vst3::win {
// The window the view puts in the host's (a child of the host's HWND).
inline constexpr wchar_t window_class[] = L"ConvolverVST3Window";

// The controls, in reading and Tab order.
enum Control : int {
    title = 100,
    bypass,
    filter_name,
    status,
    fact_values,         // Sample rate, Channels, Length, Speakers: fact_values + 0 to 3.
    fact_headings = 110, // Their headings, fact_headings + 0 to 3.
    filter_note = 120,
    message,
    note_label,
    note,
    choose, // PLUG-50: the Choose Filter split button, which holds every file action.
    // The settings: Gain, Gain mode, Mix and Latency, each with
    // setting_stride identifiers from settings + setting_stride * i: its
    // label, its trackbar or list, its value (a typed box, the note under
    // the Gain row that Gain mode leads, or the time a Latency adds), its
    // unit, and Mix's Dry and Wet. Gain mode's list comes before Gain's
    // trackbar in Tab order, as it does on screen; its label is not shown.
    settings = 200,
    mix_note = 290,
    audio_heading = 300,
    audio_rows = 310, // Each fact: its label, value and note, from audio_rows + 3 * i.
    reference_toggle = 400,
    diagnostics,
    log,
    reference,
};
enum SettingPart : int { label = 0, control, value, unit, low, high };
inline constexpr int setting_stride = 10;

// PLUG-50: the Choose Filter menu's commands, which the window also takes
// as WM_COMMAND (so a test need not open the menu): Choose File…, Reload,
// Reload When the File Changes (ticked by default), Clear Recent, and the
// recent files from menu_recent_first in the menu's order.
enum MenuCommand : int {
    menu_choose = 990,
    menu_reload,
    menu_watch,
    menu_clear_recent,
    menu_recent_first = 1000,
};

// Timers: the report twice a second while the window is on screen, and the
// filter's files once a second while it is open (Reload When the File
// Changes); Copy Diagnostics' "Copied" for two seconds.
enum Timer : unsigned { refresh_timer = 1, files_timer, copied_timer };
} // namespace convolver::vst3::win
