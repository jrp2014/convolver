// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-39 step 3: the playing processor's ownership (PlayingProcessor).
// Offering, superseding, adoption, retirement, deactivation and destruction,
// each against a probe that records its own destruction, and a stress test
// in which the control side offers continuously while two readers adopt.
// The readers run with an allocation guard: holding never allocates or
// frees. AddressSanitizer, in the sanitizer build, is a second check that no
// reader touches a freed processor.
#include "cases.hpp"
#include "playing_processor.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

thread_local bool audio_thread = false;
void* operator new(std::size_t n) {
    if (audio_thread)
        std::abort();
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (audio_thread && p)
        std::abort();
    std::free(p);
}
#include "replaced_allocation.hpp"

using namespace convolver;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}

// A processor that knows whether it is still alive.
std::atomic<int> alive{0};
struct Probe {
    static constexpr std::uint64_t live = 0x5ec0'1d5a'feb1'0c35;
    explicit Probe(std::uint64_t g) : generation(g) {
        ++alive;
    }
    Probe(const Probe&) = delete;
    Probe& operator=(const Probe&) = delete;
    ~Probe() {
        magic.store(0);
        --alive;
    }
    // What a reader does with it: fails if it has been destroyed.
    void use() {
        require(magic.load() == live, "a reader used a destroyed processor");
        uses.fetch_add(1, std::memory_order_relaxed);
    }
    std::atomic<std::uint64_t> magic{live};
    std::uint64_t generation;
    std::atomic<std::uint64_t> uses{0};
};
using One = PlayingProcessor<Probe, 1>;
using Two = PlayingProcessor<Probe, 2>;

void adopts_newest() {
    {
        One p;
        require(!p.hold(0), "a reader got a processor before any was offered");
        p.offer(std::make_unique<Probe>(1));
        require(p.newest() && p.newest()->generation == 1, "the control side lost the offer");
        {
            const auto held = p.hold(0);
            require(held && held->generation == 1, "the reader did not adopt the offer");
        }
        p.offer(std::make_unique<Probe>(2));
        // Nothing held it: the replaced one is freed by the offer itself.
        require(alive == 1 && p.kept() == 0, "a replaced processor nobody held was kept");
        const auto held = p.hold(0);
        require(held && held->generation == 2, "the reader did not adopt the newer offer");
    }
    require(alive == 0, "destruction left a processor alive");
}

// The control side returns at once while a reader holds the old processor,
// which stays alive until the reader lets it go and a later call frees it.
void control_does_not_wait() {
    One p;
    p.offer(std::make_unique<Probe>(1));
    {
        const auto held = p.hold(0);
        // On this thread: an offer that waited for the reader would never return.
        p.offer(std::make_unique<Probe>(2));
        require(p.newest()->generation == 2, "the newer offer is not the newest");
        require(alive == 2 && p.kept() == 1, "the held processor was not kept");
        held->use();
        require(held->generation == 1, "a held processor changed under its reader");
        p.reclaim();
        require(alive == 2, "reclaim() freed a processor a reader holds");
    }
    require(alive == 2, "letting go freed the processor without a control call");
    p.reclaim();
    require(alive == 1 && p.kept() == 0, "reclaim() kept a processor nobody holds");
    require(p.hold(0)->generation == 2, "the next call did not play the newer offer");
}

// An offer superseded before any reader adopted it is freed by the offer
// that supersedes it, rather than accumulating.
void superseded_offer_is_freed() {
    One p;
    p.offer(std::make_unique<Probe>(1));
    const auto held = p.hold(0);
    for (std::uint64_t g = 2; g < 50; ++g) {
        p.offer(std::make_unique<Probe>(g));
        require(alive == 2 && p.kept() == 1,
                "an offer nobody adopted was kept (generation " + std::to_string(g) + ")");
    }
    require(held->generation == 1, "the held processor changed");
}

// VST3's process() and setProcessing() may hold different generations; each
// stays alive until its own reader lets go.
void readers_hold_different_generations() {
    Two p;
    p.offer(std::make_unique<Probe>(1));
    {
        const auto first = p.hold(0);
        p.offer(std::make_unique<Probe>(2));
        {
            const auto second = p.hold(1);
            require(second->generation == 2, "the second reader did not adopt the newer offer");
            p.offer(std::make_unique<Probe>(3));
            require(alive == 3 && p.kept() == 2, "a held generation was not kept");
        }
        p.reclaim();
        require(alive == 2 && p.kept() == 1, "the second reader's processor was not freed");
        first->use();
        require(first->generation == 1, "the first reader's processor changed");
    }
    p.reclaim();
    require(alive == 1 && p.kept() == 0, "the first reader's processor was not freed");
}

