// SPDX-License-Identifier: GPL-2.0-or-later
// The loading-responsiveness gate (TODO.md, "Loading responsiveness"): times
// each plug-in format's own load entry points while another thread renders
// audio at the host's pace, as a host plays while a user picks a preset or a
// filter file.
//
// For each workload (a long stereo filter) and entry point it measures three
// phases on a running instance: a first load into an empty instance, a
// replacement of that filter by another as long, and a reset of processing
// while the second plays. Each phase reports how long the calling thread was
// held (the host's thread for state and window loads, the worker for LV2's
// worker), and the audio callbacks meanwhile: how many there were, the
// longest, and how many finished after their deadline (the start of the
// next period) and by how much. A baseline with nothing loading comes first,
// so scheduling noise on the machine is visible.
//
// The limits (jrp, 5 November 2026), enforced only in an optimised build
// without sanitizers, where timings mean something:
// - no callback is late, and none takes more than half its period, while a
//   load, a replacement or a reset runs on another thread (not where the
//   host holds back audio: LV2's restore() and its reset);
// - a load holds the host's thread at most 100 ms with filters up to 10 s,
//   and 250 ms with 30 s; the 120 s filter is reported only.
// A phase is inconclusive, not failed, when the baseline before it already
// had late callbacks: the machine was busy. An entry with a phase over the
// limits runs again, up to three times, and only the last run counts, as a
// shared machine can stall one run without a late baseline. In every build the run also
// fails when a load is refused, a callback reports an error or throws, or
// the instance does not then save the second filter.
#pragma once
#include "lifecycle.hpp"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace convolver::plugin_test::load_timing {
// A long filter for the host, as encoded host state and as a filter file (a
// .cfg naming a float WAV), made by run().
struct Filter {
    std::string name;
    Configuration configuration;
    std::vector<std::byte> state;
    std::filesystem::path file;
};

// The audio thread, for entry points that start on it (LV2's filter
// parameter arrives in run()).
class Audio {
  public:
    virtual ~Audio() = default;
    // Runs `f` on the audio thread just before its next callback, and returns
    // once that callback has finished.
    virtual void before_next(const std::function<void()>& f) = 0;
    // Returns after `n` more callbacks.
    virtual void wait_callbacks(std::size_t n) = 0;
};

struct Outcome {
    bool accepted = false;
    // How long the thread that does the work was held, when that is not the
    // whole call (LV2's worker: the request first waits for a callback).
    std::optional<std::chrono::duration<double>> held;
};

struct Entry {
    std::string name; // As the format names it: "setState", "RestoreState", ...
    std::function<Outcome(const Filter&, Audio&)> load;
    // The host holds back audio callbacks while it runs (LV2's restore(),
    // which the plug-in does not declare thread-safe): callbacks wait. Such
    // an entry must not use its Audio, whose callbacks are held back.
    bool audio_waits = false;
    // It runs on the host's thread, so its time has a limit (not LV2's
    // worker, and not a load the host stops audio for).
    bool host_thread = true;
};

struct Workload {
    std::string name;
    lifecycle::Host host;
    double seconds = 0; // Filter length.
    std::size_t block = 0;
    std::optional<std::chrono::duration<double>> held_limit; // For the host's thread.
};

struct Options {
    std::filesystem::path work_directory; // For the filter files.
    bool long_filters = false;            // Adds the 120 s filter jrp ran in GarageBand.
    // The format's reset stops audio (LV2's deactivate and activate), so its
    // callbacks have no limit.
    bool reset_holds_audio = false;
};

// The input the audio thread plays: noise from -0.1 to 0.1 on every channel,
// different for each.
std::vector<float> quiet_noise(std::size_t channel, std::size_t frames);

// The workloads until PERF-3 defines the supported ones: stereo at 48 kHz in
// 512-frame callbacks, filters of 1, 10 and 30 seconds (and 120 with
// long_filters).
std::vector<Workload> workloads(const Options&);

// Runs every entry point on every workload with `driver` (the format's
// lifecycle driver), prints one line per phase, and returns the number of
// errors: refused loads, failed callbacks and a wrong filter saved
// afterwards, and, in an optimised build, phases over the limits.
std::size_t run(lifecycle::Driver& driver, const std::vector<Entry>& entries, const Options&);
} // namespace convolver::plugin_test::load_timing
