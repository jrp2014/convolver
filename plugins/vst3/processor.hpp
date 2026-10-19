// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <atomic>
#include <mutex>

namespace convolver::vst3 {
inline const Steinberg::FUID processor_id(0x73A81329, 0xE6484ADF, 0x9DA7D731, 0xA8C94201);
inline const Steinberg::FUID controller_id(0x622AD87C, 0x23664CCF, 0xB157473F, 0xBCD4A6B9);
inline constexpr Steinberg::Vst::ParamID bypass_id = 0;
// One input and one output bus, each with 1 to 8 channels (counts may differ).
inline constexpr int max_bus_channels = 8;
HostState read_state(Steinberg::IBStream*);
bool write_state(Steinberg::IBStream*, const HostState&);
class Processor final : public Steinberg::Vst::AudioEffect {
  public:
    Processor();
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement*,
                                                     Steinberg::int32,
                                                     Steinberg::Vst::SpeakerArrangement*,
                                                     Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup&) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData&) override;
    Steinberg::uint32 PLUGIN_API getLatencySamples() override {
        return 256;
    }
    Steinberg::uint32 PLUGIN_API getTailSamples() override {
        return tail_.load();
    }
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;

  private:
    Configuration configuration(const HostState&) const;
    template <class T>
    bool segment(Steinberg::Vst::ProcessData&, Steinberg::int32, Steinberg::int32) noexcept;
    std::mutex control_;
    HostState state_;
    std::unique_ptr<HostProcessor> host_;
    std::atomic<bool> bypass_{false};
    std::atomic<Steinberg::uint32> tail_{0};
    int inputs_ = 2, outputs_ = 2;
    bool active_ = false;
};
class Controller final : public Steinberg::Vst::EditController {
  public:
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream*) override;
};
} // namespace convolver::vst3
