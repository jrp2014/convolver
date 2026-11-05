// SPDX-License-Identifier: GPL-2.0-or-later
// The load-timing harness itself (tests/plugins/load_timing.hpp), with a mock
// adapter whose loads and callbacks take as long as each case says: the
// input it plays (PERF-11), and that it never reports a pass it did not
// measure (PERF-10). The delays are far beyond the limits, so the verdicts
// hold in sanitizer builds and on busy machines; where a busy machine can
// turn a failure into "not qualified", the case accepts either, never a pass.
#include "../plugins/load_timing.hpp"
#include "convolver/host.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace timing = convolver::plugin_test::load_timing;
namespace lc = convolver::plugin_test::lifecycle;
using namespace std::chrono_literals;
using Ms = std::chrono::milliseconds;

namespace {
int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::cout << "FAILED: " << what << '\n';
        ++failures;
    }
}

// PERF-11: the input is quiet noise of both signs, different on each channel
// (the first version wrapped its negative half to about 1.8e15).
void input_is_quiet_noise() {
    const auto a = timing::quiet_noise(0, 512), b = timing::quiet_noise(1, 512);
    for (const auto* v : {&a, &b}) {
        const auto [lo, hi] = std::minmax_element(v->begin(), v->end());
        std::cout << "input: " << *lo << " to " << *hi << '\n';
        check(*lo >= -0.1f && *lo < -0.05f, "the input reaches below -0.05 and not below -0.1");
        check(*hi <= 0.1f && *hi > 0.05f, "the input reaches above 0.05 and not above 0.1");
        check(std::all_of(v->begin(), v->end(), [](float x) { return std::isfinite(x); }),
              "the input is finite");
    }
    check(a != b, "the channels differ");
}

// An adapter that keeps the state it is given, taking `load` to accept it,
// and `callback` for every callback (or only while a load runs).
class Mock final : public lc::Driver {
  public:
    Ms load{0}, callback{0};
    bool slow_only_while_loading = false;
    int slow_loads = -1; // How many loads take `load`: all, if negative.
    bool refuse = false;

    lc::Format format() const override {
        lc::Format f;
        f.name = "mock";
        return f;
    }
    void create(const lc::Host&, bool) override {
        saved_ = {};
    }
    void configure(const lc::Host&) override {}
    void destroy() override {}
    bool restore(std::span<const std::byte> state) override {
        loading_ = true;
        if (slow_loads != 0)
            std::this_thread::sleep_for(load);
        if (slow_loads > 0)
            --slow_loads;
        loading_ = false;
        if (refuse)
            return false;
        saved_ = convolver::decode_host_state(state);
        return true;
    }
    bool start() override {
        return true;
    }
    void stop() override {}
    bool process(std::span<const float* const> in, std::span<float* const> out,
                 std::size_t frames) override {
        // Spins rather than sleeps: a sleep can take many times longer
        // (175 ms for 25 ms on a macOS runner), which only slows the test.
        if (callback.count() && (!slow_only_while_loading || loading_)) {
            const auto until = std::chrono::steady_clock::now() + callback;
            while (std::chrono::steady_clock::now() < until) {
            }
        }
        for (std::size_t c = 0; c < out.size(); ++c)
            std::copy_n(in[c % in.size()], frames, out[c]);
        return true;
    }
    void set_bypass(bool) override {}
    void reset() override {}
    bool reopen(const lc::Host&, bool, bool) override {
        return false;
    }
    convolver::HostState saved() override {
        return saved_;
    }
    std::optional<convolver::HostFailure> failure() override {
        return std::nullopt;
    }
    std::size_t max_block() const override {
        return 4096;
    }

  private:
    convolver::HostState saved_;
    std::atomic<bool> loading_{false};
};

