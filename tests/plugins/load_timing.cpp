// SPDX-License-Identifier: GPL-2.0-or-later
// The loading-responsiveness timings; see load_timing.hpp.
#include "load_timing.hpp"
#include "convolver/host.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>
#if !defined(_WIN32)
#include <time.h> // clock_gettime
#endif

namespace convolver::plugin_test::load_timing {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Seconds = std::chrono::duration<double>;

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
constexpr const char* build = "sanitizers";
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
constexpr const char* build = "sanitizers";
#elif defined(NDEBUG)
constexpr const char* build = nullptr;
#else
constexpr const char* build = "debug";
#endif
#elif defined(NDEBUG)
constexpr const char* build = nullptr;
#else
constexpr const char* build = "debug";
#endif

Seconds period(const Workload& w) {
    return Seconds(static_cast<double>(w.block) / w.host.sample_rate);
}

// One path per channel, input k to output k: exponentially decaying noise,
// different for each path and each seed.
Configuration long_filter(const Workload& w, std::uint32_t seed) {
    Configuration c;
    c.sample_rate = w.host.sample_rate;
    c.input_channels = w.host.inputs;
    c.output_channels = w.host.outputs;
    c.input_delays.assign(c.input_channels, 0);
    c.output_delays.assign(c.output_channels, 0);
    const auto taps = static_cast<std::size_t>(w.seconds * w.host.sample_rate);
    const double decay = -1 / (0.25 * w.host.sample_rate); // -35 dB a second.
    for (std::size_t k = 0; k < c.output_channels; ++k) {
        FilterPath p;
        p.impulse.resize(taps);
        std::uint32_t state = seed * 2654435761u + static_cast<std::uint32_t>(k) + 1;
        for (std::size_t t = 0; t < taps; ++t) {
            state = state * 1664525u + 1013904223u;
            const double noise = (static_cast<double>(state >> 8) / (1u << 24)) * 2 - 1;
            p.impulse[t] =
                static_cast<float>(0.5 * noise * std::exp(decay * static_cast<double>(t)));
        }
        p.inputs = {{k % c.input_channels, 1}};
        p.outputs = {{k, 1}};
        c.paths.push_back(std::move(p));
    }
    return c;
}

void put(std::ofstream& out, std::uint32_t v, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>((v >> (8 * i)) & 0xff));
}

// The filter as a user would pick it: a 32-bit float WAV with one channel
// per path, and a .cfg routing them (docs/use/configuration.md).
fs::path write_file(const fs::path& folder, const std::string& name, const Configuration& c) {
    const auto wav = folder / (name + ".wav"), cfg = folder / (name + ".cfg");
    const auto channels = static_cast<std::uint32_t>(c.paths.size());
    const auto frames = static_cast<std::uint32_t>(c.paths.at(0).impulse.size());
    const auto rate = static_cast<std::uint32_t>(c.sample_rate);
    const std::uint32_t data = frames * channels * 4;
    {
        std::ofstream out(wav, std::ios::binary);
        out.write("RIFF", 4);
        put(out, 36 + data, 4);
        out.write("WAVEfmt ", 8);
        put(out, 16, 4);
        put(out, 3, 2); // IEEE float.
        put(out, channels, 2);
        put(out, rate, 4);
        put(out, rate * channels * 4, 4);
        put(out, channels * 4, 2);
        put(out, 32, 2);
        out.write("data", 4);
        put(out, data, 4);
        std::vector<float> frame(channels);
        for (std::uint32_t t = 0; t < frames; ++t) {
            for (std::uint32_t k = 0; k < channels; ++k)
                frame[k] = c.paths[k].impulse[t];
            out.write(reinterpret_cast<const char*>(frame.data()),
                      static_cast<std::streamsize>(frame.size() * sizeof(float)));
        }
        if (!out)
            throw std::runtime_error("could not write " + wav.string());
    }
    std::ofstream out(cfg);
    out << c.sample_rate << ' ' << c.input_channels << ' ' << c.output_channels << " 0\n";
    for (std::size_t i = 0; i < c.input_channels; ++i)
        out << (i ? " 0" : "0");
    out << '\n';
    for (std::size_t o = 0; o < c.output_channels; ++o)
        out << (o ? " 0" : "0");
    out << '\n';
    for (std::size_t k = 0; k < c.paths.size(); ++k)
        out << wav.filename().string() << '\n'
            << k << '\n'
            << c.paths[k].inputs.at(0).channel << '\n'
            << c.paths[k].outputs.at(0).channel << '\n';
    if (!out)
        throw std::runtime_error("could not write " + cfg.string());
    return cfg;
}

