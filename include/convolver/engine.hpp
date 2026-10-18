// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace convolver {
struct Route {
    std::size_t channel;
    float gain = 1;
};
struct FilterPath {
    std::vector<float> impulse;
    std::vector<Route> inputs, outputs;
};
struct Configuration {
    int sample_rate = 0;
    std::size_t input_channels = 0, output_channels = 0;
    std::uint32_t channel_mask = 0;
    std::vector<std::size_t> input_delays, output_delays;
    std::vector<FilterPath> paths;
};

// Timing and storage an Engine would have for this configuration and block size,
// computed by the same validation used at construction, without FFT planning or
// spectral allocation. Throws as validate_configuration does.
struct ConfigurationAnalysis {
    std::size_t latency_frames = 0; // Engine::latency_frames()
    std::size_t tail_frames = 0;    // Engine::tail_frames()
    std::size_t fft_frames = 0;     // Real transform length per partition.
    // Prepared float buffers the engine accounts against its storage limit: block
    // and delay lines, routes, filter/input spectra, overlap and backend transform
    // buffers. Excludes FFT plan internals, allocator overhead and file decoding.
    std::size_t accounted_buffer_bytes = 0;
    std::vector<std::size_t> partitions_per_path;
};
ConfigurationAnalysis analyze_configuration(const Configuration&, std::size_t block_frames = 256);
// Validate dimensions, coefficients and prepared-storage limits without FFT planning.
void validate_configuration(const Configuration&, std::size_t block_frames = 256);
// Validate and return FIR/routing support, excluding latency, without FFT planning.
std::size_t configuration_tail_frames(const Configuration&, std::size_t block_frames = 256);

// Backend selected when building the library; strings have static lifetime.
const char* fft_backend_name() noexcept;
const char* fft_backend_version() noexcept;

enum class FftPlanning { estimate, measure, patient };
struct FftOptions {
    FftPlanning planning = FftPlanning::estimate;
    bool wisdom_only = false; // Fail if the requested plans are not cached.
};
// FFTW wisdom is process-global. These operations are serialized with planning,
// and must run outside audio processing. Other backends reject wisdom operations.
bool fft_supports_wisdom() noexcept;
void import_fft_wisdom(std::string_view wisdom);
std::string export_fft_wisdom();
// Prepare this block size's forward/inverse plans; no audio or filter is needed.
// Use measure or patient; export_fft_wisdom() then saves accumulated plans.
void tune_fft(std::size_t block_frames, FftPlanning = FftPlanning::patient);

// Prepared single-precision uniformly partitioned convolution.
// Construction/destruction must be outside processing and are serialized for
// FFTW planning. Each instance has independent processing state.
class Engine {
  public:
    explicit Engine(const Configuration&, std::size_t block_frames = 256, FftOptions = {});
    ~Engine();
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    std::size_t input_channels() const noexcept;
    std::size_t output_channels() const noexcept;
    std::size_t latency_frames() const noexcept;
    // FIR/routing support after the last input frame; excludes latency.
    std::size_t tail_frames() const noexcept;
    void reset() noexcept;
    // Exact interleaved frame counts; empty input means silence (drain).
    // Nonempty spans must contain at least frames * channels samples.
    // Input and output must not overlap. No allocation/planning in processing.
    void process(std::span<const float> input, std::span<float> output, std::size_t frames);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace convolver
