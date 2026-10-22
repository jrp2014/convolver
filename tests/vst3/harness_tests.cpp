// SPDX-License-Identifier: GPL-2.0-or-later
// The VST3 adapter driven through its SDK interfaces by the shared PLUG-2 harness.
#include "../plugins/harness.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <iostream>
#include <memory>
#include <stdexcept>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace test = convolver::plugin_test;

namespace {
class Vst3 final : public test::Adapter {
  public:
    std::string format() const override {
        return "VST3";
    }
    void create(const test::Setup& setup) override {
        create_unconfigured();
        configure(setup);
    }
    bool create_unconfigured() override {
        destroy();
        processor_ = std::make_unique<convolver::vst3::Processor>();
        check(processor_->initialize(&context_) == kResultOk, "initialize");
        bypass_ = false;
        pending_bypass_ = false;
        return true;
    }
    void configure(const test::Setup& setup) override {
        // Discrete channels: any arrangement with the right count (PLUG-4 adds layouts).
        auto in = arrangement(setup.inputs), out = arrangement(setup.outputs);
        check(processor_->setBusArrangements(&in, 1, &out, 1) == kResultOk, "bus arrangement");
        ProcessSetup process{kRealtime, kSample32, static_cast<int32>(setup.max_block),
                             static_cast<SampleRate>(setup.sample_rate)};
        check(processor_->setupProcessing(process) == kResultOk, "setupProcessing");
        processor_->activateBus(kAudio, kInput, 0, true);
        processor_->activateBus(kAudio, kOutput, 0, true);
    }
    void destroy() override {
        if (!processor_)
            return;
        if (active_)
            deactivate();
        processor_->terminate();
        processor_.reset();
    }
    bool load(const test::Filter& filter) override {
        convolver::HostState state;
        state.configuration = filter.configuration;
        state.bypass = bypass_;
        MemoryStream stream;
        check(convolver::vst3::write_state(&stream, state), "encode state");
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        return processor_->setState(&stream) == kResultOk;
    }
    bool activate() override {
        if (processor_->setActive(true) != kResultOk)
            return false;
        processor_->setProcessing(true);
        active_ = true;
        return true;
    }
    void deactivate() override {
        processor_->setProcessing(false);
        processor_->setActive(false);
        active_ = false;
    }
    bool process(const test::Block& block) override {
        AudioBusBuffers input{}, output{};
        input.numChannels = static_cast<int32>(block.inputs.size());
        output.numChannels = static_cast<int32>(block.outputs.size());
        // The SDK's buffer type is not const-correct; the adapter only reads inputs.
        std::vector<float*> ins;
        for (auto* p : block.inputs)
            ins.push_back(const_cast<float*>(p));
        input.channelBuffers32 = ins.data();
        output.channelBuffers32 = const_cast<float**>(block.outputs.data());
        input.silenceFlags = block.silent_inputs;
        output.silenceFlags = 0;
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(block.frames);
        data.numInputs = data.numOutputs = 1;
        data.inputs = &input;
        data.outputs = &output;
        ParameterChanges changes;
        if (pending_bypass_) {
            // Automation at sample offset 0, as a host delivers a bypass switch.
            int32 index = 0;
            changes.addParameterData(convolver::vst3::bypass_id, index)
                ->addPoint(0, bypass_ ? 1 : 0, index);
            data.inputParameterChanges = &changes;
            pending_bypass_ = false;
        }
        const auto result = processor_->process(data);
        silent_outputs_ = output.silenceFlags;
        return result == kResultOk;
    }
    void set_bypass(bool on) override {
        bypass_ = on;
        pending_bypass_ = true;
    }
    void reset() override {
        // VST3 hosts restart processing to reset a plug-in's history.
        processor_->setProcessing(false);
        processor_->setProcessing(true);
    }
    std::size_t latency_frames() override {
        return processor_->getLatencySamples();
    }
    std::size_t tail_frames() override {
        return processor_->getTailSamples();
    }
    std::optional<convolver::Configuration> saved() override {
        MemoryStream stream;
        check(processor_->getState(&stream) == kResultOk, "getState");
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        return convolver::vst3::read_state(&stream).configuration;
    }
    std::optional<std::uint64_t> silent_outputs() override {
        return silent_outputs_;
    }

  private:
    static void check(bool condition, const char* what) {
        if (!condition)
            throw std::runtime_error(std::string("VST3 ") + what + " failed");
    }
    static SpeakerArrangement arrangement(std::size_t channels) {
        return (SpeakerArrangement(1) << channels) - 1;
    }
    HostApplication context_;
    std::unique_ptr<convolver::vst3::Processor> processor_;
    test::Setup setup_;
    bool active_ = false, bypass_ = false, pending_bypass_ = false;
    std::uint64_t silent_outputs_ = 0;
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " FIRST_USE_DIRECTORY/fixtures.txt\n";
        return 2;
    }
    // Where today's adapter does not follow the shared adapter contract
    // (docs/plans/2026-10-21-plugin-formats.md#shared-adapter-contract). VST3-7
    // removes these; the harness fails once one of them starts passing.
    test::Options options;
    const std::string direct =
        "VST3-7: a filter set while active but before any audio is "
        "crossfaded in from the empty instance instead of installed directly";
    const std::string inactive = "VST3-7: while inactive, setState checks only the channel "
                                 "limit, so a filter at the wrong rate replaces the saved state "
                                 "and activation then fails, with no output written";
    const std::string activation = "VST3-7: a configuration mismatch found at activation makes "
                                   "setActive fail, so process writes nothing instead of silence";
    for (const char* fixture : {"mono", "stereo", "crossover"}) {
        const std::string at = std::string("[") + fixture + "]";
        options.known_deviations.push_back(
            {"restore-after-activation" + at, "against the oracle: frame 256", direct});
        options.known_deviations.push_back({"failed-replacement-inactive" + at,
                                            "wrong sample rate accepted while inactive", inactive});
        options.known_deviations.push_back(
            {"failed-initial-load" + at, "wrong sample rate accepted", inactive});
        options.known_deviations.push_back({"restore-before-setup-mismatch" + at,
                                            "activation after a mismatched restore failed",
                                            activation});
    }
    options.known_deviations.push_back(
        {"empty-instance[crossover]", "an empty instance with unequal counts",
         "VST3-7: an empty instance with unequal counts passes input c to output c "
         "instead of playing silence"});
    Vst3 adapter;
    return test::run(adapter, argv[1], options);
}
