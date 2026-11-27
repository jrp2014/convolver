// SPDX-License-Identifier: GPL-2.0-or-later
// HostProcessor: the plug-ins' audio path (buffering, Gain, Mix, bypass,
// mute, filter replacement and fault counting), moved out of host.cpp
// unchanged (PLUG-24). HostFilter and the rest of the shared adapter
// contract stay in host.cpp.
#include "convolver/host.hpp"
#include "denormals.hpp"
#include "host_common.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace convolver {
using detail::clamp_gain_db;
using detail::clamp_mix;
using detail::gain_from_db;
using detail::own_dry;
// PLUG-17: HostProcessor's transitions last the same time at every sample
// rate. Gain, mix, bypass and mute ramp linearly for 64 frames' worth at
// 48 kHz (1.33 ms), and a replacement, after playing for the Latency block,
// fades in for 256 frames' worth (5.33 ms), so 48 kHz plays exactly as the
// fixed frame counts did (PLUG-16).
namespace {
constexpr int reference_rate = 48000;
constexpr std::size_t setting_ramp_at_reference = 64;
constexpr std::size_t crossfade_at_reference = 256;
// Frames at `rate` lasting as long as `frames` at the reference rate, to the
// nearest frame and at least 1.
constexpr std::size_t frames_at(int rate, std::size_t frames) noexcept {
    if (rate <= 0)
        return frames; // The engine refuses the rate anyway.
    const auto scaled = (static_cast<std::uint64_t>(rate) * frames + reference_rate / 2) /
                        static_cast<std::uint64_t>(reference_rate);
    return std::max<std::size_t>(1, static_cast<std::size_t>(scaled));
}
static_assert(frames_at(48000, 64) == 64 && frames_at(48000, 256) == 256);
static_assert(frames_at(44100, 64) == 59 && frames_at(96000, 256) == 512);
static_assert(frames_at(8000, 64) == 11 && frames_at(768000, 256) == 4096);
// PLUG-23: the engine for `c` reading `inputs` inputs: the inputs it does
// not have are added with no route from them, so they are read and ignored.
Engine fitted_engine(const Configuration& c, std::size_t block, std::size_t inputs) {
    if (c.input_channels >= inputs)
        return Engine(c, block);
    auto wider = c;
    wider.input_channels = inputs;
    if (!wider.input_delays.empty())
        wider.input_delays.resize(inputs, 0);
    return Engine(wider, block);
}
} // namespace
struct HostProcessor::Impl {
    struct Prepared {
        Engine engine;
        // Whether this filter has a dry signal (input c to output c), as
        // HostPlayback says: decided by HostFilter, never from the counts here.
        bool dry;
        // PLUG-10: the linear gain each GainMode plays for this filter (Set's
        // entry unused), as applied_gain_db() gives it; 0 (silence) where
        // there is none. Why there is none stays on the control side, in
        // FilterGains. Atomic, as update_gains() changes them while the
        // filter plays.
        std::array<std::atomic<float>, gain_mode_count> figure{1.f, 1.f, 1.f};
        Prepared* next_retired = nullptr;
        std::uint64_t number = 0; // Publications are numbered from 1, in order.
        Prepared(const Configuration& c, bool dry_, std::size_t block, std::size_t inputs,
                 const FilterGains& gains)
            : engine(fitted_engine(c, block, inputs)), dry(dry_) {
            set_gains(gains);
        }
        // Control thread; the audio thread reads them (relaxed: each figure
        // stands alone, and the ramp hides when it is seen).
        void set_gains(const FilterGains& gains) noexcept {
            const auto place = [&](GainMode mode, const std::optional<double>& db) {
                const auto i = static_cast<std::size_t>(mode);
                figure[i].store(db ? gain_from_db(*db) : 0.f, std::memory_order_relaxed);
            };
            // Estimated plays the bound where the estimate is absent.
            place(GainMode::estimate, gains.estimate_db ? gains.estimate_db : gains.no_clipping_db);
            place(GainMode::no_clipping, gains.no_clipping_db);
        }
    };
    static_assert(std::atomic<float>::is_always_lock_free);
    static_assert(std::atomic<Prepared*>::is_always_lock_free);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    const int rate;
    const std::size_t inputs, outputs, maximum, block;
    // PLUG-17: the ramps' and the crossfade's lengths at this rate.
    const std::size_t setting_ramp = frames_at(rate, setting_ramp_at_reference);
    const std::size_t crossfade = frames_at(rate, crossfade_at_reference);
    Prepared* current = nullptr;
    Prepared* previous = nullptr;
    // Control side: the filter published last (or built with). Only the
    // control side publishes, and a filter is retired only after a newer one
    // is published, so this one is alive for update_gains().
    Prepared* latest = nullptr;
    std::atomic<Prepared*> pending{nullptr}, retired{nullptr};
    std::atomic<std::size_t> tail{0};
    std::atomic<std::uint64_t> published{0}; // Publications so far (control writes).
    // DIAG-1: HostFaults, counted by the audio thread and read anywhere.
    std::atomic<std::uint64_t> rejected_input{0}, output_overflow{0}, processing_failure{0},
        invalid_call{0};
    // The fault total at the last clear_faults() (control writes), and at the
    // last fault_signalled() (audio thread writes).
    std::atomic<std::uint64_t> faults_cleared{0}, faults_signalled{0};
    std::atomic<bool> signal_held{false};
    std::uint64_t fault_total() const noexcept {
        return rejected_input.load(std::memory_order_relaxed) +
               output_overflow.load(std::memory_order_relaxed) +
               processing_failure.load(std::memory_order_relaxed) +
               invalid_call.load(std::memory_order_relaxed);
    }
    static void count(std::atomic<std::uint64_t>& n) noexcept {
        // One writer (the audio thread), so a load and a store suffice.
        n.store(n.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
    std::vector<float> input, wet, old_wet, dry;
    std::size_t dry_position = 0, warm = 0, fade = 0;
    // PLUG-10: the gain and mix the adapter set (audio thread), and the values
    // playing, which ramp to them over setting_ramp frames.
    double set_db = 0;
    // PLUG-10: through a replacement the filter fading out has its own gain
    // (previous_*), which ramps to its own target in the current mode as
    // `gain` does for the new one: a mode or Gain change during the
    // crossfade never makes either jump. In Manual mode both follow the same
    // targets from the same values, so they stay equal and the output is
    // computed as before the modes existed.
    std::size_t gain_ramp = 0, mix_ramp = 0, previous_gain_ramp = 0;
    float set_gain = 1, gain = 1, gain_target = 1, gain_step = 0;
    float previous_gain = 1, previous_gain_target = 1, previous_gain_step = 0;
    GainMode mode = GainMode::set; // PLUG-10: the gain mode (audio thread).
    // The gain `p` plays at in the current mode: the Gain setting, or the
    // filter's own figure (0, silence, when there is none).
    float target_gain(const Prepared& p) const noexcept {
        if (mode == GainMode::set)
            return set_gain;
        return p.figure[static_cast<std::size_t>(mode)].load(std::memory_order_relaxed);
    }
    float mix_setting = 1, mix = 1, mix_target = 1, mix_step = 0;
    // Bypass and mute ramp by whole frames, counted out of setting_ramp, so
    // they end exactly at 0 and 1 at every rate (PLUG-18): a float step
    // summed setting_ramp times can miss by about 1e-6.
    // The dry signal's share: setting_ramp under bypass, 0 otherwise.
    std::size_t dry_frames = 0;
    // mute(): the current filter's level ramps to 0; the level of the filter
    // being faded out keeps ramping if it was muted. Exactly setting_ramp
    // (level 1) otherwise, so an unmuted filter's output is unchanged.
    std::size_t level_frames = setting_ramp, previous_level_frames = setting_ramp;
    float share(std::size_t frames) const noexcept {
        return static_cast<float>(frames) / static_cast<float>(setting_ramp);
    }
    bool muted = false, previous_muted = false;
    // Publications up to this number were made before mute(): adopting one
    // keeps the silence. Only a filter published after it ends the silence.
    std::uint64_t muted_through = 0;
    // silence() (ROBUST-3): the publications to mute, plus one, for the
    // audio thread to apply at its next call; 0 when none is waiting.
    std::atomic<std::uint64_t> silence_through{0};
    bool bypass = false;
    bool started = false; // Audio processed since construction or reset.
    // The filter and its dry answer apart, so the Configuration overloads
    // need no copy of the filter in a HostPlayback.
    Impl(const Configuration& c, bool has_dry, std::size_t maximum_, std::size_t block_,
         const HostSettings& settings = {}, std::size_t inputs_ = 0, const FilterGains& gains = {})
        : rate(c.sample_rate), inputs(std::max(inputs_, c.input_channels)),
          outputs(c.output_channels), maximum(maximum_), block(block_) {
        if (!c.input_channels || !outputs || inputs > max_channel_count ||
            outputs > max_channel_count || !maximum || maximum > max_block_frames ||
            std::max(inputs, outputs) * maximum > (1U << 24) ||
            (inputs_ && inputs_ < c.input_channels) || (has_dry && outputs > inputs))
            throw std::invalid_argument("invalid host channel count or callback capacity");
        auto prepared = std::make_unique<Prepared>(c, has_dry, block, inputs, gains);
        input.resize(inputs * maximum);
        wet.resize(outputs * maximum);
        old_wet.resize(wet.size());
        dry.resize(outputs * block);
        tail.store(prepared->engine.tail_frames());
        current = latest = prepared.release();
        set_db = clamp_gain_db(settings.gain_db);
        set_gain = gain_from_db(set_db);
        mode = settings.gain_mode;
        gain = gain_target = target_gain(*current);
        mix_setting = static_cast<float>(clamp_mix(settings.mix));
        mix = mix_target = target_mix();
    }
    // Mix ramps as set; each filter applies it only if it has a dry signal
    // (process()), so a replacement that changes that never jumps.
    float target_mix() const noexcept {
        return mix_setting;
    }
    // Audio thread, at the start of a call: a new gain or mix starts a ramp.
    void retarget() noexcept {
        if (const auto t = target_gain(*current); t != gain_target) {
            gain_target = t;
            gain_step = (gain_target - gain) / setting_ramp;
            gain_ramp = setting_ramp;
        }
        if (previous) {
            if (const auto t = target_gain(*previous); t != previous_gain_target) {
                previous_gain_target = t;
                previous_gain_step = (previous_gain_target - previous_gain) / setting_ramp;
                previous_gain_ramp = setting_ramp;
            }
        }
        if (const auto t = target_mix(); t != mix_target) {
            mix_target = t;
            mix_step = (t - mix) / setting_ramp;
            mix_ramp = setting_ramp;
        }
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
    std::unique_ptr<Prepared> prepare(const Configuration& c, bool has_dry,
                                      const FilterGains& gains = {}) const {
        if (c.sample_rate != rate || !c.input_channels || c.input_channels > inputs ||
            c.output_channels != outputs || (has_dry && outputs > inputs))
            throw std::invalid_argument("replacement must preserve sample rate and channels");
        return std::make_unique<Prepared>(c, has_dry, block, inputs, gains);
    }
    void publish(std::unique_ptr<Prepared> prepared) noexcept {
        collect();
        // Conservative tail never shrinks during a session, including transitions.
        tail.store(std::max(tail.load(), prepared->engine.tail_frames()));
        // Counted before it is handed over: an audio thread that sees the new
        // count also sees what the control side did before publishing.
        prepared->number = published.load(std::memory_order_relaxed) + 1;
        published.store(prepared->number, std::memory_order_release);
        latest = prepared.get();
        delete pending.exchange(prepared.release(), std::memory_order_acq_rel);
    }
    void replace(const Configuration& c, bool has_dry, const FilterGains& gains = {}) {
        publish(prepare(c, has_dry, gains));
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
        // A reset breaks the stream, so a transition in progress has nothing
        // left to fade: the newer filter plays at once, and a filter published
        // before the next audio is installed directly. The older filter is only
        // linked here; the control thread deletes it.
        if (previous) {
            retire(previous);
            previous = nullptr;
        }
        current->engine.reset();
        level_frames = muted ? 0 : setting_ramp;
        previous_level_frames = setting_ramp;
        previous_muted = false;
        std::fill(dry.begin(), dry.end(), 0.f);
        dry_position = 0;
        warm = 0;
        fade = 0;
        dry_frames = bypass ? setting_ramp : 0;
        // The stream starts again: the settings apply at once, without ramps.
        gain = gain_target = target_gain(*current);
        mix = mix_target = target_mix();
        gain_ramp = mix_ramp = 0;
    }
    // Audio thread. Before any audio since construction or reset there is
    // nothing to ramp from, so the silence starts at once: the first frame
    // heard is already silent, as a filter published then is installed
    // directly.
    void mute(std::uint64_t through) noexcept {
        // The filter being faded out mutes if it was published up to
        // `through`, also when silence(through) arrives after the audio
        // thread adopted a later publication.
        if (previous && previous->number <= through)
            previous_muted = true;
        // A filter published after `through` is already playing: nothing
        // more to mute.
        if (current && current->number > through)
            return;
        muted = true;
        muted_through = std::max(muted_through, through);
        if (!started)
            level_frames = 0;
    }
    template <class T>
    bool process(std::span<const T* const> in, std::span<T* const> out,
                 std::size_t frames) noexcept {
        if (frames > maximum || in.size() > inputs || out.size() > outputs) {
            count(invalid_call);
            return false;
        }
        // Before adopting a pending filter, so that one published after
        // silence() ends it and one published before stays silent.
        if (const auto through = silence_through.exchange(0, std::memory_order_acquire))
            mute(through - 1);
        if (!frames)
            return true;
        if (!previous) {
            if (auto next = pending.exchange(nullptr, std::memory_order_acq_rel)) {
                if (started) {
                    previous = current;
                    previous_level_frames = level_frames;
                    previous_muted = muted;
                    // PLUG-10: the outgoing filter carries on with its
                    // gain, ramp included.
                    previous_gain = gain;
                    previous_gain_target = gain_target;
                    previous_gain_step = gain_step;
                    previous_gain_ramp = gain_ramp;
                    warm = block;
                    fade = 0;
                    // Outside Manual mode the new filter starts at its own
                    // figure (it is silent until its warm-up ends), so the
                    // crossfade covers the change.
                    if (mode != GainMode::set) {
                        gain = gain_target = target_gain(*next);
                        gain_ramp = 0;
                    }
                } else {
                    // Nothing has been heard yet (initial restoration): install
                    // directly, so output follows the filter from the first frame.
                    retire(current);
                }
                // PLUG-23: the new filter may have a dry signal when the old
                // one had none, or the reverse; each one's dry share fades
                // with it (below). The delay line needs no clearing: a new
                // filter is heard only after its warm-up, which is one
                // block, the line's whole length, and the line is written
                // throughout it.
                current = next;
                // A filter published before mute() is as silenced as the
                // one it replaces (jrp's contract: silent until the next
                // published filter takes over; PLUG-7 found the adoption of an
                // earlier publication lifting it).
                muted = muted && next->number <= muted_through;
                level_frames = muted ? 0 : setting_ramp;
            }
        }
        retarget();
        if (!started) {
            // Before any audio since construction or reset there is nothing
            // to ramp from: settings changed since then apply at once, as
            // reset() applies them. A latency used to hide these ramps, but
            // at zero latency the first frames are heard (PERF-2).
            dry_frames = bypass ? setting_ramp : 0;
            gain = gain_target;
            mix = mix_target;
            gain_ramp = mix_ramp = 0;
        }
        started = true;
        bool invalid_input = false;
        for (std::size_t t = 0; t < frames; ++t)
            for (std::size_t c = 0; c < inputs; ++c) {
                double v = c < in.size() && in[c] ? in[c][t] : 0;
                const bool valid =
                    std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max();
                invalid_input = invalid_input || !valid;
                input[t * inputs + c] = valid ? static_cast<float>(v) : 0.f;
            }
        if (invalid_input)
            count(rejected_input);
        // Prepared, valid nonoverlapping spans and finite input satisfy Engine's
        // checked entry point; defensive containment keeps exceptions inside host.
        try {
            current->engine.process(input, wet, frames);
            if (previous)
                previous->engine.process(input, old_wet, frames);
        } catch (...) {
            count(processing_failure);
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
                    fade = std::min(crossfade, fade + 1);
                    blend = static_cast<float>(fade) / crossfade;
                }
            }
            if (bypass && dry_frames < setting_ramp)
                ++dry_frames;
            else if (!bypass && dry_frames)
                --dry_frames;
            if (gain_ramp && --gain_ramp)
                gain += gain_step;
            else
                gain = gain_target;
            if (previous_gain_ramp && --previous_gain_ramp)
                previous_gain += previous_gain_step;
            else
                previous_gain = previous_gain_target;
            if (mix_ramp && --mix_ramp)
                mix += mix_step;
            else
                mix = mix_target;
            if (muted && level_frames)
                --level_frames;
            if (previous_muted && previous_level_frames)
                --previous_level_frames;
            // PLUG-23: each filter mixes with its own dry rule: Mix applies
            // only to a filter with a dry signal (otherwise it is fully
            // wet), and bypass passes the dry signal only as far as such a
            // filter plays. While a filter with a dry signal replaces one
            // without, or the reverse, both fade with the crossfade, so
            // neither bypass nor Mix jumps.
            const bool dry_now = current->dry, dry_before = previous && previous->dry;
            // PLUG-10: while both filters play at the same gain (always in
            // Manual mode), it is applied once, as before the modes existed.
            const float gain_before = previous ? previous_gain : gain;
            const bool shared_gain = gain_before == gain;
            const float mix_now = dry_now ? mix : 1.f;
            const float mix_before = dry_before ? mix : 1.f;
            const bool same_rule = !previous || dry_now == dry_before;
            const float dry_mix = share(dry_frames), level = share(level_frames),
                        previous_level = share(previous_level_frames);
            // A muted filter's dry signal ramps and stays silent with it
            // (PLUG-23: a failed first load passes nothing, with one output
            // too). An unmuted filter's level is exactly 1, which leaves
            // every value as before.
            const float dry_level = level;
            const float previous_dry_level = previous ? previous_level : 1.f;
            // Both filters' dry shares, crossfaded, where they apply alike.
            const float shared_dry = dry_level == 1 && previous_dry_level == 1 ? 1.f
                                     : previous
                                         ? blend * dry_level + (1 - blend) * previous_dry_level
                                         : dry_level;
            const float dry_share = same_rule ? (dry_now ? shared_dry : 0.f)
                                    : dry_now ? blend * dry_level
                                              : (1.f - blend) * previous_dry_level;
            for (std::size_t c = 0; c < outputs; ++c) {
                // Bypass passes input c to output c when the filter's counts are
                // equal. With unequal counts it plays silence on every output
                // (owner's decision, 22 October 2026): no channel has an
                // unfiltered counterpart, and a crossover must not send
                // full-range audio to a driver. PLUG-23: the filter's counts,
                // not the host's (a 2-in, 6-out filter on 6 inputs has no dry
                // signal); with a dry signal, outputs <= inputs.
                auto index = t * outputs + c;
                const float dry_input = dry_now || dry_before ? input[t * inputs + c] : 0.f;
                float dry_value = dry_input; // At zero latency the dry signal is not delayed.
                if (block) {
                    auto& delayed = dry[dry_position * outputs + c];
                    dry_value = delayed;
                    delayed = dry_input;
                }
                double value = level * static_cast<double>(wet[index]);
                if (same_rule) {
                    if (previous && !shared_gain) {
                        value = (1.0 - blend) * gain_before * previous_level * old_wet[index] +
                                blend * gain * value;
                    } else {
                        if (previous)
                            value = (1.0 - blend) * previous_level * old_wet[index] + blend * value;
                        // Gain 1 and mix 1 (the defaults) leave every value
                        // exactly as it was before the settings existed.
                        value *= gain;
                    }
                    if (mix_now != 1)
                        value = mix_now * value + (1.0 - mix_now) * shared_dry * dry_value;
                } else {
                    const double before = gain_before * previous_level * old_wet[index];
                    value =
                        blend * (mix_now * gain * value + (1.0 - mix_now) * dry_level * dry_value) +
                        (1.0 - blend) * (mix_before * before +
                                         (1.0 - mix_before) * previous_dry_level * dry_value);
                }
                value = (1.0 - dry_mix) * value + dry_mix * dry_share * dry_value;
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
                    value = 0;
                    invalid_output = true;
                }
                if (c < out.size() && out[c])
                    out[c][t] = static_cast<T>(value);
            }
            if (block && ++dry_position == block)
                dry_position = 0;
        }
        if (invalid_output) {
            count(output_overflow);
            reset();
        }
        if (previous && !warm && fade == crossfade) {
            retire(previous);
            previous = nullptr;
            previous_level_frames = setting_ramp;
            previous_muted = false;
        }
        return true;
    }
};
HostProcessor::HostProcessor(const Configuration& c, std::size_t maximum, std::size_t block)
    : impl_(std::make_unique<Impl>(c, own_dry(c), maximum, block)) {}
