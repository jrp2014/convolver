// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/io.hpp"
#include "file.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include <charconv>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
int make_preset(const std::filesystem::path& input, const std::filesystem::path& output,
                const convolver::ConfigurationSelection& selection) {
    try {
        auto config = convolver::load_configuration(input, selection);
        if (config.input_channels > convolver::vst3::max_bus_channels ||
            config.output_channels > convolver::vst3::max_bus_channels)
            throw std::invalid_argument("VST3 presets support 1 to 8 input and output channels");
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
// Options are ASCII; anything else cannot match one.
template <class Char> std::string ascii(const Char* text) {
    std::string result;
    for (; *text; ++text) {
        if (static_cast<unsigned long>(*text) > 127)
            return {};
        result.push_back(static_cast<char>(*text));
    }
    return result;
}
bool positive(const std::string& text, unsigned long maximum, unsigned long& value) {
    const auto* end = text.data() + text.size();
    auto [p, error] = std::from_chars(text.data(), end, value);
    return error == std::errc{} && p == end && value > 0 && value <= maximum;
}
template <class Char> int run(int argc, Char** argv) {
    constexpr const char* usage =
        "Usage: convolver-vst3-preset [--sample-rate HZ] [--input-channels N]\n"
        "                             [--output-channels N] IR_OR_CFG NEW_FILE.vstpreset\n";
    convolver::ConfigurationSelection selection;
    int i = 1;
    for (; i + 1 < argc; i += 2) {
        auto option = ascii(argv[i]);
        if (option.rfind("--", 0) != 0)
            break;
        auto text = ascii(argv[i + 1]);
        unsigned long value = 0;
        if (option == "--sample-rate" && positive(text, 768000, value))
            selection.sample_rate = static_cast<int>(value);
        else if (option == "--input-channels" && positive(text, 256, value))
            selection.input_channels = value;
        else if (option == "--output-channels" && positive(text, 256, value))
            selection.output_channels = value;
        else {
            std::cerr << "Invalid option or value: " << option << '\n' << usage;
            return 2;
        }
    }
    if (argc - i != 2) {
        std::cerr << usage;
        return 2;
    }
    return make_preset(argv[i], argv[i + 1], selection);
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    return run(argc, argv);
}
