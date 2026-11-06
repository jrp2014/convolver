// SPDX-License-Identifier: GPL-2.0-or-later
// The loading-responsiveness gate (TODO.md, "Loading responsiveness"): times
// each plug-in format's own load entry points while another thread renders
// audio at the host's pace, as a host plays while a user picks a preset or a
// filter file.
//
// For each workload (a long stereo filter) and entry point it measures three
// phases on a running instance: a first load into an empty instance, a
// replacement of that filter by another as long, and a reset of processing
// while the second plays. With Options::warm, the replacement and the reset
// start from a full input history (PERF-14): the renderer first plays the filter's length of
// audio as fast as it can, as if it had been playing for that long (the
// engine only processes the parts of a filter that have heard input), and
// half a second of each filter playing at that full cost is shown before it
// is replaced or reset ("warm first", "warm second"), but not judged: no
// limit for it has been decided. Each phase reports how long the calling thread was
// held (the host's thread for state and window loads, the worker for LV2's
// worker), and the audio callbacks meanwhile, each recorded (PERF-12): when
// it was due, when the thread woke for it, when it entered and left the
// plug-in, and the thread's CPU time in the call where the platform gives
// it. A callback is late when it finishes after the next was due, and slow
// when its call takes over half its period. Half a second with nothing loading comes
// before the phases and another after them (the baselines), so scheduling
// noise is visible.
//
// The limits (jrp, 5 November 2026), enforced only in an optimised build
// without sanitizers, where timings mean something:
// - no callback is late, and none takes more than half its period, while a
//   load, a replacement or a reset runs on another thread (not where the
//   host holds back audio: LV2's restore() and its reset);
// - a load holds the host's thread at most 100 ms with filters up to 10 s,
//   and 250 ms with 30 s; the 120 s filter's hold is reported only.
// Each run of an entry (one entry point with one filter) is judged
// (PERF-10, jrp's review of 59ac8e0):
// - failed: a load was refused, a callback failed or the wrong filter was
//   saved (in any build, and not run again); the host's thread was held
//   over the limit, whatever the audio thread saw; or callbacks broke the
//   limits by the plug-in's own time while both baselines were on time;
// - not qualified: callbacks broke the limits, but a baseline was late too
//   (the trailing one plays a loaded filter, so this is uncertainty, not
//   proof of a busy machine): timing could not be qualified; or only
//   because the audio thread woke late (started on time, the callback would
//   have finished on time), which these measurements cannot pin on the
//   plug-in; and every run in a build that does not enforce the limits. A
//   slow call with little CPU time still counts as the plug-in's: it may
//   have been descheduled or blocked on a lock, which the records cannot
//   tell apart (jrp's review of 5f47ecb), so it is reported as such;
// - reported only: no limit applies to any of its phases;
// - passed: none of these.
// An entry that did not pass runs again, up to three times, as a shared
// machine can stall one run. It passes only if one run passed, meeting both
// limits (see verdict()), and the summary keeps every run that did not. The
// test exits 1 if an entry failed, not_qualified_exit (CTest reports it as
// skipped) if one did not qualify or none was judged, and 0 only when an
// entry passed and every other passed or was reported only. judge() gives a
// run's verdict from its records alone, so the rules are tested without
// timing anything.
#pragma once
#include "lifecycle.hpp"
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <iosfwd>
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
    // PERF-14: the replacement and the reset start from a full input history
    // (slow to fill, so not in CI; REL-6's qualification runs use it).
    bool warm = false;
    // The format's reset stops audio (LV2's deactivate and activate), so its
    // callbacks have no limit.
    bool reset_holds_audio = false;
    // Judge by the limits; otherwise every run is "not qualified" and only
    // reports what would exceed them.
    bool enforce = optimised_build();
    int attempts = 3; // Runs of an entry, until one passes (when enforced).
};

// Sets long_filters and warm from "--long" and "--warm" among argv[first]
// onwards, as the formats' load-timing commands take them.
void read_flags(Options&, int argc, char** argv, int first);

// The input the audio thread plays: noise from -0.1 to 0.1 on every channel,
// different for each.
std::vector<float> quiet_noise(std::size_t channel, std::size_t frames);

// The workloads until PERF-3 defines the supported ones: stereo at 48 kHz in
// 512-frame callbacks, filters of 1, 10 and 30 seconds (and 120 with
// long_filters).
std::vector<Workload> workloads(const Options&);

// PERF-12: one audio callback as the renderer timed it, in seconds from the
// start of the run. The schedule restarts after a late callback, as a device
// that dropped a buffer carries on.
struct CallbackRecord {
    double due = 0;      // When the host's device asked for it.
    double woke = 0;     // When the audio thread started it.
    double entered = 0;  // When the plug-in was called, after any hold-back.
    double returned = 0; // When the plug-in returned.
    double cpu = -1;     // The thread's CPU time in the call; -1 where not measured.
    bool ok = true;      // The call succeeded and did not throw.
};

