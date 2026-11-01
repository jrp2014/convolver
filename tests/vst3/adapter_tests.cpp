// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_log.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "sound_files.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::vst3;
void require(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
// The default Latency, the delay these tests expect unless they choose another.
constexpr uint32 latency_samples = convolver::default_host_latency;
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
    // The tail is measured at the output, so it includes the 256-sample latency.
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
    for (int i = 0; i < 512; ++i)
        require(std::abs(out_right[i] - (i == 157 || i == 257 ? 1.f : 0.f)) < 1e-5 &&
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
            float want = i == 256 ? expected[c][0] : i == 257 ? expected[c][1] : 0.f;
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
            require(std::abs(wide[c][i] - (i == 256 ? c + 1. : 0.)) < 1e-5, "8x8 identity");
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
                    const float want = i == 256 + c % 2 ? static_cast<float>(c + 1) : 0.f;
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
    const auto notice = p.notice();
    require(notice.find("output 7 is filtered for the side left (SL) speaker but plays on the "
                        "front left-centre (FLC)") != std::string::npos &&
                notice.find("output 8 is filtered for the side right (SR)") != std::string::npos,
            "7.1 filter on 7.1 Cine gave no notice");
    require(p.setActive(false) == kResultOk &&
                p.setBusArrangements(&music, 1, &music, 1) == kResultOk &&
                p.setActive(true) == kResultOk && p.notice().empty(),
            "7.1 Music buses kept the notice");
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
        require(out_left[i] == 0 && std::abs(out_right[i] - (i == 256 ? .5f : 0.f)) < 1e-6,
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
            require(std::abs(out_left[i] - (i < 256 ? 0.f : 3.f)) < 1e-5 && out_right[i] == 0,
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
                f->sample_rate == 44100 && f->message.find("44100 Hz") != std::string::npos,
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
                f->message.find("newer version of Convolver") != std::string::npos,
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
                f->message.find("does not suit the host") != std::string::npos,
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
// CONVOLVER_LOG, read and set wide on Windows, as host_log_path() reads it,
// since temporary folders may hold characters outside the ANSI code page.
std::optional<std::filesystem::path> log_variable() {
#ifdef _WIN32
    if (const wchar_t* value = _wgetenv(L"CONVOLVER_LOG"))
        return std::filesystem::path(value);
#else
    if (const char* value = std::getenv("CONVOLVER_LOG"))
        return std::filesystem::path(value);
#endif
    return std::nullopt;
}
void set_log_variable(const std::optional<std::filesystem::path>& value) {
#ifdef _WIN32
    _wputenv_s(L"CONVOLVER_LOG", value ? value->wstring().c_str() : L""); // Empty removes it.
#else
    if (value)
        setenv("CONVOLVER_LOG", value->c_str(), 1);
    else
        unsetenv("CONVOLVER_LOG");
#endif
}
// A file name of this process's own, so that concurrent runs never share it.
std::filesystem::path private_temp(const char* name) {
    return std::filesystem::temp_directory_path() /
           (std::string(name) + "-" + std::to_string(std::random_device{}()));
}
// DIAG-1: the Status parameter shows each failure in one line, and the
// Convolver log holds the full explanation.
void status_and_log() {
    const auto contains = [](const std::string& text, const char* part) {
        return text.find(part) != std::string::npos;
    };
    const auto log_path = private_temp("convolver-vst3-status") += ".log";
    const auto earlier = log_variable(); // Restored below.
    set_log_variable(log_path);
    const auto log_text = [&] {
        std::ifstream in(log_path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), {});
    };
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
                !(info.flags & ParameterInfo::kCanAutomate) && info.stepCount == 4,
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
    require(c.getParamStringByValue(status_id, 0, name) == kResultOk && name[0] == 'P' &&
                name[1] == 'l',
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
    ProcessSetup odd{kRealtime, kSample32, 64, 44100.5};
    require(p.setupProcessing(odd) == kInvalidArgument &&
                contains(log_text(), "Refused the host's processing settings (44100.5 Hz, "
                                     "blocks of up to 64 samples): the sample rate must be a "
                                     "whole number"),
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
    require(contains(log_text(), "Filter not loaded, so Convolver plays silence. The filter was "
                                 "made for 44100 Hz, but the host runs at 48000 Hz. Convolver "
                                 "does not resample"),
            "failed first load not logged");
    require(p.setState(stream(good).get()) == kResultOk &&
                shown() == "Filter loaded: 48000 Hz, 2 in, 2 out." &&
                status() == convolver::HostStatus::playing &&
                contains(log_text(), ": Loaded a filter for 48000 Hz, 2 in, 2 out.\n"),
            "good load not shown or logged");
    // Wrong preset while a filter plays: the filter keeps playing, and both say so.
    require(
        p.setState(stream(wrong).get()) == kResultFalse &&
            contains(shown(), "Load failed, previous filter kept: the filter is for 44100 Hz") &&
            contains(log_text(), "Filter not loaded; the previous filter is kept."),
        "refused replacement not shown or logged");
    MemoryStream damaged;
    const std::array<unsigned char, 16> junk{12, 0, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    damaged.write(const_cast<unsigned char*>(junk.data()), 16, nullptr);
    damaged.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&damaged) == kResultFalse &&
                contains(shown(), "the preset or saved state could not be read") &&
                contains(log_text(), "The saved state could not be read"),
            "damaged state not shown or logged");
    // DIAG-1, jrp's reproduction: extreme but finite input and filter values
    // overflow the output. The audio thread writes Fault to the Status
    // parameter; the host passes it to the controller, which asks the
    // processor to explain; the log says what happened and what to change.
    auto loud = good;
    loud.configuration->paths[0].impulse[0] = std::numeric_limits<float>::max() / 4;
    require(p.setState(stream(loud).get()) == kResultOk &&
                status() == convolver::HostStatus::playing,
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
        // The overflow reaches the output after the 256-frame latency.
        for (int block = 0; block < 6; ++block) {
            left.fill(0);
            left[0] = std::numeric_limits<float>::max() / 4;
            require(p.process(d) == kResultOk, "overflowing block refused");
            for (int i = 0; i < 64; ++i)
                require(std::isfinite(left[i]) && std::isfinite(right[i]), "overflow not silenced");
        }
        // One Fault point, written once however many blocks overflow.
        require(changes.getParameterCount() == 1, "Fault not written to the Status parameter");
        auto* queue = changes.getParameterData(0);
        int32 offset = -1;
        ParamValue value = 0;
        require(queue && queue->getParameterId() == status_id && queue->getPointCount() == 1 &&
                    queue->getPoint(0, offset, value) == kResultOk && value == 1.0,
                "Fault point");
        require(contains(log_text(), "too large") == false, "fault logged on the audio thread");
        // The host hands the value to the controller, which reads it as the
        // name until the processor has explained.
        require(c.setParamNormalized(status_id, value) == kResultOk, "host passes Fault on");
    }
    require(status() == convolver::HostStatus::fault &&
                shown() == "Fault: output too large, silenced and the filter restarted. Details "
                           "in the Convolver log.",
            "fault not shown");
    require(contains(log_text(), "The output became too large to represent (") &&
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
                    count(log_text(), "The output became too large to represent (") == 2,
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
                status() == convolver::HostStatus::playing,
            "status good again");
    require(p.setActive(false) == kResultOk, "status deactivate");
    setup.sampleRate = 44100;
    require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
            "status activate at 44.1 kHz");
    require(contains(shown(), "Silent: the filter is for 48000 Hz, the host runs at 44100 Hz") &&
                contains(log_text(), "Started at 44100 Hz, 2 in, 2 out, blocks of up to 64 "
                                     "frames. The saved filter does not suit the host"),
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
                                  "Filter loaded: 44100 Hz, 2 in, 2 out; checked when the "
                                  "host starts it.",
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
    set_log_variable(earlier);
    std::filesystem::remove(log_path);
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
    // 100-frame calls: not a multiple or divisor of the 256-frame partition.
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
    require(call(music) == 3, "the latency is silence, so it is flagged");
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
    // An impulse played during bypass, then bypass off 300 frames later: the
    // filter's tail of that impulse (its last tap, 1, at 299 + latency) is
    // heard, because bypass kept the convolution running.
    const int impulse = frame + 5;
    auto click = [&](int n) { return n == impulse ? 1.f : 0.f; };
    float last = 0;
    for (int k = 0; k < 8; ++k) {
        const bool switch_off = frame <= impulse + 300 && impulse + 300 < frame + frames;
        // Flags are checked against the content in call().
        if (switch_off)
            call(click, std::pair{impulse + 300 - frame, false});
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
    // An impulse at frame 0 reaches output 1 at 256 and 258 (taps 1 and .5);
    // rounding residue can follow until the block's overlap (frame 767) ends.
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
    require(flags[0] == 3 && flags[1] == 3 && flags[2] == 1 && flags[8] == 3 && flags[9] == 3,
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
            require(!automatable && (info.flags & ParameterInfo::kIsList) && info.stepCount == 6 &&
                        latency_frames(info.defaultNormalizedValue) == 256,
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
                restored.settings->mix == .25 && restored.settings->latency == 256,
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
    // Latency: the controller tells the processor and asks the host to
    // restart; the next activation plays at the new delay.
    handler.restarts.clear();
    // Until the host restarts it, the processor reports the latency it runs at.
    require(c.setParamNormalized(latency_id, normalized_latency(1024)) == kResultOk &&
                p.settings().latency == 1024 && p.getLatencySamples() == 256 &&
                handler.latency_restarts() == 1,
            "Latency not passed on with kLatencyChanged");
    require(c.setParamNormalized(latency_id, normalized_latency(1024)) == kResultOk &&
                handler.latency_restarts() == 1,
            "an unchanged Latency restarted the component");
    require(p.setProcessing(false) == kResultOk && p.setActive(false) == kResultOk &&
                p.setActive(true) == kResultOk && p.setProcessing(true) == kResultOk,
            "restart after Latency");
    require(p.getLatencySamples() == 1024 && p.getTailSamples() == 1024,
            "latency and tail after the restart");
    in.fill(0);
    in[0] = 1;
    int at = -1;
    for (int b = 0; b < 4 && at < 0; ++b) {
        require(p.process(d) == kResultOk, "latency process");
        in[0] = 0;
        for (int i = 0; i < 512; ++i)
            if (at < 0 && std::abs(out[i]) > 1e-6)
                at = b * 512 + i;
    }
    require(at == 1024, "output not delayed by the new Latency");
    // A state with another Latency changes the processor's and asks the
    // controller's host to restart.
    MemoryStream with_latency;
    require(p.getState(&with_latency) == kResultOk, "save latency");
    with_latency.seek(0, IBStream::kIBSeekSet, nullptr);
    require(read_state(&with_latency).settings->latency == 1024, "latency not saved");
    handler.restarts.clear();
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(p.setState(&state) == kResultOk && p.settings().latency == 256 &&
                p.getLatencySamples() == 1024,
            "an older state kept the Latency, or it played before a restart");
    state.seek(0, IBStream::kIBSeekSet, nullptr);
    require(c.setComponentState(&state) == kResultOk &&
                latency_frames(c.getParamNormalized(latency_id)) == 256 &&
                handler.latency_restarts() == 1,
            "controller did not ask to restart for the restored Latency");
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
    const auto* reader = convolver::vst3::test_file_reader;
#if defined(CONVOLVER_VST3_TESTS_IO) && !defined(__APPLE__)
    convolver::vst3::test_file_reader = &convolver::detail::libsndfile_files();
#endif
#if defined(__APPLE__) || defined(CONVOLVER_VST3_TESTS_IO)
    // Accepted: the filter plays, the report names the file, the host is told
    // the project changed, and the saved state holds the filter.
    require(c.load_file(good.string()) && handler.dirty == 1, "file not loaded");
    report = c.report();
    require(field(report, "status name") == "Playing" && field(report, "filter name") == "room.wav",
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
#else
    // A build with no reader refuses, and says why.
    require(!c.load_file(good.string()) && handler.dirty == 0, "a file loaded without a reader");
    const auto f = p.failure();
    require(f && f->message.find("cannot read filter files") != std::string::npos,
            "no reader not explained");
#endif
    convolver::vst3::test_file_reader = reader;
    require(!c.load_file(""), "an empty path accepted");
    std::filesystem::remove_all(folder);
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
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
int run(const std::filesystem::path& preset_path) {
    try {
        edge_cases();
        controller_conversions();
        settings_parameters();
        report_and_files();
        tail_measured_at_output();
        output_silence_flags();
        output_silence_flags_64();
        multichannel();
        version1_presets();
        speaker_arrangements();
        placeholder_setup();
        restart_mid_transition();
        failed_first_loads();
        failure_records();
        status_and_log();
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
            return preset_path.empty() ? (sample == 256 ? 1. : 0.)
                                       : (sample == 256   ? .5
                                          : sample == 257 ? .25
                                                          : 0.);
        };
        ProcessSetup setup{kRealtime, kSample32, 73, 48000};
        require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
                "activate");
        require(p.setProcessing(true) == kResultOk && p.getLatencySamples() == 256,
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
                require(std::abs(samples[i] - expected(k * 73 + i)) < 1e-5,
                        "adapter identity latency");
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
        std::cout << "VST3 adapter compatibility passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    // CTest sets CONVOLVER_LOG; run by hand, keep test lines out of the user's log.
    const auto own_log = private_temp("convolver-vst3-tests") += ".log";
    if (!log_variable())
        set_log_variable(own_log);
    struct Remove {
        std::filesystem::path path;
        ~Remove() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } remove{own_log};
    return run(argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
}
