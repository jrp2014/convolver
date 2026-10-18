// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "convolver/io.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <sys/stat.h>
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
  --block-size N       Partition payload in frames (default 256)
  --fft-plan MODE      estimate (render default), measure, patient (tune default)
  --fft-wisdom FILE    Load FFTW plans; fail if the requested plans are missing
  --tune-fft FILE      Time FFTW plans and save wisdom to a new file, then exit
  --gain-db DB         Output gain in decibels (default 0)
  --keep-latency       Keep the leading block of algorithmic latency
  --output-format F   wav-float (default), wav-pcm16, wav-pcm24, wav-pcm32,
                      raw-float (little-endian IEEE float32)
  --dither D           PCM16 only: triangular (default), triangular-highpass,
                      rectangular or none. Other formats reject dither.
  --dither-seed N      Dither generator seed (default 0); output is reproducible
  --raw-input RATE CHANNELS
                      Read little-endian IEEE float32 with explicit metadata
  --help              Show this help
  --version           Show the FFT backend and libsndfile versions
FILE may be '-' for stdin/stdout; standard streams require RAW float format.
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
    struct stat input_info {
    }, output_info{};
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
void write_new_wisdom(const std::filesystem::path& path, const std::string& text) {
    // Exclusive creation protects existing files, including hard links/symlinks.
#ifdef _WIN32
    const int fd =
        _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
#endif
    if (fd < 0)
        throw std::runtime_error("cannot create FFTW wisdom file (must be a new path)");
#ifdef _WIN32
    auto* stream = _fdopen(fd, "wb");
    if (!stream) {
        _close(fd);
        throw std::runtime_error("cannot open FFTW wisdom stream");
    }
#else
    auto* stream = fdopen(fd, "wb");
    if (!stream) {
        close(fd);
        throw std::runtime_error("cannot open FFTW wisdom stream");
    }
#endif
    const bool written = std::fwrite(text.data(), 1, text.size(), stream) == text.size();
    const bool closed = std::fclose(stream) == 0;
    if (!written || !closed)
        throw std::runtime_error(
            "cannot finish FFTW wisdom file; remove incomplete file before retrying");
}
} // namespace
int run(int argc, char** argv) {
    try {
#ifdef SIGPIPE
        std::signal(SIGPIPE, SIG_IGN); // Surface closed consumers as I/O failures.
#endif
        std::filesystem::path filter, input, output;
        std::filesystem::path wisdom, tune;
        convolver::FftOptions fft_options;
        bool explicit_plan = false, audio_options = false;
        std::optional<convolver::RawSpec> raw;
        convolver::OutputFormat format = convolver::OutputFormat::wav_float;
        std::optional<convolver::Dither> dither;
        std::uint64_t dither_seed = 0;
        std::size_t block = 256;
        float gain = 1;
        bool keep = false;
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
                std::cout << "Convolver portable\nFFT backend: " << convolver::fft_backend_name()
                          << " / " << convolver::fft_backend_version() << '\n'
                          << convolver::sndfile_version() << '\n';
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
                else
                    throw std::invalid_argument(
                        "FFT planning mode must be estimate, measure or patient");
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
            } else if (option == "--gain-db") {
                audio_options = true;
                auto text = value();
                std::size_t end = 0;
                const double db = std::stod(text, &end);
                const double linear = std::pow(10., db / 20.);
                if (end != text.size() || !std::isfinite(db) || !std::isfinite(linear) ||
                    linear > std::numeric_limits<float>::max())
                    throw std::invalid_argument("gain must be finite and representable");
                gain = static_cast<float>(linear);
            } else if (option == "--output-format") {
                audio_options = true;
                const auto name = value();
                if (name == "wav-float")
                    format = convolver::OutputFormat::wav_float;
                else if (name == "wav-pcm16")
                    format = convolver::OutputFormat::wav_pcm16;
                else if (name == "wav-pcm24")
                    format = convolver::OutputFormat::wav_pcm24;
                else if (name == "wav-pcm32")
                    format = convolver::OutputFormat::wav_pcm32;
                else if (name == "raw-float")
                    format = convolver::OutputFormat::raw_float;
                else
                    throw std::invalid_argument("unsupported output format: " + name);
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
        if ((!wisdom.empty() || !tune.empty()) && !convolver::fft_supports_wisdom())
            throw std::invalid_argument("tuning and wisdom require the FFTW backend");
        if (!tune.empty()) {
            if (tune == "-" || !filter.empty() || !input.empty() || !output.empty() ||
                !wisdom.empty() || audio_options)
                throw std::invalid_argument("--tune-fft needs a new file path and accepts only "
                                            "--block-size and --fft-plan");
            // Reject existing targets before the potentially lengthy calibration.
            if (std::filesystem::exists(std::filesystem::symlink_status(tune)))
                throw std::invalid_argument("FFTW wisdom output must be a new file path");
            if (!explicit_plan)
                fft_options.planning = convolver::FftPlanning::patient;
            convolver::tune_fft(block, fft_options.planning);
            write_new_wisdom(tune, convolver::export_fft_wisdom());
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
        convolver::AudioReader reader(input, raw);
        const auto info = reader.info();
        std::vector<std::filesystem::path> dependencies;
        const auto config =
            convolver::load_configuration(filter, info.sample_rate, info.channels, &dependencies);
        for (const auto& source : dependencies)
            if (same_file(source, output))
                throw std::invalid_argument(
                    "output must differ from referenced configuration and impulse files");
        convolver::Engine engine(config, block, fft_options);
        if (dither && *dither != convolver::Dither::none &&
            format != convolver::OutputFormat::wav_pcm16)
            throw std::invalid_argument("--dither applies only to wav-pcm16 output");
        // PCM16 output is dithered unless the caller explicitly opts out.
        const convolver::QuantizationOptions quantization{
            format == convolver::OutputFormat::wav_pcm16
                ? dither.value_or(convolver::Dither::triangular)
                : convolver::Dither::none,
            dither_seed};
        convolver::AudioWriter writer(output, {info.sample_rate, engine.output_channels()}, format,
                                      quantization);
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
