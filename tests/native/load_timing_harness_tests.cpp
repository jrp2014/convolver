// SPDX-License-Identifier: GPL-2.0-or-later
// The load-timing harness itself (tests/plugins/load_timing.hpp): the input
// it plays (PERF-11); its verdict rules and timing summaries on made-up
// records, without timing anything (PERF-12, from jrp's review of db54160);
// and, briefly with real threads and a mock adapter, that the renderer's
// records reach those verdicts. It never reports a pass it did not measure
// (PERF-10).
#include "../plugins/load_timing.hpp"
#include "convolver/host.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

// PERF-10: an entry's verdict from made-up runs, without timing anything.
// Each run is {hold, callbacks}, or an error.
using V = timing::Verdict;
timing::Run made(V hold, V callbacks, int attempt, bool error = false) {
    timing::Run r;
    r.attempt = attempt;
    r.hold = hold;
    r.callbacks = callbacks;
    r.error = error;
    r.verdict = error ? V::failed : timing::combine(hold, callbacks);
    return r;
}

void verdict_table() {
    const auto P = V::passed, F = V::failed, N = V::not_qualified, R = V::reported_only;
    check(timing::combine(P, P) == P && timing::combine(F, P) == F && timing::combine(P, F) == F &&
              timing::combine(F, N) == F && timing::combine(N, P) == N &&
              timing::combine(R, R) == R && timing::combine(R, P) == P &&
              timing::combine(P, R) == P && timing::combine(R, N) == N &&
              timing::combine(R, F) == F,
          "combine");
    struct Row {
        const char* name;
        std::vector<std::pair<V, V>> runs;
        V expected;
        int hold_met, callbacks_met;
        int error_on = 0; // The attempt with an error, if any.
    };
    const std::vector<Row> rows{
        {"one clean run", {{P, P}}, P, 1, 1},
        {"stalled hold, then clean", {{F, P}, {P, P}}, P, 2, 1},
        // jrp's review of cc2ebde: each limit was met in some run, but no
        // run met both, so no run qualified.
        {"stalled hold, then unjudged callbacks", {{F, P}, {P, N}}, N, 2, 1},
        {"unjudged callbacks, then stalled hold", {{P, N}, {F, P}}, N, 1, 2},
        {"stalled hold, unjudged, then clean", {{F, P}, {P, N}, {P, P}}, P, 2, 1},
        // A limit broken with evidence and never met fails.
        {"late callbacks, then unjudged", {{P, F}, {P, N}}, F, 1, 0},
        {"stalled hold, every baseline late", {{F, N}, {P, N}, {P, N}}, N, 2, 0},
        // Each run broke a limit with evidence: not a pass.
        {"stalled hold, then late callbacks", {{F, P}, {P, F}}, F, 2, 1},
        {"late callbacks, then stalled hold, then both", {{P, F}, {F, P}, {P, F}}, F, 1, 2},
        {"slow loader", {{F, P}, {F, P}, {F, P}}, F, 0, 1},
        {"unjudged throughout", {{N, N}, {N, N}, {N, N}}, N, 0, 0},
        {"no limit applies", {{R, R}}, R, 0, 0},
        {"hold not limited, callbacks met", {{R, P}}, P, 0, 1},
        {"hold not limited, callbacks unjudged", {{R, N}, {R, N}, {R, N}}, N, 0, 0},
        {"error after a pass", {{P, P}, {P, P}}, F, 1, 1, 2},
        {"limits not enforced", {{N, N}}, N, 0, 0},
    };
    for (const auto& row : rows) {
        std::vector<timing::Run> runs;
        for (const auto& [hold, callbacks] : row.runs) {
            const int attempt = static_cast<int>(runs.size()) + 1;
            runs.push_back(made(hold, callbacks, attempt, attempt == row.error_on));
        }
        const auto v = timing::verdict(runs.begin(), runs.end());
        check(v == row.expected, std::string("verdict, ") + row.name + ": " + timing::describe(v));
        check(timing::met_on(runs.begin(), runs.end(), &timing::Run::hold) == row.hold_met &&
                  timing::met_on(runs.begin(), runs.end(), &timing::Run::callbacks) ==
                      row.callbacks_met,
              std::string("met on, ") + row.name);
    }
    // Exit codes: a pass needs something judged.
    const auto exit = [](std::size_t passed, std::size_t failed, std::size_t not_qualified,
                         std::size_t reported_only, std::size_t errors) {
        timing::Result r;
        r.passed = passed;
        r.failed = failed;
        r.not_qualified = not_qualified;
        r.reported_only = reported_only;
        r.errors = errors;
        return timing::exit_code(r);
    };
    check(exit(3, 0, 0, 1, 0) == 0 && exit(3, 1, 1, 0, 0) == 1 && exit(3, 0, 0, 0, 1) == 1 &&
              exit(3, 0, 1, 0, 0) == timing::not_qualified_exit &&
              exit(0, 0, 0, 2, 0) == timing::not_qualified_exit &&
              exit(0, 0, 0, 0, 0) == timing::not_qualified_exit,
          "exit codes");
}