// The audio thread's CPU time in seconds, or -1 where it is not measured.
// Windows updates thread times only at the scheduler's tick (about 15 ms),
// too coarse for a callback.
double thread_cpu() {
#if defined(_WIN32)
    return -1;
#else
    timespec t{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0)
        return -1;
    return static_cast<double>(t.tv_sec) + static_cast<double>(t.tv_nsec) * 1e-9;
#endif
}

// Calls the driver's process() every `block` frames of real time, as a
// host's audio device does, and records each callback (CallbackRecord). A
// callback is late when it finishes after the next one was due; the
// schedule then restarts from its end, as a device that dropped a buffer
// carries on. The records are allocated beforehand: the audio thread does not
// allocate or format, except to keep the first exception's text.
class Renderer final : public Audio {
  public:
    struct Seen {
        std::vector<CallbackRecord> callbacks;
        std::size_t dropped = 0;
        std::string error;
    };

    Renderer(lifecycle::Driver& driver, const Workload& w)
        : driver_(driver), period_(static_cast<double>(w.block) / w.host.sample_rate),
          in_(w.host.inputs, std::vector<float>(w.block)),
          out_(w.host.outputs, std::vector<float>(w.block)), block_(w.block),
          records_(max_records) {
        for (std::size_t c = 0; c < in_.size(); ++c)
            in_[c] = quiet_noise(c, block_); // As a host plays.
        for (auto& v : in_)
            ins_.push_back(v.data());
        for (auto& v : out_)
            outs_.push_back(v.data());
        start_ = Clock::now();
        thread_ = std::thread([this] { loop(); });
    }
    ~Renderer() override {
        stop_ = true;
        thread_.join();
    }
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Starts a new phase; returns what the previous one recorded.
    Seen take() {
        std::lock_guard lock(mutex_);
        Seen seen{{records_.begin(), records_.begin() + static_cast<std::ptrdiff_t>(used_)},
                  dropped_,
                  std::move(error_)};
        used_ = dropped_ = 0;
        error_.clear();
        return seen;
    }
    // Held by a caller whose entry point the host does not run alongside audio.
    std::mutex& gate() {
        return gate_;
    }
    void before_next(const std::function<void()>& f) override {
        std::unique_lock lock(mutex_);
        task_ = &f;
        changed_.wait(lock, [&] { return task_ == nullptr && done_ >= task_callback_; });
    }
    void wait_callbacks(std::size_t n) override {
        std::unique_lock lock(mutex_);
        const auto until = done_ + n;
        changed_.wait(lock, [&] { return done_ >= until; });
    }
    void wait_for(Seconds s) {
        wait_callbacks(static_cast<std::size_t>(std::ceil(s / period_)));
    }

  private:
    // About 11 minutes of 512-frame callbacks at 48 kHz: far more than a phase.
    static constexpr std::size_t max_records = 1 << 16;

    double at(Clock::time_point t) const {
        return Seconds(t - start_).count();
    }

