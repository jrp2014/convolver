// SPDX-License-Identifier: GPL-2.0-or-later
#include "advice.hpp"
#include "compare_cli.hpp"
#include "convolver/engine.hpp"
#include "convolver/io.hpp"
#include "convolver_version.hpp"
#include "denormals.hpp"
#include "dither_advice.hpp"
#include "file.hpp"
#include "inspection_cli.hpp"
#include "sound_files.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
constexpr auto help = R"(Convolver portable CLI
Usage: convolver --filter FILTER --input FILE --output FILE [options]
       convolver --tune-fft NEW_WISDOM_FILE --block-size N [--fft-plan MODE]
       convolver --inspect FILTER | --validate FILTER [options]  (see --inspect --help)
       convolver --compare OUTPUT EXPECTED  (see --compare --help)
FILTER is an IR file (an impulse response in a WAV or other sound file), a .cfg
or a configuration list.
  --block-size N       Engine latency in frames, 0 for none (default 256); the
                      output is aligned with the input unless --keep-latency
  --fft-plan MODE      estimate (render default), measure, patient (tune default)
                      or exhaustive
  --fft-time-limit S   Limit each FFTW plan search to S seconds (best plan so
                      far is used); the historical "Take 1 minute" was
                      --fft-plan patient --fft-time-limit 30
  --fft-wisdom FILE    Load FFTW plans; fail if the requested plans are missing
  --tune-fft FILE      Time FFTW plans and save wisdom to a new file, then exit
  --gain-db DB         Output gain in decibels (default 0), or 'auto' for the
                      historical optimum attenuation (peak response to white
                      noise) or 'no-clip' for the filter's worst-case peak
                      bound, lowered for integer output by the room rounding,
                      dither and shaping need, so input within full scale
                      never clips; the chosen gain is reported on stderr
                      exactly, to give again as a number
  --keep-latency       Keep the leading block of algorithmic latency
  --output-format F   CONTAINER-TYPE, default wav-float. Containers: wav, rf64,
                      w64, aiff, caf (rf64, w64 and caf have no 4 GiB limit),
                      flac (lossless; pcm8, pcm16, pcm24 only; needs a
                      libsndfile with FLAC) and raw
                      (headerless little-endian; no pcm8). Types: pcm8,
                      pcm16, pcm24, pcm32, float, double. Example: flac-pcm24
  --speaker-map config
                      Label WAV or RF64 output channels with the configuration's
                      channel mask (WAVE_FORMAT_EXTENSIBLE). wav- and rf64-
                      formats only.
  --dither D           Integer PCM only: triangular, triangular-highpass or
                      none. Default triangular below 20 significant bits
                      (pcm8, pcm16), none from 20 bits, where it is inaudible
  --dither-seed N      Dither generator seed (default 0); output is reproducible
  --noise-shaping S    Integer PCM only: none (default), simple, third-order or
                      ninth-order (error feedback). Convolver says when another
                      choice is clearly less audible at the sample rate
  --valid-bits N       Integer PCM only: quantize to N significant bits (8 up to
                      the container), stored left-justified, e.g. 20 in wav-pcm24
  --output-channels N  From a configuration list, select the first entry with N
                      output channels (as well as the input rate and channels)
  --raw-input RATE CHANNELS
                      Read headerless samples with explicit metadata
  --raw-format F       Sample type of --raw-input: f32le (default), f64le,
                      s16le, s24le, s32le, or the same ending in be
  --raw-filter-format F
                      Sample type of headerless .pcm/.raw filters in a .cfg:
                      f32le (default, as DRC and BruteFIR write them) or any
                      --raw-format name; they are always mono
  --help              Show this help
  --version           Show Convolver, source, FFT backend, CPU kernel and libsndfile versions
