// SPDX-License-Identifier: GPL-2.0-or-later
#include "host.hpp"
#include "convolver_identities.hpp"
#include "plugin.hpp"
#include "presets.hpp"
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/patch/patch.h>
#include <stdexcept>

namespace convolver::lv2_test {
std::string state_uri() {
    return presets::lv2_state_uri();
}
std::string filter_uri() {
    return std::string(identity::lv2_base_uri) + lv2::filter_suffix;
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
    // Hosts let plug-ins map from any thread, and the tests save from another.
    const std::lock_guard lock(mutex_);
    if (const auto it = ids_.find(uri); it != ids_.end())
        return it->second;
    uris_.emplace_back(uri);
    const auto id = static_cast<LV2_URID>(uris_.size());
    ids_.emplace(uri, id);
    return id;
}
const char* UridMap::unmap(LV2_URID id) const {
    const std::lock_guard lock(mutex_);
    return id && id <= uris_.size() ? uris_[id - 1].c_str() : nullptr;
}

namespace {
int log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char* format, va_list args) {
    auto& log = *static_cast<Log*>(handle);
    // Convolver's messages are a few hundred bytes; longer ones are cut.
    std::array<char, 8192> text{};
    // The analyzer cannot see that the caller started `args` (it is a
    // parameter, as in vprintf itself): a known false positive.
    // NOLINTNEXTLINE(clang-analyzer-valist.Uninitialized)
    const int length = std::vsnprintf(text.data(), text.size(), format, args);
    const char* name = log.urids ? log.urids->unmap(type) : nullptr;
    log.messages.push_back({name ? name : "", text.data()});
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
    : world_(world), plugin_(plugin), mode_(features.worker) {
    if (!plugin)
        throw std::runtime_error("no such LV2 plug-in in the bundle");
    schedule_.handle = this;
    schedule_.schedule_work = schedule;
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
    const LV2_Feature worker{LV2_WORKER__schedule, &schedule_};
    std::vector<const LV2_Feature*> list;
    if (features.map)
        list.push_back(&map);
    list.push_back(&unmap);
    if (features.log)
        list.push_back(&log_feature);
    if (features.max_block)
        list.push_back(&options);
    if (mode_ != Worker::none)
        list.push_back(&worker);
    list.push_back(nullptr);
    instance_ = lilv_plugin_instantiate(plugin, rate, list.data());
    if (!instance_)
        return;
    state_ = static_cast<const LV2_State_Interface*>(
        lilv_instance_get_extension_data(instance_, LV2_STATE__interface));
    worker_ = static_cast<const LV2_Worker_Interface*>(
        lilv_instance_get_extension_data(instance_, LV2_WORKER__interface));
    if (mode_ != Worker::none && !worker_)
        throw std::runtime_error("the plug-in has no worker interface");
    if (const auto* hooks = static_cast<const lv2::TestWorkerAccess*>(
            lilv_instance_get_extension_data(instance_, lv2::test_worker_uri)))
        hooks->set_pause(lilv_instance_get_handle(instance_), this, pause_hook);
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
    control_port_ = index("control");
    notify_port_ = index("notify");
    control_.assign(lv2::atom_buffer_bytes / sizeof(std::uint64_t), 0);
    notify_.assign(lv2::atom_buffer_bytes / sizeof(std::uint64_t), 0);
    lilv_instance_connect_port(instance_, control_port_, control_.data());
    lilv_instance_connect_port(instance_, notify_port_, notify_.data());
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
    if (mode_ == Worker::threaded)
        thread_ = std::thread([this] {
            std::unique_lock lock(work_mutex_);
            for (;;) {
                work_changed_.wait(lock, [&] { return stopping_ || !jobs_.empty(); });
                if (stopping_)
                    return;
                auto job = std::move(jobs_.front());
                jobs_.pop_front();
                busy_ = true;
                lock.unlock();
                worker_->work(lilv_instance_get_handle(instance_), respond, this,
                              static_cast<std::uint32_t>(job.size()), job.data());
                lock.lock();
                busy_ = false;
                work_changed_.notify_all();
            }
        });
}
Plugin::~Plugin() {
    if (!instance_)
        return;
    // As hosts do: deactivate (which waits for a work() call in progress),
    // stop the worker, dropping what is still queued, then clean up.
    if (active_)
        deactivate();
    stop_worker();
    lilv_instance_free(instance_);
}
void Plugin::stop_worker() {
    if (!thread_.joinable())
        return;
    {
        std::lock_guard lock(work_mutex_);
        stopping_ = true;
        jobs_.clear();
    }
    work_changed_.notify_all();
    thread_.join();
}

LV2_Worker_Status Plugin::schedule(LV2_Worker_Schedule_Handle handle, std::uint32_t size,
                                   const void* data) {
    auto& h = *static_cast<Plugin*>(handle);
    if (h.scheduling_fails)
        return LV2_WORKER_ERR_NO_SPACE;
    if (h.mode_ == Worker::threaded) {
        const auto* bytes = static_cast<const std::byte*>(data);
        {
            std::lock_guard lock(h.work_mutex_);
            h.jobs_.emplace_back(bytes, bytes + size);
        }
        h.work_changed_.notify_all();
        return LV2_WORKER_SUCCESS;
    }
    return h.worker_->work(lilv_instance_get_handle(h.instance_), respond, &h, size, data);
}
LV2_Worker_Status Plugin::respond(LV2_Worker_Respond_Handle handle, std::uint32_t size,
                                  const void* data) {
    auto& h = *static_cast<Plugin*>(handle);
    if (h.mode_ == Worker::immediate) {
        ++h.delivered_;
        return h.worker_->work_response(lilv_instance_get_handle(h.instance_), size, data);
    }
    const auto* bytes = static_cast<const std::byte*>(data);
    std::lock_guard lock(h.response_mutex_);
    h.responses_.emplace_back(bytes, bytes + size);
    return LV2_WORKER_SUCCESS;
}
void Plugin::deliver_responses() {
    std::deque<std::vector<std::byte>> ready;
    {
        std::lock_guard lock(response_mutex_);
        ready.swap(responses_);
    }
    for (auto& r : ready) {
        worker_->work_response(lilv_instance_get_handle(instance_),
                               static_cast<std::uint32_t>(r.size()), r.data());
        ++delivered_;
    }
}
void Plugin::wait_for_worker() {
    std::unique_lock lock(work_mutex_);
    work_changed_.wait(lock, [&] { return jobs_.empty() && !busy_; });
}

void Plugin::pause_hook(void* context, lv2::WorkerPoint point) {
    auto& h = *static_cast<Plugin*>(context);
    std::unique_lock lock(h.pause_mutex_);
    if (h.armed_ != point)
        return;
    h.armed_.reset();
    h.paused_ = true;
    h.released_ = false;
    ++h.pauses_;
    h.pause_changed_.notify_all();
    h.pause_changed_.wait(lock, [&] { return h.released_; });
    h.paused_ = false;
    h.pause_changed_.notify_all();
}
void Plugin::pause_at(lv2::WorkerPoint point) {
    std::lock_guard lock(pause_mutex_);
    armed_ = point;
}
void Plugin::wait_until_paused() {
    std::unique_lock lock(pause_mutex_);
    pause_changed_.wait(lock, [&] { return paused_; });
}
void Plugin::resume() {
    std::unique_lock lock(pause_mutex_);
    const auto pause = pauses_;
    released_ = true;
    pause_changed_.notify_all();
    // Until that pause has ended; the worker may already have paused again.
    pause_changed_.wait(lock, [&] { return !paused_ || pauses_ != pause; });
}

void Plugin::set_filter(const std::string& path, LV2_URID type) {
    messages_.push_back({true, {path, type ? type : world_.urids().map(LV2_ATOM__Path)}});
}
void Plugin::get_filter() {
    messages_.push_back({false, {"", 0}});
}
void Plugin::write_messages() {
    auto& u = world_.urids();
    LV2_Atom_Forge forge;
    lv2_atom_forge_init(&forge, u.map_feature());
    lv2_atom_forge_set_buffer(&forge, reinterpret_cast<std::uint8_t*>(control_.data()),
                              control_.size() * sizeof(std::uint64_t));
    LV2_Atom_Forge_Frame sequence;
    bool ok = lv2_atom_forge_sequence_head(&forge, &sequence, 0) != 0;
    for (const auto& [set, message] : messages_) {
        const auto& [path, type] = message;
        LV2_Atom_Forge_Frame object;
        ok = ok && lv2_atom_forge_frame_time(&forge, 0) &&
             lv2_atom_forge_object(&forge, &object, 0,
                                   u.map(set ? LV2_PATCH__Set : LV2_PATCH__Get)) &&
             lv2_atom_forge_key(&forge, u.map(LV2_PATCH__property)) &&
             lv2_atom_forge_urid(&forge, u.map(filter_uri().c_str()));
        if (set)
            ok = ok && lv2_atom_forge_key(&forge, u.map(LV2_PATCH__value)) &&
                 lv2_atom_forge_atom(&forge, static_cast<std::uint32_t>(path.size() + 1), type) &&
                 lv2_atom_forge_write(&forge, path.c_str(),
                                      static_cast<std::uint32_t>(path.size() + 1));
        if (ok)
            lv2_atom_forge_pop(&forge, &object);
    }
    if (!ok)
        throw std::runtime_error("patch messages do not fit the control port's buffer");
    lv2_atom_forge_pop(&forge, &sequence);
    messages_.clear();
    // The notify port: an empty buffer of this capacity, as hosts pass it.
    auto* out = reinterpret_cast<LV2_Atom*>(notify_.data());
    out->type = u.map(LV2_ATOM__Chunk);
    out->size =
        static_cast<std::uint32_t>(notify_.size() * sizeof(std::uint64_t) - sizeof(LV2_Atom));
}
void Plugin::read_notifications() {
    auto& u = world_.urids();
    const auto* sequence = reinterpret_cast<const LV2_Atom_Sequence*>(notify_.data());
    if (sequence->atom.type != u.map(LV2_ATOM__Sequence))
        throw std::runtime_error("the notify port does not hold a sequence after run()");
    LV2_ATOM_SEQUENCE_FOREACH(sequence, event) {
        const auto* object = reinterpret_cast<const LV2_Atom_Object*>(&event->body);
        if (event->body.type != u.map(LV2_ATOM__Object) ||
            object->body.otype != u.map(LV2_PATCH__Set))
            throw std::runtime_error("the notify port carries something other than patch:Set");
        const LV2_Atom* property = nullptr;
        const LV2_Atom* value = nullptr;
        lv2_atom_object_get(object, u.map(LV2_PATCH__property), &property, u.map(LV2_PATCH__value),
                            &value, 0);
        if (!property || property->type != u.map(LV2_ATOM__URID) ||
            reinterpret_cast<const LV2_Atom_URID*>(property)->body != u.map(filter_uri().c_str()) ||
            !value || value->type != u.map(LV2_ATOM__Path))
            throw std::runtime_error("the notify port's patch:Set is not the filter path");
        const auto* text = static_cast<const char*>(LV2_ATOM_BODY_CONST(value));
        notified.emplace_back(text, strnlen(text, value->size));
    }
}
std::optional<HostFailure> Plugin::failure() const {
    const auto* access = static_cast<const lv2::TestFailureAccess*>(
        lilv_instance_get_extension_data(instance_, lv2::test_failure_uri));
    if (!access)
        throw std::runtime_error("the plug-in has no test failure access");
    std::optional<HostFailure> copy;
    access->read(lilv_instance_get_handle(instance_), &copy,
                 [](void* context, const HostFailure* f) {
                     if (f)
                         *static_cast<std::optional<HostFailure>*>(context) = *f;
                 });
    return copy;
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
    write_messages();
    lilv_instance_run(instance_, frames);
    read_notifications();
    if (worker_)
        deliver_responses();
}
void Plugin::run_empty() {
    for (std::size_t c = 0; c < inputs_.size(); ++c)
        lilv_instance_connect_port(instance_, inputs_[c], &scratch_[c]);
    for (std::size_t c = 0; c < outputs_.size(); ++c)
        lilv_instance_connect_port(instance_, outputs_[c], &scratch_[inputs_.size() + c]);
    write_messages();
    lilv_instance_run(instance_, 0);
    read_notifications();
    if (worker_)
        deliver_responses();
}

namespace {
// state:mapPath and state:freePath for a state folder: paths inside it are
// stored relative to it.
struct PathMapping {
    std::filesystem::path folder;
    std::size_t* freed;
};
char* copy(const std::string& text) {
    auto* result = static_cast<char*>(std::malloc(text.size() + 1));
    if (result)
        std::memcpy(result, text.c_str(), text.size() + 1);
    return result;
}
char* abstract_path(LV2_State_Map_Path_Handle handle, const char* absolute) {
    const auto& m = *static_cast<const PathMapping*>(handle);
    const auto relative = std::filesystem::path(absolute).lexically_relative(m.folder);
    const bool inside = !relative.empty() && *relative.begin() != "..";
    return copy(inside ? relative.string() : absolute);
}
char* absolute_path(LV2_State_Map_Path_Handle handle, const char* abstract) {
    const auto& m = *static_cast<const PathMapping*>(handle);
    const std::filesystem::path path(abstract);
    return copy(path.is_absolute() ? path.string() : (m.folder / path).string());
}
void free_path(LV2_State_Free_Path_Handle handle, char* path) {
    ++*static_cast<PathMapping*>(handle)->freed;
    std::free(path);
}
} // namespace

LV2_State_Status Plugin::restore(std::span<const std::byte> bytes, LV2_URID type) {
    return restore({Property{world_.urids().map(state_uri().c_str()), type,
                             std::vector<std::byte>(bytes.begin(), bytes.end()),
                             LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE}});
}
LV2_State_Status Plugin::restore_empty() {
    return restore(std::vector<Property>{});
}
LV2_State_Status Plugin::restore(const std::vector<Property>& properties) {
    if (!state_)
        throw std::runtime_error("the plug-in has no state interface");
    const auto retrieve = [](LV2_State_Handle h, std::uint32_t key, std::size_t* size,
                             std::uint32_t* type, std::uint32_t* flags) -> const void* {
        for (const auto& p : *static_cast<const std::vector<Property>*>(h))
            if (p.key == key) {
                *size = p.value.size();
                *type = p.type;
                *flags = p.flags;
                return p.value.data();
            }
        return nullptr;
    };
    PathMapping mapping{state_folder_, &paths_freed_};
    LV2_State_Map_Path map{&mapping, abstract_path, absolute_path};
    LV2_State_Free_Path free{&mapping, free_path};
    const LV2_Feature map_feature{LV2_STATE__mapPath, &map};
    const LV2_Feature free_feature{LV2_STATE__freePath, &free};
    const LV2_Feature* with_paths[] = {&map_feature, &free_feature, nullptr};
    return state_->restore(lilv_instance_get_handle(instance_), retrieve,
                           const_cast<std::vector<Property>*>(&properties), 0,
                           state_folder_.empty() ? nullptr : with_paths);
}
Plugin::Saved Plugin::save() {
    if (!state_)
        throw std::runtime_error("the plug-in has no state interface");
    struct Store {
        LV2_URID key, filter;
        Saved saved;
    } s{world_.urids().map(state_uri().c_str()), world_.urids().map(filter_uri().c_str()), {}};
    const auto store = [](LV2_State_Handle h, std::uint32_t key, const void* value,
                          std::size_t size, std::uint32_t type,
                          std::uint32_t flags) -> LV2_State_Status {
        auto& s = *static_cast<Store*>(h);
        ++s.saved.properties;
        const auto* p = static_cast<const std::byte*>(value);
        Property property{key, type, std::vector<std::byte>(p, p + size), flags};
        if (key == s.key) {
            s.saved.bytes = property.value;
            s.saved.type = type;
            s.saved.flags = flags;
        } else if (key == s.filter) {
            s.saved.filter = property;
        }
        s.saved.all.push_back(std::move(property));
        return LV2_STATE_SUCCESS;
    };
    PathMapping mapping{state_folder_, &paths_freed_};
    LV2_State_Map_Path map{&mapping, abstract_path, absolute_path};
    LV2_State_Free_Path free{&mapping, free_path};
    const LV2_Feature map_feature{LV2_STATE__mapPath, &map};
    const LV2_Feature free_feature{LV2_STATE__freePath, &free};
    const LV2_Feature* with_paths[] = {&map_feature, &free_feature, nullptr};
    s.saved.status = state_->save(lilv_instance_get_handle(instance_), store, &s,
                                  LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE,
                                  state_folder_.empty() ? nullptr : with_paths);
    return s.saved;
}
} // namespace convolver::lv2_test
