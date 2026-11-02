// SPDX-License-Identifier: GPL-2.0-or-later
// RequestWorker (portable/request_worker.hpp), the thread that applies the
// Audio Unit's Latency: a stop is never lost, whenever it comes (jrp's
// report of 1 November 2026: a destructor waited for the worker forever in
// au.lifecycle). Each case runs under a watchdog, so a case that stops
// making progress fails the test instead of hanging it, with what it was
// doing and every thread's stack (CI-22).
//
// `--check-watchdog` runs only a case that never finishes, with a 1 s
// limit: it exits 0 when the report and the stacks came out, 77 (skipped)
// when no debugger is installed or, off CI, when it cannot attach, and 1
// otherwise. `--supervise` is the
// Windows helper process that runs cdb (see print_stacks).
#include "request_worker.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <process.h>
#include <windows.h>

// After windows.h, which it needs.
#include <tlhelp32.h>
#else
#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/sysctl.h>
#endif

using convolver::RequestWorker;
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;

namespace {
int failures = 0;

void require(bool value, const std::string& message) {
    if (!value) {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++failures;
    }
}

// What a case is doing, for the watchdog's report. `progress` counts steps
// done (the many-workers case counts workers); `phase` is where the test's
// thread is in the current step; `worker_point` is the last place the
// current worker reported from its own thread.
enum Phase : int { not_set, starting, requesting, stopping };
enum WorkerPoint : int { not_yet, woke, took_request, ran_task, unprobed };
std::atomic<long> progress{0};
std::atomic<int> phase{not_set};
std::atomic<int> worker_point{not_yet};

const char* phase_words(int p) {
    switch (p) {
    case starting:
        return "starting the worker (creating its thread)";
    case requesting:
        return "making the requests";
    case stopping:
        return "stopping the worker (stop() and join)";
    default:
        return "not recorded";
    }
}

const char* worker_words(int p) {
    switch (p) {
    case woke:
        return "woke for a request, not yet taken it";
    case took_request:
        return "took the request, not yet checked for a stop";
    case ran_task:
        return "ran the task";
    case unprobed:
        return "no probe (this worker runs without one, as the Audio Unit's does)";
    default:
        return "nothing yet (its thread had not run, or had not woken from its first wait)";
    }
}

// Every thread's stack, from the platform's debugger: cdb on Windows, gdb on
// Linux, lldb on macOS. The debugger suspends this process's threads while
// attached, so a separate process ends it after 30 s by the clock: a
// fork()ed child on POSIX, this program run with `--supervise` on Windows.
// Its output goes to a file, which is copied to stderr and checked for a
// thread listing.
enum class Stacks { printed, no_debugger, failed };
std::filesystem::path self; // argv[0], for --supervise.

// How long the debugger may run: 30 s, or CONVOLVER_DEBUGGER_LIMIT_MS, which
// the Watchdog stacks check sets short to end the debugger while it attaches.
long debugger_limit_ms() {
#if defined(_WIN32)
    char text[32] = "";
    (void)GetEnvironmentVariableA("CONVOLVER_DEBUGGER_LIMIT_MS", text, sizeof text);
#else
    const char* text = std::getenv("CONVOLVER_DEBUGGER_LIMIT_MS");
    if (text == nullptr)
        text = "";
#endif
    const long value = std::strtol(text, nullptr, 10);
    return value > 0 ? value : 30000;
}

#if defined(_WIN32)
std::filesystem::path cdb_path() {
    char x86[MAX_PATH] = "C:\\Program Files (x86)";
    (void)GetEnvironmentVariableA("ProgramFiles(x86)", x86, MAX_PATH);
    return std::filesystem::path(x86) / "Windows Kits" / "10" / "Debuggers" / "x64" / "cdb.exe";
}

// Resumes each thread of process `pid` once, undoing a suspension by a
// debugger that was ended while attached.
void resume_threads(DWORD pid) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;
    THREADENTRY32 entry{};
    entry.dwSize = sizeof entry;
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != pid)
            continue;
        if (const HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID)) {
            (void)ResumeThread(thread);
            CloseHandle(thread);
        }
    }
    CloseHandle(snapshot);
}

