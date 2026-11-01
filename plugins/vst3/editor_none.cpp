// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11: no window yet on this system (VST3-12 for Windows, VST3-13 for
// Linux); hosts show their generic controls.
#include "editor.hpp"

namespace convolver::vst3 {
Steinberg::IPlugView* create_editor(Controller&) {
    return nullptr;
}
} // namespace convolver::vst3
