// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-11: Convolver's window, the view the host gets from
// Controller::createView(ViewType::kEditor). On macOS it is the Audio Unit's
// AppKit window (plugins/editor/mac/) behind an IPlugView of type
// kPlatformTypeNSView, built on the SDK's EditorView (editor.mm). Windows and
// Linux have no window yet (VST3-12, VST3-13): there it is null, and hosts
// show their generic controls.
#pragma once
#include "pluginterfaces/gui/iplugview.h"

namespace convolver::vst3 {
class Controller;
// A new view for `controller` (the caller owns the reference), or null.
Steinberg::IPlugView* create_editor(Controller& controller);
} // namespace convolver::vst3
