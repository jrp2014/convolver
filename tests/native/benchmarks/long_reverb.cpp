// SPDX-License-Identifier: GPL-2.0-or-later
// PERF-2 experiment (7 November 2026): ways of making long reverbs cheaper,
// including zero latency, measured beside today's uniform engine and
// KlangFalter's two-stage engine. Nothing here is used by the product.
//
// Candidates, all single input to single output per path:
//   engine       convolver::Engine: uniform partitions of the Latency block B,
//                latency B (today's product).
//   nonuniform   Partitions that grow from a small first block to a largest
//                block (Gardner 1995; Garcia 2002), built from the same FFT
//                and multiply-accumulate kernels as the engine. With latency 0
//                a direct time-domain head covers the first block of taps, so
//                the output is not delayed at all. Each larger stage has a
//                whole block period of slack before its output is needed, and
//                the schedule decides how that slack is used:
//                  immediate  all of a stage's work at the block boundary
//                             (simple, but the largest blocks make spikes);
//                  spread     the stage's work divided by estimated cost over
//                             the boundaries of its slack, on the calling
//                             thread: no thread, so correct in realtime,
//                             prefetch and offline processing alike;
//                  worker     the multiply-accumulates and inverse transform
//                             of larger stages on a worker thread; when a
//                             result is not ready at its deadline, the calling
//                             thread computes it itself and never waits.
//   kf, kf-thread  KlangFalter's TwoStageFFTConvolver (MIT, from
//                HiFi-LoFi/FFTConvolver), head block = callback rounded up to a
//                power of two and tail block max(8192, 2 * head), as
//                KlangFalter's Processor chooses; kf runs the tail inline,
//                kf-thread on a thread with KlangFalter's blocking wait. Built
//                only when CONVOLVER_FFTCONVOLVER_DIR names a checkout (see
//                run_long_reverb.sh).
//
// --check compares every candidate with the direct double-precision oracle
// at irregular callback sizes; the measurement modes time each callback.
#include "convolver/engine.hpp"
#include "denormals.hpp"
#include "fft.hpp"
#include "kernels.hpp"
#include "oracle.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <random>
#include <semaphore>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(CONVOLVER_WITH_FFTCONVOLVER)
#include "TwoStageFFTConvolver.h"
#endif

namespace {
using convolver::detail::RealFft;
using Clock = std::chrono::steady_clock;

std::size_t next_power_of_two(std::size_t n) {
    std::size_t p = 1;
    while (p < n)
        p *= 2;
    return p;
}

// Mono convolver interface for the benchmark and the check.
class Mono {
  public:
    virtual ~Mono() = default;
    virtual void process(const float* in, float* out, std::size_t n) = 0;
    virtual std::size_t latency() const = 0;
    virtual std::string describe() const = 0;
    virtual std::string counters() const {
        return {};
    }
};

// --- Non-uniform partitioned convolution ---------------------------------
enum class Schedule { immediate, spread, worker };
struct NonUniformOptions {
    std::size_t first = 64;     // smallest FFT block N0 (power of two)
    std::size_t largest = 8192; // largest FFT block (power of two)
    std::size_t ratio = 2;      // block growth between stages (2 or 4)
    std::size_t latency = 0;    // 0, or a power of two >= first
    Schedule schedule = Schedule::spread;
};

class NonUniform final : public Mono {
    struct Stage {
        std::size_t n = 0, bins = 0, partitions = 0, offset = 0; // offset in output time
        std::size_t ring = 0;                                    // history slots
        std::vector<float> filters, history;                     // split spectra, 2 * bins per slot
        std::vector<float> input;                                // the current block's input
        std::unique_ptr<RealFft> fft;                            // audio thread's transform
        std::vector<float> re, im;                               // audio thread's accumulator
        // Spread schedule: progress through the current job.
        bool active = false;
        std::size_t job = 0, boundaries = 0, boundary = 0, unit = 0;
        double done = 0, total = 0, w_fft = 0, w_mac = 0;
        // Worker schedule.
        std::unique_ptr<RealFft> worker_fft;
        std::vector<float> worker_re, worker_im, worker_out;
        std::atomic<std::uint64_t> requested{0}, claimed{0}, finished{0};
        std::atomic<bool> cancel{false};
        std::size_t worker_slot = 0;
        std::uint64_t pending = 0; // job number awaiting its deadline (0: none)
        std::size_t deadline = 0;  // output time by which it must be committed
    };

