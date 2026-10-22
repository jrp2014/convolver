// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace convolver {
namespace {
std::string count(std::size_t n, const char* noun) {
    return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
}
std::string counts(std::size_t inputs, std::size_t outputs) {
    return count(inputs, "input") + " and " + count(outputs, "output");
}
// Why a filter does not suit the host, or empty if it does.
std::string mismatch(const Configuration& c, const HostLayout& host) {
    if (c.sample_rate != host.sample_rate)
        return "The filter was made for " + std::to_string(c.sample_rate) +
               " Hz, but the host runs at " + std::to_string(host.sample_rate) +
               " Hz. Convolver does not resample: load a filter made for " +
               std::to_string(host.sample_rate) + " Hz, or set the host to " +
               std::to_string(c.sample_rate) + " Hz.";
    if (c.input_channels != host.inputs || c.output_channels != host.outputs)
        return "The filter has " + counts(c.input_channels, c.output_channels) +
               ", but the plug-in is set up with " + counts(host.inputs, host.outputs) +
               ". Give the plug-in " + counts(c.input_channels, c.output_channels) +
               " in the host's track or routing settings, or load a filter made for " +
               counts(host.inputs, host.outputs) + ".";
    return {};
}
} // namespace

Configuration unfiltered_configuration(const HostLayout& host, bool failed) {
    Configuration c;
    c.sample_rate = host.sample_rate;
    c.input_channels = host.inputs;
    c.output_channels = host.outputs;
    if (failed || host.inputs != host.outputs) {
        // One all-zero response: exact silence on every output, and no tail.
        c.paths.push_back({{0.f}, {{0, 1}}, {{0, 1}}});
        return c;
    }
    for (std::size_t i = 0; i < host.inputs; ++i)
        c.paths.push_back({{1.f}, {{i, 1}}, {{i, 1}}});
    return c;
}

void HostFilter::load(std::optional<Configuration> next, const std::optional<HostLayout>& host,
                      const std::function<void(const Configuration&)>& install) {
    try {
        if (next && (!next->input_channels || !next->output_channels ||
                     next->input_channels > max_channels_ || next->output_channels > max_channels_))
            throw std::invalid_argument("The filter has " +
                                        counts(next->input_channels, next->output_channels) +
                                        "; this plug-in supports 1 to " +
                                        std::to_string(max_channels_) + " channels on each side.");
        if (host) {
            const auto unfiltered = next ? Configuration{} : unfiltered_configuration(*host, false);
            if (next) {
                if (auto why = mismatch(*next, *host); !why.empty())
                    throw std::invalid_argument(why);
            }
            if (install)
                install(next ? *next : unfiltered);
        }
    } catch (const std::exception& e) {
        reject(e.what());
        throw std::invalid_argument(error_);
    } catch (...) {
        reject("The filter could not be prepared.");
        throw std::invalid_argument(error_);
    }
    filter_ = std::move(next);
    failed_ = false;
    error_.clear();
}

void HostFilter::reject(std::string why) {
    failed_ = !filter_;
    error_ = (filter_ ? "Filter not loaded; the previous filter keeps playing. "
                      : "Filter not loaded, so Convolver plays silence. ") +
             why;
}

Configuration HostFilter::activate(const HostLayout& host) {
    if (!filter_) {
        if (!failed_)
            error_.clear();
        return unfiltered_configuration(host, failed_);
    }
    auto why = mismatch(*filter_, host);
    if (why.empty()) {
        error_.clear();
        return *filter_;
    }
    error_ = "The saved filter does not suit the host, so Convolver plays silence until it "
             "does. " +
             why;
    return unfiltered_configuration(host, true);
}

