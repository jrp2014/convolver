// SPDX-License-Identifier: GPL-2.0-or-later
// convolver-preset (PLUG-3): one filter, presets for every plug-in format.
// Built with CONVOLVER_VST3_PRESET_ALIAS it is convolver-vst3-preset, which
// keeps its original arguments, messages and output for existing scripts.
#include "convolver/io.hpp"
#include "presets.hpp"
#include <charconv>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;
using convolver::presets::Format;

#ifdef CONVOLVER_VST3_PRESET_ALIAS
constexpr bool alias = true;
#else
constexpr bool alias = false;
#endif

constexpr const char* alias_usage =
    "Usage: convolver-vst3-preset [--sample-rate HZ] [--input-channels N]\n"
    "                             [--output-channels N] IR_OR_CFG NEW_FILE.vstpreset\n";
constexpr const char* usage =
    "Usage: convolver-preset [--sample-rate HZ] [--input-channels N] [--output-channels N]\n"
    "                        [--name NAME] INPUT NEW_PRESET [NEW_PRESET...]\n"
    "Writes a self-contained plug-in preset: the filter is embedded, so the preset\n"
    "keeps working if the original files move or are deleted.\n"
    "  INPUT        an impulse response (WAV or another libsndfile format), a .cfg,\n"
    "               a configuration list, or a .vstpreset to convert\n"
    "  NEW_PRESET   the format follows the name; each must be a new path:\n"
    "                 NAME.vstpreset   VST3 preset file\n"
    "                 NAME.aupreset    Audio Unit preset file\n"
    "                 NAME.preset.lv2  LV2 preset bundle (a folder)\n"
    "  --sample-rate, --input-channels, --output-channels\n"
    "               choose the entry of a configuration list, as the CLI does\n"
    "  --name NAME  the name hosts show for AU and LV2 presets (default: the first\n"
    "               AU or LV2 file name, without its extension)\n"
    "See docs/use/presets.md.\n";

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
std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
bool positive(const std::string& text, unsigned long maximum, unsigned long& value) {
    const auto* end = text.data() + text.size();
    auto [p, error] = std::from_chars(text.data(), end, value);
    return error == std::errc{} && p == end && value > 0 && value <= maximum;
}
// NAME.preset.lv2 and NAME.vstpreset both give NAME.
std::string default_name(const fs::path& output) {
    auto stem = output.filename().stem();
    if (stem.extension() == ".preset")
        stem = stem.stem();
    return utf8(stem);
}
std::vector<std::byte> read_file(const fs::path& path) {
    // A preset is at most the state limit plus its small containers.
    constexpr auto limit = convolver::max_host_state_bytes + 4096;
    std::error_code error;
    if (const auto size = fs::file_size(path, error); !error && size > limit)
        throw std::runtime_error(utf8(path) + " is too large to be a Convolver preset");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open " + utf8(path));
    std::vector<char> text((std::istreambuf_iterator<char>(file)),
                           std::istreambuf_iterator<char>());
    if (text.size() > limit)
        throw std::runtime_error(utf8(path) + " is too large to be a Convolver preset");
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    return {bytes, bytes + text.size()};
}

