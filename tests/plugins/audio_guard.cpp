// SPDX-License-Identifier: GPL-2.0-or-later
#include "audio_guard.hpp"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <new>
#if defined(_WIN32)
#include <malloc.h>
#endif
// Linux counts locks by defining pthread_mutex_lock, except under
// ThreadSanitizer, which needs its own definition to see the locks.
#if defined(__has_feature)
#if __has_feature(thread_sanitizer)
#define CONVOLVER_GUARD_TSAN 1
#endif
#endif
#if defined(__SANITIZE_THREAD__)
#define CONVOLVER_GUARD_TSAN 1
#endif
#if defined(__linux__) && !defined(CONVOLVER_GUARD_TSAN)
#define CONVOLVER_GUARD_LOCKS 1
#endif
#if defined(CONVOLVER_GUARD_LOCKS)
#include <dlfcn.h>
#include <pthread.h>
#endif

namespace convolver::plugin_test::audio_guard {
namespace {
thread_local int open = 0, paused = 0;
thread_local Counts counted;
// Allocations left before the next fails; negative: no limit.
thread_local long budget = -1;
thread_local bool fail_once = false;
bool watching() noexcept {
    return open > 0 && paused == 0;
}
} // namespace
void note_allocation() noexcept;
void note_lock() noexcept;
bool allocation_allowed() noexcept;
Starve::Starve(std::size_t allowed, bool once) noexcept {
    budget = static_cast<long>(allowed);
    fail_once = once;
}
Starve::~Starve() {
    budget = -1;
}
bool allocation_allowed() noexcept {
    if (paused) // Host code the plug-in calls back.
        return true;
    if (budget == 0) {
        if (fail_once)
            budget = -1;
        return false;
    }
    if (budget > 0)
        --budget;
    return true;
}
Counts take() noexcept {
    const auto c = counted;
    counted = {};
    return c;
}
bool counts_locks() noexcept {
#if defined(CONVOLVER_GUARD_LOCKS)
    return true;
#else
    return false;
#endif
}
Scope::Scope() noexcept {
    ++open;
}
Scope::~Scope() {
    --open;
}
Pause::Pause() noexcept {
    ++paused;
}
Pause::~Pause() {
    --paused;
}
void note_allocation() noexcept {
    if (watching())
        ++counted.allocations;
}
void note_lock() noexcept {
    if (watching())
        ++counted.locks;
}
} // namespace convolver::plugin_test::audio_guard

namespace guard = convolver::plugin_test::audio_guard;

void* operator new(std::size_t n) {
    guard::note_allocation();
    if (!guard::allocation_allowed())
        throw std::bad_alloc();
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    guard::note_allocation();
    if (!guard::allocation_allowed())
        return nullptr;
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept {
    return ::operator new(n, std::nothrow);
}
void operator delete(void* p) noexcept {
    if (p)
        guard::note_allocation(); // Freeing is as forbidden as allocating.
    std::free(p);
}
void operator delete[](void* p) noexcept {
    ::operator delete(p);
}
void operator delete(void* p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    ::operator delete(p);
}
// Aligned forms (the Accelerate FFT's buffers, for example).
void* operator new(std::size_t n, std::align_val_t a) {
    guard::note_allocation();
    if (!guard::allocation_allowed())
        throw std::bad_alloc();
    const auto alignment = std::max(static_cast<std::size_t>(a), sizeof(void*));
    void* p = nullptr;
#if defined(_WIN32)
    p = _aligned_malloc(n ? n : 1, alignment);
#else
    if (posix_memalign(&p, alignment, n ? n : 1) != 0)
        p = nullptr;
#endif
    if (!p)
        throw std::bad_alloc();
    return p;
}
void* operator new[](std::size_t n, std::align_val_t a) {
    return ::operator new(n, a);
}
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(n, a);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept {
    return ::operator new(n, a, std::nothrow);
}
void operator delete(void* p, std::align_val_t) noexcept {
    if (p)
        guard::note_allocation();
#if defined(_WIN32)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
void operator delete[](void* p, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete(void* p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}
void operator delete[](void* p, std::size_t, std::align_val_t a) noexcept {
    ::operator delete(p, a);
}

#if defined(CONVOLVER_GUARD_LOCKS)
namespace {
using Lock = int (*)(pthread_mutex_t*);
// Constant-initialized, so the first lock needs no guarded static.
std::atomic<Lock> real_lock{nullptr};
Lock resolve() noexcept {
    auto real = real_lock.load(std::memory_order_acquire);
    if (!real) {
        real = reinterpret_cast<Lock>(dlsym(RTLD_NEXT, "pthread_mutex_lock"));
        real_lock.store(real, std::memory_order_release);
    }
    return real;
}
[[maybe_unused]] const Lock resolved_at_start = resolve();
} // namespace
// Every lock the plug-in takes goes through this definition: the executable's
// symbols come first in the dynamic linker's search order.
extern "C" int pthread_mutex_lock(pthread_mutex_t* mutex) {
    guard::note_lock();
    return resolve()(mutex);
}
#endif