struct HostProcessor::Impl {
    struct Prepared {
        Engine engine;
        Prepared* next_retired = nullptr;
        explicit Prepared(const Configuration& c, std::size_t block) : engine(c, block) {}
    };
    static_assert(std::atomic<Prepared*>::is_always_lock_free);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    const int rate;
    const std::size_t inputs, outputs, maximum, block;
    Prepared* current = nullptr;
    Prepared* previous = nullptr;
    std::atomic<Prepared*> pending{nullptr}, retired{nullptr};
    std::atomic<std::size_t> tail{0};
    std::vector<float> input, wet, old_wet, dry;
    std::size_t dry_position = 0, warm = 0, fade = 0;
    float dry_mix = 0;
    bool bypass = false;
    bool started = false; // Audio processed since construction or reset.
    Impl(const Configuration& c, std::size_t maximum_, std::size_t block_)
        : rate(c.sample_rate), inputs(c.input_channels), outputs(c.output_channels),
          maximum(maximum_), block(block_) {
        if (!inputs || !outputs || inputs > 256 || outputs > 256 || !maximum ||
            maximum > (1U << 20) || std::max(inputs, outputs) * maximum > (1U << 24))
            throw std::invalid_argument("invalid host channel count or callback capacity");
        auto prepared = std::make_unique<Prepared>(c, block);
        input.resize(inputs * maximum);
        wet.resize(outputs * maximum);
        old_wet.resize(wet.size());
        dry.resize(outputs * block);
        tail.store(prepared->engine.tail_frames());
        current = prepared.release();
    }
    ~Impl() {
        delete current;
        delete previous;
        delete pending.load();
        collect();
    }
    void collect() noexcept {
        auto* item = retired.exchange(nullptr, std::memory_order_acq_rel);
        while (item) {
            auto* next = item->next_retired;
            delete item;
            item = next;
        }
    }
    void replace(const Configuration& c) {
        if (c.sample_rate != rate || c.input_channels != inputs || c.output_channels != outputs)
            throw std::invalid_argument("replacement must preserve sample rate and channels");
        auto prepared = std::make_unique<Prepared>(c, block);
        collect();
        // Conservative tail never shrinks during a session, including transitions.
        tail.store(std::max(tail.load(), prepared->engine.tail_frames()));
        delete pending.exchange(prepared.release(), std::memory_order_acq_rel);
    }
    // The audio producer only links; the control consumer owns deletion.
    // Collection must never hold up adoption of another pending filter.
    void retire(Prepared* item) noexcept {
        auto* head = retired.load(std::memory_order_relaxed);
        do {
            item->next_retired = head;
        } while (!retired.compare_exchange_strong(head, item, std::memory_order_release,
                                                  std::memory_order_relaxed));
    }
    void reset() noexcept {
        started = false;
        current->engine.reset();
        if (previous)
            previous->engine.reset();
        std::fill(dry.begin(), dry.end(), 0.f);
        dry_position = 0;
        warm = previous ? block : 0;
        fade = 0;
        dry_mix = bypass ? 1.f : 0.f;
    }
    template <class T>
    bool process(std::span<const T* const> in, std::span<T* const> out,
                 std::size_t frames) noexcept {
        if (frames > maximum || in.size() > inputs || out.size() > outputs)
            return false;
        if (!frames)
            return true;
        if (!previous) {
            if (auto next = pending.exchange(nullptr, std::memory_order_acq_rel)) {
                if (started) {
                    previous = current;
                    warm = block;
                    fade = 0;
                } else {
                    // Nothing has been heard yet (initial restoration): install
                    // directly, so output follows the filter from the first frame.
                    retire(current);
                }
                current = next;
            }
        }
        started = true;
        for (std::size_t t = 0; t < frames; ++t)
            for (std::size_t c = 0; c < inputs; ++c) {
                double v = c < in.size() && in[c] ? in[c][t] : 0;
                input[t * inputs + c] =
                    std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max()
                        ? static_cast<float>(v)
                        : 0.f;
            }
        // Prepared, valid nonoverlapping spans and finite input satisfy Engine's
        // checked entry point; defensive containment keeps exceptions inside host.
        try {
            current->engine.process(input, wet, frames);
            if (previous)
                previous->engine.process(input, old_wet, frames);
        } catch (...) {
            reset();
            for (auto channel : out)
                if (channel)
                    std::fill_n(channel, frames, T(0));
            return false;
        }
        bool invalid_output = false;
        for (std::size_t t = 0; t < frames; ++t) {
            float blend = 1;
            if (previous) {
                if (warm) {
                    --warm;
                    blend = 0;
                } else {
                    fade = std::min<std::size_t>(256, fade + 1);
                    blend = static_cast<float>(fade) / 256;
                }
            }
            dry_mix = std::clamp(dry_mix + (bypass ? 1.f / 64 : -1.f / 64), 0.f, 1.f);
            for (std::size_t c = 0; c < outputs; ++c) {
                // Bypass passes input c to output c; outputs without an input are silent.
                auto index = t * outputs + c;
                auto& delayed = dry[dry_position * outputs + c];
                float dry_value = delayed;
                delayed = c < inputs ? input[t * inputs + c] : 0.f;
                double value = wet[index];
                if (previous)
                    value = (1.0 - blend) * old_wet[index] + blend * value;
                value = (1.0 - dry_mix) * value + dry_mix * dry_value;
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
                    value = 0;
                    invalid_output = true;
                }
                if (c < out.size() && out[c])
                    out[c][t] = static_cast<T>(value);
            }
            if (++dry_position == block)
                dry_position = 0;
        }
        if (invalid_output)
            reset();
        if (previous && !warm && fade == 256) {
            retire(previous);
            previous = nullptr;
        }
        return true;
    }
};
HostProcessor::HostProcessor(const Configuration& c, std::size_t maximum, std::size_t block)
    : impl_(std::make_unique<Impl>(c, maximum, block)) {}
HostProcessor::~HostProcessor() = default;
void HostProcessor::replace(const Configuration& c) {
    impl_->replace(c);
}
void HostProcessor::collect() noexcept {
    impl_->collect();
}
void HostProcessor::reset() noexcept {
    impl_->reset();
}
void HostProcessor::set_bypass(bool value) noexcept {
    impl_->bypass = value;
}
std::size_t HostProcessor::latency_frames() const noexcept {
    return impl_->block;
}
std::size_t HostProcessor::tail_frames() const noexcept {
    return impl_->tail.load();
}
bool HostProcessor::process(std::span<const float* const> in, std::span<float* const> out,
                            std::size_t n) noexcept {
    return impl_->process(in, out, n);
}
bool HostProcessor::process(std::span<const double* const> in, std::span<double* const> out,
                            std::size_t n) noexcept {
    return impl_->process(in, out, n);
}
} // namespace convolver
