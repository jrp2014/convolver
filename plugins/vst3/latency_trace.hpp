// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-16 (investigation): a record of the host's calls after a Latency
// change, for the Convolver log. REAPER kept the old latency until the
// project was reopened, and the record shows what it does in between: when
// it reads getLatencySamples() and which of setActive, setProcessing and
// setupProcessing follow the controller's restartComponent(kLatencyChanged).
//
// The record opens when the processor receives a new Latency and stays open
// for `window`. Calls are recorded from any thread, the audio thread
// included, without locking or allocating; take() turns them into log lines
// later, off the audio thread. A call repeated with the same values is
// counted rather than recorded again, so a host that polls does not fill it.
#ifndef CONVOLVER_VST3_LATENCY_TRACE_HPP
#define CONVOLVER_VST3_LATENCY_TRACE_HPP

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifdef __APPLE__
#include <pthread.h>
#endif

namespace convolver::vst3 {
class LatencyTrace {
  public:
    enum class Call : std::uint8_t {
        latency_chosen,   // a: the new Latency; b: the running processor's.
        restart_asking,   // The controller is about to call restartComponent.
        restart_answered, // result: what restartComponent returned.
        get_latency,      // a: what getLatencySamples() returned.
        get_tail,         // a: what getTailSamples() returned.
        setup_processing, // a: largest block; b: rate in Hz; result.
        set_active,       // a: the state; b: the running latency after; result.
        set_processing,   // a: the state.
        process_started,  // a: the first block's frames after (re)starting.
        repeated,         // a: how often the call before it was repeated.
    };
    static constexpr std::chrono::seconds window{120};

    LatencyTrace() noexcept : main_(thread_key()) {}

