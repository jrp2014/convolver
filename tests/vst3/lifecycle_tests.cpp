// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-7 for VST3: the processor driven through its SDK interfaces by the
// lifecycle sequences of tests/plugins/lifecycle.hpp, against the reference
// model of the shared adapter contract.
#include "../plugins/audio_guard.hpp"
#include "../plugins/lifecycle.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <array>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace lc = convolver::plugin_test::lifecycle;
namespace guard = convolver::plugin_test::audio_guard;

namespace {
void check(bool condition, const char* what) {
    if (!condition)
        throw std::runtime_error(std::string("VST3 ") + what + " failed");
}

class Vst3 final : public lc::Driver {
  public:
    lc::Format format() const override {
        using lc::Op;
        lc::Format f;
        f.name = "VST3";
        // Contract, loading and failure: VST3 counts its configuration as
        // known once activated with it, while the rate and buses stay the same.
        f.known = lc::Known::activation;
        // Contract, bypass: VST3 saves its bypass parameter with the state.
        f.state_bypass = true;
        f.operations = {Op::state_a,
                        Op::state_b,
                        Op::state_other_rate,
                        Op::state_malformed,
                        Op::state_empty,
                        Op::state_failed,
                        Op::state_b_starved,
                        Op::state_b_shortage,
                        Op::state_malformed_starved,
                        Op::play,
                        Op::bypass,
                        Op::reset,
                        Op::stop,
                        Op::start,
                        Op::reopen,
                        Op::rate,
                        Op::layout,
                        Op::setup};
        return f;
    }
    void create(const lc::Host& host, bool configured) override {
        destroy();
        processor_ = std::make_unique<convolver::vst3::Processor>();
        check(processor_->initialize(&context_) == kResultOk, "initialize");
        if (configured) {
            // As PLUG-2's driver: a host commits to its configuration at activation.
            configure(host);
            check(start(), "first activation");
            stop();
        }
    }
    void configure(const lc::Host& host) override {
        auto in = arrangement(host.inputs), out = arrangement(host.outputs);
        check(processor_->setBusArrangements(&in, 1, &out, 1) == kResultOk, "bus arrangement");
        ProcessSetup setup{kRealtime, kSample32, static_cast<int32>(max_block()),
                           static_cast<SampleRate>(host.sample_rate)};
        check(processor_->setupProcessing(setup) == kResultOk, "setupProcessing");
        processor_->activateBus(kAudio, kInput, 0, true);
        processor_->activateBus(kAudio, kOutput, 0, true);
        inputs_ = host.inputs;
        outputs_ = host.outputs;
    }
    void destroy() override {
        if (!processor_)
            return;
        if (active_)
            stop();
        processor_->terminate();
        processor_.reset();
    }
    bool restore(std::span<const std::byte> bytes) override {
        MemoryStream stream;
        std::array<unsigned char, 4> length{};
        for (std::size_t i = 0; i < 4; ++i)
            length[i] = static_cast<unsigned char>(bytes.size() >> (8 * i));
        int32 written = 0;
        stream.write(length.data(), 4, &written);
        stream.write(const_cast<std::byte*>(bytes.data()), static_cast<int32>(bytes.size()),
                     &written);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        if (starve_) {
            guard::Starve starve(starve_->first, starve_->second);
            return processor_->setState(&stream) == kResultOk;
        }
        return processor_->setState(&stream) == kResultOk;
    }
    std::optional<bool> restore_starved(std::span<const std::byte> bytes, std::size_t allowed,
                                        bool once) override {
        starve_ = std::pair(allowed, once);
        const bool accepted = restore(bytes);
        starve_.reset();
        return accepted;
    }
    bool start() override {
        if (processor_->setActive(true) != kResultOk)
            return false;
        active_ = true;
        guard::Scope audio; // setProcessing may come from the processing thread.
        processor_->setProcessing(true);
        return true;
    }
    void stop() override {
        {
            guard::Scope audio;
            processor_->setProcessing(false);
        }
        processor_->setActive(false);
        active_ = false;
    }
    bool process(std::span<const float* const> in, std::span<float* const> out,
                 std::size_t frames) override {
        return call(in, out, frames, nullptr);
    }
    void set_bypass(bool on) override {
        // A host sends the parameter with the next process call; a call with
        // no audio delivers it at once, as hosts flush parameters.
        ParameterChanges changes;
        int32 index = 0;
        changes.addParameterData(convolver::vst3::bypass_id, index)->addPoint(0, on ? 1 : 0, index);
        check(call({}, {}, 0, &changes), "bypass parameter");
    }
    void reset() override {
        guard::Scope audio;
        processor_->setProcessing(false);
        processor_->setProcessing(true);
    }
    bool reopen(const lc::Host& host, bool active, bool) override {
        MemoryStream saved;
        check(processor_->getState(&saved) == kResultOk, "getState");
        saved.seek(0, IBStream::kIBSeekSet, nullptr);
        // Hosts restore a project's state before activating, often before
        // negotiating; bypass returns with the state.
        create(host, false);
        const bool accepted = processor_->setState(&saved) == kResultOk;
        configure(host);
        if (active)
            check(start(), "activation after reopening");
        return accepted;
    }
    convolver::HostState saved() override {
        MemoryStream stream;
        check(processor_->getState(&stream) == kResultOk, "getState");
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        return convolver::vst3::read_state(&stream);
    }
    std::optional<convolver::HostFailure> failure() override {
        return processor_->failure();
    }
    std::size_t max_block() const override {
        return 512;
    }