int make_presets(const fs::path& input, const std::vector<fs::path>& outputs,
                 const convolver::ConfigurationSelection& selection, bool selected,
                 std::string name) {
    std::vector<fs::path> created;
    try {
        std::vector<Format> formats;
        for (const auto& output : outputs) {
            if (alias) {
                formats.push_back(Format::vst3);
                continue;
            }
            auto format = convolver::presets::format_for(output);
            if (!format)
                throw std::invalid_argument(
                    utf8(output) +
                    ": choose the preset format by the name's ending: .vstpreset (VST3), "
                    ".aupreset (AU) or .preset.lv2 (an LV2 preset folder)");
            formats.push_back(*format);
        }
        convolver::HostState state;
        if (!alias && convolver::presets::format_for(input) == Format::vst3) {
            if (selected)
                throw std::invalid_argument(
                    "a .vstpreset already holds one filter; --sample-rate, --input-channels "
                    "and --output-channels choose from configuration lists only");
            state = convolver::presets::read_vst3_preset(read_file(input));
        } else {
            std::vector<fs::path> raw_filters;
            state = {convolver::load_configuration(input, selection, nullptr, &raw_filters), false};
            // Presets embed the decoded taps, so say what a raw filter was taken to be.
            for (const auto& raw_filter : raw_filters)
                std::cerr << "raw filter '" << utf8(raw_filter.filename())
                          << "' read as mono f32le at " << state.configuration->sample_rate
                          << " Hz; convert raw filters in other sample types to WAV first\n";
        }
        if (alias) {
            constexpr auto max_channels =
                static_cast<std::size_t>(convolver::identity::max_channels);
            // The original tool's own check and message, for existing scripts.
            if (state.configuration && (state.configuration->input_channels > max_channels ||
                                        state.configuration->output_channels > max_channels))
                throw std::invalid_argument(
                    "VST3 presets support 1 to 8 input and output channels");
        }
        // The name hosts show for AU and LV2 presets (a .vstpreset stores none):
        // by default the first of those outputs' file names.
        for (std::size_t i = 0; name.empty() && i < outputs.size(); ++i)
            if (formats[i] != Format::vst3)
                name = default_name(outputs[i]);
        // Check every output before writing any, so a refused one leaves nothing.
        // The alias leaves existing paths to the exclusive create, as it always did.
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            convolver::presets::check_writable(formats[i], state, name);
            std::error_code error;
            if (!alias && fs::symlink_status(outputs[i], error).type() != fs::file_type::not_found)
                throw std::invalid_argument(utf8(outputs[i]) +
                                            " already exists; presets are only written to new "
                                            "paths, so choose another name or remove it first");
        }
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            try {
                convolver::presets::write_preset(formats[i], state, outputs[i], name);
            } catch (const std::exception& e) {
                if (alias)
                    throw;
                throw std::runtime_error(utf8(outputs[i]) + ": " + e.what() +
                                         ". Check that its folder exists and is writable.");
            }
            created.push_back(outputs[i]);
        }
        // Report only once every preset is written: a failure removes them all.
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            if (alias) {
                std::cout << "Created self-contained VST3 preset\n";
                continue;
            }
            std::cout << "Created self-contained " << convolver::presets::format_name(formats[i])
                      << " preset " << utf8(outputs[i]);
            if (formats[i] == Format::lv2) {
                const auto& c = *state.configuration;
                std::cout << " for " << convolver::identity::lv2_base_uri
                          << convolver::presets::lv2_layout(c.input_channels, c.output_channels)->id
                          << "; copy the folder into an LV2 preset path such as ~/.lv2";
            }
            std::cout << '\n';
        }
        return 0;
    } catch (const std::exception& e) {
        // Remove only presets this run created, so a retry starts clean.
        for (const auto& path : created) {
            std::error_code error;
            fs::remove_all(path, error);
        }
        std::cerr << e.what() << '\n';
        if (!created.empty())
            std::cerr << "No presets were kept from this run.\n";
        return 1;
    }
}

template <class Char> int run(int argc, Char** argv) {
    const char* text = alias ? alias_usage : usage;
    convolver::ConfigurationSelection selection;
    bool selected = false;
    std::string name;
    int i = 1;
    if (!alias && argc == 2 && (ascii(argv[1]) == "--help" || ascii(argv[1]) == "-h")) {
        std::cout << text;
        return 0;
    }
    for (; i + 1 < argc; i += 2) {
        auto option = ascii(argv[i]);
        if (option.rfind("--", 0) != 0)
            break;
        auto value_text = ascii(argv[i + 1]);
        unsigned long value = 0;
        if (option == "--sample-rate" && positive(value_text, 768000, value))
            selection.sample_rate = static_cast<int>(value);
        else if (option == "--input-channels" && positive(value_text, 256, value))
            selection.input_channels = value;
        else if (option == "--output-channels" && positive(value_text, 256, value))
            selection.output_channels = value;
        else if (!alias && option == "--name") {
            try {
                name = utf8(fs::path(argv[i + 1]));
            } catch (const std::exception&) { // Windows: unpaired UTF-16 surrogates
                name.clear();
            }
            if (name.empty()) {
                std::cerr << "--name needs a name in Unicode text\n" << text;
                return 2;
            }
            continue;
        } else {
            std::cerr << "Invalid option or value: " << option << '\n' << text;
            return 2;
        }
        selected = true;
    }
    const int positional = argc - i;
    if (alias ? positional != 2 : positional < 2) {
        std::cerr << text;
        return 2;
    }
    std::vector<fs::path> outputs;
    for (int k = i + 1; k < argc; ++k)
        outputs.emplace_back(argv[k]);
    return make_presets(argv[i], outputs, selection, selected, name);
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    return run(argc, argv);
}