    void loop() {
        auto due = Clock::now();
        while (!stop_) {
            // Sleeps until just before the callback is due and waits out the
            // rest: some timers (Windows) wake a millisecond or more late,
            // which would show as late callbacks the plug-in did not cause.
            std::this_thread::sleep_until(due - std::chrono::milliseconds(2));
            while (Clock::now() < due)
                std::this_thread::yield();
            const std::function<void()>* task = nullptr;
            {
                std::lock_guard lock(mutex_);
                std::swap(task, task_);
                if (task)
                    task_callback_ = done_ + 1; // before_next returns after it.
            }
            bool ok = true;
            std::string error;
            const auto woke = Clock::now();
            Clock::time_point entered, returned;
            double cpu = -1;
            {
                std::lock_guard gate(gate_); // Held while the host holds audio back.
                entered = Clock::now();
                const double cpu_before = thread_cpu();
                try { // A failure is counted, and the run goes on.
                    if (task)
                        (*task)();
                    ok = driver_.process(ins_, outs_, block_);
                } catch (const std::exception& e) {
                    ok = false;
                    error = e.what();
                } catch (...) {
                    ok = false;
                    error = "an exception that is not a std::exception";
                }
                returned = Clock::now();
                const double cpu_after = thread_cpu();
                if (cpu_before >= 0 && cpu_after >= 0)
                    cpu = cpu_after - cpu_before;
            }
            const auto next = due + std::chrono::duration_cast<Clock::duration>(period_);
            {
                std::lock_guard lock(mutex_);
                if (used_ < records_.size())
                    records_[used_++] = {at(due), at(woke), at(entered), at(returned), cpu, ok};
                else
                    ++dropped_;
                if (!ok && error_.empty())
                    error_ = std::move(error);
                ++done_;
            }
            changed_.notify_all();
            due = returned > next ? returned : next;
        }
    }

    lifecycle::Driver& driver_;
    Seconds period_;
    std::vector<std::vector<float>> in_, out_;
    std::vector<const float*> ins_;
    std::vector<float*> outs_;
    std::size_t block_;
    std::vector<CallbackRecord> records_;
    std::size_t used_ = 0, dropped_ = 0;
    std::string error_;
    Clock::time_point start_;
    std::mutex mutex_, gate_;
    std::condition_variable changed_;
    const std::function<void()>* task_ = nullptr;
    std::size_t done_ = 0, task_callback_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

void report(const std::string& format, const Workload& w, const std::string& entry, const Phase& p,
            const Callbacks& c, const std::string& note) {
    char time[32] = "-", line[256];
    if (p.held)
        std::snprintf(time, sizeof time, "%.1f", *p.held * 1000);
    const bool ok = p.accepted && c.failed == 0 && p.dropped == 0;
    std::snprintf(line, sizeof line,
                  "%-5s %-5s %-14s %-11s %8s %9zu %5zu %6zu %8.2f %8.2f %8.2f%s\n", format.c_str(),
                  w.name.c_str(), entry.c_str(), p.name.c_str(), time, c.count, c.late,
                  c.late_scheduling, c.worst_late * 1000, c.worst_wake * 1000,
                  c.longest_call * 1000, ok ? "" : "  REFUSED OR FAILED");
    std::cout << line;
    if (!p.error.empty())
        std::cout << "      a callback threw: " << p.error << '\n';
    if (p.dropped)
        std::cout << "      " << p.dropped << " callbacks beyond what the renderer records\n";
    if (!note.empty())
        std::cout << "      " << note << '\n';
    std::cout << std::flush;
}

std::string join(const std::vector<std::string>& v) {
    std::string text;
    for (const auto& s : v)
        text += (text.empty() ? "" : "; ") + s;
    return text;
}

// "2 late callbacks, a callback took over half its period", or empty.
std::string callbacks_text(std::size_t late, std::size_t slow) {
    std::string text;
    if (late)
        text = std::to_string(late) + (late == 1 ? " late callback" : " late callbacks");
    if (slow)
        text += (text.empty() ? "" : ", ") + std::string("a callback took over half its period");
    return text;
}
} // namespace

std::vector<float> quiet_noise(std::size_t channel, std::size_t frames) {
    std::vector<float> v(frames);
    for (std::size_t t = 0; t < frames; ++t) {
        // Signed before subtracting: unsigned arithmetic would wrap the
        // negative half to about 1.8e15.
        const auto step = static_cast<long long>((t * 7919 + channel * 104729) % 2001);
        v[t] = static_cast<float>(step - 1000) * 1e-4f;
    }
    return v;
}

std::vector<Workload> workloads(const Options& o) {
    std::vector<Workload> list;
    // jrp, 5 November 2026: 100 ms feels instant; 250 ms is where Apple's
    // tools count an application as hung. The 120 s filter's hold is
    // reported only; its callbacks are judged.
    for (const double s : {1.0, 10.0, 30.0})
        list.push_back({std::to_string(static_cast<int>(s)) + " s",
                        {48000, 2, 2},
                        s,
                        512,
                        Seconds(s > 10 ? 0.25 : 0.1)});
    if (o.long_filters)
        list.push_back({"120 s", {48000, 2, 2}, 120, 512, std::nullopt});
    return list;
}