  public:
    NonUniform(const std::vector<float>& h, NonUniformOptions o)
        : o_(o), k_(convolver::detail::kernels()) {
        if (o.first == 0 || (o.first & (o.first - 1)) || (o.largest & (o.largest - 1)) ||
            o.largest < o.first || (o.ratio != 2 && o.ratio != 4) ||
            (o.latency != 0 && (o.latency < o.first || (o.latency & (o.latency - 1)))))
            throw std::invalid_argument("bad non-uniform options");
        const convolver::detail::ScopedFlushDenormals flush;
        layout(h);
        if (o_.schedule == Schedule::worker && stages_.size() > 1)
            worker_ = std::thread([this] { work(); });
    }
    ~NonUniform() override {
        if (worker_.joinable()) {
            stop_ = true;
            wake_.release();
            worker_.join();
        }
    }
    std::size_t latency() const override {
        return o_.latency;
    }
    std::string describe() const override {
        std::string s = "head " + std::to_string(head_.size()) + ";";
        for (const auto& st : stages_)
            s += " " + std::to_string(st.n) + "x" + std::to_string(st.partitions) + "@" +
                 std::to_string(st.offset);
        return s;
    }
    std::string counters() const override {
        return "worker jobs " + std::to_string(by_worker_) + ", inline fallbacks " +
               std::to_string(by_fallback_);
    }

    void process(const float* in, float* out, std::size_t n) override {
        const convolver::detail::ScopedFlushDenormals flush;
        const std::size_t n0 = o_.first;
        while (n > 0) {
            const std::size_t chunk = std::min(n, n0 - time_ % n0);
            // Input into each stage's block buffer and the head's history.
            for (auto& st : stages_)
                std::memcpy(st.input.data() + time_ % st.n, in, chunk * sizeof(float));
            const std::size_t d = head_.size();
            if (d > 0) {
                std::memcpy(head_history_.data() + d - 1, in, chunk * sizeof(float));
                std::fill(out, out + chunk, 0.0f);
                // y[t] = sum_k h[k] x[t - k], vectorised over t for each tap.
                for (std::size_t k = 0; k < d; ++k) {
                    const float hk = head_[k];
                    const float* x = head_history_.data() + d - 1 - k;
                    for (std::size_t t = 0; t < chunk; ++t)
                        out[t] += hk * x[t];
                }
                std::memmove(head_history_.data(), head_history_.data() + chunk,
                             (d - 1) * sizeof(float));
            } else {
                std::fill(out, out + chunk, 0.0f);
            }
            for (std::size_t t = 0; t < chunk; ++t) {
                float& r = ring_[(time_ + t) & mask_];
                out[t] += r;
                r = 0;
            }
            time_ += chunk;
            in += chunk;
            out += chunk;
            n -= chunk;
            if (time_ % n0 == 0)
                boundary();
        }
    }

