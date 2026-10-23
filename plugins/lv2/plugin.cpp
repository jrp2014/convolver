// SPDX-License-Identifier: GPL-2.0-or-later
// The LV2 adapter (LV2-1). One shared library serves every fixed layout in
// plugins/identities.json; each layout is its own plug-in URI with fixed
// ports (docs/plans/2026-10-21-plugin-formats.md#lv2-adapter-design).
//
// Threads, as the LV2 specifications define them:
// - run() and connect_port() are the audio thread. run() never locks,
//   allocates or does I/O: it reads the bypass port, writes the latency and
//   status ports and calls HostProcessor::process.
// - instantiate, activate, deactivate, cleanup and the state interface's
//   restore() are never called at the same time as run() (the plug-in does not
//   declare state:threadSafeRestore). They take the control lock.
// - save() may run at the same time as run(). It takes the control lock and
//   touches no port and nothing run() writes.
//
// Filters arrive through state (the encoded host state under <base>state, as
// convolver-preset writes it). restore() prepares the filter itself: before
// any audio it is installed directly, afterwards HostProcessor crossfades. The
// filter path parameter and worker-thread loading are LV2-2.
#include "plugin.hpp"
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include "presets.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <lv2/atom/atom.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>
#include <lv2/options/options.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <memory>
#include <mutex>
#include <span>
#include <string>

namespace convolver::lv2 {
namespace {
constexpr std::size_t max_channels = identity::max_channels;
constexpr std::size_t layout_count = identity::fixed_layouts.size();
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<bool>::is_always_lock_free);

struct Instance {
    HostLayout layout;
    std::size_t max_block = default_max_block;
    LV2_URID state_key = 0, chunk = 0;
    LV2_Log_Logger logger{};
    // Ports, as connected by the host (audio thread only).
    const float* enabled = nullptr;
    float* latency = nullptr;
    float* status = nullptr;
    std::array<const float*, max_channels> in{};
    std::array<float*, max_channels> out{};
    // Control side, under `control`.
    std::mutex control;
    HostFilter filter{max_channels};
    std::unique_ptr<HostProcessor> host; // Exists while active.
    // Why the last load was rejected, until a load succeeds. HostFilter keeps
    // the same text but clears it at activation once the kept filter plays;
    // the status port keeps showing the failure until the user loads again.
    std::string load_error;
    std::string activation_error; // The processor for the filter could not be built.
    // Shared with run().
    std::atomic<int> shown{static_cast<int>(Status::no_filter)};
    std::atomic<bool> bypass{false}; // Last seen on the enabled port; applied at activation.

    void update_status() {
        auto s = Status::no_filter;
        if (!activation_error.empty() || !load_error.empty() || filter.failed())
            s = Status::load_failed;
        else if (filter.filter())
            s = Status::running;
        shown.store(static_cast<int>(s), std::memory_order_relaxed);
    }
    void log_error(const std::string& message) {
        // Without the host's log feature this prints to stderr, which is
        // also where LV2 hosts send a plug-in's unlogged diagnostics.
        lv2_log_error(&logger, "Convolver: %s\n", message.c_str());
    }
};

// URIs live as long as the library: lv2_descriptor hands out pointers to them.
struct Descriptors {
    std::array<std::string, layout_count> uris;
    std::array<LV2_Descriptor, layout_count> list{};
};
const Descriptors& descriptors();

std::size_t layout_index(const LV2_Descriptor* d) {
    const auto& all = descriptors().list;
    for (std::size_t i = 0; i < all.size(); ++i)
        if (&all[i] == d || std::strcmp(all[i].URI, d->URI) == 0)
            return i;
    return all.size();
}

const void* feature(const LV2_Feature* const* features, const char* uri) {
    for (auto f = features; f && *f; ++f)
        if (std::strcmp((*f)->URI, uri) == 0)
            return (*f)->data;
    return nullptr;
}

LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char*,
                       const LV2_Feature* const* features) {
    const auto index = layout_index(descriptor);
    const auto* map = static_cast<const LV2_URID_Map*>(feature(features, LV2_URID__map));
    if (index == layout_count || !map || !std::isfinite(rate) || rate < 1 || rate > 768000 ||
        std::floor(rate) != rate)
        return nullptr;
    try {
        auto p = std::make_unique<Instance>();
        const auto& layout = identity::fixed_layouts[index];
        p->layout = {static_cast<int>(rate), static_cast<std::size_t>(layout.inputs),
                     static_cast<std::size_t>(layout.outputs)};
        p->state_key = map->map(map->handle, presets::lv2_state_uri().c_str());
        p->chunk = map->map(map->handle, LV2_ATOM__Chunk);
        lv2_log_logger_init(
            &p->logger, const_cast<LV2_URID_Map*>(map),
            static_cast<LV2_Log_Log*>(const_cast<void*>(feature(features, LV2_LOG__log))));
        const auto max_length = map->map(map->handle, LV2_BUF_SIZE__maxBlockLength);
        const auto atom_int = map->map(map->handle, LV2_ATOM__Int);
        const auto atom_long = map->map(map->handle, LV2_ATOM__Long);
        for (auto o =
                 static_cast<const LV2_Options_Option*>(feature(features, LV2_OPTIONS__options));
             o && o->key; ++o) {
            if (o->key != max_length || !o->value)
                continue;
            long long frames = 0;
            if (o->type == atom_int && o->size == sizeof(std::int32_t))
                frames = *static_cast<const std::int32_t*>(o->value);
            else if (o->type == atom_long && o->size == sizeof(std::int64_t))
                frames = *static_cast<const std::int64_t*>(o->value);
            if (frames > 0)
                p->max_block =
                    static_cast<std::size_t>(std::min<long long>(frames, largest_max_block));
        }
        return p.release();
    } catch (...) {
        return nullptr;
    }
}