  private:
    bool call(std::span<const float* const> in, std::span<float* const> out, std::size_t frames,
              ParameterChanges* changes) {
        AudioBusBuffers input{}, output{};
        input.numChannels = static_cast<int32>(inputs_);
        output.numChannels = static_cast<int32>(outputs_);
        // The SDK's buffer type is not const-correct; the adapter only reads inputs.
        std::array<float*, 8> ins{};
        for (std::size_t c = 0; c < in.size(); ++c)
            ins[c] = const_cast<float*>(in[c]);
        std::array<float*, 8> outs{};
        for (std::size_t c = 0; c < out.size(); ++c)
            outs[c] = out[c];
        input.channelBuffers32 = ins.data();
        output.channelBuffers32 = outs.data();
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(frames);
        data.numInputs = data.numOutputs = 1;
        data.inputs = &input;
        data.outputs = &output;
        data.inputParameterChanges = changes;
        guard::Scope audio;
        return processor_->process(data) == kResultOk;
    }
    static SpeakerArrangement arrangement(std::size_t channels) {
        return (SpeakerArrangement(1) << channels) - 1;
    }
    HostApplication context_;
    std::unique_ptr<convolver::vst3::Processor> processor_;
    std::size_t inputs_ = 2, outputs_ = 2;
    bool active_ = false;
    std::optional<std::pair<std::size_t, bool>> starve_; // For restore_starved.
};
} // namespace

int main(int argc, char** argv) {
    try {
        lc::Options options;
        if (argc > 1)
            options.length = std::stoul(argv[1]);
        // VST3-specific: state restored before the host is configured, at
        // another rate, then the rate corrected (contract, loading and failure).
        for (const char* s : {
                 "state-other-rate setup start listen stop rate start listen",
                 "state-A setup rate start listen stop rate start listen reopen listen",
                 // Out of memory at each allocation of setState: a first
                 // load, a replacement, inactive and before setupProcessing.
                 // ROBUST-3: memory runs out just after a failed first load
                 // is recorded, before audio and while it plays.
                 "start state-malformed-starved listen",
                 "start play state-malformed-starved listen",
                 "start state-B-starved listen",
                 "start state-A play state-B-starved listen",
                 "state-A state-B-starved start listen",
                 "state-B-starved setup start listen",
             })
            options.fixed.push_back(lc::parse(s));
        std::size_t failures = 0;
        Vst3 driver;
        for (const lc::Host host : {lc::Host{48000, 2, 2}, lc::Host{48000, 1, 2}})
            failures += lc::run(driver, lc::make_filters(host), options);
        std::cout << "VST3 lifecycle sequences: " << failures << " failed\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
