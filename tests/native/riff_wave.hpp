// SPDX-License-Identifier: GPL-2.0-or-later
// IO-10, CI-66: RIFF WAV layouts written by hand for the readers' tests, as
// wave64.hpp writes Wave64 files. The libsndfile tests (io_tests.cpp) and
// the AudioToolbox tests (audio_toolbox_tests.cpp) read the same bytes, so
// a newly found layout can be tried on both readers. Written byte by byte,
// not with Convolver's own writer, so that a mistake the writer shares with
// a reader cannot hide.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace convolver::test {
// The sample a pcm24_layout() file holds in frame n: (n + 1) / 128.
inline float pcm24_layout_sample(std::size_t n) {
    return static_cast<float>(n + 1) / 128;
}
// A mono 48 kHz 24-bit PCM WAV of `frames` frames, frame n holding
// pcm24_layout_sample(n), with the RIFF and data chunk sizes as given rather
// than as counted, and `after_data` following the samples: for layouts that
// break RIFF's rules and no writer here produces, such as a last chunk of
// odd length without its pad byte (Python's wave module) or sizes that were
// never filled in (0xffffffff, FFmpeg writing to a pipe). The format chunk
// is at offset 12 and the samples start at offset 44.
inline std::string pcm24_layout(std::size_t frames, std::uint32_t riff_size,
                                std::uint32_t data_size, const std::string& after_data) {
    std::string out = "RIFF";
    const auto put = [&](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i)
            out += static_cast<char>((value >> (8 * i)) & 0xffU);
    };
    put(riff_size, 4);
    out += "WAVEfmt ";
    put(16, 4);
    put(1, 2); // PCM
    put(1, 2);
    put(48000, 4);
    put(48000 * 3, 4);
    put(3, 2);
    put(24, 2);
    out += "data";
    put(data_size, 4);
    for (std::size_t n = 0; n < frames; ++n)
        put(static_cast<std::uint32_t>(n + 1) << 16, 3);
    return out + after_data;
}
} // namespace convolver::test
