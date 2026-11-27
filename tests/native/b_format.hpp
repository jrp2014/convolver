// SPDX-License-Identifier: GPL-2.0-or-later
// EDGE-3 and EDGE-16: a four-channel file at 48 kHz, two frames long (an
// impulse, then half of one, on the first channel), whose format chunk marks
// it as Ambisonics B-format (an .amb file's WAVE_FORMAT_EXTENSIBLE subtype).
// EDGE-16: the same marking in an RF64 or Wave64 file, or in a WAV after
// many other chunks, where Convolver used to miss it; and with the format
// chunk where reading it needs the size from RF64's 'ds64' (after the audio)
// or Wave64's 8-byte alignment (after a chunk of odd size). The samples are
// 32-bit float, which Convolver decodes itself in an extensible Wave64 file
// (IO-12: libsndfile 1.2.2 decodes them as integers).
#pragma once
#include <bit>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace convolver::test {
enum class BFormatFile { wav, rf64, rf64_audio_first, w64, w64_odd_chunk };
// `extra_chunks`: in a WAV, that many four-byte 'JUNK' chunks before the
// format chunk.
inline void b_format_wave(const std::filesystem::path& p, BFormatFile form = BFormatFile::wav,
                          int extra_chunks = 0) {
    std::string format, audio, bytes;
    const auto put = [](std::string& to, std::uint64_t value, int size) {
        for (int i = 0; i < size; ++i)
            to += static_cast<char>((value >> (8 * i)) & 0xffU);
    };
    put(format, 0xfffe, 2);
    put(format, 4, 2);
    put(format, 48000, 4);
    put(format, 48000 * 16, 4);
    put(format, 16, 2);
    put(format, 32, 2);
    put(format, 22, 2);
    put(format, 32, 2);
    put(format, 0, 4);
    const bool w64 = form == BFormatFile::w64 || form == BFormatFile::w64_odd_chunk;
    const bool rf64 = form == BFormatFile::rf64 || form == BFormatFile::rf64_audio_first;
    format += std::string("\x03\x00\x00\x00\x21\x07\xd3\x11\x86\x44\xc8\xc1\xca\x00\x00\x00", 16);
    for (const float sample : {1.f, 0.f, 0.f, 0.f, .5f, 0.f, 0.f, 0.f})
        put(audio, std::bit_cast<std::uint32_t>(sample), 4);
    if (w64) {
        // Sony Wave64: GUIDs for tags, 64-bit sizes that count the 24-byte
        // chunk header, each chunk starting on an 8-byte boundary.
        const std::string guid_tail("\xf3\xac\xd3\x11\x8c\xd1\x00\xc0\x4f\x8e\xdb\x8a", 12);
        std::string junk;
        if (form == BFormatFile::w64_odd_chunk) {
            junk = "junk" + guid_tail;
            put(junk, 24 + 5, 8);
            junk += std::string(5 + 3, '\0'); // Five bytes, then three to the boundary.
        }
        bytes += std::string("riff\x2e\x91\xcf\x11\xa5\xd6\x28\xdb\x04\xc1\x00\x00", 16);
        put(bytes, 40 + junk.size() + 24 + format.size() + 24 + audio.size(), 8);
        bytes += "wave" + guid_tail + junk + "fmt " + guid_tail;
        put(bytes, 24 + format.size(), 8);
        bytes += format + "data" + guid_tail;
        put(bytes, 24 + audio.size(), 8);
        bytes += audio;
    } else {
        std::string chunks;
        for (int i = 0; i < extra_chunks; ++i) {
            chunks += "JUNK";
            put(chunks, 4, 4);
            put(chunks, 0, 4);
        }
        std::string fmt_chunk = "fmt ", data_chunk = "data";
        put(fmt_chunk, format.size(), 4);
        fmt_chunk += format;
        // RF64 gives the audio's size in 'ds64' instead (EBU Tech 3306).
        put(data_chunk, rf64 ? 0xffffffffU : audio.size(), 4);
        data_chunk += audio;
        chunks +=
            form == BFormatFile::rf64_audio_first ? data_chunk + fmt_chunk : fmt_chunk + data_chunk;
        if (rf64) {
            std::string ds64;
            put(ds64, 4 + 8 + 28 + chunks.size(), 8); // The RIFF size,
            put(ds64, audio.size(), 8);               // the audio's,
            put(ds64, 2, 8);                          // its frames,
            put(ds64, 0, 4);                          // and no table.
            bytes += "RF64";
            put(bytes, 0xffffffffU, 4);
            bytes += "WAVEds64";
            put(bytes, ds64.size(), 4);
            bytes += ds64 + chunks;
        } else {
            bytes += "RIFF";
            put(bytes, 4 + chunks.size(), 4);
            bytes += "WAVE" + chunks;
        }
    }
    std::ofstream f(p, std::ios::binary);
    f << bytes;
    if (!f)
        throw std::runtime_error("fixture write failed");
}
// Every layout above, written to `dir`; returns their file names.
inline std::vector<std::string> b_format_files(const std::filesystem::path& dir) {
    const std::vector<std::pair<std::string, BFormatFile>> files{
        {"b-format.wav", BFormatFile::wav},
        {"b-format-rf64.wav", BFormatFile::rf64},
        {"b-format-rf64-audio-first.wav", BFormatFile::rf64_audio_first},
        {"b-format.w64", BFormatFile::w64},
        {"b-format-odd-chunk.w64", BFormatFile::w64_odd_chunk},
        {"b-format-chunks.wav", BFormatFile::wav}};
    std::vector<std::string> names;
    for (const auto& [name, form] : files) {
        // 64 'JUNK' chunks: the old check gave up after 63 (jrp's review of
        // 7bd024f).
        b_format_wave(dir / name, form, name == "b-format-chunks.wav" ? 64 : 0);
        names.push_back(name);
    }
    return names;
}
} // namespace convolver::test