bool optimised_build() {
    return build == nullptr;
}

const char* describe(Verdict v) {
    switch (v) {
    case Verdict::passed:
        return "passed";
    case Verdict::failed:
        return "FAILED";
    case Verdict::not_qualified:
        return "not qualified";
    case Verdict::reported_only:
        return "reported only";
    }
    return "?";
}

Callbacks summarise(const std::vector<CallbackRecord>& records, double period) {
    Callbacks c;
    const double half = period / 2;
    for (const auto& r : records) {
        ++c.count;
        c.failed += r.ok ? 0 : 1;
        const double call = r.returned - r.entered, own = r.returned - r.woke;
        c.longest = std::max(c.longest, own);
        c.longest_call = std::max(c.longest_call, call);
        c.worst_wake = std::max(c.worst_wake, r.woke - r.due);
        // Slow with little CPU time: the thread was descheduled in the call.
        const bool descheduled = r.cpu >= 0 && r.cpu <= half && call > half;
        if (r.returned > r.due + period) {
            ++c.late;
            const double by = r.returned - (r.due + period);
            c.worst_late = std::max(c.worst_late, by);
            c.missed += static_cast<std::size_t>(std::ceil(by / period));
            // Started on time, it would have finished on time.
            if (own <= period || descheduled)
                ++c.late_scheduling;
        }
        if (call > half) {
            ++c.slow;
            if (descheduled)
                ++c.slow_scheduling;
        }
    }
    return c;
}

Judged judge(const RunRecord& rec, const Limits& l) {
    Judged j;
    auto& r = j.run;
    r.workload = rec.workload;
    r.entry = rec.entry;
    r.attempt = rec.attempt;
    std::vector<Callbacks> seen;
    std::size_t errors = rec.saved ? 0 : 1;
    // Late callbacks in a baseline: the trailing one plays a loaded filter,
    // so that shows uncertainty rather than proving the machine alone was
    // busy (jrp, 5 November 2026); the run's timing cannot be qualified.
    bool busy = false;
    for (const auto& p : rec.phases) {
        seen.push_back(summarise(p.callbacks, l.period));
        errors += seen.back().failed + (p.accepted ? 0 : 1) + (p.dropped ? 1 : 0);
        busy = busy || (p.baseline && seen.back().late);
    }
    bool hold_judged = false, callbacks_judged = false, hold_over = false, callbacks_over = false;
    std::vector<std::string> failed, unqualified;
    for (std::size_t i = 0; i < rec.phases.size(); ++i) {
        const auto& p = rec.phases[i];
        const auto& c = seen[i];
        if (p.baseline) {
            j.notes.push_back(
                c.late ? "note: late callbacks here, so this run's timing could not be qualified"
                       : "");
            continue;
        }
        std::vector<std::string> over, noisy;
        // The hold is the plug-in's own time on the caller's thread, judged
        // whatever the audio thread saw.
        const bool hold_limited = p.held && l.host_thread && l.held;
        hold_judged = hold_judged || hold_limited;
        if (hold_limited && *p.held > *l.held) {
            char text[96];
            std::snprintf(text, sizeof text, "held the host's thread %.1f ms, over %.0f ms",
                          *p.held * 1000, *l.held * 1000);
            over.emplace_back(text);
            hold_over = true;
        }
        if (!p.audio_held) {
            callbacks_judged = true;
            const auto own = callbacks_text(c.late - c.late_scheduling, c.slow - c.slow_scheduling);
            if (!own.empty() && busy)
                noisy.push_back(own + " (a baseline was late too)");
            else if (!own.empty()) {
                over.push_back(own);
                callbacks_over = true;
            }
            const auto scheduling = callbacks_text(c.late_scheduling, c.slow_scheduling);
            if (!scheduling.empty())
                noisy.push_back(scheduling + " (the thread woke late or was descheduled; the "
                                             "plug-in's own time was within the limits)");
        }
        std::string note;
        for (const auto& v : over) {
            failed.push_back(p.name + ": " + v);
            note += (note.empty() ? "" : "; ") + v;
        }
        if (!note.empty())
            note = (l.enforce ? "OVER THE LIMIT: " : "over the limit (not enforced): ") + note;
        for (const auto& v : noisy) {
            unqualified.push_back(p.name + ": " + v);
            note += (note.empty() ? "" : "; ") + std::string("not judged: ") + v;
        }
        j.notes.push_back(note);
    }
    // Each limit separately: the hold of the host's thread, then the callbacks.
    r.hold = !hold_judged ? Verdict::reported_only : hold_over ? Verdict::failed : Verdict::passed;
    r.callbacks = !callbacks_judged      ? Verdict::reported_only
                  : callbacks_over       ? Verdict::failed
                  : !unqualified.empty() ? Verdict::not_qualified
                                         : Verdict::passed;
    r.errors = errors;
    r.error = errors != 0;
    if (r.error) {
        // A defect, not timing: judged in every build, never rerun.
        r.verdict = Verdict::failed;
        r.why = std::to_string(errors) +
                " errors (a refused load, a failed callback or the wrong filter saved)";
    } else if (!l.enforce) {
        r.verdict = r.hold = r.callbacks = Verdict::not_qualified;
        r.why = "limits not enforced in this build";
        if (!failed.empty())
            r.why += "; would fail: " + join(failed);
    } else {
        r.verdict = combine(r.hold, r.callbacks);
        r.why = join(failed);
        if (!unqualified.empty())
            r.why += (r.why.empty() ? "" : "; ") + std::string("not judged: ") + join(unqualified);
    }
    return j;
}