  private:
    void layout(const std::vector<float>& h) {
        const std::size_t taps = h.size();
        const std::size_t n0 = o_.first;
        // Output time t of filter tap i is i + latency. The direct head covers
        // the taps whose output time is below the first block.
        const std::size_t start = std::max(o_.latency, n0);
        const std::size_t head_taps = std::min(taps, start - o_.latency);
        head_.assign(h.begin(), h.begin() + static_cast<std::ptrdiff_t>(head_taps));
        if (!head_.empty())
            head_history_.assign(head_.size() - 1 + n0, 0.0f);
        std::size_t time = start; // output time of the next uncovered tap
        std::size_t n = n0;
        std::size_t lookahead = 0;
        while (time < taps + o_.latency) {
            // A stage of block m needs its first output time at or after
            // 2m (m for the first stage, which works at the boundary itself).
            const auto grow = [&](std::size_t m) { return std::min(m * o_.ratio, o_.largest); };
            while (n < o_.largest && time >= 2 * grow(n))
                n = grow(n);
            std::size_t end = taps + o_.latency;
            if (n < o_.largest)
                end = std::min(end, 2 * grow(n));
            const std::size_t partitions = (end - time + n - 1) / n;
            add_stage(h, n, partitions, time);
            lookahead = std::max(lookahead, time + 2 * n);
            time += partitions * n;
            n = grow(n);
        }
        const std::size_t ring = next_power_of_two(lookahead + 2 * n0);
        ring_.assign(ring, 0.0f);
        mask_ = ring - 1;
    }

    void add_stage(const std::vector<float>& h, std::size_t n, std::size_t partitions,
                   std::size_t offset) {
        Stage& st = stages_.emplace_back();
        st.n = n;
        st.bins = n + 1;
        st.partitions = partitions;
        st.offset = offset;
        st.ring = partitions + 2; // two spare slots: see the worker schedule
        const std::size_t stride = 2 * st.bins;
        st.filters.assign(partitions * stride, 0.0f);
        st.history.assign(st.ring * stride, 0.0f);
        st.input.assign(n, 0.0f);
        st.fft = std::make_unique<RealFft>(2 * n);
        st.re.assign(st.bins, 0.0f);
        st.im.assign(st.bins, 0.0f);
        const float scale = 1.0f / static_cast<float>(2 * n);
        for (std::size_t p = 0; p < partitions; ++p) {
            float* time = st.fft->time();
            std::fill(time, time + 2 * n, 0.0f);
            for (std::size_t i = 0; i < n; ++i) {
                const std::size_t out_time = offset + p * n + i;
                if (out_time >= o_.latency && out_time - o_.latency < h.size())
                    time[i] = h[out_time - o_.latency];
            }
            st.fft->forward();
            k_.deinterleave(st.fft->frequency(), st.filters.data() + p * stride, st.bins, scale);
        }
        // Rough cost weights in floating-point operations: a real transform of
        // 2n is about 5 n log2(2n); a partition's complex multiply-add is 8 per bin.
        st.w_fft = 5.0 * static_cast<double>(n) * std::log2(2.0 * static_cast<double>(n));
        st.w_mac = 8.0 * static_cast<double>(st.bins);
        st.total = 2 * st.w_fft + static_cast<double>(partitions) * st.w_mac;
        if (o_.schedule == Schedule::worker && stages_.size() > 1) {
            st.worker_fft = std::make_unique<RealFft>(2 * n);
            st.worker_re.assign(st.bins, 0.0f);
            st.worker_im.assign(st.bins, 0.0f);
            st.worker_out.assign(2 * n, 0.0f);
        }
    }

