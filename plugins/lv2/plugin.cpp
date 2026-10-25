// SPDX-License-Identifier: GPL-2.0-or-later
// The LV2 adapter (LV2-1, LV2-2). One shared library serves every fixed layout
// in plugins/identities.json; each layout is its own plug-in URI with fixed
// ports (docs/project/plans/2026-10-21-plugin-formats.md#lv2-adapter-design).
//
// Threads, as the LV2 specifications define them:
// - run(), connect_port() and the worker's work_response() are the audio
//   thread. They never lock, allocate or do I/O: run() reads the bypass port
//   and patch messages, writes the latency, status and notify ports, schedules
//   worker requests (and a fault report, DIAG-1, for work() to log) and calls
//   HostProcessor::process.
// - work() runs on the host's worker thread, or inside run() when the host
//   is free-wheeling. It takes the control lock.
// - instantiate, activate, deactivate, cleanup and the state interface's
//   restore() are never called at the same time as run() (the plug-in does not
//   declare state:threadSafeRestore). They take the control lock.
// - save() may run at the same time as run(). It takes the control lock and
//   touches no port and nothing run() writes.
//
// Filters arrive two ways:
// - Through state: the encoded host state under <base>state (convolver-preset
//   writes it; save() stores it), and the filter file's path under
//   <base>filter. restore() prefers the embedded copy, falls back on the file,
//   and loads synchronously: before any audio the filter is installed
//   directly, afterwards HostProcessor crossfades.
// - Through the filter parameter: a patch:Set in run() becomes a numbered
//   request (a generation) for the worker, which loads the file, prepares the
//   filter and publishes it only if no newer request has arrived meanwhile.
//   Every request is answered, so the status port shows "loading" until the
//   newest one has been, and patch:Set on the notify port names the file
//   that plays.
#include "plugin.hpp"
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "convolver_identities.hpp"
#include "filter_file.hpp"
#include "presets.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <lv2/atom/atom.h>
#include <lv2/atom/forge.h>
#include <lv2/atom/util.h>
#include <lv2/buf-size/buf-size.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/log/logger.h>
#include <lv2/options/options.h>
#include <lv2/patch/patch.h>
#include <lv2/state/state.h>
#include <lv2/urid/urid.h>
#include <lv2/worker/worker.h>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace convolver::lv2 {
namespace {
constexpr std::size_t max_channels = identity::max_channels;
constexpr std::size_t layout_count = identity::fixed_layouts.size();
static_assert(std::atomic<int>::is_always_lock_free && std::atomic<bool>::is_always_lock_free &&
              std::atomic<std::uint64_t>::is_always_lock_free);

// What run() hands the worker; the host copies it. The path follows,
// `length` bytes without a NUL.
struct Request {
    std::uint64_t generation = 0;
    std::uint32_t length = 0;
    std::uint32_t too_long = 0; // The path was longer than max_path_bytes and is left out.
};
enum class Outcome : std::uint32_t { installed, superseded, failed };
// What work() hands back to work_response(). The installed filter's path
// follows, `length` bytes without a NUL (empty when it came from state).
struct Response {
    std::uint64_t generation = 0; // The request answered.
    std::uint64_t installed = 0;  // The generation of the filter that plays.
    std::uint32_t length = 0;
    Outcome outcome = Outcome::failed;
};

struct Uris {
    LV2_URID state_key = 0, filter_key = 0, chunk = 0, path = 0, string = 0, urid = 0;
    LV2_URID patch_set = 0, patch_get = 0, patch_property = 0, patch_value = 0;
};

struct Instance {
    HostLayout layout;
    std::size_t max_block = default_max_block;
    Uris uris;
    LV2_Log_Logger logger{};
    LV2_Atom_Forge forge{};
    const LV2_Worker_Schedule* schedule = nullptr; // Absent: the host has no worker.
    // Ports, as connected by the host (audio thread only).
    const float* enabled = nullptr;
    float* latency = nullptr;
    float* status = nullptr;
    std::array<const float*, max_channels> in{};
    std::array<float*, max_channels> out{};
    const LV2_Atom_Sequence* control_in = nullptr;
    LV2_Atom_Sequence* notify_out = nullptr;

    // Control side, under `control`.
    std::mutex control;
    HostFilter filter{max_channels};
    std::unique_ptr<HostProcessor> host; // Exists while active.
    // The filter that plays: the request that installed it and its file
    // (empty when it came from state alone). Generations only increase.
    std::uint64_t installed = 0;
    std::string installed_path;
    std::uint64_t handled = 0; // The newest request whose outcome is recorded.
    bool closing = false;
    void* pause_context = nullptr;
    void (*pause)(void*, WorkerPoint) = nullptr;