namespace {
std::size_t offered_latency(std::size_t frames) {
    if (std::find(host_latencies.begin(), host_latencies.end(), frames) == host_latencies.end())
        throw std::invalid_argument("the latency " + std::to_string(frames) +
                                    " frames is not one the plug-ins offer (0, or 64 to 4096 "
                                    "frames in powers of two)");
    return frames;
}
} // namespace
HostProcessor::HostProcessor(const Configuration& c, std::size_t maximum,
                             const HostSettings& settings)
    : impl_(std::make_unique<Impl>(c, own_dry(c), maximum, offered_latency(settings.latency),
                                   settings)) {}
HostProcessor::HostProcessor(const Configuration& c, std::size_t maximum,
                             const HostSettings& settings, std::size_t inputs)
    : impl_(std::make_unique<Impl>(c, own_dry(c), maximum, offered_latency(settings.latency),
                                   settings, inputs)) {}
namespace {
// HostPlayback::filter is never null when built by filter_playback() or
// HostFilter; a default-constructed one is refused rather than dereferenced.
// PLUG-57: `use` gets the filter as it plays, laid out on each channel here,
// only where a processor is built from it, and for no longer.
template <class Use> decltype(auto) with_played(const HostPlayback& p, Use&& use) {
    if (!p.filter)
        throw std::invalid_argument("no filter to play");
    if (!p.each_channel)
        return use(*p.filter);
    const auto laid_out = on_each_channel(*p.filter, p.each_channel);
    return use(laid_out);
}
} // namespace
HostProcessor::HostProcessor(const HostPlayback& p, std::size_t maximum,
                             const HostSettings& settings, std::size_t inputs)
    : impl_(with_played(p, [&](const Configuration& c) {
          return std::make_unique<Impl>(c, p.dry, maximum, offered_latency(settings.latency),
                                        settings, inputs, p.gains);
      })) {}
