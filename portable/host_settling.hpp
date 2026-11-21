// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-39 step 2: one rule for when the host has settled its sample rate and
// channels, for the adapters whose hosts may restore state before they choose
// them (VST3 and the Audio Unit; LV2 always knows them).
//
// Each adapter feeds the same three events and asks two questions:
// - started(): the host started the plug-in with a configuration (VST3
//   setActive(true), the Audio Unit's Initialize);
// - played(): audio with samples was processed (the audio thread; one
//   relaxed atomic store, no lock, no allocation);
// - stopped(): the host stopped it (VST3 setActive(false), Cleanup);
// - known(): the host's configuration if it counts as known, to check
//   requests against (HostFilter::load()'s `host`);
// - check(): what a request that does not suit that configuration means
//   (HostFilter::load()'s HostLoadCheck); a chosen single filter is read for
//   that configuration only when it is settled (any answer but
//   keep_until_suited), while a
//   configuration list always chooses its entry for known() (PLUG-27).
//
// A file chosen in the window follows one rule in both formats (PLUG-27,
// jrp, 20 November 2026): it is refused for not suiting the host only while
// the host is playing audio through the plug-in with its configuration
// (started, and audio processed in this start: playing(); PLUG-41);
// otherwise it is
// kept and checked when the host starts with its configuration, as a preset
// is. Audacity 4.0.2 sets its window's copy up at 44.1 kHz and starts it only
// for a moment to pass on parameter edits, whatever rate the project plays
// at, so a 48 kHz filter chosen there before playback was refused (jrp's
// Audacity run, 20 November 2026).
//
// For restored state the two formats keep their own rule, as jrp decided (13
// November 2026), and the adapter contract records ("state that arrives
// before the host is configured"):
// - HostBinding::activation (VST3): a start binds the configuration for as
//   long as it stays the same; restored state is refused only once audio has
//   played with it (in that start or an earlier one since it last changed),
//   and a project the host says it is opening never is: hosts start the
//   plug-in on placeholder buses before they restore a project (REAPER;
//   jrp's crossover retest, 7 November 2026).
// - HostBinding::rendering (Audio Unit): while started, the configuration is
//   the host's and restored state is checked against it; stopped, it stays
//   known only if audio rendered with that start, and then still binds
//   restored state. JUCE hosts
//   initialize at AUBase's default 44.1 kHz to try a layout, then restore the
//   project before preparing to play; counting that probe would turn a
//   48 kHz project's filter into a failed load.
//
// The control-side members are used under the adapter's control lock;
// played() may run on the audio thread at the same time.
#pragma once
#include "convolver/host.hpp"
#include <atomic>
#include <optional>

namespace convolver {

enum class HostBinding { activation, rendering };

class HostSettling {
  public:
    explicit HostSettling(HostBinding binding) noexcept : binding_(binding) {}

    void started(const HostLayout& now) noexcept {
        // VST3: audio played with this configuration before still counts;
        // only a new one needs the host to play again.
        if (binding_ == HostBinding::rendering || !started_ || !same_configuration(*started_, now))
            played_.store(false, std::memory_order_relaxed);
        started_ = now;
        active_ = true;
        played_now_.store(false, std::memory_order_relaxed);
    }
    void played() noexcept {
        played_.store(true, std::memory_order_relaxed);
        played_now_.store(true, std::memory_order_relaxed);
    }
    void stopped() noexcept {
        active_ = false;
        if (binding_ == HostBinding::rendering && played_.load(std::memory_order_relaxed))
            committed_ = started_;
    }

    // `now`: the configuration the host has set. While started it is the
    // host's; stopped, it counts only while it still matches the binding
    // start (VST3: the last start; Audio Unit: the last start that rendered).
    std::optional<HostLayout> known(const HostLayout& now) const noexcept {
        if (active_)
            return now;
        const auto& bound = binding_ == HostBinding::activation ? started_ : committed_;
        if (bound && same_configuration(*bound, now))
            return now;
        return std::nullopt;
    }
    // PLUG-27 (jrp, 20 and 21 November 2026): whether the host is playing
    // audio through the plug-in now: started, and audio processed in this
    // start. Adapters confirm a chosen filter's check (HostFilter::confirm())
    // only when they see this on a control call while the host is still
    // running the plug-in, never at the stop, so a brief start such as
    // Audacity 4.0.2's FlushParameters (start, one block, stop, with no
    // control call between) confirms nothing, without guessing which starts
    // are brief.
    bool playing() const noexcept {
        return active_ && played_now_.load(std::memory_order_relaxed);
    }
    // The configuration of the current or last start, which processors are
    // built for (VST3 builds one for a new Latency with it).
    const std::optional<HostLayout>& last_start() const noexcept {
        return started_;
    }
    HostLoadCheck check(HostRequestKind kind) const noexcept {
        // PLUG-27, both formats: a chosen file is refused only while the
        // host plays audio in this start. Not from audio played in an
        // earlier start with the same configuration, which VST3 remembers
        // for restored state: a file chosen after a restart, before its
        // first block, waits as the Audio Unit's does (jrp's review, 21
        // November 2026).
        if (kind == HostRequestKind::chosen_file)
            return playing() ? HostLoadCheck::keep_unless_one_plays
                             : HostLoadCheck::keep_until_suited;
        // Settled, but a later start may bring another configuration: a
        // filter that does not suit is refused only while one that suits
        // plays (PLUG-42).
        if (binding_ == HostBinding::rendering)
            return HostLoadCheck::keep_unless_one_plays;
        switch (kind) {
        case HostRequestKind::chosen_file: // Answered above.
            return HostLoadCheck::keep_until_suited;
        case HostRequestKind::restore:
            return played_.load(std::memory_order_relaxed) ? HostLoadCheck::keep_unless_one_plays
                                                           : HostLoadCheck::keep_until_suited;
        case HostRequestKind::session:
            return HostLoadCheck::keep_until_suited;
        }
        return HostLoadCheck::keep_unless_one_plays;
    }

  private:
    HostBinding binding_;
    bool active_ = false;
    std::optional<HostLayout> started_;   // The current or last start.
    std::optional<HostLayout> committed_; // Audio Unit: the last start that rendered.
    std::atomic<bool> played_{false};     // Since the binding start (see started()).
    std::atomic<bool> played_now_{false}; // Audio processed in this start.
};

} // namespace convolver
