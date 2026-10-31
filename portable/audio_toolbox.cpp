// SPDX-License-Identifier: GPL-2.0-or-later
// AU-6: filter files read with macOS's own AudioToolbox (ExtAudioFile), so the
// Audio Unit links nothing beyond macOS (jrp's decision, 30 October 2026).
// It reads what the libsndfile reader reads, with the same checks around it:
// truncated WAVs are refused (validate_wave), the rate and channel limits are
// the same, and nonfinite samples are an error. Headerless .pcm and .raw
// filters are decoded here, exactly as io.cpp decodes them. Integer samples
// become floats by the same exact scaling; tests/native/audio_toolbox_tests.cpp
// compares the two readers on every format both read.
#include "config_parse.hpp"
#include "sound_files.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace convolver::detail {
namespace {
void validate(AudioInfo info) {
    if (info.sample_rate <= 0 || info.sample_rate > 768000 || info.channels == 0 ||
        info.channels > 256)
        throw std::invalid_argument("audio rate must be 1..768000 Hz and channels 1..256");
}
constexpr auto sandbox_hint = "a sandboxed host, such as GarageBand, lets a plug-in read only "
                              "the files chosen in its window";
// Carbon's file errors (MacErrors.h), which Core Audio passes on.
constexpr OSStatus file_not_found = -43, permission_error = -54, access_denied = -5000;
// What a Core Audio status means for someone choosing a filter file, with the
// code itself (four characters when printable, as Apple documents them).
std::string explain(OSStatus status) {
    std::string meaning;
    switch (status) {
    case kAudioFileUnsupportedFileTypeError:
        meaning = "macOS does not recognise it as a sound file";
        break;
    case kAudioFileUnsupportedDataFormatError:
        meaning = "macOS cannot decode this sound file's encoding";
        break;
    case kAudioFileInvalidFileError:
    case kAudioFileInvalidChunkError:
    case kAudioFileInvalidPacketOffsetError:
    case kAudioFileEndOfFileError:
        meaning = "the sound file is damaged or incomplete";
        break;
    case kAudioFilePermissionsError:
    case permission_error:
    case access_denied:
        meaning = std::string("permission denied (") + sandbox_hint + ")";
        break;
    case file_not_found:
        meaning = "no such file";
        break;
    default:
        meaning = "macOS could not read it";
        break;
    }
    const auto code = static_cast<std::uint32_t>(status);
    std::string text;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const auto c = static_cast<char>((code >> shift) & 0xffU);
        if (c < 32 || c > 126) {
            text.clear();
            break;
        }
        text += c;
    }
    return meaning + " (Core Audio error " +
           (text.empty() ? std::to_string(status) : "'" + text + "'") + ")";
}
std::string missing(const std::filesystem::path& path) {
    std::error_code ec;
    if (std::filesystem::exists(path, ec))
        return "not a regular file";
    // A sandbox may refuse even to say whether the file is there.
    if (ec == std::errc::permission_denied || ec == std::errc::operation_not_permitted)
        return std::string("permission denied (") + sandbox_hint + ")";
    if (ec && ec != std::errc::no_such_file_or_directory)
        return ec.message();
    return "no such file";
}
// What both readers check before opening: a regular file, not standard input.
void require_file(const std::filesystem::path& path, const std::string& opening) {
    std::error_code ec;
    if (path == "-" || !std::filesystem::is_regular_file(path, ec))
        throw std::runtime_error(
            opening + (path == "-" ? "standard input cannot be a filter" : missing(path)));
}

