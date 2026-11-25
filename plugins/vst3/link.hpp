// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// VST3-26: how the edit controller reaches the processor of the same
// instance. The factory does not mark the processor distributable (flags 0,
// factory.cpp), so the SDK requires hosts to keep both halves in one process;
// the controller then calls the processor directly instead of exchanging
// settings, requests and answers in messages, which a host may deliver late
// or between two calls of a restore (VST3-15 to VST3-24).
//
// The processor owns a Bridge and registers it under a random key. The
// controller numbers each connection it makes (Bridges::stamp()) and sends
// the number in a pair_request_message; the processor answers with its key
// and that number in a pair_message, from notify() or, if the request came
// before the processor was connected, from its connect(). The controller
// keeps the Bridge only for an answer to its current connection (VST3-27): a
// host that delivers late may pass on an earlier processor's answer after it
// connected the controller to another one, and that processor's key is still
// valid while it lives. A key that is unknown (another process, a closed
// processor) leaves the controller unpaired: it then shows the host's values
// and can load nothing. Messages carry no settings, so a late one has nothing stale in it:
// changed_message only says "read me again".
#include "convolver/host.hpp"
#include "host_report.hpp"
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <utility>

namespace convolver::vst3 {
// "key": the Bridge's key; "connection": the number from the request it answers.
inline constexpr const char* pair_message = "ConvolverPair";
// "connection": the controller's number for its current connection.
inline constexpr const char* pair_request_message = "ConvolverPairRequest";
// The processor changed in a call the host made to it (a restore, an
// activation, new buses): the controller reads it again on the UI thread.
inline constexpr const char* changed_message = "ConvolverChanged";

// What the controller reads from the processor, all under one lock, so the
// parts agree.
struct PairView {
    HostReport report;  // The window's report, which holds the Status line.
    std::string format; // The processor's log tag ("VST3 #2").
    HostSettings settings;
    bool bypass = false; // As the Status shows it.
    // Counts the times something other than the host's process() set the
    // settings (an accepted restore, a chosen preset that holds settings).
    // The controller takes Gain, Mix and Bypass only when it moved: between
    // two moves they are the host's, which passes them in process().
    std::uint64_t generation = 0;
    std::uint32_t latency = 0; // What getLatencySamples() returns.
    // Bridges::stamp() at the last restore the processor accepted: a choice
    // the controller made while unpaired is sent only if it is newer.
    std::uint64_t restored_at = 0;
};
// The outcome of a file the window chose, or of a waiting configuration list
// read again (`tried`: there was one).
struct LoadAnswer {
    bool tried = false;
    bool accepted = false;
    std::optional<HostSettings> settings; // A chosen preset's (PLUG-26).
};

// What the controller may ask of the processor; each call is made on the
// controller's thread and takes the processor's lock itself.
class ProcessorLink {
  public:
    virtual PairView view() = 0;
    virtual void chosen_latency(std::size_t frames) = 0;
    virtual void chosen_gain_mode(GainMode) = 0;
    virtual LoadAnswer load_chosen(const std::string& path) = 0;
    // PLUG-40: a list refused before the host's settings were known, read
    // again once the host plays audio (the window asks twice a second).
    virtual LoadAnswer read_waiting_list() = 0;
    // PLUG-22: the Status reads Bypassed at once, not at the next block.
    virtual void show_bypass(bool) = 0;
    // PLUG-22: the controller state's name, shown while that filter plays.
    virtual void offer_name(const std::string& name, const std::string& identity) = 0;
    // DIAG-1: explain processing faults; `only_if_due`: at most every ten
    // seconds, as the window asks often.
    virtual void explain_faults(bool only_if_due) = 0;

  protected:
    ~ProcessorLink() = default;
};

// Shared by the processor and the controllers that paired with it. The
// processor detaches before it goes, waiting for calls in progress; after
// that call() refuses and attached() is false.
class Bridge {
  public:
    explicit Bridge(ProcessorLink& processor) : processor_(&processor) {}
    template <class F> bool call(F&& f) {
        ProcessorLink* processor = nullptr;
        {
            std::lock_guard lock(mutex_);
            if (!processor_)
                return false;
            processor = processor_; // detach() may clear processor_ now; it waits for this call.
            ++calls_;
        }
        struct Done {
            Bridge& bridge;
            ~Done() {
                std::lock_guard lock(bridge.mutex_);
                if (--bridge.calls_ == 0)
                    bridge.idle_.notify_all();
            }
        } done{*this};
        std::forward<F>(f)(*processor);
        return true;
    }
    bool attached() {
        std::lock_guard lock(mutex_);
        return processor_ != nullptr;
    }
    void detach() {
        std::unique_lock lock(mutex_);
        processor_ = nullptr;
        idle_.wait(lock, [this] { return calls_ == 0; });
    }
    // The processor changed in a host call; the controller reads it again
    // at its next call on the UI thread (take_changed()).
    void changed() noexcept {
        changed_.store(true);
    }
    bool take_changed() noexcept {
        return changed_.exchange(false);
    }

  private:
    std::mutex mutex_;
    std::condition_variable idle_;
    ProcessorLink* processor_; // Null once detached.
    int calls_ = 0;
    std::atomic<bool> changed_{true};
};

// The process-wide table of processors by key. Keys are random, so a
// controller cannot reach another instance's processor by mistake.
class Bridges {
  public:
    static std::uint64_t add(std::shared_ptr<Bridge> bridge) {
        auto& t = table();
        std::lock_guard lock(t.mutex);
        std::uint64_t key = 0;
        do
            key = (static_cast<std::uint64_t>(t.random()) << 32) ^ t.random();
        while (key == 0 || t.bridges.count(key));
        t.bridges.emplace(key, std::move(bridge));
        return key;
    }
    static void remove(std::uint64_t key) noexcept {
        auto& t = table();
        std::lock_guard lock(t.mutex);
        t.bridges.erase(key);
    }
    static std::shared_ptr<Bridge> find(std::uint64_t key) {
        auto& t = table();
        std::lock_guard lock(t.mutex);
        const auto at = t.bridges.find(key);
        return at == t.bridges.end() ? nullptr : at->second.lock();
    }
    // A process-wide count that orders what the two halves did before they
    // paired (both live in one process).
    static std::uint64_t stamp() noexcept {
        static std::atomic<std::uint64_t> count{0};
        return ++count;
    }

  private:
    struct Table {
        std::mutex mutex;
        std::map<std::uint64_t, std::weak_ptr<Bridge>> bridges;
        std::mt19937 random{std::random_device{}()};
    };
    static Table& table() {
        static Table t;
        return t;
    }
};
} // namespace convolver::vst3