    // The completed block into the transform, before the next block's input
    // overwrites it.
    static void load(Stage& st) {
        float* time = st.fft->time();
        std::memcpy(time, st.input.data(), st.n * sizeof(float));
        std::fill(time + st.n, time + 2 * st.n, 0.0f);
    }
    // Unit 0: forward transform of the loaded block `job` into its history slot.
    void forward(Stage& st, std::size_t job) {
        st.fft->forward();
        const std::size_t stride = 2 * st.bins;
        k_.deinterleave(st.fft->frequency(), st.history.data() + (job % st.ring) * stride, st.bins,
                        1.0f);
    }
    void mac(const Stage& st, std::size_t job, std::size_t p, float* re, float* im) const {
        const std::size_t stride = 2 * st.bins;
        const float* x = st.history.data() + ((job + st.ring - p) % st.ring) * stride;
        if (p == 0)
            k_.multiply(re, im, x, st.filters.data(), st.bins);
        else
            k_.multiply_accumulate(re, im, x, st.filters.data() + p * stride, st.bins);
    }
    // Inverse transform of the accumulated spectrum into `time` (2n samples).
    void inverse(RealFft& fft, const Stage& st, const float* re, const float* im) const {
        k_.interleave(re, im, fft.sum(), st.bins);
        fft.inverse();
    }
    void commit(const Stage& st, std::size_t job, const float* samples) {
        // Block `job` started at input time job * n; its output starts offset later.
        const std::size_t at = job * st.n + st.offset;
        for (std::size_t i = 0; i < 2 * st.n; ++i)
            ring_[(at + i) & mask_] += samples[i];
    }
    void run_whole(Stage& st, std::size_t job) {
        load(st);
        forward(st, job);
        for (std::size_t p = 0; p < st.partitions; ++p)
            mac(st, job, p, st.re.data(), st.im.data());
        inverse(*st.fft, st, st.re.data(), st.im.data());
        commit(st, job, st.fft->time());
    }
    // Spread schedule: do the units whose cumulative cost fits the share of
    // the job due by this boundary; the last boundary finishes the job.
    void advance(Stage& st) {
        ++st.boundary;
        const bool last = st.boundary == st.boundaries;
        const double target =
            st.total * static_cast<double>(st.boundary) / static_cast<double>(st.boundaries);
        const std::size_t units = st.partitions + 2;
        while (st.unit < units) {
            const double w = st.unit == 0 || st.unit == units - 1 ? st.w_fft : st.w_mac;
            if (!last && st.done + w > target)
                break;
            if (st.unit == 0)
                forward(st, st.job);
            else if (st.unit < units - 1)
                mac(st, st.job, st.unit - 1, st.re.data(), st.im.data());
            else {
                inverse(*st.fft, st, st.re.data(), st.im.data());
                commit(st, st.job, st.fft->time());
            }
            st.done += w;
            ++st.unit;
        }
        if (st.unit == units)
            st.active = false;
    }

    void boundary() {
        const std::size_t n0 = o_.first;
        for (std::size_t s = 0; s < stages_.size(); ++s) {
            Stage& st = stages_[s];
            // Commit a worker job whose deadline is this boundary.
            if (st.pending != 0 && time_ == st.deadline)
                settle(st);
            if (time_ % st.n != 0)
                continue;
            const std::size_t job = time_ / st.n - 1; // the block that just completed
            if (s == 0 || o_.schedule == Schedule::immediate) {
                run_whole(st, job);
            } else if (o_.schedule == Schedule::spread) {
                load(st);
                st.active = true;
                st.job = job;
                st.boundaries = st.n / n0;
                st.boundary = 0;
                st.unit = 0;
                st.done = 0;
            } else {
                // Worker: the forward transform here, the rest on the worker.
                load(st);
                forward(st, job);
                st.pending = job + 1;
                st.deadline = time_ + st.n - n0;
                st.cancel.store(false, std::memory_order_relaxed);
                st.requested.store(job + 1, std::memory_order_release);
                wake_.release();
            }
        }
        if (o_.schedule == Schedule::spread)
            for (std::size_t s = 1; s < stages_.size(); ++s)
                if (stages_[s].active)
                    advance(stages_[s]);
    }

    // At the deadline: take the worker's result, or compute it here.
    void settle(Stage& st) {
        const std::uint64_t id = st.pending;
        st.pending = 0;
        std::uint64_t expected = id - 1;
        if (st.claimed.compare_exchange_strong(expected, id, std::memory_order_acq_rel)) {
            ++by_fallback_; // the worker never started it
        } else if (st.finished.load(std::memory_order_acquire) == id) {
            ++by_worker_;
            commit(st, id - 1, st.worker_out.data());
            return;
        } else {
            st.cancel.store(true, std::memory_order_relaxed); // the worker is late
            ++by_fallback_;
        }
        for (std::size_t p = 0; p < st.partitions; ++p)
            mac(st, id - 1, p, st.re.data(), st.im.data());
        inverse(*st.fft, st, st.re.data(), st.im.data());
        commit(st, id - 1, st.fft->time());
    }

