// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace convolver {
struct AudioInfo {
    int sample_rate;
    std::size_t channels;
};
// RAW streaming is little-endian IEEE float32, with explicit metadata.
using RawSpec = AudioInfo;
enum class OutputFormat { wav_float, wav_pcm16, wav_pcm24, wav_pcm32, raw_float };

// PCM16 quantization. Samples are scaled by 32768, offset by the dither (in
// 16-bit LSBs), rounded to nearest with ties toward +infinity, then clipped to
// [-32768, 32767]. There is no noise shaping.
// - none: plain rounding.
// - rectangular: uniform dither in [-0.5, 0.5) LSB.
// - triangular: independent TPDF dither, sum of two uniforms, in (-1, 1) LSB.
// - triangular_highpass: the historical engine's "Triangular" choice, the
//   per-channel difference of successive uniform values, also in (-1, 1) LSB.
// Dither comes from a seeded generator consumed in interleaved sample order, so
// output depends only on the seed, channel count and sample sequence, not on how
// the stream is split into write calls.
enum class Dither { none, rectangular, triangular, triangular_highpass };
struct QuantizationOptions {
    Dither dither = Dither::none;
    std::uint64_t seed = 0;
};
class Pcm16Quantizer {
  public:
    Pcm16Quantizer(std::size_t channels, QuantizationOptions = {});
    // Sizes must match and hold complete frames. NaN quantizes to zero.
    void quantize(std::span<const float> interleaved, std::span<std::int16_t> out) noexcept;
    void reset() noexcept;

  private:
    float uniform() noexcept;
    std::size_t channels_, channel_ = 0;
    QuantizationOptions options_;
    std::uint64_t state_;
    std::vector<float> previous_;
};
// Optional container metadata for WAV output.
struct OutputMetadata {
    // WAVE speaker positions (bit 0 front left ... bit 17 top back right), one
    // set bit per channel in channel order. Zero leaves the speakers unspecified
    // and writes the same file as before. A nonzero mask writes
    // WAVE_FORMAT_EXTENSIBLE; it needs WAV output, known bits only and as many
    // bits as channels, and is checked before the output is opened.
    std::uint32_t channel_mask = 0;
};
// Speaker bits libsndfile can label in WAVE_FORMAT_EXTENSIBLE (bits 0 to 17).
inline constexpr std::uint32_t known_speaker_bits = 0x3ffff;
class AudioReader {
  public:
    explicit AudioReader(const std::filesystem::path&, std::optional<RawSpec> = {});
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;
    AudioInfo info() const noexcept;
    // Frame count the file declares, when known (never for streams). Reading may
    // still fail earlier on a damaged file.
    std::optional<std::size_t> declared_frames() const noexcept;
    std::size_t read(std::span<float> interleaved);
    void close();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class AudioWriter {
  public:
    // Dither other than none requires wav_pcm16. PCM16 output always uses
    // Pcm16Quantizer; PCM24/PCM32 keep libsndfile's conversion and clipping.
    AudioWriter(const std::filesystem::path&, AudioInfo, OutputFormat, QuantizationOptions = {},
                OutputMetadata = {});
    ~AudioWriter();
    AudioWriter(const AudioWriter&) = delete;
    AudioWriter& operator=(const AudioWriter&) = delete;
    void write(std::span<const float> interleaved);
    void close();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// IR audio file, legacy .cfg, or configuration list. A list selects the first
// entry matching both expected properties; zero means unspecified.
Configuration load_configuration(const std::filesystem::path&, int expected_rate = 0,
                                 std::size_t expected_inputs = 0,
                                 std::vector<std::filesystem::path>* source_paths = nullptr);
const char* sndfile_version() noexcept;
} // namespace convolver
