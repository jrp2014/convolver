// SPDX-License-Identifier: GPL-2.0-or-later
// For a test that replaces `operator new(std::size_t)` and
// `operator delete(void*)` to watch or fail allocations: the array and sized
// forms, each forwarding to those two. Include it in one file of the test,
// after defining them. With only some forms replaced, the runtime's own
// delete can free what the test's new allocated, which AddressSanitizer
// reports as alloc-dealloc-mismatch (CI-30: GCC's library calls the sized
// forms). The nothrow forms call these in GCC's and LLVM's libraries; the
// aligned forms (std::align_val_t) stay the runtime's, which pairs its own
// aligned new and delete.
#pragma once
#include <cstddef>
#include <new>

void* operator new[](std::size_t n) {
    return ::operator new(n);
}
// GCC warns that these deletes mismatch their pointers' operator new once
// inlined; each forwards to the test's own pair, so they match.
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif
void operator delete[](void* p) noexcept {
    ::operator delete(p);
}
void operator delete(void* p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    ::operator delete(p);
}
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif
