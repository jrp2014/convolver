// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver_version.hpp"
#include "processor.hpp"
#include "public.sdk/source/main/pluginfactory.h"
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver;
using namespace convolver::vst3;
BEGIN_FACTORY_DEF(identity::vendor.data(), identity::vendor_url.data(), "")
DEF_CLASS2(INLINE_UID_FROM_FUID(processor_id), PClassInfo::kManyInstances, kVstAudioEffectClass,
           "Convolver", 0, "Fx|Reverb", CONVOLVER_VERSION_STRING, kVstVersionString,
           Processor::create)
DEF_CLASS2(INLINE_UID_FROM_FUID(controller_id), PClassInfo::kManyInstances,
           kVstComponentControllerClass, "Convolver Controller", 0, "", CONVOLVER_VERSION_STRING,
           kVstVersionString, Controller::create)
END_FACTORY
bool InitModule() {
    return true;
}
bool DeinitModule() {
    return true;
}
