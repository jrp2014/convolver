// SPDX-License-Identifier: GPL-2.0-or-later
// Port numbers and status values of the LV2 plug-ins, shared by the adapter
// and its tests. generate_ttl.py writes the same numbers into convolver.ttl;
// lv2.adapter checks that the two agree.
#pragma once
#include "convolver/host.hpp"
#include <cstddef>
#include <cstdint>

namespace convolver::lv2 {
// Every layout has the three control ports first, then its audio inputs
// (symbols in_1 ... in_N), then its audio outputs (out_1 ... out_M), then the
// two atom ports for patch messages (symbols control and notify).
inline constexpr std::uint32_t enabled_port = 0; // lv2:enabled: 1 processes, 0 bypasses.
inline constexpr std::uint32_t latency_port = 1; // Output: the reported latency in frames.
inline constexpr std::uint32_t status_port = 2;  // Output: one of the Status values.
inline constexpr std::uint32_t first_audio_port = 3;
// Input atom sequence: patch:Set and patch:Get for the filter parameter.
constexpr std::uint32_t control_port(std::size_t inputs, std::size_t outputs) {
    return first_audio_port + static_cast<std::uint32_t>(inputs + outputs);
}
// Output atom sequence: patch:Set naming the filter file that plays.
constexpr std::uint32_t notify_port(std::size_t inputs, std::size_t outputs) {
    return control_port(inputs, outputs) + 1;
}
// Size the atom ports ask for (rsz:minimumSize): a patch message with the
// longest path the plug-in accepts.
inline constexpr std::uint32_t atom_buffer_bytes = 8192;

// What the status port shows. Loading: a filter file is being loaded.
enum class Status : int { running = 0, no_filter = 1, load_failed = 2, loading = 3 };

// The filter parameter (<base>filter, an atom:Path, patch:writable) and the
// longest path it takes, in bytes without the terminating NUL. Longer paths
// are refused with "load failed".
inline constexpr const char* filter_suffix = "filter";
inline constexpr std::size_t max_path_bytes = 4096;

// Callback capacity when the host gives no bufsz:maxBlockLength; longer
// run() calls are processed in pieces of at most this many frames.
inline constexpr std::uint32_t default_max_block = 4096;
inline constexpr std::uint32_t largest_max_block = 65536;

// For the tests only, never advertised in convolver.ttl: extension_data() with
// this URI gives the PLUG-8 failure record, which hosts see only as the status
// port and the log message. Not a stable interface; it may change any time.
inline constexpr const char* test_failure_uri = "urn:convolver:lv2-test:failure";
struct TestFailureAccess {
    // Calls `receive` with the instance's failure record (null if none) under
    // the control lock. Passes a C++ object, so the caller must be built with
    // the same compiler and standard library (the tests are).
    void (*read)(void* instance, void* context, void (*receive)(void*, const HostFailure*));
};
// Also for the tests only: pauses inside the worker, at the points where a
// newer request can overtake a load (docs/project/plans/2026-10-21-plugin-formats.md
// #lv2-adapter-design). work() calls `pause(context, point)` under the control
// lock, so the caller must not make control calls until it returns.
inline constexpr const char* test_worker_uri = "urn:convolver:lv2-test:worker";
enum class WorkerPoint : int {
    prepared = 1, // The new filter is prepared; the final generation check is next.
    checked = 2,  // The final check passed; publication is next.
    read = 3,     // The file was read (or failed to read); nothing is recorded yet.
};
struct TestWorkerAccess {
    // Sets or (with null) clears the pause hook, under the control lock.
    void (*set_pause)(void* instance, void* context, void (*pause)(void*, WorkerPoint));
};
} // namespace convolver::lv2