    void work() {
        const convolver::detail::ScopedFlushDenormals flush;
        while (!stop_) {
            wake_.acquire();
            bool any = true;
            while (any && !stop_) {
                any = false;
                // Earliest deadline first: smaller stages have nearer deadlines.
                for (std::size_t s = 1; s < stages_.size(); ++s) {
                    Stage& st = stages_[s];
                    const std::uint64_t id = st.requested.load(std::memory_order_acquire);
                    std::uint64_t expected = id - 1;
                    if (id == 0 || !st.claimed.compare_exchange_strong(expected, id,
                                                                       std::memory_order_acq_rel))
                        continue;
                    any = true;
                    bool cancelled = false;
                    for (std::size_t p = 0; p < st.partitions; ++p) {
                        if (st.cancel.load(std::memory_order_relaxed)) {
                            cancelled = true;
                            break;
                        }
                        mac(st, id - 1, p, st.worker_re.data(), st.worker_im.data());
                    }
                    if (cancelled)
                        continue;
                    inverse(*st.worker_fft, st, st.worker_re.data(), st.worker_im.data());
                    std::memcpy(st.worker_out.data(), st.worker_fft->time(),
                                2 * st.n * sizeof(float));
                    st.finished.store(id, std::memory_order_release);
                    break; // rescan from the smallest stage
                }
            }
        }
    }

    NonUniformOptions o_;
    const convolver::detail::Kernels& k_;
    std::vector<float> head_, head_history_, ring_;
    std::size_t mask_ = 0, time_ = 0;
    std::deque<Stage> stages_; // Stages hold atomics, so they never move.
    std::thread worker_;
    std::counting_semaphore<1 << 20> wake_{0};
    std::atomic<bool> stop_{false};
    std::size_t by_worker_ = 0, by_fallback_ = 0;
};

// --- Today's engine -------------------------------------------------------
class EngineMono final : public Mono {
  public:
    EngineMono(const std::vector<float>& h, std::size_t block, int rate) : block_(block) {
        convolver::Configuration c;
        c.sample_rate = rate;
        c.input_channels = c.output_channels = 1;
        c.paths.push_back({h, {{0, 1}}, {{0, 1}}});
        engine_ = std::make_unique<convolver::Engine>(c, block);
    }
    void process(const float* in, float* out, std::size_t n) override {
        engine_->process({in, n}, {out, n}, n);
    }
    std::size_t latency() const override {
        return engine_->latency_frames();
    }
    std::string describe() const override {
        return "uniform " + std::to_string(block_);
    }

  private:
    std::size_t block_;
    std::unique_ptr<convolver::Engine> engine_;
};

#if defined(CONVOLVER_WITH_FFTCONVOLVER)
// --- KlangFalter's engine -------------------------------------------------
class KfThreaded final : public fftconvolver::TwoStageFFTConvolver {
  public:
    KfThreaded() : thread_([this] { run(); }) {}
    ~KfThreaded() override {
        {
            std::lock_guard lock(m_);
            stop_ = true;
        }
        cv_.notify_all();
        thread_.join();
    }

  protected:
    // KlangFalter's Convolver: notify the thread, and block until it is done.
    void startBackgroundProcessing() override {
        {
            std::lock_guard lock(m_);
            busy_ = true;
        }
        cv_.notify_all();
    }
    void waitForBackgroundProcessing() override {
        std::unique_lock lock(m_);
        cv_.wait(lock, [this] { return !busy_; });
    }

