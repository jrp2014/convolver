// SPDX-License-Identifier: GPL-2.0-or-later
// Private: how the configuration loader opens filter files. The loader itself
// (config.cpp) links no sound-file library: the tools and the LV2 plug-in hand
// it libsndfile (io.cpp), and the Audio Unit hands it macOS's own AudioToolbox
// (audio_toolbox.cpp), so the component needs nothing beyond macOS. Not installed.
#pragma once
#include "convolver/io.hpp"
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace convolver::detail {
// One open filter file, read once from the start.
class SoundFile {
  public:
    virtual ~SoundFile() = default;
    virtual AudioInfo info() const noexcept = 0;
    // The frame count the file declares, when it declares one.
    virtual std::optional<std::size_t> declared_frames() const noexcept = 0;
    // Fills whole frames, interleaved; returns the frames read, zero at the end.
    // Throws std::runtime_error on a read failure or a nonfinite sample.
    virtual std::size_t read(std::span<float> interleaved) = 0;
    virtual void close() = 0;
};
// A sound-file reader. Every member throws std::runtime_error (or
// std::invalid_argument for a bad caller argument) saying why a file cannot
// be read.
struct SoundFiles {
    // A file read through its own header.
    std::unique_ptr<SoundFile> (*open)(const std::filesystem::path&);
    // A headerless file of `spec` channels at its rate, stored as `encoding`.
    std::unique_ptr<SoundFile> (*open_raw)(const std::filesystem::path&, RawSpec spec,
                                           RawEncoding encoding);
    // Whether the content is a sound file, as sound_file_header() decides for
    // libsndfile: a .pcm or .raw filter is headerless unless this is true.
    bool (*recognised)(const std::filesystem::path&);
};
// libsndfile, as the command line reads files (io.cpp, in Convolver::io).
const SoundFiles& libsndfile_files();
#ifdef __APPLE__
// macOS's AudioToolbox (audio_toolbox.cpp, in convolver_audio_toolbox).
const SoundFiles& audio_toolbox_files();
#endif

// load_configuration(), reading filter files with `files`.
Configuration load_configuration(const std::filesystem::path&, const ConfigurationSelection&,
                                 const SoundFiles& files,
                                 std::vector<std::filesystem::path>* source_paths = nullptr,
                                 std::vector<std::filesystem::path>* raw_filters = nullptr);

// Shared by the readers.
// What the first bytes of a file say about it: a container marker no sample
// data can be mistaken for (RIFF, RIFX, RF64, BW64, riff, FORM, caff, fLaC,
// OggS, .snd, dns.), a pattern sample data often matches (a bare MPEG frame
// sync, an Akai MPC 2000 marker), or neither. Only regular files are opened.
enum class HeaderMarker { container, sample_like, none };
HeaderMarker header_marker(const std::filesystem::path&);
// Rejects a RIFF/WAVE file whose chunks or data are truncated, which libsndfile
// would accept with fewer frames. Other files pass unchecked.
void validate_wave(const std::filesystem::path&);
// Bytes per raw sample; throws std::invalid_argument for pcm8.
std::size_t raw_sample_bytes(SampleType);
// One raw sample as float. Integers are scaled by 2^-(bits-1), as libsndfile does.
float decode_raw(const std::byte*, RawEncoding) noexcept;
} // namespace convolver::detail
