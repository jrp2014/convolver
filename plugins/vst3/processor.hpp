// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace convolver::vst3 {
// Permanent class IDs from plugins/identities.json (PLUG-4); never change them.
inline const Steinberg::FUID processor_id(identity::vst3_processor_class_id[0],
                                          identity::vst3_processor_class_id[1],
                                          identity::vst3_processor_class_id[2],
                                          identity::vst3_processor_class_id[3]);
inline const Steinberg::FUID controller_id(identity::vst3_controller_class_id[0],
                                           identity::vst3_controller_class_id[1],
                                           identity::vst3_controller_class_id[2],
                                           identity::vst3_controller_class_id[3]);
inline constexpr Steinberg::Vst::ParamID bypass_id = 0;
// DIAG-1: read-only, never automated or saved. Its value is a HostStatus and
// its text the instance's HostStatusLine, which the processor sends to the
// controller in a status_message whenever it may have changed.
inline constexpr Steinberg::Vst::ParamID status_id = 1;
inline constexpr const char* status_message = "ConvolverStatus";
// One input and one output bus, each with 1 to 8 channels (counts may differ).
inline constexpr int min_bus_channels = identity::min_channels;
inline constexpr int max_bus_channels = identity::max_channels;
// The WAV channel-mask bits of a VST3 arrangement: bits 0 to 17 have the same
// meaning in both, and VST3 orders channels by bit. 0 (no named speakers) for
// any arrangement using other speakers, kMono (kSpeakerM) included.
std::uint32_t wav_speakers(Steinberg::Vst::SpeakerArrangement) noexcept;
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
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
    // The filter's advisory notice (HostFilter::notice), for example output
    // speakers that differ from the host's arrangement; logged when it changes.
    std::string notice();
    // Why the last load failed or the instance plays silence (PLUG-8), as
    // HostFilter::failure() keeps it; logged in full, summarised by status().
    std::optional<HostFailure> failure();
    // What the Status parameter shows (DIAG-1).
    HostStatusLine status();

  private:
    void record_activation_failure(const char* why) noexcept;
    // Writes to the Convolver log (host_log), skipping a repeat of the line
    // this instance wrote last, as hosts restart plug-ins often.
    void log(const std::string&, bool problem = true) noexcept;
    // Sends status() to the controller; outside control_, as the controller
    // may call back into the host.
    void send_status() noexcept;
    HostStatusLine status_locked() const;
    Steinberg::tresult load_state(Steinberg::IBStream*);
    Steinberg::tresult activate(Steinberg::TBool);
    HostLayout layout() const;
    std::optional<HostLayout> known_layout() const;
    template <class T>
    bool segment(Steinberg::Vst::ProcessData&, Steinberg::int32, Steinberg::int32) noexcept;
    std::mutex control_;
    HostFilter filter_{max_bus_channels}; // The shared adapter contract's control side.
    std::unique_ptr<HostProcessor> host_;
    std::atomic<bool> bypass_{false};
    std::atomic<Steinberg::uint32> tail_{0};
    int inputs_ = 2, outputs_ = 2;
    Steinberg::Vst::SpeakerArrangement input_arrangement_ = Steinberg::Vst::SpeakerArr::kStereo,
                                       output_arrangement_ = Steinberg::Vst::SpeakerArr::kStereo;
    bool active_ = false;
    // The rate and buses of the last activation. The host's configuration counts
    // as known only while it still matches: setupProcessing alone is not binding.
    std::optional<HostLayout> activated_;
    std::string logged_;    // The last line log() wrote.
    const std::string tag_; // "VST3 #n": this instance in the Convolver log.
};
class Controller final : public Steinberg::Vst::EditController {
  public:
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream*) override;
    // The processor's status_message: the Status parameter's value and text.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
    // The Status parameter's current value reads as the processor's line; other
    // values (a host asking how a value would read) as their status name.
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID,
                                                        Steinberg::Vst::ParamValue,
                                                        Steinberg::Vst::String128) override;
    std::string status_text() const {
        return status_text_;
    }

  private:
    std::string status_text_;
};
} // namespace convolver::vst3