Result run(lifecycle::Driver& driver, const std::vector<Entry>& entries,
           const std::vector<Workload>& list, const Options& o) {
    const auto format = driver.format().name;
    Result result;
    std::cout << format << " load timing ("
              << (o.enforce ? std::string(build ? build : "optimised") + " build: limits enforced"
                            : std::string(build ? build : "optimised") +
                                  " build: timings not representative, limits reported only")
              << ")\n"
              << "held: ms the calling thread was held; late: callbacks that finished after the "
                 "next was due; sched.: of those, late only because the thread woke late or was "
                 "descheduled; worst: ms past the next deadline at worst; wake: the latest start, "
                 "ms after due; call: ms of the longest call into the plug-in\n";
    char header[256];
    std::snprintf(header, sizeof header, "%-5s %-5s %-14s %-11s %8s %9s %5s %6s %8s %8s %8s\n", "",
                  "filt.", "entry", "phase", "held ms", "callbacks", "late", "sched.", "worst",
                  "wake", "call");
    std::cout << header;
    fs::create_directories(o.work_directory);
    for (const auto& w : list) {
        Filter x, y;
        x.name = "x";
        y.name = "y";
        x.configuration = long_filter(w, 1);
        y.configuration = long_filter(w, 2);
        for (auto* f : {&x, &y}) {
            HostState s;
            s.configuration = f->configuration;
            f->state = encode_host_state(s);
            f->file = write_file(o.work_directory, format + "-" + f->name, f->configuration);
        }
        const Limits limits{period(w).count(),
                            w.held_limit ? std::optional<double>(w.held_limit->count())
                                         : std::nullopt,
                            true, o.enforce};
        for (const auto& e : entries)
            // A shared machine can stall any one run, so an entry that did not
            // pass runs again, up to o.attempts times. Every run is kept.
            for (int attempt = 1;; ++attempt) {
                RunRecord rec{w.name, e.name, attempt, {}, true};
                const auto phase = [](std::string name, bool baseline, Renderer::Seen seen) {
                    Phase p;
                    p.name = std::move(name);
                    p.baseline = baseline;
                    p.callbacks = std::move(seen.callbacks);
                    p.dropped = seen.dropped;
                    p.error = std::move(seen.error);
                    return p;
                };
                driver.create(w.host, true);
                if (!driver.start())
                    throw std::runtime_error(format + ": the instance did not start");
                {
                    Renderer audio(driver, w);
                    const auto settle = Seconds(0.3); // Beyond any crossfade.
                    // Half a second with nothing loading before the phases and
                    // another after them: one short baseline on Windows CI was
                    // once on time by chance, between two that were not.
                    const auto baseline = Seconds(0.5);
                    audio.wait_for(settle);
                    audio.take();
                    audio.wait_for(baseline);
                    rec.phases.push_back(phase("baseline", true, audio.take()));
                    for (const auto* f : {&x, &y}) {
                        Outcome outcome;
                        Seconds held{0};
                        const auto timed = [&] {
                            const auto begun = Clock::now();
                            outcome = e.load(*f, audio);
                            held = outcome.held.value_or(Clock::now() - begun);
                        };
                        if (e.audio_waits) {
                            std::lock_guard hold(audio.gate()); // Timed once audio has stopped.
                            timed();
                        } else
                            timed();
                        audio.wait_for(settle);
                        auto p = phase(f == &x ? "first load" : "replacement", false, audio.take());
                        p.held = held.count();
                        p.accepted = outcome.accepted;
                        p.audio_held = e.audio_waits;
                        rec.phases.push_back(std::move(p));
                    }
                    audio.before_next([&] { driver.reset(); });
                    audio.wait_for(settle);
                    auto reset = phase("reset", false, audio.take());
                    reset.audio_held = o.reset_holds_audio;
                    rec.phases.push_back(std::move(reset));
                    audio.wait_for(baseline);
                    rec.phases.push_back(phase("idle after", true, audio.take()));
                    // The filter that plays is the replacement, as saved.
                    const auto saved = driver.saved();
                    rec.saved = saved.configuration && saved.configuration->paths.at(0).impulse ==
                                                           y.configuration.paths[0].impulse;
                    if (!rec.saved)
                        std::cout << format << ": after " << e.name
                                  << " the instance does not save " << w.name << " filter y\n";
                }
                driver.stop();
                driver.destroy();
                auto entry_limits = limits;
                entry_limits.host_thread = e.host_thread;
                auto judged = judge(rec, entry_limits);
                for (std::size_t i = 0; i < rec.phases.size(); ++i)
                    report(format, w, e.name, rec.phases[i],
                           summarise(rec.phases[i].callbacks, limits.period), judged.notes[i]);
                const auto& r = judged.run;
                result.errors += r.errors;
                result.runs.push_back(r);
                result.records.push_back(std::move(rec));
                // The entry so far: run again while it has not passed.
                const auto so_far = verdict(result.runs.end() - attempt, result.runs.end());
                if (o.enforce && !r.error &&
                    (so_far == Verdict::failed || so_far == Verdict::not_qualified) &&
                    attempt < o.attempts) {
                    std::cout << "      run " << attempt << " of " << o.attempts << " "
                              << describe(r.verdict) << "; running " << e.name << " with the "
                              << w.name << " filter again\n";
                    continue;
                }
                break;
            }
        fs::remove(x.file);
        fs::remove(y.file);
        fs::remove(fs::path(x.file).replace_extension(".wav"));
        fs::remove(fs::path(y.file).replace_extension(".wav"));
    }
    // One line for each entry and filter, with every run that did not pass.
    std::cout << format << " load timing summary\n";
    for (std::size_t i = 0; i < result.runs.size();) {
        auto j = i;
        while (j < result.runs.size() && result.runs[j].workload == result.runs[i].workload &&
               result.runs[j].entry == result.runs[i].entry)
            ++j;
        const auto v = verdict(result.runs.begin() + static_cast<std::ptrdiff_t>(i),
                               result.runs.begin() + static_cast<std::ptrdiff_t>(j));
        char line[96];
        std::snprintf(line, sizeof line, "  %-5s %-14s %s", result.runs[i].workload.c_str(),
                      result.runs[i].entry.c_str(), describe(v));
        std::cout << line << " (" << (j - i) << (j - i == 1 ? " run" : " runs");
        if (v != Verdict::passed && j - i > 1) {
            // Which run met each limit, when no run met both.
            const auto a = result.runs.begin() + static_cast<std::ptrdiff_t>(i),
                       b = result.runs.begin() + static_cast<std::ptrdiff_t>(j);
            for (const auto& [name, limit] :
                 {std::pair{"hold", &Run::hold}, std::pair{"callbacks", &Run::callbacks}})
                if (const int k = met_on(a, b, limit))
                    std::cout << "; " << name << " met on run " << k;
        }
        std::cout << ")\n";
        for (auto k = i; k < j; ++k)
            if (result.runs[k].verdict != Verdict::passed &&
                result.runs[k].verdict != Verdict::reported_only)
                std::cout << "        run " << result.runs[k].attempt << " "
                          << describe(result.runs[k].verdict) << ": " << result.runs[k].why << '\n';
        switch (v) {
        case Verdict::passed:
            ++result.passed;
            if (j - i > 1)
                ++result.passed_after_failure; // Runs stop at the first pass.
            break;
        case Verdict::failed:
            ++result.failed;
            break;
        case Verdict::not_qualified:
            ++result.not_qualified;
            break;
        case Verdict::reported_only:
            ++result.reported_only;
            break;
        }
        i = j;
    }
    const char* overall = result.errors || result.failed ? "FAILED"
                          : result.not_qualified         ? "NOT QUALIFIED"
                          : result.passed == 0           ? "REPORTED ONLY"
                                                         : "PASSED";
    std::cout << format << " load timing: " << overall << " (" << result.passed << " passed";
    if (result.passed_after_failure)
        std::cout << ", " << result.passed_after_failure << " of them after a run that did not";
    std::cout << "; " << result.failed << " failed, " << result.not_qualified << " not qualified, "
              << result.reported_only << " reported only; " << result.errors << " errors)\n";
    return result;
}

