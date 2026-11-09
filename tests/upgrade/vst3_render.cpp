// SPDX-License-Identifier: GPL-2.0-or-later
// COMPAT-1: render audio through a VST3 bundle as a host does. The bundle is
// loaded from its file, so the same program renders with a published
// package's plug-in (to capture a release's expected audio) and with the
// plug-in just built (to check it against that capture).
//
// Usage: convolver_vst3_render BUNDLE PRESET RATE INPUTS OUTPUTS IN.f32 OUT.f32 FRAMES
//
// IN.f32 holds the input as interleaved 32-bit floats in native byte order.
// The program restores PRESET (a .vstpreset), plays the input followed by
// silence in host blocks of irregular sizes, drops the latency the plug-in
// reports and writes FRAMES interleaved output frames to OUT.f32. It prints
// the reported latency on stdout.
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/module.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}

std::vector<char> read_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "cannot open " + path);
    return {std::istreambuf_iterator<char>(file), {}};
}

// Discrete channels: the adapter accepts any arrangement with the right count.
SpeakerArrangement arrangement(int channels) {
    return (SpeakerArrangement(1) << channels) - 1;
}

// Host blocks of irregular sizes, so the capture does not depend on one size.
constexpr std::array<int, 8> block_sizes{1, 73, 137, 3, 129, 512, 64, 137};
constexpr int max_block = 512;

int render(const std::vector<std::string>& args) {
    const std::string& bundle = args[0];
    const std::string& preset_path = args[1];
    const int rate = std::stoi(args[2]);
    const int inputs = std::stoi(args[3]);
    const int outputs = std::stoi(args[4]);
    const std::string& input_path = args[5];
    const std::string& output_path = args[6];
    const long long frames = std::stoll(args[7]);
    require(inputs > 0 && inputs <= 32 && outputs > 0 && outputs <= 32 && frames > 0 && rate > 0,
            "invalid rate, channel counts or frame count");

    std::string error;
    auto module = VST3::Hosting::Module::create(bundle, error);
    require(module != nullptr, "cannot load " + bundle + ": " + error);
    const auto factory = module->getFactory();
    const auto classes = factory.classInfos();
    const auto info = std::find_if(classes.begin(), classes.end(), [](const auto& c) {
        return c.category() == kVstAudioEffectClass;
    });
    require(info != classes.end(), "the bundle has no audio processor class");
    auto component = factory.createInstance<IComponent>(info->ID());
    require(component != nullptr, "cannot create the processor");
    FUnknownPtr<IAudioProcessor> processor(component);
    require(processor != nullptr, "the processor has no IAudioProcessor");

    HostApplication host;
    require(component->initialize(&host) == kResultOk, "initialize");
    SpeakerArrangement in_layout = arrangement(inputs), out_layout = arrangement(outputs);
    require(processor->setBusArrangements(&in_layout, 1, &out_layout, 1) == kResultOk,
            "the plug-in refused the channel counts");

    auto preset_bytes = read_file(preset_path);
    MemoryStream preset(preset_bytes.data(), static_cast<TSize>(preset_bytes.size()));
    require(PresetFile::loadPreset(&preset, FUID::fromTUID(info->ID().data()), component),
            "the plug-in refused the preset");

    component->activateBus(kAudio, kInput, 0, true);
    component->activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{kOffline, kSample32, max_block, static_cast<double>(rate)};
    require(processor->setupProcessing(setup) == kResultOk, "setupProcessing");
    require(component->setActive(true) == kResultOk, "the plug-in did not activate");
    require(processor->setProcessing(true) == kResultOk, "setProcessing");
    const long long latency = processor->getLatencySamples();

    const auto input_bytes = read_file(input_path);
    require(input_bytes.size() % (sizeof(float) * static_cast<std::size_t>(inputs)) == 0,
            "the input is not a whole number of frames");
    std::vector<float> input(input_bytes.size() / sizeof(float));
    std::copy(input_bytes.begin(), input_bytes.end(), reinterpret_cast<char*>(input.data()));
    const long long input_frames = static_cast<long long>(input.size()) / inputs;

    std::vector<std::vector<float>> in_channels(inputs, std::vector<float>(max_block));
    std::vector<std::vector<float>> out_channels(outputs, std::vector<float>(max_block));
    std::vector<float*> in_pointers, out_pointers;
    for (auto& c : in_channels)
        in_pointers.push_back(c.data());
    for (auto& c : out_channels)
        out_pointers.push_back(c.data());
    AudioBusBuffers in{}, out{};
    in.numChannels = inputs;
    out.numChannels = outputs;
    in.channelBuffers32 = in_pointers.data();
    out.channelBuffers32 = out_pointers.data();
    ProcessData data{};
    data.processMode = kOffline;
    data.symbolicSampleSize = kSample32;
    data.numInputs = data.numOutputs = 1;
    data.inputs = &in;
    data.outputs = &out;

    std::vector<float> result;
    result.reserve(static_cast<std::size_t>(frames * outputs));
    const long long total = latency + frames;
    long long position = 0;
    for (std::size_t b = 0; position < total; ++b) {
        const int block = block_sizes[b % block_sizes.size()];
        bool silent = true;
        for (int i = 0; i < block; ++i)
            for (int c = 0; c < inputs; ++c) {
                const long long frame = position + i;
                const float v = frame < input_frames ? input[frame * inputs + c] : 0.f;
                in_channels[c][i] = v;
                silent = silent && v == 0.f;
            }
        in.silenceFlags = silent ? (uint64(1) << inputs) - 1 : 0;
        data.numSamples = block;
        require(processor->process(data) == kResultOk, "process");
        for (int i = 0; i < block; ++i) {
            const long long frame = position + i;
            if (frame >= latency && frame < total)
                for (int c = 0; c < outputs; ++c)
                    result.push_back(out_channels[c][i]);
        }
        position += block;
    }
    processor->setProcessing(false);
    component->setActive(false);
    component->terminate();

    std::ofstream file(output_path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(result.data()),
               static_cast<std::streamsize>(result.size() * sizeof(float)));
    require(file.good(), "cannot write " + output_path);
    std::cout << "latency " << latency << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 9) {
        std::cerr << "usage: " << argv[0]
                  << " BUNDLE PRESET RATE INPUTS OUTPUTS IN.f32 OUT.f32 FRAMES\n";
        return 2;
    }
    try {
        return render(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
}
