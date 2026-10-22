// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-2: one comparison harness for every plug-in adapter.
//
// Each format's test implements Adapter by driving its own host API (VST3
// interfaces, an LV2 host library, an AU graph) and calls run(). The harness
// owns the cases and the criteria, so every format is held to the same rules:
//
// - the independent double-precision oracle (tests/native/oracle.hpp), at most
//   1e-5 absolute for the fixtures' noise input; it is the authority;
// - the CLI render of the same fixture, which a fresh adapter must reproduce
//   exactly once its latency is allowed for (both run the same engine);
// - exact latency (256 frames), exact tail for a fresh processor, and the
//   never-shrinking tail bound after live replacements;
// - the shared adapter contract in docs/plans/2026-10-21-plugin-formats.md.
//
// A format that does not yet follow part of the contract lists those cases as
// known deviations with the TODO item that fixes them. A listed case that
// starts passing fails the run, so the list cannot go stale.
#pragma once
#include "convolver/engine.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace convolver::plugin_test {
inline constexpr std::size_t latency = 256;
inline constexpr double tolerance = 1e-5;

// A filter to load. Fixtures have a source file (the .cfg or IR a user would
// pick); variants made by the harness (replacements, mismatches) have only the
// configuration, which adapters load through their embedded state.
struct Filter {
    std::string name;
    std::filesystem::path source;
    Configuration configuration;
};

// What the host negotiates before activation.
struct Setup {
    int sample_rate = 0;
    std::size_t inputs = 0, outputs = 0, max_block = 0;
};

// One host callback. Inputs and outputs are planar; in-place callbacks pass the
// same buffers for both (for channels that exist on both sides). Silent input
// channels hold zeros and are flagged, as a host's silence flags would say.
struct Block {
    std::span<const float* const> inputs;
    std::span<float* const> outputs;
    std::size_t frames = 0;
    std::uint64_t silent_inputs = 0;
};

class Adapter {
  public:
    virtual ~Adapter() = default;
    virtual std::string format() const = 0;
    // A new instance configured for the setup, inactive, with nothing loaded.
    virtual void create(const Setup&) = 0;
    virtual void destroy() = 0;
    // Load a filter through the format's own state or parameter mechanism.
    // False means the adapter rejected it (the host would see an error).
    virtual bool load(const Filter&) = 0;
    // Start and stop processing (VST3 setActive and setProcessing, LV2
    // activate/deactivate, AU Initialize/Uninitialize).
    virtual bool activate() = 0;
    virtual void deactivate() = 0;
    // False means the adapter reported a processing error.
    virtual bool process(const Block&) = 0;
    // Takes effect at the start of the next process call.
    virtual void set_bypass(bool) = 0;
    // The format's reset of processing history while active.
    virtual void reset() = 0;
    // In frames: an AU adapter converts its seconds.
    virtual std::size_t latency_frames() = 0;
    virtual std::size_t tail_frames() = 0;
    // The filter the instance would save now; nothing for an empty instance.
    virtual std::optional<Configuration> saved() = 0;
    // Outputs marked silent by the last process call, where the format has flags.
    virtual std::optional<std::uint64_t> silent_outputs() {
        return std::nullopt;
    }
};

struct Deviation {
    std::string case_name; // Exact case name, as printed, e.g. "empty-instance[crossover]".
    std::string reason;    // What differs and the TODO item that fixes it.
};

struct Options {
    std::vector<Deviation> known_deviations;
    std::size_t max_block = 1024; // The host maximum; also one of the block sizes tested.
};

// Run every case for every fixture listed in fixtures.txt, as written by
// `tests/plugins/fixtures.py run-documented`. Prints one line per case and a
// summary; returns a process exit status.
int run(Adapter&, const std::filesystem::path& fixtures, const Options& = {});
} // namespace convolver::plugin_test
