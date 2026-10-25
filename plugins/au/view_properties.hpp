// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: the custom properties between the Audio Unit and its own window,
// in a header of their own so the window's Objective-C++ code does not need
// the AudioUnitSDK. Apple's range for private properties starts at 64000.
// Internal between the window and the unit, which always ship together: not
// a stable interface, so not in plugins/identities.json.
#pragma once
#include <AudioToolbox/AudioToolbox.h>

namespace convolver::au {
// Read-only, global. A CFDictionaryRef of CFString values, report_fields()
// (portable/host_report.hpp), which the caller releases (QA1684). The unit
// signals a property change whenever the status may have changed.
inline constexpr AudioUnitPropertyID report_property = 64000;
// Read-only, global. A CFStringRef the caller releases: diagnostics_text(),
// the report and this instance's recent Convolver log lines, for Copy
// diagnostics. Reads the log file.
inline constexpr AudioUnitPropertyID diagnostics_property = 64001;
// Read and write, global. A CFStringRef: setting it loads that filter file (a
// .cfg, a configuration list or a sound file) for the unit's rate and
// channels, as convolver-preset reads it, and the saved state then holds the
// filter, not the path. The result shows in the report; setting it returns
// an error if the file was not loaded. Reading it gives the path of the
// file loaded in this session, empty if the filter came from saved state.
// The caller releases what it reads.
inline constexpr AudioUnitPropertyID load_property = 64002;
} // namespace convolver::au