void connect_port(LV2_Handle handle, std::uint32_t port, void* data) {
    auto& p = *static_cast<Instance*>(handle);
    const auto ni = p.layout.inputs, no = p.layout.outputs;
    if (port == enabled_port)
        p.enabled = static_cast<const float*>(data);
    else if (port == latency_port)
        p.latency = static_cast<float*>(data);
    else if (port == status_port)
        p.status = static_cast<float*>(data);
    else if (port >= first_audio_port && port < first_audio_port + ni)
        p.in[port - first_audio_port] = static_cast<const float*>(data);
    else if (port >= first_audio_port + ni && port < first_audio_port + ni + no)
        p.out[port - first_audio_port - ni] = static_cast<float*>(data);
}

void activate_locked(Instance& p) {
    p.host.reset();
    p.activation_error.clear();
    try {
        // A fresh processor: the filter plays from the first frame, its tail
        // and history start afresh, and LV2's activate() is also its reset.
        p.host = std::make_unique<HostProcessor>(p.filter.activate(p.layout), p.max_block);
    } catch (const std::exception& e) {
        p.activation_error = std::string("The filter could not be prepared, so Convolver plays "
                                         "silence: ") +
                             e.what();
    } catch (...) {
        p.activation_error = "The filter could not be prepared, so Convolver plays silence.";
    }
    if (!p.host) {
        try {
            p.host = std::make_unique<HostProcessor>(unfiltered_configuration(p.layout, true),
                                                     p.max_block);
        } catch (...) {
        }
        p.log_error(p.activation_error);
    }
    if (p.host) {
        p.host->set_bypass(p.bypass.load(std::memory_order_relaxed));
        p.host->reset(); // Start fully bypassed or fully wet, without a ramp.
    }
    p.update_status();
}

// No exception may leave a callback into C host code; the catch-all handlers
// below cover allocation failures while building messages.
void activate(LV2_Handle handle) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        activate_locked(p);
    } catch (...) {
    }
}

void run(LV2_Handle handle, std::uint32_t frames) {
    auto& p = *static_cast<Instance*>(handle);
    const auto ni = p.layout.inputs, no = p.layout.outputs;
    if (p.latency)
        *p.latency = static_cast<float>(p.host ? p.host->latency_frames() : 256);
    if (p.status)
        *p.status = static_cast<float>(p.shown.load(std::memory_order_relaxed));
    // lv2:enabled is 1 to process and 0 to bypass; anything else counts as on.
    const bool bypass = p.enabled && *p.enabled < .5f;
    p.bypass.store(bypass, std::memory_order_relaxed);
    if (!p.host) {
        for (std::size_t c = 0; c < no; ++c)
            if (p.out[c])
                std::fill_n(p.out[c], frames, 0.f);
        return;
    }
    p.host->set_bypass(bypass);
    std::array<const float*, max_channels> in{};
    std::array<float*, max_channels> out{};
    for (std::size_t done = 0; done < frames;) {
        const auto n = std::min<std::size_t>(frames - done, p.max_block);
        for (std::size_t c = 0; c < ni; ++c)
            in[c] = p.in[c] ? p.in[c] + done : nullptr;
        for (std::size_t c = 0; c < no; ++c)
            out[c] = p.out[c] ? p.out[c] + done : nullptr;
        if (!p.host->process(std::span(in).first(ni), std::span(out).first(no), n))
            for (std::size_t c = 0; c < no; ++c)
                if (out[c])
                    std::fill_n(out[c], n, 0.f);
        done += n;
    }
}