    // Shared between the audio and control sides.
    std::atomic<int> shown{static_cast<int>(Status::no_filter)}; // Status of the record.
    std::atomic<bool> bypass{false}; // Last seen on the enabled port; applied at activation.
    // The newest request: run() (patch:Set) and restore() count it.
    std::atomic<std::uint64_t> requested{0};
    // A request run() could not hand to the worker, for the next control
    // call to record: its generation times two, plus one without a worker.
    std::atomic<std::uint64_t> dropped{0};
    std::atomic<bool> stopping{false}; // Set at cleanup: later answers do nothing.
    // True while the processor plays what an instance without a filter plays
    // (identity for equal counts), so a first load that run() refuses must
    // mute it at once (HostProcessor::mute) rather than wait for the next
    // control call. Set at activation and after a successful restore, and
    // cleared before the worker publishes a filter, so run() never mutes a
    // filter it has already started playing. It can be left true only while
    // silence plays (a failed first load), where muting changes nothing.
    std::atomic<bool> unfiltered{true};
    // DIAG-1: whether a fault report is queued. The baseline Fault is
    // measured from is the processor's (HostProcessor::clear_faults(), which
    // HostFilter::load calls on an accepted load; a new processor starts at 0).
    std::atomic<bool> fault_report_queued{false};
    // The fault total the last report covers: set by run() when it schedules
    // one and by work() when it has logged, never both at once (one report at
    // a time); reset with the processor.
    std::atomic<std::uint64_t> faults_reported{0};

    // Audio side: run() and work_response(), and restore(), which never
    // runs at the same time as them.
    std::uint64_t answered = 0;     // The newest request answered.
    std::uint64_t refused_here = 0; // A request run() answered itself with "load failed".
    std::uint64_t reported = 0;     // The installed generation the notify port last named.
    std::array<char, max_path_bytes> reported_path{};
    std::size_t reported_length = 0;
    bool notify = false; // Send patch:Set with the reported path at the end of run().

