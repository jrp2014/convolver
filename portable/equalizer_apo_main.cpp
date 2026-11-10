// SPDX-License-Identifier: GPL-2.0-or-later
// convolver-equalizer-apo: write a Convolver filter as an Equalizer APO
// configuration, for system-wide playback on Windows. See docs/use/equalizer-apo.md.
#include "advice.hpp"
#include "convolver/inspection.hpp"
#include "convolver/io.hpp"
#include "convolver_version.hpp"
#include "equalizer_apo.hpp"
#include "file.hpp"
#include "utf8.hpp"
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace {
namespace fs = std::filesystem;
namespace eapo = convolver::equalizer_apo;
using convolver::detail::advice_text;
using convolver::detail::utf8_string;

constexpr const char* usage =
    "Usage: convolver-equalizer-apo INPUT NEW_FOLDER\n"
    "Writes a Convolver filter as an Equalizer APO configuration, so Windows can\n"
    "apply it to everything played through a device (except ASIO and WASAPI\n"
    "exclusive-mode playback, which bypass Equalizer APO).\n"
    "  INPUT       an IR file (an impulse response in a WAV or other sound file),\n"
    "              a .cfg or a configuration list; every entry of a list is exported\n"
    "  NEW_FOLDER  a new folder to create, holding convolver.txt and the\n"
    "              impulse responses as WAV files\n"
    "A device that no exported filter fits (another sample rate or too few\n"
    "channels) is silenced, not played unfiltered.\n"
    "Copy the folder into Equalizer APO's config folder and add a line such as\n"
    "  Include: NEW_FOLDER\\convolver.txt\n"
    "to its config.txt. See docs/use/equalizer-apo.md.\n";

std::string location(const convolver::SourcePosition& p) {
    if (p.file.empty())
        return {};
    auto s = utf8_string(p.file);
    if (p.line)
        s += ':' + std::to_string(p.line);
    return s + ": ";
}

// Every configuration the input can select, in the order Convolver tries them.
std::vector<eapo::Source> load(const fs::path& input) {
    convolver::InspectionOptions options;
    options.all_configurations = true;
    const auto report = convolver::inspect_configuration(input, options);
    if (!report.valid) {
        std::string message = utf8_string(input) + " cannot be exported until these are fixed:";
        for (const auto& d : report.diagnostics)
            if (d.severity == convolver::DiagnosticSeverity::error) {
                // Each diagnostic's own Cause and Fix lines, indented under it.
                std::string text = d.message;
                for (std::size_t at = 0; (at = text.find('\n', at)) != std::string::npos; at += 5)
                    text.replace(at, 1, "\n    ");
                message += "\n  " + location(d.location) + text;
            }
        throw std::runtime_error(message +
                                 "\nFix: Correct each one, then export again; convolver "
                                 "--validate " +
                                 utf8_string(input) + " lists them too.");
    }
    std::vector<eapo::Source> sources;
    for (const auto& c : report.configurations)
        sources.push_back({convolver::load_configuration(c.file), utf8_string(c.file.filename())});
    if (sources.empty())
        throw std::runtime_error(
            advice_text({utf8_string(input) + " holds no configuration to export",
                         "The configuration list names no configuration or IR file.",
                         "Add the filters to the list, or export a .cfg or IR file directly.",
                         {}}));
    return sources;
}

void write_text(const fs::path& path, const std::string& text) {
    const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
    convolver::detail::write_new_file(path, std::span<const std::byte>(bytes, text.size()));
}

int export_folder(const fs::path& input, const fs::path& folder) {
    bool created = false;
    try {
        const auto sources = load(input);
        const auto folder_name = utf8_string(
            folder.filename().empty() ? folder.parent_path().filename() : folder.filename());
        const auto result = eapo::make_export(
            sources, {"Convolver filter for Equalizer APO, written by convolver-equalizer-apo " +
                          std::string(CONVOLVER_VERSION_STRING),
                      "from " + utf8_string(input.filename()) +
                          ". To use it, put this folder in Equalizer APO's",
                      "config folder and add this line to config.txt:",
                      "  Include: " + folder_name + '\\' + eapo::text_file_name});
        if (result.filters.empty())
            throw std::runtime_error("nothing to export from " + utf8_string(input));
        // Equalizer APO builds each IR path in a MAX_PATH buffer and plays a path
        // whose IR it cannot open unfiltered, so refuse names that would not fit
        // in its usual config folder.
        constexpr std::size_t max_path = 259;
        const std::string config_folder = "C:\\Program Files\\EqualizerAPO\\config\\";
        for (const auto& ir : result.impulses)
            if (config_folder.size() + folder_name.size() + 1 + ir.name.size() > max_path)
                throw std::invalid_argument(advice_text(
                    {"the folder name " + folder_name + " is too long for Equalizer APO",
                     "Equalizer APO cannot open files whose full path is over " +
                         std::to_string(max_path) + " characters, such as " + config_folder +
                         folder_name + '\\' + ir.name + ", and would play that path unfiltered.",
                     "Choose a shorter folder name.",
                     {}}));
        std::error_code error;
        if (fs::symlink_status(folder, error).type() != fs::file_type::not_found)
            throw std::invalid_argument(
                advice_text({utf8_string(folder) + " already exists",
                             "convolver-equalizer-apo writes only to a new folder, so it never "
                             "replaces an export.",
                             "Choose another name, or remove the old folder first.",
                             {}}));
        if (!fs::create_directory(folder, error) || error)
            throw std::runtime_error(advice_text(
                {"cannot create " + utf8_string(folder),
                 fs::is_directory(
                     folder.parent_path().empty() ? fs::path(".") : folder.parent_path(), error)
                     ? "Its parent folder exists, but this program could not write there."
                     : "Its parent folder does not exist.",
                 "Check that the parent folder exists and is writable, or choose another place.",
                 {}}));
        created = true;
        for (const auto& ir : result.impulses) {
            convolver::AudioWriter writer(folder / ir.name, {ir.sample_rate, 1},
                                          convolver::OutputFormat::wav_float);
            writer.write(ir.samples);
            writer.close();
        }
        write_text(folder / eapo::text_file_name, result.text);

        std::cout << "Created " << utf8_string(folder) << " with " << eapo::text_file_name
                  << " and " << result.impulses.size()
                  << (result.impulses.size() == 1 ? " impulse response" : " impulse responses")
                  << ".\n";
        for (std::size_t n = 0; n < result.filters.size(); ++n) {
            const auto& f = result.filters[n];
            const auto& c = sources[f.source].configuration;
            std::cout << "Filter " << n + 1 << " (" << sources[f.source].label
                      << ") applies to a device at " << c.sample_rate << " Hz with "
                      << f.device_channels
                      << (f.largest_device > f.device_channels
                              ? " to " + std::to_string(f.largest_device) +
                                    " channels (channels after " +
                                    std::to_string(f.device_channels) + " silent)"
                              : std::string(f.device_channels == 1 ? " channel" : " channels"))
                      << ": " << c.input_channels << " in, " << c.output_channels << " out.\n";
        }
        for (const auto& note : result.skipped)
            std::cout << "Note: " << note << '\n';
        for (std::size_t n = 0; n < result.filters.size(); ++n)
            if (result.filters[n].device_channels == 1)
                std::cout << "Note: filter " << n + 1
                          << " has one channel, so on a stereo device only the left channel\n"
                             "plays (filtered) and the right is silent. To filter both, export a "
                             ".cfg\nthat applies the impulse response to each channel; "
                             "docs/use/equalizer-apo.md shows one.\n";
        std::cout << "To use it: copy the folder into Equalizer APO's config folder (usually\n"
                     "C:\\Program Files\\EqualizerAPO\\config), add the line\n"
                     "  Include: "
                  << folder_name << '\\' << eapo::text_file_name
                  << "\n"
                     "to config.txt there, and set the device's sample rate in Windows sound\n"
                     "settings to one listed above. A device that no filter fits is silenced\n"
                     "rather than played unfiltered. Copy the whole folder: Equalizer APO plays\n"
                     "a path whose IR file it cannot read unfiltered.\n";
        return 0;
    } catch (const std::exception& e) {
        if (created) {
            std::error_code error;
            fs::remove_all(folder, error);
        }
        auto advice = convolver::detail::parse_advice(e.what());
        if (created)
            advice.note +=
                std::string(advice.note.empty() ? "" : " ") + "Nothing was kept from this run.";
        std::cerr << convolver::detail::cli_error("convolver-equalizer-apo", advice_text(advice));
        return 1;
    }
}

template <class Char> int run(int argc, Char** argv) {
    if (argc == 2) {
        const fs::path option(argv[1]);
        if (option == "--help" || option == "-h") {
            std::cout << usage;
            return 0;
        }
        if (option == "--version") {
            std::cout << "convolver-equalizer-apo " << CONVOLVER_VERSION_STRING << '\n';
            return 0;
        }
    }
    if (argc != 3) {
        std::cerr << usage;
        return 2;
    }
    return export_folder(argv[1], argv[2]);
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    return run(argc, argv);
}
