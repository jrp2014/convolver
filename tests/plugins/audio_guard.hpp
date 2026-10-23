// SPDX-License-Identifier: GPL-2.0-or-later
// Counts what a plug-in does on the audio thread that the shared adapter
// contract forbids there ("Nothing on the audio thread allocates, locks or
// does I/O"): allocations through operator new, and, on Linux, mutex locks.
//
// Link audio_guard.cpp into a test executable to replace the global operator
// new and delete (and, on Linux, pthread_mutex_lock). Open a Scope around
// each call into the plug-in's audio thread; a Pause inside it covers host
// code the plug-in calls back (an LV2 host queuing work, for example), which
// is the host's business. A plug-in loaded at run time (LV2) shares these
// definitions only if the executable exports its symbols (CMake
// ENABLE_EXPORTS); self_test() shows whether it does.
#pragma once
#include <cstddef>

namespace convolver::plugin_test::audio_guard {
struct Counts {
    std::size_t allocations = 0, locks = 0;
};
// What open Scopes on this thread have counted since the last take().
Counts take() noexcept;
// Whether mutex locks are counted on this platform (Linux only).
bool counts_locks() noexcept;

class Scope {
  public:
    Scope() noexcept;
    ~Scope();
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};
class Pause {
  public:
    Pause() noexcept;
    ~Pause();
    Pause(const Pause&) = delete;
    Pause& operator=(const Pause&) = delete;
};
// While open, this thread's allocations after the first `allowed` fail with
// std::bad_alloc (nothrow forms return null), as when memory runs out; with
// `once`, only the next one fails and memory then returns. A Pause exempts
// host code, as it does from counting.
class Starve {
  public:
    explicit Starve(std::size_t allowed, bool once = false) noexcept;
    ~Starve();
    Starve(const Starve&) = delete;
    Starve& operator=(const Starve&) = delete;
};
} // namespace convolver::plugin_test::audio_guard
