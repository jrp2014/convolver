// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace convolver {
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
    void reset() noexcept {
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
                previous = current;
                current = next;
                warm = block;
                fade = 0;
            }
        }
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
            // The audio producer only links; the control consumer owns deletion.
            // Collection must never hold up adoption of another pending filter.
            auto* head = retired.load(std::memory_order_relaxed);
            do {
                previous->next_retired = head;
            } while (!retired.compare_exchange_strong(head, previous, std::memory_order_release,
                                                      std::memory_order_relaxed));
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
