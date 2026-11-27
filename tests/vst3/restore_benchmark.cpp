// SPDX-License-Identifier: GPL-2.0-or-later
// PERF-22: what Audacity 4's playback start costs the VST3, step by step.
// Audacity makes a fresh plug-in pair at every Play and hands it the state it
// saved, so each trial here does the same with a new, connected pair: the
// processor's setState, the controller's setComponentState, setupProcessing,
// setActive and the first block, then the getState Audacity reads at the end
// (the total column covers all of them). setActive is where the filter is
// prepared. The library passes inside those steps, reading the state among
// them, are then timed alone on the same filter.
//
//   convolver_vst3_restore_benchmark [--trials N] [--block FRAMES] FILTER...
#include "attenuation.hpp"
#include "convolver/engine.hpp"
#include "convolver/host.hpp"
#include "filter_file.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "sound_files.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::vst3;
namespace {
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void require(bool ok, const char* what) {
    if (!ok)
        throw std::runtime_error(what);
}
// The saved state as bytes: what a host keeps in its project.
std::vector<char> saved_state(const std::filesystem::path& filter) {
    // The reader the plug-in itself uses on this platform (processor.cpp).
#ifdef __APPLE__
    const auto& files = convolver::detail::audio_toolbox_files();
#else
    const auto& files = convolver::detail::libsndfile_files();
#endif
    const auto state = convolver::filter_file_state(filter, {}, files);
    MemoryStream stream;
    require(write_state(&stream, state), "write_state");
    return {stream.getData(), stream.getData() + stream.getSize()};
}
void rewind(MemoryStream& s) {
    s.seek(0, IBStream::kIBSeekSet, nullptr);
}
struct Times {
    std::vector<double> set_state, component, setup, activate, block, get_state, total;
};
double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v.empty() ? 0 : (v[(v.size() - 1) / 2] + v[v.size() / 2]) / 2;
}
void trial(const std::vector<char>& bytes, int block, Times& t) {
    MemoryStream state;
    int32 written = 0;
    state.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), &written);

    HostApplication context;
    const auto all = Clock::now();
    Processor p;
    Controller c;
    require(p.initialize(&context) == kResultOk, "processor initialize");
    require(c.initialize(&context) == kResultOk, "controller initialize");
    // Connected as a host connects them, so the messages between the halves
    // after each step are part of what is timed.
    require(p.connect(&c) == kResultOk && c.connect(&p) == kResultOk, "connect");
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    require(p.setBusArrangements(&stereo, 1, &stereo, 1) == kResultOk, "stereo buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);

    rewind(state);
    auto start = Clock::now();
    require(p.setState(&state) == kResultOk, "setState");
    t.set_state.push_back(ms_since(start));
    rewind(state);
    start = Clock::now();
    require(c.setComponentState(&state) == kResultOk, "setComponentState");
    t.component.push_back(ms_since(start));
    ProcessSetup setup{kRealtime, kSample32, block, 48000};
    start = Clock::now();
    require(p.setupProcessing(setup) == kResultOk, "setupProcessing");
    t.setup.push_back(ms_since(start));
    start = Clock::now();
    require(p.setActive(true) == kResultOk, "setActive");
    t.activate.push_back(ms_since(start));

    std::vector<std::vector<float>> in(2, std::vector<float>(block)),
        out(2, std::vector<float>(block));
    in[0][0] = in[1][0] = 1;
    float* ins[2] = {in[0].data(), in[1].data()};
    float* outs[2] = {out[0].data(), out[1].data()};
    AudioBusBuffers input{}, output{};
    input.numChannels = output.numChannels = 2;
    input.channelBuffers32 = ins;
    output.channelBuffers32 = outs;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = block;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &input;
    data.outputs = &output;
    require(p.setProcessing(true) == kResultOk, "setProcessing");
    start = Clock::now();
    require(p.process(data) == kResultOk, "process");
    t.block.push_back(ms_since(start));
    for (const auto& channel : out)
        require(std::any_of(channel.begin(), channel.end(), [](float v) { return v != 0; }),
                "the first block is silent");

    MemoryStream again;
    start = Clock::now();
    require(p.getState(&again) == kResultOk, "getState");
    t.get_state.push_back(ms_since(start));
    t.total.push_back(ms_since(all));
    p.setProcessing(false);
    p.setActive(false);
    p.disconnect(&c);
    c.disconnect(&p);
    c.terminate();
    p.terminate();
}
// The library passes inside those steps, timed alone on the same filter, to
// say which one costs what: each runs over every sample of the filter.
void parts(const std::vector<char>& bytes, int block, int trials) {
    std::vector<double> decode, validate, tail, sums, identity, engine, encode;
    for (int i = 0; i <= trials; ++i) {
        MemoryStream s;
        int32 written = 0;
        s.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), &written);
        rewind(s);
        auto start = Clock::now();
        const auto state = read_state(&s);
        const double d = ms_since(start);
        const auto& c = *state.configuration;
        start = Clock::now();
        convolver::validate_configuration(c);
        const double v = ms_since(start);
        start = Clock::now();
        volatile auto t = convolver::configuration_tail_frames(c);
        (void)t;
        const double tl = ms_since(start);
        start = Clock::now();
        volatile auto k = convolver::detail::kernel_sums(c).peak;
        (void)k;
        const double ks = ms_since(start);
        start = Clock::now();
        volatile auto id = convolver::configuration_identity(c);
        (void)id;
        const double ident = ms_since(start);
        start = Clock::now();
        {
            convolver::Engine e(c, static_cast<std::size_t>(block));
        }
        const double en = ms_since(start);
        start = Clock::now();
        {
            const auto out = convolver::encode_host_state(state);
        }
        const double ec = ms_since(start);
        if (i == 0)
            continue; // Warm-up.
        decode.push_back(d);
        validate.push_back(v);
        tail.push_back(tl);
        sums.push_back(ks);
        identity.push_back(ident);
        engine.push_back(en);
        encode.push_back(ec);
    }
    std::printf("  parts (ms): read_state %.1f, validate_configuration %.1f, "
                "configuration_tail_frames %.1f, kernel_sums %.1f, configuration_identity %.1f, "
                "Engine %.1f, encode_host_state %.1f\n",
                median(decode), median(validate), median(tail), median(sums), median(identity),
                median(engine), median(encode));
}
} // namespace
int main(int argc, char** argv) try {
    int trials = 7, block = 4800;
    std::vector<std::filesystem::path> filters;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--trials" && i + 1 < argc)
            trials = std::stoi(argv[++i]);
        else if (a == "--block" && i + 1 < argc)
            block = std::stoi(argv[++i]);
        else
            filters.emplace_back(a);
    }
    require(!filters.empty(), "name one or more filters");
    std::printf("filter,state_mb,setState,setComponentState,setupProcessing,setActive,"
                "first_block,getState,total\n");
    for (const auto& f : filters) {
        const auto bytes = saved_state(f);
        Times t;
        trial(bytes, block, t); // Warm-up, not counted: first use of the libraries.
        t = {};
        for (int i = 0; i < trials; ++i)
            trial(bytes, block, t);
        std::printf("%s,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f\n",
                    f.parent_path().filename().string().c_str(), bytes.size() / 1e6,
                    median(t.set_state), median(t.component), median(t.setup), median(t.activate),
                    median(t.block), median(t.get_state), median(t.total));
        parts(bytes, block, trials);
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << "restore benchmark: " << e.what() << "\n";
    return 1;
}