Verdict combine(Verdict hold, Verdict callbacks) {
    if (hold == Verdict::failed || callbacks == Verdict::failed)
        return Verdict::failed;
    if (hold == Verdict::not_qualified || callbacks == Verdict::not_qualified)
        return Verdict::not_qualified;
    if (hold == Verdict::reported_only && callbacks == Verdict::reported_only)
        return Verdict::reported_only;
    return Verdict::passed;
}

Verdict verdict(std::vector<Run>::const_iterator first, std::vector<Run>::const_iterator last) {
    if (std::any_of(first, last, [](const Run& r) { return r.error; }))
        return Verdict::failed;
    // A pass needs one complete qualifying run: one that met both limits
    // (or met one where the other does not apply).
    if (std::any_of(first, last, [](const Run& r) { return r.verdict == Verdict::passed; }))
        return Verdict::passed;
    // Otherwise each limit across the runs: met if one run met it.
    const auto across = [&](Verdict Run::* limit) {
        const auto any = [&](Verdict v) {
            return std::any_of(first, last, [&](const Run& r) { return r.*limit == v; });
        };
        return any(Verdict::passed)          ? Verdict::passed
               : any(Verdict::failed)        ? Verdict::failed
               : any(Verdict::not_qualified) ? Verdict::not_qualified
                                             : Verdict::reported_only;
    };
    const auto hold = across(&Run::hold), callbacks = across(&Run::callbacks);
    // Failed: a limit that no run met and one run broke with evidence, or
    // every run broke one (a stalled hold in one, late callbacks after an
    // on-time baseline in the other).
    const auto broke = [](const Run& r) {
        return r.hold == Verdict::failed || r.callbacks == Verdict::failed;
    };
    if (hold == Verdict::failed || callbacks == Verdict::failed || std::all_of(first, last, broke))
        return Verdict::failed;
    if (hold == Verdict::reported_only && callbacks == Verdict::reported_only)
        return Verdict::reported_only;
    // Each limit met in some run, or one could not be judged, but no run
    // met both: not qualified.
    return Verdict::not_qualified;
}

int met_on(std::vector<Run>::const_iterator first, std::vector<Run>::const_iterator last,
           Verdict Run::* limit) {
    const auto k =
        std::find_if(first, last, [&](const Run& r) { return r.*limit == Verdict::passed; });
    return k == last ? 0 : k->attempt;
}

int exit_code(const Result& r) {
    if (r.errors || r.failed)
        return 1;
    // Nothing judged (every entry reported only, or none ran) is not a pass.
    return r.not_qualified || r.passed == 0 ? not_qualified_exit : 0;
}
} // namespace convolver::plugin_test::load_timing
