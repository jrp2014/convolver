// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-3: the SDK-free .vstpreset writer and reader (portable/presets.cpp)
// against Steinberg's own PresetFile, in both directions.
#include "presets.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
void require(bool v, const std::string& m) {
    if (!v)
        throw std::runtime_error(m);
}
std::vector<std::byte> contents(MemoryStream& stream) {
    const auto* data = reinterpret_cast<const std::byte*>(stream.getData());
    return {data, data + stream.getSize()};
}
// What convolver-vst3-preset wrote before PLUG-3, with an optional controller
// chunk and metadata as hosts add them; or, with `controller_state` (PLUG-31),
// the controller state the plug-in's controller would save.
std::vector<std::byte> sdk_preset(const convolver::HostState& state, bool host_extras,
                                  const std::string& controller_state = {}) {
    MemoryStream component, controller, preset;
    require(convolver::vst3::write_state(&component, state), "SDK component state");
    component.seek(0, IBStream::kIBSeekSet, nullptr);
    const char xml[] = "<MetaInfo></MetaInfo>";
    const auto controller_bytes = host_extras ? std::string("controller") : controller_state;
    if (!controller_bytes.empty()) {
        int32 written = 0;
        controller.write(const_cast<char*>(controller_bytes.data()),
                         static_cast<int32>(controller_bytes.size()), &written);
        controller.seek(0, IBStream::kIBSeekSet, nullptr);
    }
    require(PresetFile::savePreset(&preset, convolver::vst3::processor_id, &component,
                                   controller_bytes.empty() ? nullptr : &controller,
                                   host_extras ? xml : nullptr,
                                   host_extras ? static_cast<int32>(std::strlen(xml)) : -1),
            "SDK savePreset");
    return contents(preset);
}
// The controller state the SDK finds in a preset.
std::string sdk_controller_state(const std::vector<std::byte>& bytes) {
    MemoryStream stream;
    int32 written = 0;
    stream.write(const_cast<std::byte*>(bytes.data()), static_cast<int32>(bytes.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    PresetFile file(&stream);
    require(file.readChunkList(), "SDK reads the chunk list");
    if (!file.seekToControllerState())
        return {};
    const auto* entry = file.getEntry(kControllerState);
    std::string out(static_cast<std::size_t>(entry->size), '\0');
    int32 read = 0;
    stream.read(out.data(), static_cast<int32>(out.size()), &read);
    require(read == static_cast<int32>(out.size()), "SDK reads the controller state");
    return out;
}
convolver::HostState sdk_read(const std::vector<std::byte>& bytes) {
    MemoryStream stream;
    int32 written = 0;
    stream.write(const_cast<std::byte*>(bytes.data()), static_cast<int32>(bytes.size()), &written);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    PresetFile file(&stream);
    require(file.readChunkList(), "SDK reads the chunk list");
    require(file.getClassID() == convolver::vst3::processor_id, "SDK reads our class ID");
    require(file.seekToComponentState(), "SDK finds the component state");
    return convolver::vst3::read_state(&stream);
}
convolver::HostState routed(std::size_t inputs, std::size_t outputs, std::uint32_t mask) {
    convolver::Configuration c{44100, inputs, outputs, mask, {}, {}, {}};
    c.input_delays.assign(inputs, 1);
    c.output_delays.assign(outputs, 2);
    for (std::size_t o = 0; o < outputs; ++o)
        c.paths.push_back({{.5f, -.25f, 1e-7f},
                           {{static_cast<std::uint32_t>(o % inputs), -.5f}},
                           {{static_cast<std::uint32_t>(o), 2.f}}});
    return {std::move(c), false};
}
} // namespace

int main() {
    try {
        char sdk_id[33] = {};
        convolver::vst3::processor_id.toString(sdk_id);
        require(convolver::presets::vst3_class_id() == sdk_id, "class ID text matches FUID");
        std::vector<convolver::HostState> states{
            {}, routed(1, 1, 0), routed(2, 2, 3), routed(2, 6, 0x3F), routed(8, 8, 0x63F)};
        states[2].bypass = true;
        for (const auto& state : states) {
            const auto ours = convolver::presets::vst3_preset(state);
            require(ours == sdk_preset(state, false), "byte-identical to PresetFile::savePreset");
            const auto expected = convolver::encode_host_state(state);
            require(convolver::encode_host_state(sdk_read(ours)) == expected,
                    "SDK reads our preset");
            require(convolver::encode_host_state(
                        convolver::presets::read_vst3_preset(sdk_preset(state, true))) == expected,
                    "we read SDK presets with controller and metadata chunks");
            require(sdk_controller_state(ours).empty(), "a controller state without a name");
            // PLUG-31: with the filter's name, as savePreset writes the
            // controller's state, and the SDK finds it again.
            const auto name = convolver::presets::vst3_filter_name(state, "Room \xC3\xBC.wav");
            require(name.name.empty() == !state.configuration, "a name for every filter");
            if (name.name.empty())
                continue;
            const auto controller = convolver::vst3::encode_controller_state(name);
            const auto named = convolver::presets::vst3_preset(state, name);
            require(named == sdk_preset(state, false, controller),
                    "named preset byte-identical to PresetFile::savePreset");
            require(convolver::encode_host_state(sdk_read(named)) == expected,
                    "SDK reads our named preset");
            require(sdk_controller_state(named) == controller, "SDK reads our controller state");
            const auto back =
                convolver::presets::read_vst3_filter_name(sdk_preset(state, false, controller));
            require(back.name == name.name && back.identity == name.identity,
                    "we read the SDK's controller state");
        }
        std::cout << "VST3 presets: identical to the SDK's, readable both ways\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
