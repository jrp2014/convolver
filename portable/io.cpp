// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/io.hpp"
#include "bytes.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <limits>
#include <mutex>
#include <optional>
#include <sndfile.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif

namespace convolver {
namespace {
void validate(AudioInfo info) {
    if (info.sample_rate <= 0 || info.sample_rate > 768000 || info.channels == 0 ||
        info.channels > 256)
        throw std::invalid_argument("audio rate must be 1..768000 Hz and channels 1..256");
}
std::runtime_error error(const std::string& action, SNDFILE* file) {
    return std::runtime_error(action + ": " + sf_strerror(file));
}
// WAVE speaker bit -> libsndfile channel-map value. The enumerations differ in
// order (front left is SF_CHANNEL_MAP_LEFT, not SF_CHANNEL_MAP_FRONT_LEFT).
constexpr std::array<int, 18> speaker_map{SF_CHANNEL_MAP_LEFT,
                                          SF_CHANNEL_MAP_RIGHT,
                                          SF_CHANNEL_MAP_CENTER,
                                          SF_CHANNEL_MAP_LFE,
                                          SF_CHANNEL_MAP_REAR_LEFT,
                                          SF_CHANNEL_MAP_REAR_RIGHT,
                                          SF_CHANNEL_MAP_FRONT_LEFT_OF_CENTER,
                                          SF_CHANNEL_MAP_FRONT_RIGHT_OF_CENTER,
                                          SF_CHANNEL_MAP_REAR_CENTER,
                                          SF_CHANNEL_MAP_SIDE_LEFT,
                                          SF_CHANNEL_MAP_SIDE_RIGHT,
                                          SF_CHANNEL_MAP_TOP_CENTER,
                                          SF_CHANNEL_MAP_TOP_FRONT_LEFT,
                                          SF_CHANNEL_MAP_TOP_FRONT_CENTER,
                                          SF_CHANNEL_MAP_TOP_FRONT_RIGHT,
                                          SF_CHANNEL_MAP_TOP_REAR_LEFT,
                                          SF_CHANNEL_MAP_TOP_REAR_CENTER,
                                          SF_CHANNEL_MAP_TOP_REAR_RIGHT};
static_assert(known_speaker_bits == (1u << speaker_map.size()) - 1);
void validate_speakers(std::uint32_t mask, std::size_t channels, Container container) {
    if (!mask)
        return;
    if (container == Container::raw)
        throw std::invalid_argument("a speaker map needs WAV output; RAW has no header");
    if (container != Container::wav)
        throw std::invalid_argument("a speaker map needs WAV output; other containers do not "
                                    "carry speaker labels yet");
    if (mask & ~known_speaker_bits)
        throw std::invalid_argument("speaker mask has unknown speaker bits (only 0x3ffff are "
                                    "defined): 0x" +
                                    [&] {
                                        char text[9];
                                        std::snprintf(text, sizeof text, "%x", mask);
                                        return std::string(text);
                                    }());
    if (static_cast<std::size_t>(std::popcount(mask)) != channels)
        throw std::invalid_argument("speaker mask names " + std::to_string(std::popcount(mask)) +
                                    " speakers for " + std::to_string(channels) +
                                    " output channels");
}
// The WAVE_FORMAT_EXTENSIBLE fields a RIFF/WAVE file declares, if it has them.
struct Extensible {
    std::uint16_t valid_bits = 0;
    std::uint32_t channel_mask = 0;
    std::streamoff valid_bits_offset = 0; // File offset of wValidBitsPerSample.
};
std::optional<Extensible> declared_extensible(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 12> head{};
    file.read(reinterpret_cast<char*>(head.data()), head.size());
    if (!file || std::string_view(reinterpret_cast<char*>(head.data()), 4) != "RIFF")
        return std::nullopt;
    for (int chunk = 0; chunk < 64; ++chunk) {
        std::array<unsigned char, 8> header{};
        file.read(reinterpret_cast<char*>(header.data()), header.size());
        if (!file)
            return std::nullopt;
        const auto length = detail::read_le32(std::as_bytes(std::span(header)).subspan(4));
        if (std::string_view(reinterpret_cast<char*>(header.data()), 4) == "fmt ") {
            const auto start = static_cast<std::streamoff>(file.tellg());
            std::array<unsigned char, 24> fmt{};
            file.read(reinterpret_cast<char*>(fmt.data()), fmt.size());
            const auto bytes = std::as_bytes(std::span(fmt));
            if (!file || length < 40 || detail::read_le16(bytes) != 0xfffe)
                return std::nullopt;
            return Extensible{detail::read_le16(bytes.subspan(18)),
                              detail::read_le32(bytes.subspan(20)), start + 18};
        }
        file.seekg(static_cast<std::streamoff>(length + (length & 1u)), std::ios::cur);
    }
    return std::nullopt;
}
// libsndfile always declares every container bit valid, and labels speakers by
// channel count when given no map. Record fewer bits and the requested mask.
void write_extensible(const std::filesystem::path& path, unsigned bits, std::uint32_t mask) {
    const auto format = declared_extensible(path);
    if (!format)
        throw std::runtime_error("audio output " + path.string() +
                                 " has no WAVE_FORMAT_EXTENSIBLE header for its valid bits");
    std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(format->valid_bits_offset);
    // wValidBitsPerSample, then dwChannelMask.
    std::array<char, 6> value{};
    value[0] = static_cast<char>(bits & 255);
    value[1] = static_cast<char>(bits >> 8);
    for (unsigned i = 0; i < 4; ++i)
        value[2 + i] = static_cast<char>((mask >> (8 * i)) & 255);
    file.write(value.data(), value.size());
    file.close();
    if (!file)
        throw std::runtime_error("cannot record valid bits in " + path.string());
}
void validate_raw_stdin(std::size_t frame_bytes) {
    // On a seekable descriptor libsndfile reports a whole-frame count, which
    // can conceal trailing bytes if a read ends exactly at that frame count.
#ifdef _WIN32
    struct _stat64 status {};
    if (_fstat64(0, &status) != 0 || (status.st_mode & _S_IFMT) != _S_IFREG)
        return;
    const auto position = _lseeki64(0, 0, SEEK_CUR);
#else
    struct stat status {};
    if (fstat(0, &status) != 0 || !S_ISREG(status.st_mode))
        return;
    const auto position = lseek(0, 0, SEEK_CUR);
#endif
    if (position < 0 || status.st_size < position ||
        static_cast<std::uint64_t>(status.st_size - position) % frame_bytes)
        throw std::runtime_error("RAW input ends with an incomplete frame");
}
// libsndfile accepts truncated RIFF data by reducing the frame count. Check
// container boundaries for ordinary RIFF/WAVE files before handing it decoding.
// Other libsndfile containers retain that library's validation semantics.
void validate_wave(const std::filesystem::path& path) {
    if (path == "-" || !std::filesystem::is_regular_file(path))
        return;
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 40> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), 12);
    if (file.gcount() != 12 ||
        std::string_view(reinterpret_cast<char*>(bytes.data()), 4) != "RIFF" ||
        std::string_view(reinterpret_cast<char*>(bytes.data() + 8), 4) != "WAVE")
        return;
    const std::uint64_t end =
        std::uint64_t(detail::read_le32(std::as_bytes(std::span(bytes)).subspan(4))) + 8;
    if (end < 12 || end > std::filesystem::file_size(path))
        throw std::runtime_error("truncated or invalid RIFF/WAVE size");
    unsigned block_align = 0;
    bool pcm_or_float = false;
    std::vector<std::uint32_t> data_sizes;
    std::uint64_t position = 12;
    std::size_t chunks = 0;
    while (position < end) {
        if (++chunks > 65536 || end - position < 8)
            throw std::runtime_error("invalid RIFF/WAVE chunk layout");
        file.seekg(static_cast<std::streamoff>(position));
        file.read(reinterpret_cast<char*>(bytes.data()), 8);
        if (!file)
            throw std::runtime_error("cannot read RIFF/WAVE chunk header");
        const auto length = detail::read_le32(std::as_bytes(std::span(bytes)).subspan(4));
        const auto chunk_end = position + 8 + length + (length & 1u);
        if (chunk_end > end)
            throw std::runtime_error("truncated RIFF/WAVE chunk");
        const auto tag = std::string_view(reinterpret_cast<char*>(bytes.data()), 4);
        if (tag == "data")
            data_sizes.push_back(length);
        if (tag == "fmt ") {
            if (length < 16)
                throw std::runtime_error("invalid WAV format chunk");
            file.read(reinterpret_cast<char*>(bytes.data()), std::min<std::uint32_t>(length, 40));
            if (!file)
                throw std::runtime_error("cannot read WAV format");
            unsigned format = detail::read_le16(std::as_bytes(std::span(bytes)));
            if (format == 0xfffe && length >= 40)
                format = detail::read_le16(std::as_bytes(std::span(bytes)).subspan(24));
            pcm_or_float = format == 1 || format == 3;
            block_align = detail::read_le16(std::as_bytes(std::span(bytes)).subspan(12));
        }
        position = chunk_end;
    }
    if (pcm_or_float)
        for (const auto length : data_sizes)
            if (!block_align || length % block_align)
                throw std::runtime_error("WAV data ends with an incomplete frame");
}
SNDFILE* open(const std::filesystem::path& path, int mode, SF_INFO& info) {
    if (path == "-") {
        if ((info.format & SF_FORMAT_TYPEMASK) != SF_FORMAT_RAW)
            throw std::invalid_argument(
                mode == SFM_READ
                    ? "reading audio from standard input ('-') needs --raw-input RATE CHANNELS: "
                      "a pipe carries headerless samples (little-endian float32 unless "
                      "--raw-format says otherwise), so the sample rate and channel count "
                      "must be given on the command line"
                    : "writing audio to standard output ('-') needs a raw output format such "
                      "as --output-format raw-float: a file header cannot be completed on a "
                      "pipe");
        const int fd = mode == SFM_READ ? 0 : 1;
#ifdef _WIN32
        if (_setmode(fd, _O_BINARY) == -1)
            throw std::runtime_error("cannot set standard stream to binary mode");
#endif
        return sf_open_fd(fd, mode, &info, SF_FALSE);
    }
#ifdef _WIN32
    return sf_wchar_open(path.c_str(), mode, &info);
#else
    return sf_open(path.c_str(), mode, &info);
#endif
}
// libsndfile writes the last open failure, and some of its open steps, to
// globals, so opens are serialized together with reading that failure:
// LV2 instances load filter files on several worker threads at once. Other
// code in the same process that uses libsndfile is beyond this lock.
SNDFILE* open_or_throw(const std::filesystem::path& path, int mode, SF_INFO& info,
                       const std::string& action) {
    static std::mutex opening;
    const std::lock_guard lock(opening);
    SNDFILE* file = open(path, mode, info);
    if (!file)
        throw error(action + " " + path.string(), nullptr);
    return file;
}
struct Handle {
    SNDFILE* file = nullptr;
    ~Handle() {
        if (file)
            sf_close(file);
    }
    void close() {
        if (auto* f = std::exchange(file, nullptr)) {
            const int prior = sf_error(f);
            const std::string prior_message = prior ? sf_strerror(f) : "";
            const int code = sf_close(f);
            if (prior)
                throw std::runtime_error("audio finalization failed: " + prior_message);
            if (code)
                throw std::runtime_error(std::string("audio close failed: ") +
                                         sf_error_number(code));
        }
    }
};
sf_count_t frames(std::size_t samples, std::size_t channels) {
    if (samples % channels)
        throw std::invalid_argument("audio buffer must contain complete frames");
    const auto n = samples / channels;
    if (n > static_cast<std::size_t>(std::numeric_limits<sf_count_t>::max()))
        throw std::length_error("audio buffer too large");
    return static_cast<sf_count_t>(n);
}
std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
} // namespace
bool supported(OutputEncoding e) noexcept {
    switch (e.container) {
    case Container::flac:
        return e.sample == SampleType::pcm8 || e.sample == SampleType::pcm16 ||
               e.sample == SampleType::pcm24;
    case Container::raw:
        return e.sample != SampleType::pcm8;
    default:
        return true;
    }
}
OutputEncoding output_encoding(OutputFormat format) noexcept {
    switch (format) {
    case OutputFormat::wav_pcm8:
        return {Container::wav, SampleType::pcm8};
    case OutputFormat::wav_pcm16:
        return {Container::wav, SampleType::pcm16};
    case OutputFormat::wav_pcm24:
        return {Container::wav, SampleType::pcm24};
    case OutputFormat::wav_pcm32:
        return {Container::wav, SampleType::pcm32};
    case OutputFormat::raw_float:
        return {Container::raw, SampleType::float32};
    default:
        return {Container::wav, SampleType::float32};
    }
}
unsigned container_bits(SampleType sample) noexcept {
    switch (sample) {
    case SampleType::pcm8:
        return 8;
    case SampleType::pcm16:
        return 16;
    case SampleType::pcm24:
        return 24;
    case SampleType::pcm32:
        return 32;
    default:
        return 0;
    }
}
unsigned container_bits(OutputFormat format) noexcept {
    switch (format) {
    case OutputFormat::wav_float:
    case OutputFormat::wav_pcm16:
    case OutputFormat::wav_pcm24:
    case OutputFormat::wav_pcm32:
    case OutputFormat::raw_float:
    case OutputFormat::wav_pcm8:
        return container_bits(output_encoding(format).sample);
    }
    return 0;
}
namespace {
// Error-feedback coefficients of the historical noise shapers (dither.h),
// newest error first.
std::vector<double> shaping_coefficients(NoiseShaping shaping) {
    switch (shaping) {
    case NoiseShaping::none:
        return {};
    case NoiseShaping::simple:
        return {1};
    case NoiseShaping::second_order:
        return {1.287, -0.651};
    case NoiseShaping::third_order:
        return {1.329, -0.735, 0.0646};
    case NoiseShaping::ninth_order:
        return {2.203, -3.210, 3.970, -4.404, 3.668, -2.656, 1.644, -0.767, 0.118};
    case NoiseShaping::sony_sbm:
        return {1.47933,   -1.59032,  1.64436,    -1.36613,   0.926704,    -0.557931,
                -0.267859, -0.106726, -0.0285161, 0.00123066, -0.00616555, 0.003067};
    }
    throw std::invalid_argument("unknown noise shaping");
}
} // namespace
Quantizer::Quantizer(std::size_t channels, unsigned bits, QuantizationOptions options)
    : channels_(channels), bits_(bits), options_(options), state_(options.seed) {
    if (channels == 0 || channels > 256)
        throw std::invalid_argument("quantizer channels must be 1..256");
    if (bits < 8 || bits > 32)
        throw std::invalid_argument("quantizer bits must be 8..32");
    switch (options.dither) {
    case Dither::none:
    case Dither::rectangular:
    case Dither::triangular:
    case Dither::triangular_highpass:
        break;
    default:
        throw std::invalid_argument("unknown dither");
    }
    coefficients_ = shaping_coefficients(options.noise_shaping);
    scale_ = std::ldexp(1.0, static_cast<int>(bits) - 1);
    low_ = -scale_;
    high_ = scale_ - 1;
    reset();
}
float Quantizer::uniform() noexcept {
    // 24 random bits give exactly representable floats in [0, 1).
    return static_cast<float>(splitmix64(state_) >> 40) * 0x1p-24f;
}
float Quantizer::dither() noexcept {
    switch (options_.dither) {
    case Dither::none:
        break;
    case Dither::rectangular:
        return uniform() - 0.5f;
    case Dither::triangular: {
        // Sequence RNG draws explicitly: subtraction operands are unordered.
        const float first = uniform();
        const float second = uniform();
        return first - second;
    }
    case Dither::triangular_highpass: {
        const float u = uniform();
        const float d = u - previous_[channel_];
        previous_[channel_] = u;
        return d;
    }
    }
    return 0;
}
void Quantizer::reset() noexcept {
    state_ = options_.seed;
    channel_ = 0;
    previous_.assign(options_.dither == Dither::triangular_highpass ? channels_ : 0, 0.f);
    for (auto& v : previous_)
        v = uniform();
    errors_.assign(channels_ * coefficients_.size(), 0.0);
}
void Quantizer::quantize(std::span<const float> in, std::span<std::int32_t> out) noexcept {
    const auto count = std::min(in.size(), out.size());
    const auto order = coefficients_.size();
    for (std::size_t i = 0; i < count; ++i) {
        const double d = dither();
        double* const e = errors_.data() + channel_ * order;
        double feedback = 0;
        for (std::size_t k = 0; k < order; ++k)
            feedback += coefficients_[k] * e[k];
        if (++channel_ == channels_)
            channel_ = 0;
        // Double keeps the scaled float sample and dither exact before rounding
        // (for up to 24 significant bits without shaping).
        const double target = static_cast<double>(in[i]) * scale_ - feedback;
        const double x = std::floor(target + d + 0.5);
        if (order) {
            // Rounding error including dither, before clipping; zero for NaN or
            // infinity so a bad sample cannot poison later ones.
            const double error = std::isfinite(x) ? x - target : 0.0;
            for (std::size_t k = order - 1; k > 0; --k)
                e[k] = e[k - 1];
            e[0] = error;
        }
        if (std::isnan(x))
            out[i] = 0;
        else
            out[i] = x >= high_  ? static_cast<std::int32_t>(high_)
                     : x <= low_ ? static_cast<std::int32_t>(low_)
                                 : static_cast<std::int32_t>(x);
    }
}
Pcm16Quantizer::Pcm16Quantizer(std::size_t channels, QuantizationOptions options)
    : quantizer_(channels, 16, options) {}