void deactivate(LV2_Handle handle) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        p.host.reset();
    } catch (...) {
    }
}

void cleanup(LV2_Handle handle) {
    auto* p = static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p->control); // Lets a save() in progress finish first.
        p->host.reset();
    } catch (...) {
    }
    delete p;
}

LV2_State_Status save(LV2_Handle handle, LV2_State_Store_Function store, LV2_State_Handle state,
                      std::uint32_t, const LV2_Feature* const*) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        HostState saved;
        saved.configuration = p.filter.filter();
        saved.failed = p.filter.failed(); // Reopening after a failed load stays silent.
        // Bypass is the lv2:enabled port, which the host saves itself.
        const auto bytes = encode_host_state(saved);
        if (p.host)
            p.host->collect();
        return store(state, p.state_key, bytes.data(), bytes.size(), p.chunk,
                     LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    } catch (...) {
        return LV2_STATE_ERR_UNKNOWN;
    }
}

LV2_State_Status restore_locked(Instance& p, LV2_State_Retrieve_Function retrieve,
                                LV2_State_Handle state) {
    std::size_t size = 0;
    std::uint32_t type = 0, flags = 0;
    const void* value = retrieve(state, p.state_key, &size, &type, &flags);
    if (!value)
        return LV2_STATE_SUCCESS; // Nothing of ours saved (for example, port values only).
    const bool was_failed = p.filter.failed();
    auto status = LV2_STATE_ERR_UNKNOWN;
    try {
        HostState next;
        if (type != p.chunk) {
            status = LV2_STATE_ERR_BAD_TYPE;
            p.filter.reject("The saved state has the wrong type; it should be a chunk of bytes "
                            "(atom:Chunk, or xsd:base64Binary in a preset file).");
            throw std::invalid_argument(p.filter.error());
        }
        try {
            if (size > max_host_state_bytes)
                throw std::invalid_argument("it is larger than " +
                                            std::to_string(max_host_state_bytes) + " bytes");
            next = decode_host_state(std::span(static_cast<const std::byte*>(value), size));
        } catch (const std::exception& e) {
            p.filter.reject(std::string("The saved state could not be read: ") + e.what());
            throw;
        }
        // Bypass is the lv2:enabled port, which the host restores itself;
        // HostFilter leaves the state's bypass flag to the adapter, which ignores it.
        // LV2 fixes the rate and channels at instantiation, so the host's
        // configuration is always known and checked here. While active, a
        // prepared replacement is published (installed directly if no audio
        // has run yet); inactive, the next activation installs it.
        p.filter.load(std::move(next), p.layout, [&](const Configuration& c) {
            if (p.host)
                p.host->replace(c);
        });
        p.activation_error.clear();
        p.load_error.clear();
        p.update_status();
        if (p.filter.failed())
            p.log_error(p.filter.error()); // A saved failure: silence again, explained.
        return LV2_STATE_SUCCESS;
    } catch (...) {
        // A failed first load plays silence rather than the unfiltered input.
        if (p.host && p.filter.failed() && !was_failed) {
            try {
                p.host->replace(p.filter.activate(p.layout));
            } catch (...) {
            }
        }
        p.load_error = p.filter.error();
        p.update_status();
        p.log_error(p.load_error);
        return status;
    }
}

LV2_State_Status restore(LV2_Handle handle, LV2_State_Retrieve_Function retrieve,
                         LV2_State_Handle state, std::uint32_t, const LV2_Feature* const*) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        return restore_locked(p, retrieve, state);
    } catch (...) {
        return LV2_STATE_ERR_UNKNOWN;
    }
}

const LV2_State_Interface state_interface{save, restore};

const void* extension_data(const char* uri) {
    if (std::strcmp(uri, LV2_STATE__interface) == 0)
        return &state_interface;
    return nullptr;
}

const Descriptors& descriptors() {
    static const Descriptors all = [] {
        Descriptors d;
        for (std::size_t i = 0; i < layout_count; ++i) {
            d.uris[i] =
                std::string(identity::lv2_base_uri) + std::string(identity::fixed_layouts[i].id);
            d.list[i] = {d.uris[i].c_str(), instantiate, connect_port,  activate, run,
                         deactivate,        cleanup,     extension_data};
        }
        return d;
    }();
    return all;
}
} // namespace
} // namespace convolver::lv2

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(std::uint32_t index) {
    try {
        const auto& all = convolver::lv2::descriptors().list;
        return index < all.size() ? &all[index] : nullptr;
    } catch (...) {
        return nullptr;
    }
}
