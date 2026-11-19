// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-39 step 3: who owns the processor a plug-in plays, and when a replaced
// one is freed, for the adapters whose control side replaces the processor
// while audio runs (a new Latency, an activation).
//
// The control side offers a replacement and returns; each reader (a thread
// that uses the processor: VST3's process() and setProcessing(), the Audio
// Unit's Render) adopts the newest offer when it next starts using the
// processor, at a callback boundary; and a later control call frees the old
// one once no reader holds it. The control side never waits for audio, and
// the audio side only loads and stores atomics: it never locks, allocates or
// frees.
//
// The rules (jrp's conditions of 13 November 2026, step 3 of the
// consolidation plan):
// - Every reader is protected: each has its own mark, and nothing a mark
//   holds is freed.
// - Retirement is bounded. A replaced processor waits in one of
//   `Readers + 1` places until no mark holds it, so an offer nobody adopted
//   is freed by the next control call, and at most `Readers` replaced
//   processors stay alive after any control call: each is held by a
//   different reader's mark.
// - Deactivation and destruction, when no audio runs, free everything.
// - No thread of its own: freeing happens in the control calls.
//
// Why a reader can never use a freed processor: hold() stores its mark and
// then checks that the processor is still the newest offer, and offer()
// publishes the newest offer and then reads every mark (all sequentially
// consistent). So either the control side sees the mark and keeps the
// processor, or the reader sees the newer offer and tries again before it
// touches the old one. A retired processor is freed only when a reading of
// the marks, taken after it was replaced, found none holding it.
#pragma once
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <thread>

namespace convolver {

template <class Processor, std::size_t Readers> class PlayingProcessor {
    static_assert(Readers >= 1, "a processor nobody reads needs no handover");

  public:
    PlayingProcessor() = default;
    PlayingProcessor(const PlayingProcessor&) = delete;
    PlayingProcessor& operator=(const PlayingProcessor&) = delete;
    ~PlayingProcessor() {
        clear();
    }

    // Control side, one call at a time (the adapter's control lock).

    // The newest offer, which control calls use (it may not play yet:
    // readers adopt it at their next call). Null before the first offer and
    // after clear().
    Processor* newest() const noexcept {
        return newest_.get();
    }
    // Makes `next` (null to offer none) what every reader adopts from its
    // next hold(), keeps the previous offer until no reader holds it, and
    // frees the replaced processors no reader holds any longer. Never waits.
    void offer(std::unique_ptr<Processor> next) noexcept {
        auto previous = std::move(newest_);
        newest_ = std::move(next);
        offered_.store(newest_.get());
        if (previous) {
            // A place is always free: after the last reclaim() at most
            // `Readers` replaced processors were kept.
            for (auto& place : retired_)
                if (!place) {
                    place = std::move(previous);
                    break;
                }
            assert(!previous);
        }
        reclaim();
    }
    // Frees the replaced processors no reader holds. Never waits.
    void reclaim() noexcept {
        // One reading of each mark, taken after the processors below were
        // replaced: a reader that marks one of them later finds a newer
        // offer and lets it go without using it. One reading per mark also
        // bounds what is kept by the number of readers.
        std::array<Processor*, Readers> held{};
        for (std::size_t r = 0; r < Readers; ++r)
            held[r] = marks_[r].load();
        for (auto& place : retired_) {
            if (!place)
                continue;
            bool holding = false;
            for (const auto* h : held)
                holding = holding || h == place.get();
            if (!holding)
                place.reset();
        }
    }
    // Deactivation and destruction, when no audio runs: offers none and
    // frees every processor. A host that still runs a reader meanwhile is
    // waited for, as long as that one call lasts, rather than have its
    // processor freed under it.
    void clear() noexcept {
        offer(nullptr);
        while (kept() != 0) {
            std::this_thread::yield();
            reclaim();
        }
    }
    // How many replaced processors are kept because a reader may hold them.
    std::size_t kept() const noexcept {
        std::size_t n = 0;
        for (const auto& place : retired_)
            n += place != nullptr;
        return n;
    }

    // Reader side: each reader, numbered from 0, holds one processor at a
    // time, on one thread at a time. Atomic loads and stores only.

    // The processor a reader uses, marked for as long as this lives.
    class Held {
      public:
        Processor* get() const noexcept {
            return processor_;
        }
        Processor* operator->() const noexcept {
            return processor_;
        }
        explicit operator bool() const noexcept {
            return processor_ != nullptr;
        }
        Held(const Held&) = delete;
        Held& operator=(const Held&) = delete;
        ~Held() {
            mark_.store(nullptr, std::memory_order_release);
        }

      private:
        friend class PlayingProcessor;
        Held(std::atomic<Processor*>& mark, Processor* processor) noexcept
            : mark_(mark), processor_(processor) {}
        std::atomic<Processor*>& mark_;
        Processor* processor_;
    };
    // Adopts the newest offer (null if none) for reader `reader`.
    Held hold(std::size_t reader) noexcept {
        assert(reader < Readers);
        auto& mark = marks_[reader];
        Processor* processor = nullptr;
        do {
            processor = offered_.load();
            mark.store(processor);
        } while (processor != offered_.load());
        return {mark, processor};
    }

  private:
    std::unique_ptr<Processor> newest_;
    // Replaced processors a reader may still hold.
    std::array<std::unique_ptr<Processor>, Readers + 1> retired_{};
    std::atomic<Processor*> offered_{nullptr}; // newest_, as readers see it.
    std::array<std::atomic<Processor*>, Readers> marks_{};
};

} // namespace convolver