void Pcm16Quantizer::reset() noexcept {
    quantizer_.reset();
}
void Pcm16Quantizer::quantize(std::span<const float> in, std::span<std::int16_t> out) noexcept {
    std::array<std::int32_t, 256> scratch{};
    const auto count = std::min(in.size(), out.size());
    for (std::size_t offset = 0; offset < count; offset += scratch.size()) {
        const auto n = std::min(scratch.size(), count - offset);
        const auto part = std::span(scratch).first(n);
        quantizer_.quantize(in.subspan(offset, n), part);
        for (std::size_t i = 0; i < n; ++i)
            out[offset + i] = static_cast<std::int16_t>(part[i]);
    }
}
namespace {
std::size_t raw_sample_bytes(SampleType sample) {
    switch (sample) {
    case SampleType::pcm16:
        return 2;
    case SampleType::pcm24:
        return 3;
    case SampleType::pcm32:
    case SampleType::float32:
        return 4;
    case SampleType::float64:
        return 8;
    default:
        throw std::invalid_argument("RAW samples must be pcm16, pcm24, pcm32, float32 or float64");
    }
}
// One RAW sample as float. Integers are scaled by 2^-(bits-1), as libsndfile does.
float decode_raw(const std::byte* p, RawEncoding e) noexcept {
    const auto width = e.sample == SampleType::float64 ? 8u
                       : e.sample == SampleType::pcm16 ? 2u
                       : e.sample == SampleType::pcm24 ? 3u
                                                       : 4u;
    std::uint64_t v = 0;
    for (unsigned i = 0; i < width; ++i) {
        const auto byte = std::to_integer<std::uint64_t>(p[e.big_endian ? i : width - 1 - i]);
        v = (v << 8) | byte;
    }
    switch (e.sample) {
    case SampleType::float32:
        return std::bit_cast<float>(static_cast<std::uint32_t>(v));
    case SampleType::float64:
        return static_cast<float>(std::bit_cast<double>(v));
    default: {
        // Sign-extend from the sample width, then scale exactly.
        const auto bits = 8 * width;
        const auto shift = 64 - bits;
        const auto value = static_cast<std::int64_t>(v << shift) >> shift;
        return static_cast<float>(static_cast<double>(value) * std::ldexp(1.0, 1 - int(bits)));
    }
    }
}
} // namespace
struct AudioReader::Impl {
    Handle handle;
    AudioInfo info{};
    std::optional<std::size_t> frames;
    bool raw = false, raw_stdin = false;
    RawEncoding encoding;
    std::size_t sample_bytes = 4;
    std::vector<std::byte> bytes;
};
AudioReader::AudioReader(const std::filesystem::path& path, std::optional<RawSpec> raw,
                         RawEncoding encoding)
    : impl_(std::make_unique<Impl>()) {
    SF_INFO sf{};
    if (raw) {
        impl_->raw = true;
        validate(*raw);
        impl_->encoding = encoding;
        impl_->sample_bytes = raw_sample_bytes(encoding.sample);
        const auto frame_bytes = impl_->sample_bytes * raw->channels;
        sf.samplerate = raw->sample_rate;
        sf.channels = static_cast<int>(raw->channels);
        // libsndfile only opens the stream; read() decodes the bytes itself.
        sf.format = SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE;
        if (path == "-")
            validate_raw_stdin(frame_bytes);
        if (path != "-" && std::filesystem::is_regular_file(path) &&
            std::filesystem::file_size(path) % frame_bytes)
            throw std::runtime_error("RAW input ends with an incomplete frame");
    } else
        validate_wave(path);
#ifdef _WIN32
    // libsndfile 1.2.2's ReadFile path treats ERROR_BROKEN_PIPE (normal pipe
    // EOF) as an error. Use the CRT's EOF-aware byte reader for borrowed RAW
    // stdin; file decoding and all output still use libsndfile.
    if (raw && path == "-") {
        if (_setmode(0, _O_BINARY) == -1)
            throw std::runtime_error("cannot set stdin to binary mode");
        impl_->raw_stdin = true;
        impl_->info = *raw;
        return;
    }
#endif
    impl_->handle.file = open_or_throw(path, SFM_READ, sf, "cannot open audio input");
    impl_->info = {sf.samplerate, static_cast<std::size_t>(sf.channels)};
    try {
        validate(impl_->info);
    } catch (const std::invalid_argument& e) {
        // The file's own metadata is out of range: a data error, not a caller error.
        throw std::runtime_error("unsupported audio input " + path.string() + ": " + e.what());
    }
    if (path != "-" && sf.frames >= 0 && sf.frames != SF_COUNT_MAX)
        impl_->frames = static_cast<std::size_t>(sf.frames);
}
AudioReader::~AudioReader() = default;
AudioInfo AudioReader::info() const noexcept {
    return impl_->info;
}
std::optional<std::size_t> AudioReader::declared_frames() const noexcept {
    return impl_->frames;
}
std::size_t AudioReader::read(std::span<float> data) {
    auto* file = impl_->handle.file;
    if (!file && !impl_->raw_stdin)
        throw std::logic_error("audio reader is closed");
    const auto count = frames(data.size(), impl_->info.channels);
    if (!count)
        return 0;
    sf_count_t got;
    if (impl_->raw) {
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        const auto width = impl_->sample_bytes;
        if (data.size() > static_cast<std::size_t>(std::numeric_limits<sf_count_t>::max()) / 8)
            throw std::length_error("RAW buffer too large");
        const auto wanted = static_cast<sf_count_t>(data.size() * width);
        impl_->bytes.resize(data.size() * width);
        auto* buffer = impl_->bytes.data();
        // Reading bytes preserves incomplete frames on nonseekable inputs;
        // sf_readf_float silently drops their trailing samples/bytes.
        sf_count_t bytes = 0;
#ifdef _WIN32
        if (impl_->raw_stdin) {
            while (bytes < wanted) {
                const auto chunk =
                    static_cast<unsigned>(std::min<sf_count_t>(wanted - bytes, INT_MAX));
                const auto n = _read(0, reinterpret_cast<char*>(buffer) + bytes, chunk);
                if (n < 0) {
                    if (errno == EINTR)
                        continue;
                    throw std::system_error(errno, std::generic_category(),
                                            "RAW stdin read failed");
                }
                if (n == 0)
                    break;
                bytes += n;
            }
        } else
#endif
        {
            bytes = sf_read_raw(file, buffer, wanted);
            if (bytes < 0 || sf_error(file))
                throw error("RAW audio read failed", file);
        }
        const auto frame_bytes = static_cast<sf_count_t>(width * impl_->info.channels);
        if (bytes % frame_bytes)
            throw std::runtime_error("RAW input ends with an incomplete frame");
        for (std::size_t i = 0; i < static_cast<std::size_t>(bytes) / width; ++i)
            data[i] = decode_raw(buffer + i * width, impl_->encoding);
        got = bytes / frame_bytes;
    } else {
        got = sf_readf_float(file, data.data(), count);
    }
    if (got < 0 || (file && sf_error(file)))
        throw error("audio read failed", file);
    for (std::size_t i = 0; i < static_cast<std::size_t>(got) * impl_->info.channels; ++i)
        if (!std::isfinite(data[i]))
            throw std::runtime_error("nonfinite audio sample");
    return static_cast<std::size_t>(got);
}
void AudioReader::close() {
    impl_->raw_stdin = false;
    impl_->handle.close();
}
struct AudioWriter::Impl {
    Handle handle;
    AudioInfo info{};
    std::filesystem::path path;
    std::uint32_t speakers = 0;
    unsigned valid_bits = 0, container_bits = 0;
    bool raw = false, limited = false;
    std::size_t frame_bytes = 0;
    std::optional<Quantizer> quantizer;
    std::vector<std::int32_t> buffer;
    // Reserve ample room for the WAV headers written by this wrapper.
    std::uint64_t wav_remaining = std::uint64_t(UINT32_MAX) - 4096;
};
namespace {
OutputEncoding checked(OutputFormat format) {
    switch (format) {
    case OutputFormat::wav_float:
    case OutputFormat::wav_pcm16:
    case OutputFormat::wav_pcm24:
    case OutputFormat::wav_pcm32:
    case OutputFormat::raw_float:
    case OutputFormat::wav_pcm8:
        return output_encoding(format);
    }
    throw std::invalid_argument("unknown audio output format");
}
int sndfile_format(OutputEncoding e) {
    if (!supported(e))
        throw std::invalid_argument("unsupported audio output format");
    int container = 0;
    switch (e.container) {
    case Container::wav:
        container = SF_FORMAT_WAV;
        break;
    case Container::rf64:
        container = SF_FORMAT_RF64;
        break;
    case Container::w64:
        container = SF_FORMAT_W64;
        break;
    case Container::aiff:
        container = SF_FORMAT_AIFF;
        break;
    case Container::caf:
        container = SF_FORMAT_CAF;
        break;
    case Container::flac:
        container = SF_FORMAT_FLAC;
        break;
    case Container::raw:
        container = SF_FORMAT_RAW | SF_ENDIAN_LITTLE;
        break;
    default:
        throw std::invalid_argument("unknown audio output container");
    }
    const bool wav_family = e.container == Container::wav || e.container == Container::rf64 ||
                            e.container == Container::w64;
    switch (e.sample) {
    case SampleType::pcm8:
        return container | (wav_family ? SF_FORMAT_PCM_U8 : SF_FORMAT_PCM_S8);
    case SampleType::pcm16:
        return container | SF_FORMAT_PCM_16;
    case SampleType::pcm24:
        return container | SF_FORMAT_PCM_24;
    case SampleType::pcm32:
        return container | SF_FORMAT_PCM_32;
    case SampleType::float32:
        return container | SF_FORMAT_FLOAT;
    case SampleType::float64:
        return container | SF_FORMAT_DOUBLE;
    }
    throw std::invalid_argument("unknown audio sample type");
}
} // namespace
AudioWriter::AudioWriter(const std::filesystem::path& path, AudioInfo info, OutputFormat format,
                         QuantizationOptions quantization, OutputMetadata metadata)
    : AudioWriter(path, info, checked(format), quantization, metadata) {}
