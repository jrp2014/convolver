// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-7: lifecycle sequences for every plug-in adapter and for the shared
// host layer.
//
// A host does many things to a plug-in instance in turn: it loads filters,
// plays audio, resets, bypasses, stops and starts it, changes the rate or the
// channels, saves and reopens the session. Each of these is tested on its own
// elsewhere; the defects found in jrp's reviews of 0.4.19 and 0.4.30 came from
// combinations. This runner enumerates every sequence of operations up to a
// length, and runs a few long seeded ones, and after every operation compares
// the instance with a small reference model:
//
// - the filter that is saved (and the "load failed" flag, and VST3's bypass);
// - the failure record (PLUG-8): its kind, its request number, and whether
//   the instance plays silence;
// - the reported path and the status (LV2);
// - that the audio callbacks neither allocate nor lock (audio_guard.hpp).
//
// What is audible is compared at the end of each sequence (and wherever the
// sequence listens): silence is played until any transition, ramp and tail is
// over, then a short noise burst, which must come out as the double-precision
// oracle of the filter the model says plays (or the latency-delayed dry
// signal, identity, or exact silence). Every prefix of an enumerated sequence
// is itself enumerated, so the audible filter is checked after every
// operation without listening in between, which would change the sequence.
//
// The model is plain data and one function per operation (lifecycle.cpp). Each
// expectation cites the clause of docs/develop/plugin-contract.md or of the
// format's specification it comes from, never the implementation. A failing
// sequence prints its operations, so it can be pasted into a fixed list.
#pragma once
#include "convolver/host.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace convolver::plugin_test::lifecycle {
enum class Op {
    // Filters arriving as host state (a preset, or a session), in every format.
    state_a,                 // A valid filter, A.
    state_b,                 // Another valid filter, B.
    state_other_rate,        // A filter made for the other sample rate.
    state_malformed,         // Bytes that are not host state.
    state_empty,             // Host state with no filter: an empty instance.
    state_failed,            // A saved failed first load (state version 2).
    state_b_starved,         // B, first with memory running out at each allocation
                             // in turn; fixed sequences only (request numbers are
                             // not followed afterwards).
    state_b_shortage,        // B, first with one allocation failing, each in turn.
    state_malformed_starved, // Malformed state, first with memory running out at
                             // each allocation in turn (ROBUST-3); fixed
                             // sequences only.
    // What a host does.
    play,   // Some audio, in uneven callbacks; may leave a transition running.
    bypass, // Toggle bypass.
    reset,  // The format's reset of processing while active.
    stop,   // Deactivate.
    start,  // Activate.
    reopen, // Save the session, close the instance, open it again.
    rate,   // While stopped: switch the host to the other sample rate, or back.
    layout, // While stopped: switch the host to other output channels, or back.
    setup,  // Negotiate an instance created before the host was configured.
    listen, // Compare what is audible with the model (also done at the end).
    // LV2.
    empty_map,    // Restore a state map with none of the plug-in's properties.
    file_a,       // Choose A's file on the filter parameter (queued for the worker).
    file_missing, // Choose a file that does not exist (queued).
    file_refused, // Choose A's file while the host cannot queue work.
    work,         // The host's worker runs every queued request.
    overtake,     // A new request for A's file arrives while the worker reads the
                  // only queued one.
};
const char* name(Op);

// When a format treats the host's rate and channels as known, so that state
// is checked against them at once rather than at activation (the contract's
// "state that arrives before the host is configured").
enum class Known {
    always,     // LV2: fixed at instantiation.
    activation, // VST3: once activated with them, while they stay the same.
    rendering,  // AU: while initialized, and afterwards while they match the
                // last initialization that rendered audio (plugins/au/effect.hpp).
};

struct Format {
    std::string name;
    Known known = Known::always;
    std::vector<Op> operations; // Besides listen, which every format supports.
    bool state_bypass = false;  // Accepted state sets bypass (VST3 saves it).
    bool worker = true;         // LV2: the host provides a worker.
    bool status = false;        // LV2: the status port.
    bool path = false;          // LV2: the saved filter path.
};

