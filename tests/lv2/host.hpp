// SPDX-License-Identifier: GPL-2.0-or-later
// A small LV2 host for the tests, built on lilv: it loads the bundle, finds
// plug-ins by URI, instantiates them with the features a host provides
// (urid:map, log:log, options with bufsz:maxBlockLength), connects ports by
// symbol, and calls the state interface as hosts do.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <lilv/lilv.h>
#include <lv2/log/log.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
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
    std::vector<std::string> uris_;
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

struct Features {
    bool map = true, log = true;
    std::optional<std::int32_t> max_block; // bufsz:maxBlockLength, if given.
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
    struct Saved {
        LV2_State_Status status = LV2_STATE_SUCCESS;
        std::vector<std::byte> bytes;
        LV2_URID type = 0;
        std::uint32_t flags = 0;
        std::size_t properties = 0;
    };
    Saved save();
    Log log;

  private:
    World& world_;
    const LilvPlugin* plugin_;
    LilvInstance* instance_ = nullptr;
    const LV2_State_Interface* state_ = nullptr;
    std::uint32_t enabled_port_ = 0, latency_port_ = 0, status_port_ = 0;
    std::vector<std::uint32_t> inputs_, outputs_;
    std::vector<LV2_Options_Option> options_;
    std::int32_t max_block_ = 0;
    std::vector<float> scratch_;
    bool active_ = false;
};

std::string state_uri();
} // namespace convolver::lv2_test