// PERF-12: runs made of records, judged without timing anything. The period
// is 10 ms and the hold limit 100 ms, as for a 10 s filter.
constexpr double period = 0.01;
constexpr double hold_limit = 0.1;

// `n` callbacks, each due a period after the last (or when a late one
// returned), waking `wake` late and spending `call` in the plug-in, with
// `cpu` of CPU time (-1: not measured).
std::vector<timing::CallbackRecord> steady(std::size_t n, double wake = 0, double call = 0.0005,
                                           double cpu = -1) {
    std::vector<timing::CallbackRecord> v;
    double due = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const double woke = due + wake, returned = woke + call;
        v.push_back({due, woke, woke, returned, cpu, true});
        due = std::max(due + period, returned);
    }
    return v;
}

// A run's records: baselines of 50 callbacks before and after, two loads
// each holding the caller `held` with `loading` callbacks, and a reset.
struct Spec {
    double held = 0.005;
    std::vector<timing::CallbackRecord> before = steady(50), loading = steady(30),
                                        after = steady(50);
    bool accepted = true, saved = true, audio_held = false;
    std::size_t dropped = 0;
};

timing::RunRecord record(const Spec& s) {
    timing::RunRecord r{"10 s", "state", 1, {}, s.saved};
    const auto phase = [](const char* name, bool baseline,
                          std::vector<timing::CallbackRecord> callbacks) {
        timing::Phase p;
        p.name = name;
        p.baseline = baseline;
        p.callbacks = std::move(callbacks);
        return p;
    };
    r.phases.push_back(phase("baseline", true, s.before));
    for (const char* name : {"first load", "replacement"}) {
        auto p = phase(name, false, s.loading);
        p.held = s.held;
        p.accepted = s.accepted;
        p.audio_held = s.audio_held;
        p.dropped = s.dropped;
        r.phases.push_back(std::move(p));
    }
    auto reset = phase("reset", false, steady(30));
    reset.audio_held = s.audio_held; // As LV2 deactivates and activates.
    r.phases.push_back(std::move(reset));
    r.phases.push_back(phase("idle after", true, s.after));
    return r;
}

timing::Run judged(const Spec& s, bool enforce = true, bool host_thread = true,
                   std::optional<double> limit = hold_limit) {
    return timing::judge(record(s), {period, limit, host_thread, enforce}).run;
}

std::string text(const timing::Run& r) {
    return std::string(timing::describe(r.verdict)) + " (hold " + timing::describe(r.hold) +
           ", callbacks " + timing::describe(r.callbacks) + ", " + std::to_string(r.errors) +
           " errors): " + r.why;
}

bool says(const timing::Run& r, const char* what) {
    return r.why.find(what) != std::string::npos;
}

// The timings each callback gives: how late, how many deadlines missed, and
// whose time it was.
void summaries() {
    using R = timing::CallbackRecord;
    const std::vector<R> v{
        {0.00, 0.000, 0.000, 0.0005, -1, true},    // On time.
        {0.01, 0.010, 0.010, 0.0450, -1, true},    // The plug-in's: late by 25 ms, 3 deadlines.
        {0.05, 0.062, 0.062, 0.0625, -1, true},    // Woke 12 ms late, fast: scheduling.
        {0.07, 0.070, 0.070, 0.0760, -1, true},    // Slow: 6 ms of a 10 ms period.
        {0.08, 0.080, 0.080, 0.0860, 0.001, true}, // As long, 1 ms of CPU: off the CPU.
        {0.09, 0.090, 0.090, 0.0905, -1, false},   // Failed.
    };
    const auto c = timing::summarise(v, period);
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    check(c.count == 6 && c.failed == 1, "summary: count and failures");
    check(c.late == 2 && c.late_scheduling == 1, "summary: two late, one of them scheduling");
    check(c.missed == 3 + 1, "summary: deadlines missed " + std::to_string(c.missed));
    check(c.slow == 3 && c.slow_off_cpu == 1, "summary: three slow (" + std::to_string(c.slow) +
                                                  "), one mostly off the CPU (" +
                                                  std::to_string(c.slow_off_cpu) + ")");
    check(near(c.worst_late, 0.025) && near(c.worst_wake, 0.012) && near(c.longest, 0.035) &&
              near(c.longest_call, 0.035),
          "summary: worst late, wake, longest");
    check(timing::summarise({}, period).count == 0, "summary: no callbacks");
}

// What each kind of run is judged, as jrp's review of db54160 lists them.
void records() {
    {
        const auto r = judged({});
        check(r.verdict == timing::Verdict::passed && r.hold == timing::Verdict::passed &&
                  r.callbacks == timing::Verdict::passed,
              "clean run: " + text(r));
    }
    {
        // Deadline missed, callback fast: the thread woke late.
        Spec s;
        s.loading = steady(30, 0.012);
        const auto r = judged(s);
        check(r.verdict == timing::Verdict::not_qualified && r.hold == timing::Verdict::passed &&
                  says(r, "the thread woke late"),
              "late wake, fast callback: " + text(r));
    }
    {
        // The callback itself slow: over half its period.
        Spec s;
        s.loading = steady(30, 0, 0.006);
        const auto r = judged(s);
        check(r.verdict == timing::Verdict::failed && r.callbacks == timing::Verdict::failed &&
                  says(r, "over half its period"),
              "slow callback: " + text(r));
    }
    {
        // As long, with little CPU time: blocked on a lock inside the plug-in
        // or descheduled, which the records cannot tell apart (jrp's review
        // of 5f47ecb). It is the plug-in's, so it fails, and says so.
        Spec s;
        s.loading = steady(30, 0, 0.006, 0.001);
        auto r = judged(s);
        check(r.verdict == timing::Verdict::failed && r.callbacks == timing::Verdict::failed &&
                  says(r, "mostly off the CPU: blocked or descheduled"),
              "slow callback with little CPU time: " + text(r));
        // A callback blocked past the next deadline, with almost no CPU time.
        s.loading = steady(30, 0, 0.025, 0.0001);
        r = judged(s);
        check(r.verdict == timing::Verdict::failed && says(r, "late callback") &&
                  says(r, "mostly off the CPU"),
              "blocked callback: " + text(r));
    }
    {
        // Late by its own time: started on time, it still ends past the next deadline.
        Spec s;
        s.loading = steady(30, 0, 0.012);
        const auto r = judged(s);
        check(r.verdict == timing::Verdict::failed && says(r, "late callback"),
              "late by its own time: " + text(r));
    }
    {
        // Loader over its budget fails, whatever the audio thread saw: also
        // with both baselines late and every callback late (the review of
        // 59ac8e0's reproduction).
        Spec s;
        s.held = 0.4;
        auto r = judged(s);
        check(r.verdict == timing::Verdict::failed && r.hold == timing::Verdict::failed &&
                  says(r, "held the host's thread 400.0 ms, over 100 ms"),
              "slow loader: " + text(r));
        s.before = s.after = s.loading = steady(50, 0, 0.012);
        r = judged(s);
        check(r.verdict == timing::Verdict::failed && r.hold == timing::Verdict::failed &&
                  r.callbacks == timing::Verdict::not_qualified,
              "slow loader, every callback late: " + text(r));
    }
    {
        // Baseline unsuitable, before or after: the plug-in's slow callbacks
        // are not judged.
        for (const bool late_before : {true, false}) {
            Spec s;
            (late_before ? s.before : s.after) = steady(50, 0.012);
            s.loading = steady(30, 0, 0.006);
            const auto r = judged(s);
            check(r.verdict == timing::Verdict::not_qualified && says(r, "a baseline was late"),
                  std::string("late baseline ") + (late_before ? "before" : "after") + ": " +
                      text(r));
        }
    }
    {
        // Functional errors fail in every build, and say how many.
        Spec refused;
        refused.accepted = false;
        Spec failing;
        failing.loading[3].ok = false;
        Spec wrong;
        wrong.saved = false;
        Spec dropped;
        dropped.dropped = 5;
        for (const auto* s : {&refused, &failing, &wrong, &dropped})
            for (const bool enforce : {true, false}) {
                const auto r = judged(*s, enforce);
                check(r.error && r.errors > 0 && r.verdict == timing::Verdict::failed &&
                          says(r, "errors"),
                      "error: " + text(r));
            }
    }
    {
        // The host holds audio back and the work is off its thread: no limit
        // applies, however slow.
        Spec s;
        s.held = 0.4;
        s.audio_held = true;
        s.loading = steady(30, 0, 0.3);
        const auto r = judged(s, true, false);
        check(r.verdict == timing::Verdict::reported_only, "nothing limited: " + text(r));
    }
    {
        // No hold limit (the 120 s filter): the callbacks are still judged.
        Spec s;
        s.held = 0.7;
        auto r = judged(s, true, true, std::nullopt);
        check(r.verdict == timing::Verdict::passed && r.hold == timing::Verdict::reported_only,
              "120 s, clean: " + text(r));
        s.loading = steady(30, 0, 0.006);
        r = judged(s, true, true, std::nullopt);
        check(r.verdict == timing::Verdict::failed, "120 s, slow callbacks: " + text(r));
    }
    {
        // A build that does not enforce the limits: not qualified, saying
        // what would fail.
        Spec s;
        s.held = 0.4;
        const auto r = judged(s, false);
        check(r.verdict == timing::Verdict::not_qualified && !r.error && says(r, "would fail"),
              "not enforced: " + text(r));
    }
    {
        // A note for each phase, and only where there is something to say.
        Spec s;
        s.held = 0.4;
        const auto j = timing::judge(record(s), {period, hold_limit, true, true});
        check(j.notes.size() == 5 && j.notes[0].empty() && j.notes[3].empty() &&
                  j.notes[1].find("OVER THE LIMIT") != std::string::npos,
              "notes per phase");
    }
}

