// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
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
namespace {
void require(bool ok, const char* message) {
    if (!ok)
        throw std::runtime_error(message);
}
int probe(const std::filesystem::path& path, int rate, int count, bool echo) {
    try {
        require(count == 1 || count == 2, "mono/stereo only");
        HostApplication context;
        Processor p;
        require(p.initialize(&context) == kResultOk, "initialize");
        SpeakerArrangement layout = count == 1 ? SpeakerArr::kMono : SpeakerArr::kStereo;
        require(p.setBusArrangements(&layout, 1, &layout, 1) == kResultOk, "buses");
        std::ifstream file(path, std::ios::binary);
        std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
        MemoryStream preset(bytes.data(), static_cast<TSize>(bytes.size()));
        require(PresetFile::loadPreset(&preset, processor_id, &p), "load embedded preset");
        p.activateBus(kAudio, kInput, 0, true);
        p.activateBus(kAudio, kOutput, 0, true);
        ProcessSetup setup{kOffline, kSample32, 137, static_cast<double>(rate)};
        require(p.setupProcessing(setup) == kResultOk && p.setActive(true) == kResultOk,
                "activate matching rate/layout");
        require(p.setProcessing(true) == kResultOk && p.getLatencySamples() == 256, "latency");
        std::array<float, 137> left{}, right{};
        float* channels[] = {left.data(), right.data()};
        AudioBusBuffers in{}, out{};
        in.numChannels = out.numChannels = count;
        in.channelBuffers32 = out.channelBuffers32 = channels;
        ProcessData data{};
        data.symbolicSampleSize = kSample32;
        data.numInputs = data.numOutputs = 1;
        data.inputs = &in;
        data.outputs = &out;
        // Independent time-domain oracle: channel impulses of 1 and -0.5.
        int position = 0;
        for (int block : {1, 73, 137, 3, 129, 137, 137, 137}) {
            left.fill(0);
            right.fill(0);
            if (position == 0) {
                left[0] = 1;
                right[0] = -.5f;
            }
            in.silenceFlags = position ? static_cast<uint64>((1 << count) - 1) : 0;
            data.numSamples = block;
            require(p.process(data) == kResultOk, "process");
            for (int i = 0; i < block; ++i) {
                const int sample = position + i;
                const float expected = sample == 256             ? (echo ? .5f : 1.f)
                                       : (echo && sample == 513) ? .25f
                                                                 : 0.f;
                for (int c = 0; c < count; ++c)
                    require(std::isfinite(channels[c][i]) &&
                                std::abs(channels[c][i] - expected * (c == 0 ? 1.f : -.5f)) < 1e-5f,
                            "packaged preset disagrees with independent oracle");
            }
            position += block;
        }
        MemoryStream saved;
        require(p.getState(&saved) == kResultOk, "save state");
        p.setProcessing(false);
        p.setActive(false);
        saved.seek(0, IBStream::kIBSeekSet, nullptr);
        require(p.setState(&saved) == kResultOk, "restore state");
        p.terminate();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    if (argc != 5)
        return 2;
    return probe(argv[1], std::stoi(argv[2]), std::stoi(argv[3]),
                 std::filesystem::path(argv[4]) == "echo");
}
