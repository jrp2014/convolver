// SPDX-License-Identifier: GPL-2.0-or-later
// convolver-equalizer-apo: write a Convolver filter as an Equalizer APO
// configuration, for system-wide playback on Windows. See docs/equalizer-apo.md.
#include "convolver/inspection.hpp"
#include "convolver/io.hpp"
#include "convolver_version.hpp"
#include "equalizer_apo.hpp"
#include "file.hpp"
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

constexpr const char* usage =
    "Usage: convolver-equalizer-apo INPUT NEW_FOLDER\n"
    "Writes a Convolver filter as an Equalizer APO configuration, so Windows can\n"
    "apply it to everything played through a device (except ASIO and WASAPI\n"
    "exclusive-mode playback, which bypass Equalizer APO).\n"
    "  INPUT       an impulse response (WAV or another libsndfile format), a .cfg,\n"
    "              or a configuration list; every entry of a list is exported\n"
    "  NEW_FOLDER  a new folder to create, holding convolver.txt and the\n"
    "              impulse responses as WAV files\n"
    "A device that no exported filter fits (another sample rate or too few\n"
    "channels) is silenced, not played unfiltered.\n"
    "Copy the folder into Equalizer APO's config folder and add a line such as\n"
    "  Include: NEW_FOLDER\\convolver.txt\n"
    "to its config.txt. See docs/equalizer-apo.md.\n";

std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
std::string location(const convolver::SourcePosition& p) {
    if (p.file.empty())
        return {};
    auto s = utf8(p.file);
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
        std::string message = utf8(input) + " cannot be exported until these are fixed:";
        for (const auto& d : report.diagnostics)
            if (d.severity == convolver::DiagnosticSeverity::error)
                message += "\n  " + location(d.location) + d.message;
        throw std::runtime_error(message);
    }
    std::vector<eapo::Source> sources;
    for (const auto& c : report.configurations)
        sources.push_back({convolver::load_configuration(c.file), utf8(c.file.filename())});
    if (sources.empty())
        throw std::runtime_error(utf8(input) + " holds no configuration to export");
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
        const auto folder_name =
            utf8(folder.filename().empty() ? folder.parent_path().filename() : folder.filename());
        const auto result = eapo::make_export(
            sources,
            {"Convolver filter for Equalizer APO, written by convolver-equalizer-apo " +
                 std::string(CONVOLVER_VERSION_STRING),
             "from " + utf8(input.filename()) + ". To use it, put this folder in Equalizer APO's",
             "config folder and add this line to config.txt:",
             "  Include: " + folder_name + '\\' + eapo::text_file_name});
        if (result.filters.empty())
            throw std::runtime_error("nothing to export from " + utf8(input));
        std::error_code error;
        if (fs::symlink_status(folder, error).type() != fs::file_type::not_found)
            throw std::invalid_argument(utf8(folder) +
                                        " already exists; the export is only written to a new "
                                        "folder, so choose another name or remove it first");
        if (!fs::create_directory(folder, error) || error)
            throw std::runtime_error("cannot create " + utf8(folder) +
                                     ". Check that its parent folder exists and is writable.");
        created = true;
        for (const auto& ir : result.impulses) {
            convolver::AudioWriter writer(folder / ir.name, {ir.sample_rate, 1},
                                          convolver::OutputFormat::wav_float);
            writer.write(ir.samples);
            writer.close();
        }
        write_text(folder / eapo::text_file_name, result.text);

        std::cout << "Created " << utf8(folder) << " with " << eapo::text_file_name << " and "
                  << result.impulses.size()
                  << (result.impulses.size() == 1 ? " impulse response" : " impulse responses")
                  << ".\n";
        for (std::size_t n = 0; n < result.filters.size(); ++n) {
            const auto& f = result.filters[n];
            const auto& c = sources[f.source].configuration;
            std::cout << "Filter " << n + 1 << " (" << sources[f.source].label
                      << ") applies to a device at " << c.sample_rate << " Hz with "
                      << f.device_channels << (f.device_channels == 1 ? " channel" : " channels")
                      << (f.device_channels < eapo::max_device_channels
                              ? " (or more, up to " + std::to_string(eapo::max_device_channels) +
                                    ", the extra ones silent)"
                              : std::string())
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
                             "docs/equalizer-apo.md shows one.\n";
        std::cout << "To use it: copy the folder into Equalizer APO's config folder (usually\n"
                     "C:\\Program Files\\EqualizerAPO\\config), add the line\n"
                     "  Include: "
                  << folder_name << '\\' << eapo::text_file_name
                  << "\n"
                     "to config.txt there, and set the device's sample rate in Windows sound\n"
                     "settings to one listed above. A device that no filter fits is silenced\n"
                     "rather than played unfiltered.\n";
        return 0;
    } catch (const std::exception& e) {
        if (created) {
            std::error_code error;
            fs::remove_all(folder, error);
        }
        std::cerr << "convolver-equalizer-apo: " << e.what() << '\n';
        if (created)
            std::cerr << "Nothing was kept from this run.\n";
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
