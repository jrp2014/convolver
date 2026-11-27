// SPDX-License-Identifier: GPL-2.0-or-later
// convolver-preset (PLUG-3): one filter, presets for every plug-in format.
// Built with CONVOLVER_VST3_PRESET_ALIAS it is convolver-vst3-preset, which
// keeps its original arguments, messages and output for existing scripts.
#include "advice.hpp"
#include "convolver/io.hpp"
#include "filter_file.hpp"
#include "presets.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <charconv>
#include <cstddef>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {
namespace fs = std::filesystem;
using convolver::detail::advice_of;
using convolver::detail::advice_text;
using convolver::detail::cli_error;
using convolver::detail::utf8_string;
using convolver::presets::Format;

#ifdef CONVOLVER_VST3_PRESET_ALIAS
constexpr bool alias = true;
#else
constexpr bool alias = false;
#endif

// Messages start with the program's name, as the other tools' do; the alias
// keeps its original messages.
constexpr const char* program = alias ? "" : "convolver-preset: ";

constexpr const char* alias_usage =
    "Usage: convolver-vst3-preset [--sample-rate HZ] [--input-channels N]\n"
    "                             [--output-channels N] IR_OR_CFG NEW_FILE.vstpreset\n";
constexpr const char* usage =
    "Usage: convolver-preset [--sample-rate HZ] [--input-channels N] [--output-channels N]\n"
    "                        [--name NAME] INPUT NEW_PRESET [NEW_PRESET...]\n"
    "Writes a self-contained plug-in preset: the filter is embedded, so the preset\n"
    "keeps working if the original files move or are deleted. A .vstpreset and an\n"
    ".aupreset also keep the filter file's name, not its folder, which the\n"
    "window shows in its file details.\n"
    "  INPUT        an IR file (an impulse response in a WAV or other sound file),\n"
    "               a .cfg, a configuration list, or a .vstpreset to convert\n"
    "  NEW_PRESET   the format follows the name; each must be a new path:\n"
    "                 NAME.vstpreset   VST3 preset file\n"
    "                 NAME.aupreset    Audio Unit preset file\n"
    "                 NAME.preset.lv2  LV2 preset bundle (a folder)\n"
    "  --sample-rate, --input-channels, --output-channels\n"
    "               choose the entry of a configuration list, as the CLI does\n"
    "  --name NAME  the preset's name: every format stores it, and the plug-in window\n"
    "               shows it as its heading; hosts list AU and LV2 presets by it\n"
    "               (at most 200 bytes of UTF-8; default: the first AU or LV2 file\n"
    "               name, or with only .vstpreset outputs the first one's, without\n"
    "               its extension)\n"
    "Options may come before or after the file names.\n"
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
    return utf8_string(stem);
}
// PLUG-54 (jrp's review of 7bd024f): a name given with --name is refused,
// before anything is read or written, when the filter's state cannot hold it
// (storable_preset_name()), instead of being left out without a word. A name
// taken from a file name keeps the documented fallback.
void check_given_name(const std::string& name) {
    if (name.size() > convolver::max_preset_name_bytes)
        throw convolver::detail::AdvisedError<std::invalid_argument>(
            {"the preset name given with --name is " + std::to_string(name.size()) +
                 " bytes long; a preset keeps at most " +
                 std::to_string(convolver::max_preset_name_bytes),
             "Every preset stores its name with the filter, and the plug-in window shows it as "
             "its heading. The name is stored as UTF-8: plain English letters, digits, spaces "
             "and punctuation take one byte each, and letters of other scripts two to four.",
             "Give a shorter name with --name.",
             {}});
    if (convolver::storable_preset_name(name))
        return;
    if (std::any_of(name.begin(), name.end(),
                    [](char c) { return static_cast<unsigned char>(c) < 0x20 || c == 0x7f; }))
        throw convolver::detail::AdvisedError<std::invalid_argument>(
            {"the preset name given with --name contains control characters",
             "Presets cannot store tabs, line breaks or other control characters in a name.",
             "Give a plain name with --name.",
             {}});
    throw convolver::detail::AdvisedError<std::invalid_argument>(
        {"the preset name given with --name cannot be stored",
         "It is not valid Unicode text (UTF-8).",
         "Give a plain name with --name.",
         {}});
}
int make_presets(const fs::path& input, const std::vector<fs::path>& outputs,
                 const convolver::ConfigurationSelection& selection, bool selected,
                 std::string name) {
    std::vector<fs::path> created;
    try {
        if (!name.empty())
            check_given_name(name);
        std::vector<Format> formats;
        for (const auto& output : outputs) {
            if (alias) {
                formats.push_back(Format::vst3);
                continue;
            }
            auto format = convolver::presets::format_for(output);
            if (!format)
                throw convolver::detail::AdvisedError<std::invalid_argument>(
                    {utf8_string(output) + ": the preset format is not known from this name",
                     "convolver-preset chooses the format by the name's ending.",
                     "End the name with .vstpreset (VST3), .aupreset (AU) or .preset.lv2 (an LV2 "
                     "preset folder).",
                     {}});
            formats.push_back(*format);
        }
        convolver::HostState state;
        // PLUG-31: the name a .vstpreset keeps for the window, as a file chosen
        // there gives it (PLUG-22): the file's name with its extension.
        std::string filter_file_name;
        bool from_preset = false; // The alias never reads a .vstpreset.
        if constexpr (!alias)
            from_preset = convolver::presets::format_for(input) == Format::vst3;
        if (from_preset) {
            if (selected)
                throw convolver::detail::AdvisedError<std::invalid_argument>(
                    {"--sample-rate, --input-channels and --output-channels cannot choose from a "
                     ".vstpreset",
                     "A .vstpreset already holds one filter; these options choose an entry from "
                     "a configuration list.",
                     "Leave out those options, or make the preset from the configuration list.",
                     {}});
            try {
                // PLUG-26: the reader the plug-in windows use for a chosen preset.
                auto read = convolver::presets::read_vst3_preset_file(input);
                state = std::move(read.state);
                filter_file_name = std::move(read.filter_name);
            } catch (const std::invalid_argument& e) {
                throw convolver::detail::AdvisedError<std::invalid_argument>(
                    {utf8_string(input) + ": " + advice_of(e).problem,
                     "Only a .vstpreset that holds a Convolver filter can be converted.",
                     "Convert a .vstpreset that a VST3 host saved from Convolver, or make the "
                     "preset from the filter itself (an IR, .cfg or configuration list).",
                     {}});
            }
        } else {
            std::vector<fs::path> raw_filters;
            // The step the plug-ins use for a file chosen in their window (PLUG-9).
            state = convolver::filter_file_state(input, selection,
                                                 convolver::detail::libsndfile_files(),
                                                 &raw_filters, false, !alias);
            // The alias keeps its original output bytes, so it writes no name.
            if (!alias)
                filter_file_name = utf8_string(input.filename());
            // Presets embed the decoded taps, so say what a raw filter was taken to be.
            for (const auto& raw_filter : raw_filters)
                std::cerr << program << "raw filter '" << utf8_string(raw_filter.filename())
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
        // The name hosts show for AU and LV2 presets (hosts show a .vstpreset
        // by its file name): by default the first of those outputs' file
        // names.
        for (std::size_t i = 0; name.empty() && i < outputs.size(); ++i)
            if (formats[i] != Format::vst3)
                name = default_name(outputs[i]);
        // PLUG-53: the preset's own name in its state, the same in every
        // format, which heads the plug-in window: that name, or with only
        // .vstpreset outputs the first one's file name. A name from a file
        // name is left out when it cannot be stored (HostState::preset_name);
        // a --name that cannot was refused above. The alias keeps its
        // original bytes.
        if constexpr (!alias) {
            const auto preset_name =
                name.empty() && !outputs.empty() ? default_name(outputs[0]) : name;
            state.preset_name = convolver::storable_preset_name(preset_name) ? preset_name : "";
        }
        // Check every format and every output before writing any, so a refused
        // one leaves nothing. Each format is checked once and the state encoded
        // once for all of them (PERF-28). The alias leaves existing paths to the
        // exclusive create, as it always did.
        std::optional<convolver::presets::PresetData> data;
        try {
            data.emplace(state, formats, name);
        } catch (const convolver::presets::ChannelLimit& e) {
            if (alias)
                throw;
            throw convolver::detail::AdvisedError<std::invalid_argument>(
                {advice_of(e).problem,
                 "The plug-ins take 1 to 8 channels on each side, so their presets do too.",
                 "Use the command-line tool, convolver, for filters with more channels.",
                 {}});
        }
        if constexpr (!alias) {
            for (const auto& output : outputs) {
                std::error_code error;
                if (fs::symlink_status(output, error).type() != fs::file_type::not_found)
                    throw convolver::detail::AdvisedError<std::invalid_argument>(
                        {utf8_string(output) + " already exists",
                         "convolver-preset writes only to new paths, so it never "
                         "replaces a preset.",
                         "Choose another name, or remove the old preset first.",
                         {}});
            }
        }
        for (std::size_t i = 0; i < outputs.size(); ++i) {
            try {
                data->write(formats[i], outputs[i], filter_file_name);
            } catch (const std::exception& e) {
                if (alias)
                    throw;
                // A failure that already says why keeps its words, which name
                // the path (IO-13: a path too long for Windows).
                if (!advice_of(e).cause.empty())
                    throw;
                std::error_code error;
                throw convolver::detail::AdvisedError<std::runtime_error>(
                    {utf8_string(outputs[i]) + ": " + advice_of(e).problem,
                     fs::is_directory(outputs[i].parent_path().empty() ? fs::path(".")
                                                                       : outputs[i].parent_path(),
                                      error)
                         ? "Its folder exists, but this program could not write there."
                         : "Its folder does not exist.",
                     "Check that the folder exists and is writable, or choose another place.",
                     {}});
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
                      << " preset " << utf8_string(outputs[i]);
            if (formats[i] == Format::lv2) {
                std::cout << " for " << convolver::identity::lv2_base_uri << data->lv2_layout()->id
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
        if (alias) {
            std::cerr << e.what() << '\n';
        } else {
            auto advice = advice_of(e);
            if (!created.empty())
                advice.note += std::string(advice.note.empty() ? "" : " ") +
                               "No presets were kept from this run.";
            std::cerr << cli_error("convolver-preset", advice_text(advice));
        }
        return 1;
    }
}

template <class Char> int run(int argc, Char** argv) {
    const char* text = alias ? alias_usage : usage;
    convolver::ConfigurationSelection selection;
    bool selected = false;
    std::string name;
    int i = 1;
    // Options may come anywhere (the alias: only before the file names, as it
    // always did); an option's value is the next argument.
    std::vector<int> positional;
    for (; i < argc; ++i) {
        auto option = ascii(argv[i]);
        if constexpr (!alias) {
            if (option == "--help" || option == "-h") {
                std::cout << text;
                return 0;
            }
        }
        if (option.rfind("--", 0) != 0) {
            if (alias)
                break;
            positional.push_back(i);
            continue;
        }
        if (i + 1 >= argc) {
            if (alias)
                break;
            const bool known = option == "--sample-rate" || option == "--input-channels" ||
                               option == "--output-channels" || option == "--name";
            std::cerr << cli_error(
                             "convolver-preset",
                             known ? advice_text({option + " needs a value",
                                                  {},
                                                  "Write the value after it, such as " + option +
                                                      (option == "--name"          ? " \"Room\""
                                                       : option == "--sample-rate" ? " 48000"
                                                                                   : " 2") +
                                                      ".",
                                                  {}})
                                   : advice_text({"invalid option: " + option,
                                                  {},
                                                  "Use one of the options listed below.",
                                                  {}}))
                      << text;
            return 2;
        }
        auto value_text = ascii(argv[++i]);
        unsigned long value = 0;
        // The alias has no --name.
        if constexpr (!alias) {
            if (option == "--name") {
                try {
                    name = utf8_string(fs::path(argv[i]));
                } catch (const std::exception&) { // Windows: unpaired UTF-16 surrogates
                    name.clear();
                }
                if (name.empty()) {
                    std::cerr << cli_error("convolver-preset",
                                           advice_text({"--name needs a name in Unicode text",
                                                        {},
                                                        "Write the preset's name after --name.",
                                                        {}}))
                              << text;
                    return 2;
                }
                continue;
            }
        }
        if (option == "--sample-rate" && positive(value_text, convolver::max_sample_rate, value))
            selection.sample_rate = static_cast<int>(value);
        else if (option == "--input-channels" &&
                 positive(value_text, convolver::max_channel_count, value))
            selection.input_channels = value;
        else if (option == "--output-channels" &&
                 positive(value_text, convolver::max_channel_count, value))
            selection.output_channels = value;
        else {
            if (alias) {
                std::cerr << "Invalid option or value: " << option << '\n' << text;
                return 2;
            }
            const bool numeric = option == "--sample-rate" || option == "--input-channels" ||
                                 option == "--output-channels";
            const auto limit = option == "--sample-rate"
                                   ? static_cast<std::size_t>(convolver::max_sample_rate)
                                   : convolver::max_channel_count;
            std::cerr
                << cli_error(
                       "convolver-preset",
                       numeric
                           ? advice_text(
                                 {option + " " + value_text + " is not a whole number from 1 to " +
                                      std::to_string(limit),
                                  {},
                                  "Write a whole number in that range, such "
                                  "as " +
                                      option + (option == "--sample-rate" ? " 48000" : " 2") + ".",
                                  {}})
                           : advice_text({"invalid option: " + option,
                                          {},
                                          "Use one of the options listed below.",
                                          {}}))
                << text;
            return 2;
        }
        selected = true;
    }
    for (; i < argc; ++i) // The alias: everything after the options.
        positional.push_back(i);
    if (alias ? positional.size() != 2 : positional.size() < 2) {
        std::cerr << text;
        return 2;
    }
    std::vector<fs::path> outputs;
    for (std::size_t k = 1; k < positional.size(); ++k)
        outputs.emplace_back(argv[positional[k]]);
    return make_presets(argv[positional.front()], outputs, selection, selected, name);
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    return run(argc, argv);
}
