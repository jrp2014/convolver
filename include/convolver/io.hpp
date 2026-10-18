// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/engine.hpp"
#include <filesystem>
#include <memory>
#include <optional>
#include <span>

namespace convolver {
struct AudioInfo {
    int sample_rate;
    std::size_t channels;
};
// RAW streaming is little-endian IEEE float32, with explicit metadata.
using RawSpec = AudioInfo;
enum class OutputFormat { wav_float, wav_pcm16, wav_pcm24, wav_pcm32, raw_float };
class AudioReader {
  public:
    explicit AudioReader(const std::filesystem::path&, std::optional<RawSpec> = {});
    ~AudioReader();
    AudioReader(const AudioReader&) = delete;
    AudioReader& operator=(const AudioReader&) = delete;
    AudioInfo info() const noexcept;
    std::size_t read(std::span<float> interleaved);
    void close();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class AudioWriter {
  public:
    AudioWriter(const std::filesystem::path&, AudioInfo, OutputFormat);
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
