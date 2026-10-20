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
    // Paths sharing an input mix share history, so this is an upper bound.
    std::size_t accounted_buffer_bytes = 0;
    std::vector<std::size_t> partitions_per_path;
};
ConfigurationAnalysis analyze_configuration(const Configuration&, std::size_t block_frames = 256);
// Validate dimensions, coefficients and prepared-storage limits without FFT planning.
void validate_configuration(const Configuration&, std::size_t block_frames = 256);
// Validate and return FIR/routing support, excluding latency, without FFT planning.
std::size_t configuration_tail_frames(const Configuration&, std::size_t block_frames = 256);

// The historical engine's "optimum attenuation": the gain that keeps output
// within full scale (|sample| <= 1) for input within full scale.
struct AttenuationEstimate {
    // Historical method: the largest |output| over the whole response to seeded
    // uniform white noise in [-1, 1) on every input, lengths times the longest
    // impulse long (at least 65536 frames), and -20 log10 of it. Typical
    // programme peaks, not a bound.
    double noise_peak = 0;
    double noise_attenuation_db = 0;
    // The largest |output| any input within full scale can produce: for each
    // output, the sum over inputs of the absolute taps of the combined
    // input-to-output response (route gains included, delays irrelevant).
    // Applying worst_case_attenuation_db guarantees no clipping.
    double worst_case_peak = 0;
    double worst_case_attenuation_db = 0;
};
// Validates like the Engine. A zero peak (silent routing) gives 0 dB. Runs the
// convolution offline with estimate planning: not for the audio thread.
AttenuationEstimate estimate_attenuation(const Configuration&, std::uint64_t seed = 0,
                                         std::size_t lengths = 10);

// Backend selected when building the library; strings have static lifetime.
const char* fft_backend_name() noexcept;
const char* fft_backend_version() noexcept;

// FFTW planner rigour, as the historical engine offered: estimate (no timing),
// measure, patient and exhaustive try progressively more algorithms.
enum class FftPlanning { estimate, measure, patient, exhaustive };
struct FftOptions {
    FftPlanning planning = FftPlanning::estimate;
    bool wisdom_only = false; // Fail if the requested plans are not cached.
    // Upper bound in seconds on each plan's search (FFTW returns the best plan
    // found so far); zero means unlimited. The historical "Take 1 minute" choice
    // was patient with 30 s each for the forward and inverse plans.
    double time_limit_seconds = 0;
};
// FFTW wisdom is process-global. These operations are serialized with planning,
// and must run outside audio processing. Other backends reject wisdom operations.
bool fft_supports_wisdom() noexcept;
void import_fft_wisdom(std::string_view wisdom);
std::string export_fft_wisdom();
// Prepare this block size's forward/inverse plans; no audio or filter is needed.
// Use measure, patient or exhaustive; export_fft_wisdom() then saves accumulated
// plans. The FftOptions form also takes a time limit (wisdom_only must be false).
void tune_fft(std::size_t block_frames, FftPlanning = FftPlanning::patient);
void tune_fft(std::size_t block_frames, FftOptions);

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
