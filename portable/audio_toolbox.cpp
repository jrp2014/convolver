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
#include "convolver/engine.hpp"
#include "sound_files.hpp"
#include "utf8.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace convolver::detail {
namespace {
void validate(AudioInfo info) {
    if (info.sample_rate <= 0 || info.sample_rate > max_sample_rate || info.channels == 0 ||
        info.channels > max_channel_count)
        throw std::invalid_argument("audio rate must be 1.." + std::to_string(max_sample_rate) +
                                    " Hz and channels 1.." + std::to_string(max_channel_count));
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
        meaning = std::string(damaged_file_advice);
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

// IO-11: a WAV whose sizes were never filled in, as AudioToolbox is shown it:
// the file's own bytes with the sizes its header should have had written
// over the unfilled ones. Left to open such a file itself, AudioToolbox reads
// its audio short of where libsndfile ends: five mono 24-bit frames as four
// and six as five (native.audio_toolbox, PR #176, runs 42715853793 and
// 42796865380). The calls are the ones Apple gives for a file the caller
// reads (AudioFileOpenWithCallbacks, then ExtAudioFileWrapAudioFileID).
struct SizedWave {
    int descriptor = -1;
    WaveSizes sizes;
    SizedWave() = default;
    SizedWave(const SizedWave&) = delete;
    SizedWave& operator=(const SizedWave&) = delete;
    ~SizedWave() {
        if (descriptor >= 0)
            ::close(descriptor);
    }
    // AudioFile_ReadProc: fewer bytes than asked for only at the end.
    static OSStatus read(void* self, SInt64 position, UInt32 count, void* buffer, UInt32* actual) {
        const auto& wave = *static_cast<const SizedWave*>(self);
        auto* out = static_cast<unsigned char*>(buffer);
        *actual = 0;
        if (position < 0)
            return kAudioFilePositionError;
        UInt32 done = 0;
        while (done < count) {
            const auto got = ::pread(wave.descriptor, out + done, count - done,
                                     static_cast<off_t>(position) + static_cast<off_t>(done));
            if (got < 0 && errno == EINTR)
                continue;
            if (got < 0)
                return kAudioFileUnspecifiedError;
            if (got == 0)
                break;
            done += static_cast<UInt32>(got);
        }
        fill_in_sizes(wave.sizes, static_cast<std::uint64_t>(position),
                      std::span<unsigned char>(out, done));
        *actual = done;
        return noErr;
    }
    // AudioFile_GetSizeProc: what the RIFF size, once filled in, says.
    static SInt64 length(void* self) {
        return SInt64{static_cast<const SizedWave*>(self)->sizes.riff_size} + 8;
    }
};

// A file read through its header by ExtAudioFile, decoded to 32-bit floats.
class ToolboxFile final : public SoundFile {
  public:
    explicit ToolboxFile(const std::filesystem::path& path) : path_(path) {
        const auto opening = "cannot open audio input " + detail::utf8_string(path) + ": ";
        require_file(path, opening);
        // Held by file_ from here on, so every refusal below disposes of it
        // (jrp's review: a file refused after opening was left open).
        OSStatus opened = noErr;
        if (const auto sizes = validate_wave(path)) {
            file_.wave = std::make_unique<SizedWave>();
            file_.wave->sizes = *sizes;
            file_.wave->descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
            if (file_.wave->descriptor < 0) {
                // The hint only where a sandbox can be the cause, as in missing().
                const std::error_code error(errno, std::generic_category());
                const bool denied = error == std::errc::permission_denied ||
                                    error == std::errc::operation_not_permitted;
                throw std::runtime_error(
                    opening + error.message() +
                    (denied ? std::string(" (") + sandbox_hint + ")" : std::string()));
            }
            opened = AudioFileOpenWithCallbacks(file_.wave.get(), &SizedWave::read, nullptr,
                                                &SizedWave::length, nullptr, kAudioFileWAVEType,
                                                &file_.audio_file);
            if (opened != noErr)
                file_.audio_file = nullptr;
            else
                opened = ExtAudioFileWrapAudioFileID(file_.audio_file, false, &file_.ref);
        } else {
            const auto& native = path.native();
            CFURLRef url = CFURLCreateFromFileSystemRepresentation(
                kCFAllocatorDefault, reinterpret_cast<const UInt8*>(native.data()),
                static_cast<CFIndex>(native.size()), false);
            if (!url)
                throw std::runtime_error(opening + "the path is not valid");
            opened = ExtAudioFileOpenURL(url, &file_.ref);
            CFRelease(url);
        }
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
        if (!(rate >= 1 && rate <= max_sample_rate) || rate != std::floor(rate))
            throw std::runtime_error("unsupported audio input " + detail::utf8_string(path) +
                                     ": the rate, " + std::to_string(rate) +
                                     " Hz, is not a whole number of hertz from 1 to " +
                                     std::to_string(max_sample_rate));
        info_ = {static_cast<int>(rate), format.mChannelsPerFrame};
        try {
            validate(info_);
        } catch (const std::invalid_argument& e) {
            // The file's own metadata is out of range: a data error, not a caller error.
            throw std::runtime_error("unsupported audio input " + detail::utf8_string(path) + ": " +
                                     e.what());
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
                throw std::runtime_error(invalid_sample_message(
                    path_, position_ + i / channels, i % channels + 1, info_.sample_rate));
        position_ += frames;
        return frames;
    }
    void close() override {
        const auto ref = std::exchange(file_.ref, nullptr);
        const OSStatus status = ref ? ExtAudioFileDispose(ref) : noErr;
        file_.close_audio_file();
        if (status != noErr)
            throw std::runtime_error("audio close failed: " + explain(status));
    }

  private:
    // Owns the open file: disposed of by the destructor, which also runs
    // when the constructor throws after opening.
    struct Handle {
        ExtAudioFileRef ref = nullptr;
        // Only for a WAV shown with its sizes filled in: the audio file that
        // ref wraps and does not own, and what its callbacks read from.
        AudioFileID audio_file = nullptr;
        std::unique_ptr<SizedWave> wave;
        Handle() = default;
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        // After ref is disposed of, which may still read through it.
        void close_audio_file() noexcept {
            if (const auto id = std::exchange(audio_file, nullptr))
                AudioFileClose(id);
            wave.reset();
        }
        ~Handle() {
            if (ref)
                ExtAudioFileDispose(ref);
            close_audio_file();
        }
    };
    Handle file_;
    AudioInfo info_{};
    std::optional<std::size_t> frames_;
    std::filesystem::path path_;
    std::uint64_t position_ = 0; // Frames read so far, for messages.
};

// A headerless file, decoded as io.cpp decodes one.
class RawFile final : public SoundFile {
  public:
    RawFile(const std::filesystem::path& path, RawSpec spec, RawEncoding encoding)
        : info_(spec), encoding_(encoding), width_(raw_sample_bytes(encoding.sample)), path_(path) {
        validate(spec);
        const auto opening = "cannot open audio input " + detail::utf8_string(path) + ": ";
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
            throw std::runtime_error(detail::raw_incomplete_frame);
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
            throw std::runtime_error(detail::raw_incomplete_frame);
        for (std::size_t i = 0; i < got / width_; ++i) {
            data[i] = decode_raw(bytes_.data() + i * width_, encoding_);
            if (!std::isfinite(data[i]))
                throw std::runtime_error(
                    invalid_sample_message(path_, position_ + i / info_.channels,
                                           i % info_.channels + 1, info_.sample_rate, encoding_));
        }
        position_ += got / frame_bytes_;
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
    std::filesystem::path path_;
    std::uint64_t position_ = 0; // Frames read so far, for messages.
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
                throw std::runtime_error("cannot open audio input " + detail::utf8_string(path) +
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