// A file read through its header by ExtAudioFile, decoded to 32-bit floats.
class ToolboxFile final : public SoundFile {
  public:
    explicit ToolboxFile(const std::filesystem::path& path) {
        const auto opening = "cannot open audio input " + path.string() + ": ";
        require_file(path, opening);
        validate_wave(path);
        const auto& native = path.native();
        CFURLRef url = CFURLCreateFromFileSystemRepresentation(
            kCFAllocatorDefault, reinterpret_cast<const UInt8*>(native.data()),
            static_cast<CFIndex>(native.size()), false);
        if (!url)
            throw std::runtime_error(opening + "the path is not valid");
        // Held by file_ from here on, so every refusal below disposes of it
        // (jrp's review: a file refused after opening was left open).
        const OSStatus opened = ExtAudioFileOpenURL(url, &file_.ref);
        CFRelease(url);
        if (opened != noErr) {
            file_.ref = nullptr;
            throw std::runtime_error(opening + explain(opened));
        }
        AudioStreamBasicDescription format{};
        UInt32 size = sizeof format;
        if (const auto status = ExtAudioFileGetProperty(
                file_.ref, kExtAudioFileProperty_FileDataFormat, &size, &format))
            throw std::runtime_error(opening + explain(status));
        const double rate = format.mSampleRate;
        if (!(rate >= 1 && rate <= 768000) || rate != std::floor(rate))
            throw std::runtime_error("unsupported audio input " + path.string() + ": the rate, " +
                                     std::to_string(rate) +
                                     " Hz, is not a whole number of hertz from 1 to 768000");
        info_ = {static_cast<int>(rate), format.mChannelsPerFrame};
        try {
            validate(info_);
        } catch (const std::invalid_argument& e) {
            // The file's own metadata is out of range: a data error, not a caller error.
            throw std::runtime_error("unsupported audio input " + path.string() + ": " + e.what());
        }
        AudioStreamBasicDescription client{};
        client.mSampleRate = rate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagsNativeFloatPacked;
        client.mChannelsPerFrame = format.mChannelsPerFrame;
        client.mBitsPerChannel = 32;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = client.mBytesPerPacket = 4 * format.mChannelsPerFrame;
        if (const auto status = ExtAudioFileSetProperty(
                file_.ref, kExtAudioFileProperty_ClientDataFormat, sizeof client, &client))
            throw std::runtime_error(opening + explain(status));
        SInt64 frames = 0;
        size = sizeof frames;
        if (ExtAudioFileGetProperty(file_.ref, kExtAudioFileProperty_FileLengthFrames, &size,
                                    &frames) == noErr &&
            frames >= 0)
            frames_ = static_cast<std::size_t>(frames);
    }
    ToolboxFile(const ToolboxFile&) = delete;
    ToolboxFile& operator=(const ToolboxFile&) = delete;
    AudioInfo info() const noexcept override {
        return info_;
    }
    std::optional<std::size_t> declared_frames() const noexcept override {
        return frames_;
    }
    std::size_t read(std::span<float> data) override {
        if (!file_.ref)
            throw std::logic_error("audio reader is closed");
        const auto channels = info_.channels;
        if (data.size() % channels)
            throw std::invalid_argument("audio buffer must contain complete frames");
        const auto most = std::numeric_limits<UInt32>::max() / (4 * channels);
        auto frames = static_cast<UInt32>(std::min<std::size_t>(data.size() / channels, most));
        if (!frames)
            return 0;
        AudioBufferList buffers{};
        buffers.mNumberBuffers = 1;
        buffers.mBuffers[0].mNumberChannels = static_cast<UInt32>(channels);
        buffers.mBuffers[0].mDataByteSize = frames * 4 * static_cast<UInt32>(channels);
        buffers.mBuffers[0].mData = data.data();
        if (const auto status = ExtAudioFileRead(file_.ref, &frames, &buffers))
            throw std::runtime_error("audio read failed: " + explain(status));
        for (std::size_t i = 0; i < std::size_t{frames} * channels; ++i)
            if (!std::isfinite(data[i]))
                throw std::runtime_error("nonfinite audio sample");
        return frames;
    }
    void close() override {
        if (const auto ref = std::exchange(file_.ref, nullptr))
            if (const auto status = ExtAudioFileDispose(ref))
                throw std::runtime_error("audio close failed: " + explain(status));
    }

