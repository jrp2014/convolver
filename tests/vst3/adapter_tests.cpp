// SPDX-License-Identifier: GPL-2.0-or-later
#include "../native/cases.hpp"
#include "../plugins/test_log.hpp"
#include "advice.hpp"
#include "host_log.hpp"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "sound_files.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::vst3;
void require(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
// One block of silence through an active processor: a host that has played
// audio with its configuration has settled it, so restored state that does
// not suit it is refused from then on (until then REAPER may still change
// the buses it started the plug-in with).
void play_block(Processor& p, int inputs, int outputs, int frames) {
    std::vector<std::vector<float>> in(inputs, std::vector<float>(frames)),
        out(outputs, std::vector<float>(frames));
    std::vector<float*> ins, outs;
    for (auto& c : in)
        ins.push_back(c.data());
    for (auto& c : out)
        outs.push_back(c.data());
    AudioBusBuffers input{}, output{};
    input.numChannels = inputs;
    output.numChannels = outputs;
    input.channelBuffers32 = ins.data();
    output.channelBuffers32 = outs.data();
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = frames;
    require(p.process(d) == kResultOk, "a block of silence");
}
// The default Latency, the delay these tests expect unless they choose another.
constexpr uint32 latency_samples = convolver::default_host_latency;
constexpr int L = static_cast<int>(latency_samples); // For frame indexes.
// What a host's component handler receives from the controller: restart
// requests (kLatencyChanged), edits and dirty marks.
class Handler final : public IComponentHandler, public IComponentHandler2 {
  public:
    tresult PLUGIN_API beginEdit(ParamID id) override {
        edits.push_back({'b', id, 0});
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        edits.push_back({'p', id, value});
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override {
        edits.push_back({'e', id, 0});
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        restarts.push_back(flags);
        if (on_restart)
            on_restart(flags);
        return kResultOk;
    }
    tresult PLUGIN_API setDirty(TBool state) override {
        dirty += state ? 1 : 0;
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString) override {
        return kNotImplemented;
    }
    tresult PLUGIN_API startGroupEdit() override {
        return kNotImplemented;
    }
    tresult PLUGIN_API finishGroupEdit() override {
        return kNotImplemented;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler::iid, IComponentHandler)
        QUERY_INTERFACE(iid, obj, IComponentHandler2::iid, IComponentHandler2)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return 1;
    }
    uint32 PLUGIN_API release() override {
        return 1;
    }
    struct Edit {
        char kind;
        ParamID id;
        ParamValue value;
    };
    std::vector<Edit> edits;
    // How many restart requests asked the host to read the latency again;
    // the Status text asks for kParamValuesChanged on every change.
    std::size_t latency_restarts() const {
        return static_cast<std::size_t>(std::count_if(
            restarts.begin(), restarts.end(), [](int32 f) { return (f & kLatencyChanged) != 0; }));
    }
    std::vector<int32> restarts;
    std::function<void(int32)> on_restart; // What a host does when asked to restart.
    int dirty = 0;
};
void edge_cases() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "edge initialize");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "stereo arrangement");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    convolver::HostState saved;
    saved.configuration =
        convolver::Configuration{48000, 2, 2, 3, {}, {}, {{{0, 1}, {{0, 1}}, {{1, 1}}}}};
    MemoryStream state;
    require(write_state(&state, saved), "encode routed state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore routed state");
    ProcessSetup setup{kOffline, kSample32, 512, 48000};
    require(p.setupProcessing(setup) == kResultOk, "offline setup");
    // The tail is measured at the output, so it includes the latency.
    require(p.getTailSamples() == 1 + latency_samples, "tail unavailable before activation");
    require(p.setActive(true) == kResultOk, "offline activate");
    require(p.getTailSamples() == 1 + latency_samples, "reported FIR tail");
    p.setProcessing(true);
    std::array<float, 512> left{}, right{}, out_left{}, out_right{};
    left[0] = 1;
    float* ins[] = {left.data(), right.data()};
    float* outs[] = {out_left.data(), out_right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 100;
    require(p.process(d) == kResultOk, "routed first block");
    // Flagged silent, but the left buffer still holds its impulse: input flags
    // are not trusted, so it is read and filtered too (VST3-8), and the first
    // block's tail still arrives.
    input.silenceFlags = 3;
    d.numSamples = 512;
    require(p.process(d) == kResultOk, "silent input with wet tail");
    // The filter echoes each impulse one frame late, plus the latency: the
    // second impulse (frame 0 of this block) at latency + 1, and the first
    // (100 frames earlier) at latency - 99, if that falls in this block.
    const auto echo = static_cast<int>(latency_samples) + 1, earlier = echo - 100;
    for (int i = 0; i < 512; ++i)
        require(std::abs(out_right[i] - (i == earlier || i == echo ? 1.f : 0.f)) < 1e-5 &&
                    out_left[i] == 0,
                "silence flags cut the tail or dropped flagged audio");
    // Only the left output, all zero, is flagged (VST3-10); the right holds the tail.
    require(output.silenceFlags == 1, "tail marked silent, or silent output not marked");
    auto mismatch = saved;
    mismatch.configuration->sample_rate = 44100;
    MemoryStream wrong;
    write_state(&wrong, mismatch);
    wrong.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&wrong) != kResultOk, "active sample rate mismatch accepted");
    MemoryStream damaged;
    unsigned zero = 0;
    damaged.write(&zero, 4, nullptr);
    damaged.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&damaged) != kResultOk, "malformed state accepted");
    MemoryStream restored;
    require(p.getState(&restored) == kResultOk, "state snapshot");
    restored.seek(0, IBStream::kIBSeekSet, nullptr);
    require(read_state(&restored).configuration->sample_rate == 48000,
            "failed restore mutated current state");
    p.setProcessing(false);
    p.setProcessing(true);
    input.silenceFlags = 0;
    left.fill(1);
    right.fill(1);
    d.numSamples = 512;
    p.process(d); // Fill the latency-matched dry delay.
    ParameterChanges changes;
    int32 index = 0;
    auto* queue = changes.addParameterData(bypass_id, index);
    queue->addPoint(16, 1, index);
    d.inputParameterChanges = &changes;
    require(p.process(d) == kResultOk, "sample-offset bypass automation");
    // Wet left channel is zero; dry is one. Transition begins at offset 16.
    require(out_left[15] == 0 && std::abs(out_left[16] - 1.f / 64) < 1e-6 && out_left[79] == 1,
            "bypass ramp offset/length");
    d.inputParameterChanges = nullptr;
    ProcessData flush{};
    flush.inputParameterChanges = &changes;
    require(p.process(flush) == kResultOk, "parameter flush with no buses");
    p.setProcessing(false);
    p.setActive(false);
    saved.configuration->paths[0].impulse.resize(1001, 0);
    saved.configuration->input_delays = {3, 0};
    saved.configuration->output_delays = {0, 7};
    MemoryStream longer;
    write_state(&longer, saved);
    longer.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&longer) == kResultOk && p.getTailSamples() == 1010 + latency_samples,
            "inactive replacement tail including routing delays");
    setup.sampleRate = 44100;
    require(p.setupProcessing(setup) == kResultOk, "rate change setup");
    // Shared adapter contract: a kept filter that does not suit the host plays
    // silence (not the unfiltered input), and returns once the rate is corrected.
    require(p.setActive(true) == kResultOk, "a mismatched IR rate must activate, silent");
    // Bypass still plays the input, delayed by the latency.
    require(p.getTailSamples() == latency_samples, "silent instance tail is not the latency");
    p.setProcessing(true);
    ParameterChanges wet;
    index = 0;
    wet.addParameterData(bypass_id, index)->addPoint(0, 0, index);
    d.inputParameterChanges = &wet;
    left.fill(1);
    right.fill(1);
    for (int k = 0; k < 3; ++k) {
        require(p.process(d) == kResultOk, "silent process");
        d.inputParameterChanges = nullptr;
    }
    for (int i = 0; i < 512; ++i)
        require(out_left[i] == 0 && out_right[i] == 0, "mismatched rate not silent");
    MemoryStream kept;
    require(p.getState(&kept) == kResultOk, "state snapshot at the wrong rate");
    kept.seek(0, IBStream::kIBSeekSet, nullptr);
    require(read_state(&kept).configuration->sample_rate == 48000,
            "mismatched filter not kept as the saved state");
    p.setProcessing(false);
    p.setActive(false);
    setup.sampleRate = 48000;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk &&
                p.getTailSamples() == 1010 + latency_samples,
            "corrected rate does not bring the filter back");
    p.setActive(false);
    p.terminate();
}
void multichannel() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "multichannel initialize");
    SpeakerArrangement nine = (SpeakerArrangement(1) << 9) - 1, stereo = SpeakerArr::kStereo,
                       surround = SpeakerArr::k51, octo = SpeakerArr::k71Music;
    require(p.setBusArrangements(&nine, 1, &stereo, 1) != kResultOk &&
                p.setBusArrangements(&stereo, 1, &nine, 1) != kResultOk,
            "nine channels accepted");
    require(p.setBusArrangements(&octo, 1, &octo, 1) == kResultOk, "8x8 arrangement");
    require(p.setBusArrangements(&stereo, 1, &surround, 1) == kResultOk, "2x6 arrangement");
    SpeakerArrangement current = 0;
    require(p.getBusArrangement(kOutput, 0, current) == kResultOk && current == surround,
            "output arrangement not stored");
    // Left feeds front left and the LFE at half gain; right feeds front right and,
    // one sample later, both surrounds with opposite signs.
    convolver::HostState saved;
    saved.configuration = convolver::Configuration{48000, 2, 6, 0, {}, {}, {}};
    saved.configuration->paths = {{{1}, {{0, 1}}, {{0, 1}, {3, .5f}}},
                                  {{1}, {{1, 1}}, {{1, 1}}},
                                  {{0, 1}, {{1, 1}}, {{4, 1}, {5, -1}}}};
    MemoryStream state;
    require(write_state(&state, saved), "encode 2x6 state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore 2x6 state");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 300, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "2x6 activate");
    p.setProcessing(true);
    std::array<std::array<float, 300>, 2> ins{};
    std::array<std::array<float, 300>, 6> outs{};
    ins[0][0] = 1;
    ins[1][0] = 2;
    float* in_ptr[] = {ins[0].data(), ins[1].data()};
    std::array<float*, 6> out_ptr{};
    for (int c = 0; c < 6; ++c)
        out_ptr[c] = outs[c].data();
    AudioBusBuffers input{}, output{};
    input.numChannels = 2;
    output.numChannels = 6;
    input.channelBuffers32 = in_ptr;
    output.channelBuffers32 = out_ptr.data();
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 300;
    require(p.process(d) == kResultOk, "2x6 process");
    const std::array<std::array<float, 2>, 6> expected{
        {{1, 0}, {2, 0}, {0, 0}, {.5f, 0}, {0, 2}, {0, -2}}};
    for (int c = 0; c < 6; ++c)
        for (int i = 0; i < 300; ++i) {
            float want = i == L ? expected[c][0] : i == L + 1 ? expected[c][1] : 0.f;
            require(std::abs(outs[c][i] - want) < 1e-5, "2x6 routing");
        }
    output.numChannels = 2;
    require(p.process(d) != kResultOk, "output bus with wrong channel count accepted");
    p.setProcessing(false);
    p.setActive(false);
    require(p.setBusArrangements(&octo, 1, &octo, 1) == kResultOk, "8x8 after 2x6");
    // The 2x6 preset does not suit 8x8 buses: silence, and the preset is kept.
    require(p.setActive(true) == kResultOk && p.getTailSamples() == latency_samples,
            "2x6 preset on 8x8 buses must activate, silent");
    MemoryStream kept;
    require(p.getState(&kept) == kResultOk, "state snapshot on 8x8 buses");
    kept.seek(0, IBStream::kIBSeekSet, nullptr);
    require(read_state(&kept).configuration->output_channels == 6, "2x6 preset not kept");
    p.setActive(false);
    // PLUG-23: REAPER gives the effect on a six-channel track 6 inputs and 6
    // outputs. The 2x6 preset plays, reading inputs 1-2 and ignoring 3-6.
    require(p.setBusArrangements(&surround, 1, &surround, 1) == kResultOk &&
                p.setActive(true) == kResultOk,
            "2x6 preset on 6x6 buses");
    p.setProcessing(true);
    std::array<std::array<float, 300>, 6> six{};
    std::array<float*, 6> six_ptr{};
    for (int c = 0; c < 6; ++c) {
        six[c][0] = c == 0 ? 1.f : c == 1 ? 2.f : 7.f;
        six_ptr[c] = six[c].data();
        outs[c].fill(0);
    }
    input.numChannels = 6;
    input.channelBuffers32 = six_ptr.data();
    output.numChannels = 6;
    const auto wide_routing = [&](const std::string& what) {
        for (int c = 0; c < 6; ++c)
            outs[c].fill(0);
        require(p.process(d) == kResultOk, (what + ": 6x6 process").c_str());
        for (int c = 0; c < 6; ++c)
            for (int i = 0; i < 300; ++i) {
                float want = i == L ? expected[c][0] : i == L + 1 ? expected[c][1] : 0.f;
                require(std::abs(outs[c][i] - want) < 1e-5, (what + ": routing").c_str());
            }
    };
    wide_routing("2x6 preset on 6x6 buses");
    // Saved and reopened on the same buses, the filter keeps its own counts.
    MemoryStream surround_state;
    require(p.getState(&surround_state) == kResultOk, "state on 6x6 buses");
    surround_state.seek(0, IBStream::kIBSeekSet, nullptr);
    const auto wide_saved = read_state(&surround_state).configuration;
    require(wide_saved && wide_saved->input_channels == 2 && wide_saved->output_channels == 6,
            "a 2x6 preset on 6x6 buses saved with other channel counts");
    p.setProcessing(false);
    p.setActive(false);
    surround_state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&surround_state) == kResultOk && p.setActive(true) == kResultOk,
            "2x6 state reopened on 6x6 buses");
    p.setProcessing(true);
    wide_routing("2x6 state reopened on 6x6 buses");
    // The filter's counts differ, so there is no dry signal although the
    // buses' counts are equal: Mix 0% still plays the filtered sound, and
    // bypass is silent.
    int32 at = 0;
    ParameterChanges dry_mix, bypassed, restored;
    dry_mix.addParameterData(mix_id, at)->addPoint(0, 0, at);
    d.inputParameterChanges = &dry_mix;
    wide_routing("Mix 0% with a 2x6 preset on 6x6 buses");
    bypassed.addParameterData(bypass_id, at = 0)->addPoint(0, 1, at);
    d.inputParameterChanges = &bypassed;
    for (int c = 0; c < 6; ++c)
        outs[c].fill(1);
    require(p.process(d) == kResultOk, "bypassed 6x6 process");
    for (int c = 0; c < 6; ++c)
        for (int i = 64; i < 300; ++i)
            require(outs[c][i] == 0, "bypass with a 2x6 preset on 6x6 buses is not silent");
    restored.addParameterData(mix_id, at = 0)->addPoint(0, 1, at);
    restored.addParameterData(bypass_id, at = 0)->addPoint(0, 0, at);
    d.inputParameterChanges = &restored;
    require(p.process(d) == kResultOk, "Mix 100% and bypass off again");
    // jrp, 7 November 2026: kept at the wrong rate, the crossover plays
    // nothing, and with no dry signal bypass stays silent too.
    p.setProcessing(false);
    p.setActive(false);
    auto slower = setup;
    slower.sampleRate = 44100;
    require(p.setupProcessing(slower) == kResultOk && p.setActive(true) == kResultOk,
            "2x6 preset at 44.1 kHz on 6x6 buses must activate, silent");
    p.setProcessing(true);
    for (ParameterChanges* changes : {&bypassed, &dry_mix}) {
        d.inputParameterChanges = changes;
        for (int c = 0; c < 6; ++c)
            outs[c].fill(1);
        require(p.process(d) == kResultOk, "kept 2x6 preset process");
        for (int c = 0; c < 6; ++c)
            for (int i = 0; i < 300; ++i)
                require(outs[c][i] == 0,
                        "a kept 2x6 preset that does not suit the host passes the input");
    }
    d.inputParameterChanges = &restored;
    require(p.process(d) == kResultOk, "Mix 100% and bypass off once more");
    d.inputParameterChanges = nullptr;
    p.setProcessing(false);
    p.setActive(false);
    require(p.setBusArrangements(&octo, 1, &octo, 1) == kResultOk, "8x8 after 6x6");
    convolver::HostState identity;
    MemoryStream empty;
    require(write_state(&empty, identity), "encode identity state");
    empty.seek(0, IBStream::kIBSeekSet, nullptr);
    setup.symbolicSampleSize = kSample64;
    require(p.setState(&empty) == kResultOk && p.setupProcessing(setup) == kResultOk &&
                p.setActive(true) == kResultOk,
            "8x8 identity activate");
    p.setProcessing(true);
    std::array<std::array<double, 300>, 8> wide{};
    std::array<double*, 8> wide_ptr{};
    for (int c = 0; c < 8; ++c) {
        wide[c][0] = c + 1;
        wide_ptr[c] = wide[c].data();
    }
    input.numChannels = output.numChannels = 8;
    input.channelBuffers64 = output.channelBuffers64 = wide_ptr.data();
    d.symbolicSampleSize = kSample64;
    require(p.process(d) == kResultOk, "8x8 in-place double process");
    for (int c = 0; c < 8; ++c)
        for (int i = 0; i < 300; ++i)
            require(std::abs(wide[c][i] - (i == L ? c + 1. : 0.)) < 1e-5, "8x8 identity");
    p.setProcessing(false);
    p.setActive(false);
    convolver::HostState too_wide;
    too_wide.configuration = convolver::Configuration{48000, 9, 9, 0, {}, {}, {}};
    for (std::size_t c = 0; c < 9; ++c)
        too_wide.configuration->paths.push_back({{1}, {{c, 1}}, {{c, 1}}});
    MemoryStream wide_state;
    require(write_state(&wide_state, too_wide), "encode 9x9 state");
    wide_state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&wide_state) != kResultOk, "9x9 preset accepted");
    p.terminate();
}
// State a host marks as a project being restored (VST 3.6 IStreamAttributes,
// PresetAttributes::kStateType "Project").
class ProjectStream final : public MemoryStream, public IStreamAttributes {
  public:
    ProjectStream() : attributes_(HostAttributeList::make()) {
        String128 type{};
        const std::string_view project = StateType::kProject;
        std::copy(project.begin(), project.end(), type);
        attributes_->setString(PresetAttributes::kStateType, type);
    }
    tresult PLUGIN_API getFileName(String128) override {
        return kNotImplemented;
    }
    IAttributeList* PLUGIN_API getAttributes() override {
        return attributes_;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, IStreamAttributes::iid, IStreamAttributes)
        return MemoryStream::queryInterface(iid, obj);
    }
    uint32 PLUGIN_API addRef() override {
        return MemoryStream::addRef();
    }
    uint32 PLUGIN_API release() override {
        return MemoryStream::release();
    }

  private:
    IPtr<IAttributeList> attributes_;
};
// jrp's REAPER crossover retest, 7 November 2026: reopening a project, and
// FX Offline then Online, REAPER starts the plug-in on stereo buses, restores
// the saved state, then gives it the track's six inputs and outputs. The
// saved 2-in, 6-out crossover must survive the stereo start and play on six.
// Each order: state while active or after deactivation; a host that has
// played audio on stereo buses but says it is restoring a project; and the
// state saved while the crossover was kept silent on stereo buses (jrp's
// review of eeaa843: saving then must not lose the filter).
void restored_while_host_settles() {
    convolver::HostState saved;
    saved.configuration = convolver::Configuration{48000, 2, 6, 0, {}, {}, {}};
    saved.configuration->paths = {{{1}, {{0, 1}}, {{0, 1}, {3, .5f}}},
                                  {{1}, {{1, 1}}, {{1, 1}}},
                                  {{0, 1}, {{1, 1}}, {{4, 1}, {5, -1}}}};
    const std::array<std::array<float, 2>, 6> expected{
        {{1, 0}, {2, 0}, {0, 0}, {.5f, 0}, {0, 2}, {0, -2}}};
    SpeakerArrangement stereo = SpeakerArr::kStereo, surround = SpeakerArr::k51;
    struct Order {
        bool while_active, project, resaved;
    };
    std::vector<char> silent_save; // From the first order, while kept silent.
    for (const auto [while_active, project, resaved] :
         {Order{true, false, false}, Order{true, true, false}, Order{false, false, false},
          Order{false, true, false}, Order{true, false, true}}) {
        const std::string order = std::string(while_active ? "active" : "inactive") +
                                  (project ? ", host says project" : "") +
                                  (resaved ? ", saved while kept silent" : "") + ": ";
        const auto need = [&](bool v, const char* what) { require(v, (order + what).c_str()); };
        HostApplication context;
        Processor p;
        need(p.initialize(&context) == kResultOk, "initialize");
        need(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "stereo buses");
        p.activateBus(kAudio, kInput, 0, true);
        p.activateBus(kAudio, kOutput, 0, true);
        ProcessSetup setup{kRealtime, kSample32, 300, 48000};
        need(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
             "stereo start");
        p.setProcessing(true);
        if (project) // Played already: only the host's word marks a restore.
            play_block(p, 2, 2, 300);
        if (!while_active) {
            p.setProcessing(false);
            p.setActive(false);
        }
        std::unique_ptr<MemoryStream> state =
            project ? std::make_unique<ProjectStream>() : std::make_unique<MemoryStream>();
        if (resaved)
            need(state->write(silent_save.data(), static_cast<int32>(silent_save.size()),
                              nullptr) == kResultOk,
                 "copy the state saved while kept silent");
        else
            need(write_state(state.get(), saved), "encode the crossover");
        state->seek(0, IBStream::kIBSeekSet, nullptr);
        need(p.setState(state.get()) == kResultOk,
             "the saved crossover was refused on the host's stereo start");
        if (while_active) {
            // Kept but silent on stereo buses, with the reason, and no
            // dry signal: the crossover's counts differ.
            const auto f = p.failure();
            need(f && f->silent && f->kind == convolver::HostFailureKind::channels,
                 "the kept crossover gives no reason for its silence");
            // Past any transition from what played before (the empty
            // instance's unchanged input, once audio has played).
            for (int k = 0; k < 4; ++k) {
                std::array<std::array<float, 300>, 2> ins{}, outs{};
                ins[0].fill(1);
                ins[1].fill(1);
                float* in_ptr[] = {ins[0].data(), ins[1].data()};
                float* out_ptr[] = {outs[0].data(), outs[1].data()};
                AudioBusBuffers input{}, output{};
                input.numChannels = output.numChannels = 2;
                input.channelBuffers32 = in_ptr;
                output.channelBuffers32 = out_ptr;
                ProcessData d{};
                d.symbolicSampleSize = kSample32;
                d.numInputs = d.numOutputs = 1;
                d.inputs = &input;
                d.outputs = &output;
                d.numSamples = 300;
                need(p.process(d) == kResultOk, "stereo process");
                if (project && k < 3)
                    continue;
                for (const auto& channel : outs)
                    for (const float v : channel)
                        need(v == 0, "the kept crossover is not silent on stereo buses");
            }
            // What the host saves now is the crossover, not the failure.
            MemoryStream now;
            need(p.getState(&now) == kResultOk, "save while kept silent");
            const auto bytes = static_cast<std::size_t>(now.getSize());
            if (!resaved && !project)
                silent_save.assign(now.getData(), now.getData() + bytes);
            now.seek(0, IBStream::kIBSeekSet, nullptr);
            const auto held = read_state(&now);
            need(!held.failed && held.configuration && held.configuration->input_channels == 2 &&
                     held.configuration->output_channels == 6,
                 "saved while kept silent, the state lost the crossover");
            p.setProcessing(false);
            p.setActive(false);
        }
        need(p.setBusArrangements(&surround, 1, &surround, 1) == kResultOk &&
                 p.setActive(true) == kResultOk,
             "six-channel start");
        p.setProcessing(true);
        need(!p.failure(), "the restored crossover does not play on six channels");
        std::array<std::array<float, 300>, 6> six{}, outs{};
        std::array<float*, 6> in_ptr{}, out_ptr{};
        for (int c = 0; c < 6; ++c) {
            six[c][0] = c == 0 ? 1.f : c == 1 ? 2.f : 7.f;
            in_ptr[c] = six[c].data();
            out_ptr[c] = outs[c].data();
        }
        AudioBusBuffers input{}, output{};
        input.numChannels = output.numChannels = 6;
        input.channelBuffers32 = in_ptr.data();
        output.channelBuffers32 = out_ptr.data();
        ProcessData d{};
        d.symbolicSampleSize = kSample32;
        d.numInputs = d.numOutputs = 1;
        d.inputs = &input;
        d.outputs = &output;
        d.numSamples = 300;
        need(p.process(d) == kResultOk, "six-channel process");
        for (int c = 0; c < 6; ++c)
            for (int i = 0; i < 300; ++i) {
                const float want = i == L ? expected[c][0] : i == L + 1 ? expected[c][1] : 0.f;
                need(std::abs(outs[c][i] - want) < 1e-5, "routing on six channels");
            }
        // Once the host has played with its buses, a preset that does not
        // suit them is refused and the crossover keeps playing.
        convolver::HostState stereo_filter;
        stereo_filter.configuration =
            convolver::Configuration{48000, 2, 2, 0, {}, {}, {{{.5f}, {{0, 1}}, {{1, 1}}}}};
        MemoryStream preset;
        need(write_state(&preset, stereo_filter), "encode a stereo preset");
        preset.seek(0, IBStream::kIBSeekSet, nullptr);
        need(p.setState(&preset) == kResultFalse,
             "a stereo preset was accepted on six settled outputs");
        const auto f = p.failure();
        need(f && !f->silent, "the refused preset silenced the crossover");
        // Restarted on the same buses, the host has still played with
        // them: the preset is refused before any new audio too.
        p.setProcessing(false);
        p.setActive(false);
        need(p.setActive(true) == kResultOk, "restart on six channels");
        p.setProcessing(true);
        preset.seek(0, IBStream::kIBSeekSet, nullptr);
        need(p.setState(&preset) == kResultFalse,
             "a stereo preset was accepted after a restart on the same buses");
        MemoryStream kept;
        need(p.getState(&kept) == kResultOk, "save");
        kept.seek(0, IBStream::kIBSeekSet, nullptr);
        const auto restored = read_state(&kept).configuration;
        need(restored && restored->input_channels == 2 && restored->output_channels == 6,
             "the crossover is not what is saved");
        p.setProcessing(false);
        p.setActive(false);
        p.terminate();
    }
    // Audio played on six channels does not settle the stereo buses the host
    // changes to next: restored there before audio plays, the crossover is
    // kept, and plays when the host returns to six (the independent review
    // of c3dbf0d).
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "changed buses: initialize");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 300, 48000};
    require(p.setBusArrangements(&surround, 1, &surround, 1) == kResultOk &&
                p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "changed buses: six-channel start");
    p.setProcessing(true);
    play_block(p, 6, 6, 300);
    p.setProcessing(false);
    p.setActive(false);
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk &&
                p.setActive(true) == kResultOk,
            "changed buses: stereo start");
    MemoryStream state;
    require(write_state(&state, saved), "changed buses: encode the crossover");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk,
            "changed buses: the crossover was refused before audio played on stereo buses");
    const auto f = p.failure();
    require(f && f->silent && f->kind == convolver::HostFailureKind::channels,
            "changed buses: the kept crossover gives no reason for its silence");
    p.setActive(false);
    require(p.setBusArrangements(&surround, 1, &surround, 1) == kResultOk &&
                p.setActive(true) == kResultOk && !p.failure(),
            "changed buses: the crossover does not play on six channels again");
    p.setActive(false);
    p.terminate();
}
// PLUG-4: version-1 state records counts and an advisory output mask, no layout.
// It is "unspecified" (discrete): it must load on every arrangement with matching
// counts, whatever its mask says, and route by bus index. Stricter layout checks
// (VST3-1) must keep this test passing.
void version1_presets() {
    const std::array<SpeakerArrangement, 4> arrangements{
        SpeakerArr::k51, SpeakerArr::k60Music,
        kSpeakerL | kSpeakerR | kSpeakerC | kSpeakerLfe | kSpeakerSl | kSpeakerSr,
        kSpeakerTfl | kSpeakerTfc | kSpeakerTfr | kSpeakerTrl | kSpeakerTrc | kSpeakerTrr};
    for (const std::uint32_t mask : {0U, 0x3FU, 0x60FU})
        for (auto arrangement : arrangements) {
            HostApplication context;
            Processor p;
            require(p.initialize(&context) == kResultOk, "version-1 initialize");
            SpeakerArrangement stereo = SpeakerArr::kStereo;
            require(p.setBusArrangements(&stereo, 1, &arrangement, 1) == kResultOk,
                    "six-channel arrangement refused");
            // Output c receives input c % 2 with gain c + 1, so order is visible.
            convolver::HostState saved;
            saved.configuration = convolver::Configuration{48000, 2, 6, mask, {}, {}, {}};
            for (std::size_t c = 0; c < 6; ++c)
                saved.configuration->paths.push_back(
                    {{1}, {{c % 2, 1}}, {{c, static_cast<float>(c + 1)}}});
            MemoryStream state;
            require(write_state(&state, saved), "encode version-1 state");
            require(state.getData()[8] == 1 && state.getData()[9] == 0,
                    "state without a layout must still be written as version 1");
            state.seek(0, IBStream::kIBSeekSet, nullptr);
            require(p.setState(&state) == kResultOk, "version-1 preset rejected by layout");
            p.activateBus(kAudio, kInput, 0, true);
            p.activateBus(kAudio, kOutput, 0, true);
            ProcessSetup setup{kRealtime, kSample32, 300, 48000};
            require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
                    "version-1 preset failed to activate on a matching count");
            // The output mask is advice only (VST3-1): 5.1 with back or side
            // speakers suits k51 and the side-speaker 5.1; others get a notice.
            const bool five_one = arrangement == SpeakerArr::k51 || arrangement == arrangements[2];
            require(p.notice().empty() == (mask == 0 || five_one),
                    "version-1 output mask compared wrongly with the arrangement");
            p.setProcessing(true);
            std::array<std::array<float, 300>, 2> ins{};
            std::array<std::array<float, 300>, 6> outs{};
            ins[0][0] = 1;
            ins[1][1] = 1;
            float* in_ptr[] = {ins[0].data(), ins[1].data()};
            std::array<float*, 6> out_ptr{};
            for (int c = 0; c < 6; ++c)
                out_ptr[c] = outs[c].data();
            AudioBusBuffers input{}, output{};
            input.numChannels = 2;
            output.numChannels = 6;
            input.channelBuffers32 = in_ptr;
            output.channelBuffers32 = out_ptr.data();
            ProcessData d{};
            d.symbolicSampleSize = kSample32;
            d.numInputs = d.numOutputs = 1;
            d.inputs = &input;
            d.outputs = &output;
            d.numSamples = 300;
            require(p.process(d) == kResultOk, "version-1 process");
            for (int c = 0; c < 6; ++c)
                for (int i = 0; i < 300; ++i) {
                    const float want = i == L + c % 2 ? static_cast<float>(c + 1) : 0.f;
                    require(std::abs(outs[c][i] - want) < 1e-5,
                            "version-1 preset not routed by bus index");
                }
            p.setProcessing(false);
            p.setActive(false);
            p.terminate();
        }
}
// VST3-1: arrangements map to WAV speaker bits directly, and a filter whose
// output mask names other speakers than the bus plays anyway, with a notice.
void speaker_arrangements() {
    require(wav_speakers(SpeakerArr::kStereo) == 0x3 && wav_speakers(SpeakerArr::k51) == 0x3F &&
                wav_speakers(SpeakerArr::k71Music) == 0x63F &&
                wav_speakers(SpeakerArr::k71Cine) == 0xFF,
            "arrangement not mapped to WAV speaker bits");
    require(wav_speakers(SpeakerArr::kMono) == 0 &&
                wav_speakers(SpeakerArr::k71CineFullRear) == 0 &&
                wav_speakers(SpeakerArr::kStereoWide) == 0 &&
                wav_speakers(SpeakerArr::kAmbi1stOrderACN) == 0 &&
                wav_speakers(SpeakerArr::k71_4) == 0x2D63F,
            "arrangement mapped wrongly to WAV speaker bits 0-17");
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "speaker initialize");
    SpeakerArrangement cine = SpeakerArr::k71Cine, music = SpeakerArr::k71Music;
    require(p.setBusArrangements(&cine, 1, &cine, 1) == kResultOk, "7.1 Cine buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 64, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "speaker activate");
    convolver::HostState saved;
    saved.configuration = convolver::Configuration{48000, 8, 8, 0x63F, {}, {}, {}};
    for (std::size_t c = 0; c < 8; ++c)
        saved.configuration->paths.push_back({{1}, {{c, 1}}, {{c, 1}}});
    MemoryStream state;
    require(write_state(&state, saved), "encode 7.1 state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "7.1 filter refused on 7.1 Cine");
    const auto notice = convolver::detail::advice_text(p.notice());
    require(notice.find("output 7 is filtered for the side left (SL) speaker but plays on the "
                        "front left-centre (FLC)") != std::string::npos &&
                notice.find("output 8 is filtered for the side right (SR)") != std::string::npos,
            "7.1 filter on 7.1 Cine gave no notice");
    require(p.setActive(false) == kResultOk &&
                p.setBusArrangements(&music, 1, &music, 1) == kResultOk &&
                p.setActive(true) == kResultOk && p.notice().empty(),
            "7.1 Music buses kept the notice");
    play_block(p, 8, 8, 64);
    // Changing only the arrangement keeps the host's configuration known: a
    // preset at the wrong rate is still refused and the working filter kept.
    require(p.setActive(false) == kResultOk &&
                p.setBusArrangements(&cine, 1, &cine, 1) == kResultOk,
            "back to 7.1 Cine buses");
    saved.configuration->sample_rate = 44100;
    MemoryStream wrong_rate;
    require(write_state(&wrong_rate, saved), "encode 44.1 kHz state");
    wrong_rate.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&wrong_rate) == kResultFalse,
            "wrong-rate preset accepted after an arrangement change");
    MemoryStream kept;
    require(p.getState(&kept) == kResultOk, "save after refused preset");
    kept.seek(0, IBStream::kIBSeekSet, nullptr);
    require(read_state(&kept).configuration->sample_rate == 48000,
            "refused preset replaced the working filter");
    p.terminate();
}
// JUCE-based hosts (and pluginval) call setupProcessing with placeholder values
// before restoring a project's state at its real rate. Until the plug-in has
// been activated, setupProcessing is not binding: the state must be kept.
void placeholder_setup() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "placeholder initialize");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "placeholder buses");
    ProcessSetup setup{kRealtime, kSample32, 512, 44100};
    require(p.setupProcessing(setup) == kResultOk, "placeholder setup");
    convolver::HostState saved;
    saved.configuration =
        convolver::Configuration{48000, 2, 2, 0, {}, {}, {{{.5f}, {{0, 1}}, {{1, 1}}}}};
    MemoryStream state;
    require(write_state(&state, saved), "encode 48 kHz state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "state after a placeholder setup rejected");
    setup.sampleRate = 48000;
    require(p.setupProcessing(setup) == kResultOk, "real setup");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    require(p.setActive(true) == kResultOk, "activate at the real rate");
    p.setProcessing(true);
    std::array<float, 512> left{}, right{}, out_left{}, out_right{};
    left[0] = 1;
    float* ins[] = {left.data(), right.data()};
    float* outs[] = {out_left.data(), out_right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 512;
    require(p.process(d) == kResultOk, "placeholder process");
    for (int i = 0; i < 512; ++i)
        require(out_left[i] == 0 && std::abs(out_right[i] - (i == L ? .5f : 0.f)) < 1e-6,
                "state restored after a placeholder setup does not play");
    // Now the rate is committed: a mismatch while inactive is rejected at once.
    p.setProcessing(false);
    p.setActive(false);
    saved.configuration->sample_rate = 44100;
    MemoryStream wrong;
    require(write_state(&wrong, saved), "encode 44.1 kHz state");
    wrong.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&wrong) != kResultOk, "mismatch after activation accepted while inactive");
    p.terminate();
}
// A host that stops and restarts processing during a crossfade resets the
// plug-in: a preset loaded in between plays from the first resumed sample.
void restart_mid_transition() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "restart initialize");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "restart buses");
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setupProcessing(setup) == kResultOk, "restart setup");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    const auto load = [&](float gain) {
        convolver::HostState saved;
        saved.configuration =
            convolver::Configuration{48000, 2, 2, 0, {}, {}, {{{gain}, {{0, 1}}, {{0, 1}}}}};
        MemoryStream state;
        require(write_state(&state, saved), "encode restart state");
        state.seek(0, IBStream::kIBSeekSet, nullptr);
        return p.setState(&state);
    };
    require(load(1) == kResultOk, "restart first state");
    require(p.setActive(true) == kResultOk, "restart activate");
    p.setProcessing(true);
    std::array<float, 512> left{}, right{}, out_left{}, out_right{};
    left.fill(1);
    float* ins[] = {left.data(), right.data()};
    float* outs[] = {out_left.data(), out_right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    for (const int into : {64, 400}) { // Stop during the warm-up, then the crossfade.
        d.numSamples = 512;
        require(p.process(d) == kResultOk, "restart process");
        require(load(2) == kResultOk, "restart replacement");
        d.numSamples = into;
        require(p.process(d) == kResultOk, "restart transition");
        p.setProcessing(false);
        require(load(3) == kResultOk, "restart reload");
        p.setProcessing(true);
        d.numSamples = 512;
        require(p.process(d) == kResultOk, "restart resumed");
        for (int i = 0; i < 512; ++i)
            require(std::abs(out_left[i] - (i < L ? 0.f : 3.f)) < 1e-5 && out_right[i] == 0,
                    "preset loaded while stopped mid-transition does not play at once");
    }
    p.setProcessing(false);
    p.setActive(false);
    p.terminate();
}
// A failed first load plays silence, never the unfiltered input: a wrong-rate
// preset sent to an active empty instance, and malformed state before activation.
void failed_first_loads() {
    for (const bool while_active : {true, false}) {
        HostApplication context;
        Processor p;
        require(p.initialize(&context) == kResultOk, "failed-load initialize");
        SpeakerArrangement stereo = SpeakerArr::kStereo;
        require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "failed-load buses");
        p.activateBus(kAudio, kInput, 0, true);
        p.activateBus(kAudio, kOutput, 0, true);
        ProcessSetup setup{kRealtime, kSample32, 512, 48000};
        require(p.setupProcessing(setup) == kResultOk, "failed-load setup");
        std::array<float, 512> left{}, right{}, out_left{}, out_right{};
        float* ins[] = {left.data(), right.data()};
        float* outs[] = {out_left.data(), out_right.data()};
        AudioBusBuffers input{}, output{};
        input.numChannels = output.numChannels = 2;
        input.channelBuffers32 = ins;
        output.channelBuffers32 = outs;
        ProcessData d{};
        d.symbolicSampleSize = kSample32;
        d.numInputs = d.numOutputs = 1;
        d.inputs = &input;
        d.outputs = &output;
        d.numSamples = 512;
        left.fill(1);
        right.fill(1);
        if (while_active) {
            // An empty 2x2 instance plays identity until a load fails.
            require(p.setActive(true) == kResultOk, "empty activate");
            p.setProcessing(true);
            for (int k = 0; k < 2; ++k)
                require(p.process(d) == kResultOk, "empty process");
            // Accelerate's FFT is not exact, so compare with a tolerance.
            require(std::abs(out_left[511] - 1) < 1e-5 && std::abs(out_right[511] - 1) < 1e-5,
                    "empty 2x2 instance not identity");
            convolver::HostState wrong;
            wrong.configuration =
                convolver::Configuration{44100, 2, 2, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
            MemoryStream state;
            require(write_state(&state, wrong), "encode wrong-rate state");
            state.seek(0, IBStream::kIBSeekSet, nullptr);
            require(p.setState(&state) != kResultOk, "wrong-rate first load accepted while active");
        } else {
            MemoryStream damaged;
            const std::array<unsigned char, 16> junk{16, 0, 0, 0, 1, 2,  3,  4,
                                                     5,  6, 7, 8, 9, 10, 11, 12};
            damaged.write(const_cast<unsigned char*>(junk.data()), 16, nullptr);
            damaged.seek(0, IBStream::kIBSeekSet, nullptr);
            require(p.setState(&damaged) != kResultOk, "malformed first state accepted");
            require(p.setActive(true) == kResultOk, "activate after a malformed first state");
            p.setProcessing(true);
        }
        for (int k = 0; k < 3; ++k) // Past any 512-frame transition.
            require(p.process(d) == kResultOk, "process after a failed first load");
        for (int i = 0; i < 512; ++i)
            require(out_left[i] == 0 && out_right[i] == 0,
                    while_active ? "failed first load while active not silent"
                                 : "malformed first state not silent");
        MemoryStream saved;
        require(p.getState(&saved) == kResultOk, "state after a failed first load");
        saved.seek(0, IBStream::kIBSeekSet, nullptr);
        const auto kept = read_state(&saved);
        require(!kept.configuration && kept.failed,
                "a failed first load must be saved as failed, without the filter");
        p.setProcessing(false);
        p.setActive(false);
        p.terminate();
        // Reopening that project in a fresh instance stays silent, not identity.
        Processor reopened;
        require(reopened.initialize(&context) == kResultOk, "reopen initialize");
        require(reopened.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "reopen buses");
        reopened.activateBus(kAudio, kInput, 0, true);
        reopened.activateBus(kAudio, kOutput, 0, true);
        saved.seek(0, IBStream::kIBSeekSet, nullptr);
        require(reopened.setState(&saved) == kResultOk, "saved failed load rejected on reopen");
        require(reopened.setupProcessing(setup) == kResultOk &&
                    reopened.setActive(true) == kResultOk,
                "reopen activate");
        reopened.setProcessing(true);
        for (int k = 0; k < 2; ++k)
            require(reopened.process(d) == kResultOk, "reopen process");
        for (int i = 0; i < 512; ++i)
            require(out_left[i] == 0 && out_right[i] == 0, "reopened failed load plays its input");
        reopened.setProcessing(false);
        reopened.setActive(false);
        reopened.terminate();
    }
}
// PLUG-8: every rejected setState and every mismatch found at activation keeps
// a structured reason, which the tests read even though VST3 cannot show it.
void failure_records() {
    using convolver::HostFailureKind;
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "failure-record initialize");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "failure-record buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 64, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "failure-record activate");
    play_block(p, 2, 2, 64);
    require(!p.failure(), "empty instance has a failure");
    const auto stream = [](const convolver::HostState& s) {
        auto m = std::make_unique<MemoryStream>();
        require(write_state(m.get(), s), "encode failure-record state");
        m->seek(0, IBStream::kIBSeekSet, nullptr);
        return m;
    };
    convolver::HostState good;
    good.configuration =
        convolver::Configuration{48000, 2, 2, 0, {}, {}, {{{.5f}, {{0, 1}}, {{1, 1}}}}};
    auto wrong = good;
    wrong.configuration->sample_rate = 44100;
    require(p.setState(stream(wrong).get()) == kResultFalse, "wrong-rate state accepted");
    auto f = p.failure();
    require(f && f->kind == HostFailureKind::sample_rate && f->request.number == 1 && f->silent &&
                f->sample_rate == 44100 && f->message.problem.find("44100 Hz") != std::string::npos,
            "wrong-rate first load not recorded");
    MemoryStream damaged;
    const std::array<unsigned char, 16> junk{12, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    damaged.write(const_cast<unsigned char*>(junk.data()), 16, nullptr);
    damaged.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&damaged) == kResultFalse, "damaged state accepted");
    f = p.failure();
    require(f && f->kind == HostFailureKind::state_unreadable && f->request.number == 2,
            "damaged state not recorded");
    MemoryStream newer;
    const std::array<unsigned char, 16> v4{12, 0, 0, 0, 0x43, 0x4e, 0x56, 0x53,
                                           4,  0, 0, 0, 0,    0,    0,    0};
    newer.write(const_cast<unsigned char*>(v4.data()), 16, nullptr);
    newer.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&newer) == kResultFalse, "newer state accepted");
    f = p.failure();
    require(f && f->kind == HostFailureKind::state_newer && f->request.number == 3 &&
                f->message.problem.find("newer version of Convolver") != std::string::npos,
            "newer state not recorded");
    require(p.setState(stream(good).get()) == kResultOk && !p.failure(),
            "a good state did not clear the failure");
    // A failed replacement keeps the working filter and says so.
    auto crossover = good;
    crossover.configuration->output_channels = 6;
    require(p.setState(stream(crossover).get()) == kResultFalse, "2->6 state accepted on 2x2");
    f = p.failure();
    require(f && f->kind == HostFailureKind::channels && f->request.number == 5 && !f->silent,
            "failed replacement not recorded");
    // Reactivating at another rate: the kept filter does not suit, so silence,
    // with the reason belonging to the request that supplied it (4).
    require(p.setActive(false) == kResultOk, "failure-record deactivate");
    setup.sampleRate = 44100;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "activate at 44.1 kHz");
    f = p.failure();
    require(f && f->kind == HostFailureKind::sample_rate && f->request.number == 4 && f->silent &&
                f->message.problem.find("the saved filter is for 48000 Hz") == 0,
            "mismatch at activation not recorded");
    require(p.setActive(false) == kResultOk, "failure-record deactivate again");
    setup.sampleRate = 48000;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "activate at 48 kHz again");
    f = p.failure();
    require(f && f->request.number == 5, "activation failure kept, or request 5's failure lost");
    p.setActive(false);
    p.terminate();
}
// This process's Convolver log, chosen once in main() (CI-28).
std::filesystem::path process_log;
// A file name of this process's own, so that concurrent runs never share it.
std::filesystem::path private_temp(const char* name) {
    return std::filesystem::temp_directory_path() /
           (std::string(name) + "-" + std::to_string(std::random_device{}()));
}
// VST3-9: a host rate that is not a whole number of hertz (48000/1.001, a
// pull-down rate) is accepted, and matched strictly: no filter suits it, so
// the plug-in keeps its filter, plays silence (bypass still plays the input),
// names both rates, and plays the filter again when the host returns to its
// rate. Only floating-point rounding of a whole rate counts as that rate.
void fractional_rates() {
    const convolver::test_log::LogSince log(process_log);
    const auto logged = [&](const std::string& part) {
        return log.text().find(part) != std::string::npos;
    };
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "fractional initialize");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "fractional buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    // Rates that are not finite, or outside 1 to 768000 Hz, are refused.
    const std::array<double, 6> invalid{std::numeric_limits<double>::quiet_NaN(),
                                        std::numeric_limits<double>::infinity(),
                                        -48000.0,
                                        0.0,
                                        0.999,
                                        convolver::max_sample_rate + 0.5};
    for (const double rate : invalid) {
        ProcessSetup bad{kRealtime, kSample32, 512, rate};
        require(p.setupProcessing(bad) == kInvalidArgument, "an invalid rate accepted");
    }
    // A project opening at a pull-down rate: the state arrives first.
    convolver::HostState saved;
    saved.configuration = convolver::Configuration{
        48000, 2, 2, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}, {{1}, {{1, 1}}, {{1, 1}}}}};
    MemoryStream state;
    require(write_state(&state, saved), "encode fractional-rate state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "state before setup refused");
    const double pulldown = 48000 / 1.001;
    ProcessSetup setup{kRealtime, kSample32, 512, pulldown};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "a fractional rate refused");
    auto f = p.failure();
    require(f && f->kind == convolver::HostFailureKind::sample_rate && f->silent &&
                f->message.problem ==
                    "the saved filter is for 48000 Hz, and the host runs at 47952.04795 Hz" &&
                f->message.fix == "Set the host's project or audio device to 48000 Hz." &&
                f->message.cause.find("whole number of hertz") != std::string::npos,
            "fractional rate mismatch not explained with both rates");
    require(logged("Started at 47952.04795 Hz, 2 in, 2 out"), "fractional rate not logged");
    // Silence, and bypass still plays the input (delayed by the latency).
    p.setProcessing(true);
    std::array<float, 512> left{}, right{}, out_left{}, out_right{};
    left.fill(1);
    right.fill(-1);
    float* ins[] = {left.data(), right.data()};
    float* outs[] = {out_left.data(), out_right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 512;
    const auto play = [&](int calls, std::optional<bool> bypass = {}) {
        ParameterChanges changes;
        int32 index = 0;
        if (bypass)
            changes.addParameterData(bypass_id, index)->addPoint(0, *bypass ? 1 : 0, index);
        d.inputParameterChanges = &changes;
        for (int k = 0; k < calls; ++k) {
            require(p.process(d) == kResultOk, "fractional-rate process");
            d.inputParameterChanges = nullptr;
        }
    };
    // Exactly, for silence; within float rounding, for a filtered signal.
    const auto all = [&](float l, float r) {
        const auto near = [](float value) {
            return [value](float x) { return value == 0 ? x == 0 : std::abs(x - value) <= 1e-5f; };
        };
        return std::all_of(out_left.begin(), out_left.end(), near(l)) &&
               std::all_of(out_right.begin(), out_right.end(), near(r));
    };
    play(2);
    require(all(0, 0), "a fractional rate played the filter");
    play(3, true);
    require(all(1, -1), "bypass at a fractional rate does not play the input");
    play(1, false);
    play(2);
    require(all(0, 0), "not silent again after bypass");
    MemoryStream kept;
    require(p.getState(&kept) == kResultOk, "save at a fractional rate");
    kept.seek(0, IBStream::kIBSeekSet, nullptr);
    require(read_state(&kept).configuration->sample_rate == 48000,
            "the filter was not kept at a fractional rate");
    // A replacement at the fractional rate is refused, the filter kept.
    MemoryStream again;
    write_state(&again, saved);
    again.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&again) == kResultFalse && p.failure() &&
                p.failure()->message.problem.find("runs at 47952.04795 Hz") != std::string::npos,
            "a replacement accepted at a fractional rate");
    p.setProcessing(false);
    require(p.setActive(false) == kResultOk, "fractional deactivate");
    // Proposed at the whole part after running at the fractional rate: the
    // host's rate is not known again until activation, so state for another
    // rate is kept to be checked then, as at any proposed rate.
    setup.sampleRate = 47952;
    MemoryStream proposed;
    write_state(&proposed, saved);
    proposed.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setupProcessing(setup) == kResultOk && p.setState(&proposed) == kResultOk,
            "the whole part taken as the fractional rate already known");
    // Another fractional rate is a change of rate too.
    setup.sampleRate = 44100 / 1.001;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk &&
                p.failure() &&
                p.failure()->message.problem.find("runs at 44055.94406 Hz") != std::string::npos,
            "a second fractional rate not explained");
    require(p.setActive(false) == kResultOk, "second fractional deactivate");
    // Back at the filter's rate, give or take floating-point rounding: the
    // filter plays again.
    setup.sampleRate = std::nextafter(48000.0, 49000.0);
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "the filter's rate refused");
    f = p.failure();
    require(!f || !f->silent, "the filter's rate did not bring the filter back");
    require(logged("Started at 48000 Hz, 2 in, 2 out"), "a rounded rate not logged as 48000 Hz");
    p.setProcessing(true);
    play(2);
    require(all(1, -1), "the filter does not play at its rate again");
    p.setProcessing(false);
    require(p.setActive(false) == kResultOk, "recovered deactivate");
    // A rate a thousandth of a hertz away is another rate.
    setup.sampleRate = 48000.001;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk &&
                p.failure() && p.failure()->silent &&
                p.failure()->message.problem.find("runs at 48000.001 Hz") != std::string::npos,
            "a nearby rate treated as the filter's");
    p.setActive(false);
    p.terminate();
}
// DIAG-1: the Status parameter shows each failure in one line, and the
// Convolver log holds the full explanation.
void status_and_log() {
    const auto contains = [](const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    };
    // CI-28: the process's own log (main), read from where it ended, so a
    // crash here keeps what the case logged.
    const convolver::test_log::LogSince log(process_log);
    const auto log_text = [&] { return log.text(); };
    HostApplication context;
    Processor p;
    Controller c;
    // CI-6: the Convolver log names the SDK the plug-in was built with.
    require(contains(convolver::execution_line(), ", built with VST3 SDK "),
            "SDK missing from the Convolver log's session line");
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk,
            "status initialize");
    ParameterInfo info{};
    require(c.getParameterCount() == 5 && c.getParameterInfo(4, info) == kResultOk &&
                info.id == status_id && (info.flags & ParameterInfo::kIsReadOnly) &&
                (info.flags & ParameterInfo::kIsList) &&
                !(info.flags & ParameterInfo::kCanAutomate) && info.stepCount == 5,
            "Status parameter not read-only list");
    // The text the host shows for the parameter's current value.
    const auto shown = [&] {
        String128 text{};
        require(c.getParamStringByValue(status_id, c.getParamNormalized(status_id), text) ==
                    kResultOk,
                "status text");
        std::string ascii;
        for (int i = 0; text[i]; ++i)
            ascii += static_cast<char>(text[i]);
        return ascii;
    };
    const auto status = [&] {
        return static_cast<convolver::HostStatus>(
            std::lround(c.getParameterObject(status_id)->toPlain(c.getParamNormalized(status_id))));
    };
    require(p.connect(&c) == kResultOk && c.connect(&p) == kResultOk, "status connect");
    require(shown() == "No filter loaded. Load a Convolver preset." &&
                status() == convolver::HostStatus::no_filter,
            "status before activation");
    // Other values read as their names (a host asking how a value would read).
    String128 name{};
    require(c.getParamStringByValue(status_id, 0, name) == kResultOk && name[0] == 'A' &&
                name[1] == 'c',
            "status value names");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    // DIAG-1: refused host settings are logged with the reason; a refused
    // layout only once per instance, as hosts try one after another.
    SpeakerArrangement nine = (SpeakerArrangement(1) << 9) - 1;
    require(p.setBusArrangements(&nine, 1, &stereo, 1) == kResultFalse &&
                p.setBusArrangements(&stereo, 1, &nine, 1) == kResultFalse,
            "status nine channels accepted");
    const auto count = [](const std::string& text, const char* part) {
        std::size_t n = 0;
        for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
            ++n;
        return n;
    };
    require(contains(log_text(), "Refused the host's channel layout: it has 9 input and 2 "
                                 "output channels; Convolver takes 1 to 8 of each.") &&
                count(log_text(), "Refused the host's channel layout") == 1,
            "refused layout not logged once");
    ProcessSetup odd{kRealtime, kSample32, 64, 768000.5};
    require(p.setupProcessing(odd) == kInvalidArgument &&
                contains(log_text(), "Refused the host's processing settings (768000.5 Hz, "
                                     "blocks of up to 64 samples): the sample rate must be "
                                     "from 1 to 768000 Hz."),
            "refused rate not logged");
    require(p.getState(nullptr) == kResultFalse &&
                contains(log_text(), convolver::save_failure_message),
            "failed save not logged");
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "status buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 64, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "status activate");
    require(contains(shown(), "audio passes unchanged") && contains(log_text(), "VST3 #") &&
                contains(log_text(), ": Started at 48000 Hz, 2 in, 2 out, blocks of up to 64 "
                                     "frames. No filter, so audio passes unchanged."),
            "empty start not shown or logged");
    play_block(p, 2, 2, 64);
    const auto stream = [](const convolver::HostState& s) {
        auto m = std::make_unique<MemoryStream>();
        require(write_state(m.get(), s), "encode status state");
        m->seek(0, IBStream::kIBSeekSet, nullptr);
        return m;
    };
    convolver::HostState good;
    good.configuration =
        convolver::Configuration{48000, 2, 2, 0, {}, {}, {{{.5f}, {{0, 1}}, {{1, 1}}}}};
    auto wrong = good;
    wrong.configuration->sample_rate = 44100;
    // A wrong-rate preset as the first load: silence, said in the window and
    // explained in the log.
    require(p.setState(stream(wrong).get()) == kResultFalse, "status wrong rate accepted");
    require(shown() == "Silent: the filter is for 44100 Hz, the host runs at 48000 Hz. Details "
                       "in the Convolver log." &&
                status() == convolver::HostStatus::load_failed,
            "failed first load not shown");
    require(contains(log_text(), "Not loaded, so Convolver plays silence: the filter is for "
                                 "44100 Hz, and the host runs at 48000 Hz. Cause: Convolver "
                                 "never resamples"),
            "failed first load not logged");
    require(p.setState(stream(good).get()) == kResultOk &&
                shown() == "Filter loaded: 48000 Hz, 2 in, 2 out." &&
                status() == convolver::HostStatus::active &&
                contains(log_text(), ": Loaded a filter for 48000 Hz, 2 in, 2 out.\n"),
            "good load not shown or logged");
    // Wrong preset while a filter plays: the filter keeps playing, and both say so.
    require(
        p.setState(stream(wrong).get()) == kResultFalse &&
            contains(shown(), "Load failed, previous filter kept: the filter is for 44100 Hz") &&
            contains(log_text(), "Not loaded; the previous filter keeps playing: the filter is "
                                 "for 44100 Hz"),
        "refused replacement not shown or logged");
    MemoryStream damaged;
    const std::array<unsigned char, 16> junk{12, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    damaged.write(const_cast<unsigned char*>(junk.data()), 16, nullptr);
    damaged.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&damaged) == kResultFalse &&
                contains(shown(), "the saved project or preset could not be read") &&
                contains(log_text(), "the saved project or preset could not be read: "),
            "damaged state not shown or logged");
    // DIAG-1, jrp's reproduction: extreme but finite input and filter values
    // overflow the output. The audio thread writes Fault to the Status
    // parameter; the host passes it to the controller, which asks the
    // processor to explain; the log says what happened and what to change.
    auto loud = good;
    loud.configuration->paths[0].impulse[0] = std::numeric_limits<float>::max() / 4;
    require(p.setState(stream(loud).get()) == kResultOk &&
                status() == convolver::HostStatus::active,
            "loud filter not loaded");
    {
        std::array<float, 64> left{}, right{};
        left[0] = std::numeric_limits<float>::max() / 4;
        float* ins[] = {left.data(), right.data()};
        float* outs[] = {left.data(), right.data()};
        AudioBusBuffers input{}, output{};
        input.numChannels = output.numChannels = 2;
        input.channelBuffers32 = ins;
        output.channelBuffers32 = outs;
        ParameterChanges changes(4);
        ProcessData d{};
        d.symbolicSampleSize = kSample32;
        d.numInputs = d.numOutputs = 1;
        d.inputs = &input;
        d.outputs = &output;
        d.outputParameterChanges = &changes;
        d.numSamples = 64;
        p.setProcessing(true);
        // The overflow reaches the output after the latency.
        for (int block = 0; block < 6; ++block) {
            left.fill(0);
            left[0] = std::numeric_limits<float>::max() / 4;
            require(p.process(d) == kResultOk, "overflowing block refused");
            for (int i = 0; i < 64; ++i)
                require(std::isfinite(left[i]) && std::isfinite(right[i]), "overflow not silenced");
        }
        // One Fault point, written once however many blocks overflow: Fault
        // is value 4 of 0 to 5 (Bypassed is last, PLUG-22), so 0.8.
        require(changes.getParameterCount() == 1, "Fault not written to the Status parameter");
        auto* queue = changes.getParameterData(0);
        int32 offset = -1;
        ParamValue value = 0;
        require(queue && queue->getParameterId() == status_id && queue->getPointCount() == 1 &&
                    queue->getPoint(0, offset, value) == kResultOk && value == 0.8,
                "Fault point");
        require(contains(log_text(), "too large") == false, "fault logged on the audio thread");
        // The host hands the value to the controller, which reads it as the
        // name until the processor has explained; here as a host that keeps
        // normalized values as float does (0.80000001).
        require(c.setParamNormalized(status_id, static_cast<float>(value)) == kResultOk,
                "host passes Fault on");
    }
    require(status() == convolver::HostStatus::fault &&
                shown() == "Fault: output too large, silenced and the filter restarted. Details "
                           "in the Convolver log.",
            "fault not shown");
    require(contains(log_text(), "the output became too large to represent (") &&
                contains(log_text(), "Lower the level going into Convolver"),
            "fault not logged");
    // Stopping playback explains nothing: setProcessing may run on the audio
    // thread. Nothing new is logged twice.
    {
        const auto logged = log_text();
        p.setProcessing(false);
        MemoryStream saved;
        require(p.getState(&saved) == kResultOk && log_text() == logged, "fault logged twice");
    }
    // A second episode with the same text is logged too (the Status already
    // shows Fault, so no new point): saving explains it.
    {
        std::array<float, 64> left{}, right{};
        float* ins[] = {left.data(), right.data()};
        float* outs[] = {left.data(), right.data()};
        AudioBusBuffers input{}, output{};
        input.numChannels = output.numChannels = 2;
        input.channelBuffers32 = ins;
        output.channelBuffers32 = outs;
        ParameterChanges changes(4);
        ProcessData d{};
        d.symbolicSampleSize = kSample32;
        d.numInputs = d.numOutputs = 1;
        d.inputs = &input;
        d.outputs = &output;
        d.outputParameterChanges = &changes;
        d.numSamples = 64;
        p.setProcessing(true);
        for (int block = 0; block < 6; ++block) {
            left.fill(0);
            left[0] = std::numeric_limits<float>::max() / 4;
            require(p.process(d) == kResultOk, "second overflow refused");
        }
        require(changes.getParameterCount() == 0, "Fault written again while shown");
        MemoryStream saved;
        require(p.getState(&saved) == kResultOk &&
                    count(log_text(), "the output became too large to represent (") == 2,
                "second fault episode not logged on save");
    }
    // jrp's review of 0.4.43: a load failure comes before Fault on the audio
    // side too. After a refused replacement, invalid input writes no Fault
    // over "Load failed"; an accepted load releases the signal.
    {
        require(p.setState(stream(good).get()) == kResultOk &&
                    p.setState(stream(wrong).get()) == kResultFalse &&
                    status() == convolver::HostStatus::load_failed,
                "refused replacement before invalid input");
        std::array<float, 64> left{}, right{};
        float* ins[] = {left.data(), right.data()};
        float* outs[] = {left.data(), right.data()};
        AudioBusBuffers input{}, output{};
        input.numChannels = output.numChannels = 2;
        input.channelBuffers32 = ins;
        output.channelBuffers32 = outs;
        ProcessData d{};
        d.symbolicSampleSize = kSample32;
        d.numInputs = d.numOutputs = 1;
        d.inputs = &input;
        d.outputs = &output;
        d.numSamples = 64;
        const auto play_invalid = [&](ParameterChanges& changes) {
            d.outputParameterChanges = &changes;
            left.fill(0);
            left[0] = std::numeric_limits<float>::quiet_NaN();
            require(p.process(d) == kResultOk, "invalid input refused");
        };
        ParameterChanges held(4);
        play_invalid(held);
        require(held.getParameterCount() == 0 && status() == convolver::HostStatus::load_failed,
                "Fault written over Load failed");
        // Nor does the log say Fault shows (jrp's review of 0.5.2).
        {
            const auto shows_fault = count(log_text(), "The status shows Fault");
            MemoryStream explained;
            require(p.getState(&explained) == kResultOk &&
                        contains(log_text(), "The status shows Load failed instead of Fault") &&
                        count(log_text(), "The status shows Fault") == shows_fault,
                    "the log says the status shows Fault over Load failed");
        }
        require(p.setState(stream(good).get()) == kResultOk, "good after refusal");
        ParameterChanges released(4);
        play_invalid(released);
        require(released.getParameterCount() == 1, "Fault not written after an accepted load");
        MemoryStream saved;
        require(p.getState(&saved) == kResultOk && status() == convolver::HostStatus::fault,
                "fault after an accepted load not shown");
    }
    // Restarted at another rate: silence until the rate is put back.
    require(p.setState(stream(good).get()) == kResultOk &&
                status() == convolver::HostStatus::active,
            "status good again");
    require(p.setActive(false) == kResultOk, "status deactivate");
    setup.sampleRate = 44100;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "status activate at 44.1 kHz");
    require(contains(shown(), "Silent: the filter is for 48000 Hz, the host runs at 44100 Hz") &&
                contains(log_text(), "Started at 44100 Hz, 2 in, 2 out, blocks of up to 64 "
                                     "frames. Convolver plays silence. The saved filter is for "
                                     "48000 Hz"),
            "mismatch at activation not shown or logged");
    // Restarting the same way again does not repeat the line.
    const auto before = log_text();
    require(p.setActive(false) == kResultOk && p.setActive(true) == kResultOk &&
                log_text() == before,
            "repeated start logged again");
    // State restored before the host is known (a project opening): not checked yet.
    Processor q;
    Controller d;
    require(q.initialize(&context) == kResultOk && d.initialize(&context) == kResultOk &&
                q.connect(&d) == kResultOk && d.connect(&q) == kResultOk,
            "second status instance");
    require(q.setState(stream(wrong).get()) == kResultOk, "unchecked state refused");
    String128 text{};
    d.getParamStringByValue(status_id, d.getParamNormalized(status_id), text);
    require(text[0] == 'F' && d.status_text() ==
                                  "Filter loaded: 44100 Hz, 2 in, 2 out; waiting for audio "
                                  "settings from the host.",
            "unchecked state not shown");
    q.disconnect(&d);
    d.disconnect(&q);
    q.terminate();
    d.terminate();
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    p.terminate();
    c.terminate();
}
// VST3-8: the reported tail is measured at the output, as Steinberg's tail
// test does: the last input sample's last tap arrives at sample tail - 1 after
// the input stops, and only rounding residue follows.
void tail_measured_at_output() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "tail initialize");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    convolver::HostState saved;
    saved.configuration = convolver::Configuration{48000, 2, 2, 3, {}, {}, {}};
    std::vector<float> taps(300, 0.f);
    taps.front() = .5f;
    taps.back() = 1.f;
    saved.configuration->paths = {{taps, {{0, 1}}, {{0, 1}}}, {taps, {{1, 1}}, {{1, 1}}}};
    MemoryStream state;
    require(write_state(&state, saved), "encode tail state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore tail state");
    ProcessSetup setup{kRealtime, kSample32, 64, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "tail activate");
    const auto tail = p.getTailSamples();
    require(tail == 299 + latency_samples, "tail is not the filter's tail plus the latency");
    p.setProcessing(true);
    std::array<float, 64> left{}, right{}, out_left{}, out_right{};
    float* ins[] = {left.data(), right.data()};
    float* outs[] = {out_left.data(), out_right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 64;
    const int stop = 11 * 64; // The input stops here; the last input sample is 1.
    float last = 0, after = 0;
    for (int b = 0; b < 40; ++b) {
        for (int i = 0; i < 64; ++i) {
            const int n = b * 64 + i;
            left[i] = right[i] = n < stop ? (n == stop - 1 ? 1.f : std::sin(.37f * n)) : 0.f;
        }
        require(p.process(d) == kResultOk, "tail process");
        for (int i = 0; i < 64; ++i) {
            const auto since = b * 64 + i - stop;
            if (since == static_cast<int>(tail) - 1)
                last = out_left[i];
            if (since >= static_cast<int>(tail))
                after = std::max({after, std::abs(out_left[i]), std::abs(out_right[i])});
        }
    }
    require(std::abs(last - 1.f) < 1e-5, "the reported tail does not end at the filter's last tap");
    require(after < 1e-6, "output continues after the reported tail");
    p.setProcessing(false);
    p.setActive(false);
    p.terminate();
}
// VST3-10: an output channel is flagged silent exactly when a call left it all
// zero. Silent input (even flagged silent) does not flag the output until the
// filter's tail and the latency have drained, and bypass keeps the convolution
// running, so the tail of audio played while bypassed is heard after it.
void output_silence_flags() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "silence initialize");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    convolver::HostState saved;
    saved.configuration = convolver::Configuration{48000, 2, 2, 3, {}, {}, {}};
    std::vector<float> taps(300, 0.f);
    taps.front() = .5f;
    taps.back() = 1.f;
    saved.configuration->paths = {{taps, {{0, 1}}, {{0, 1}}}, {taps, {{1, 1}}, {{1, 1}}}};
    MemoryStream state;
    require(write_state(&state, saved), "encode silence state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore silence state");
    // 100-frame calls: not a multiple or divisor of the 64-frame first block.
    constexpr int frames = 100;
    ProcessSetup setup{kRealtime, kSample32, frames, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "silence activate");
    const auto tail = static_cast<int>(p.getTailSamples());
    p.setProcessing(true);
    std::array<float, frames> left{}, right{}, out_left{}, out_right{};
    float* ins[] = {left.data(), right.data()};
    float* outs[] = {out_left.data(), out_right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = frames;
    int frame = 0;
    // One call with input from `signal` (frame number to value); the bypass
    // switches at `offset` when set. Returns the output flags after checking
    // that each flag means exactly an all-zero channel.
    auto call = [&](auto signal, std::optional<std::pair<int, bool>> bypass = {}) {
        for (int i = 0; i < frames; ++i)
            left[i] = right[i] = signal(frame + i);
        const bool zero_in = std::all_of(left.begin(), left.end(), [](float v) { return v == 0; });
        input.silenceFlags = zero_in ? 3 : 0;
        output.silenceFlags = 0;
        ParameterChanges changes;
        d.inputParameterChanges = nullptr;
        if (bypass) {
            int32 index = 0;
            changes.addParameterData(bypass_id, index)
                ->addPoint(bypass->first, bypass->second ? 1 : 0, index);
            d.inputParameterChanges = &changes;
        }
        require(p.process(d) == kResultOk, "silence process");
        for (int c = 0; c < 2; ++c) {
            const auto* out = c ? out_right.data() : out_left.data();
            const bool zero = std::all_of(out, out + frames, [](float v) { return v == 0; });
            require(zero == ((output.silenceFlags >> c) & 1),
                    "an output flag does not match whether the channel is all zero");
        }
        frame += frames;
        return output.silenceFlags;
    };
    const int stop = 7 * frames + 13; // The input stops here; its last sample is 1.
    auto music = [&](int n) {
        return n < stop ? (n == stop - 1 ? 1.f : std::sin(.37f * float(n)) * .5f + .6f) : 0.f;
    };
    require(call(music) == (L >= frames ? 3 : 0),
            "the latency is silence, so it is flagged; audio is not");
    while (frame < stop + tail - frames)
        require(call(music) == 0 || frame < latency_samples,
                "audio or the filter's tail was flagged silent");
    // The call holding the tail's last sample. Float rounding in the last
    // blocks computed from the audio can leave values near 1e-8 for up to two
    // partitions after it; from then on the output is exactly zero.
    require(call(music) == 0, "the end of the tail was flagged silent");
    auto drained = [&](auto signal) {
        const int limit = frame + 2 * latency_samples + frames;
        while (call(signal) != 3)
            require(frame <= limit, "drained output not flagged silent");
        for (int k = 0; k < 3; ++k)
            require(call(signal) == 3, "silence after the tail not flagged");
    };
    drained(music);
    // Bypass on while silent: the dry signal is silence too, so still flagged.
    require(call(music, std::pair{0, true}) == 3, "bypassed silence not flagged");
    // An impulse played during bypass, then bypass off 200 frames later: the
    // filter's tail of that impulse (its last tap, 1, at 299 + latency, after
    // the 64-frame ramp even at zero latency) is heard, because bypass kept
    // the convolution running.
    const int impulse = frame + 5;
    auto click = [&](int n) { return n == impulse ? 1.f : 0.f; };
    float last = 0;
    for (int k = 0; k < 8; ++k) {
        const bool switch_off = frame <= impulse + 200 && impulse + 200 < frame + frames;
        // Flags are checked against the content in call().
        if (switch_off)
            call(click, std::pair{impulse + 200 - frame, false});
        else
            call(click);
        const int at = impulse + latency_samples + 299;
        if (frame - frames <= at && at < frame)
            last = out_left[at - (frame - frames)];
    }
    require(std::abs(last - 1.f) < 1e-5, "bypass did not keep the convolution history");
    drained(click);
    p.setProcessing(false);
    p.setActive(false);
    p.terminate();
}
// VST3-10 with 64-bit host buffers: the flags follow each channel's content.
void output_silence_flags_64() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "64-bit silence initialize");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    convolver::HostState saved;
    // Input 0 to output 1 only: output 0 is always silent.
    saved.configuration =
        convolver::Configuration{48000, 2, 2, 3, {}, {}, {{{1, 0, .5f}, {{0, 1}}, {{1, 1}}}}};
    MemoryStream state;
    require(write_state(&state, saved), "encode 64-bit silence state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore 64-bit silence state");
    constexpr int frames = 100;
    ProcessSetup setup{kRealtime, kSample64, frames, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "64-bit silence activate");
    p.setProcessing(true);
    std::array<double, frames> left{}, right{}, out_left{}, out_right{};
    double* ins[] = {left.data(), right.data()};
    double* outs[] = {out_left.data(), out_right.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers64 = ins;
    output.channelBuffers64 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample64;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = frames;
    // An impulse at frame 0 reaches output 1 at the latency and two frames
    // later (taps 1 and .5); rounding residue can follow for a few blocks.
    std::vector<std::uint64_t> flags;
    for (int b = 0; b < 10; ++b) {
        left[0] = b == 0 ? 1 : 0;
        output.silenceFlags = 0;
        require(p.process(d) == kResultOk, "64-bit silence process");
        for (int c = 0; c < 2; ++c) {
            const auto& out = c ? out_right : out_left;
            const bool zero = std::all_of(out.begin(), out.end(), [](double v) { return v == 0; });
            require(zero == ((output.silenceFlags >> c) & 1),
                    "a 64-bit output flag does not match whether the channel is all zero");
        }
        flags.push_back(output.silenceFlags);
    }
    const auto arrives = static_cast<std::size_t>(L / frames);
    require(std::all_of(flags.begin(), flags.begin() + static_cast<std::ptrdiff_t>(arrives),
                        [](std::uint64_t f) { return f == 3; }) &&
                flags[arrives] == 1 && flags[8] == 3 && flags[9] == 3,
            "64-bit output flags do not follow the output");
    p.setProcessing(false);
    p.setActive(false);
    p.terminate();
}
// VST3-8: the Bypass text converts both ways, and the controller's own
// state calls succeed (it keeps no state, so they store and read nothing).
// VST3-4: Gain, Mix and Latency as parameters, applied by the processor,
// saved in the component state (version 3 only when not at the defaults), and
// a new Latency reported to the host with kLatencyChanged.
void settings_parameters() {
    HostApplication context;
    Handler handler; // Outlives the controller, which holds it.
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk,
            "settings initialize");
    require(c.setComponentHandler(&handler) == kResultOk, "settings handler");
    require(p.connect(&c) == kResultOk && c.connect(&p) == kResultOk, "settings connect");
    // The settings first, then Bypass and Status, with their IDs and flags.
    const std::array<ParamID, 5> order{gain_id, mix_id, latency_id, bypass_id, status_id};
    require(c.getParameterCount() == 5, "settings parameter count");
    for (int32 i = 0; i < 5; ++i) {
        ParameterInfo info{};
        require(c.getParameterInfo(i, info) == kResultOk && info.id == order[i],
                "settings parameter order");
        const bool automatable = (info.flags & ParameterInfo::kCanAutomate) != 0;
        if (info.id == gain_id)
            require(automatable && info.stepCount == 0 &&
                        std::abs(info.defaultNormalizedValue - normalized_gain(0)) < 1e-12,
                    "Gain parameter");
        // A host that keeps normalized values as float still sets 0 dB exactly,
        // so a project reset to the default saves as before the settings.
        require(gain_db(static_cast<float>(normalized_gain(0))) == 0 &&
                    gain_db(std::nextafter(static_cast<float>(normalized_gain(0)), 1.f)) == 0 &&
                    gain_db(normalized_gain(-.01)) < 0,
                "Gain near 0 dB from a float host");
        if (info.id == mix_id)
            require(automatable && info.defaultNormalizedValue == 1, "Mix parameter");
        if (info.id == latency_id)
            require(!automatable && (info.flags & ParameterInfo::kIsList) && info.stepCount == 7 &&
                        latency_frames(info.defaultNormalizedValue) == 0,
                    "Latency parameter");
    }
    const auto text = [&](ParamID id, ParamValue v) {
        String128 t{};
        require(c.getParamStringByValue(id, v, t) == kResultOk, "settings text");
        std::string ascii;
        for (int i = 0; t[i]; ++i)
            ascii += static_cast<char>(t[i]);
        return ascii;
    };
    require(text(gain_id, normalized_gain(-6.5)) == "-6.5" && text(mix_id, .25) == "25" &&
                text(latency_id, normalized_latency(1024)) == "1024 frames",
            "settings value text");
    // Hosts find the wet/dry mix by its function name.
    IParameterFunctionName* function = nullptr;
    require(c.queryInterface(IParameterFunctionName::iid, reinterpret_cast<void**>(&function)) ==
                    kResultOk &&
                function,
            "IParameterFunctionName not offered");
    ParamID found = kNoParamId;
    require(function->getParameterIDFromFunctionName(kRootUnitId, FunctionNameType::kDryWetMix,
                                                     found) == kResultTrue &&
                found == mix_id,
            "Mix not found as kDryWetMix");
    function->release();

    // A one-tap filter of gain 0.5: wet is half the input, dry the input.
    SpeakerArrangement mono = SpeakerArr::kMono;
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "settings buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    convolver::HostState half;
    half.configuration =
        convolver::Configuration{48000, 1, 1, 0, {}, {}, {{{.5f}, {{0, 1}}, {{0, 1}}}}};
    MemoryStream state;
    require(write_state(&state, half), "encode settings state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore settings state");
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk &&
                p.setProcessing(true) == kResultOk,
            "settings activate");
    std::array<float, 512> in{}, out{};
    float* ins[] = {in.data()};
    float* outs[] = {out.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 1;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 512;
    in.fill(1);
    const auto run = [&](ParameterChanges* changes) {
        d.inputParameterChanges = changes;
        require(p.process(d) == kResultOk, "settings process");
        d.inputParameterChanges = nullptr;
        require(p.process(d) == kResultOk, "settings process"); // Past the 64-frame ramp.
        return out[511];
    };
    require(std::abs(run(nullptr) - .5f) < 1e-6, "default settings changed the output");
    // Gain -6.0206 dB halves the wet signal; the host sends the last point
    // of a block, which the processor takes.
    const double halve = 20 * std::log10(.5);
    ParameterChanges gain;
    int32 index = 0;
    auto* queue = gain.addParameterData(gain_id, index);
    queue->addPoint(0, normalized_gain(0), index);
    queue->addPoint(100, normalized_gain(halve), index);
    require(std::abs(run(&gain) - .25f) < 1e-5, "Gain not applied");
    // Mix 25%: a quarter of the wet (0.25) and three quarters of the dry (1).
    ParameterChanges mix;
    index = 0;
    mix.addParameterData(mix_id, index)->addPoint(0, .25, index);
    require(std::abs(run(&mix) - (.25f * .25f + .75f)) < 1e-5, "Mix not applied");
    // The settings travel in the state as version 3, to a new processor and
    // to the controller.
    c.setParamNormalized(gain_id, normalized_gain(halve));
    c.setParamNormalized(mix_id, .25);
    MemoryStream saved;
    require(p.getState(&saved) == kResultOk, "save settings");
    saved.seek(0, IBStream::kIBSeekSet, nullptr);
    {
        std::array<unsigned char, 12> head{};
        int32 got = 0;
        saved.read(head.data(), 12, &got);
        require(got == 12 && head[8] == 3, "settings not saved as state version 3");
        saved.seek(0, IBStream::kIBSeekSet, nullptr);
    }
    const auto restored = read_state(&saved);
    require(restored.settings && std::abs(restored.settings->gain_db - halve) < 1e-9 &&
                restored.settings->mix == .25 && restored.settings->latency == latency_samples,
            "saved settings");
    Handler other_handler;
    Controller other;
    require(other.initialize(&context) == kResultOk &&
                other.setComponentHandler(&other_handler) == kResultOk,
            "second controller");
    saved.seek(0, IBStream::kIBSeekSet, nullptr);
    require(other.setComponentState(&saved) == kResultOk &&
                std::abs(other.getParamNormalized(gain_id) - normalized_gain(halve)) < 1e-12 &&
                other.getParamNormalized(mix_id) == .25 && other_handler.latency_restarts() == 0,
            "controller did not read the settings");
    other.terminate();
    // An older state (a preset from convolver-preset) brings the defaults back.
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore an older state");
    require(std::abs(run(nullptr) - .5f) < 1e-5, "an older state kept the settings");
    MemoryStream plain;
    require(p.getState(&plain) == kResultOk, "save default settings");
    plain.seek(0, IBStream::kIBSeekSet, nullptr);
    {
        std::array<unsigned char, 12> head{};
        int32 got = 0;
        plain.read(head.data(), 12, &got);
        require(got == 12 && head[8] == 1, "default settings not saved as state version 1");
    }
    // Latency (VST3-16): while active the new choice plays at once, before
    // the controller asks the host to restart, so a host that only reads
    // getLatencySamples() when asked (as REAPER 7.85 does) reads the latency
    // that plays, and one that restarts the component gets the same.
    const auto impulse_delay = [&] {
        in.fill(0);
        for (int b = 0; b < 10; ++b) // Longer than the longest Latency.
            require(p.process(d) == kResultOk, "latency flush");
        in[0] = 1;
        int at = -1;
        for (int b = 0; b < 10 && at < 0; ++b) {
            require(p.process(d) == kResultOk, "latency process");
            in[0] = 0;
            for (int i = 0; i < 512; ++i)
                if (at < 0 && std::abs(out[i]) > 1e-6)
                    at = b * 512 + i;
        }
        return at;
    };
    std::vector<uint32> read_at_restart;
    handler.on_restart = [&](int32 flags) {
        if (flags & kLatencyChanged)
            read_at_restart.push_back(p.getLatencySamples());
    };
    handler.restarts.clear();
    require(impulse_delay() == L, "output not delayed by the default Latency");
    {
        const convolver::test_log::LogSince log(process_log);
        require(c.setParamNormalized(latency_id, normalized_latency(1024)) == kResultOk &&
                    p.settings().latency == 1024 && p.getLatencySamples() == 1024 &&
                    p.getTailSamples() == 1024 && handler.latency_restarts() == 1 &&
                    read_at_restart == std::vector<uint32>{1024},
                "the new Latency was not in use when the host was asked to restart");
        require(log.text().find("Latency changed from " + std::to_string(latency_samples) +
                                " to 1024 frames; the sound paused "
                                "briefly") != std::string::npos,
                "the Latency change not logged");
    }
    require(impulse_delay() == 1024, "output not delayed by the new Latency at once");
    require(c.setParamNormalized(latency_id, normalized_latency(1024)) == kResultOk &&
                handler.latency_restarts() == 1,
            "an unchanged Latency restarted the component");
    // A host that restarts the component, as the SDK describes, keeps it.
    require(p.setProcessing(false) == kResultOk && p.setActive(false) == kResultOk &&
                p.getLatencySamples() == 1024 && p.setActive(true) == kResultOk &&
                p.setProcessing(true) == kResultOk && p.getLatencySamples() == 1024,
            "restart after Latency");
    require(impulse_delay() == 1024, "output not delayed by the Latency after the restart");
    // Back down, and quick successive choices: each plays when it is read.
    read_at_restart.clear();
    for (const std::size_t frames : {256, 0, 2048, 64, 512})
        require(c.setParamNormalized(latency_id, normalized_latency(frames)) == kResultOk &&
                    p.getLatencySamples() == frames && read_at_restart.back() == frames,
                "a successive Latency was not in use when read");
    require(read_at_restart == std::vector<uint32>{256, 0, 2048, 64, 512},
            "successive Latency choices");
    require(impulse_delay() == 512, "output not delayed by the last of several choices");
    // A processor that cannot be built: the running one keeps playing, the
    // host reads the latency that plays, and the log and report say why.
    latency_preparation_for_tests = [] { throw std::runtime_error("no memory for the test"); };
    {
        const convolver::test_log::LogSince log(process_log);
        read_at_restart.clear();
        const auto chosen = c.setParamNormalized(latency_id, normalized_latency(2048));
        latency_preparation_for_tests = nullptr;
        require(chosen == kResultOk && p.settings().latency == 2048 &&
                    p.getLatencySamples() == 512 && read_at_restart == std::vector<uint32>{512},
                "a failed Latency preparation changed the latency the host reads");
        require(log.text().find("Latency 2048 frames could not be applied, so the plug-in keeps "
                                "512 frames: no memory for the test") != std::string::npos,
                "a failed Latency preparation not logged");
    }
    require(impulse_delay() == 512, "a failed Latency preparation changed the output");
    // The next activation builds the chosen one.
    require(p.setProcessing(false) == kResultOk && p.setActive(false) == kResultOk &&
                p.setActive(true) == kResultOk && p.setProcessing(true) == kResultOk &&
                p.getLatencySamples() == 2048 && impulse_delay() == 2048,
            "the activation after a failed Latency did not use it");
    // Inactive, a choice waits for the activation.
    require(p.setProcessing(false) == kResultOk && p.setActive(false) == kResultOk &&
                c.setParamNormalized(latency_id, normalized_latency(1024)) == kResultOk &&
                p.getLatencySamples() == 1024 && p.setActive(true) == kResultOk &&
                p.setProcessing(true) == kResultOk && p.getLatencySamples() == 1024 &&
                impulse_delay() == 1024,
            "a Latency chosen while inactive");
    // A state with another Latency plays it at once too, and the
    // controller's host is asked to restart.
    MemoryStream with_latency;
    require(p.getState(&with_latency) == kResultOk, "save latency");
    with_latency.seek(0, IBStream::kIBSeekSet, nullptr);
    require(read_state(&with_latency).settings->latency == 1024, "latency not saved");
    handler.restarts.clear();
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk && p.settings().latency == latency_samples &&
                p.getLatencySamples() == latency_samples && impulse_delay() == L,
            "an older state kept the Latency, or it did not play at once");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    read_at_restart.clear();
    require(c.setComponentState(&state) == kResultOk &&
                latency_frames(c.getParamNormalized(latency_id)) == latency_samples &&
                handler.latency_restarts() == 1 &&
                read_at_restart == std::vector<uint32>{latency_samples},
            "controller did not ask to restart for the restored Latency");
    // PERF-2 (jrp, 8 November 2026: zero first in the list): a project saved
    // before zero was offered keeps its Latency, since the state holds frames,
    // not the menu position, and hosts cannot automate Latency. Every earlier
    // choice restores to the same frames in the processor and the controller.
    // (One saved at the old default, 256, has no settings and now opens at
    // the new default, as the decision accepted.)
    for (const std::size_t frames : {64, 128, 256, 512, 1024, 2048, 4096}) {
        convolver::HostState earlier;
        earlier.configuration = half.configuration;
        earlier.settings = convolver::HostSettings{0, 1, frames};
        MemoryStream old_project;
        require(write_state(&old_project, earlier), "encode an earlier project");
        old_project.seek(0, IBStream::kIBSeekSet, nullptr);
        require(p.setState(&old_project) == kResultOk && p.settings().latency == frames,
                "an earlier project's Latency changed in the processor");
        old_project.seek(0, IBStream::kIBSeekSet, nullptr);
        require(c.setComponentState(&old_project) == kResultOk &&
                    latency_frames(c.getParamNormalized(latency_id)) == frames &&
                    latency_frames(normalized_latency(frames)) == frames,
                "an earlier project's Latency changed in the controller");
    }
    handler.on_restart = nullptr;
    p.setProcessing(false);
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}
// VST3-16: Latency choices and filter replacements while the audio thread
// plays. Each choice replaces the processor at once, so the control side
// must wait until process() has left the previous one before freeing it
// (the sanitizer build reports a use after free if it does not). Between
// changes the control side waits until the audio thread has finished
// another block, so every change is made while the audio thread is running;
// whether it lands inside a process() call is up to the scheduler, so this
// is a stress test of the handover, not a deterministic interleaving.
void latency_while_playing() {
    HostApplication context;
    Handler handler;
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                c.setComponentHandler(&handler) == kResultOk && p.connect(&c) == kResultOk &&
                c.connect(&p) == kResultOk,
            "playing latency initialize");
    SpeakerArrangement mono = SpeakerArr::kMono;
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "playing latency buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    const auto one_tap = [](float gain) {
        convolver::HostState state;
        state.configuration =
            convolver::Configuration{48000, 1, 1, 0, {}, {}, {{{gain}, {{0, 1}}, {{0, 1}}}}};
        auto stream = std::make_unique<MemoryStream>();
        require(write_state(stream.get(), state), "encode playing latency state");
        return stream;
    };
    const auto half = one_tap(.5f), quarter = one_tap(.25f);
    half->seek(0, IBStream::kIBSeekSet, nullptr);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setState(half.get()) == kResultOk && p.setupProcessing(setup) == kResultOk &&
                p.setActive(true) == kResultOk && p.setProcessing(true) == kResultOk,
            "playing latency activate");
    std::vector<uint32> read_at_restart;
    handler.on_restart = [&](int32 flags) {
        if (flags & kLatencyChanged)
            read_at_restart.push_back(p.getLatencySamples());
    };
    std::array<float, 512> in{}, out{};
    float* ins[] = {in.data()};
    float* outs[] = {out.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 1;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 512;
    in.fill(1);
    std::atomic<bool> stop{false}, failed{false};
    std::atomic<int> blocks{0};
    std::thread audio([&] {
        while (!stop.load()) {
            if (p.process(d) != kResultOk)
                failed.store(true);
            blocks.fetch_add(1);
        }
    });
    const auto after_a_block = [&] {
        const auto seen = blocks.load();
        while (blocks.load() == seen && !failed.load())
            std::this_thread::yield();
    };
    std::size_t chosen = latency_samples,
                playing = latency_samples; // The controller's and the processor's.
    float gain = .5f;                      // The filter that plays.
    std::vector<uint32> expected;
    for (int i = 0; i < 24; ++i) {
        after_a_block();
        if (i % 3 == 2) { // A replacement filter, whose state resets the Latency to the default.
            auto& next = i % 2 ? quarter : half;
            gain = i % 2 ? .25f : .5f;
            next->seek(0, IBStream::kIBSeekSet, nullptr);
            require(p.setState(next.get()) == kResultOk, "playing latency filter");
            playing = latency_samples;
            after_a_block();
        }
        // A choice the controller does not hold and the processor does not
        // play, so every one replaces the processor.
        auto at = static_cast<std::size_t>(i);
        auto frames = convolver::host_latencies[at % convolver::host_latencies.size()];
        while (frames == chosen || frames == playing)
            frames = convolver::host_latencies[++at % convolver::host_latencies.size()];
        chosen = playing = frames;
        expected.push_back(static_cast<uint32>(frames));
        require(c.setParamNormalized(latency_id, normalized_latency(frames)) == kResultOk &&
                    p.getLatencySamples() == frames,
                "a Latency chosen while playing is not the one in use");
    }
    after_a_block();
    stop.store(true);
    audio.join();
    require(!failed.load(), "process() failed while the Latency changed");
    require(read_at_restart == expected, "a Latency read while playing was not the one chosen");
    // The last filter at the last Latency.
    in.fill(0);
    for (int b = 0; b < 10; ++b)
        require(p.process(d) == kResultOk, "playing latency flush");
    in[0] = 1;
    int at = -1;
    float value = 0;
    for (int b = 0; b < 10 && at < 0; ++b) {
        require(p.process(d) == kResultOk, "playing latency impulse");
        in[0] = 0;
        for (int i = 0; i < 512; ++i)
            if (at < 0 && std::abs(out[i]) > 1e-6) {
                at = b * 512 + i;
                value = out[i];
            }
    }
    require(at == static_cast<int>(chosen) && std::abs(value - gain) < 1e-6,
            "the last filter at the last Latency did not play");
    handler.on_restart = nullptr;
    p.setProcessing(false);
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}
// VST3-16: the same handover, step by step. process() is held inside a
// block (holding_for_tests) while another thread chooses a new Latency: the
// new processor is reported at once, the choice does not return while
// process() still holds the old processor (the control side waits:
// releasing_for_tests), and it returns once that block ends.
namespace handover {
std::atomic<bool> inside{false}, leave{false}, waited{false};
void hold_block() {
    inside.store(true);
    while (!leave.load())
        std::this_thread::yield();
}
void note_wait() {
    waited.store(true);
}
} // namespace handover
void latency_handover_waits() {
    HostApplication context;
    Handler handler;
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                c.setComponentHandler(&handler) == kResultOk && p.connect(&c) == kResultOk &&
                c.connect(&p) == kResultOk,
            "handover initialize");
    SpeakerArrangement mono = SpeakerArr::kMono;
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "handover buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk &&
                p.setProcessing(true) == kResultOk,
            "handover activate");
    std::array<float, 512> in{}, out{};
    float* ins[] = {in.data()};
    float* outs[] = {out.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 1;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    d.numSamples = 512;
    holding_for_tests = handover::hold_block;
    releasing_for_tests = handover::note_wait;
    std::atomic<tresult> played{kResultFalse};
    std::thread audio([&] { played.store(p.process(d)); });
    while (!handover::inside.load())
        std::this_thread::yield();
    holding_for_tests = nullptr; // process() is held inside its block.
    std::atomic<bool> chosen{false};
    std::thread control([&] {
        c.setParamNormalized(latency_id, normalized_latency(1024));
        chosen.store(true);
    });
    // The control side waits for the held block, or (broken) returns.
    while (!handover::waited.load() && !chosen.load())
        std::this_thread::yield();
    const bool waited = handover::waited.load() && !chosen.load();
    const auto reported = p.getLatencySamples();
    handover::leave.store(true);
    control.join();
    audio.join();
    releasing_for_tests = nullptr;
    require(waited, "a replaced processor was freed while process() held it");
    require(reported == 1024, "the new Latency was not reported while the old block finished");
    require(played.load() == kResultOk && chosen.load(), "the held block or the choice failed");
    require(p.process(d) == kResultOk && p.getLatencySamples() == 1024,
            "the new processor does not play");
    p.setProcessing(false);
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}
// A 16-bit WAV with an impulse of one half on every channel.
void write_wav(const std::filesystem::path& path, int rate, int channels, int frames) {
    std::ofstream out(path, std::ios::binary);
    const auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out.put(static_cast<char>((value >> (8 * i)) & 0xff));
    };
    const auto data = static_cast<std::uint32_t>(frames * channels * 2);
    out.write("RIFF", 4);
    put(36 + data, 4);
    out.write("WAVEfmt ", 8);
    put(16, 4);
    put(1, 2); // PCM
    put(static_cast<std::uint32_t>(channels), 2);
    put(static_cast<std::uint32_t>(rate), 4);
    put(static_cast<std::uint32_t>(rate * channels * 2), 4);
    put(static_cast<std::uint32_t>(channels * 2), 2);
    put(16, 2);
    out.write("data", 4);
    put(data, 4);
    for (int f = 0; f < frames; ++f)
        for (int c = 0; c < channels; ++c)
            put(f == 0 ? 16384 : 0, 2);
    if (!out)
        throw std::runtime_error("could not write " + path.string());
}
// VST3-3: the processor sends the window's report with the status, the
// controller keeps it, and a filter file chosen in the window loads through
// a message as a state restore would, marking the project changed.
void report_and_files() {
    const auto field = [](const std::vector<std::pair<std::string, std::string>>& report,
                          const char* name) {
        for (const auto& [key, value] : report)
            if (key == name)
                return value;
        return std::string("(missing)");
    };
    HostApplication context;
    Handler handler;
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                c.setComponentHandler(&handler) == kResultOk,
            "report initialize");
    require(c.report().empty() && c.diagnostics().empty(), "a report before connecting");
    require(p.connect(&c) == kResultOk && c.connect(&p) == kResultOk, "report connect");
    auto report = c.report();
    require(field(report, "status name") == "No filter" &&
                field(report, "title").find("VST3 #") != std::string::npos &&
                field(report, "filter name") == "None",
            "report of an empty instance");
    require(c.diagnostics().find("Recent Convolver log lines for VST3 #") != std::string::npos,
            "diagnostics text");
    SpeakerArrangement mono = SpeakerArr::kMono;
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "report buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "report activate");
    report = c.report();
    require(field(report, "sample rate") == "48000", "report rate after activation");
    // A window reads twice a second; the host is asked to reread the
    // parameters only when the Status line changes.
    handler.restarts.clear();
    c.report();
    c.report();
    require(std::none_of(handler.restarts.begin(), handler.restarts.end(),
                         [](int32 f) { return (f & kParamValuesChanged) != 0; }),
            "an unchanged report asked the host to reread the parameters");
    // A Gain the host changed reaches the report when the window asks again.
    ParameterChanges gain;
    int32 index = 0;
    gain.addParameterData(gain_id, index)->addPoint(0, normalized_gain(-12), index);
    ProcessData flush{};
    flush.inputParameterChanges = &gain;
    require(p.process(flush) == kResultOk, "report gain");
    require(c.report() != report, "report not refreshed on request");

    const auto folder = private_temp("convolver-vst3-files");
    std::filesystem::create_directories(folder);
    const auto good = folder / "room.wav", wrong = folder / "other rate.wav";
    write_wav(good, 48000, 1, 8);
    write_wav(wrong, 44100, 1, 8);
    // Accepted: the filter plays, the report names the file, the host is told
    // the project changed, and the saved state holds the filter.
    require(c.load_file(good.string()) && handler.dirty == 1, "file not loaded");
    report = c.report();
    require(field(report, "status name") == "Active" && field(report, "filter name") == "room.wav",
            "report after loading a file");
    auto f = p.failure();
    require(!f, "a loaded file left a failure");
    MemoryStream saved;
    require(p.getState(&saved) == kResultOk, "save after loading a file");
    saved.seek(0, IBStream::kIBSeekSet, nullptr);
    const auto state = read_state(&saved);
    require(state.configuration && state.configuration->sample_rate == 48000 &&
                std::abs(state.configuration->paths.at(0).impulse.at(0) - .5f) < 1e-4,
            "saved state does not hold the file's filter");
    // Refused: the filter stays, the report says why, the project is not
    // marked changed.
    require(!c.load_file(wrong.string()) && handler.dirty == 1, "a wrong-rate file accepted");
    f = p.failure();
    require(f && f->request.source == convolver::HostFilterSource::file &&
                f->request.path == wrong.string(),
            "the refused file is not the failure's request");
    report = c.report();
    require(field(report, "filter name") == "room.wav" &&
                field(report, "filter note") != "(missing)" &&
                !field(report, "filter note").empty(),
            "report after a refused file");
    require(!c.load_file(""), "an empty path accepted");
    std::filesystem::remove_all(folder);
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}
// PLUG-22: the controller state keeps the chosen file's name, which the
// window shows again after the project is reopened, but only while the
// filter it belongs to plays; and Bypass from the controller reads as
// Bypassed at once.
void saved_filter_name() {
    // The format: anything not ours reads as no name.
    const ControllerState kept{"room (left).wav", "0123456789abcdef"};
    const auto encoded = encode_controller_state(kept);
    const auto back = decode_controller_state(encoded);
    require(back.name == kept.name && back.identity == kept.identity,
            "controller state round trip");
    for (const auto& bad :
         {std::string(), encoded.substr(0, encoded.size() - 1), std::string("CVCS"),
          std::string("CVCX") + encoded.substr(4), encode_controller_state({"", ""}).substr(0, 9),
          encode_controller_state({"room.wav", ""}), encode_controller_state({"", "1"})}) {
        const auto none = decode_controller_state(bad);
        require(none.name.empty() && none.identity.empty(), "damaged controller state read");
    }
    auto newer = encoded;
    newer[4] = 2;
    require(decode_controller_state(newer).name.empty(), "a newer controller state read");
    auto long_name =
        encode_controller_state({std::string(convolver::max_filter_name_bytes + 1, 'a'), "1"});
    require(decode_controller_state(long_name).name.empty(), "an over-long name read");

    const auto field = [](const std::vector<std::pair<std::string, std::string>>& report,
                          const char* name) {
        for (const auto& [key, value] : report)
            if (key == name)
                return value;
        return std::string("(missing)");
    };
    struct Instance {
        HostApplication context;
        Handler handler;
        Processor p;
        Controller c;
        explicit Instance(double rate = 48000) {
            require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                        c.setComponentHandler(&handler) == kResultOk &&
                        p.connect(&c) == kResultOk && c.connect(&p) == kResultOk,
                    "name instance");
            SpeakerArrangement mono = SpeakerArr::kMono;
            ProcessSetup setup{kRealtime, kSample32, 512, rate};
            require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk &&
                        p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
                    "name activate");
        }
        ~Instance() {
            p.setActive(false);
            p.disconnect(&c);
            c.disconnect(&p);
            c.terminate();
            p.terminate();
        }
    };
    // Before PLUG-22 the controller state was empty; it still is with no file.
    {
        Instance fresh;
        MemoryStream empty;
        require(fresh.c.getState(&empty) == kResultOk && empty.getSize() == 0,
                "a controller state without a file");
        empty.seek(0, IBStream::kIBSeekSet, nullptr);
        require(fresh.c.setState(&empty) == kResultOk && fresh.c.setState(nullptr) == kResultOk,
                "an empty controller state refused");
    }
    const auto folder = private_temp("convolver-vst3-name");
    std::filesystem::create_directories(folder);
    const auto good = folder / "room.wav", other = folder / "hall.wav";
    write_wav(good, 48000, 1, 8);
    write_wav(other, 48000, 1, 16);
    MemoryStream component, controller, hall;
    std::string identity;
    {
        Instance first;
        require(first.c.load_file(good.string()), "name file not loaded");
        const auto report = first.c.report();
        identity = field(report, "filter identity");
        require(identity.size() == 16, "no filter identity");
        // Bypass from the host or the window: Bypassed before audio runs.
        require(first.c.setParamNormalized(bypass_id, 1) == kResultOk &&
                    field(first.c.report(), "status name") == "Bypassed",
                "bypass not shown at once");
        require(first.c.setParamNormalized(bypass_id, 0) == kResultOk &&
                    field(first.c.report(), "status name") == "Active",
                "bypass off not shown at once");
        require(first.p.getState(&component) == kResultOk &&
                    first.c.getState(&controller) == kResultOk,
                "save with a file");
        controller.seek(0, IBStream::kIBSeekSet, nullptr);
        std::string bytes(static_cast<std::size_t>(controller.getSize()), '\0');
        int32 read = 0;
        controller.read(bytes.data(), static_cast<int32>(bytes.size()), &read);
        const auto saved = decode_controller_state(bytes);
        require(saved.name == "room.wav" && saved.identity == identity,
                "controller state does not hold the name");
        // Another filter's state, for the mismatch below.
        require(first.c.load_file(other.string()) && first.p.getState(&hall) == kResultOk,
                "second file");
        require(field(first.c.report(), "filter identity") != identity,
                "two filters with one identity");
    }
    {
        // Reopened as hosts do: component state, then the controller's.
        Instance reopened;
        component.seek(0, IBStream::kIBSeekSet, nullptr);
        controller.seek(0, IBStream::kIBSeekSet, nullptr);
        require(reopened.p.setState(&component) == kResultOk, "reopen component");
        component.seek(0, IBStream::kIBSeekSet, nullptr);
        require(reopened.c.setComponentState(&component) == kResultOk &&
                    reopened.c.setState(&controller) == kResultOk,
                "reopen controller");
        auto report = reopened.c.report();
        require(field(report, "filter name") == "room.wav" && field(report, "filter path").empty(),
                "name not shown after reopening");
        require(reopened.c.diagnostics().find("\nFilter: room.wav") != std::string::npos,
                "diagnostics do not name the file after reopening");
        // Saved again without choosing a file: the name is kept.
        MemoryStream again;
        require(reopened.c.getState(&again) == kResultOk && again.getSize() == controller.getSize(),
                "name lost when saved again");
        // A preset of another filter: the name no longer applies.
        hall.seek(0, IBStream::kIBSeekSet, nullptr);
        require(reopened.p.setState(&hall) == kResultOk, "other preset");
        report = reopened.c.report();
        require(field(report, "filter name") == "Restored filter",
                "a saved name shown for another filter");
    }
    // A refused preset (jrp's v0.12.8 REAPER check): the host sends its
    // controller state too, which names a filter that never played. The
    // playing filter keeps its name in the window and in the saved state,
    // whichever order the host sends the two states in.
    const auto slow = folder / "slow.wav";
    write_wav(slow, 44100, 1, 8);
    MemoryStream slow_component, slow_controller, hall_controller;
    {
        Instance at44(44100);
        require(at44.c.load_file(slow.string()) && at44.p.getState(&slow_component) == kResultOk &&
                    at44.c.getState(&slow_controller) == kResultOk,
                "44.1 kHz preset");
        require(at44.c.load_file(other.string()) == false, "a 48 kHz file loaded at 44.1 kHz");
    }
    {
        Instance hall48;
        require(hall48.c.load_file(other.string()) &&
                    hall48.c.getState(&hall_controller) == kResultOk,
                "hall preset's controller state");
    }
    const auto saved_name = [](Controller& c) {
        MemoryStream out;
        require(c.getState(&out) == kResultOk, "save the controller state");
        out.seek(0, IBStream::kIBSeekSet, nullptr);
        std::string bytes(static_cast<std::size_t>(out.getSize()), '\0');
        int32 read = 0;
        out.read(bytes.data(), static_cast<int32>(bytes.size()), &read);
        return decode_controller_state(bytes).name;
    };
    for (const bool controller_first : {false, true}) {
        Instance playing;
        component.seek(0, IBStream::kIBSeekSet, nullptr);
        controller.seek(0, IBStream::kIBSeekSet, nullptr);
        require(playing.p.setState(&component) == kResultOk &&
                    playing.c.setState(&controller) == kResultOk,
                "restore room.wav");
        require(field(playing.c.report(), "filter name") == "room.wav", "room.wav not restored");
        play_block(playing.p, 1, 1, 512); // Playing, so a preset for 44.1 kHz is refused.
        slow_component.seek(0, IBStream::kIBSeekSet, nullptr);
        slow_controller.seek(0, IBStream::kIBSeekSet, nullptr);
        if (controller_first) {
            playing.c.setState(&slow_controller);
            playing.p.setState(&slow_component);
        } else {
            playing.p.setState(&slow_component);
            playing.c.setState(&slow_controller);
        }
        auto report = playing.c.report();
        require(field(report, "filter identity") == identity,
                "the refused preset replaced the playing filter");
        require(field(report, "filter name") == "room.wav",
                "a refused preset renamed the playing filter");
        require(saved_name(playing.c) == "room.wav",
                "a refused preset's name saved for the playing filter");
        // A preset that loads afterwards still takes its own name, in
        // either order.
        hall.seek(0, IBStream::kIBSeekSet, nullptr);
        hall_controller.seek(0, IBStream::kIBSeekSet, nullptr);
        if (controller_first) {
            require(playing.c.setState(&hall_controller) == kResultOk &&
                        playing.p.setState(&hall) == kResultOk,
                    "hall after the refusal");
        } else {
            require(playing.p.setState(&hall) == kResultOk &&
                        playing.c.setState(&hall_controller) == kResultOk,
                    "hall after the refusal");
        }
        require(field(playing.c.report(), "filter name") == "hall.wav" &&
                    saved_name(playing.c) == "hall.wav",
                "a preset after a refusal lost its name");
    }
    std::filesystem::remove_all(folder);
}
// PLUG-31: a preset made by convolver-preset, loaded as the SDK's
// PresetFile::loadPreset does for a host (the component state, then the
// controller's), names its filter file in the window. An empty `expected`:
// a preset without a name (convolver-vst3-preset's), which must not gain one.
void preset_filter_name(const std::filesystem::path& preset_path, const std::string& expected) {
    HostApplication context;
    Handler handler;
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                c.setComponentHandler(&handler) == kResultOk && p.connect(&c) == kResultOk &&
                c.connect(&p) == kResultOk,
            "preset name instance");
    SpeakerArrangement mono = SpeakerArr::kMono;
    ProcessSetup setup{kRealtime, kSample32, 512, 48000};
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk &&
                p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "preset name activate");
    std::ifstream file(preset_path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
    MemoryStream preset(bytes.data(), static_cast<TSize>(bytes.size()));
    require(PresetFile::loadPreset(&preset, processor_id, &p, &c), "SDK preset loading");
    std::string shown = "(missing)", identity;
    for (const auto& [key, value] : c.report()) {
        if (key == "filter name")
            shown = value;
        if (key == "filter identity")
            identity = value;
    }
    require(!identity.empty(), "a generated preset loaded no filter");
    if (!expected.empty() && shown != expected)
        throw std::runtime_error("a generated preset shows '" + shown + "', not '" + expected +
                                 "'");
    // Saved again by the host: the name stays with the filter.
    MemoryStream again;
    require(c.getState(&again) == kResultOk, "save after a generated preset");
    std::string saved(static_cast<std::size_t>(again.getSize()), '\0');
    again.seek(0, IBStream::kIBSeekSet, nullptr);
    int32 read = 0;
    again.read(saved.data(), static_cast<int32>(saved.size()), &read);
    const auto kept = decode_controller_state(saved);
    if (expected.empty())
        require(saved.empty(), "a preset without a name saved with one");
    else
        require(kept.name == expected && kept.identity == identity, "name lost when saved again");
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
    std::cout << "A generated preset shows: " << shown << '\n';
}
// VST3-15: what a host may put between the components
// (IConnectionPoint): a proxy that passes each message on at once and
// returns the receiver's answer, as the SDK's ConnectionProxy does, or one
// that keeps the messages, answers kResultOk itself and delivers them later,
// as a host that runs the two components apart might.
class Proxy final : public IConnectionPoint {
  public:
    Proxy(IConnectionPoint& to, bool defer) : to_(to), defer_(defer) {}
    tresult PLUGIN_API connect(IConnectionPoint*) override {
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override {
        return kResultOk;
    }
    tresult PLUGIN_API notify(IMessage* message) override {
        if (!defer_)
            return to_.notify(message);
        kept_.emplace_back(message); // Holds a reference until delivered.
        return kResultOk;
    }
    // Delivers the oldest message kept, if any.
    bool deliver_one() {
        if (kept_.empty())
            return false;
        const auto message = kept_.front();
        kept_.pop_front();
        to_.notify(message);
        return true;
    }
    // Delivers the messages kept so far, in order. Returns how many.
    std::size_t deliver() {
        std::size_t n = 0;
        while (!kept_.empty()) {
            const auto message = kept_.front();
            kept_.pop_front();
            to_.notify(message);
            ++n;
        }
        return n;
    }
    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override {
        QUERY_INTERFACE(iid, obj, FUnknown::iid, IConnectionPoint)
        QUERY_INTERFACE(iid, obj, IConnectionPoint::iid, IConnectionPoint)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return 1;
    }
    uint32 PLUGIN_API release() override {
        return 1;
    }

  private:
    IConnectionPoint& to_;
    const bool defer_;
    std::deque<IPtr<IMessage>> kept_;
};
// The report, a chosen file and a new Latency through each proxy: whether a
// file was accepted, the project marked changed and the host asked to
// restart must not depend on the message arriving during sendMessage().
void messages_through_proxies() {
    const auto field = [](const std::vector<std::pair<std::string, std::string>>& report,
                          const char* name) {
        for (const auto& [key, value] : report)
            if (key == name)
                return value;
        return std::string("(missing)");
    };
    const auto folder = private_temp("convolver-vst3-proxies");
    std::filesystem::create_directories(folder);
    const auto good = folder / "room.wav", wrong = folder / "other rate.wav";
    write_wav(good, 48000, 1, 8);
    write_wav(wrong, 44100, 1, 8);
    for (const bool defer : {false, true}) {
        const std::string how = defer ? " (deferring proxy)" : " (forwarding proxy)";
        const auto check = [&](bool v, const char* what) {
            if (!v)
                throw std::runtime_error(what + how);
        };
        HostApplication context;
        Handler handler;
        Processor p;
        Controller c;
        check(p.initialize(&context) == kResultOk && c.initialize(&context) == kResultOk &&
                  c.setComponentHandler(&handler) == kResultOk,
              "proxy initialize");
        Proxy to_controller(c, defer), to_processor(p, defer);
        check(p.connect(&to_controller) == kResultOk && c.connect(&to_processor) == kResultOk,
              "proxy connect");
        // What the host's message loop does later; nothing for a forwarding proxy.
        const auto deliver = [&] {
            while (to_controller.deliver() + to_processor.deliver() > 0) {
            }
        };
        // The window asks twice a second, so a late answer shows at the next ask.
        const auto report = [&] {
            c.report();
            deliver();
            return c.report();
        };
        deliver();
        check(field(report(), "status name") == "No filter", "proxy report");
        SpeakerArrangement mono = SpeakerArr::kMono;
        check(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "proxy buses");
        p.activateBus(kAudio, kInput, 0, true);
        p.activateBus(kAudio, kOutput, 0, true);
        ProcessSetup setup{kRealtime, kSample32, 512, 48000};
        check(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
              "proxy activate");
        check(field(report(), "sample rate") == "48000", "proxy report after activation");
        // A new Latency: the host reads it when asked to restart, so it must
        // be asked only once the processor has it.
        std::vector<std::size_t> latency_at_restart;
        handler.on_restart = [&](int32 flags) {
            if (flags & kLatencyChanged)
                latency_at_restart.push_back(p.settings().latency);
        };
        check(c.setParamNormalized(latency_id, normalized_latency(1024)) == kResultOk,
              "proxy Latency");
        deliver();
        check(latency_at_restart == std::vector<std::size_t>{1024},
              "host asked to restart before the processor had the new Latency");
        if (defer) {
            // Changed three times before the processor hears: only the
            // last choice's answer asks for the restart, so the host never
            // restarts at a Latency that is about to change.
            latency_at_restart.clear();
            check(c.setParamNormalized(latency_id, normalized_latency(512)) == kResultOk &&
                      c.setParamNormalized(latency_id, normalized_latency(2048)) == kResultOk &&
                      c.setParamNormalized(latency_id, normalized_latency(512)) == kResultOk,
                  "proxy Latency three times");
            to_processor.deliver_one(); // The first 512 arrives; its answer comes back.
            to_controller.deliver();
            check(latency_at_restart.empty() && p.settings().latency == 512,
                  "host asked to restart while a later Latency was on its way");
            deliver();
            check(latency_at_restart == std::vector<std::size_t>{512},
                  "host not asked to restart for the last Latency");
        }
        handler.on_restart = nullptr;
        // Accepted: the project is marked changed once, when the answer arrives.
        check(c.load_file(good.string()), "proxy file not loaded");
        deliver();
        auto shown = report();
        check(handler.dirty == 1 && field(shown, "filter name") == "room.wav",
              "proxy file not playing, or the project not marked changed");
        // Refused: never marked changed. A forwarding proxy brings the
        // refusal at once; a deferring one later, in the report.
        const bool refused_result = c.load_file(wrong.string());
        deliver();
        shown = report();
        check(defer || !refused_result, "a refused file reported as accepted");
        check(handler.dirty == 1, "a refused file marked the project changed");
        check(field(shown, "filter name") == "room.wav" && !field(shown, "filter note").empty(),
              "refused file not explained in the report");
        p.setActive(false);
        p.disconnect(&to_controller);
        c.disconnect(&to_processor);
        deliver();
        c.terminate();
        p.terminate();
    }
    std::filesystem::remove_all(folder);
}
// PLUG-13 (jrp's review of 276c7dc): a filter that fills the saved state at
// default settings loaded and saved, and after Gain changed to -6 dB
// getState() failed. Load, change a setting, save and restore, at the limit.
void settings_at_state_limit() {
    // One mono path takes 17 words besides its samples, and room is kept
    // free for the settings.
    constexpr std::size_t at_limit =
        (convolver::max_host_state_bytes - convolver::host_settings_room_bytes) / 4 - 17;
    const auto state = [](std::size_t samples) {
        convolver::Configuration c{48000, 1, 1, 0, {}, {}, {}};
        c.paths.push_back({std::vector<float>(samples, 0.f), {{0, 1}}, {{0, 1}}});
        c.paths[0].impulse[0] = 1;
        auto stream = std::make_unique<MemoryStream>();
        require(write_state(stream.get(), convolver::HostState{std::move(c)}),
                "encode a filter near the limit");
        stream->seek(0, IBStream::kIBSeekSet, nullptr);
        return stream;
    };
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "limit initialize");
    SpeakerArrangement mono = SpeakerArr::kMono;
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "limit buses");
    require(p.setState(state(at_limit + 1).get()) == kResultFalse,
            "a filter with no room for the settings was accepted");
    require(p.setState(state(at_limit).get()) == kResultOk, "the filter at the limit was refused");
    // The host changes Gain to -6 dB, as in the review.
    ParameterChanges gain;
    int32 index = 0;
    gain.addParameterData(gain_id, index)->addPoint(0, normalized_gain(-6), index);
    ProcessData flush{};
    flush.inputParameterChanges = &gain;
    require(p.process(flush) == kResultOk && p.settings().gain_db != 0, "limit gain");
    MemoryStream saved;
    require(p.getState(&saved) == kResultOk, "the filter at the limit did not save with Gain");
    saved.seek(0, IBStream::kIBSeekSet, nullptr);
    Processor reopened;
    require(reopened.initialize(&context) == kResultOk &&
                reopened.setBusArrangements(&mono, 1, &mono, 1) == kResultOk,
            "limit reopen");
    require(reopened.setState(&saved) == kResultOk &&
                std::abs(reopened.settings().gain_db - p.settings().gain_db) < 1e-9,
            "the saved filter and Gain were not restored");
    reopened.terminate();
    p.terminate();
}
void controller_conversions() {
    HostApplication context;
    Controller c;
    require(c.initialize(&context) == kResultOk, "controller initialize");
    for (ParamValue v : {0.0, 1.0}) {
        String128 text{};
        require(c.getParamStringByValue(bypass_id, v, text) == kResultOk, "bypass text");
        ParamValue back = -1;
        require(c.getParamValueByString(bypass_id, text, back) == kResultOk && back == v,
                "bypass text does not convert back");
    }
    String128 upper{'O', 'N', 0}, number{'0', 0}, other{'m', 'a', 'y', 'b', 'e', 0};
    ParamValue value = -1;
    require(c.getParamValueByString(bypass_id, upper, value) == kResultOk && value == 1,
            "ON not accepted");
    require(c.getParamValueByString(bypass_id, number, value) == kResultOk && value == 0,
            "0 not accepted");
    String128 one{'1', 0}, fraction{'0', '.', '5', 0};
    require(c.getParamValueByString(bypass_id, one, value) == kResultOk && value == 1,
            "1 not accepted");
    // The SDK's own conversion reads any text as 0 on macOS.
    require(c.getParamValueByString(bypass_id, fraction, value) != kResultOk,
            "a fraction accepted");
    require(c.getParamValueByString(bypass_id, other, value) != kResultOk,
            "unrelated text accepted");
    require(c.getParamValueByString(bypass_id, nullptr, value) == kInvalidArgument,
            "no text accepted");
    MemoryStream state;
    require(c.getState(&state) == kResultOk, "controller getState");
    int64 size = -1;
    state.tell(&size);
    require(size == 0, "controller wrote state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(c.setState(&state) == kResultOk, "controller setState");
    c.terminate();
}
// The identity filter, or the preset given on the command line, through
// 32-bit and 64-bit processing and a state round trip.
void processing(const std::filesystem::path& preset_path) {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "initialize");
    SpeakerArrangement mono = SpeakerArr::kMono, stereo = SpeakerArr::kStereo;
    require(p.setBusArrangements(&mono, 1, &stereo, 1) == kResultOk, "mono to stereo buses");
    require(p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk, "mono buses");
    if (!preset_path.empty()) {
        std::ifstream file(preset_path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        MemoryStream preset(bytes.data(), static_cast<TSize>(bytes.size()));
        require(PresetFile::loadPreset(&preset, processor_id, &p), "SDK preset loading");
    }
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    auto expected = [&](int sample) {
        return preset_path.empty() ? (sample == L ? 1. : 0.)
                                   : (sample == L       ? .5
                                      : sample == L + 1 ? .25
                                                        : 0.);
    };
    ProcessSetup setup{kRealtime, kSample32, 73, 48000};
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk, "activate");
    require(p.setProcessing(true) == kResultOk && p.getLatencySamples() == latency_samples,
            "start latency");
    std::array<float, 73> samples{};
    float* channels[] = {samples.data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 1;
    input.channelBuffers32 = output.channelBuffers32 = channels;
    ProcessData d{};
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input;
    d.outputs = &output;
    for (int k = 0; k < 8; ++k) {
        samples.fill(0);
        if (k == 0)
            samples[0] = 1;
        d.numSamples = 73;
        require(p.process(d) == kResultOk, "process");
        for (int i = 0; i < 73; ++i)
            require(std::abs(samples[i] - expected(k * 73 + i)) < 1e-5, "adapter identity latency");
    }
    MemoryStream state;
    require(p.getState(&state) == kResultOk, "save state");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk, "restore while active");
    ProcessData flush{};
    require(p.process(flush) == kResultOk, "parameter-only flush");
    require(p.setProcessing(false) == kResultOk && p.setActive(false) == kResultOk, "stop");
    setup.symbolicSampleSize = kSample64;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "64-bit setup");
    require(p.canProcessSampleSize(kSample64) == kResultOk, "64-bit negotiation");
    p.setProcessing(true);
    std::array<double, 73> doubles{};
    double* ptr[] = {doubles.data()};
    input.channelBuffers64 = output.channelBuffers64 = ptr;
    d.symbolicSampleSize = kSample64;
    for (int k = 0; k < 8; ++k) {
        doubles.fill(0);
        if (k == 0)
            doubles[0] = 1;
        require(p.process(d) == kResultOk, "double process");
        for (int i = 0; i < 73; ++i)
            require(std::abs(doubles[i] - expected(k * 73 + i)) < 1e-5, "double identity");
    }
    p.setProcessing(false);
    p.setActive(false);
    p.terminate();
}
int run(const std::filesystem::path& preset_path, const std::optional<std::string>& preset_name) {
    std::vector<convolver::test::Case> list{
        {"edge_cases", edge_cases},
        {"fractional_rates", fractional_rates},
        {"controller_conversions", controller_conversions},
        {"settings_parameters", settings_parameters},
        {"latency_while_playing", latency_while_playing},
        {"latency_handover_waits", latency_handover_waits},
        {"report_and_files", report_and_files},
        {"saved_filter_name", saved_filter_name},
        {"messages_through_proxies", messages_through_proxies},
        {"settings_at_state_limit", settings_at_state_limit},
        {"tail_measured_at_output", tail_measured_at_output},
        {"output_silence_flags", output_silence_flags},
        {"output_silence_flags_64", output_silence_flags_64},
        {"multichannel", multichannel},
        {"restored_while_host_settles", restored_while_host_settles},
        {"version1_presets", version1_presets},
        {"speaker_arrangements", speaker_arrangements},
        {"placeholder_setup", placeholder_setup},
        {"restart_mid_transition", restart_mid_transition},
        {"failed_first_loads", failed_first_loads},
        {"failure_records", failure_records},
        {"status_and_log", status_and_log},
    };
    if (preset_name)
        list.insert(list.begin(),
                    {"preset_filter_name", [&] { preset_filter_name(preset_path, *preset_name); }});
    list.push_back({"processing", [&] { processing(preset_path); }});
    if (convolver::test::run_cases(list))
        return 1;
    std::cout << "VST3 adapter compatibility passed\n";
    return 0;
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    // CI-28: the log CTest names in the build folder, set once before any
    // instance exists and never removed, so it is evidence after a crash.
    process_log = convolver::test_log::use_test_log("convolver-adapter-tests.log");
    // A preset to load, and the filter name it should show (PLUG-31; empty
    // for none).
    std::optional<std::string> preset_name;
    if (argc == 3) {
        const auto name = std::filesystem::path(argv[2]).u8string();
        preset_name.emplace(name.begin(), name.end());
    }
    return run(argc >= 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{}, preset_name);
}