// A folder of its own, so that builds testing at once do not share one.
std::filesystem::path scratch() {
    return std::filesystem::temp_directory_path() /
           ("convolver-load-timing-harness-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

struct Case {
    const char* name;
    Ms load, callback;
    bool slow_only_while_loading = false, refuse = false, enforce = true;
    int slow_loads = -1;
};

timing::Result run(const Case& c) {
    std::cout << "\ncase: " << c.name << '\n';
    Mock mock;
    mock.load = c.load;
    mock.callback = c.callback;
    mock.slow_only_while_loading = c.slow_only_while_loading;
    mock.refuse = c.refuse;
    mock.slow_loads = c.slow_loads;
    timing::Options o;
    o.work_directory = scratch();
    o.enforce = c.enforce;
    // A short filter: the mock does not prepare it, and a 100 ms limit.
    const std::vector<timing::Workload> list{{"tiny", {48000, 2, 2}, 0.01, 512, 100ms}};
    const std::vector<timing::Entry> entries{
        {"state", [&](const timing::Filter& f, timing::Audio&) {
             return timing::Outcome{mock.restore(f.state)};
         }}};
    auto r = timing::run(mock, entries, list, o);
    std::filesystem::remove_all(o.work_directory);
    return r;
}

std::string text(const timing::Result& r) {
    return std::to_string(r.passed) + " passed, " + std::to_string(r.failed) + " failed, " +
           std::to_string(r.not_qualified) + " not qualified, " + std::to_string(r.reported_only) +
           " reported only, " + std::to_string(r.errors) + " errors, exit " +
           std::to_string(timing::exit_code(r));
}

// PERF-10: the harness judges what it measured, and says so.
void verdicts() {
    {
        // A quick adapter never fails on its hold. Its callbacks are the
        // machine's: one stalled run after another, other tests running
        // alongside, can fail them, so they are not checked here.
        const auto r = run({"quick loads and callbacks", 0ms, 0ms});
        check(r.errors == 0 && std::none_of(r.runs.begin(), r.runs.end(),
                                            [](const timing::Run& run) {
                                                return run.why.find("held the host's thread") !=
                                                       std::string::npos;
                                            }),
              "quick adapter: " + text(r));
    }
    {
        // A loader over the limit fails on every run, each one kept.
        const auto r = run({"slow loader", 400ms, 0ms});
        check(r.failed == 1 && timing::exit_code(r) == 1, "slow loader: " + text(r));
        check(r.runs.size() == 3 && std::all_of(r.runs.begin(), r.runs.end(),
                                                [](const timing::Run& run) {
                                                    return run.verdict == timing::Verdict::failed;
                                                }),
              "slow loader: three failed runs kept");
        check(!r.runs.empty() && r.runs[0].why.find("held the host's thread") != std::string::npos,
              "slow loader: the run says the host's thread was held");
    }
    {
        // Slow only on the first run (its two loads): run 1 fails and is kept;
        // the next is not held (its callbacks are the machine's).
        const auto r = run({"slow on the first run only", 400ms, 0ms, false, false, true, 2});
        check(r.runs.size() >= 2 && r.runs[0].hold == timing::Verdict::failed &&
                  r.runs[1].hold == timing::Verdict::passed &&
                  (r.passed == 0 || r.passed_after_failure == 1),
              "slow on the first run only: " + text(r));
    }
    {
        // As on a busy macOS runner: the hold stalled on the first run only,
        // and every callback is late, the baseline's too. The hold limit is
        // met by the later runs, the callbacks cannot be judged: not
        // qualified, not failed.
        const auto r = run({"slow once, every callback late", 400ms, 12ms, false, false, true, 1});
        check(r.not_qualified == 1 && r.failed == 0 && r.runs.size() == 3 &&
                  r.runs[0].hold == timing::Verdict::failed &&
                  r.runs[1].hold == timing::Verdict::passed &&
                  timing::exit_code(r) == timing::not_qualified_exit,
              "slow once with late callbacks: " + text(r));
    }
    {
        // The review's reproduction: a slow loader with every callback late,
        // the baseline included, still fails on the hold.
        const auto r = run({"slow loader, every callback late", 400ms, 12ms});
        check(r.failed == 1 && timing::exit_code(r) == 1,
              "slow loader with late callbacks: " + text(r));
    }
    {
        // Late callbacks with a late baseline show nothing: not qualified,
        // and the test is skipped rather than passed.
        const auto r = run({"every callback late, quick loader", 0ms, 12ms});
        check(r.not_qualified == 1 && r.passed == 0 && r.failed == 0 &&
                  timing::exit_code(r) == timing::not_qualified_exit,
              "late callbacks throughout: " + text(r));
    }
    {
        // Callbacks late only while loading fail (or, on a busy machine, do
        // not qualify); they never pass.
        const auto r = run({"callbacks late while loading", 50ms, 12ms, true});
        check(r.passed == 0 && timing::exit_code(r) != 0,
              "callbacks late while loading: " + text(r));
    }
    {
        // A build that does not enforce the limits never reports a pass.
        const auto r = run({"slow loader, limits not enforced", 400ms, 0ms, false, false, false});
        check(r.not_qualified == 1 && r.runs.size() == 1 &&
                  timing::exit_code(r) == timing::not_qualified_exit,
              "limits not enforced: " + text(r));
    }
    {
        // An entry no limit applies to (the host stops audio for the load,
        // which is not on its thread) is reported only, however slow.
        Mock mock;
        mock.load = 400ms;
        timing::Options o;
        o.work_directory = scratch();
        o.enforce = true;
        o.reset_holds_audio = true;
        std::cout << "\ncase: reported only\n";
        const auto r = timing::run(mock,
                                   {{"held back",
                                     [&](const timing::Filter& f, timing::Audio&) {
                                         return timing::Outcome{mock.restore(f.state)};
                                     },
                                     true, false}},
                                   {{"tiny", {48000, 2, 2}, 0.01, 512, 100ms}}, o);
        std::filesystem::remove_all(o.work_directory);
        check(r.reported_only == 1 && r.passed == 0 && r.runs.size() == 1 &&
                  timing::exit_code(r) == 0,
              "reported only: " + text(r));
    }
    {
        // A refused load is an error in any build.
        const auto r = run({"refused load", 0ms, 0ms, false, true});
        check(r.errors > 0 && r.failed == 1 && r.runs.size() == 1 && timing::exit_code(r) == 1,
              "refused load: " + text(r));
    }
}
} // namespace

int main() {
    try {
        input_is_quiet_noise();
        verdicts();
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << '\n';
        return 1;
    }
    std::cout << "load-timing harness: " << failures << " failed\n";
    return failures ? 1 : 0;
}
