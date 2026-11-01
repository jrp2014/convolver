// SPDX-License-Identifier: GPL-2.0-or-later
// RequestWorker (portable/request_worker.hpp), the thread that applies the
// Audio Unit's Latency: a stop is never lost, whenever it comes (jrp's
// report of 1 November 2026: a destructor waited for the worker forever in
// au.lifecycle). Each case runs under a watchdog, so a lost stop fails the
// test instead of hanging it.
#include "request_worker.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <string>
#include <thread>

using convolver::RequestWorker;
using namespace std::chrono_literals;

namespace {
int failures = 0;

void require(bool value, const std::string& message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++failures;
    }
}

// Runs `body` on its own thread and fails at once if it has not finished
// within 10 s: a hung join cannot be undone, so the process ends there.
void within_time(const char* what, const std::function<void()>& body) {
    std::promise<void> done;
    auto finished = done.get_future();
    std::thread runner([&] {
        body();
        done.set_value();
    });
    if (finished.wait_for(10s) != std::future_status::ready) {
        std::fprintf(stderr, "FAIL: %s did not finish within 10 s (a lost stop)\n", what);
        std::fflush(stderr);
        std::_Exit(1);
    }
    runner.join();
}

// Waits up to 10 s for `condition`.
bool eventually(const std::function<bool()>& condition) {
    for (int i = 0; i < 10000; ++i) {
        if (condition())
            return true;
        std::this_thread::sleep_for(1ms);
    }
    return condition();
}

// A gate a task waits at until the test opens it.
struct Gate {
    std::atomic<bool> entered{false}, open{false};
    void pass() {
        entered.store(true);
        while (!open.load())
            std::this_thread::sleep_for(100us);
    }
};

void idle_stop() {
    within_time("stopping an idle worker", [] {
        std::atomic<int> runs{0};
        RequestWorker worker;
        worker.start([&] { ++runs; });
        worker.stop();
        require(runs == 0, "an idle worker ran its task");
        worker.stop(); // A second stop, as the destructor makes, does nothing.
    });
    within_time("destroying a worker that never started", [] { RequestWorker worker; });
}

void requests_run_the_task() {
    within_time("requests", [] {
        std::atomic<int> runs{0};
        Gate gate;
        RequestWorker worker;
        worker.start([&] {
            if (++runs == 1)
                gate.pass();
        });
        worker.request();
        require(eventually([&] { return gate.entered.load(); }), "a request did not run the task");
        // Requests while the task runs make it run once more, not once each.
        worker.request();
        worker.request();
        gate.open.store(true);
        require(eventually([&] { return runs == 2; }), "requests during the task were lost");
        std::this_thread::sleep_for(50ms);
        require(runs == 2, "requests during the task ran it more than once more");
        worker.stop();
    });
}

void stop_while_running() {
    within_time("stopping while the task runs", [] {
        std::atomic<int> runs{0};
        Gate gate;
        RequestWorker worker;
        worker.start([&] {
            ++runs;
            gate.pass();
        });
        worker.request();
        require(eventually([&] { return gate.entered.load(); }), "the task did not start");
        std::atomic<bool> stopped{false};
        std::thread stopper([&] {
            worker.stop();
            stopped.store(true);
        });
        std::this_thread::sleep_for(50ms);
        require(!stopped, "stop() did not wait for the running task");
        gate.open.store(true);
        stopper.join();
        require(runs == 1, "the task ran again after the stop");
    });
}

// The race jrp found: the stop comes just as the worker wakes for a request
// (before it takes it), or just after it has taken it (before it checks for
// a stop). The worker is held at that point until stop() has asked it to
// end, as a destructor does. Checking for the stop first, then taking the
// request, erased the stop's wake there: the task ran, the worker slept,
// and stop() waited for it forever.
void stop_at(RequestWorker::Point at, const char* what) {
    within_time(what, [at, what] {
        std::atomic<int> runs{0};
        std::atomic<bool> held{false};
        RequestWorker worker;
        worker.start([&] { ++runs; },
                     [&](RequestWorker::Point point) {
                         if (point != at || held.exchange(true))
                             return;
                         while (!worker.stop_requested())
                             std::this_thread::sleep_for(100us);
                     });
        worker.request();
        require(eventually([&] { return held.load(); }), std::string(what) + ": not reached");
        worker.stop();
        require(runs == 0, std::string(what) + ": the task ran after the stop");
    });
}
} // namespace

int main() {
    idle_stop();
    requests_run_the_task();
    stop_while_running();
    stop_at(RequestWorker::Point::taking, "a stop as the worker wakes");
    stop_at(RequestWorker::Point::taken, "a stop after the worker takes the request");
    // Many short-lived workers, as au.lifecycle makes units: each one is
    // requested and stopped at once, so stops land at every point.
    within_time("many workers", [] {
        for (int i = 0; i < 2000; ++i) {
            RequestWorker worker;
            worker.start([] {});
            worker.request();
            if (i % 2)
                worker.request();
        }
    });
    if (failures)
        return 1;
    std::puts("request worker tests passed");
    return 0;
}