  private:
    void run() {
        const convolver::detail::ScopedFlushDenormals flush;
        std::unique_lock lock(m_);
        for (;;) {
            cv_.wait(lock, [this] { return busy_ || stop_; });
            if (stop_)
                return;
            lock.unlock();
            doBackgroundProcessing();
            lock.lock();
            busy_ = false;
            cv_.notify_all();
        }
    }
    std::mutex m_;
    std::condition_variable cv_;
    bool busy_ = false, stop_ = false;
    std::thread thread_;
};

class KfMono final : public Mono {
  public:
    KfMono(const std::vector<float>& h, std::size_t callback, bool threaded) : threaded_(threaded) {
        head_ = next_power_of_two(callback);
        tail_ = std::max<std::size_t>(8192, 2 * head_);
        if (threaded)
            conv_ = std::make_unique<KfThreaded>();
        else
            conv_ = std::make_unique<fftconvolver::TwoStageFFTConvolver>();
        if (!conv_->init(head_, tail_, h.data(), h.size()))
            throw std::runtime_error("FFTConvolver init failed");
    }
    void process(const float* in, float* out, std::size_t n) override {
        const convolver::detail::ScopedFlushDenormals flush;
        conv_->process(in, out, n);
    }
    std::size_t latency() const override {
        return 0;
    }
    std::string describe() const override {
        return std::string(threaded_ ? "KlangFalter threaded" : "KlangFalter inline") + " head " +
               std::to_string(head_) + " tail " + std::to_string(tail_);
    }

  private:
    bool threaded_;
    std::size_t head_ = 0, tail_ = 0;
    std::unique_ptr<fftconvolver::TwoStageFFTConvolver> conv_;
};
#endif

// --- Command line ---------------------------------------------------------
struct Options {
    std::string mode = "measure", candidate = "nonuniform", schedule = "spread";
    double ir_seconds = 30, seconds = 10;
    int rate = 48000;
    std::size_t paths = 2, callback = 256, block = 256, first = 64, largest = 8192, ratio = 2,
                latency = 0;
    bool paced = false, warm = true;
};

const char* usage =
    "usage: convolver_long_reverb_benchmark [--mode measure|check]\n"
    "  [--candidate engine|nonuniform|kf|kf-thread] [--schedule immediate|spread|worker]\n"
    "  [--ir-seconds S] [--rate HZ] [--paths N] [--callback FRAMES] [--seconds S]\n"
    "  [--block B] (engine) [--first N0] [--largest N] [--ratio 2|4] [--latency L]\n"
    "  [--paced 0|1] (sleep to real time between callbacks) [--warm 0|1]\n"
    "measure: one CSV row of callback times for N independent paths (a stereo reverb is 2,\n"
    "true stereo 4). check: every candidate against the double-precision oracle.";

Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            throw std::invalid_argument(usage);
        const std::string key = argv[i], value = argv[i + 1];
        const auto size = [&] { return static_cast<std::size_t>(std::stoull(value)); };
        if (key == "--mode")
            o.mode = value;
        else if (key == "--candidate")
            o.candidate = value;
        else if (key == "--schedule")
            o.schedule = value;
        else if (key == "--ir-seconds")
            o.ir_seconds = std::stod(value);
        else if (key == "--seconds")
            o.seconds = std::stod(value);
        else if (key == "--rate")
            o.rate = std::stoi(value);
        else if (key == "--paths")
            o.paths = size();
        else if (key == "--callback")
            o.callback = size();
        else if (key == "--block")
            o.block = size();
        else if (key == "--first")
            o.first = size();
        else if (key == "--largest")
            o.largest = size();
        else if (key == "--ratio")
            o.ratio = size();
        else if (key == "--latency")
            o.latency = size();
        else if (key == "--paced")
            o.paced = value == "1";
        else if (key == "--warm")
            o.warm = value == "1";
        else
            throw std::invalid_argument(usage);
    }
    return o;
}

Schedule schedule_of(const std::string& s) {
    if (s == "immediate")
        return Schedule::immediate;
    if (s == "spread")
        return Schedule::spread;
    if (s == "worker")
        return Schedule::worker;
    throw std::invalid_argument(usage);
}

