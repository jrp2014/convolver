// SPDX-License-Identifier: GPL-2.0-or-later
// IO-12: Sony Wave64 files with WAVE_FORMAT_EXTENSIBLE, written by hand so
// that they can hold any subtype, for the readers' tests. libsndfile 1.2.2
// decodes every subtype of such a file as integer PCM: 32-bit float 1.0
// reads as 0.49609375, which is how jrp found it (review of ca12f773).
#pragma once
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace convolver::test {
// Little-endian samples: 32- or 64-bit float, or 32-bit PCM (x * 2^31).
inline std::string wave64_samples(const std::vector<double>& values, int bits, bool pcm = false) {
    std::string out;
    for (const auto x : values) {
        const std::uint64_t v =
            pcm          ? static_cast<std::uint32_t>(static_cast<std::int32_t>(x * 2147483648.0))
            : bits == 32 ? std::uint64_t{std::bit_cast<std::uint32_t>(static_cast<float>(x))}
                         : std::bit_cast<std::uint64_t>(x);
        for (int i = 0; i < bits / 8; ++i)
            out += static_cast<char>((v >> (8 * i)) & 0xffU);
    }
    return out;
}
// A 48 kHz Wave64 file whose format chunk gives WAVE_FORMAT_EXTENSIBLE with
// the subtype GUID whose first field is `subtype` (1 PCM, 3 IEEE float,
// 6 A-law, 7 µ-law), `bits` per sample and `audio` as the data chunk's
// contents. `after_audio`: the contents of a 'junk' chunk after the audio.
// `audio_size`: the audio's length as its chunk header gives it, if not
// its true one (a file cut short).
inline void extensible_wave64(const std::filesystem::path& path, unsigned channels,
                              unsigned subtype, unsigned bits, const std::string& audio,
                              const std::string& after_audio = {},
                              std::uint64_t audio_size = ~std::uint64_t{0}) {
    const auto put = [](std::string& to, std::uint64_t value, int size) {
        for (int i = 0; i < size; ++i)
            to += static_cast<char>((value >> (8 * i)) & 0xffU);
    };
    const unsigned block_align = channels * bits / 8;
    std::string format;
    put(format, 0xfffe, 2);
    put(format, channels, 2);
    put(format, 48000, 4);
    put(format, 48000 * block_align, 4);
    put(format, block_align, 2);
    put(format, bits, 2);
    put(format, 22, 2);   // The extension's size.
    put(format, bits, 2); // Valid bits.
    put(format, 0, 4);    // No speaker positions.
    put(format, subtype, 4);
    format += std::string("\x00\x00\x10\x00\x80\x00\x00\xaa\x00\x38\x9b\x71", 12);
    // GUIDs for tags; 64-bit sizes that count the 24-byte chunk header;
    // each chunk starting on an 8-byte boundary.
    const std::string tail("\xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a", 12);
    const auto chunk = [&](const std::string& tag, const std::string& body, std::uint64_t size) {
        std::string c = tag + tail;
        put(c, 24 + size, 8);
        c += body;
        c.resize((c.size() + 7) & ~std::size_t{7}, '\0');
        return c;
    };
    std::string body =
        "wave" + tail + chunk("fmt ", format, format.size()) +
        chunk("data", audio, audio_size == ~std::uint64_t{0} ? audio.size() : audio_size);
    if (!after_audio.empty())
        body += chunk("junk", after_audio, after_audio.size());
    std::string bytes("riff\x2e\x91\xcf\x11\xa5\xd6\x28\xdb\x04\xc1\x00\x00", 16);
    put(bytes, 24 + body.size(), 8);
    std::ofstream f(path, std::ios::binary);
    f << bytes << body;
    if (!f)
        throw std::runtime_error("fixture write failed");
}
// The IO-12 files every reader must read the same: name, channels, and
// the samples of each channel in turn (none: refused).
struct Wave64Case {
    const char* name;
    std::size_t channels;
    std::vector<std::vector<float>> expected;
};
inline std::vector<Wave64Case> wave64_files(const std::filesystem::path& dir) {
    // jrp's file: a two-sample 32-bit float filter holding 1.0, 0.0.
    extensible_wave64(dir / "float32.w64", 1, 3, 32, wave64_samples({1, 0}, 32));
    extensible_wave64(dir / "float64.w64", 1, 3, 64, wave64_samples({1, -.25}, 64));
    extensible_wave64(dir / "float32-stereo.w64", 2, 3, 32,
                      wave64_samples({1, .5, -.25, .125}, 32));
    // Only the audio chunk is audio, whatever follows it.
    extensible_wave64(dir / "float32-then-junk.w64", 1, 3, 32, wave64_samples({.5, .25}, 32),
                      wave64_samples({1, 1, 1}, 32));
    // PCM, which libsndfile reads correctly, is still left to it.
    extensible_wave64(dir / "pcm32.w64", 1, 1, 32, wave64_samples({.5, -.5}, 32, true));
    // Refused: encodings libsndfile would decode as integers, and a float
    // file whose audio chunk claims more than the file holds.
    extensible_wave64(dir / "alaw.w64", 1, 6, 8, "\xd5\x55");
    extensible_wave64(dir / "float24.w64", 1, 3, 24, std::string(6, '\0'));
    extensible_wave64(dir / "float32-cut.w64", 1, 3, 32, wave64_samples({1, 0}, 32), {}, 16);
    return {{"float32.w64", 1, {{1, 0}}},
            {"float64.w64", 1, {{1, -.25f}}},
            {"float32-stereo.w64", 2, {{1, -.25f}, {.5f, .125f}}},
            {"float32-then-junk.w64", 1, {{.5f, .25f}}},
            {"pcm32.w64", 1, {{.5f, -.5f}}},
            {"alaw.w64", 1, {}},
            {"float24.w64", 1, {}},
            {"float32-cut.w64", 1, {}}};
}
} // namespace convolver::test
