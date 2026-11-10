// SPDX-License-Identifier: GPL-2.0-or-later
// The parts of reading filter files that need no sound-file library, shared by
// the libsndfile reader (io.cpp) and the AudioToolbox reader (audio_toolbox.cpp).
#include "sound_files.hpp"
#include "advice.hpp"
#include "bytes.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace convolver {
namespace detail {
HeaderMarker header_marker(const std::filesystem::path& path) {
    // Opening anything but a regular file could block (a named pipe) or
    // consume data that cannot be read again.
    std::error_code ec;
    if (path == "-" || !std::filesystem::is_regular_file(path, ec))
        return HeaderMarker::none;
    unsigned char head[4]{};
    std::ifstream file(path, std::ios::binary);
    file.read(reinterpret_cast<char*>(head), sizeof head);
    if (file.gcount() == sizeof head) {
        // These container markers are unmistakable, and libsndfile does not
        // always say it saw them: it does not know BW64, and it reports an AU
        // or WAV in an encoding it lacks as unrecognised.
        const std::string_view marker(reinterpret_cast<const char*>(head), sizeof head);
        for (const std::string_view known : {"RIFF", "RIFX", "RF64", "BW64", "riff", "FORM", "caff",
                                             "fLaC", "OggS", ".snd", "dns."})
            if (marker == known)
                return HeaderMarker::container;
    }
    // libsndfile identifies two formats from too few bits for sample data not
    // to match by chance: a bare MPEG frame sync (11 bits, about one file in
    // 5000; its source calls the check "prone to false positives") and an Akai
    // MPC 2000 sample (two bytes, 01 04). Neither is a format anyone names
    // .pcm or .raw. An MPEG file with an ID3v2 tag still counts: libsndfile
    // identifies it from the tag.
    if (file.gcount() >= 2 &&
        ((head[0] == 0xFF && (head[1] & 0xE0) == 0xE0) || (head[0] == 1 && head[1] == 4)))
        return HeaderMarker::sample_like;
    return HeaderMarker::none;
}
namespace {
// A size a writer leaves when it cannot go back to fill in the real one:
// FFmpeg writes it in the RIFF header and in 'data' first, and replaces it
// only when it finishes writing to a file it can seek in.
constexpr std::uint32_t unknown_size = 0xffffffff;
// A WAV without sizes that is refused all the same, with its own advice.
struct UnsizedRefusal : std::runtime_error {
    UnsizedRefusal(const char* what, const char* cause_text, const char* fix_text)
        : std::runtime_error(what), cause(cause_text), fix(fix_text) {}
    const char* cause;
    const char* fix;
};

// libsndfile takes a WAV that ends early for a shorter one, so the chunk
// layout is checked here first. IO-10: two layouts that break the format's
// rules are accepted, because libsndfile, FFmpeg and macOS's AudioToolbox
// read them and common writers produce them. Both rules are narrow on
// purpose; every other mismatch is still refused.
// - No pad byte after the last chunk: a chunk of odd length ending exactly
//   where the RIFF size ends (Python's wave module writes mono 24-bit audio
//   with an odd number of frames so). A RIFF size that counts the pad byte
//   in a file that lacks it is a file cut short, and is refused.
// - Sizes never filled in (IO-11, jrp's decision of 10 November 2026): a
//   RIFF size of unknown_size that the file is too short for means "to the
//   end of the file", and so does a 'data' size of unknown_size in such a
//   file. A writer stopped half way (FFmpeg killed while writing to a file)
//   leaves the same header, so that file is read as if it were whole; all
//   that can be checked is that PCM or float audio ends on a whole frame.
//   Two such files are refused, each with its own advice. One has audio of
//   an odd number of bytes: AudioToolbox drops the last byte and reads a
//   frame fewer than libsndfile (native.audio_toolbox, PR #176), and a
//   filter must not load shorter in a Mac plug-in than on the command line.
//   The other is a file as long as the RIFF size says or longer, 4 GiB:
//   there unknown_size is a size like any other, and libsndfile reads the
//   audio only as far as it reaches.
void check_wave(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::array<unsigned char, 40> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()), 12);
    if (file.gcount() != 12 ||
        std::string_view(reinterpret_cast<char*>(bytes.data()), 4) != "RIFF" ||
        std::string_view(reinterpret_cast<char*>(bytes.data() + 8), 4) != "WAVE")
        return;
    const auto riff_size = detail::read_le32(std::as_bytes(std::span(bytes)).subspan(4));
    const std::uint64_t file_size = std::filesystem::file_size(path);
    std::uint64_t end = std::uint64_t(riff_size) + 8;
    const bool unsized = riff_size == unknown_size && end > file_size;
    if (unsized)
        end = file_size;
    if (end < 12 || end > file_size)
        throw std::runtime_error("truncated or invalid RIFF/WAVE size");
    unsigned block_align = 0;
    bool pcm_or_float = false;
    std::vector<std::uint64_t> data_sizes;
    std::uint64_t position = 12;
    std::size_t chunks = 0;
    while (position < end) {
        if (++chunks > 65536 || end - position < 8)
            throw std::runtime_error("invalid RIFF/WAVE chunk layout");
        file.seekg(static_cast<std::streamoff>(position));
        file.read(reinterpret_cast<char*>(bytes.data()), 8);
        if (!file)
            throw std::runtime_error("cannot read RIFF/WAVE chunk header");
        std::uint64_t length = detail::read_le32(std::as_bytes(std::span(bytes)).subspan(4));
        auto chunk_end = position + 8 + length + (length & 1u);
        // Compared before 'fmt ' below reuses the buffer.
        const auto tag = std::string_view(reinterpret_cast<char*>(bytes.data()), 4);
        const bool data = tag == "data", format_chunk = tag == "fmt ";
        if (unsized && data && length == unknown_size) {
            length = end - position - 8;
            chunk_end = end;
        } else if ((length & 1u) && chunk_end == end + 1) {
            chunk_end = end; // The last chunk, without its pad byte.
        }
        if (!unsized && data && riff_size == unknown_size && length == unknown_size)
            throw UnsizedRefusal(
                "the sizes in its WAV header were never filled in and the file is 4 GiB or longer",
                "The program that wrote it could not go back to the header: it wrote to a pipe "
                "(as FFmpeg does with '-f wav -'), or it was stopped before it finished. A WAV "
                "header cannot give a size of 4 GiB or more, so nothing says where the audio ends.",
                "Write the audio to a file in a format for long audio, such as RF64 or W64, "
                "or send it as a RAW stream.");
        if (chunk_end > end)
            throw std::runtime_error("truncated RIFF/WAVE chunk");
        if (unsized && data && (length & 1u))
            throw UnsizedRefusal(
                "the sizes in its WAV header were never filled in and its audio is an odd number "
                "of bytes",
                "The program that wrote it could not go back to the header: it wrote to a pipe "
                "(as FFmpeg does with '-f wav -'), or it was stopped before it finished. "
                "Programs disagree about where such audio ends, and some drop its last sample.",
                "Write the file again, to a file instead of a pipe, and let the program finish.");
        if (data)
            data_sizes.push_back(length);
        if (format_chunk) {
            if (length < 16)
                throw std::runtime_error("invalid WAV format chunk");
            file.read(reinterpret_cast<char*>(bytes.data()),
                      static_cast<std::streamsize>(std::min<std::uint64_t>(length, 40)));
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
} // namespace
void validate_wave(const std::filesystem::path& path) {
    if (path == "-" || !std::filesystem::is_regular_file(path))
        return;
    try {
        check_wave(path);
    } catch (const std::runtime_error& e) {
        // One place turns the path into text, whichever advice follows.
        const auto* unsized = dynamic_cast<const UnsizedRefusal*>(&e);
        throw std::runtime_error(
            advice_text({"cannot read sound file " + path.string() + ": " + e.what(),
                         unsized ? unsized->cause
                                 : "The file is damaged or incomplete, or is not a sound file.",
                         unsized ? unsized->fix : "Export it again from the program that made it.",
                         {}}));
    }
}
std::string sample_position(std::uint64_t frame, std::size_t channel, int sample_rate) {
    char seconds[32];
    std::snprintf(seconds, sizeof seconds, "%.3f",
                  sample_rate > 0 ? static_cast<double>(frame) / sample_rate : 0.0);
    return std::string("at ") + seconds + " s (frame " + std::to_string(frame) + "), channel " +
           std::to_string(channel);
}
std::string invalid_sample_message(const std::filesystem::path& path, std::uint64_t frame,
                                   std::size_t channel, int sample_rate,
                                   std::optional<RawEncoding> raw) {
    const bool stream = path == "-";
    Advice a{std::string(stream ? "cannot read audio from standard input"
                                : "cannot read sound file " + path.string()) +
                 ": invalid sample (not a number, or infinite) " +
                 sample_position(frame, channel, sample_rate),
             {},
             {},
             {}};
    if (raw) {
        a.cause = "Headerless samples were read as " + raw_encoding_name(*raw) +
                  ", and these bytes are not a valid sample of that type.";
        a.fix = "If they are stored in another type, say which (--raw-format for --input, "
                "--raw-filter-format for a filter).";
        a.note = "Otherwise the program that wrote them produced invalid samples.";
    } else {
        a.cause = "Sound files should hold only finite samples: the file is damaged, or the "
                  "program that made it wrote invalid samples.";
        a.fix = "Export it again from that program.";
    }
    return advice_text(a);
}
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
} // namespace detail

std::optional<RawEncoding> parse_raw_encoding(std::string_view name) noexcept {
    if (name.size() != 5 || (name.substr(3) != "le" && name.substr(3) != "be"))
        return std::nullopt;
    constexpr std::pair<std::string_view, SampleType> samples[]{{"s16", SampleType::pcm16},
                                                                {"s24", SampleType::pcm24},
                                                                {"s32", SampleType::pcm32},
                                                                {"f32", SampleType::float32},
                                                                {"f64", SampleType::float64}};
    for (const auto& [text, value] : samples)
        if (name.substr(0, 3) == text)
            return RawEncoding{value, name.substr(3) == "be"};
    return std::nullopt;
}
std::string raw_encoding_name(RawEncoding encoding) {
    std::string name;
    switch (encoding.sample) {
    case SampleType::pcm16:
        name = "s16";
        break;
    case SampleType::pcm24:
        name = "s24";
        break;
    case SampleType::pcm32:
        name = "s32";
        break;
    case SampleType::float32:
        name = "f32";
        break;
    case SampleType::float64:
        name = "f64";
        break;
    default:
        throw std::invalid_argument("raw streams have no 8-bit sample type");
    }
    return name + (encoding.big_endian ? "be" : "le");
}
} // namespace convolver
