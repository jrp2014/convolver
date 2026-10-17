// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace convolver {
struct Route { std::size_t channel; float gain = 1; };
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

// Prepared single-precision uniformly partitioned convolution using FFTW.
// Construction/destruction must be outside processing and are serialized for
// FFTW planning. Each instance has independent processing state.
class Engine {
public:
    explicit Engine(const Configuration&, std::size_t block_frames = 256);
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
    void process(std::span<const float> input, std::span<float> output,
                 std::size_t frames);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
