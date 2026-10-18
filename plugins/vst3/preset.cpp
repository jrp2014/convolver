// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/io.hpp"
#include "file.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <iostream>
#include <stdexcept>
using namespace Steinberg;
using namespace Steinberg::Vst;
int make_preset(const std::filesystem::path& input, const std::filesystem::path& output) {
    try {
        auto config = convolver::load_configuration(input);
        if (config.input_channels != config.output_channels || config.input_channels > 2)
            throw std::invalid_argument(
                "VST3 presets require mono or stereo with equal input/output counts");
        convolver::HostState state{std::move(config), false};
        MemoryStream component, preset;
        if (!convolver::vst3::write_state(&component, state))
            throw std::runtime_error("cannot encode component state");
        component.seek(0, IBStream::kIBSeekSet, nullptr);
        if (!PresetFile::savePreset(&preset, convolver::vst3::processor_id, &component))
            throw std::runtime_error("cannot encode VST3 preset");
        convolver::detail::write_new_file(
            output,
            std::as_bytes(std::span(preset.getData(), static_cast<std::size_t>(preset.getSize()))));
        std::cout << "Created self-contained VST3 preset\n";
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
    if (argc != 3) {
        std::cerr << "Usage: convolver-vst3-preset IR_OR_CFG NEW_FILE.vstpreset\n";
        return 2;
    }
    return make_preset(argv[1], argv[2]);
}