// A reverb-like response: noise decaying by 60 dB over the given length,
// after a unit direct sound.
std::vector<float> reverb(std::size_t taps, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0f, 1.0f);
    std::vector<float> h(taps);
    const double per_tap = std::log(1e-3) / static_cast<double>(taps);
    for (std::size_t i = 0; i < taps; ++i)
        h[i] = 0.3f * noise(rng) * static_cast<float>(std::exp(per_tap * static_cast<double>(i)));
    h[0] = 1.0f;
    return h;
}

std::unique_ptr<Mono> make(const Options& o, const std::string& candidate,
                           const std::vector<float>& h) {
    if (candidate == "engine")
        return std::make_unique<EngineMono>(h, o.block, o.rate);
    if (candidate == "nonuniform")
        return std::make_unique<NonUniform>(
            h, NonUniformOptions{o.first, o.largest, o.ratio, o.latency, schedule_of(o.schedule)});
#if defined(CONVOLVER_WITH_FFTCONVOLVER)
    if (candidate == "kf" || candidate == "kf-thread")
        return std::make_unique<KfMono>(h, o.callback, candidate == "kf-thread");
#endif
    throw std::invalid_argument("unknown or unavailable candidate " + candidate);
}

double percentile(std::vector<double> v, double q) {
    if (v.empty())
        return 0;
    const auto k = static_cast<std::size_t>(q * static_cast<double>(v.size() - 1));
    std::nth_element(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(k), v.end());
    return v[k];
}

int measure(const Options& o) {
    const auto taps = static_cast<std::size_t>(o.ir_seconds * o.rate);
    std::vector<std::unique_ptr<Mono>> paths;
    const auto prepare_start = Clock::now();
    for (std::size_t p = 0; p < o.paths; ++p)
        paths.push_back(make(o, o.candidate, reverb(taps, 1 + static_cast<std::uint32_t>(p))));
    const double prepare_ms =
        std::chrono::duration<double, std::milli>(Clock::now() - prepare_start).count();
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> uniform(-0.5f, 0.5f);
    std::vector<float> in(o.callback), out(o.callback);
    // The engine skips history known to be silent (PERF-5), so fill it first.
    if (o.warm && o.candidate == "engine")
        for (std::size_t done = 0; done < taps + o.block; done += o.callback)
            for (auto& p : paths) {
                for (auto& x : in)
                    x = uniform(rng);
                p->process(in.data(), out.data(), o.callback);
            }
    const auto callbacks =
        static_cast<std::size_t>(o.seconds * o.rate / static_cast<double>(o.callback));
    std::vector<double> times;
    times.reserve(callbacks);
    const auto period = std::chrono::duration<double>(static_cast<double>(o.callback) / o.rate);
    const auto start = Clock::now();
    const std::clock_t cpu_start = std::clock(); // all threads of the process
    for (std::size_t c = 0; c < callbacks; ++c) {
        for (auto& x : in)
            x = uniform(rng);
        const auto t0 = Clock::now();
        for (auto& p : paths)
            p->process(in.data(), out.data(), o.callback);
        times.push_back(std::chrono::duration<double, std::micro>(Clock::now() - t0).count());
        if (o.paced)
            std::this_thread::sleep_until(start + std::chrono::duration_cast<Clock::duration>(
                                                      period * static_cast<double>(c + 1)));
    }
    const double cpu_s = static_cast<double>(std::clock() - cpu_start) / CLOCKS_PER_SEC;
    const double audio_s = static_cast<double>(callbacks * o.callback) / o.rate;
    double sum = 0;
    for (double t : times)
        sum += t;
    const double mean = sum / static_cast<double>(times.size());
    const double period_us = 1e6 * static_cast<double>(o.callback) / o.rate;
    std::printf(
        "%s,%s,%.0f,%d,%zu,%zu,%zu,%s,%.1f,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.1f,\"%s\",\"%s\"\n",
        o.candidate.c_str(), o.candidate == "nonuniform" ? o.schedule.c_str() : "", o.ir_seconds,
        o.rate, o.paths, o.callback, paths[0]->latency(), o.paced ? "paced" : "unpaced", prepare_ms,
        mean, percentile(times, 0.5), percentile(times, 0.99), percentile(times, 0.999),
        *std::max_element(times.begin(), times.end()), 100.0 * mean / period_us,
        100.0 * cpu_s / audio_s, paths[0]->describe().c_str(), paths[0]->counters().c_str());
    return 0;
}

