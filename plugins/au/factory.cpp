// SPDX-License-Identifier: GPL-2.0-or-later
// The component's entry point, named by factoryFunction in Info.plist. It is
// the only symbol the bundle exports.
#include "effect.hpp"
#include <AudioUnitSDK/AUPlugInDispatch.h>

using ConvolverAU = convolver::au::Effect;
AUSDK_COMPONENT_ENTRY(ausdk::AUBaseFactory, ConvolverAU)
