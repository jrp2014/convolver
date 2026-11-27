// SPDX-License-Identifier: GPL-2.0-or-later
// Private: how the configuration loader opens filter files. The loader itself
// (config.cpp) links no sound-file library: the tools and the LV2 plug-in hand
// it libsndfile (io.cpp), and the Audio Unit hands it macOS's own AudioToolbox
// (audio_toolbox.cpp), so the component needs nothing beyond macOS. Not installed.
#pragma once
#include "convolver/advice.hpp"
#include "convolver/io.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace convolver::detail {
enum class FilterLayout; // config_parse.hpp
// One open filter file, read once from the start.
class SoundFile {
  public:
    virtual ~SoundFile() = default;
    virtual AudioInfo info() const noexcept = 0;
    // The frame count the file declares, when it declares one.
    virtual std::optional<std::size_t> declared_frames() const noexcept = 0;
    // EDGE-16: whether the header marks the file as Ambisonics B-format
    // (ambisonic_b_format()), read once when the file is opened.
    virtual bool ambisonic() const noexcept = 0;
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

// load_configuration(), reading filter files with `files`. `plugin`: the
// selection is a plug-in host's, so a mismatch is explained in its terms
// (explain_no_match()). `options`: the selection comes from command-line
// options alone, with no audio (convolver-preset), so the advice is to
// choose other values. `chosen_paths` receives what the selected filter was
// read from (Resolver::chosen_sources()): unlike `source_paths`, not the IRs
// of list entries passed over. `chosen_layout` receives the routing the
// result was laid out with (EDGE-3, default_layout()): as_is for an exact
// match. A plug-in or preset keeps a one-channel filter as it is, and
// chosen_layout still says each_channel. `default_layouts` false takes an
// exact match only, so a plug-in can try PLUG-33's fewer inputs before the
// default routings (read_filter_file()); a refusal then explains only that.
Configuration load_configuration(const std::filesystem::path&, const ConfigurationSelection&,
                                 const SoundFiles& files,
                                 std::vector<std::filesystem::path>* source_paths = nullptr,
                                 std::vector<std::filesystem::path>* raw_filters = nullptr,
                                 bool plugin = false, bool options = false,
                                 std::vector<std::filesystem::path>* chosen_paths = nullptr,
                                 FilterLayout* chosen_layout = nullptr,
                                 bool default_layouts = true);

// Shared by the readers.
// What the first bytes of a file say about it: a container marker no sample
// data can be mistaken for (RIFF, RIFX, RF64, BW64, riff, FORM, caff, fLaC,
// OggS, .snd, dns.), a pattern sample data often matches (a bare MPEG frame
// sync, an Akai MPC 2000 marker), or neither. Only regular files are opened.
enum class HeaderMarker { container, sample_like, none };
HeaderMarker header_marker(const std::filesystem::path&);
// IO-11: the sizes a WAV's header should have given, for a file whose writer
// never filled them in (FFmpeg writing to a pipe leaves 0xFFFFFFFF).
struct WaveSizes {
    std::uint32_t riff_size = 0;    // The file's length less 8, at offset 4.
    std::uint64_t data_size_at = 0; // Where the 'data' size is; 0 if it was given.
    std::uint32_t data_size = 0;    // The audio's length in bytes, to go there.
};
// Writes those sizes over the unfilled ones in `bytes`, which were read from
// the file at offset `position`: the file as its header should have been.
// libsndfile reads such a file to its end unaided; AudioToolbox reads it
// short unless it is shown the sizes (audio_toolbox.cpp).
void fill_in_sizes(const WaveSizes&, std::uint64_t position,
                   std::span<unsigned char> bytes) noexcept;
// Rejects a RIFF/WAVE file whose chunks or data are truncated, which libsndfile
// would accept with fewer frames, naming the file and damaged_file_advice.
// Other files pass unchecked. Returns the sizes of a WAV whose header never
// got them, which is read to the end of the file.
std::optional<WaveSizes> validate_wave(const std::filesystem::path&);
// EDGE-3: whether a file's format chunk marks it as Ambisonics B-format (the
// extensible format's B-format subtypes, as .amb files carry), which is never
// played as true stereo. EDGE-16: in a WAV, RF64, BW64 or Wave64 file,
// wherever the chunk is among the others. False for anything else, a file
// that cannot be read included: its reader explains that. Each reader asks
// once, as it opens the file (SoundFile::ambisonic()).
bool ambisonic_b_format(const std::filesystem::path&) noexcept;
// IO-12: samples stored as plain numbers, `frames` frames of info.channels
// samples each in `encoding`, from byte `offset` of a file.
struct StoredSamples {
    AudioInfo info;
    RawEncoding encoding;
    std::uint64_t offset = 0;
    std::uint64_t frames = 0;
};
// IO-12: libsndfile 1.2.2 decodes every subtype of a Wave64 file with
// WAVE_FORMAT_EXTENSIBLE as integer PCM, so 32-bit float 1.0 reads as 0.496,
// A-law and µ-law come out as noise, and 64-bit float does not open. For a
// Wave64 file whose extensible subtype is 32- or 64-bit float (FFmpeg writes
// pcm_f32le and pcm_f64le so), returns where its samples are, for
// open_stored_samples(); both readers decode them so. Throws an AdvisedError
// naming the file for a subtype that is neither PCM nor such float. Returns
// nothing for every other file, a Wave64 file it cannot parse included: its
// reader explains that.
std::optional<StoredSamples> extensible_wave64_float(const std::filesystem::path&);
// Reads `samples` from `path` itself, needing no sound-file library: the
// headerless files of the AudioToolbox reader, and extensible_wave64_float()
// files on both readers. `headerless` says the samples were read as the type
// a .cfg gave (invalid_sample_message()); `ambisonic` is
// SoundFile::ambisonic(); `hint` follows the reason a file cannot be opened.
// Throws std::runtime_error for a rate or channel count out of range.
std::unique_ptr<SoundFile> open_stored_samples(const std::filesystem::path&,
                                               const StoredSamples& samples, bool headerless,
                                               bool ambisonic, std::string_view hint = {});
// DIAG-1: what to do about a sound file that cannot be read because of its
// content, added to the reason by both readers.
inline constexpr std::string_view damaged_file_advice =
    "the file is damaged or incomplete, or is not a sound file; export it again from the "
    "program that made it";
// A headerless file or stream whose length is not a whole number of frames.
inline Advice raw_incomplete_frame() {
    return {"RAW input ends with an incomplete frame",
            "Its length is not a whole number of frames.",
            "Check the channel count and sample type given for it, or whether it was cut short.",
            {}};
}
// Where a sample is, for messages: "at 0.100 s (frame 4800), channel 2".
// Frames count from 0 and channels from 1.
std::string sample_position(std::uint64_t frame, std::size_t channel, int sample_rate);
// The message for a NaN or infinite sample read from `path` ("-": standard
// input). `raw`: the file has no header and was read as this sample type, so
// the type is the likelier cause than damage.
Advice invalid_sample_message(const std::filesystem::path& path, std::uint64_t frame,
                              std::size_t channel, int sample_rate,
                              std::optional<RawEncoding> raw = std::nullopt);
// Bytes per raw sample; throws std::invalid_argument for pcm8.
std::size_t raw_sample_bytes(SampleType);
// One raw sample as float. `width` must be raw_sample_bytes(e.sample), which
// the readers validate and keep (IO-14); a width of 0 or more than 8 decodes
// as NaN, which the readers refuse. Integers are scaled by 2^-(bits-1), as
// libsndfile does.
float decode_raw(const std::byte*, RawEncoding, std::size_t width) noexcept;
} // namespace convolver::detail
