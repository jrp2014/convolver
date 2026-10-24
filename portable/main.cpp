// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "convolver/io.hpp"
#include "convolver_version.hpp"
#include "file.hpp"
#include "inspection_cli.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
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
Usage: convolver --filter IR_OR_CFG --input FILE --output FILE [options]
       convolver --tune-fft NEW_WISDOM_FILE --block-size N [--fft-plan MODE]
       convolver --inspect CONFIG | --validate CONFIG [options]  (see --inspect --help)
  --block-size N       Partition payload in frames (default 256)
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
                      bound (input within full scale, no dither or shaping
                      headroom); the chosen value is reported on stderr
  --keep-latency       Keep the leading block of algorithmic latency
  --output-format F   CONTAINER-TYPE, default wav-float. Containers: wav, rf64,
                      w64, aiff, caf (rf64, w64 and caf have no 4 GiB limit),
                      flac (lossless; pcm8, pcm16, pcm24 only; needs a
                      libsndfile with FLAC) and raw
                      (headerless little-endian; no pcm8). Types: pcm8,
                      pcm16, pcm24, pcm32, float, double. Example: flac-pcm24
  --speaker-map config
                      Label WAV output channels with the configuration's channel
                      mask (WAVE_FORMAT_EXTENSIBLE). wav- formats only.
  --dither D           Integer PCM only: none (default), triangular,
                      triangular-highpass or rectangular
  --dither-seed N      Dither generator seed (default 0); output is reproducible
  --noise-shaping S    Integer PCM only: none (default), simple, second-order,
                      third-order, ninth-order or sony-sbm (error feedback)
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
  --version           Show Convolver, source, FFT backend and libsndfile versions