// PERF-13: the records as JSON: brackets balance outside strings, no comma
// before a closing bracket, text escaped, and every callback kept.
void records_json() {
    timing::Result result;
    Spec s;
    s.held = 0.4;
    auto rec = record(s);
    rec.phases[1].error = "a \"quoted\" error\n";
    result.runs.push_back(timing::judge(rec, {period, hold_limit, true, true}).run);
    result.records.push_back(rec);
    result.failed = 1;
    timing::Workload w{"10 s", {48000, 2, 2}, 10, 480, std::chrono::milliseconds(100)};
    timing::Options o;
    std::ostringstream out;
    timing::write_records(out, "mock", result, {w}, o);
    const auto json = out.str();
    int depth = 0;
    bool in_string = false, escaped = false, balanced = true, trailing = false;
    char last = 0;
    for (const char c : json) {
        if (in_string) {
            in_string = escaped || c != '"';
            escaped = !escaped && c == '\\';
            continue;
        }
        if (c == '"')
            in_string = true;
        else if (c == '{' || c == '[')
            ++depth;
        else if (c == '}' || c == ']') {
            trailing = trailing || last == ',';
            balanced = balanced && --depth >= 0;
        }
        if (c != ' ' && c != '\n')
            last = c;
    }
    std::size_t rows = 0;
    for (auto at = json.find("], ["); at != std::string::npos; at = json.find("], [", at + 1))
        ++rows;
    check(balanced && depth == 0 && !in_string && !trailing, "records: well-formed JSON");
    check(json.find("\"format\": \"mock\"") != std::string::npos &&
              json.find("\"result\": \"FAILED\"") != std::string::npos &&
              json.find("\"held\": 0.400000") != std::string::npos &&
              json.find("a \\\"quoted\\\" error\\u000a") != std::string::npos,
          "records: fields and escapes");
    // Each phase's callbacks less one separator each: 50 + 30 + 30 + 30 + 50.
    check(rows == 190 - 5, "records: every callback, " + std::to_string(rows) + " separators");
}

// An adapter that keeps the state it is given, taking `load` to accept it.
class Mock final : public lc::Driver {
  public:
    Ms load{0};
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
        std::this_thread::sleep_for(load);
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
        for (std::size_t c = 0; c < out.size(); ++c)
            std::copy_n(in[c % in.size()], frames, out[c]);
        ++calls;
        return true;
    }
    std::atomic<std::size_t> calls{0};
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
};

