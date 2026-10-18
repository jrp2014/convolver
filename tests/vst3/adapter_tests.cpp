// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <array>
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
int run(const std::filesystem::path& preset_path) {
    try {
        edge_cases();
        HostApplication context;
        Processor p;
        require(p.initialize(&context) == kResultOk, "initialize");
        SpeakerArrangement mono = SpeakerArr::kMono, stereo = SpeakerArr::kStereo;
        require(p.setBusArrangements(&mono, 1, &stereo, 1) != kResultOk,
                "mismatched buses accepted");
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
