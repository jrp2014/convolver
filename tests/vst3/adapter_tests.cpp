// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <array>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
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
    require(p.getTailSamples() == 1, "tail unavailable before activation");
    require(p.setActive(true) == kResultOk, "offline activate");
    require(p.getTailSamples() == 1, "reported FIR tail");
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
    input.silenceFlags = 3;
    d.numSamples = 512;
    require(p.process(d) == kResultOk, "silent input with wet tail");
    for (int i = 0; i < 512; ++i)
        require(std::abs(out_right[i] - (i == 157 ? 1.f : 0.f)) < 1e-5 && out_left[i] == 0,
                "silence lost tail/routing");
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
    require(p.setState(&longer) == kResultOk && p.getTailSamples() == 1010,
            "inactive replacement tail including routing delays");
    setup.sampleRate = 44100;
    require(p.setupProcessing(setup) == kResultOk, "rate change setup");
    require(p.setActive(true) != kResultOk, "mismatched IR rate activated");
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
    require(p.setActive(true) != kResultOk, "2x6 preset activated on 8x8 buses");
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
int run(const std::filesystem::path& preset_path) {
    try {
        edge_cases();
        multichannel();
        version1_presets();
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