  private:
    // Owns the open file: disposed of by the destructor, which also runs
    // when the constructor throws after opening.
    struct Handle {
        ExtAudioFileRef ref = nullptr;
        Handle() = default;
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        ~Handle() {
            if (ref)
                ExtAudioFileDispose(ref);
        }
    };
    Handle file_;
    AudioInfo info_{};
    std::optional<std::size_t> frames_;
};

// A headerless file, decoded as io.cpp decodes one.
class RawFile final : public SoundFile {
  public:
    RawFile(const std::filesystem::path& path, RawSpec spec, RawEncoding encoding)
        : info_(spec), encoding_(encoding), width_(raw_sample_bytes(encoding.sample)) {
        validate(spec);
        const auto opening = "cannot open audio input " + path.string() + ": ";
        require_file(path, opening);
        file_.open(path, std::ios::binary);
        if (!file_)
            throw std::runtime_error(opening + "it cannot be opened (" + sandbox_hint + ")");
        std::error_code ec;
        const auto bytes = std::filesystem::file_size(path, ec);
        if (ec)
            throw std::runtime_error(opening + ec.message());
        frame_bytes_ = width_ * spec.channels;
        if (bytes % frame_bytes_)
            throw std::runtime_error("RAW input ends with an incomplete frame");
        frames_ = static_cast<std::size_t>(bytes / frame_bytes_);
    }
    AudioInfo info() const noexcept override {
        return info_;
    }
    std::optional<std::size_t> declared_frames() const noexcept override {
        return frames_;
    }
    std::size_t read(std::span<float> data) override {
        if (!file_.is_open())
            throw std::logic_error("audio reader is closed");
        if (data.size() % info_.channels)
            throw std::invalid_argument("audio buffer must contain complete frames");
        bytes_.resize(data.size() * width_);
        file_.read(reinterpret_cast<char*>(bytes_.data()),
                   static_cast<std::streamsize>(bytes_.size()));
        const auto got = static_cast<std::size_t>(file_.gcount());
        if (file_.bad())
            throw std::runtime_error("RAW audio read failed");
        if (got % frame_bytes_)
            throw std::runtime_error("RAW input ends with an incomplete frame");
        for (std::size_t i = 0; i < got / width_; ++i) {
            data[i] = decode_raw(bytes_.data() + i * width_, encoding_);
            if (!std::isfinite(data[i]))
                throw std::runtime_error("nonfinite audio sample");
        }
        return got / frame_bytes_;
    }
    void close() override {
        file_.close();
    }

  private:
    AudioInfo info_;
    RawEncoding encoding_;
    std::size_t width_, frame_bytes_ = 1;
    std::size_t frames_ = 0;
    std::ifstream file_;
    std::vector<std::byte> bytes_;
};

// A .pcm or .raw filter is read through a header only when it starts with a
// container marker. libsndfile also recognises a few formats without one;
// AudioToolbox guesses from content more freely, and a raw filter misread as,
// say, MPEG audio would play the wrong filter.
bool recognised(const std::filesystem::path& path) {
    return header_marker(path) == HeaderMarker::container;
}
} // namespace

const SoundFiles& audio_toolbox_files() {
    static const SoundFiles files{
        [](const std::filesystem::path& path) -> std::unique_ptr<SoundFile> {
            // A .pcm or .raw file without a container marker is a headerless
            // filter, read only as a .cfg names one (with its rate and
            // channels). Chosen or listed directly, ExtAudioFile could take
            // its samples for some other format and play noise as the filter
            // (the fourth review); libsndfile refuses such a file.
            if (raw_filter_name(path) && !recognised(path))
                throw std::runtime_error("cannot open audio input " + path.string() +
                                         ": a headerless .pcm or .raw filter is read only when "
                                         "a .cfg names it, with its rate and channels");
            return std::make_unique<ToolboxFile>(path);
        },
        [](const std::filesystem::path& path, RawSpec spec,
           RawEncoding encoding) -> std::unique_ptr<SoundFile> {
            return std::make_unique<RawFile>(path, spec, encoding);
        },
        recognised};
    return files;
}
} // namespace convolver::detail