    void update_status() {
        auto s = Status::no_filter;
        // A failure stays recorded until a later load succeeds (PLUG-8).
        // failed() also covers a record that could not be built.
        if (filter.failure() || filter.failed())
            s = Status::load_failed;
        else if (filter.filter())
            s = Status::running;
        shown.store(static_cast<int>(s), std::memory_order_relaxed);
    }
    // DIAG-1, control side: explains faults the processor counted that no
    // earlier call explained.
    // Returns the processor's fault total it explained up to.
    std::uint64_t log_faults() noexcept {
        if (!host)
            return 0;
        const auto totals = host->faults();
        try {
            if (const auto fresh = filter.observe(totals); fresh.any())
                log_error(fault_message(fresh));
        } catch (...) { // Out of memory for the message: the status port still shows it.
        }
        return totals.total();
    }
    // Why the newest request failed, or the saved failure it restored.
    void log_rejection() {
        if (const auto& r = filter.rejection())
            log_error(r->message);
    }
    void log_error(const std::string& message) {
        // Without the host's log feature this prints to stderr, which is
        // also where LV2 hosts send a plug-in's unlogged diagnostics.
        lv2_log_error(&logger, "Convolver: %s\n", message.c_str());
    }
    // After a rejection: a failed first load plays silence rather than the
    // unfiltered input.
    void silence_failed_first_load(bool was_failed) {
        if (host && filter.failed() && !was_failed) {
            try {
                host->replace(unfiltered_configuration(layout, true));
            } catch (...) {
            }
        }
    }
    // Records a request run() could not hand to the worker, unless a newer
    // one has been recorded since. Control side, under the lock.
    void record_dropped() {
        const auto d = dropped.exchange(0, std::memory_order_acq_rel);
        const auto generation = d >> 1;
        if (!d || generation <= handled)
            return;
        handled = generation;
        const bool was_failed = filter.failed();
        filter.reject(HostFailureKind::filter_file,
                      (d & 1) != 0
                          ? "This host does not give Convolver a worker thread (the LV2 worker "
                            "extension), which loading a filter file needs. Load a preset or a "
                            "saved session instead, or use a host that supports the worker."
                          : "The host could not start loading the filter file (its worker queue "
                            "was full). Choose the file again.",
                      {HostFilterSource::file, "", generation});
        silence_failed_first_load(was_failed);
        update_status();
        log_rejection();
    }
    // The installed filter's path, for the notify port. Audio side, or
    // restore().
    void report(std::uint64_t generation, const char* path, std::size_t length) {
        if (generation <= reported)
            return;
        reported = generation;
        reported_length = std::min(length, max_path_bytes);
        std::memcpy(reported_path.data(), path, reported_length);
        notify = true;
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
    auto* map = static_cast<LV2_URID_Map*>(const_cast<void*>(feature(features, LV2_URID__map)));
    if (index == layout_count || !map || !std::isfinite(rate) || rate < 1 || rate > 768000 ||
        std::floor(rate) != rate)
        return nullptr;
    try {
        auto p = std::make_unique<Instance>();
        const auto& layout = identity::fixed_layouts[index];
        p->layout = {static_cast<int>(rate), static_cast<std::size_t>(layout.inputs),
                     static_cast<std::size_t>(layout.outputs)};
        const auto id = [&](const char* uri) { return map->map(map->handle, uri); };
        auto& u = p->uris;
        u.state_key = id(presets::lv2_state_uri().c_str());
        u.filter_key = id((std::string(identity::lv2_base_uri) + filter_suffix).c_str());
        u.chunk = id(LV2_ATOM__Chunk);
        u.path = id(LV2_ATOM__Path);
        u.string = id(LV2_ATOM__String);
        u.urid = id(LV2_ATOM__URID);
        u.patch_set = id(LV2_PATCH__Set);
        u.patch_get = id(LV2_PATCH__Get);
        u.patch_property = id(LV2_PATCH__property);
        u.patch_value = id(LV2_PATCH__value);
        lv2_atom_forge_init(&p->forge, map);
        lv2_log_logger_init(
            &p->logger, map,
            static_cast<LV2_Log_Log*>(const_cast<void*>(feature(features, LV2_LOG__log))));
        p->schedule =
            static_cast<const LV2_Worker_Schedule*>(feature(features, LV2_WORKER__schedule));
        const auto max_length = id(LV2_BUF_SIZE__maxBlockLength);
        const auto atom_int = id(LV2_ATOM__Int);
        const auto atom_long = id(LV2_ATOM__Long);
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
    else if (port == control_port(ni, no))
        p.control_in = static_cast<const LV2_Atom_Sequence*>(data);
    else if (port == notify_port(ni, no))
        p.notify_out = static_cast<LV2_Atom_Sequence*>(data);
}

void record_activation_failure(Instance& p, const char* why) noexcept {
    try {
        p.filter.fail_activation(why);
    } catch (...) { // Out of memory for the message: nothing more to keep.
    }
}

void activate_locked(Instance& p) {
    p.record_dropped();
    p.log_faults(); // Before the processor that counted them goes.
    p.host.reset();
    try {
        // A fresh processor: the filter plays from the first frame, its tail
        // and history start afresh, and LV2's activate() is also its reset.
        p.host = std::make_unique<HostProcessor>(p.filter.activate(p.layout), p.max_block);
    } catch (const std::exception& e) {
        record_activation_failure(p, e.what());
    } catch (...) {
        record_activation_failure(p, "the processor could not be built.");
    }
    if (!p.host) {
        try {
            p.host = std::make_unique<HostProcessor>(unfiltered_configuration(p.layout, true),
                                                     p.max_block);
        } catch (...) {
        }
        p.log_error(p.filter.error());
    }
    if (p.host) {
        p.host->set_bypass(p.bypass.load(std::memory_order_relaxed));
        p.host->reset(); // Start fully bypassed or fully wet, without a ramp.
    }
    p.unfiltered.store(!p.filter.filter(), std::memory_order_release);
    // A report the host dropped (some flush queued work on deactivation)
    // must not block later ones.
    p.fault_report_queued.store(false, std::memory_order_release);
    p.faults_reported.store(0, std::memory_order_relaxed);
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

// --- Loading a filter file ------------------------------------------------------

void pause_at(const Instance& p, WorkerPoint point) {
    if (p.pause)
        p.pause(p.pause_context, point);
}

// work()'s part under the lock: the three steps of the plan's "stale requests".
// `damaged`: the host passed a request whose length does not match.
Outcome load_request(Instance& p, const Request& r, const std::string& path, bool damaged) {
    const auto newest = [&] { return p.requested.load(std::memory_order_acquire); };
    if (r.generation < newest())
        return Outcome::superseded; // Rapid loads coalesce to the newest.
    const HostRequest request{HostFilterSource::file, path, r.generation};
    // Copied before anything is published, so recording what plays below
    // cannot run out of memory. Running out here, like the copy above, leaves
    // work() to answer with nothing recorded or changed.
    std::string kept_path = path;
    p.log_faults(); // Explained under the filter that played; a load clears them.
    const bool was_failed = p.filter.failed();
    const auto failed = [&] {
        p.handled = std::max(p.handled, r.generation);
        p.silence_failed_first_load(was_failed);
        p.update_status();
        p.log_rejection();
        return Outcome::failed;
    };
    if (damaged) {
        p.filter.reject(HostFailureKind::filter_file,
                        "The host damaged the filter request on its way to its worker thread, so "
                        "the file was not loaded. Choose the file again; if this repeats, report "
                        "it to the host's developers.",
                        request);
        return failed();
    }
    if (r.too_long) {
        p.filter.reject(HostFailureKind::filter_file,
                        "The filter file's path is longer than " + std::to_string(max_path_bytes) +
                            " bytes. Move the file to a folder with a shorter path.",
                        request);
        return failed();
    }
    // The step convolver-preset uses, for this instance's rate and channels
    // (a list chooses its entry as in the CLI; PLUG-9).
    FilterFileFailure why;
    auto next = read_filter_file(path, p.layout, why);
    pause_at(p, WorkerPoint::read);
    // 1. Already overtaken: stop before recording a failure or FFT planning.
    if (r.generation < newest())
        return Outcome::superseded;
    if (!next) {
        p.filter.reject(why.kind, std::move(why.message), request);
        return failed();
    }
    try {
        p.filter.load(
            std::move(*next), p.layout,
            [&](const Configuration& c) {
                if (!p.host) { // Inactive: the next activation installs it.
                    if (r.generation != newest())
                        throw SupersededRequest();
                    return;
                }
                std::optional<HostProcessor::Replacement> prepared;
                try {
                    prepared.emplace(p.host->prepare(c));
                } catch (...) { // Out of memory: no failure for an overtaken request.
                    if (r.generation != newest())
                        throw SupersededRequest();
                    throw;
                }
                pause_at(p, WorkerPoint::prepared);
                // 2. The last check, after preparation.
                if (r.generation != newest())
                    throw SupersededRequest();
                pause_at(p, WorkerPoint::checked);
                // 3. Publish. From here on run() must not mute: the filter is
                // accepted (HostFilter::load commits without throwing).
                p.unfiltered.store(false, std::memory_order_release);
                p.host->publish(std::move(*prepared));
            },
            request, p.host.get()); // run() goes on during preparation: one fault snapshot.
    } catch (const SupersededRequest&) {
        return Outcome::superseded;
    } catch (...) {
        return failed();
    }
    p.installed = r.generation;
    p.installed_path = std::move(kept_path);
    p.handled = std::max(p.handled, r.generation);
    p.update_status();
    return Outcome::installed;
}

LV2_Worker_Status work(LV2_Handle handle, LV2_Worker_Respond_Function respond,
                       LV2_Worker_Respond_Handle target, std::uint32_t size, const void* data) {
    auto& p = *static_cast<Instance*>(handle);
    Request r;
    if (size < sizeof r)
        return LV2_WORKER_ERR_UNKNOWN;
    std::memcpy(&r, data, sizeof r);
    if (r.generation == 0) { // DIAG-1: run()'s fault report, not a request; no answer.
        try {
            std::lock_guard lock(p.control);
            if (!p.closing) // Faults counted since run() asked are covered too.
                p.faults_reported.store(p.log_faults(), std::memory_order_relaxed);
        } catch (...) {
        }
        p.fault_report_queued.store(false, std::memory_order_release);
        return LV2_WORKER_SUCCESS;
    }
    // A damaged request is still answered, as a failure, so the status does
    // not stay at Loading.
    const bool damaged = size - sizeof r != r.length || r.length > max_path_bytes;
    // If memory runs out, the answer still goes back, without a path.
    Response answer;
    answer.generation = r.generation;
    std::vector<std::byte> reply;
    try {
        const std::string path =
            damaged ? std::string()
                    : std::string(static_cast<const char*>(data) + sizeof r, r.length);
        std::lock_guard lock(p.control);
        if (p.closing)
            return LV2_WORKER_SUCCESS;
        p.record_dropped();
        answer.outcome = load_request(p, r, path, damaged);
        if (p.host)
            p.host->collect();
        answer.installed = p.installed;
        answer.length =
            static_cast<std::uint32_t>(std::min(p.installed_path.size(), max_path_bytes));
        reply.resize(sizeof answer + answer.length);
        std::memcpy(reply.data(), &answer, sizeof answer);
        std::memcpy(reply.data() + sizeof answer, p.installed_path.data(), answer.length);
    } catch (...) {
        answer.installed = 0;
        answer.length = 0;
        reply.clear();
    }
    if (reply.empty())
        return respond(target, sizeof answer, &answer);
    return respond(target, static_cast<std::uint32_t>(reply.size()), reply.data());
}

LV2_Worker_Status work_response(LV2_Handle handle, std::uint32_t size, const void* data) {
    auto& p = *static_cast<Instance*>(handle);
    Response r;
    if (size < sizeof r || p.stopping.load(std::memory_order_relaxed))
        return LV2_WORKER_SUCCESS;
    std::memcpy(&r, data, sizeof r);
    if (size - sizeof r != r.length || r.length > max_path_bytes)
        return LV2_WORKER_ERR_UNKNOWN;
    // The status follows the newest request only; run() then shows the
    // record's status, which work() updated before it answered.
    if (r.generation == p.requested.load(std::memory_order_relaxed) && r.generation > p.answered)
        p.answered = r.generation;
    // The path follows the filter that plays, whichever answer reports it.
    p.report(r.installed, static_cast<const char*>(data) + sizeof r, r.length);
    return LV2_WORKER_SUCCESS;
}

const LV2_Worker_Interface worker_interface{work, work_response, nullptr};

// --- Audio thread -----------------------------------------------------------------

// A request run() answers itself: no worker, or the host could not queue it.
// An instance with no filter accepted fails its first load here, so it plays
// silence from now on, not its unfiltered input (the shared contract); the
// next control call records why. An accepted filter keeps playing.
void refuse_request(Instance& p, std::uint64_t generation, bool no_worker) {
    p.answered = p.refused_here = generation;
    p.dropped.store(generation << 1 | (no_worker ? 1 : 0), std::memory_order_release);
    if (!p.host)
        return;
    // The count first, then the flag: the worker clears the flag before it
    // publishes, so a filter it publishes after this sees the flag clear or
    // is newer than the count, and is never silenced. An unfiltered
    // configuration that restore() published and run() has not yet picked up
    // is silenced as well.
    const auto published = p.host->publications();
    if (p.unfiltered.load(std::memory_order_acquire))
        p.host->mute(published);
}

void request_file(Instance& p, const LV2_Atom* value) {
    const auto generation = p.requested.load(std::memory_order_relaxed) + 1;
    p.requested.store(generation, std::memory_order_release);
    if (!p.schedule) {
        refuse_request(p, generation, true);
        return;
    }
    const auto* text = static_cast<const char*>(LV2_ATOM_BODY_CONST(value));
    const auto length = strnlen(text, value->size);
    // On the stack: run() never allocates. The host copies what it needs.
    std::array<std::byte, sizeof(Request) + max_path_bytes> buffer;
    Request r;
    r.generation = generation;
    r.too_long = length > max_path_bytes ? 1 : 0;
    r.length = r.too_long ? 0 : static_cast<std::uint32_t>(length);
    std::memcpy(buffer.data(), &r, sizeof r);
    std::memcpy(buffer.data() + sizeof r, text, r.length);
    if (p.schedule->schedule_work(p.schedule->handle,
                                  static_cast<std::uint32_t>(sizeof r + r.length),
                                  buffer.data()) != LV2_WORKER_SUCCESS)
        refuse_request(p, generation, false);
}

void read_messages(Instance& p) {
    if (!p.control_in)
        return;
    const auto& u = p.uris;
    // Walk the host's buffer as bytes. The events follow the sequence header,
    // and each must fit inside the size the host reports.
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(p.control_in);
    const std::size_t end = sizeof(LV2_Atom) + p.control_in->atom.size;
    std::size_t at = sizeof(LV2_Atom_Sequence);
    while (at + sizeof(LV2_Atom_Event) <= end) {
        const auto* event = reinterpret_cast<const LV2_Atom_Event*>(bytes + at);
        const std::size_t body_end = at + sizeof(LV2_Atom_Event) + event->body.size;
        if (body_end > end)
            break;
        at += sizeof(LV2_Atom_Event) + lv2_atom_pad_size(event->body.size);
        if (!lv2_atom_forge_is_object_type(&p.forge, event->body.type))
            continue;
        const auto* object = reinterpret_cast<const LV2_Atom_Object*>(&event->body);
        const LV2_Atom* property = nullptr;
        const LV2_Atom* value = nullptr;
        lv2_atom_object_get(object, u.patch_property, &property, u.patch_value, &value, 0);
        const bool ours = property && property->type == u.urid &&
                          reinterpret_cast<const LV2_Atom_URID*>(property)->body == u.filter_key;
        if (object->body.otype == u.patch_set && ours && value &&
            (value->type == u.path || value->type == u.string))
            request_file(p, value);
        else if (object->body.otype == u.patch_get && (!property || ours))
            p.notify = true; // Answer with the file that plays.
    }
}

// The notify port's sequence: patch:Set naming the file that plays, when
// there is news or a patch:Get to answer. Kept pending if the host's buffer
// is too small this time.
void write_notification(Instance& p) {
    if (!p.notify_out)
        return;
    auto& forge = p.forge;
    const auto capacity = p.notify_out->atom.size;
    lv2_atom_forge_set_buffer(&forge, reinterpret_cast<std::uint8_t*>(p.notify_out), capacity);
    LV2_Atom_Forge_Frame sequence;
    if (!lv2_atom_forge_sequence_head(&forge, &sequence, 0))
        return;
    if (p.notify) {
        const auto& u = p.uris;
        LV2_Atom_Forge_Frame object;
        bool written = lv2_atom_forge_frame_time(&forge, 0) &&
                       lv2_atom_forge_object(&forge, &object, 0, u.patch_set);
        written = written && lv2_atom_forge_key(&forge, u.patch_property) &&
                  lv2_atom_forge_urid(&forge, u.filter_key) &&
                  lv2_atom_forge_key(&forge, u.patch_value) &&
                  lv2_atom_forge_path(&forge, p.reported_path.data(),
                                      static_cast<std::uint32_t>(p.reported_length));
        if (!written) { // Too small: an empty sequence, and another try next time.
            p.notify_out->atom.type = forge.Sequence;
            p.notify_out->atom.size = sizeof(LV2_Atom_Sequence_Body);
            p.notify_out->body.unit = 0;
            p.notify_out->body.pad = 0;
            return;
        }
        lv2_atom_forge_pop(&forge, &object);
        p.notify = false;
    }
    lv2_atom_forge_pop(&forge, &sequence);
}

// The processor's fault total, while it exists.
std::uint64_t fault_total(const Instance& p) {
    return p.host ? p.host->faults().total() : 0;
}

Status shown_status(const Instance& p) {
    if (p.requested.load(std::memory_order_relaxed) > p.answered)
        return Status::loading;
    if (p.answered && p.refused_here == p.answered)
        return Status::load_failed;
    const auto shown = static_cast<Status>(p.shown.load(std::memory_order_relaxed));
    // A load failure comes first, as host_status() orders them.
    if (shown != Status::load_failed && p.host && p.host->faults_since_clear())
        return Status::fault;
    return shown;
}

// DIAG-1: new faults are explained by work(), outside the audio thread. One
// report at a time; a host without a worker explains them at the next save,
// restore, activation or deactivation.
void report_faults(Instance& p) {
    const auto total = fault_total(p);
    // Only for faults no report was scheduled for yet.
    if (!p.schedule || !p.host || !p.host->faults_since_clear() ||
        total <= p.faults_reported.load(std::memory_order_relaxed) ||
        p.fault_report_queued.exchange(true, std::memory_order_acq_rel))
        return;
    Request r; // Generation 0: requests are numbered from 1.
    if (p.schedule->schedule_work(p.schedule->handle, sizeof r, &r) == LV2_WORKER_SUCCESS)
        p.faults_reported.store(total, std::memory_order_relaxed);
    else
        p.fault_report_queued.store(false, std::memory_order_release);
}

void run(LV2_Handle handle, std::uint32_t frames) {
    auto& p = *static_cast<Instance*>(handle);
    const auto ni = p.layout.inputs, no = p.layout.outputs;
    read_messages(p);
    if (p.latency)
        *p.latency = static_cast<float>(p.host ? p.host->latency_frames() : 256);
    // lv2:enabled is 1 to process and 0 to bypass; anything else counts as on.
    const bool bypass = p.enabled && *p.enabled < .5f;
    p.bypass.store(bypass, std::memory_order_relaxed);
    if (!p.host) {
        for (std::size_t c = 0; c < no; ++c)
            if (p.out[c])
                std::fill_n(p.out[c], frames, 0.f);
    } else {
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
    // After processing, so a fault in this call shows at once.
    if (p.status)
        *p.status = static_cast<float>(shown_status(p));
    report_faults(p);
    write_notification(p);
}

void deactivate(LV2_Handle handle) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        p.record_dropped();
        p.log_faults();
        p.host.reset();
    } catch (...) {
    }
}

void cleanup(LV2_Handle handle) {
    auto* p = static_cast<Instance*>(handle);
    try {
        // Lets a save() or work() already in progress finish first. LV2
        // requires the host to stop calling work() before cleanup(): one
        // that starts after this returns finds the instance freed.
        std::lock_guard lock(p->control);
        p->closing = true;
        p->stopping.store(true, std::memory_order_relaxed);
        p->host.reset();
    } catch (...) {
    }
    delete p;
}

// --- State ------------------------------------------------------------------------

// state:mapPath and state:freePath, when the host passes them to save() or
// restore(). Without mapPath, paths are stored and read as they are.
class Paths {
  public:
    explicit Paths(const LV2_Feature* const* features)
        : map_(static_cast<const LV2_State_Map_Path*>(feature(features, LV2_STATE__mapPath))),
          free_(static_cast<const LV2_State_Free_Path*>(feature(features, LV2_STATE__freePath))) {}
    bool mapped() const {
        return map_ != nullptr;
    }
    std::string abstract(const std::string& absolute) const {
        return map_ ? take(map_->abstract_path(map_->handle, absolute.c_str()), absolute)
                    : absolute;
    }
    std::string absolute(const std::string& abstract) const {
        return map_ ? take(map_->absolute_path(map_->handle, abstract.c_str()), abstract)
                    : abstract;
    }

  private:
    std::string take(char* converted, const std::string& fallback) const {
        if (!converted)
            return fallback;
        std::string result;
        try {
            result = converted;
        } catch (...) {
            release(converted);
            throw;
        }
        release(converted);
        return result;
    }
    // As the host asks: with state:freePath if given, otherwise free().
    void release(char* converted) const {
        if (free_)
            free_->free_path(free_->handle, converted);
        else
            std::free(converted);
    }
    const LV2_State_Map_Path* map_;
    const LV2_State_Free_Path* free_;
};

LV2_State_Status save(LV2_Handle handle, LV2_State_Store_Function store, LV2_State_Handle state,
                      std::uint32_t, const LV2_Feature* const* features) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        p.record_dropped();
        p.log_faults();
        HostState saved;
        saved.configuration = p.filter.filter();
        saved.failed = p.filter.failed(); // Reopening after a failed load stays silent.
        // Bypass is the lv2:enabled port, which the host saves itself.
        const auto bytes = encode_host_state(saved);
        if (p.host)
            p.host->collect();
        const auto status = store(state, p.uris.state_key, bytes.data(), bytes.size(), p.uris.chunk,
                                  LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
        if (status != LV2_STATE_SUCCESS || p.installed_path.empty())
            return status;
        // The file the filter came from, mapped so the session can move.
        // Restore prefers the embedded copy above.
        const auto path = Paths(features).abstract(p.installed_path);
        return store(state, p.uris.filter_key, path.c_str(), path.size() + 1, p.uris.path,
                     LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
    } catch (...) {
        return LV2_STATE_ERR_UNKNOWN;
    }
}

// The embedded host state. Records any failure under `request` and sets the
// status restore() then reports; installs nothing.
std::optional<HostState> read_state(Instance& p, const void* value, std::size_t size,
                                    std::uint32_t type, const HostRequest& request,
                                    LV2_State_Status& status) {
    status = LV2_STATE_ERR_UNKNOWN;
    if (type != p.uris.chunk) {
        status = LV2_STATE_ERR_BAD_TYPE;
        p.filter.reject(HostFailureKind::state_unreadable,
                        "The saved state has the wrong type; it should be a chunk of bytes "
                        "(atom:Chunk, or xsd:base64Binary in a preset file).",
                        request);
        return std::nullopt;
    }
    try {
        if (size > max_host_state_bytes)
            throw std::invalid_argument("it is larger than " +
                                        std::to_string(max_host_state_bytes) + " bytes");
        return decode_host_state(std::span(static_cast<const std::byte*>(value), size));
    } catch (const NewerHostState& e) {
        p.filter.reject(HostFailureKind::state_newer,
                        std::string("The saved state could not be read: ") + e.what(), request);
    } catch (const std::exception& e) {
        p.filter.reject(HostFailureKind::state_unreadable,
                        std::string("The saved state could not be read: ") + e.what(), request);
    }
    return std::nullopt;
}

// The saved filter path: an atom:Path (mapped back with state:mapPath), an
// atom:String, or PipeWire's bare value (type 0, maybe without a NUL).
std::optional<std::string> read_path(Instance& p, const void* value, std::size_t size,
                                     std::uint32_t type, const Paths& paths,
                                     const HostRequest& request) {
    if (type != p.uris.path && type != p.uris.string && type != 0) {
        p.filter.reject(HostFailureKind::state_unreadable,
                        "The saved filter path has the wrong type; it should be a path "
                        "(atom:Path) or a string.",
                        request);
        return std::nullopt;
    }
    const auto* text = static_cast<const char*>(value);
    auto path = std::string(text, strnlen(text, size));
    if (type == p.uris.path && paths.mapped())
        path = paths.absolute(path);
    if (path.size() > max_path_bytes) {
        p.filter.reject(HostFailureKind::filter_file,
                        "The saved filter path is longer than " + std::to_string(max_path_bytes) +
                            " bytes. Move the file to a folder with a shorter path.",
                        request);
        return std::nullopt;
    }
    return path;
}

LV2_State_Status restore_locked(Instance& p, LV2_State_Retrieve_Function retrieve,
                                LV2_State_Handle state, const LV2_Feature* const* features) {
    p.record_dropped();
    p.log_faults(); // Explained under the filter that played; a load clears them.
    std::size_t state_size = 0, path_size = 0;
    std::uint32_t state_type = 0, path_type = 0, flags = 0;
    const void* embedded = retrieve(state, p.uris.state_key, &state_size, &state_type, &flags);
    const void* saved_path = retrieve(state, p.uris.filter_key, &path_size, &path_type, &flags);
    // A request like any other, so answers to older ones no longer count.
    // run() never runs during restore(), so the audio side is ours here.
    const auto generation = p.requested.load(std::memory_order_relaxed) + 1;
    p.requested.store(generation, std::memory_order_release);
    p.answered = generation;
    p.handled = generation;
    const bool was_failed = p.filter.failed();
    const HostRequest from_state{HostFilterSource::state, "", generation};
    auto status = LV2_STATE_SUCCESS;
    std::optional<std::string> path;
    if (saved_path)
        path = read_path(p, saved_path, path_size, path_type, Paths(features), from_state);
    // LV2 fixes the rate and channels at instantiation, so the host's
    // configuration is always known and checked here. While active, a
    // prepared replacement is published (installed directly if no audio has
    // run yet); inactive, the next activation installs it.
    const auto install = [&](const Configuration& c) {
        if (p.host)
            p.host->replace(c);
    };
    // The path that plays if a load succeeds, copied before anything is
    // published so that recording it cannot run out of memory. Running out
    // here, as in read_path(), returns an error with nothing changed.
    std::string kept_path = path.value_or("");
    bool loaded = false;
    if (!embedded && !saved_path) {
        // Neither property: LV2 requires falling back to defaults ("This
        // allows the host to reset the plugin state with an empty map"), so
        // this returns to an empty instance, clears any failure and path, and
        // supersedes loads still outstanding.
        try {
            p.filter.load(HostState{}, p.layout, install, from_state, p.host.get());
            loaded = true;
        } catch (...) {
        }
    }
    if (embedded) {
        if (auto next = read_state(p, embedded, state_size, state_type, from_state, status)) {
            try {
                // Bypass is the lv2:enabled port, which the host restores
                // itself; the state's bypass flag is ignored.
                p.filter.load(std::move(*next), p.layout, install, from_state, p.host.get());
                loaded = true;
            } catch (...) {
                status = LV2_STATE_ERR_UNKNOWN;
            }
        }
    }
    if (!loaded && path) { // No usable embedded copy: the file.
        if (embedded)
            p.log_rejection();
        const HostRequest from_file{HostFilterSource::file, *path, generation};
        FilterFileFailure why;
        if (auto next = read_filter_file(*path, p.layout, why)) {
            try {
                p.filter.load(std::move(*next), p.layout, install, from_file, p.host.get());
                loaded = true;
            } catch (...) {
            }
        } else {
            p.filter.reject(why.kind, std::move(why.message), from_file);
        }
    }
    if (loaded) {
        p.installed = generation;
        p.installed_path = std::move(kept_path);
        p.unfiltered.store(!p.filter.filter(), std::memory_order_release);
        status = LV2_STATE_SUCCESS;
    } else {
        p.silence_failed_first_load(was_failed);
        if (status == LV2_STATE_SUCCESS)
            status = LV2_STATE_ERR_UNKNOWN;
    }
    p.update_status();
    if (!loaded || p.filter.failed())
        p.log_rejection(); // A rejection, or a saved failure: silence again, explained.
    p.report(p.installed, p.installed_path.data(), p.installed_path.size());
    return status;
}

LV2_State_Status restore(LV2_Handle handle, LV2_State_Retrieve_Function retrieve,
                         LV2_State_Handle state, std::uint32_t,
                         const LV2_Feature* const* features) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        return restore_locked(p, retrieve, state, features);
    } catch (...) {
        return LV2_STATE_ERR_UNKNOWN;
    }
}

const LV2_State_Interface state_interface{save, restore};

// --- Test-only access (plugin.hpp) ------------------------------------------------

void read_failure(void* handle, void* context, void (*receive)(void*, const HostFailure*)) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        const auto& f = p.filter.failure();
        receive(context, f ? &*f : nullptr);
    } catch (...) {
    }
}
const TestFailureAccess test_failure{read_failure};

void set_pause(void* handle, void* context, void (*pause)(void*, WorkerPoint)) {
    auto& p = *static_cast<Instance*>(handle);
    try {
        std::lock_guard lock(p.control);
        p.pause_context = context;
        p.pause = pause;
    } catch (...) {
    }
}
const TestWorkerAccess test_worker{set_pause};

const void* extension_data(const char* uri) {
    if (std::strcmp(uri, LV2_STATE__interface) == 0)
        return &state_interface;
    if (std::strcmp(uri, LV2_WORKER__interface) == 0)
        return &worker_interface;
    if (std::strcmp(uri, test_failure_uri) == 0)
        return &test_failure;
    if (std::strcmp(uri, test_worker_uri) == 0)
        return &test_worker;
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