// Every candidate against the oracle, with irregular callback sizes.
int check(const Options& o) {
    struct Case {
        std::string name;
        Options options;
    };
    std::vector<Case> cases;
    for (const std::string schedule : {"immediate", "spread", "worker"})
        for (std::size_t latency : {std::size_t(0), std::size_t(32), std::size_t(128)})
            for (std::size_t ratio : {std::size_t(2), std::size_t(4)}) {
                Options c = o;
                c.candidate = "nonuniform";
                c.schedule = schedule;
                c.first = 32;
                c.largest = 1024;
                c.ratio = ratio;
                c.latency = latency;
                cases.push_back({"nonuniform " + schedule + " latency " + std::to_string(latency) +
                                     " ratio " + std::to_string(ratio),
                                 c});
            }
    {
        Options c = o;
        c.candidate = "engine";
        c.block = 64;
        cases.push_back({"engine 64", c});
    }
#if defined(CONVOLVER_WITH_FFTCONVOLVER)
    for (const std::string kf : {"kf", "kf-thread"}) {
        Options c = o;
        c.candidate = kf;
        c.callback = 64;
        cases.push_back({kf, c});
    }
#endif
    const std::size_t taps = 20000, frames = 40000;
    const auto h = reverb(taps, 3);
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> uniform(-0.5f, 0.5f);
    std::vector<float> input(frames);
    for (auto& x : input)
        x = uniform(rng);
    convolver::Configuration config;
    config.sample_rate = 48000;
    config.input_channels = config.output_channels = 1;
    config.paths.push_back({h, {{0, 1}}, {{0, 1}}});
    const auto expected = convolver::test::oracle(config, input, 0);
    double peak = 0;
    for (double y : expected)
        peak = std::max(peak, std::abs(y));
    int failures = 0;
    for (const auto& c : cases) {
        for (int paced = 0; paced <= (c.options.schedule == "worker" ? 1 : 0); ++paced) {
            auto conv = make(c.options, c.options.candidate, h);
            const std::size_t latency = conv->latency();
            const std::size_t total = frames + taps + latency;
            std::vector<float> in(total, 0.0f), out(total, 0.0f);
            std::copy(input.begin(), input.end(), in.begin());
            std::mt19937 sizes(5);
            std::uniform_int_distribution<std::size_t> size(1, 300);
            std::size_t pos = 0;
            while (pos < total) {
                const std::size_t n = std::min(total - pos, size(sizes));
                conv->process(in.data() + pos, out.data() + pos, n);
                if (paced)
                    std::this_thread::sleep_for(std::chrono::microseconds(20 * n));
                pos += n;
            }
            double worst = 0;
            for (std::size_t t = 0; t + latency < total && t < expected.size(); ++t)
                worst = std::max(worst, std::abs(out[t + latency] - expected[t]));
            const double relative = worst / peak;
            const bool ok = relative < 1e-5;
            failures += ok ? 0 : 1;
            std::printf("%-44s %s worst %.2e of peak (%.1f dB) %s %s\n", c.name.c_str(),
                        paced ? "paced  " : "unpaced", relative, 20 * std::log10(relative + 1e-30),
                        ok ? "ok" : "FAIL", conv->counters().c_str());
        }
    }
    return failures == 0 ? 0 : 1;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const Options o = parse(argc, argv);
        if (o.mode == "check")
            return check(o);
        if (o.mode == "header") {
            std::puts(
                "candidate,schedule,ir_seconds,rate,paths,callback,latency,pacing,prepare_ms,"
                "mean_us,p50_us,p99_us,p999_us,max_us,load_percent,cpu_percent,layout,counters");
            return 0;
        }
        return measure(o);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 2;
    }
}
