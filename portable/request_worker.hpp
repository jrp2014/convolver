// SPDX-License-Identifier: GPL-2.0-or-later
// A thread that runs a task after each request, off the thread that asks:
// the Audio Unit applies a new Latency this way (PLUG-10), because the host
// may choose it on an audio thread. request() is an atomic store and
// notify, which neither allocates nor waits. Requests made while the task
// runs make it run once more, not once each. stop() ends the thread and
// waits for a task already running to finish.
#pragma once
#include <atomic>
#include <functional>
#include <thread>
#include <utility>

namespace convolver {

class RequestWorker {
  public:
    // Where the worker is, for tests that force an interleaving: about to
    // take a request (just woken), and just taken it (before it checks for
    // a stop). A test's probe may wait there for stop_requested().
    enum class Point { taking, taken };
    using Probe = std::function<void(Point)>;

    RequestWorker() = default;
    RequestWorker(const RequestWorker&) = delete;
    RequestWorker& operator=(const RequestWorker&) = delete;
    ~RequestWorker() {
        stop();
    }

    // Starts the thread, once. Call it when everything `task` uses exists.
    void start(std::function<void()> task, Probe probe = {}) {
        task_ = std::move(task);
        probe_ = std::move(probe);
        thread_ = std::thread([this] { run(); });
    }

    void request() noexcept {
        requested_.store(true);
        // notify_all: libc++ can park waiters on a slot shared by many
        // atomics; each one woken rechecks its own flag.
        requested_.notify_all();
    }

    // Asks the thread to end, without waiting for it (a probe may call it).
    void request_stop() noexcept {
        stopping_.store(true);
        request();
    }

    bool stop_requested() const noexcept {
        return stopping_.load();
    }

    // Ends the thread and waits for it. Never call it from the task.
    void stop() noexcept {
        request_stop();
        if (thread_.joinable())
            thread_.join();
    }

  private:
    void run() noexcept {
        for (;;) {
            requested_.wait(false);
            if (probe_)
                probe_(Point::taking);
            // Take the request before checking for a stop (jrp, 1 November
            // 2026). Checking first lost a stop that came between the check
            // and the take: taking it erased the stop's wake, and the thread
            // slept while stop() waited for it. Now a stop before the check
            // is seen, and one after it leaves `requested_` set, so the next
            // wait returns at once.
            requested_.store(false);
            if (probe_)
                probe_(Point::taken);
            if (stopping_.load())
                return;
            task_();
        }
    }

    std::function<void()> task_;
    Probe probe_;
    std::atomic<bool> requested_{false};
    std::atomic<bool> stopping_{false};
    std::thread thread_; // Last: started after the members it uses.
};

} // namespace convolver
