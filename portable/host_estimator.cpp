// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_estimator.hpp"
#include "advice.hpp"
#include "attenuation.hpp"
#include "figure_cache.hpp"
#include <utility>

namespace convolver {
HostEstimator::HostEstimator(Ready ready) : ready_(std::move(ready)) {}

HostEstimator::~HostEstimator() {
    stop();
}

void HostEstimator::stop() noexcept {
    try {
        {
            const std::lock_guard lock(mutex_);
            quit_ = true;
            waiting_.reset();
            stop_running_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable())
            thread_.join();
        const std::lock_guard lock(mutex_);
        quit_ = false;
    } catch (...) { // A lock or the join failed: nothing else to do.
    }
}

bool HostEstimator::search(const HostFilter& filter) noexcept {
    try {
        auto wanted = filter.estimate_wanted();
        std::unique_lock lock(mutex_);
        if (!wanted) {
            waiting_.reset();
            if (running_)
                stop_running_ = true;
            return true;
        }
        const bool same_running = running_ && *running_ == wanted->identity &&
                                  running_keyed_ == wanted->keyed && !stop_running_;
        if (same_running) {
            waiting_.reset(); // The search running finds it.
            return true;
        }
        if (running_)
            stop_running_ = true; // Another filter's: nothing waits for it.
        waiting_ = std::move(wanted);
        if (!thread_.joinable()) {
            try {
                thread_ = std::thread([this] { run(); });
            } catch (...) {
                waiting_.reset();
                return false;
            }
        }
        lock.unlock();
        wake_.notify_one();
        return true;
    } catch (...) { // Out of memory for the search's figures.
        return false;
    }
}

void HostEstimator::wait() noexcept {
    try {
        std::unique_lock lock(mutex_);
        finished_.wait(lock, [this] { return !waiting_ && !running_; });
    } catch (...) { // The lock failed: nothing to wait with.
    }
}

bool HostEstimator::busy() const noexcept {
    try {
        const std::lock_guard lock(mutex_);
        return waiting_.has_value() || running_.has_value();
    } catch (...) {
        return false;
    }
}

void HostEstimator::run() noexcept {
    try {
        std::unique_lock lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return quit_ || waiting_.has_value(); });
            if (quit_)
                return;
            auto next = std::move(*waiting_);
            waiting_.reset();
            running_ = next.identity;
            running_keyed_ = next.keyed;
            stop_running_ = false;
            lock.unlock();
            std::string why;
            bool stopped = false;
            try {
                detail::search_estimate(next, &stop_running_);
            } catch (const detail::EstimateStopped&) {
                stopped = true;
            } catch (const std::exception& e) {
                try {
                    why = detail::advice_of(e).problem;
                } catch (...) {
                    why = "out of memory";
                }
            } catch (...) {
                why = "an unexpected error";
            }
            next.filter.reset(); // The search's copy of the filter goes now.
            lock.lock();
            running_.reset();
            if (!waiting_)
                finished_.notify_all();
            if (quit_)
                return;
            if (stopped)
                continue;
            lock.unlock();
            try {
                ready_(next.identity, why);
            } catch (...) { // The adapter's: nothing to undo here.
            }
            lock.lock();
        }
    } catch (...) { // The lock failed: later searches wait for the next instance.
    }
}

std::string search_estimate(HostEstimator& estimator, HostFilter& filter, PlayingLookup lookup,
                            const void* playing) noexcept {
    if (estimator.search(filter))
        return {};
    // No thread: as before PLUG-74, on the caller's.
    try {
        filter.find_estimates_later(false);
        filter.estimate(filter.estimating(), [&](const FilterGains& gains) {
            if (auto* processor = lookup(playing))
                processor->update_gains(gains);
        });
        filter.find_estimates_later(true);
        return {};
    } catch (const std::exception& e) {
        filter.find_estimates_later(true);
        try {
            return detail::advice_of(e).problem;
        } catch (...) {
        }
    } catch (...) {
        filter.find_estimates_later(true);
    }
    return {};
}

void finish_estimate(HostEstimator& estimator, HostFilter& filter, PlayingLookup lookup,
                     const void* playing) noexcept {
    try {
        if (!filter.estimate_wanted())
            return;
        estimator.wait();
        filter.estimate_found([&](const FilterGains& gains) {
            if (auto* processor = lookup(playing))
                processor->update_gains(gains);
        });
    } catch (...) { // Out of memory: the Clip-safe figure plays until `ready`.
    }
}

GainModeChange estimate_ready(HostFilter& filter, const HostSettings& chosen,
                              std::uint64_t identity, const std::string& why, PlayingLookup lookup,
                              const void* playing) noexcept {
    GainModeChange change;
    if (!filter.filter() || filter.filter_identity() != identity || !filter.estimating() ||
        chosen.gain_mode != GainMode::estimate)
        return change; // Nothing waits for it now.
    try {
        if (why.empty()) {
            if (!filter.estimate_found([&](const FilterGains& gains) {
                    if (auto* processor = lookup(playing))
                        processor->update_gains(gains);
                }))
                return change;
            change.line =
                "The filter's estimate is ready: " + gain_words(chosen, &filter.gains()) + ".";
            change.problem = !applied_gain_db(chosen, filter.gains());
        } else if (filter.estimate_wanted()) {
            change.line = "The filter's estimate could not be found (" + why +
                          "), so Estimated plays the Clip-safe figure; choose Estimated again to "
                          "retry.";
            change.problem = true;
        }
    } catch (...) { // Out of memory for the line, or for the estimate's figures.
    }
    return change;
}
} // namespace convolver