// --supervise PID LOG: cdb attached noninvasively (-pv: the threads are
// suspended, listed and left as they were), ended at debugger_limit_ms().
// Symbols come only from the test's own folder (-y): the runners' default
// path asks Microsoft's symbol server over the network, and cdb then ran
// past 30 s with no listing. System frames still show by their exports.
int supervise(const char* pid, const char* log) {
    const auto cdb = cdb_path().string();
    const auto quoted_log = "\"" + std::string(log) + "\"";
    char exe[MAX_PATH] = "";
    (void)GetModuleFileNameA(nullptr, exe, MAX_PATH); // argv[0] is "supervisor" here.
    const auto symbols = "\"" + std::filesystem::path(exe).parent_path().string() + "\"";
    const auto child =
        _spawnl(_P_NOWAIT, cdb.c_str(), "cdb", "-pv", "-p", pid, "-y", symbols.c_str(), "-logo",
                quoted_log.c_str(), "-c", "\"~*k 30; qd\"", nullptr);
    if (child == -1)
        return 126;
    const auto process = reinterpret_cast<HANDLE>(child);
    DWORD code = 124;
    if (WaitForSingleObject(process, static_cast<DWORD>(debugger_limit_ms())) == WAIT_OBJECT_0)
        (void)GetExitCodeProcess(process, &code);
    else {
        // cdb suspended the test's threads; ended, it cannot resume them.
        TerminateProcess(process, 124);
        WaitForSingleObject(process, 5000);
        resume_threads(static_cast<DWORD>(std::strtoul(pid, nullptr, 10)));
    }
    CloseHandle(process);
    return static_cast<int>(code);
}
#endif

#if !defined(_WIN32)
// The debugger's supervisor calls this after fork(), so system calls only.
// lldb's attach stops the test (SIGSTOP), and a stop can outlast the
// debugger: on macos-26 the test was left stopped (T) in its wait for the
// supervisor, which had exited (CI-25). On macOS, lldb's debugserver holds
// the test and may finish detaching after lldb exits, so this resumes the
// test until nothing traces it, for up to 2 s. getppid() == test means the
// test is still there, not a process that reused its pid.
void resume_test(pid_t test) {
    for (int tries = 0; tries < 20 && getppid() == test; ++tries) {
#if defined(__APPLE__)
        // Looked up before the SIGCONT, so that the last one is sent after
        // the detach: a tracer may take one sent while it still traces.
        int query[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, test};
        kinfo_proc info{};
        size_t size = sizeof info;
        const bool traced = sysctl(query, 4, &info, &size, nullptr, 0) == 0 && size != 0 &&
                            (info.kp_proc.p_flag & P_TRACED) != 0;
        kill(test, SIGCONT);
        if (!traced)
            return;
        const timespec pause{0, 100'000'000};
        nanosleep(&pause, nullptr);
#else
        kill(test, SIGCONT);
        return; // gdb detaches before it exits.
#endif
    }
    if (getppid() == test)
        kill(test, SIGCONT);
}
#endif

Stacks print_stacks() {
    std::fflush(stdout);
    std::fflush(stderr);
    const auto log = std::filesystem::temp_directory_path() / ("convolver-request-worker-stacks-" +
                                                               std::to_string(
#if defined(_WIN32)
                                                                   _getpid()
#else
                                                                   getpid()
#endif
                                                                       ) +
                                                               ".txt");
    std::error_code ignored;
    std::filesystem::remove(log, ignored);
    int status = 0;
    const auto began = Clock::now();
#if defined(_WIN32)
    if (!std::filesystem::exists(cdb_path())) {
        std::fprintf(stderr, "No thread stacks: cdb.exe not found at %s\n",
                     cdb_path().string().c_str());
        return Stacks::no_debugger;
    }
    const auto pid = std::to_string(_getpid());
    const auto quoted_log = "\"" + log.string() + "\"";
    status = static_cast<int>(_spawnl(_P_WAIT, self.string().c_str(), "supervisor", "--supervise",
                                      pid.c_str(), quoted_log.c_str(), nullptr));
#else
#if defined(__linux__)
    (void)prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0); // Yama: let a child attach.
    const char* debugger = "gdb";
    const auto command = "exec gdb -p " + std::to_string(getpid()) +
                         " -batch -nx -ex 'thread apply all bt 30' >'" + log.string() +
                         "' 2>&1 </dev/null";
