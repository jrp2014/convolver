// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "public.sdk/source/main/pluginfactory.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::vst3;
BEGIN_FACTORY_DEF("Convolver", "https://github.com/jrp2014/convolver", "")
DEF_CLASS2(INLINE_UID_FROM_FUID(processor_id), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Convolver", 0, "Fx|Reverb", "0.1.0", kVstVersionString, Processor::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(controller_id), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "Convolver Controller", 0, "", "0.1.0", kVstVersionString,
           Controller::create)
END_FACTORY
bool InitModule() {
    return true;
}
bool DeinitModule() {
    return true;
}
