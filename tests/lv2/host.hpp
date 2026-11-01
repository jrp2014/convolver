// SPDX-License-Identifier: GPL-2.0-or-later
// A small LV2 host for the tests, built on lilv: it loads the bundle, finds
// plug-ins by URI, instantiates them with the features a host provides
// (urid:map, log:log, options with bufsz:maxBlockLength, the worker),
// connects ports by symbol, sends and reads patch messages on the atom ports,
// and calls the state interface as hosts do.
#pragma once
#include "convolver/host.hpp"
#include "plugin.hpp"
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <lilv/lilv.h>
#include <lv2/log/log.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace convolver::lv2_test {
class UridMap {
  public:
    UridMap();
    UridMap(const UridMap&) = delete;
    UridMap& operator=(const UridMap&) = delete;
    LV2_URID map(const char* uri);
    const char* unmap(LV2_URID) const;
    LV2_URID_Map* map_feature() {
        return &map_;
    }
    LV2_URID_Unmap* unmap_feature() {
        return &unmap_;
    }

  private:
    mutable std::mutex mutex_;
    std::deque<std::string> uris_; // A deque, so unmap's strings never move.
    std::unordered_map<std::string, LV2_URID> ids_;
    LV2_URID_Map map_{};
    LV2_URID_Unmap unmap_{};
};

// Messages the plug-in sent through log:log.
struct Log {
    struct Message {
        std::string type, text;
    };
    std::vector<Message> messages;
    UridMap* urids = nullptr;
    LV2_Log_Log feature{};
    bool contains(const std::string& type, const std::string& text) const;
};

class World {
  public:
    explicit World(const std::filesystem::path& bundle);
    ~World();
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    LilvWorld* get() const {
        return world_;
    }
    UridMap& urids() {
        return urids_;
    }
    // The plug-in at base URI + id, or null.
    const LilvPlugin* plugin(const std::string& uri) const;
    // The fixed layout's plug-in for these counts (discrete-N-M), or null.
    const LilvPlugin* plugin(std::size_t inputs, std::size_t outputs) const;
    // Loads another bundle, such as a preset folder.
    void load_bundle(const std::filesystem::path&);

  private:
    LilvWorld* world_;
    UridMap urids_;
};

// How the host runs the plug-in's worker.
enum class Worker {
    none,        // No work:schedule feature.
    threaded,    // A worker thread; answers are delivered after the next run().
    synchronous, // Free-wheeling: work() runs inside schedule_work, answers after run().
    immediate,   // As synchronous, but answers are delivered at once, inside run().
    deferred,    // Requests wait until run_deferred_work(); answers after the next run().
};

struct Features {
    bool map = true, log = true;
    std::optional<std::int32_t> max_block; // bufsz:maxBlockLength, if given.
    Worker worker = Worker::synchronous;
};

// One property of saved state, as a host stores it.
struct Property {
    LV2_URID key = 0, type = 0;
    std::vector<std::byte> value;
    std::uint32_t flags = 0;
};

class Plugin {
  public:
    Plugin(World&, const LilvPlugin*, double rate, const Features& = {});
    ~Plugin();
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;
    bool instantiated() const {
        return instance_ != nullptr;
    }
    LilvInstance* instance() const {
        return instance_;
    }
    const LilvPlugin* plugin() const {
        return plugin_;
    }
    std::size_t inputs() const {
        return inputs_.size();
    }
    std::size_t outputs() const {
        return outputs_.size();
    }
    void activate();
    void deactivate();
    bool active() const {
        return active_;
    }
    // Connects the audio ports to these buffers (null leaves a port
    // unconnected, which LV2 does not allow; tests never pass null) and runs.
    void run(std::span<const float* const> in, std::span<float* const> out, std::uint32_t frames);
    // run() with zero frames, as hosts do to read output control ports.
    void run_empty();
    // Control ports, read and written around run().
    float enabled = 1, latency = -1, status = -1;
    // The state interface, called directly with the property under <base>state.
    LV2_State_Status restore(std::span<const std::byte>, LV2_URID type);
    LV2_State_Status restore_empty(); // No properties at all.
    // Any properties. With a state folder (set_state_folder), state:mapPath
    // and state:freePath are passed, mapping paths inside the folder to
    // relative ones.
    LV2_State_Status restore(const std::vector<Property>&);
    // With CONVOLVER_AUDIO_GUARD: the plug-in's restore() runs with
    // audio_guard::Starve(first, second).
    std::optional<std::pair<std::size_t, bool>> starve_restore;
    struct Saved {
        LV2_State_Status status = LV2_STATE_SUCCESS;
        std::vector<std::byte> bytes; // Under <base>state.
        LV2_URID type = 0;
        std::uint32_t flags = 0;
        std::size_t properties = 0;
        std::optional<Property> filter; // Under <base>filter.
        std::vector<Property> all;
    };
    Saved save();
    // save() with a store function that refuses every property, as a host
    // out of space would.
    bool store_fails = false;
    void set_state_folder(const std::filesystem::path& folder) {
        state_folder_ = folder;
    }
    std::size_t paths_freed() const {
        return paths_freed_;
    }

