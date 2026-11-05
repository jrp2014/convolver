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
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace convolver::plugin_test::load_timing {
namespace {
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
using Seconds = std::chrono::duration<double>;

double ms(Seconds s) {
    return s.count() * 1000;
}

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

// What the audio thread saw during one phase.
struct Callbacks {
    std::size_t count = 0, late = 0, failed = 0;
    Seconds longest{0}, worst_late{0};
    std::string error; // The first exception a callback threw.
};

// Calls the driver's process() every `block` frames of real time, as a
// host's audio device does. A callback is late when it finishes after the
// next one was due; the schedule then restarts from its end, as a device
// that dropped a buffer carries on.
class Renderer final : public Audio {
  public:
    Renderer(lifecycle::Driver& driver, const Workload& w)
        : driver_(driver), period_(static_cast<double>(w.block) / w.host.sample_rate),
          in_(w.host.inputs, std::vector<float>(w.block)),
          out_(w.host.outputs, std::vector<float>(w.block)), block_(w.block) {
        for (std::size_t c = 0; c < in_.size(); ++c)
            in_[c] = quiet_noise(c, block_); // As a host plays.
        for (auto& v : in_)
            ins_.push_back(v.data());
        for (auto& v : out_)
            outs_.push_back(v.data());
        thread_ = std::thread([this] { loop(); });
    }
    ~Renderer() override {
        stop_ = true;
        thread_.join();
    }
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    Seconds period() const {
        return period_;
    }
    // Starts a new phase; returns what the previous one saw.
    Callbacks take() {
        std::lock_guard lock(mutex_);
        return std::exchange(seen_, {});
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
            // From before the gate: a callback the host holds back is long.
            const auto begun = Clock::now();
            Clock::time_point end;
            {
                std::lock_guard gate(gate_);
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
                end = Clock::now();
            }
            const auto next = due + std::chrono::duration_cast<Clock::duration>(period_);
            {
                std::lock_guard lock(mutex_);
                ++seen_.count;
                seen_.failed += ok ? 0 : 1;
                if (seen_.error.empty())
                    seen_.error = std::move(error);
                seen_.longest = std::max<Seconds>(seen_.longest, end - begun);
                if (end > next) {
                    ++seen_.late;
                    seen_.worst_late = std::max<Seconds>(seen_.worst_late, end - next);
                }
                ++done_;
            }
            changed_.notify_all();
            due = end > next ? end : next;
        }
    }

    lifecycle::Driver& driver_;
    Seconds period_;
    std::vector<std::vector<float>> in_, out_;
    std::vector<const float*> ins_;
    std::vector<float*> outs_;
    std::size_t block_;
    std::mutex mutex_, gate_;
    std::condition_variable changed_;
    const std::function<void()>* task_ = nullptr;
    std::size_t done_ = 0, task_callback_ = 0;
    Callbacks seen_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

void report(const std::string& format, const Workload& w, const std::string& entry,
            const char* phase, std::optional<Seconds> held, const Callbacks& c, bool ok,
            const std::string& over = {}) {
    char time[32] = "-", line[256];
    if (held)
        std::snprintf(time, sizeof time, "%.1f", ms(*held));
    std::snprintf(line, sizeof line, "%-5s %-5s %-14s %-12s %10s %9zu %6zu %10.2f %10.2f%s\n",
                  format.c_str(), w.name.c_str(), entry.c_str(), phase, time, c.count, c.late,
                  ms(c.worst_late), ms(c.longest), ok ? "" : "  REFUSED OR FAILED");
    std::cout << line;
    if (!c.error.empty())
        std::cout << "      a callback threw: " << c.error << '\n';
    if (!over.empty())
        std::cout << "      " << over << '\n';
    std::cout << std::flush;
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
                 "next was due, worst by how many ms; longest: ms of the longest callback\n";
    char header[256];
    std::snprintf(header, sizeof header, "%-5s %-5s %-14s %-12s %10s %9s %6s %10s %10s\n", "",
                  "filt.", "entry", "phase", "held ms", "callbacks", "late", "worst ms",
                  "longest ms");
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
        for (const auto& e : entries)
            // A shared machine can stall any one run, so an entry that did not
            // pass runs again, up to o.attempts times. Every run is kept.
            for (int attempt = 1;; ++attempt) {
                Run r{w.name, e.name, attempt, Verdict::passed, {}};
                const auto errors_before = result.errors;
                // Held back by a busy machine before anything loaded: late
                // callbacks while loading then say nothing about the plug-in.
                bool busy = false;
                // Each limit applied to some phase; each was exceeded.
                bool hold_judged = false, callbacks_judged = false;
                bool hold_over = false, callbacks_over = false;
                std::vector<std::string> failed, unqualified;
                // Judges one phase by jrp's limits; returns the note for its line.
                const auto judge = [&](const char* phase, std::optional<Seconds> held,
                                       const Callbacks& c, bool audio_held) {
                    std::vector<std::string> over, noisy;
                    // The hold is the plug-in's own time on the caller's
                    // thread, judged whatever the audio thread saw.
                    const bool hold_limited = held && e.host_thread && w.held_limit;
                    hold_judged = hold_judged || hold_limited;
                    callbacks_judged = callbacks_judged || !audio_held;
                    if (hold_limited && *held > *w.held_limit) {
                        hold_over = true;
                        char text[96];
                        std::snprintf(text, sizeof text,
                                      "held the host's thread %.1f ms, over %.0f ms", ms(*held),
                                      ms(*w.held_limit));
                        over.emplace_back(text);
                    }
                    std::string callbacks;
                    if (!audio_held && c.late)
                        callbacks = std::to_string(c.late) +
                                    (c.late == 1 ? " late callback" : " late callbacks");
                    if (!audio_held && c.longest > period(w) / 2)
                        callbacks += (callbacks.empty() ? "" : ", ") +
                                     std::string("a callback took over half its period");
                    if (!callbacks.empty())
                        (busy ? noisy : over).push_back(callbacks);
                    callbacks_over = callbacks_over || (!callbacks.empty() && !busy);
                    std::string note;
                    for (const auto& v : over) {
                        failed.push_back(std::string(phase) + ": " + v);
                        note += (note.empty() ? "" : "; ") + v;
                    }
                    if (!note.empty())
                        note =
                            (o.enforce ? "OVER THE LIMIT: " : "over the limit (not enforced): ") +
                            note;
                    for (const auto& v : noisy) {
                        unqualified.push_back(std::string(phase) + ": " + v +
                                              " (the baseline was late too)");
                        note += (note.empty() ? "" : "; ") + std::string("not judged, ") + v +
                                " with the baseline late too";
                    }
                    return note;
                };
                driver.create(w.host, true);
                if (!driver.start())
                    throw std::runtime_error(format + ": the instance did not start");
                {
                    Renderer audio(driver, w);
                    const auto settle = Seconds(0.3); // Beyond any crossfade.
                    audio.wait_for(settle);
                    audio.take();
                    audio.wait_for(Seconds(0.25));
                    const auto idle = audio.take();
                    busy = idle.late != 0;
                    report(format, w, e.name, "baseline", std::nullopt, idle, idle.failed == 0,
                           busy ? "note: the machine was busy, so late callbacks in this run are "
                                  "not judged"
                                : "");
                    result.errors += idle.failed;
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
                        const auto seen = audio.take();
                        const bool ok = outcome.accepted && seen.failed == 0;
                        const char* phase = f == &x ? "first load" : "replacement";
                        report(format, w, e.name, phase, held, seen, ok,
                               judge(phase, held, seen, e.audio_waits));
                        result.errors += ok ? 0 : 1;
                    }
                    audio.before_next([&] { driver.reset(); });
                    audio.wait_for(settle);
                    const auto reset = audio.take();
                    report(format, w, e.name, "reset", std::nullopt, reset, reset.failed == 0,
                           judge("reset", std::nullopt, reset, o.reset_holds_audio));
                    result.errors += reset.failed;
                    // The filter that plays is the replacement, as saved.
                    const auto saved = driver.saved();
                    if (!saved.configuration || saved.configuration->paths.at(0).impulse !=
                                                    y.configuration.paths[0].impulse) {
                        std::cout << format << ": after " << e.name
                                  << " the instance does not save " << w.name << " filter y\n";
                        ++result.errors;
                    }
                }
                driver.stop();
                driver.destroy();
                const auto join = [](const std::vector<std::string>& v) {
                    std::string text;
                    for (const auto& s : v)
                        text += (text.empty() ? "" : "; ") + s;
                    return text;
                };
                // Each limit separately: the hold of the host's thread, then
                // the callbacks.
                r.hold = !hold_judged ? Verdict::reported_only
                         : hold_over  ? Verdict::failed
                                      : Verdict::passed;
                r.callbacks = !callbacks_judged      ? Verdict::reported_only
                              : callbacks_over       ? Verdict::failed
                              : !unqualified.empty() ? Verdict::not_qualified
                                                     : Verdict::passed;
                r.error = result.errors != errors_before;
                if (r.error) {
                    // A defect, not timing: judged in every build, never rerun.
                    r.verdict = Verdict::failed;
                    r.why = std::to_string(result.errors - errors_before) +
                            " errors (a refused load, a failed callback or the wrong filter "
                            "saved)";
                } else if (!o.enforce) {
                    r.verdict = r.hold = r.callbacks = Verdict::not_qualified;
                    r.why = "limits not enforced in this build";
                    if (!failed.empty())
                        r.why += "; would fail: " + join(failed);
                } else {
                    r.verdict = combine(r.hold, r.callbacks);
                    r.why = join(failed);
                    if (!unqualified.empty())
                        r.why += (r.why.empty() ? "" : "; ") + std::string("not judged: ") +
                                 join(unqualified);
                }
                result.runs.push_back(r);
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
        if (v == Verdict::passed && j - i > 1) {
            // Which run met each limit, as they can differ.
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
    // One limit across the runs: met if one run met it.
    const auto across = [&](Verdict Run::* limit) {
        const auto any = [&](Verdict v) {
            return std::any_of(first, last, [&](const Run& r) { return r.*limit == v; });
        };
        return any(Verdict::passed)          ? Verdict::passed
               : any(Verdict::failed)        ? Verdict::failed
               : any(Verdict::not_qualified) ? Verdict::not_qualified
                                             : Verdict::reported_only;
    };
    const auto v = combine(across(&Run::hold), across(&Run::callbacks));
    // Limits met in different runs pass only if one run broke neither: a
    // hold over the limit in one run and late callbacks after an on-time
    // baseline in the other is two failures, not a pass.
    const auto broke = [](const Run& r) {
        return r.hold == Verdict::failed || r.callbacks == Verdict::failed;
    };
    if (v == Verdict::passed && std::all_of(first, last, broke))
        return Verdict::failed;
    return v;
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