#else
    const char* debugger = "lldb";
    const auto command = "exec lldb -p " + std::to_string(getpid()) +
                         " --batch -o 'thread backtrace all -c 30' >'" + log.string() +
                         "' 2>&1 </dev/null";
#endif
    const auto which = std::string("command -v ") + debugger + " >/dev/null 2>&1";
    if (std::system(which.c_str()) != 0) {
        std::fprintf(stderr, "No thread stacks: %s is not installed\n", debugger);
        return Stacks::no_debugger;
    }
    // Everything the children need is made before fork(): after it, they
    // only make system calls (fork, setpgid, exec, waitpid, kill, write,
    // getppid, clock_gettime, nanosleep, proc_listchildpids, sysctl) and
    // _exit.
    const pid_t test = getpid();
    const auto limit = std::chrono::milliseconds(debugger_limit_ms());
    const pid_t supervisor = fork();
    if (supervisor == 0) {
        const pid_t child = fork();
        if (child == 0) {
            // A process group of its own, which the kill below ends whole.
            (void)setpgid(0, 0);
            execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        if (child < 0)
            _exit(126);
        (void)setpgid(child, child); // Either side may run first.
        const auto deadline = Clock::now() + limit;
        int child_status = 0;
        while (Clock::now() < deadline) {
            if (waitpid(child, &child_status, WNOHANG) == child) {
                resume_test(test);
                _exit(WIFEXITED(child_status) ? WEXITSTATUS(child_status) : 125);
            }
            const timespec pause{0, 100'000'000};
            nanosleep(&pause, nullptr);
        }
        // Killing debugserver while it is attached makes macOS end the test
        // with SIGKILL, so say why first.
        const char note[] = "The debugger ran past its time limit and is ended; on macOS "
                            "the test may then end with SIGKILL.\n";
        (void)write(STDERR_FILENO, note, sizeof note - 1);
#if defined(__APPLE__)
        // lldb starts debugserver in a process group of its own, and
        // debugserver holds the test suspended until it exits: end lldb's
        // children first (the sh above exec'd lldb, so `child` is lldb).
        pid_t children[64]{};
        const int count = proc_listchildpids(child, children, static_cast<int>(sizeof children));
        for (int i = 0; i < count && i < 64; ++i)
            if (children[i] > 1 && children[i] != test)
                kill(children[i], SIGKILL);
#endif
        kill(-child, SIGKILL);
        while (waitpid(child, &child_status, 0) < 0 && errno == EINTR) {
        }
        const timespec settle{1, 0}; // For the kernel to detach what was killed.
        nanosleep(&settle, nullptr);
        resume_test(test); // In case the ended debugger left it stopped.
        _exit(124);
    }
    if (supervisor < 0) {
        std::fprintf(stderr, "No thread stacks: could not start the debugger\n");
        return Stacks::failed;
    }
    // A debugger's attach or detach may interrupt this wait (EINTR).
    while (waitpid(supervisor, &status, 0) < 0 && errno == EINTR) {
    }
    status = WIFEXITED(status) ? WEXITSTATUS(status) : 125;
#endif
    std::ifstream in(log, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::filesystem::remove(log, ignored);
    const auto seconds = std::chrono::duration<double>(Clock::now() - began).count();
    std::fprintf(stderr, "Thread stacks (debugger exit status %d%s, after %.1f s):\n%s\n", status,
                 status == 124 ? ", ended at its time limit" : "", seconds, text.c_str());
    // gdb, lldb and cdb's `k` each head a thread's frames in their own way.
    for (const char* heading : {"Thread 1 (", "thread #1", "Child-SP"})
        if (text.find(heading) != std::string::npos)
            return Stacks::printed;
    std::fprintf(stderr, "No thread stacks: the debugger printed no thread listing\n");
    return Stacks::failed;
}

std::chrono::seconds watchdog_limit{10};
bool checking_watchdog = false; // --check-watchdog: the exit code says whether stacks came out.

// Runs `body` on its own thread. If `progress` has not moved for
// watchdog_limit, this reports where the case was, with every thread's
// stack, and ends the process: a hung join cannot be undone. It says
// "timeout", not why: a stall on a busy runner and a lost wake look alike
// until the stacks show which (CI-22).
void within_time(const char* what, const std::function<void()>& body) {
    phase.store(not_set);
    worker_point.store(not_yet);
    std::promise<void> done;
    auto finished = done.get_future();
    std::thread runner([&] {
        body();
        done.set_value();
    });
    const auto began = Clock::now();
    auto seen = progress.load();
    auto last_change = began;
    while (finished.wait_for(100ms) != std::future_status::ready) {
        const auto now = Clock::now();
        if (const auto current = progress.load(); current != seen) {
            seen = current;
            last_change = now;
        } else if (now - last_change >= watchdog_limit) {
            const auto seconds = [](Clock::duration d) {
                return std::chrono::duration<double>(d).count();
            };
            std::fprintf(stderr,
                         "FAIL: %s: timeout, no progress for %.1f s (%.1f s in the case). "
                         "Steps done: %ld. The test's thread was %s; the current worker had %s.\n",
                         what, seconds(now - last_change), seconds(now - began), seen,
                         phase_words(phase.load()), worker_words(worker_point.load()));
            const auto stacks = print_stacks();
            std::fflush(stderr);
            if (checking_watchdog) {
                // Off CI (no CI variable), a debugger that cannot attach (no
                // ptrace in a container, no developer-tools access on macOS)
                // skips the check rather than failing it.
#if defined(_WIN32)
                const bool on_ci = GetEnvironmentVariableA("CI", nullptr, 0) > 0;
#else
                const bool on_ci = std::getenv("CI") != nullptr;
#endif
                std::_Exit(stacks == Stacks::printed           ? 0
                           : stacks == Stacks::failed && on_ci ? 1
                                                               : 77);
            }
            std::_Exit(1);
        }
    }
    runner.join();
}

// Waits up to 5 s by the clock for `condition`, less than the watchdog's
// limit, so a condition that never holds is reported as itself.
bool eventually(const std::function<bool()>& condition) {
    const auto deadline = Clock::now() + 5s;
    while (Clock::now() < deadline) {
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

int main(int argc, char** argv) {
    self = argv[0];
#if defined(_WIN32)
    if (argc == 4 && std::string_view(argv[1]) == "--supervise")
        return supervise(argv[2], argv[3]);
#endif
    if (argc > 1 && std::string_view(argv[1]) == "--check-watchdog") {
        watchdog_limit = 1s;
        checking_watchdog = true;
        within_time("the watchdog check (a case that never finishes)", [] {
            phase.store(stopping);
            std::promise<void> never;
            never.get_future().wait();
        });
        return 1; // Not reached: the watchdog ends the process.
    }
    idle_stop();
    requests_run_the_task();
    stop_while_running();
    stop_at(RequestWorker::Point::taking, "a stop as the worker wakes");
    stop_at(RequestWorker::Point::taken, "a stop after the worker takes the request");
    // Many short-lived workers, as au.lifecycle makes units: each one is
    // requested and stopped at once, so stops land at every point.
    // The watchdog's report says which step a stall came in. Half the
    // workers report their points through a probe; the other half run
    // without one, as the Audio Unit's worker does, so the probe's extra
    // call does not change every interleaving.
    within_time("many workers", [] {
        for (int i = 0; i < 2000; ++i) {
            const bool probed = i % 4 < 2;
            worker_point.store(probed ? not_yet : unprobed);
            RequestWorker worker;
            phase.store(starting);
            if (probed)
                worker.start([] { worker_point.store(ran_task); },
                             [](RequestWorker::Point point) {
                                 worker_point.store(
                                     point == RequestWorker::Point::taking ? woke : took_request);
                             });
            else
                worker.start([] {});
            phase.store(requesting);
            worker.request();
            if (i % 2)
                worker.request();
            phase.store(stopping);
            worker.stop();
            ++progress;
        }
    });
    if (failures)
        return 1;
    std::puts("request worker tests passed");
    return 0;
}