    // Patch messages, sent at the start of the next run(): patch:Set of the
    // filter parameter (an atom:Path unless `type` says otherwise) and
    // patch:Get.
    void set_filter(const std::string& path, LV2_URID type = 0);
    void get_filter();
    // Paths the plug-in named in patch:Set on its notify port, oldest first.
    std::vector<std::string> notified;

    // The worker. Scheduling can be made to fail (LV2_WORKER_ERR_NO_SPACE).
    bool scheduling_fails = false;
    // Threaded worker: waits until the queue is empty and work() has returned.
    void wait_for_worker();
    // Deferred worker: calls work() on this thread for up to `count` queued
    // requests, oldest first, as a host's worker thread would when it gets
    // round to them. Returns how many it ran.
    std::size_t run_deferred_work(std::size_t count = ~std::size_t(0));
    std::size_t queued_work();
    // Deferred worker: forgets queued requests, as hosts may on deactivation.
    void drop_queued_work();
    // Pauses the next work() call to reach `point`, through the plug-in's
    // test hook; wait_until_paused() returns once it has, resume() lets it go.
    void pause_at(lv2::WorkerPoint point);
    void wait_until_paused();
    void resume();
    std::size_t responses_delivered() const {
        return delivered_;
    }
    // The PLUG-8 failure record, read through the plug-in's test-only
    // extension (plugin.hpp); empty when there is none.
    std::optional<HostFailure> failure() const;
    Log log;

  private:
    void deliver_responses();
    void write_messages();
    void read_notifications();
    void stop_worker();
    static LV2_Worker_Status schedule(LV2_Worker_Schedule_Handle, std::uint32_t, const void*);
    static LV2_Worker_Status respond(LV2_Worker_Respond_Handle, std::uint32_t, const void*);
    static void pause_hook(void*, lv2::WorkerPoint);
    World& world_;
    const LilvPlugin* plugin_;
    LilvInstance* instance_ = nullptr;
    const LV2_State_Interface* state_ = nullptr;
    const LV2_Worker_Interface* worker_ = nullptr;
    Worker mode_ = Worker::synchronous;
    LV2_Worker_Schedule schedule_{};
    std::filesystem::path state_folder_;
    std::size_t paths_freed_ = 0, delivered_ = 0;
    std::uint32_t enabled_port_ = 0, latency_port_ = 0, status_port_ = 0;
    std::uint32_t control_port_ = 0, notify_port_ = 0;
    // Atom buffers, 8-byte aligned.
    std::vector<std::uint64_t> control_, notify_;
    std::vector<std::pair<bool, std::pair<std::string, LV2_URID>>> messages_; // set?, path, type
    // The worker thread, its queue and the answers waiting for delivery.
    std::thread thread_;
    std::mutex work_mutex_;
    std::condition_variable work_changed_;
    std::deque<std::vector<std::byte>> jobs_;
    bool busy_ = false, stopping_ = false;
    std::mutex response_mutex_;
    std::deque<std::vector<std::byte>> responses_;
    // The pause hook's state.
    std::mutex pause_mutex_;
    std::condition_variable pause_changed_;
    std::optional<lv2::WorkerPoint> armed_;
    bool paused_ = false, released_ = false;
    std::size_t pauses_ = 0;
    std::vector<std::uint32_t> inputs_, outputs_;
    std::vector<LV2_Options_Option> options_;
    std::int32_t max_block_ = 0;
    std::vector<float> scratch_;
    bool active_ = false;
};

std::string state_uri();
std::string filter_uri();
} // namespace convolver::lv2_test
