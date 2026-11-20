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
//   (HostFilter::load()'s HostLoadCheck).
//
// The two formats keep their own rule, as jrp decided (13 November 2026), and
// the adapter contract records ("state that arrives before the host is
// configured"):
// - HostBinding::activation (VST3): a start binds the configuration for as
//   long as it stays the same, so a chosen file is refused at once; restored
//   state is refused only once audio has played with it (in that start or an
//   earlier one since it last changed), and a project the host says it is
//   opening never is: hosts start the plug-in on placeholder buses before
//   they restore a project (REAPER; jrp's crossover retest, 7 November 2026).
// - HostBinding::rendering (Audio Unit): a start binds the configuration only
//   once audio has rendered with it, and then binds every request. JUCE hosts
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
    }
    void played() noexcept {
        played_.store(true, std::memory_order_relaxed);
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
    // The configuration of the current or last start, which processors are
    // built for (VST3 builds one for a new Latency with it).
    const std::optional<HostLayout>& last_start() const noexcept {
        return started_;
    }
    HostLoadCheck check(HostRequestKind kind) const noexcept {
        if (binding_ == HostBinding::rendering)
            return HostLoadCheck::refuse_mismatch;
        switch (kind) {
        case HostRequestKind::chosen_file:
            return HostLoadCheck::refuse_mismatch;
        case HostRequestKind::restore:
            return played_.load(std::memory_order_relaxed) ? HostLoadCheck::refuse_mismatch
                                                           : HostLoadCheck::keep_until_suited;
        case HostRequestKind::session:
            return HostLoadCheck::keep_until_suited;
        }
        return HostLoadCheck::refuse_mismatch;
    }

  private:
    HostBinding binding_;
    bool active_ = false;
    std::optional<HostLayout> started_;   // The current or last start.
    std::optional<HostLayout> committed_; // Audio Unit: the last start that rendered.
    std::atomic<bool> played_{false};     // Since the binding start (see started()).
};

} // namespace convolver
