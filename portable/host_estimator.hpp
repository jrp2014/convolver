// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PLUG-74: the noise estimate behind Gain mode Estimated, found on a thread
// of the plug-in's own rather than the host's. The estimate filters ten
// filter lengths of noise, seconds for a long reverb (1.7 s for 30 s, 19 s
// for 120 s on a Linux cloud machine), and a host waiting for it at a
// project's restore or at the Gain-mode switch shows a frozen window or a
// late start. With the search here, Estimated plays the Clip-safe figure
// (never louder) until the estimate is ready, then ramps to it in place, as
// choosing Estimated always did. Control side only; private to
// Convolver::host. Every in-tree adapter keeps one per instance.
#include "convolver/host.hpp"
#include "host_log.hpp"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace convolver {
class HostEstimator {
  public:
    // `ready` runs on the estimator's thread after each search that ran to
    // its end, holding none of the estimator's locks: `identity` is the
    // filter searched (HostFilter::filter_identity()), `why` empty when its
    // estimate is now in the figure cache (HostFilter::estimate_found()
    // takes it) or why it could not be found. The adapter takes its control
    // lock in it, so it must never destroy the estimator while holding that
    // lock.
    using Ready = std::function<void(std::uint64_t identity, const std::string& why)>;
    explicit HostEstimator(Ready ready);
    // stop().
    ~HostEstimator();
    // Ends a search within one block of noise and waits for the thread,
    // including a `ready` call in progress; a later search starts it again.
    // Not under the lock `ready` takes. VST3 calls it at terminate().
    void stop() noexcept;
    HostEstimator(const HostEstimator&) = delete;
    HostEstimator& operator=(const HostEstimator&) = delete;
    // Control thread, after an accepted load or a Gain-mode change: searches
    // for the estimate `filter` wants (HostFilter::estimate_wanted()),
    // replacing a search not yet started and ending one running for another
    // filter; with none wanted, ends any search, as nothing waits for it.
    // The thread starts at the first search. Returns false only when a
    // search is wanted and cannot start (no thread or memory for it).
    bool search(const HostFilter& filter) noexcept;
    // Waits until no search is waiting or running: the one wanted has put
    // its estimate in the figure cache, failed, or been stopped. Not for
    // `ready`, which may still be on its way. Under the adapter's lock is
    // fine; finish_estimate() uses it.
    void wait() noexcept;
    // Tests: whether a search is waiting or running.
    bool busy() const noexcept;

  private:
    void run() noexcept;
    Ready ready_;
    mutable std::mutex mutex_;
    std::condition_variable wake_, finished_;
    std::optional<HostEstimateSearch> waiting_;
    std::optional<std::uint64_t> running_; // The identity searched now.
    std::uint64_t running_keyed_ = 0;
    std::atomic<bool> stop_running_{false};
    bool quit_ = false;
    std::thread thread_;
};

// PLUG-74: after an accepted load or a Gain-mode change, under the adapter's
// control lock: starts the search the filter wants; if none can start, finds
// the estimate in place, as before PLUG-74, and gives it to the processor
// `playing` returns. Returns why that failed too, as a phrase, or empty.
// Never throws.
std::string search_estimate(HostEstimator&, HostFilter&, PlayingLookup,
                            const void* playing) noexcept;
template <class Playing>
std::string search_estimate(HostEstimator& estimator, HostFilter& filter,
                            const Playing& playing) noexcept {
    return search_estimate(
        estimator, filter, [](const void* p) { return (*static_cast<const Playing*>(p))(); },
        &playing);
}
// PLUG-74: before a processor starts (an activation, or a restore while the
// host renders offline), under the adapter's control lock: waits for the
// search the filter wants and takes its estimate, so a render never depends
// on how long the search took: an export plays Estimated's own figure from
// its first frame, as before PLUG-74. Restores before the activation still
// gain: their searches have run meanwhile, each instance's on its own
// thread. Gives the estimate to the processor `playing` returns, if any.
// Never throws; if the search failed, the Clip-safe figure plays and
// `ready` says why.
void finish_estimate(HostEstimator&, HostFilter&, PlayingLookup, const void* playing) noexcept;
template <class Playing>
void finish_estimate(HostEstimator& estimator, HostFilter& filter,
                     const Playing& playing) noexcept {
    finish_estimate(
        estimator, filter, [](const void* p) { return (*static_cast<const Playing*>(p))(); },
        &playing);
}
inline void finish_estimate(HostEstimator& estimator, HostFilter& filter) noexcept {
    finish_estimate(estimator, filter, [] { return static_cast<HostProcessor*>(nullptr); });
}
// PLUG-74: in the adapter's HostEstimator::Ready, under its control lock,
// with the settings in force: gives a found estimate of the filter accepted
// now to the processor `playing` returns, which ramps to it in place, and
// the line to log; an empty line when nothing changed (another filter was
// accepted meanwhile, or another Gain mode chosen). Never throws.
GainModeChange estimate_ready(HostFilter&, const HostSettings& chosen, std::uint64_t identity,
                              const std::string& why, PlayingLookup, const void* playing) noexcept;
template <class Playing>
GainModeChange estimate_ready(HostFilter& filter, const HostSettings& chosen,
                              std::uint64_t identity, const std::string& why,
                              const Playing& playing) noexcept {
    return estimate_ready(
        filter, chosen, identity, why,
        [](const void* p) { return (*static_cast<const Playing*>(p))(); }, &playing);
}
} // namespace convolver
