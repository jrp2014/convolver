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
    choose,
    recent,
    reload,
    watch,
    // The settings: Gain, Mix and Latency, each with setting_stride
    // identifiers from settings + setting_stride * i: its label, its
    // trackbar or list, its value (a typed box, or the time a Latency adds),
    // its unit, and Mix's Dry and Wet.
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

// Timers: the report twice a second while the window is on screen, and the
// filter's files once a second while it is open (Reload When the File
// Changes); Copy Diagnostics' "Copied" for two seconds.
enum Timer : unsigned { refresh_timer = 1, files_timer, copied_timer };
} // namespace convolver::vst3::win
