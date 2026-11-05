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
// Measurement only: jrp sets the limits for the caller's time and for late
// callbacks. Until then the run fails only when a load is refused or a
// callback reports an error. Timings mean something only in an optimised
// build without sanitizers; the report says which build ran.
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
    // which the plug-in does not declare thread-safe): callbacks wait.
    bool audio_waits = false;
};

struct Workload {
    std::string name;
    lifecycle::Host host;
    double seconds = 0; // Filter length.
    std::size_t block = 0;
};

struct Options {
    std::filesystem::path work_directory; // For the filter files.
    bool long_filters = false;            // Adds the 120 s filter jrp ran in GarageBand.
};

// The workloads until PERF-3 defines the supported ones: stereo at 48 kHz in
// 512-frame callbacks, filters of 1, 10 and 30 seconds (and 120 with
// long_filters).
std::vector<Workload> workloads(const Options&);

// Runs every entry point on every workload with `driver` (the format's
// lifecycle driver), prints one line per phase, and returns the number of
// errors: refused loads and failed callbacks. Timings never fail the run.
std::size_t run(lifecycle::Driver& driver, const std::vector<Entry>& entries, const Options&);
} // namespace convolver::plugin_test::load_timing