FILE may be '-' for stdin/stdout; standard streams must be raw (--raw-input,
a raw- output format).
Files retain the entire FIR/routing tail. Algorithmic latency is trimmed unless
--keep-latency is used. Sample-rate/channel mismatches are errors; no resampling.
)";
// DIAG-2: an option the command line cannot use, with its cause and fix.
[[noreturn]] void bad_option(std::string problem, std::string cause, std::string fix) {
    throw std::invalid_argument(
        convolver::detail::advice_text({std::move(problem), std::move(cause), std::move(fix), {}}));
}
// `option` names the value in the problem ("--block", "--raw-input's sample
// rate"); `example` is the fix's example, by default `option` followed by 2.
std::size_t count(std::string_view s, std::string_view option, std::string_view example = {},
                  std::size_t least = 1) {
    std::size_t n = 0;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (error != std::errc{} || end != s.data() + s.size() || n < least)
        bad_option(std::string(option) + " " + std::string(s) +
                       " is not a whole number of at least " + std::to_string(least),
                   {},
                   "Write a whole number, such as " +
                       (example.empty() ? std::string(option) + " 2" : std::string(example)) + ".");
    return n;
}
// A decimal number in the C locale, or nothing (std::stod's only message is
// its own name).
std::optional<double> number(const std::string& text) {
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    double value = 0;
    if (!(stream >> value) || !stream.eof())
        return std::nullopt;
    return value;
}
// The fewest decimals that number() reads back as the same double, so a
// reported gain can be given to --gain-db exactly.
std::string exact_decimal(double value) {
    for (int decimals = 2; decimals <= 20; ++decimals) {
        std::ostringstream text;
        text.imbue(std::locale::classic());
        text << std::fixed << std::setprecision(decimals) << value;
        if (number(text.str()) == value)
            return text.str();
    }
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::setprecision(17) << value;
    return text.str();
}
// GAIN-3: the no-clip gain's allowance for the convolution's float rounding,
// relative to the bound: 2^-16 (0.00013 dB), about 100 times the largest
// overshoot measured at the bound (1.2e-7 of full scale, with 65,536 and
// 262,144 random taps at blocks of 64 to 16,384 frames).
constexpr double no_clip_rounding = 1.0 / 65536;
// CONTAINER-TYPE, for example wav-pcm24 or flac-pcm16.
std::optional<convolver::OutputEncoding> parse_output_format(std::string_view name) {
    using convolver::Container;
    using convolver::SampleType;
    const auto dash = name.find('-');
    if (dash == std::string_view::npos)
        return std::nullopt;
    constexpr std::pair<std::string_view, Container> containers[]{
        {"wav", Container::wav},   {"rf64", Container::rf64}, {"w64", Container::w64},
        {"aiff", Container::aiff}, {"caf", Container::caf},   {"flac", Container::flac},
        {"raw", Container::raw}};
    constexpr std::pair<std::string_view, SampleType> samples[]{
        {"pcm8", SampleType::pcm8},     {"pcm16", SampleType::pcm16},
        {"pcm24", SampleType::pcm24},   {"pcm32", SampleType::pcm32},
        {"float", SampleType::float32}, {"double", SampleType::float64}};
    convolver::OutputEncoding result;
    bool container = false, sample = false;
    for (const auto& [text, value] : containers)
        if (name.substr(0, dash) == text) {
            result.container = value;
            container = true;
        }
    for (const auto& [text, value] : samples)
        if (name.substr(dash + 1) == text) {
            result.sample = value;
            sample = true;
        }
    if (!container || !sample || !convolver::supported(result))
        return std::nullopt;
    return result;
}
std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    if (a == "-" || b == "-")
        return false;
    std::error_code ec;
    if (std::filesystem::equivalent(a, b, ec) && !ec)
        return true;
    return std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b);
}
bool same_stdin(const std::filesystem::path& output) {
    if (output == "-")
        return false; // Shell redirection has already opened stdout.
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION input_info{}, output_info{};
    const auto input = reinterpret_cast<HANDLE>(_get_osfhandle(0));
    if (!GetFileInformationByHandle(input, &input_info))
        return false;
    const auto target = CreateFileW(output.c_str(), FILE_READ_ATTRIBUTES,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
    if (target == INVALID_HANDLE_VALUE)
        return false;
    const bool valid = GetFileInformationByHandle(target, &output_info) != 0;
    CloseHandle(target);
    return valid && input_info.dwVolumeSerialNumber == output_info.dwVolumeSerialNumber &&
           input_info.nFileIndexHigh == output_info.nFileIndexHigh &&
           input_info.nFileIndexLow == output_info.nFileIndexLow;
#else
    struct stat input_info{}, output_info{};
    return fstat(STDIN_FILENO, &input_info) == 0 && S_ISREG(input_info.st_mode) &&
           stat(output.c_str(), &output_info) == 0 && input_info.st_dev == output_info.st_dev &&
           input_info.st_ino == output_info.st_ino;
#endif
}
std::string read_wisdom(const std::filesystem::path& path) {
    if (path == "-")
        bad_option("--fft-wisdom cannot read standard input", {},
                   "Give the path of a wisdom file that --tune-fft saved.");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error(convolver::detail::advice_text(
            {"cannot open the FFTW wisdom file " + path.string(),
             std::filesystem::exists(path) ? "The file is there, but this program may not read it."
                                           : "There is no file at that path.",
             "Check the path, or save wisdom first with --tune-fft.",
             {}}));
    constexpr std::size_t limit = 1024 * 1024;
    std::string text(limit + 1, '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (file.bad())
        throw std::runtime_error("cannot read FFTW wisdom file");
    text.resize(static_cast<std::size_t>(file.gcount()));
    if (text.size() > limit)
        throw std::runtime_error("FFTW wisdom file exceeds 1 MiB");
    return text;
}
} // namespace
int run(int argc, char** argv) {
    if (convolver::cli::is_comparison(argc, argv))
        return convolver::cli::run_comparison(argc, argv);
    if (convolver::cli::is_inspection(argc, argv))
        return convolver::cli::run_inspection(argc, argv);
    try {
#ifdef SIGPIPE
        std::signal(SIGPIPE, SIG_IGN); // Surface closed consumers as I/O failures.
#endif
        std::filesystem::path filter, input, output;
        std::filesystem::path wisdom, tune;
        convolver::FftOptions fft_options;
        bool explicit_plan = false, audio_options = false;
        std::optional<convolver::RawSpec> raw;
        convolver::OutputEncoding format;
        convolver::RawEncoding raw_encoding, raw_filter_encoding;
        std::optional<convolver::Dither> dither;
        std::optional<std::uint64_t> dither_seed;
        std::optional<convolver::NoiseShaping> shaping;
        unsigned valid_bits = 0;
        std::size_t block = 256, output_channels = 0;
        float gain = 1;
        std::optional<std::string> automatic_gain;
        bool keep = false, speaker_map = false, raw_format = false, raw_filter_format = false;
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc)
                    bad_option(option + " needs a value", "It is the last argument.",
                               "Write its value after it; convolver --help lists each option.");
                return argv[i];
            };
            if (option == "--help") {
                std::cout << help;
                return 0;
            }
            if (option == "--version") {
                std::cout << "Convolver portable " << CONVOLVER_VERSION_STRING
                          << "\nFFT backend: " << convolver::fft_backend_name() << " / "
                          << convolver::fft_backend_version()
                          << "\nCPU kernels: " << convolver::cpu_kernels_name() << '\n'
                          << convolver::sndfile_version()
                          << (convolver::available(convolver::Container::flac)
                                  ? " (FLAC output available)\n"
                                  : " (no FLAC output: built without FLAC)\n");
                return 0;
            }
            if (option == "--filter")
                filter = utf8_path(value());
            else if (option == "--input")
                input = utf8_path(value());
            else if (option == "--output")
                output = utf8_path(value());
            else if (option == "--fft-wisdom")
                wisdom = utf8_path(value());
            else if (option == "--tune-fft")
                tune = utf8_path(value());
            else if (option == "--fft-plan") {
                explicit_plan = true;
                const auto mode = value();
                if (mode == "estimate")
                    fft_options.planning = convolver::FftPlanning::estimate;
                else if (mode == "measure")
                    fft_options.planning = convolver::FftPlanning::measure;
                else if (mode == "patient")
                    fft_options.planning = convolver::FftPlanning::patient;
                else if (mode == "exhaustive")
                    fft_options.planning = convolver::FftPlanning::exhaustive;
                else
                    bad_option("--fft-plan " + mode + " is not a planning mode", {},
                               "Use estimate, measure, patient or exhaustive.");
            } else if (option == "--fft-time-limit") {
                const auto text = value();
                const auto seconds = number(text);
                if (!seconds || !(*seconds > 0) || *seconds > 86400)
                    bad_option("--fft-time-limit " + text + " is not a time limit",
                               "It takes a number of seconds above 0 and at most 86400 (a day).",
                               "Write a number of seconds, such as --fft-time-limit 60.");
                fft_options.time_limit_seconds = *seconds;
            } else if (option == "--output-channels") {
                audio_options = true;
                output_channels = count(value(), option);
                if (output_channels > convolver::max_channel_count)
                    bad_option("--output-channels " + std::to_string(output_channels) +
                                   " is too many",
                               "Convolver handles 1 to " +
                                   std::to_string(convolver::max_channel_count) + " channels.",
                               "Ask for at most " + std::to_string(convolver::max_channel_count) +
                                   " channels.");
            } else if (option == "--block-size")
                block = count(value(), option, "--block-size 256", 0); // 0: zero latency.
            else if (option == "--keep-latency") {
                keep = true;
                audio_options = true;
            } else if (option == "--raw-input") {
                audio_options = true;
                const auto rate =
                    count(value(), "--raw-input's sample rate", "--raw-input 48000 2");
                const auto channels =
                    count(value(), "--raw-input's channel count", "--raw-input 48000 2");
                if (rate > static_cast<std::size_t>(convolver::max_sample_rate) ||
                    channels > convolver::max_channel_count)
                    bad_option("--raw-input " + std::to_string(rate) + " " +
                                   std::to_string(channels) + " is beyond Convolver's limits",
                               "Convolver handles rates up to " +
                                   std::to_string(convolver::max_sample_rate) + " Hz and up to " +
                                   std::to_string(convolver::max_channel_count) + " channels.",
                               "Check the rate and channel count, such as --raw-input 48000 2.");
                raw = convolver::RawSpec{static_cast<int>(rate), channels};
            } else if (option == "--raw-format") {
                audio_options = true;
                const auto name = value();
                const auto parsed = convolver::parse_raw_encoding(name);
                if (!parsed)
                    bad_option("unsupported raw format: " + name, {},
                               "Use s16le, s24le, s32le, f32le or f64le, or the same with be for "
                               "big-endian.");
                raw_encoding = *parsed;
                raw_format = true;
            } else if (option == "--raw-filter-format") {
                audio_options = true;
                const auto name = value();
                const auto parsed = convolver::parse_raw_encoding(name);
                if (!parsed)
                    bad_option("unsupported raw filter format: " + name, {},
                               "Use s16le, s24le, s32le, f32le or f64le, or the same with be for "
                               "big-endian.");
                raw_filter_encoding = *parsed;
                raw_filter_format = true;
            } else if (option == "--gain-db") {
                audio_options = true;
                auto text = value();
                if (text == "auto" || text == "no-clip") {
                    automatic_gain = text;
                    continue;
                }
                automatic_gain.reset();
                const auto db = number(text);
                if (!db)
                    bad_option("--gain-db " + text + " is not a gain",
                               "--gain-db takes a number of decibels (such as -6), 'no-clip' or "
                               "'auto'.",
                               "Write the gain as a number, such as --gain-db -6, or use "
                               "--gain-db no-clip to keep full-scale input within full scale.");
                const double linear = std::pow(10., *db / 20.);
                if (!std::isfinite(*db) || !std::isfinite(linear) ||
                    linear > std::numeric_limits<float>::max())
                    bad_option("--gain-db " + text + " is too large",
                               "The gain must fit in a 32-bit float, so at most about 770 dB.",
                               "Use a smaller gain, such as --gain-db -6, or --gain-db no-clip.");
                gain = static_cast<float>(linear);
            } else if (option == "--speaker-map") {
                audio_options = true;
                if (const auto map = value(); map != "config")
                    bad_option("--speaker-map " + map + " is not a speaker map",
                               "Convolver can only label the output with the speakers in the "
                               "configuration's channel mask.",
                               "Write --speaker-map config.");
                speaker_map = true;
            } else if (option == "--output-format") {
                audio_options = true;
                const auto name = value();
                const auto parsed = parse_output_format(name);
                if (!parsed)
                    bad_option("unsupported output format: " + name,
                               "An output format is CONTAINER-TYPE: wav, rf64, w64, aiff, caf or "
                               "raw with pcm16, pcm24, pcm32, float or double (wav, rf64, w64, "
                               "aiff and caf also pcm8), or flac with pcm8, pcm16 or pcm24.",
                               "Use one of those, such as --output-format wav-pcm24.");
                if (!convolver::available(parsed->container))
                    throw std::invalid_argument(convolver::detail::advice_text(
                        {"this build of Convolver cannot write FLAC",
                         "Its libsndfile was built without FLAC support.",
                         "Choose another container, such as wav or rf64, and convert to FLAC "
                         "afterwards if you need it (for example with flac).",
                         {}}));
                format = *parsed;
            } else if (option == "--dither") {
                audio_options = true;
                dither = convolver::cli::parse_dither(value());
            } else if (option == "--noise-shaping") {
                audio_options = true;
                shaping = convolver::cli::parse_noise_shaping(value());
            } else if (option == "--valid-bits") {
                audio_options = true;
                const auto bits = count(value(), option);
                if (bits < 8 || bits > 32)
                    bad_option("--valid-bits " + std::to_string(bits) + " is out of range", {},
                               "Use 8 to 32 bits.");
                valid_bits = static_cast<unsigned>(bits);
            } else if (option == "--dither-seed") {
                audio_options = true;
                const auto text = value();
                std::uint64_t seed = 0;
                const auto [end, error] =
                    std::from_chars(text.data(), text.data() + text.size(), seed);
                if (error != std::errc{} || end != text.data() + text.size())
                    bad_option("--dither-seed " + text + " is not a seed", {},
                               "Use a whole number of 0 or more, such as --dither-seed 1.");
                dither_seed = seed;
            } else
                bad_option("unknown option: " + option, {},
                           "Check its spelling; convolver --help lists every option.");
        }
        if (speaker_map && format.container != convolver::Container::wav &&
            format.container != convolver::Container::rf64)
            bad_option("--speaker-map needs a wav- or rf64- output format",
                       format.container == convolver::Container::raw
                           ? "Headerless raw output has nowhere to keep speaker labels."
                       : format.container == convolver::Container::caf ||
                               format.container == convolver::Container::aiff
                           ? "CAF and AIFF files label speakers with Apple's layouts, which "
                             "cannot keep WAVE's back and side speakers apart, so the labels "
                             "would change meaning."
                           : "libsndfile, which writes the file, cannot store speaker labels in "
                             "this format.",
                       "Use a wav or rf64 output format, such as --output-format wav-float or "
                       "rf64-float, or leave out --speaker-map.");
        if (raw_format && !raw)
            bad_option("--raw-format needs --raw-input RATE CHANNELS",
                       "--raw-format says how headerless input samples are stored, which only "
                       "applies with --raw-input.",
                       "Add --raw-input with the rate and channel count, such as --raw-input "
                       "48000 2, or leave out --raw-format.");
        const auto bits = convolver::container_bits(format.sample);
        const auto pcm_only = [](const std::string& what) {
            bad_option(what + " applies only to integer PCM output",
                       "Float output keeps every sample exactly, so there is nothing to dither "
                       "or shape.",
                       "Choose an integer output format, such as --output-format wav-pcm16, or "
                       "leave out " +
                           what + ".");
        };
        if (!bits && dither && *dither != convolver::Dither::none)
            pcm_only("--dither");
        if (!bits && shaping && *shaping != convolver::NoiseShaping::none)
            pcm_only("--noise-shaping");
        if (valid_bits && !bits)
            pcm_only("--valid-bits");
        if (valid_bits && valid_bits > bits)
            bad_option(
                "--valid-bits " + std::to_string(valid_bits) + " exceeds the output container's " +
                    std::to_string(bits) + " bits",
                {}, "Use at most " + std::to_string(bits) + " valid bits, or a larger container.");
        if ((!wisdom.empty() || !tune.empty()) && !convolver::fft_supports_wisdom())
            bad_option("tuning and wisdom require the FFTW backend",
                       std::string("This build uses ") + convolver::fft_backend_name() +
                           ", which plans its transforms itself.",
                       "Leave out --tune-fft and --fft-wisdom, or use a build with FFTW "
                       "(convolver --version names the FFT backend).");
        if (fft_options.time_limit_seconds > 0 &&
            fft_options.planning == convolver::FftPlanning::estimate && tune.empty())
            bad_option("--fft-time-limit needs --fft-plan measure, patient or exhaustive",
                       "estimate, the default, takes no time to plan.",
                       "Add --fft-plan measure, or leave out --fft-time-limit.");
        if (!tune.empty()) {
            if (tune == "-" || !filter.empty() || !input.empty() || !output.empty() ||
                !wisdom.empty() || audio_options)
                bad_option("--tune-fft needs a new file path and accepts only --block-size, "
                           "--fft-plan and --fft-time-limit",
                           "It only measures FFT plans and saves them; it filters nothing.",
                           "Run --tune-fft on its own, then pass the file to --fft-wisdom when "
                           "filtering.");
            // Reject existing targets before the potentially lengthy calibration.
            if (std::filesystem::exists(std::filesystem::symlink_status(tune)))
                bad_option(tune.string() + " already exists", "--tune-fft never replaces a file.",
                           "Choose a new file name, or remove the old file first.");
            if (!explicit_plan)
                fft_options.planning = convolver::FftPlanning::patient;
            convolver::tune_fft(block, fft_options);
            const auto wisdom_text = convolver::export_fft_wisdom();
            convolver::detail::write_new_file(tune, std::as_bytes(std::span(wisdom_text)));
            std::cout << "Saved FFTW wisdom for every transform size the engine uses, "
                         "which serves every --block-size (forward and inverse plans)\n";
            return 0;
        }
        if (filter.empty() || input.empty() || output.empty())
            bad_option(std::string(filter.empty()  ? "--filter"
                                   : input.empty() ? "--input"
                                                   : "--output") +
                           " is missing",
                       "Filtering needs a filter, an input and an output.",
                       "Give all three, such as: convolver --filter room.cfg --input in.wav "
                       "--output out.wav (convolver --help lists the rest).");
        if (same_file(input, output) || same_file(filter, output) ||
            (input == "-" && same_stdin(output)))
            bad_option("the output is the same file as the " +
                           std::string(same_file(filter, output) ? "filter" : "input"),
                       "Convolver writes the output while it still reads the input and filter, "
                       "so writing over either would destroy it.",
                       "Choose another output name.");
        if (!wisdom.empty()) {
            if (same_file(wisdom, output))
                bad_option("the output is the same file as the FFTW wisdom file", {},
                           "Choose another output name.");
            convolver::import_fft_wisdom(read_wisdom(wisdom));
            fft_options.wisdom_only = true;
        }
        convolver::AudioReader reader(input, raw, raw_encoding);
        const auto info = reader.info();
        std::vector<std::filesystem::path> dependencies, raw_filters;
        const auto config = convolver::load_configuration(
            filter,
            convolver::ConfigurationSelection{info.sample_rate, info.channels, output_channels,
                                              raw_filter_encoding},
            &dependencies, &raw_filters);
        // A raw filter's sample type is an assumption, so say which one was used.
        // Names are relative to the configuration's folder, so two filters with
        // the same file name in different folders can be told apart.
        for (const auto& raw_filter : raw_filters) {
            std::error_code ec;
            auto shown = std::filesystem::relative(
                raw_filter,
                filter.parent_path().empty() ? std::filesystem::path(".") : filter.parent_path(),
                ec);
            if (ec || shown.empty())
                shown = raw_filter;
            const auto name = shown.generic_u8string();
            std::cerr << "convolver: raw filter '" << std::string(name.begin(), name.end())
                      << "' read as mono " << convolver::raw_encoding_name(raw_filter_encoding)
                      << " at " << config.sample_rate << " Hz"
                      << (raw_filter_format ? ""
                                            : " (choose another sample type with "
                                              "--raw-filter-format)")
                      << '\n';
        }
        if (raw_filter_format && raw_filters.empty())
            std::cerr << "convolver: --raw-filter-format has no effect: the configuration "
                         "has no headerless .pcm or .raw filters\n";
        for (const auto& source : dependencies)
            if (same_file(source, output))
                bad_option("the output is the same file as " + source.string(),
                           "The configuration reads that file, so writing over it would destroy "
                           "the filter.",
                           "Choose another output name.");
        // TPDF dither by default below 20 significant bits, and advice on
        // pointless or clearly beaten choices (dither_advice.hpp).
        const auto plan = convolver::cli::plan_quantization(bits, valid_bits, info.sample_rate,
                                                            dither, shaping, dither_seed);
        for (const auto& note : plan.notes)
            std::cerr << "convolver: " << note << '\n';
        const auto& quantization = plan.options;
        if (automatic_gain) {
            const bool safe = *automatic_gain == "no-clip";
            // GAIN-2: no-clip needs only the bound, which takes no convolution
            // (about 0.4 ms rather than 70 ms for 65,536 stereo taps); auto
            // filters noise. A zero peak (silent routing) gives 0 dB.
            double peak = 0;
            if (safe) {
                convolver::validate_configuration(config);
                peak = convolver::worst_case_peak(config);
                // As estimate_attenuation() refuses a noise response that is
                // not finite in single precision.
                if (!(peak <= std::numeric_limits<float>::max()))
                    throw std::overflow_error(convolver::detail::advice_text(
                        {"cannot choose the no-clip gain: the filter's largest possible peak "
                         "is too large for single-precision processing",
                         "The filter coefficients are far above full scale.",
                         "Check how the filter files are scaled.",
                         {}}));
            } else {
                peak = convolver::estimate_attenuation(config).noise_peak;
            }
            auto db = peak > 0 ? -20 * std::log10(peak) : 0.;
            // GAIN-3: for integer output, no-clip also leaves room for
            // rounding, dither and noise shaping (H steps, io.hpp) and for the
            // convolution's float rounding, so input within full scale never
            // clips. Float output keeps the bound alone.
            double headroom_db = 0;
            const unsigned significant = quantization.valid_bits ? quantization.valid_bits : bits;
            if (safe && bits && peak > 0) {
                const double limit = 1 - convolver::quantization_headroom(quantization) /
                                             std::ldexp(1.0, static_cast<int>(significant) - 1);
                headroom_db = -20 * std::log10(limit / (1 + no_clip_rounding));
                db -= headroom_db;
            }
            gain = static_cast<float>(std::pow(10., db / 20.));
            if (!std::isfinite(gain))
                throw std::invalid_argument(convolver::detail::advice_text(
                    {"the estimated gain is not representable",
                     "The filter's peak is so small or large that the gain to match it does "
                     "not fit in a 32-bit float.",
                     "Check the IRs (one may be silent or damaged), or give --gain-db as a number.",
                     {}}));
            // Printed exactly, so that --gain-db with this figure gives the
            // same gain and the same file.
            std::cerr << "convolver: " << (safe ? "no-clip" : "optimum") << " attenuation "
                      << exact_decimal(db) << " dB ("
                      << (safe ? "largest possible peak " : "white-noise peak ") << std::fixed
                      << std::setprecision(4) << peak;
            if (headroom_db > 0) {
                const bool dithered = quantization.dither != convolver::Dither::none;
                const bool shaped = quantization.noise_shaping != convolver::NoiseShaping::none;
                std::cerr << ", including " << std::setprecision(4) << headroom_db
                          << " dB of headroom for " << significant << "-bit output"
                          << (dithered && shaped ? " with dither and noise shaping"
                              : dithered         ? " with dither"
                              : shaped           ? " with noise shaping"
                                                 : "");
            }
            std::cerr << ")\n";
        }
        convolver::Engine engine(config, block, fft_options);
        convolver::OutputMetadata metadata;
        if (speaker_map) {
            metadata.channel_mask = config.channel_mask;
            if (!metadata.channel_mask)
                std::cerr << (format.container == convolver::Container::rf64
                                  ? "convolver: the configuration declares no speaker mask; the "
                                    "output gets libsndfile's default RF64 header, which may "
                                    "label the channels\n"
                                  : "convolver: the configuration declares no speaker mask; the "
                                    "output channels stay unlabelled\n");
        }
        const auto open_output = [&] {
            try {
                return convolver::AudioWriter(output, {info.sample_rate, engine.output_channels()},
                                              format, quantization, metadata);
            } catch (const std::invalid_argument& e) {
                if (!metadata.channel_mask)
                    throw;
                std::ostringstream text;
                text << "--speaker-map: the configuration's channel mask 0x" << std::hex
                     << metadata.channel_mask << " cannot label this output: " << e.what();
                throw std::invalid_argument(text.str());
            }
        };
        auto writer = open_output();
        // DIAG-1: from here on a failure leaves a partial output, so say so
        // (output safety in the command-line guide).
        try {
            // PERF-15: the whole render (Engine::process, --gain-db and
            // quantization) flushes subnormals to zero, as the plug-ins do.
            const convolver::detail::ScopedFlushDenormals flush;
            constexpr std::size_t chunk = 4096;
            const auto channels = engine.output_channels();
            std::vector<float> in(chunk * engine.input_channels()), out(chunk * channels);
            std::size_t skip = keep ? 0 : engine.latency_frames();
            std::uint64_t written = 0; // Output frames so far, for messages.
            auto emit = [&](std::size_t frames) {
                const auto drop = std::min(skip, frames);
                skip -= drop;
                auto samples =
                    std::span<float>(out).subspan(drop * channels, (frames - drop) * channels);
                for (std::size_t i = 0; i < samples.size(); ++i) {
                    samples[i] *= gain;
                    if (!std::isfinite(samples[i]))
                        throw std::runtime_error(convolver::detail::advice_text(
                            {"the output overflowed " +
                                 convolver::detail::sample_position(
                                     written + i / channels, i % channels + 1, info.sample_rate),
                             "The sample there is too large for a 32-bit float (infinite, or "
                             "not a number): the filter's gain, --gain-db or the input's level "
                             "is too high.",
                             "Lower --gain-db, or use '--gain-db no-clip', which keeps "
                             "full-scale input within full scale.",
                             {}}));
                }
                written += frames - drop;
                if (!samples.empty())
                    writer.write(samples);
            };
            bool any = false;
            while (const auto frames = reader.read(in)) {
                any = true;
                engine.process(std::span<const float>(in).first(frames * engine.input_channels()),
                               out, frames);
                emit(frames);
            }
            if (any) {
                auto remaining = engine.latency_frames() + engine.tail_frames();
                while (remaining) {
                    const auto n = std::min(chunk, remaining);
                    engine.process({}, out, n);
                    emit(n);
                    remaining -= n;
                }
            }
            reader.close();
            writer.close();
        } catch (const std::exception& error) {
            auto advice = convolver::detail::parse_advice(error.what());
            const auto incomplete =
                (output == "-" ? std::string("the audio written to standard output")
                               : output.string()) +
                " is incomplete: it holds only the audio written before this failure, so do not "
                "use it.";
            advice.note = advice.note.empty() ? incomplete : advice.note + " " + incomplete;
            std::cerr << convolver::detail::cli_error("convolver",
                                                      convolver::detail::advice_text(advice));
            return 1;
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << convolver::detail::cli_error("convolver", error.what());
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> arguments;
        std::vector<char*> pointers;
        arguments.reserve(argc);
        pointers.reserve(argc);
        for (int i = 0; i < argc; ++i) {
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1,
                                                 nullptr, 0, nullptr, nullptr);
            if (!size)
                throw std::runtime_error("invalid Unicode command line");
            std::string text(static_cast<std::size_t>(size), '\0');
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, text.data(), size,
                                     nullptr, nullptr))
                throw std::runtime_error("cannot decode command line");
            text.pop_back();
            arguments.push_back(std::move(text));
        }
        for (auto& text : arguments)
            pointers.push_back(text.data());
        return run(argc, pointers.data());
    } catch (const std::exception& error) {
        std::cerr << convolver::detail::cli_error("convolver", error.what());
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    return run(argc, argv);
}
#endif