    // Not the audio thread: opens (or reopens) the record for `window`.
    void open() noexcept {
        const auto now = clock_now();
        origin_.store(now, std::memory_order_relaxed);
        until_.store(now + std::chrono::nanoseconds(window).count(), std::memory_order_release);
    }
    bool is_open() const noexcept {
        return clock_now() < until_.load(std::memory_order_acquire);
    }
    // process(): remembers which thread is the audio thread, and records the
    // first block after an activation or setProcessing(true).
    void audio_block(std::int64_t frames) noexcept {
        audio_.store(thread_key(), std::memory_order_relaxed);
        if (!started_.exchange(true, std::memory_order_relaxed))
            record(Call::process_started, frames);
    }
    // The next block counts as the first again.
    void restarted() noexcept {
        started_.store(false, std::memory_order_relaxed);
    }
    // Any thread, real-time safe: records the call while the record is open.
    void record(Call call, std::int64_t a = 0, std::int64_t b = 0,
                std::int64_t result = 0) noexcept {
        if (!is_open())
            return;
        const auto thread = thread_key();
        // The same call with the same values on the same thread as the last
        // one: counted only.
        const auto key = (static_cast<std::uint64_t>(call) << 56) ^
                         (static_cast<std::uint64_t>(a) << 8) ^ static_cast<std::uint64_t>(b) ^
                         (static_cast<std::uint64_t>(thread) << 20);
        if (last_key_.exchange(key, std::memory_order_relaxed) == key) {
            repeats_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (const auto n = repeats_.exchange(0, std::memory_order_relaxed))
            push(Call::repeated, static_cast<std::int64_t>(n), 0, 0, thread);
        push(call, a, b, result, thread);
    }
    // Not the audio thread, one caller at a time: the lines recorded since
    // the last call.
    std::vector<std::string> take() {
        std::vector<std::string> lines;
        // A call repeated since the last record is counted now, as nothing
        // else may follow it.
        if (const auto n = repeats_.exchange(0, std::memory_order_relaxed))
            push(Call::repeated, static_cast<std::int64_t>(n), 0, 0, 0);
        auto tail = tail_.load(std::memory_order_relaxed);
        for (;;) {
            auto& slot = slots_[tail % slots_.size()];
            if (slot.sequence.load(std::memory_order_acquire) != tail + 1)
                break;
            lines.push_back(line(slot));
            tail_.store(++tail, std::memory_order_release);
        }
        if (const auto lost = lost_.exchange(0, std::memory_order_relaxed))
            lines.push_back("Latency trace: " + std::to_string(lost) +
                            " host calls were not recorded (the record was full).");
        return lines;
    }

  private:
    struct Slot {
        std::atomic<std::uint64_t> sequence{0}; // Its position + 1 once written.
        std::int64_t time = 0, a = 0, b = 0, result = 0;
        std::size_t thread = 0;
        bool main = false;
        std::uint8_t call = 0;
    };
    static std::int64_t clock_now() noexcept {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    }
    static std::size_t thread_key() noexcept {
        return std::hash<std::thread::id>{}(std::this_thread::get_id());
    }
    static bool on_main_thread(std::size_t thread, std::size_t main) noexcept {
#ifdef __APPLE__
        static_cast<void>(thread);
        static_cast<void>(main);
        return pthread_main_np() != 0;
#else
        return thread == main;
#endif
    }
    void push(Call call, std::int64_t a, std::int64_t b, std::int64_t result,
              std::size_t thread) noexcept {
        auto head = head_.load(std::memory_order_relaxed);
        do {
            if (head - tail_.load(std::memory_order_acquire) >= slots_.size()) {
                lost_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
        } while (!head_.compare_exchange_weak(head, head + 1, std::memory_order_relaxed));
        auto& slot = slots_[head % slots_.size()];
        slot.time = clock_now() - origin_.load(std::memory_order_relaxed);
        slot.a = a;
        slot.b = b;
        slot.result = result;
        slot.thread = thread;
        slot.main = on_main_thread(thread, main_);
        slot.call = static_cast<std::uint8_t>(call);
        slot.sequence.store(head + 1, std::memory_order_release);
    }
    static std::string result_name(std::int64_t r) {
        switch (r) {
        case 0:
            return "kResultOk";
        case 1:
            return "kResultFalse";
        default:
            return "result " + std::to_string(r) + " (kNotImplemented or another failure)";
        }
    }
    std::string line(const Slot& s) const {
        const auto n = [](std::int64_t v) { return std::to_string(v); };
        if (s.call == static_cast<std::uint8_t>(Call::repeated))
            return "Latency trace: the call above was repeated " + n(s.a) + " more times.";
        std::array<char, 32> ms{};
        std::snprintf(ms.data(), ms.size(), "%.1f", static_cast<double>(s.time) / 1e6);
        std::string where = s.main                                               ? "main thread"
                            : s.thread == audio_.load(std::memory_order_relaxed) ? "audio thread"
                                                                                 : "";
        if (where.empty()) {
            std::array<char, 24> id{};
            std::snprintf(id.data(), id.size(), "thread %04zx", s.thread & 0xffff);
            where = id.data();
        }
        std::string what;
        switch (s.call) {
        case static_cast<std::uint8_t>(Call::latency_chosen):
            what = "Latency " + n(s.a) + " frames chosen (the processor runs at " + n(s.b) + ")";
            break;
        case static_cast<std::uint8_t>(Call::restart_asking):
            what = "the controller calls restartComponent(kLatencyChanged)";
            break;
        case static_cast<std::uint8_t>(Call::restart_answered):
            what = "restartComponent(kLatencyChanged) returned " + result_name(s.result);
            break;
        case static_cast<std::uint8_t>(Call::get_latency):
            what = "getLatencySamples() returned " + n(s.a);
            break;
        case static_cast<std::uint8_t>(Call::get_tail):
            what = "getTailSamples() returned " + n(s.a);
            break;
        case static_cast<std::uint8_t>(Call::setup_processing):
            what = "setupProcessing(" + n(s.b) + " Hz, blocks up to " + n(s.a) + ") returned " +
                   result_name(s.result);
            break;
        case static_cast<std::uint8_t>(Call::set_active):
            what = std::string("setActive(") + (s.a ? "true" : "false") + ") returned " +
                   result_name(s.result) + "; the processor runs at " + n(s.b);
            break;
        case static_cast<std::uint8_t>(Call::set_processing):
            what = std::string("setProcessing(") + (s.a ? "true" : "false") + ")";
            break;
        case static_cast<std::uint8_t>(Call::process_started):
            what = "process() started again, " + n(s.a) + " frames";
            break;
        default:
            what = "call " + n(s.call);
        }
        return "Latency trace +" + std::string(ms.data()) + " ms, " + where + ": " + what + ".";
    }
    std::array<Slot, 256> slots_{};
    std::atomic<std::uint64_t> head_{0}, tail_{0};
    std::atomic<std::uint64_t> lost_{0}, repeats_{0}, last_key_{~std::uint64_t{0}};
    std::atomic<std::int64_t> origin_{0}, until_{0};
    std::atomic<std::size_t> audio_{0};
    std::atomic<bool> started_{true};
    std::size_t main_;
};
} // namespace convolver::vst3

#endif
