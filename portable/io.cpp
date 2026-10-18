// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/io.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cerrno>
#include <climits>
#include <cmath>
#include <fstream>
#include <limits>
#include <sndfile.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <utility>
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
std::uint32_t little32(const unsigned char* b) {
    return std::uint32_t(b[0]) | (std::uint32_t(b[1]) << 8) | (std::uint32_t(b[2]) << 16) |
           (std::uint32_t(b[3]) << 24);
}
unsigned little16(const unsigned char* b) {
    return unsigned(b[0]) | (unsigned(b[1]) << 8);
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
    const std::uint64_t end = std::uint64_t(little32(bytes.data() + 4)) + 8;
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
        const auto length = little32(bytes.data() + 4);
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
            unsigned format = little16(bytes.data());
            if (format == 0xfffe && length >= 40)
                format = little16(bytes.data() + 24);
            pcm_or_float = format == 1 || format == 3;
            block_align = little16(bytes.data() + 12);
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
} // namespace
struct AudioReader::Impl {
    Handle handle;
    AudioInfo info{};
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
    validate(impl_->info);
}
AudioReader::~AudioReader() = default;
AudioInfo AudioReader::info() const noexcept {
    return impl_->info;
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
        auto* raw_bytes = reinterpret_cast<const unsigned char*>(data.data());
        for (std::size_t i = 0; i < static_cast<std::size_t>(bytes) / 4; ++i) {
            const auto* b = raw_bytes + i * 4;
            const std::uint32_t bits = little32(b);
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
    bool raw = false;
    std::size_t frame_bytes = 0;
    // Reserve ample room for the WAV headers written by this wrapper.
    std::uint64_t wav_remaining = std::uint64_t(UINT32_MAX) - 4096;
};
AudioWriter::AudioWriter(const std::filesystem::path& path, AudioInfo info, OutputFormat format)
    : impl_(std::make_unique<Impl>()) {
    validate(info);
    impl_->info = info;
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
    if (!sf_format_check(&sf))
        throw std::invalid_argument("unsupported audio output format");
    impl_->handle.file = open(path, SFM_WRITE, sf);
    if (!impl_->handle.file)
        throw error("cannot open audio output " + path.string(), nullptr);
    sf_command(impl_->handle.file, SFC_SET_CLIPPING, nullptr, SF_TRUE);
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
    const auto written = sf_writef_float(file, data.data(), count);
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
    impl_->handle.close();
}
const char* sndfile_version() noexcept {
    return sf_version_string();
}
} // namespace convolver