FILE may be '-' for stdin/stdout; standard streams must be raw (--raw-input,
a raw- output format).
Files retain the entire FIR/routing tail. Algorithmic latency is trimmed unless
--keep-latency is used. Sample-rate/channel mismatches are errors; no resampling.
)";
std::size_t count(std::string_view s) {
    std::size_t n = 0;
    const auto [end, error] = std::from_chars(s.data(), s.data() + s.size(), n);
    if (error != std::errc{} || end != s.data() + s.size() || n == 0)
        throw std::invalid_argument("expected a positive integer: " + std::string(s));
    return n;
}
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
        throw std::invalid_argument("wisdom requires a file path");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open FFTW wisdom file");
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
        std::uint64_t dither_seed = 0;
        convolver::NoiseShaping shaping = convolver::NoiseShaping::none;
        unsigned valid_bits = 0;
        std::size_t block = 256, output_channels = 0;
        float gain = 1;
        std::optional<std::string> automatic_gain;
        bool keep = false, speaker_map = false, raw_format = false, raw_filter_format = false;
        for (int i = 1; i < argc; ++i) {
            std::string option = argv[i];
            auto value = [&]() -> std::string {
                if (++i >= argc)
                    throw std::invalid_argument("missing value for " + option);
                return argv[i];
            };
            if (option == "--help") {
                std::cout << help;
                return 0;
            }
            if (option == "--version") {
                std::cout << "Convolver portable " << CONVOLVER_VERSION_STRING
                          << "\nFFT backend: " << convolver::fft_backend_name() << " / "
                          << convolver::fft_backend_version() << '\n'
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
                    throw std::invalid_argument(
                        "FFT planning mode must be estimate, measure, patient or exhaustive");
            } else if (option == "--fft-time-limit") {
                const auto text = value();
                std::size_t end = 0;
                const double seconds = std::stod(text, &end);
                if (end != text.size() || !(seconds > 0) || seconds > 86400)
                    throw std::invalid_argument(
                        "--fft-time-limit must be a number of seconds, above 0 and at most 86400");
                fft_options.time_limit_seconds = seconds;
            } else if (option == "--output-channels") {
                audio_options = true;
                output_channels = count(value());
                if (output_channels > 256)
                    throw std::invalid_argument("--output-channels must be 1..256");
            } else if (option == "--block-size")
                block = count(value());
            else if (option == "--keep-latency") {
                keep = true;
                audio_options = true;
            } else if (option == "--raw-input") {
                audio_options = true;
                const auto rate = count(value());
                const auto channels = count(value());
                if (rate > 768000 || channels > 256)
                    throw std::invalid_argument("RAW metadata exceeds supported limits");
                raw = convolver::RawSpec{static_cast<int>(rate), channels};
            } else if (option == "--raw-format") {
                audio_options = true;
                const auto name = value();
                const auto parsed = convolver::parse_raw_encoding(name);
                if (!parsed)
                    throw std::invalid_argument("unsupported raw format: " + name +
                                                " (use s16le, s24le, s32le, f32le or f64le, or "
                                                "the same with be for big-endian)");
                raw_encoding = *parsed;
                raw_format = true;
            } else if (option == "--raw-filter-format") {
                audio_options = true;
                const auto name = value();
                const auto parsed = convolver::parse_raw_encoding(name);
                if (!parsed)
                    throw std::invalid_argument("unsupported raw filter format: " + name +
                                                " (use s16le, s24le, s32le, f32le or f64le, or "
                                                "the same with be for big-endian)");
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
                std::size_t end = 0;
                const double db = std::stod(text, &end);
                const double linear = std::pow(10., db / 20.);
                if (end != text.size() || !std::isfinite(db) || !std::isfinite(linear) ||
                    linear > std::numeric_limits<float>::max())
                    throw std::invalid_argument("gain must be finite and representable");
                gain = static_cast<float>(linear);
            } else if (option == "--speaker-map") {
                audio_options = true;
                if (value() != "config")
                    throw std::invalid_argument("--speaker-map accepts only 'config'");
                speaker_map = true;
            } else if (option == "--output-format") {
                audio_options = true;
                const auto name = value();
                const auto parsed = parse_output_format(name);
                if (!parsed)
                    throw std::invalid_argument(
                        "unsupported output format: " + name +
                        " (use CONTAINER-TYPE: wav, rf64, w64, aiff, caf or raw with pcm16, "
                        "pcm24, pcm32, float or double, wav/rf64/w64/aiff/caf also pcm8; "
                        "flac with pcm8, pcm16 or pcm24)");
                if (!convolver::available(parsed->container))
                    throw std::invalid_argument(
                        "this build of Convolver cannot write FLAC: its libsndfile was built "
                        "without FLAC support. Choose another container, such as wav or rf64");
                format = *parsed;
            } else if (option == "--dither") {
                audio_options = true;
                const auto name = value();
                if (name == "none")
                    dither = convolver::Dither::none;
                else if (name == "rectangular")
                    dither = convolver::Dither::rectangular;
                else if (name == "triangular")
                    dither = convolver::Dither::triangular;
                else if (name == "triangular-highpass")
                    dither = convolver::Dither::triangular_highpass;
                else
                    throw std::invalid_argument("unsupported dither: " + name);
            } else if (option == "--noise-shaping") {
                audio_options = true;
                const auto name = value();
                if (name == "none")
                    shaping = convolver::NoiseShaping::none;
                else if (name == "simple")
                    shaping = convolver::NoiseShaping::simple;
                else if (name == "second-order")
                    shaping = convolver::NoiseShaping::second_order;
                else if (name == "third-order")
                    shaping = convolver::NoiseShaping::third_order;
                else if (name == "ninth-order")
                    shaping = convolver::NoiseShaping::ninth_order;
                else if (name == "sony-sbm")
                    shaping = convolver::NoiseShaping::sony_sbm;
                else
                    throw std::invalid_argument("unsupported noise shaping: " + name);
            } else if (option == "--valid-bits") {
                audio_options = true;
                const auto bits = count(value());
                if (bits < 8 || bits > 32)
                    throw std::invalid_argument("--valid-bits must be between 8 and 32");
                valid_bits = static_cast<unsigned>(bits);
            } else if (option == "--dither-seed") {
                audio_options = true;
                const auto text = value();
                const auto [end, error] =
                    std::from_chars(text.data(), text.data() + text.size(), dither_seed);
                if (error != std::errc{} || end != text.data() + text.size())
                    throw std::invalid_argument("dither seed must be a non-negative integer");
            } else
                throw std::invalid_argument("unknown option: " + option);
        }
        if (speaker_map && format.container != convolver::Container::wav)
            throw std::invalid_argument(
                "--speaker-map needs a wav- output format; other containers carry no "
                "speaker labels yet");
        if (raw_format && !raw)
            throw std::invalid_argument("--raw-format needs --raw-input RATE CHANNELS");
        const auto bits = convolver::container_bits(format.sample);
        if (!bits && dither && *dither != convolver::Dither::none)
            throw std::invalid_argument("--dither applies only to integer PCM output");
        if (!bits && shaping != convolver::NoiseShaping::none)
            throw std::invalid_argument("--noise-shaping applies only to integer PCM output");
        if (valid_bits && valid_bits > bits)
            throw std::invalid_argument(
                bits ? "--valid-bits exceeds the output container's " + std::to_string(bits) +
                           " bits"
                     : std::string("--valid-bits applies only to integer PCM output"));
        if ((!wisdom.empty() || !tune.empty()) && !convolver::fft_supports_wisdom())
            throw std::invalid_argument("tuning and wisdom require the FFTW backend");
        if (fft_options.time_limit_seconds > 0 &&
            fft_options.planning == convolver::FftPlanning::estimate && tune.empty())
            throw std::invalid_argument(
                "--fft-time-limit needs --fft-plan measure, patient or exhaustive");
        if (!tune.empty()) {
            if (tune == "-" || !filter.empty() || !input.empty() || !output.empty() ||
                !wisdom.empty() || audio_options)
                throw std::invalid_argument("--tune-fft needs a new file path and accepts only "
                                            "--block-size, --fft-plan and --fft-time-limit");
            // Reject existing targets before the potentially lengthy calibration.
            if (std::filesystem::exists(std::filesystem::symlink_status(tune)))
                throw std::invalid_argument("FFTW wisdom output must be a new file path");
            if (!explicit_plan)
                fft_options.planning = convolver::FftPlanning::patient;
            convolver::tune_fft(block, fft_options);
            const auto wisdom_text = convolver::export_fft_wisdom();
            convolver::detail::write_new_file(tune, std::as_bytes(std::span(wisdom_text)));
            std::cout << "Saved FFTW wisdom for block size " << block
                      << " (forward and inverse plans)\n";
            return 0;
        }
        if (filter.empty() || input.empty() || output.empty())
            throw std::invalid_argument("--filter, --input and --output are required (see --help)");
        if (same_file(input, output) || same_file(filter, output) ||
            (input == "-" && same_stdin(output)))
            throw std::invalid_argument("output must differ from input and filter");
        if (!wisdom.empty()) {
            if (same_file(wisdom, output))
                throw std::invalid_argument("output must differ from FFTW wisdom file");
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
                throw std::invalid_argument(
                    "output must differ from referenced configuration and impulse files");
        if (automatic_gain) {
            const auto estimate = convolver::estimate_attenuation(config);
            const bool safe = *automatic_gain == "no-clip";
            const auto db =
                safe ? estimate.worst_case_attenuation_db : estimate.noise_attenuation_db;
            gain = static_cast<float>(std::pow(10., db / 20.));
            if (!std::isfinite(gain))
                throw std::invalid_argument("the estimated gain is not representable");
            std::cerr << "convolver: " << (safe ? "no-clip" : "optimum") << " attenuation "
                      << std::fixed << std::setprecision(2) << db << " dB ("
                      << (safe ? "largest possible peak " : "white-noise peak ")
                      << std::setprecision(4)
                      << (safe ? estimate.worst_case_peak : estimate.noise_peak) << ")\n";
        }
        convolver::Engine engine(config, block, fft_options);
        // Dither and shaping are opt-in; integer PCM is plainly rounded by default.
        const convolver::QuantizationOptions quantization{dither.value_or(convolver::Dither::none),
                                                          dither_seed, shaping, valid_bits};
        convolver::OutputMetadata metadata;
        if (speaker_map) {
            metadata.channel_mask = config.channel_mask;
            if (!metadata.channel_mask)
                std::cerr << "convolver: the configuration declares no speaker mask; the output "
                             "channels stay unlabelled\n";
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

        constexpr std::size_t chunk = 4096;
        std::vector<float> in(chunk * engine.input_channels()),
            out(chunk * engine.output_channels());
        std::size_t skip = keep ? 0 : engine.latency_frames();
        auto emit = [&](std::size_t frames) {
            const auto drop = std::min(skip, frames);
            skip -= drop;
            auto samples = std::span<float>(out).subspan(
                drop * engine.output_channels(), (frames - drop) * engine.output_channels());
            for (float& v : samples) {
                v *= gain;
                if (!std::isfinite(v))
                    throw std::runtime_error("nonfinite output (input/filter/gain overflow)");
            }
            if (!samples.empty())
                writer.write(samples);
        };
        bool any = false;
        while (const auto frames = reader.read(in)) {
            any = true;
            engine.process(std::span<const float>(in).first(frames * engine.input_channels()), out,
                           frames);
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
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "convolver: " << error.what() << '\n';
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
        std::cerr << "convolver: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    return run(argc, argv);
}
#endif
