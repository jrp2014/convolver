// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.hpp"
#include "convolver_identities.hpp"
#include "presets.hpp"
#include <cstdarg>
#include <cstdio>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <stdexcept>

namespace convolver::lv2_test {
std::string state_uri() {
    return presets::lv2_state_uri();
}

UridMap::UridMap() {
    map_.handle = this;
    map_.map = [](LV2_URID_Map_Handle h, const char* uri) {
        return static_cast<UridMap*>(h)->map(uri);
    };
    unmap_.handle = this;
    unmap_.unmap = [](LV2_URID_Unmap_Handle h, LV2_URID id) {
        return static_cast<const UridMap*>(h)->unmap(id);
    };
}
LV2_URID UridMap::map(const char* uri) {
    if (const auto it = ids_.find(uri); it != ids_.end())
        return it->second;
    uris_.emplace_back(uri);
    const auto id = static_cast<LV2_URID>(uris_.size());
    ids_.emplace(uri, id);
    return id;
}
const char* UridMap::unmap(LV2_URID id) const {
    return id && id <= uris_.size() ? uris_[id - 1].c_str() : nullptr;
}

namespace {
int log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char* format, va_list args) {
    auto& log = *static_cast<Log*>(handle);
    va_list copy;
    va_copy(copy, args);
    const int length = std::vsnprintf(nullptr, 0, format, copy);
    va_end(copy);
    std::string text(length > 0 ? static_cast<std::size_t>(length) : 0, '\0');
    if (length > 0)
        std::vsnprintf(text.data(), text.size() + 1, format, args);
    const char* name = log.urids ? log.urids->unmap(type) : nullptr;
    log.messages.push_back({name ? name : "", text});
    return length;
}
int log_printf(LV2_Log_Handle handle, LV2_URID type, const char* format, ...) {
    va_list args;
    va_start(args, format);
    const int result = log_vprintf(handle, type, format, args);
    va_end(args);
    return result;
}
} // namespace

bool Log::contains(const std::string& type, const std::string& text) const {
    for (const auto& m : messages)
        if (m.type == type && m.text.find(text) != std::string::npos)
            return true;
    return false;
}

World::World(const std::filesystem::path& bundle) : world_(lilv_world_new()) {
    if (!world_)
        throw std::runtime_error("lilv_world_new failed");
    // The LV2 specifications (core, state, ...) from the system or LV2_PATH,
    // then only our bundle: other installed plug-ins play no part.
    lilv_world_load_specifications(world_);
    lilv_world_load_plugin_classes(world_);
    load_bundle(bundle);
}
World::~World() {
    lilv_world_free(world_);
}
void World::load_bundle(const std::filesystem::path& bundle) {
    auto directory = std::filesystem::absolute(bundle).string();
    if (directory.empty() || directory.back() != '/')
        directory += '/';
    LilvNode* node = lilv_new_file_uri(world_, nullptr, directory.c_str());
    lilv_world_load_bundle(world_, node);
    lilv_node_free(node);
}
const LilvPlugin* World::plugin(const std::string& uri) const {
    LilvNode* node = lilv_new_uri(world_, uri.c_str());
    const auto* result = lilv_plugins_get_by_uri(lilv_world_get_all_plugins(world_), node);
    lilv_node_free(node);
    return result;
}
const LilvPlugin* World::plugin(std::size_t inputs, std::size_t outputs) const {
    return plugin(std::string(identity::lv2_base_uri) + "discrete-" + std::to_string(inputs) + "-" +
                  std::to_string(outputs));
}