// The sample rate and channels a host negotiates.
struct Host {
    int sample_rate = 0;
    std::size_t inputs = 0, outputs = 0;
    bool operator==(const Host&) const = default;
};

// The filters a sequence uses, made by make_filters(). A suits `host`; B is
// another filter for it; other_rate suits `host` at `other_rate`; `wide` has
// `other_outputs` outputs. Files are LV2's: A as a file, and a missing one.
struct Filters {
    Host host;
    int other_rate = 0;
    std::size_t other_outputs = 0;
    Configuration a, b, other_rate_filter;
    std::filesystem::path a_file, missing_file;
};
// Short synthetic filters for `host`, with uneven routing and gains, so that
// each output is distinguishable; tails are a few frames.
Filters make_filters(const Host&);

class Driver {
  public:
    virtual ~Driver() = default;
    virtual Format format() const = 0;
    // A new inactive instance with nothing loaded: negotiated for `host`
    // (activated once and stopped, as PLUG-2's create does where activation
    // binds the configuration) or, if `configured` is false, before the host
    // has negotiated anything.
    virtual void create(const Host&, bool configured) = 0;
    // While stopped: negotiate `host` (its rate and channels).
    virtual void configure(const Host&) = 0;
    virtual void destroy() = 0;
    // Encoded host state through the format's own state mechanism; true if
    // the instance accepted it. Bypass in the state is false.
    virtual bool restore(std::span<const std::byte>) = 0;
    // restore() with allocations failing in the plug-in's own call after the
    // first `allowed` (all, or only the next with `once`; audio_guard::Starve);
    // nothing if the driver cannot do that.
    virtual std::optional<bool> restore_starved(std::span<const std::byte>, std::size_t /*allowed*/,
                                                bool /*once*/) {
        return std::nullopt;
    }
    virtual bool start() = 0;
    virtual void stop() = 0;
    // One callback; inputs and outputs are planar. False: a processing error.
    virtual bool process(std::span<const float* const>, std::span<float* const>,
                         std::size_t frames) = 0;
    virtual void set_bypass(bool) = 0; // From the next callback on.
    virtual void reset() = 0;
    // As a host reopens a project: saves the session, closes the instance and
    // opens a new one, restores the session (before negotiating, where the
    // format allows that), negotiates `host`, restores host-owned bypass and
    // starts it if `active`. True if the restore was accepted.
    virtual bool reopen(const Host&, bool active, bool bypass) = 0;
    // What the instance would save now.
    virtual HostState saved() = 0;
    virtual std::optional<HostFailure> failure() = 0;
    virtual std::size_t max_block() const = 0;
    // LV2.
    virtual std::string saved_path() {
        return {};
    }
    virtual int status() {
        return -1;
    }
    virtual bool restore_empty_map() {
        return false;
    }
    // Sends the request in a run() call of no frames; `refused`: the host
    // cannot queue it.
    virtual void choose_file(const std::filesystem::path&, bool /*refused*/) {}
    virtual void work() {}
    virtual void overtake(const std::filesystem::path&) {}
};

// Where an enumeration starts: a new instance, negotiated or not, after a
// fixed prologue. Prologues reach deeper states (a filter playing) without
// enumerating the operations that lead there.
struct Start {
    bool configured = true;
    std::vector<Op> prologue;
};
struct Options {
    // Every allowed sequence of up to `length` operations after each start.
    std::size_t length = 4;
    // Default: configured and empty; unconfigured (formats with `setup`);
    // started with A playing.
    std::vector<Start> starts;
    unsigned seeds = 8;                 // Seeded sequences of `seeded_length` operations.
    std::size_t seeded_length = 40;     //
    std::vector<std::vector<Op>> fixed; // Regressions and scripted cases, run first.
};
// The default starts for a format.
std::vector<Start> default_starts(const Format&);

// Runs the fixed, enumerated and seeded sequences for `filters`. Prints each
// failing sequence and a summary; returns the number of failures.
std::size_t run(Driver&, const Filters&, const Options& = {});
// A sequence written as names separated by spaces, as failures print them.
std::vector<Op> parse(const std::string&);
} // namespace convolver::plugin_test::lifecycle
