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
// Each run of an entry (one entry point with one filter) is judged
// (PERF-10, jrp's review of 59ac8e0):
// - failed: a load was refused, a callback failed or the wrong filter was
//   saved (in any build, and not run again); the host's thread was held
//   over the limit, whatever the audio thread saw; or callbacks broke the
//   limits after an on-time baseline;
// - not qualified: callbacks broke the limits, but the baseline before them
//   was already late (a busy machine), so the run shows nothing either way;
//   and every run in a build that does not enforce the limits;
// - reported only: no limit applies to any of its phases;
// - passed: none of these.
// An entry that did not pass runs again, up to three times, as a shared
// machine can stall one run; each limit is then met if one run met it (see
// verdict()), and the summary keeps every run that did not pass. The test exits 1 if an entry
// failed, not_qualified_exit (CTest reports it as skipped) if one did not qualify, and 0 only when
// every entry passed or was reported only.
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

// True in an optimised build without sanitizers, the only build whose
// timings mean something.
bool optimised_build();

struct Options {
    std::filesystem::path work_directory; // For the filter files.
    bool long_filters = false;            // Adds the 120 s filter jrp ran in GarageBand.
    // The format's reset stops audio (LV2's deactivate and activate), so its
    // callbacks have no limit.
    bool reset_holds_audio = false;
    // Judge by the limits; otherwise every run is "not qualified" and only
    // reports what would exceed them.
    bool enforce = optimised_build();
    int attempts = 3; // Runs of an entry, until one passes (when enforced).
};

// The input the audio thread plays: noise from -0.1 to 0.1 on every channel,
// different for each.
std::vector<float> quiet_noise(std::size_t channel, std::size_t frames);

// The workloads until PERF-3 defines the supported ones: stereo at 48 kHz in
// 512-frame callbacks, filters of 1, 10 and 30 seconds (and 120 with
// long_filters).
std::vector<Workload> workloads(const Options&);

// PERF-10: what one run of an entry with one workload showed. "Not qualified"
// is not a pass: the run could not show that the limits hold.
// "Reported only": no limit applies to any of the run's phases (LV2's
// restore(), for which the host stops audio; the worker's own time), so it
// neither passes nor fails.
enum class Verdict { passed, failed, not_qualified, reported_only };
const char* describe(Verdict);
struct Run {
    std::string workload, entry;
    int attempt = 1;
    Verdict verdict = Verdict::passed; // combine(hold, callbacks), or failed on an error.
    std::string why;                   // What was over the limits, or could not be judged.
    // Each limit on its own: the hold of the host's thread, and the callbacks.
    Verdict hold = Verdict::reported_only, callbacks = Verdict::reported_only;
    bool error = false; // A refused load, a failed callback or the wrong filter saved.
};
// A run's verdict from its two limits: failed if either failed, else not
// qualified if either was, else reported only if neither applied, else passed.
Verdict combine(Verdict hold, Verdict callbacks);
struct Result {
    std::size_t errors = 0; // Refused loads, failed callbacks, a wrong filter saved.
    std::vector<Run> runs;  // Every run, those repeated included, in order.
    // Entries (one workload, one entry point) by their verdict().
    std::size_t passed = 0, failed = 0, not_qualified = 0, reported_only = 0;
    std::size_t passed_after_failure = 0; // Of `passed`: an earlier run did not pass.
};
// An entry's verdict from its runs: failed if a run had an error; otherwise
// each limit is met if one run met it, else failed if one run failed it,
// else not qualified if one could not judge it, else does not apply; and
// the two combine as for a run. So a run whose hold stalled once, followed
// by runs within it, meets the hold limit.
Verdict verdict(std::vector<Run>::const_iterator first, std::vector<Run>::const_iterator last);

// Runs every entry point on every workload with `driver` (the format's
// lifecycle driver), prints one line per phase and a summary that keeps every
// run that did not pass.
Result run(lifecycle::Driver& driver, const std::vector<Entry>& entries,
           const std::vector<Workload>& workloads, const Options&);

// CTest's SKIP_RETURN_CODE for the timing tests: nothing failed, but not
// every entry qualified, so the test is reported as skipped, not passed.
constexpr int not_qualified_exit = 77;
// 1 if there were errors or an entry failed, not_qualified_exit if an entry
// did not qualify, otherwise 0.
int exit_code(const Result&);
} // namespace convolver::plugin_test::load_timing
