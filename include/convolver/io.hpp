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
// Integer WAV formats: wav_pcm8 is unsigned 8-bit; the others are signed.
enum class OutputFormat { wav_float, wav_pcm16, wav_pcm24, wav_pcm32, raw_float, wav_pcm8 };
// Container bits of an integer output format, or zero for float formats.
unsigned container_bits(OutputFormat) noexcept;

// Integer quantization, shared by every integer output format. With B
// significant (valid) bits, samples are scaled by 2^(B-1), offset by the dither
// (in LSBs of B bits) and the noise-shaping feedback, rounded to nearest with
// ties toward +infinity, then clipped to [-2^(B-1), 2^(B-1) - 1].
// - none: plain rounding.
// - rectangular: uniform dither in [-0.5, 0.5) LSB.
// - triangular: independent TPDF dither, sum of two uniforms, in (-1, 1) LSB.
// - triangular_highpass: the historical engine's "Triangular" choice, the
//   per-channel difference of successive uniform values, also in (-1, 1) LSB.
// Dither comes from a seeded generator consumed in interleaved sample order, so
// output depends only on the seed, channel count and sample sequence, not on how
// the stream is split into write calls.
enum class Dither { none, rectangular, triangular, triangular_highpass };
// The historical engine's error-feedback noise shapers. Each subtracts a fixed
// FIR combination of the channel's previous quantization errors, H = sum c_k
// e[n-k], before rounding, so the total error spectrum is E(z)(1 - sum c_k z^-k):
// - simple: c = {1}, first-order high-pass.
// - second_order: c = {1.287, -0.651}.
// - third_order: c = {1.329, -0.735, 0.0646}.
// - ninth_order: nine coefficients from 2.203 (psychoacoustically weighted).
// - sony_sbm: twelve coefficients from 1.47933 ("Pseudo Sony SBM").
// The fed-back error is the rounding error including dither, taken before
// clipping, so it stays bounded and the shaper cannot run away after overload.
enum class NoiseShaping { none, simple, second_order, third_order, ninth_order, sony_sbm };
struct QuantizationOptions {
    Dither dither = Dither::none;
    std::uint64_t seed = 0;
    NoiseShaping noise_shaping = NoiseShaping::none;
    // Significant bits in each integer sample, 8 up to the container size; zero
    // means the whole container. Fewer bits are stored left-justified, with the
    // low bits zero, in a WAVE_FORMAT_EXTENSIBLE file declaring them as
    // wValidBitsPerSample (for example 20 valid bits in a 24-bit container).
    unsigned valid_bits = 0;
};
class Quantizer {
  public:
    // bits: significant bits, 8 to 32. Throws on invalid channels, bits or options.
    Quantizer(std::size_t channels, unsigned bits, QuantizationOptions = {});
    // Sizes must match and hold complete frames. Results lie in the B-bit range.
    // NaN quantizes to zero; neither NaN nor infinity disturbs the shaper state.
    void quantize(std::span<const float> interleaved, std::span<std::int32_t> out) noexcept;
    void reset() noexcept;
    unsigned bits() const noexcept {
        return bits_;
    }

  private:
    float uniform() noexcept;
    float dither() noexcept;
    std::size_t channels_, channel_ = 0;
    unsigned bits_;
    double scale_, low_, high_;
    QuantizationOptions options_;
    std::uint64_t state_;
    std::vector<float> previous_;
    std::vector<double> coefficients_, errors_; // errors_: channels x order, newest first
};
// The 16-bit case of Quantizer, kept for existing callers.
class Pcm16Quantizer {
  public:
    Pcm16Quantizer(std::size_t channels, QuantizationOptions = {});
    void quantize(std::span<const float> interleaved, std::span<std::int16_t> out) noexcept;
    void reset() noexcept;

  private:
    Quantizer quantizer_;
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
    // Every integer format is quantized by Quantizer with these options; float
    // and RAW formats reject dither, noise shaping and valid bits.
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