// A folder of its own, so that builds testing at once do not share one.
std::filesystem::path scratch() {
    return std::filesystem::temp_directory_path() /
           ("convolver-load-timing-harness-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

timing::Result run(Mock& mock, int attempts, Ms limit = 100ms, bool warm = false) {
    timing::Options o;
    o.work_directory = scratch();
    o.enforce = true;
    o.attempts = attempts;
    o.warm = warm;
    // A short filter: the mock does not prepare it (half a second when warm,
    // so that a fill is 49 callbacks).
    const std::vector<timing::Workload> list{
        {"tiny", {48000, 2, 2}, warm ? 0.5 : 0.01, 512, limit}};
    const std::vector<timing::Entry> entries{
        {"state", [&](const timing::Filter& f, timing::Audio&) {
             return timing::Outcome{mock.restore(f.state), std::nullopt};
         }}};
    auto r = timing::run(mock, entries, list, o);
    std::filesystem::remove_all(o.work_directory);
    return r;
}

// A little with real threads: the renderer's records reach the verdict.
// Runs that should pass the hold get a limit of two seconds, far above any
// runner's scheduling delays: on 8 November 2026 a starved macOS runner
// (run 42522591379 on 59a093f, sanitizer build, every callback waking about
// 60 ms late) held an instant load 130 ms, over the 100 ms these runs used,
// and overslept the slow loader's 60 ms by 150 ms. The slow loader can only
// get slower, so its 20 ms limit still fails it on any machine.
constexpr Ms passing_limit = 2000ms;

void integration() {
    {
        Mock mock;
        const auto r = run(mock, 1, passing_limit);
        const auto every_phase_heard = [&] {
            for (const auto& rec : r.records)
                for (const auto& p : rec.phases)
                    if (p.callbacks.empty())
                        return false;
            return !r.records.empty();
        };
        // Each callback's times in order, and its CPU time (where measured)
        // within the call, allowing for the clock reads around it.
        const auto records_consistent = [&] {
            for (const auto& rec : r.records)
                for (const auto& p : rec.phases)
                    for (const auto& c : p.callbacks)
                        if (!(c.due <= c.woke && c.woke <= c.entered && c.entered <= c.returned) ||
                            (c.cpu != -1 && (c.cpu < 0 || c.cpu > c.returned - c.entered + 0.002)))
                            return false;
            return true;
        };
        check(r.errors == 0 && r.runs.size() == 1 && r.records.size() == 1 &&
                  r.records[0].phases.size() == 5 && every_phase_heard() && records_consistent() &&
                  r.runs[0].hold == timing::Verdict::passed,
              "quick adapter: " + text(r.runs.at(0)));
    }
    {
        // Over its limit on each of two runs: both kept, the entry fails.
        Mock mock;
        mock.load = 60ms;
        const auto r = run(mock, 2, 20ms);
        check(r.failed == 1 && r.runs.size() == 2 && timing::exit_code(r) == 1 &&
                  r.runs[0].hold == timing::Verdict::failed &&
                  r.runs[1].hold == timing::Verdict::failed,
              "slow loader: " + text(r.runs.at(0)));
    }
    {
        Mock mock;
        mock.refuse = true;
        const auto r = run(mock, 3);
        check(r.errors > 0 && r.failed == 1 && r.runs.size() == 1 && timing::exit_code(r) == 1,
              "refused load: " + text(r.runs.at(0)));
    }
    {
        // PERF-14: warm phases are filled faster than real time, shown and
        // not judged. The verdict itself depends on the runner's scheduling
        // (macOS CI's thread woke late), so only the hold is checked.
        Mock mock;
        const auto r = run(mock, 1, passing_limit, true);
        std::string names;
        bool reported_only_warm = true;
        for (const auto& p : r.records.at(0).phases) {
            names += (names.empty() ? "" : ", ") + p.name;
            reported_only_warm = reported_only_warm && p.reported == (p.name.rfind("warm", 0) == 0);
        }
        check(names == "baseline, first load, warm first, replacement, warm second, reset, idle "
                       "after" &&
                  reported_only_warm && !r.runs.at(0).error &&
                  r.runs.at(0).hold == timing::Verdict::passed,
              "warm phases: " + names + "; " + text(r.runs.at(0)));
        // Two fills of 49 callbacks, plus the paced ones (about 3.5 s of them).
        check(mock.calls.load() >= 2 * 49 + 250,
              "warm phases: " + std::to_string(mock.calls.load()) + " callbacks");
    }
}
} // namespace

int main() {
    try {
        input_is_quiet_noise();
        verdict_table();
        summaries();
        records();
        records_json();
        integration();
    } catch (const std::exception& e) {
        std::cout << "FAILED: " << e.what() << '\n';
        return 1;
    }
    std::cout << "load-timing harness: " << failures << " failed\n";
    return failures ? 1 : 0;
}