AudioWriter::AudioWriter(const std::filesystem::path& path, AudioInfo info, OutputEncoding encoding,
                         QuantizationOptions quantization, OutputMetadata metadata)
    : impl_(std::make_unique<Impl>()) {
    validate(info);
    validate_speakers(metadata.channel_mask, info.channels, encoding.container);
    SF_INFO sf{};
    sf.samplerate = info.sample_rate;
    sf.channels = static_cast<int>(info.channels);
    sf.format = sndfile_format(encoding);
    const auto bits = container_bits(encoding.sample);
    if (!bits && (quantization.dither != Dither::none ||
                  quantization.noise_shaping != NoiseShaping::none || quantization.valid_bits))
        throw std::invalid_argument(
            "dither, noise shaping and valid bits apply only to integer PCM output");
    if (quantization.valid_bits && (quantization.valid_bits < 8 || quantization.valid_bits > bits))
        throw std::invalid_argument("valid bits must be between 8 and the container's " +
                                    std::to_string(bits) + " bits");
    impl_->container_bits = bits;
    impl_->valid_bits = quantization.valid_bits ? quantization.valid_bits : bits;
    if (bits)
        impl_->quantizer.emplace(info.channels, impl_->valid_bits, quantization);
    impl_->info = info;
    impl_->path = path;
    impl_->speakers = metadata.channel_mask;
    impl_->raw = encoding.container == Container::raw;
    if (impl_->valid_bits != bits && encoding.container != Container::wav)
        throw std::invalid_argument("valid bits below the container size need WAV output");
    // RIFF/WAVE and AIFF chunk sizes are 32-bit; the other containers have no
    // such limit (FLAC frames are counted in 36 bits).
    impl_->limited = encoding.container == Container::wav || encoding.container == Container::aiff;
    impl_->frame_bytes = info.channels * (bits                                     ? bits / 8
                                          : encoding.sample == SampleType::float64 ? 8
                                                                                   : 4);
    // Speaker labels and reduced valid bits both need WAVE_FORMAT_EXTENSIBLE.
    if (impl_->speakers || impl_->valid_bits != bits)
        sf.format = (sf.format & ~SF_FORMAT_TYPEMASK) | SF_FORMAT_WAVEX;
    if (!sf_format_check(&sf))
        throw std::invalid_argument("unsupported audio output format");
    impl_->handle.file = open_or_throw(path, SFM_WRITE, sf, "cannot open audio output");
    sf_command(impl_->handle.file, SFC_SET_CLIPPING, nullptr, SF_TRUE);
    if (impl_->speakers) {
        // Channels take the set bits in increasing order, as WAVE requires.
        std::vector<int> map;
        for (std::size_t bit = 0; bit < speaker_map.size(); ++bit)
            if (impl_->speakers & (1u << bit))
                map.push_back(speaker_map[bit]);
        // libsndfile falls back to a default mask when it rejects a map.
        if (sf_command(impl_->handle.file, SFC_SET_CHANNEL_MAP_INFO, map.data(),
                       static_cast<int>(map.size() * sizeof(int))) != SF_TRUE)
            throw std::runtime_error(std::string(sf_version_string()) +
                                     " cannot write this speaker map to " + path.string());
    }
}
AudioWriter::~AudioWriter() = default;
void AudioWriter::write(std::span<const float> data) {
    auto* file = impl_->handle.file;
    if (!file)
        throw std::logic_error("audio writer is closed");
    const auto count = frames(data.size(), impl_->info.channels);
    for (const auto sample : data)
        if (!std::isfinite(sample))
            throw std::runtime_error("nonfinite audio sample");
    if (!count)
        return;
    if (impl_->limited &&
        static_cast<std::uint64_t>(count) > impl_->wav_remaining / impl_->frame_bytes)
        throw std::length_error("WAV or AIFF output exceeds the 4 GiB container limit; select "
                                "an rf64, w64, caf, flac or raw output format for a larger "
                                "stream");
    sf_count_t written;
    if (impl_->quantizer) {
        // Bound scratch memory; the quantizer carries its state across calls.
        // libsndfile's int samples are left-justified 32-bit values: it keeps
        // the container's top bits, so B valid bits land left-justified.
        constexpr std::size_t chunk_frames = 4096;
        const auto channels = impl_->info.channels;
        const auto shift = 32 - impl_->valid_bits;
        impl_->buffer.resize(std::min(data.size(), chunk_frames * channels));
        written = 0;
        for (std::size_t offset = 0; offset < data.size();) {
            const auto n = std::min(data.size() - offset, impl_->buffer.size());
            const auto part = std::span<std::int32_t>(impl_->buffer).first(n);
            impl_->quantizer->quantize(data.subspan(offset, n), part);
            for (auto& v : part)
                v = static_cast<std::int32_t>(static_cast<std::uint32_t>(v) << shift);
            const auto frames_here = static_cast<sf_count_t>(n / channels);
            const auto got = sf_writef_int(file, part.data(), frames_here);
            written += got;
            if (got != frames_here)
                break;
            offset += n;
        }
    } else
        written = sf_writef_float(file, data.data(), count);
    if (written != count || sf_error(file))
        throw error("audio write failed or was short", file);
    if (impl_->limited)
        impl_->wav_remaining -= static_cast<std::uint64_t>(count) * impl_->frame_bytes;
}
void AudioWriter::close() {
    // Some libsndfile versions discard a container-header error during
    // sf_close. Finalize the WAV header while its error state is inspectable.
    if (impl_->handle.file && !impl_->raw)
        sf_command(impl_->handle.file, SFC_UPDATE_HEADER_NOW, nullptr, 0);
    const bool finishing = impl_->handle.file != nullptr;
    impl_->handle.close();
    if (!finishing)
        return;
    const bool reduced = impl_->container_bits && impl_->valid_bits != impl_->container_bits;
    if (reduced)
        write_extensible(impl_->path, impl_->valid_bits, impl_->speakers);
    if (!impl_->speakers && !reduced)
        return;
    // Confirm the finished header carries the requested labels and valid bits.
    const auto declared = declared_extensible(impl_->path);
    if (!declared || declared->channel_mask != impl_->speakers)
        throw std::runtime_error("audio output " + impl_->path.string() +
                                 " was written without the requested speaker mask");
    if (impl_->container_bits && declared->valid_bits != impl_->valid_bits)
        throw std::runtime_error("audio output " + impl_->path.string() +
                                 " was written without the requested valid bits");
}
const char* sndfile_version() noexcept {
    return sf_version_string();
}
} // namespace convolver