// What the audio thread saw in one phase, from its records. A callback is
// late when it returns after the next one was due, and slow when the call
// takes more than half its period. A late callback is not the plug-in's own
// time if it would have been on time had it started on time (the thread
// woke late). A slow call with little CPU time is counted apart, but is
// still the plug-in's: blocked or descheduled, the records cannot tell.
struct Callbacks {
    std::size_t count = 0, failed = 0;
    std::size_t late = 0, late_scheduling = 0; // Of late: the thread woke late.
    std::size_t slow = 0, slow_off_cpu = 0;    // Of slow: under half a period of CPU time.
    std::size_t missed = 0;                    // Deadlines that passed while a late callback ran.
    double worst_late = 0;                     // Seconds past the next deadline, at worst.
    double worst_wake = 0;                     // Seconds from due to the start, at worst.
    double longest = 0;      // Seconds from start to return (any hold-back included).
    double longest_call = 0; // Seconds in the plug-in.
};
Callbacks summarise(const std::vector<CallbackRecord>&, double period);

// One phase of a run: the baselines with nothing loading (before the phases
// and after them), a first load, a replacement and a reset, each filter
// playing at full history before the last two (reported only).
struct Phase {
    std::string name;
    bool baseline = false;      // Nothing loads: shows scheduling noise.
    std::optional<double> held; // Seconds the calling thread was held, for a load.
    bool accepted = true;       // The load was accepted.
    bool audio_held = false;    // The host held audio back: callbacks reported only.
    // Nothing loads and no limit applies: shown, never judged (PERF-14's
    // full history, until a limit for playing a long filter is decided).
    bool reported = false;
    std::vector<CallbackRecord> callbacks;
    std::size_t dropped = 0; // Callbacks beyond what the renderer could record.
    std::string error;       // The first exception a callback threw.
};
// Everything one run of an entry recorded, for its verdict and its report.
struct RunRecord {
    std::string workload, entry;
    int attempt = 1;
    std::vector<Phase> phases;
    bool saved = true; // The instance then saved the replacement.
};

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
    std::size_t errors = 0;
};
// A run's verdict from its two limits: failed if either failed, else not
// qualified if either was, else reported only if neither applied, else passed.
Verdict combine(Verdict hold, Verdict callbacks);

// The limits a run is judged by.
struct Limits {
    double period = 0;          // Seconds between callbacks.
    std::optional<double> held; // For the host's thread; none: reported only.
    bool host_thread = true;    // The entry runs on the host's thread.
    bool enforce = true;        // Otherwise every run is not qualified.
};
// PERF-12: a run's verdict from its records alone, without timing anything
// (see Run): errors fail it; a hold over the limit fails it whatever the
// audio thread saw; late or slow callbacks that are the plug-in's fail it
// while both baselines were on time, and are not judged otherwise; late
// callbacks only because the thread woke late are not judged. `notes` has a line
// for each phase, empty where there is nothing to say.
struct Judged {
    Run run;
    std::vector<std::string> notes;
};
Judged judge(const RunRecord&, const Limits&);

struct Result {
    std::size_t errors = 0;         // Refused loads, failed callbacks, a wrong filter saved.
    std::vector<Run> runs;          // Every run, those repeated included, in order.
    std::vector<RunRecord> records; // What each of those runs recorded.
    // Entries (one workload, one entry point) by their verdict().
    std::size_t passed = 0, failed = 0, not_qualified = 0, reported_only = 0;
    std::size_t passed_after_failure = 0; // Of `passed`: an earlier run did not pass.
};
// An entry's verdict from its runs (jrp, review of cc2ebde: one complete
// qualifying run): failed if a run had an error; passed if one run passed;
// failed if a limit was broken with evidence in one run and met in none, or
// if every run broke a limit; reported only if no limit applied to any run;
// otherwise not qualified. So a run whose hold stalled, followed by one
// within the hold whose callbacks could not be judged, is not qualified,
// although each limit was met in some run.
Verdict verdict(std::vector<Run>::const_iterator first, std::vector<Run>::const_iterator last);
// The attempt that first met `limit` (&Run::hold or &Run::callbacks) among
// an entry's runs, or 0 if none did: the summary gives it for an entry that
// did not pass.
int met_on(std::vector<Run>::const_iterator first, std::vector<Run>::const_iterator last,
           Verdict Run::* limit);

// Runs every entry point on every workload with `driver` (the format's
// lifecycle driver), prints one line per phase and a summary that keeps every
// run that did not pass.
Result run(lifecycle::Driver& driver, const std::vector<Entry>& entries,
           const std::vector<Workload>& workloads, const Options&);

// PERF-13: every run's records as JSON, with the build, the compiler, the FFT
// backend and the machine they came from; run() writes them to
// <work_directory>/<format>-load-timing.json.
void write_records(std::ostream&, const std::string& format, const Result&,
                   const std::vector<Workload>&, const Options&);

// CTest's SKIP_RETURN_CODE for the timing tests: nothing failed, but not
// every entry qualified, so the test is reported as skipped, not passed.
constexpr int not_qualified_exit = 77;
// 1 if there were errors or an entry failed; not_qualified_exit if an entry
// did not qualify, or none passed (every one reported only, or none ran);
// otherwise 0.
int exit_code(const Result&);
} // namespace convolver::plugin_test::load_timing