// However often the control side offers, no more processors stay alive than
// the newest and one for each reader.
void kept_is_bounded() {
    Two p;
    p.offer(std::make_unique<Probe>(1));
    const auto first = p.hold(0);
    p.offer(std::make_unique<Probe>(2));
    const auto second = p.hold(1);
    for (std::uint64_t g = 3; g < 200; ++g) {
        p.offer(g % 7 ? std::make_unique<Probe>(g) : nullptr);
        require(p.kept() <= 2 && alive <= 3, "more replaced processors were kept than readers");
    }
    first->use();
    second->use();
}

// Deactivation: nothing is offered and everything is freed.
void clear_frees_everything() {
    Two p;
    p.offer(std::make_unique<Probe>(1));
    {
        const auto held = p.hold(0);
    }
    p.offer(std::make_unique<Probe>(2));
    p.clear();
    require(alive == 0 && p.kept() == 0 && !p.newest(), "clear() left a processor alive");
    require(!p.hold(0) && !p.hold(1), "a reader got a processor after clear()");
    p.offer(std::make_unique<Probe>(3)); // Activated again.
    require(p.hold(1)->generation == 3, "the next activation's processor did not play");
}

// A host that renders while it deactivates: clear() waits for that one call
// rather than free the processor under it.
void clear_waits_for_a_running_reader() {
    One p;
    p.offer(std::make_unique<Probe>(1));
    std::atomic<bool> holding{false}, let_go{false};
    std::thread reader([&] {
        const auto held = p.hold(0);
        holding = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        held->use();
        let_go = true;
    });
    while (!holding)
        std::this_thread::yield();
    p.clear();
    const bool waited = let_go.load();
    reader.join();
    require(waited, "clear() freed a processor a reader was still using");
    require(alive == 0, "clear() left a processor alive");
}

// The control side offers continuously, sometimes none, while two readers
// adopt and use what they hold. Each reader sees generations only increase,
// never uses a freed processor and never allocates; no more processors stay
// alive than the newest and one for each reader.
void stress() {
    Two p;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> failures{0};
    std::array<std::uint64_t, 2> adopted{};
    auto read = [&](std::size_t reader) {
        audio_thread = true;
        std::uint64_t last = 0, count = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            const auto held = p.hold(reader);
            if (!held)
                continue;
            if (held->magic.load() != Probe::live || held->generation < last)
                failures.fetch_add(1);
            last = held->generation;
            held->uses.fetch_add(1, std::memory_order_relaxed);
            ++count;
        }
        audio_thread = false;
        adopted[reader] = count;
    };
    std::thread a(read, 0), b(read, 1);
    std::uint64_t generation = 0, too_many = 0;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
    while (std::chrono::steady_clock::now() < end) {
        ++generation;
        p.offer(generation % 64 ? std::make_unique<Probe>(generation) : nullptr);
        if (generation % 3 == 0)
            p.reclaim();
        too_many += p.kept() > 2 || alive > 3;
    }
    stop = true;
    a.join();
    b.join();
    p.clear();
    std::printf("stress: %llu offers; readers adopted %llu and %llu times\n",
                static_cast<unsigned long long>(generation),
                static_cast<unsigned long long>(adopted[0]),
                static_cast<unsigned long long>(adopted[1]));
    require(failures == 0, "a reader used a destroyed processor or went back a generation");
    require(too_many == 0, "more replaced processors were kept than readers");
    require(adopted[0] > 0 && adopted[1] > 0, "a reader never adopted a processor");
    require(alive == 0, "processors were left alive");
}
} // namespace

int main() {
    const auto failed = convolver::test::run_cases(
        {
            {"adopts_newest", adopts_newest},
            {"control_does_not_wait", control_does_not_wait},
            {"superseded_offer_is_freed", superseded_offer_is_freed},
            {"readers_hold_different_generations", readers_hold_different_generations},
            {"kept_is_bounded", kept_is_bounded},
            {"clear_frees_everything", clear_frees_everything},
            {"clear_waits_for_a_running_reader", clear_waits_for_a_running_reader},
            {"stress", stress},
        },
        [] {
            audio_thread = false;
            alive = 0;
        });
    if (failed)
        return 1;
    std::cout << "playing processor passed\n";
}