Plugin::Plugin(World& world, const LilvPlugin* plugin, double rate, const Features& features)
    : world_(world), plugin_(plugin) {
    if (!plugin)
        throw std::runtime_error("no such LV2 plug-in in the bundle");
    auto& urids = world.urids();
    log.urids = &urids;
    log.feature.handle = &log;
    log.feature.printf = log_printf;
    log.feature.vprintf = log_vprintf;
    if (features.max_block) {
        max_block_ = *features.max_block;
        options_.push_back({LV2_OPTIONS_INSTANCE, 0, urids.map(LV2_BUF_SIZE__maxBlockLength),
                            sizeof(std::int32_t), urids.map(LV2_ATOM__Int), &max_block_});
    }
    options_.push_back({LV2_OPTIONS_INSTANCE, 0, 0, 0, 0, nullptr});
    const LV2_Feature map{LV2_URID__map, urids.map_feature()};
    const LV2_Feature unmap{LV2_URID__unmap, urids.unmap_feature()};
    const LV2_Feature log_feature{LV2_LOG__log, &log.feature};
    const LV2_Feature options{LV2_OPTIONS__options, options_.data()};
    std::vector<const LV2_Feature*> list;
    if (features.map)
        list.push_back(&map);
    list.push_back(&unmap);
    if (features.log)
        list.push_back(&log_feature);
    if (features.max_block)
        list.push_back(&options);
    list.push_back(nullptr);
    instance_ = lilv_plugin_instantiate(plugin, rate, list.data());
    if (!instance_)
        return;
    state_ = static_cast<const LV2_State_Interface*>(
        lilv_instance_get_extension_data(instance_, LV2_STATE__interface));
    const auto index = [&](const std::string& symbol) {
        LilvNode* node = lilv_new_string(world.get(), symbol.c_str());
        const auto* port = lilv_plugin_get_port_by_symbol(plugin, node);
        lilv_node_free(node);
        if (!port)
            throw std::runtime_error("the plug-in has no port " + symbol);
        return lilv_port_get_index(plugin, port);
    };
    enabled_port_ = index("enabled");
    latency_port_ = index("latency");
    status_port_ = index("status");
    LilvNode* audio = lilv_new_uri(world.get(), LV2_CORE__AudioPort);
    LilvNode* input = lilv_new_uri(world.get(), LV2_CORE__InputPort);
    const auto ports = lilv_plugin_get_num_ports(plugin);
    for (std::uint32_t i = 0; i < ports; ++i) {
        const auto* port = lilv_plugin_get_port_by_index(plugin, i);
        if (lilv_port_is_a(plugin, port, audio))
            (lilv_port_is_a(plugin, port, input) ? inputs_ : outputs_).push_back(i);
    }
    lilv_node_free(audio);
    lilv_node_free(input);
    lilv_instance_connect_port(instance_, enabled_port_, &enabled);
    lilv_instance_connect_port(instance_, latency_port_, &latency);
    lilv_instance_connect_port(instance_, status_port_, &status);
    scratch_.assign(inputs_.size() + outputs_.size(), 0.f);
}
Plugin::~Plugin() {
    if (!instance_)
        return;
    if (active_)
        deactivate();
    lilv_instance_free(instance_);
}
void Plugin::activate() {
    lilv_instance_activate(instance_);
    active_ = true;
}
void Plugin::deactivate() {
    lilv_instance_deactivate(instance_);
    active_ = false;
}
void Plugin::run(std::span<const float* const> in, std::span<float* const> out,
                 std::uint32_t frames) {
    if (in.size() != inputs_.size() || out.size() != outputs_.size())
        throw std::runtime_error("wrong number of audio buffers for the plug-in");
    for (std::size_t c = 0; c < in.size(); ++c)
        lilv_instance_connect_port(instance_, inputs_[c], const_cast<float*>(in[c]));
    for (std::size_t c = 0; c < out.size(); ++c)
        lilv_instance_connect_port(instance_, outputs_[c], out[c]);
    lilv_instance_run(instance_, frames);
}
void Plugin::run_empty() {
    for (std::size_t c = 0; c < inputs_.size(); ++c)
        lilv_instance_connect_port(instance_, inputs_[c], &scratch_[c]);
    for (std::size_t c = 0; c < outputs_.size(); ++c)
        lilv_instance_connect_port(instance_, outputs_[c], &scratch_[inputs_.size() + c]);
    lilv_instance_run(instance_, 0);
}

namespace {
struct Retrieval {
    LV2_URID key;
    std::span<const std::byte> bytes;
    LV2_URID type;
};
} // namespace

LV2_State_Status Plugin::restore(std::span<const std::byte> bytes, LV2_URID type) {
    if (!state_)
        throw std::runtime_error("the plug-in has no state interface");
    Retrieval r{world_.urids().map(state_uri().c_str()), bytes, type};
    const auto retrieve = [](LV2_State_Handle h, std::uint32_t key, std::size_t* size,
                             std::uint32_t* t, std::uint32_t* flags) -> const void* {
        const auto& r = *static_cast<const Retrieval*>(h);
        if (key != r.key)
            return nullptr;
        *size = r.bytes.size();
        *t = r.type;
        *flags = LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE;
        return r.bytes.data();
    };
    return state_->restore(lilv_instance_get_handle(instance_), retrieve, &r, 0, nullptr);
}
LV2_State_Status Plugin::restore_empty() {
    const auto retrieve = [](LV2_State_Handle, std::uint32_t, std::size_t*, std::uint32_t*,
                             std::uint32_t*) -> const void* { return nullptr; };
    return state_->restore(lilv_instance_get_handle(instance_), retrieve, nullptr, 0, nullptr);
}
Plugin::Saved Plugin::save() {
    if (!state_)
        throw std::runtime_error("the plug-in has no state interface");
    struct Store {
        LV2_URID key;
        Saved saved;
    } s{world_.urids().map(state_uri().c_str()), {}};
    const auto store = [](LV2_State_Handle h, std::uint32_t key, const void* value,
                          std::size_t size, std::uint32_t type,
                          std::uint32_t flags) -> LV2_State_Status {
        auto& s = *static_cast<Store*>(h);
        ++s.saved.properties;
        if (key == s.key) {
            const auto* p = static_cast<const std::byte*>(value);
            s.saved.bytes.assign(p, p + size);
            s.saved.type = type;
            s.saved.flags = flags;
        }
        return LV2_STATE_SUCCESS;
    };
    s.saved.status = state_->save(lilv_instance_get_handle(instance_), store, &s,
                                  LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, nullptr);
    return s.saved;
}
} // namespace convolver::lv2_test
