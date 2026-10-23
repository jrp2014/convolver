// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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
    require(output.silenceFlags == 0, "tail marked silent");
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
    const std::array<unsigned char, 16> v3{12, 0, 0, 0, 0x43, 0x4e, 0x56, 0x53,
                                           3,  0, 0, 0, 0,    0,    0,    0};
    newer.write(const_cast<unsigned char*>(v3.data()), 16, nullptr);
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
    require(std::abs(last - 1.f) < 1e-5, "the tail ends before the filter's last tap");
    require(after < 1e-6, "output continues after the reported tail");
    p.setProcessing(false);
    p.setActive(false);
    p.terminate();
}
// VST3-8: the Bypass text converts both ways, and the controller's own
// state calls succeed (it keeps no state, so they store and read nothing).
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
            "a number is no longer accepted");
    require(c.getParamValueByString(bypass_id, other, value) != kResultOk,
            "unrelated text accepted");
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
        tail_measured_at_output();
        multichannel();
        version1_presets();
        speaker_arrangements();
        placeholder_setup();
        restart_mid_transition();
        failed_first_loads();
        failure_records();
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
    return run(argc == 2 ? std::filesystem::path(argv[1]) : std::filesystem::path{});
}
