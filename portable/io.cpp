// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/io.hpp"
#include "bytes.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
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
void validate_speakers(std::uint32_t mask, std::size_t channels, OutputFormat format) {
    if (!mask)
        return;
    if (format == OutputFormat::raw_float)
        throw std::invalid_argument("a speaker map needs WAV output; RAW has no header");
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
// The dwChannelMask a RIFF/WAVE file declares, or nothing if it has none.
std::optional<std::uint32_t> declared_speakers(const std::filesystem::path& path) {
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
            std::array<unsigned char, 24> fmt{};
            file.read(reinterpret_cast<char*>(fmt.data()), fmt.size());
            const auto bytes = std::as_bytes(std::span(fmt));
            if (!file || length < 40 || detail::read_le16(bytes) != 0xfffe)
                return std::nullopt;
            return detail::read_le32(bytes.subspan(20));
        }
        file.seekg(static_cast<std::streamoff>(length + (length & 1u)), std::ios::cur);
    }
    return std::nullopt;
}
void validate_raw_stdin(std::size_t channels) {
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
        static_cast<std::uint64_t>(status.st_size - position) % (4 * channels))
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
            throw std::invalid_argument("standard streams require explicit RAW float32 metadata");
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
Pcm16Quantizer::Pcm16Quantizer(std::size_t channels, QuantizationOptions options)
    : channels_(channels), options_(options), state_(options.seed) {
    if (channels == 0 || channels > 256)
        throw std::invalid_argument("quantizer channels must be 1..256");
    switch (options.dither) {
    case Dither::none:
    case Dither::rectangular:
    case Dither::triangular:
    case Dither::triangular_highpass:
        break;
    default:
        throw std::invalid_argument("unknown dither");
    }
    reset();
}
float Pcm16Quantizer::uniform() noexcept {
    // 24 random bits give exactly representable floats in [0, 1).
    return static_cast<float>(splitmix64(state_) >> 40) * 0x1p-24f;
}
void Pcm16Quantizer::reset() noexcept {
    state_ = options_.seed;
    channel_ = 0;
    previous_.assign(options_.dither == Dither::triangular_highpass ? channels_ : 0, 0.f);
    for (auto& v : previous_)
        v = uniform();
}
void Pcm16Quantizer::quantize(std::span<const float> in, std::span<std::int16_t> out) noexcept {
    const auto count = std::min(in.size(), out.size());
    for (std::size_t i = 0; i < count; ++i) {
        float d = 0;
        switch (options_.dither) {
        case Dither::none:
            break;
        case Dither::rectangular:
            d = uniform() - 0.5f;
            break;
        case Dither::triangular: {
            // Sequence RNG draws explicitly: subtraction operands are unordered.
            const float first = uniform();
            const float second = uniform();
            d = first - second;
            break;
        }
        case Dither::triangular_highpass: {
            const float u = uniform();
            d = u - previous_[channel_];
            previous_[channel_] = u;
            break;
        }
        }
        if (++channel_ == channels_)
            channel_ = 0;
        // Double keeps the scaled float sample and dither exact before rounding.
        const double x = std::floor(static_cast<double>(in[i]) * 32768.0 + d + 0.5);
        if (std::isnan(x))
            out[i] = 0;
        else
            out[i] = x >= 32767.0    ? std::int16_t{32767}
                     : x <= -32768.0 ? std::int16_t{-32768}
                                     : static_cast<std::int16_t>(x);
    }
}
struct AudioReader::Impl {
    Handle handle;
    AudioInfo info{};
    std::optional<std::size_t> frames;
    bool raw = false, raw_stdin = false;
};
AudioReader::AudioReader(const std::filesystem::path& path, std::optional<RawSpec> raw)
    : impl_(std::make_unique<Impl>()) {
    SF_INFO sf{};
    if (raw) {
        impl_->raw = true;
        validate(*raw);
        sf.samplerate = raw->sample_rate;
        sf.channels = static_cast<int>(raw->channels);
        sf.format = SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE;
        if (path == "-")
            validate_raw_stdin(raw->channels);
        if (path != "-" && std::filesystem::is_regular_file(path) &&
            std::filesystem::file_size(path) % (4 * raw->channels))
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
    impl_->handle.file = open(path, SFM_READ, sf);
    if (!impl_->handle.file)
        throw error("cannot open audio input " + path.string(), nullptr);
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
        if (data.size() > static_cast<std::size_t>(std::numeric_limits<sf_count_t>::max() / 4))
            throw std::length_error("RAW buffer too large");
        // Reading bytes preserves incomplete frames on nonseekable inputs;
        // sf_readf_float silently drops their trailing samples/bytes.
        sf_count_t bytes = 0;
#ifdef _WIN32
        if (impl_->raw_stdin) {
            const auto wanted = static_cast<sf_count_t>(data.size() * 4);
            while (bytes < wanted) {
                const auto chunk =
                    static_cast<unsigned>(std::min<sf_count_t>(wanted - bytes, INT_MAX));
                const auto n = _read(0, reinterpret_cast<char*>(data.data()) + bytes, chunk);
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
            bytes = sf_read_raw(file, data.data(), static_cast<sf_count_t>(data.size() * 4));
            if (bytes < 0 || sf_error(file))
                throw error("RAW audio read failed", file);
        }
        if (bytes % static_cast<sf_count_t>(4 * impl_->info.channels))
            throw std::runtime_error("RAW input ends with an incomplete frame");
        const auto raw_bytes = std::as_bytes(data);
        for (std::size_t i = 0; i < static_cast<std::size_t>(bytes) / 4; ++i) {
            const auto bits = detail::read_le32(raw_bytes.subspan(i * 4));
            data[i] = std::bit_cast<float>(bits);
        }
        got = bytes / static_cast<sf_count_t>(4 * impl_->info.channels);
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
    bool raw = false;
    std::size_t frame_bytes = 0;
    std::optional<Pcm16Quantizer> pcm16;
    std::vector<std::int16_t> pcm16_buffer;
    // Reserve ample room for the WAV headers written by this wrapper.
    std::uint64_t wav_remaining = std::uint64_t(UINT32_MAX) - 4096;
};
AudioWriter::AudioWriter(const std::filesystem::path& path, AudioInfo info, OutputFormat format,
                         QuantizationOptions quantization, OutputMetadata metadata)
    : impl_(std::make_unique<Impl>()) {
    validate(info);
    validate_speakers(metadata.channel_mask, info.channels, format);
    if (quantization.dither != Dither::none && format != OutputFormat::wav_pcm16)
        throw std::invalid_argument("dither applies only to PCM16 output");
    if (format == OutputFormat::wav_pcm16)
        impl_->pcm16.emplace(info.channels, quantization);
    impl_->info = info;
    impl_->path = path;
    impl_->speakers = metadata.channel_mask;
    impl_->raw = format == OutputFormat::raw_float;
    impl_->frame_bytes = info.channels * (format == OutputFormat::wav_pcm16   ? 2
                                          : format == OutputFormat::wav_pcm24 ? 3
                                                                              : 4);
    SF_INFO sf{};
    sf.samplerate = info.sample_rate;
    sf.channels = static_cast<int>(info.channels);
    switch (format) {
    case OutputFormat::wav_float:
        sf.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        break;
    case OutputFormat::wav_pcm16:
        sf.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        break;
    case OutputFormat::wav_pcm24:
        sf.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;
        break;
    case OutputFormat::wav_pcm32:
        sf.format = SF_FORMAT_WAV | SF_FORMAT_PCM_32;
        break;
    case OutputFormat::raw_float:
        sf.format = SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE;
        break;
    default:
        throw std::invalid_argument("unknown audio output format");
    }
    if (impl_->speakers)
        sf.format = (sf.format & ~SF_FORMAT_TYPEMASK) | SF_FORMAT_WAVEX;
    if (!sf_format_check(&sf))
        throw std::invalid_argument("unsupported audio output format");
    impl_->handle.file = open(path, SFM_WRITE, sf);
    if (!impl_->handle.file)
        throw error("cannot open audio output " + path.string(), nullptr);
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
    if (!impl_->raw &&
        static_cast<std::uint64_t>(count) > impl_->wav_remaining / impl_->frame_bytes)
        throw std::length_error(
            "WAV output exceeds 4 GiB container limit; select raw-float for a larger stream");
    sf_count_t written;
    if (impl_->pcm16) {
        // Bound scratch memory; the quantizer carries its state across calls.
        constexpr std::size_t chunk_frames = 4096;
        const auto channels = impl_->info.channels;
        impl_->pcm16_buffer.resize(std::min(data.size(), chunk_frames * channels));
        written = 0;
        for (std::size_t offset = 0; offset < data.size();) {
            const auto n = std::min(data.size() - offset, impl_->pcm16_buffer.size());
            const auto part = std::span<std::int16_t>(impl_->pcm16_buffer).first(n);
            impl_->pcm16->quantize(data.subspan(offset, n), part);
            const auto frames_here = static_cast<sf_count_t>(n / channels);
            const auto got = sf_writef_short(file, part.data(), frames_here);
            written += got;
            if (got != frames_here)
                break;
            offset += n;
        }
    } else
        written = sf_writef_float(file, data.data(), count);
    if (written != count || sf_error(file))
        throw error("audio write failed or was short", file);
    if (!impl_->raw)
        impl_->wav_remaining -= static_cast<std::uint64_t>(count) * impl_->frame_bytes;
}
void AudioWriter::close() {
    // Some libsndfile versions discard a container-header error during
    // sf_close. Finalize the WAV header while its error state is inspectable.
    if (impl_->handle.file && !impl_->raw)
        sf_command(impl_->handle.file, SFC_UPDATE_HEADER_NOW, nullptr, 0);
    const bool check_speakers = impl_->handle.file && impl_->speakers;
    impl_->handle.close();
    // Confirm the finished header carries the requested labels.
    if (check_speakers && declared_speakers(impl_->path) != impl_->speakers)
        throw std::runtime_error("audio output " + impl_->path.string() +
                                 " was written without the requested speaker mask");
}
const char* sndfile_version() noexcept {
    return sf_version_string();
}
} // namespace convolver
