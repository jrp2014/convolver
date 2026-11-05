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

const char* build() {
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    return "sanitizers: timings not representative";
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
    return "sanitizers: timings not representative";
#elif defined(NDEBUG)
    return "optimised";
#else
    return "debug: timings not representative";
#endif
#elif defined(NDEBUG)
    return "optimised";
#else
    return "debug: timings not representative";
#endif
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
            for (std::size_t t = 0; t < block_; ++t) // Quiet noise, as a host plays.
                in_[c][t] = static_cast<float>(((t * 7919 + c * 104729) % 2001) - 1000) * 1e-4f;
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
            const char* phase, std::optional<Seconds> held, const Callbacks& c, bool ok) {
    char time[32] = "-", line[256];
    if (held)
        std::snprintf(time, sizeof time, "%.1f", ms(*held));
    std::snprintf(line, sizeof line, "%-5s %-5s %-14s %-12s %10s %9zu %6zu %10.2f %10.2f%s\n",
                  format.c_str(), w.name.c_str(), entry.c_str(), phase, time, c.count, c.late,
                  ms(c.worst_late), ms(c.longest), ok ? "" : "  REFUSED OR FAILED");
    std::cout << line;
    if (!c.error.empty())
        std::cout << "      a callback threw: " << c.error << '\n';
    std::cout << std::flush;
}
} // namespace

std::vector<Workload> workloads(const Options& o) {
    std::vector<Workload> list;
    for (const double s : {1.0, 10.0, 30.0})
        list.push_back({std::to_string(static_cast<int>(s)) + " s", {48000, 2, 2}, s, 512});
    if (o.long_filters)
        list.push_back({"120 s", {48000, 2, 2}, 120, 512});
    return list;
}

std::size_t run(lifecycle::Driver& driver, const std::vector<Entry>& entries, const Options& o) {
    const auto format = driver.format().name;
    std::size_t errors = 0;
    std::cout << format << " load timing (" << build() << "; measurement only, no limits set yet)\n"
              << "held: ms the calling thread was held; late: callbacks that finished after the "
                 "next was due, worst by how many ms; longest: ms of the longest callback\n";
    char header[256];
    std::snprintf(header, sizeof header, "%-5s %-5s %-14s %-12s %10s %9s %6s %10s %10s\n", "",
                  "filt.", "entry", "phase", "held ms", "callbacks", "late", "worst ms",
                  "longest ms");
    std::cout << header;
    fs::create_directories(o.work_directory);
    for (const auto& w : workloads(o)) {
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
        for (const auto& e : entries) {
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
                report(format, w, e.name, "baseline", std::nullopt, idle, idle.failed == 0);
                errors += idle.failed;
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
                    report(format, w, e.name, f == &x ? "first load" : "replacement", held, seen,
                           ok);
                    errors += ok ? 0 : 1;
                }
                audio.before_next([&] { driver.reset(); });
                audio.wait_for(settle);
                const auto reset = audio.take();
                report(format, w, e.name, "reset", std::nullopt, reset, reset.failed == 0);
                errors += reset.failed;
                // The filter that plays is the replacement, as saved.
                const auto saved = driver.saved();
                if (!saved.configuration ||
                    saved.configuration->paths.at(0).impulse != y.configuration.paths[0].impulse) {
                    std::cout << format << ": after " << e.name << " the instance does not save "
                              << w.name << " filter y\n";
                    ++errors;
                }
            }
            driver.stop();
            driver.destroy();
        }
        fs::remove(x.file);
        fs::remove(y.file);
        fs::remove(fs::path(x.file).replace_extension(".wav"));
        fs::remove(fs::path(y.file).replace_extension(".wav"));
    }
    std::cout << format << " load timing: " << errors << " errors\n";
    return errors;
}
} // namespace convolver::plugin_test::load_timing