HostProcessor::~HostProcessor() = default;
void HostProcessor::replace(const Configuration& c) {
    impl_->replace(c, own_dry(c));
}
void HostProcessor::replace(const HostPlayback& p) {
    with_played(p, [&](const Configuration& c) { impl_->replace(c, p.dry, p.gains); });
}
struct HostProcessor::Replacement::Data {
    const Impl* owner = nullptr;
    std::unique_ptr<Impl::Prepared> prepared;
};
HostProcessor::Replacement::Replacement(std::unique_ptr<Data> data) noexcept
    : data_(std::move(data)) {}
HostProcessor::Replacement::Replacement(Replacement&&) noexcept = default;
HostProcessor::Replacement& HostProcessor::Replacement::operator=(Replacement&&) noexcept = default;
HostProcessor::Replacement::~Replacement() = default;
HostProcessor::Replacement HostProcessor::prepare(const Configuration& c) const {
    auto prepared = impl_->prepare(c, own_dry(c));
    // Member by member: Apple Clang lacks parenthesized aggregate initialization.
    auto data = std::make_unique<Replacement::Data>();
    data->owner = impl_.get();
    data->prepared = std::move(prepared);
    return Replacement(std::move(data));
}
HostProcessor::Replacement HostProcessor::prepare(const HostPlayback& p) const {
    auto prepared =
        with_played(p, [&](const Configuration& c) { return impl_->prepare(c, p.dry, p.gains); });
    // Member by member: Apple Clang lacks parenthesized aggregate initialization.
    auto data = std::make_unique<Replacement::Data>();
    data->owner = impl_.get();
    data->prepared = std::move(prepared);
    return Replacement(std::move(data));
}
void HostProcessor::publish(Replacement&& r) noexcept {
    // A Replacement from another processor, or one already published, is ignored.
    if (r.data_ && r.data_->owner == impl_.get() && r.data_->prepared)
        impl_->publish(std::move(r.data_->prepared));
    r.data_.reset();
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
void HostProcessor::set_gain_db(double db) noexcept {
    db = clamp_gain_db(db);
    if (db == impl_->set_db)
        return;
    impl_->set_db = db;
    impl_->set_gain = gain_from_db(db);
}
void HostProcessor::set_mix(double mix) noexcept {
    impl_->mix_setting = static_cast<float>(clamp_mix(mix));
}
void HostProcessor::set_gain_mode(GainMode mode) noexcept {
    impl_->mode = mode;
}
void HostProcessor::update_gains(const FilterGains& gains) noexcept {
    impl_->latest->set_gains(gains);
}
bool HostProcessor::keep(const HostPlayback& playback) noexcept {
    // HostFilter vouches that the filter is the one published last; a
    // processor that met a fault starts afresh instead, as it did before
    // PLUG-38. A muted processor never plays an accepted filter (mute() and
    // silence() follow a failed first load), so it never gets here. The
    // load's commit then clears the fault baseline as for any accepted load:
    // a fault the audio thread counts between this check and that commit is
    // logged by the next observe() but not shown as Fault, a window of a few
    // instructions on the control side.
    if (!playback.playing || faults_since_clear())
        return false;
    update_gains(playback.gains);
    return true;
}
void HostProcessor::play(const HostPlayback& playback) {
    if (!keep(playback))
        replace(playback);
}
std::uint64_t HostProcessor::publications() const noexcept {
    return impl_->published.load(std::memory_order_acquire);
}
void HostProcessor::mute() noexcept {
    mute(publications());
}
void HostProcessor::mute(std::uint64_t through) noexcept {
    impl_->mute(through);
}
void HostProcessor::silence() noexcept {
    // The control side is the only publisher, so this count is exact.
    silence(impl_->published.load(std::memory_order_relaxed));
}
void HostProcessor::silence(std::uint64_t through) noexcept {
    impl_->silence_through.store(through + 1, std::memory_order_release);
}
std::size_t HostProcessor::latency_frames() const noexcept {
    return impl_->block;
}
std::size_t HostProcessor::tail_frames() const noexcept {
    return impl_->tail.load();
}
HostFaults HostProcessor::faults() const noexcept {
    HostFaults f;
    f.rejected_input = impl_->rejected_input.load(std::memory_order_relaxed);
    f.output_overflow = impl_->output_overflow.load(std::memory_order_relaxed);
    f.processing_failure = impl_->processing_failure.load(std::memory_order_relaxed);
    f.invalid_call = impl_->invalid_call.load(std::memory_order_relaxed);
    return f;
}
HostFaults HostProcessor::clear_faults() noexcept {
    const auto snapshot = faults();
    impl_->faults_cleared.store(snapshot.total(), std::memory_order_release);
    return snapshot;
}
void HostProcessor::hold_fault_signal(bool held) noexcept {
    impl_->signal_held.store(held, std::memory_order_release);
}
bool HostProcessor::faults_since_clear() const noexcept {
    return impl_->fault_total() > impl_->faults_cleared.load(std::memory_order_acquire);
}
bool HostProcessor::fault_to_signal() const noexcept {
    const auto cleared = impl_->faults_cleared.load(std::memory_order_acquire);
    return impl_->fault_total() > cleared &&
           impl_->faults_signalled.load(std::memory_order_relaxed) <= cleared &&
           !impl_->signal_held.load(std::memory_order_acquire);
}
void HostProcessor::fault_signalled() noexcept {
    impl_->faults_signalled.store(impl_->fault_total(), std::memory_order_relaxed);
}
bool HostProcessor::process(std::span<const float* const> in, std::span<float* const> out,
                            std::size_t n) noexcept {
    // PERF-15: the whole call (conversion, gain, mixing and the engines) runs
    // with subnormals flushed to zero; the host's mode is restored on return.
    const detail::ScopedFlushDenormals flush;
    return impl_->process(in, out, n);
}
bool HostProcessor::process(std::span<const double* const> in, std::span<double* const> out,
                            std::size_t n) noexcept {
    // PERF-15: the whole call (conversion, gain, mixing and the engines) runs
    // with subnormals flushed to zero; the host's mode is restored on return.
    const detail::ScopedFlushDenormals flush;
    return impl_->process(in, out, n);
}
} // namespace convolver
