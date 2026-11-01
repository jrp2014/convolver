// SPDX-License-Identifier: GPL-2.0-or-later
// LV2 adapter tests through lilv, beyond the shared PLUG-2 cases:
// - the bundle: every PLUG-4 layout, port numbers and properties as plugin.hpp says;
// - instantiation: urid:map required, whole sample rates only;
// - status port, log messages and saved state after good and bad loads;
// - presets written by convolver-preset, and lilv's own state round trip;
// - run() calls longer than bufsz:maxBlockLength (or without it);
// - seeded lifecycle sequences checked against a small model after every step;
// - LV2-2: filter files through the worker (every way a host runs it, failures,
//   rapid loads, the preparation window with paused work, teardown), and state
//   with a path (state:mapPath, PipeWire's bare value, lilv's state files).
#include "../native/oracle.hpp"
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "convolver_identities.hpp"
#include "host.hpp"
#include "host_log.hpp"
#include "plugin.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/patch/patch.h>
#include <lv2/presets/presets.h>
#include <lv2/resize-port/resize-port.h>
#include <lv2/worker/worker.h>
#include <random>
#include <span>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <tuple>

using namespace convolver;
namespace host = convolver::lv2_test;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void check(bool condition, const std::string& what) {
    if (!condition) {
        ++failures;
        std::cout << "FAIL  " << what << '\n';
    }
}
constexpr std::size_t latency = 256;

std::vector<float> read_audio(const fs::path& path) {
    AudioReader reader(path);
    std::vector<float> result, chunk(4096 * reader.info().channels);
    while (const auto frames = reader.read(chunk))
        result.insert(result.end(), chunk.begin(),
                      chunk.begin() + static_cast<std::ptrdiff_t>(frames * reader.info().channels));
    return result;
}

struct Fixture {
    std::string name;
    fs::path source;
    Configuration filter;
    std::vector<float> input; // Interleaved.
};

Configuration wrong_rate(Configuration c) {
    c.sample_rate = c.sample_rate == 44100 ? 48000 : 44100;
    return c;
}
Configuration wrong_outputs(Configuration c) {
    ++c.output_channels;
    if (!c.output_delays.empty())
        c.output_delays.push_back(0);
    return c;
}
// Another valid filter: two frames later, inverted and scaled.
Configuration other(Configuration c) {
    for (auto& p : c.paths) {
        p.impulse.insert(p.impulse.begin(), 2, 0.f);
        for (auto& v : p.impulse)
            v *= -.75f;
    }
    return c;
}
std::vector<std::byte> state_of(const std::optional<Configuration>& c, bool failed = false) {
    HostState s;
    s.configuration = c;
    s.failed = failed;
    return encode_host_state(s);
}
bool same(const std::optional<Configuration>& a, const std::optional<Configuration>& b) {
    if (!a || !b)
        return !a && !b;
    return state_of(a) == state_of(b);
}

// Plays interleaved input (silence past its end) in callbacks of `block`
// frames and returns interleaved output.
std::vector<float> render(host::Plugin& p, const std::vector<float>& input, std::size_t frames,
                          std::size_t block) {
    const auto ni = p.inputs(), no = p.outputs();
    std::vector<std::vector<float>> in(ni, std::vector<float>(block)),
        out(no, std::vector<float>(block));
    std::vector<const float*> ip(ni);
    std::vector<float*> op(no);
    std::vector<float> result(frames * no);
    for (std::size_t done = 0; done < frames;) {
        const auto n = std::min(block, frames - done);
        for (std::size_t c = 0; c < ni; ++c) {
            for (std::size_t t = 0; t < n; ++t) {
                const auto i = (done + t) * ni + c;
                in[c][t] = i < input.size() ? input[i] : 0.f;
            }
            ip[c] = in[c].data();
        }
        for (std::size_t c = 0; c < no; ++c) {
            std::fill(out[c].begin(), out[c].end(), .75f);
            op[c] = out[c].data();
        }
        p.run(ip, op, static_cast<std::uint32_t>(n));
        for (std::size_t c = 0; c < no; ++c)
            for (std::size_t t = 0; t < n; ++t)
                result[(done + t) * no + c] = out[c][t];
        done += n;
    }
    return result;
}

double worst = 0;
// Output against the double-precision oracle, within 1e-5 (PLUG-2's tolerance).
bool matches(const std::vector<float>& actual, const std::vector<double>& expected) {
    for (std::size_t i = 0; i < actual.size(); ++i) {
        const double e = i < expected.size() ? expected[i] : 0;
        const double error = std::abs(actual[i] - e);
        if (!(error <= 1e-5))
            return false;
        worst = std::max(worst, error);
    }
    return true;
}
bool silent(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return x == 0; });
}

std::unique_ptr<host::Plugin> instance(host::World& world, const Configuration& c,
                                       const host::Features& f = {}) {
    auto p = std::make_unique<host::Plugin>(
        world, world.plugin(c.input_channels, c.output_channels), c.sample_rate, f);
    if (!p->instantiated())
        throw std::runtime_error("instantiation failed");
    return p;
}
LV2_URID chunk(host::World& world) {
    return world.urids().map(LV2_ATOM__Chunk);
}

// --- The bundle -------------------------------------------------------------------
void bundle(host::World& world) {
    auto* w = world.get();
    const auto node = [&](const char* uri) { return lilv_new_uri(w, uri); };
    LilvNode* enabled = node(LV2_CORE__enabled);
    LilvNode* latency_designation = node(LV2_CORE__latency);
    LilvNode* reports = node(LV2_CORE__reportsLatency);
    LilvNode* audio = node(LV2_CORE__AudioPort);
    LilvNode* control = node(LV2_CORE__ControlPort);
    LilvNode* input = node(LV2_CORE__InputPort);
    LilvNode* output = node(LV2_CORE__OutputPort);
    LilvNode* state_interface = node(LV2_STATE__interface);
    LilvNode* worker_interface = node(LV2_WORKER__interface);
    LilvNode* atom_port = node(LV2_ATOM__AtomPort);
    LilvNode* patch_message = node(LV2_PATCH__Message);
    LilvNode* patch_writable = node(LV2_PATCH__writable);
    LilvNode* minimum_size = node(LV2_RESIZE_PORT__minimumSize);
    LilvNode* filter = node(host::filter_uri().c_str());
    for (const auto& layout : identity::fixed_layouts) {
        const auto uri = std::string(identity::lv2_base_uri) + std::string(layout.id);
        const auto* p = world.plugin(uri);
        check(p != nullptr, uri + ": in the bundle");
        if (!p)
            continue;
        check(lilv_plugin_verify(p), uri + ": lilv_plugin_verify");
        const auto ni = static_cast<std::uint32_t>(layout.inputs);
        const auto no = static_cast<std::uint32_t>(layout.outputs);
        check(lilv_plugin_get_num_ports(p) == lv2::notify_port(ni, no) + 1, uri + ": port count");
        const auto port = [&](std::uint32_t i) { return lilv_plugin_get_port_by_index(p, i); };
        const auto symbol = [&](std::uint32_t i) {
            return std::string(lilv_node_as_string(lilv_port_get_symbol(p, port(i))));
        };
        check(symbol(lv2::enabled_port) == "enabled" &&
                  lilv_port_is_a(p, port(lv2::enabled_port), control) &&
                  lilv_port_is_a(p, port(lv2::enabled_port), input),
              uri + ": enabled is control input " + std::to_string(lv2::enabled_port));
        check(lilv_plugin_get_port_by_designation(p, input, enabled) == port(lv2::enabled_port),
              uri + ": enabled has lv2:designation lv2:enabled");
        check(symbol(lv2::latency_port) == "latency" &&
                  lilv_port_is_a(p, port(lv2::latency_port), output) &&
                  lilv_port_has_property(p, port(lv2::latency_port), reports),
              uri + ": latency is a reportsLatency output");
        check(lilv_plugin_get_port_by_designation(p, output, latency_designation) ==
                  port(lv2::latency_port),
              uri + ": latency has lv2:designation lv2:latency");
        check(lilv_plugin_has_latency(p) &&
                  lilv_plugin_get_latency_port_index(p) == lv2::latency_port,
              uri + ": lilv finds the latency port");
        check(symbol(lv2::status_port) == "status" &&
                  lilv_port_is_a(p, port(lv2::status_port), output),
              uri + ": status is a control output");
        LilvScalePoints* points = lilv_port_get_scale_points(p, port(lv2::status_port));
        check(points && lilv_scale_points_size(points) == 5, uri + ": five status labels");
        const std::pair<lv2::Status, std::string> labels[] = {
            {lv2::Status::running, "Running"},
            {lv2::Status::no_filter, "No filter"},
            {lv2::Status::load_failed, "Load failed"},
            {lv2::Status::loading, "Loading"},
            {lv2::Status::fault, "Fault"}};
        for (const auto& [value, label] : labels) {
            bool found = false;
            LILV_FOREACH(scale_points, i, points) {
                const auto* point = lilv_scale_points_get(points, i);
                found = found || (lilv_node_as_float(lilv_scale_point_get_value(point)) ==
                                      static_cast<float>(value) &&
                                  lilv_node_as_string(lilv_scale_point_get_label(point)) == label);
            }
            check(found,
                  uri + ": status " + std::to_string(static_cast<int>(value)) + " is " + label);
        }
        lilv_scale_points_free(points);
        for (std::uint32_t c = 0; c < ni + no; ++c) {
            const auto i = lv2::first_audio_port + c;
            const bool is_input = c < ni;
            const auto expected =
                (is_input ? "in_" : "out_") + std::to_string(is_input ? c + 1 : c - ni + 1);
            check(symbol(i) == expected && lilv_port_is_a(p, port(i), audio) &&
                      lilv_port_is_a(p, port(i), is_input ? input : output),
                  uri + ": port " + std::to_string(i) + " is audio " + expected);
        }
        for (const auto& [index, name, is_input] :
             {std::tuple{lv2::control_port(ni, no), "control", true},
              std::tuple{lv2::notify_port(ni, no), "notify", false}}) {
            check(symbol(index) == name && lilv_port_is_a(p, port(index), atom_port) &&
                      lilv_port_is_a(p, port(index), is_input ? input : output) &&
                      lilv_port_supports_event(p, port(index), patch_message),
                  uri + ": " + name + " is an atom port for patch messages");
            LilvNodes* sizes = lilv_port_get_value(p, port(index), minimum_size);
            check(sizes && lilv_nodes_size(sizes) == 1 &&
                      lilv_node_as_int(lilv_nodes_get(sizes, lilv_nodes_begin(sizes))) ==
                          static_cast<int>(lv2::atom_buffer_bytes),
                  uri + ": " + name + " asks for " + std::to_string(lv2::atom_buffer_bytes) +
                      " bytes");
            lilv_nodes_free(sizes);
        }
        LilvNodes* writable =
            lilv_world_find_nodes(w, lilv_plugin_get_uri(p), patch_writable, nullptr);
        check(writable && lilv_nodes_size(writable) == 1 &&
                  lilv_node_equals(lilv_nodes_get(writable, lilv_nodes_begin(writable)), filter),
              uri + ": the filter parameter is patch:writable");
        lilv_nodes_free(writable);
        check(lilv_plugin_has_extension_data(p, worker_interface), uri + ": work:interface");
        LilvNodes* required = lilv_plugin_get_required_features(p);
        check(required && lilv_nodes_size(required) == 1 &&
                  std::string(lilv_node_as_uri(
                      lilv_nodes_get(required, lilv_nodes_begin(required)))) == LV2_URID__map,
              uri + ": requires urid:map and nothing else");
        lilv_nodes_free(required);
        check(lilv_plugin_has_extension_data(p, state_interface), uri + ": state:interface");
        // The library agrees with the Turtle about which URI it serves.
        host::Plugin instance(world, p, 48000);
        check(instance.instantiated() && instance.inputs() == ni && instance.outputs() == no &&
                  std::string(lilv_instance_get_uri(instance.instance())) == uri,
              uri + ": instantiates with its own URI");
    }
    // convolver.ttl, which lilv has now loaded for the plug-ins, describes it.
    {
        LilvNode* range = node("http://www.w3.org/2000/01/rdf-schema#range");
        LilvNode* type = node("http://www.w3.org/1999/02/22-rdf-syntax-ns#type");
        LilvNode* parameter = node(LV2_CORE_PREFIX "Parameter");
        LilvNode* path = node(LV2_ATOM__Path);
        check(lilv_world_ask(w, filter, type, parameter) && lilv_world_ask(w, filter, range, path),
              "the filter parameter is an lv2:Parameter with range atom:Path");
        for (auto* n : {range, type, parameter, path})
            lilv_node_free(n);
    }
    for (auto* n :
         {enabled, latency_designation, reports, audio, control, input, output, state_interface,
          worker_interface, atom_port, patch_message, patch_writable, minimum_size, filter})
        lilv_node_free(n);
}

// --- Instantiation ----------------------------------------------------------------
void instantiation(host::World& world) {
    const auto* p = world.plugin(2, 2);
    host::Features no_map;
    no_map.map = false;
    {
        // DIAG-1: each refusal says why in the host's log.
        host::Plugin refused(world, p, 48000, no_map);
        check(!refused.instantiated() &&
                  refused.log.contains("", "Convolver: Refused to start an instance at 48000 Hz: "
                                           "the host does not provide the urid:map feature"),
              "refuses to run without urid:map, and says why");
    }
    for (const double rate : {0., -48000., 44100.5, 1e9, std::nan("")}) {
        host::Plugin refused(world, p, rate);
        check(!refused.instantiated() &&
                  refused.log.contains(LV2_LOG__Error, "the sample rate must be a whole number"),
              "refuses a sample rate of " + std::to_string(rate) + ", and says why");
    }
    host::Features plain;
    plain.log = false;
    check(host::Plugin(world, p, 44100, plain).instantiated(), "runs without log:log or options");
}

// --- Status, log and saved state --------------------------------------------------
void status_and_log(host::World& world, const Fixture& f) {
    const auto& a = f.filter;
    const auto label = f.name + ": ";
    const auto error = world.urids().map(LV2_LOG__Error);
    (void)error;
    auto p = instance(world, a);
    p->activate();
    p->run_empty();
    check(p->latency == 256, label + "latency port reports 256");
    check(p->status == static_cast<float>(lv2::Status::no_filter),
          label + "empty: status no filter");
    check(p->save().properties == 1 && !decode_host_state(p->save().bytes).configuration,
          label + "empty: saves one property with no filter");
    // DIAG-1: a save the host refuses is logged, as hosts may not report it.
    p->store_fails = true;
    check(p->save().status != LV2_STATE_SUCCESS &&
              p->log.contains(LV2_LOG__Error, convolver::save_failure_message),
          label + "failed save is logged");
    p->store_fails = false;

    check(p->restore(state_of(wrong_rate(a)), chunk(world)) != LV2_STATE_SUCCESS,
          label + "first load at the wrong rate is rejected");
    p->run_empty();
    check(p->status == static_cast<float>(lv2::Status::load_failed),
          label + "failed first load: status load failed");
    check(p->log.contains(LV2_LOG__Error, "does not resample"),
          label + "failed first load: the explanation reaches log:log");
    {
        const auto r = p->failure();
        check(r && r->kind == HostFailureKind::sample_rate && r->request.number == 1 && r->silent &&
                  p->log.contains(LV2_LOG__Error, r->message),
              label + "failed first load: recorded, and the log carries its message");
    }
    {
        const auto s = decode_host_state(p->save().bytes);
        check(s.failed && !s.configuration, label + "failed first load is saved as a failure");
    }
    check(silent(render(*p, f.input, 2000, 300)), label + "failed first load plays silence");

    check(p->restore(state_of(a), chunk(world)) == LV2_STATE_SUCCESS, label + "valid load");
    p->run_empty();
    check(p->status == static_cast<float>(lv2::Status::running), label + "status running");
    check(!p->failure(), label + "a valid load clears the failure");

    p->log.messages.clear();
    check(p->restore(state_of(wrong_outputs(a)), chunk(world)) != LV2_STATE_SUCCESS,
          label + "replacement with the wrong outputs is rejected");
    p->run_empty();
    check(p->status == static_cast<float>(lv2::Status::load_failed),
          label + "failed replacement: status load failed");
    check(p->log.contains(LV2_LOG__Error, "previous filter is kept"),
          label + "failed replacement: logged, previous filter kept");
    check(p->failure() && p->failure()->kind == HostFailureKind::channels &&
              p->failure()->request.number == 3 && !p->failure()->silent,
          label + "failed replacement: recorded as channels, not silent");
    p->deactivate();
    p->activate();
    p->run_empty();
    check(p->failure() && p->failure()->request.number == 3 &&
              p->status == static_cast<float>(lv2::Status::load_failed),
          label + "failed replacement: still recorded after reactivation");
    check(same(decode_host_state(p->save().bytes).configuration, a),
          label + "failed replacement: saved state is the running filter");

    p->log.messages.clear();
    check(p->restore(state_of(a), world.urids().map(LV2_ATOM__String)) == LV2_STATE_ERR_BAD_TYPE,
          label + "state of the wrong type is rejected");
    check(p->log.contains(LV2_LOG__Error, "wrong type"), label + "wrong type is logged");
    check(p->failure() && p->failure()->kind == HostFailureKind::state_unreadable &&
              p->failure()->request.number == 4,
          label + "wrong type: recorded as unreadable state");
    const std::vector<std::byte> garbage(40, std::byte{0x5a});
    p->log.messages.clear();
    check(p->restore(garbage, chunk(world)) != LV2_STATE_SUCCESS, label + "corrupt state rejected");
    check(p->log.contains(LV2_LOG__Error, "could not be read"), label + "corrupt state is logged");
    check(p->failure() && p->failure()->kind == HostFailureKind::state_unreadable &&
              p->failure()->request.number == 5,
          label + "corrupt state: recorded as unreadable state");
    const auto newer =
        std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 4, 0, 0, 0, 0, 0, 0, 0});
    p->log.messages.clear();
    check(p->restore(std::as_bytes(std::span(newer)), chunk(world)) != LV2_STATE_SUCCESS &&
              p->failure() && p->failure()->kind == HostFailureKind::state_newer &&
              p->log.contains(LV2_LOG__Error, "newer version of Convolver"),
          label + "state from a newer Convolver: rejected, recorded and explained");
    check(same(decode_host_state(p->save().bytes).configuration, a),
          label + "rejections keep the running filter saved");
    const auto saved = p->save();
    check(saved.type == chunk(world) && (saved.flags & LV2_STATE_IS_POD) &&
              (saved.flags & LV2_STATE_IS_PORTABLE),
          label + "saved as a portable plain chunk");

    // DIAG-1, jrp's reproduction: extreme but finite input and filter values
    // overflow the output. It is silenced, the status port shows Fault at
    // once, and the worker explains in log:log; loading again clears it.
    auto loud = a;
    for (auto& path : loud.paths)
        for (auto& v : path.impulse)
            v = v ? std::numeric_limits<float>::max() / 4 : 0.f;
    check(p->restore(state_of(loud), chunk(world)) == LV2_STATE_SUCCESS, label + "loud filter");
    p->log.messages.clear();
    std::vector<float> extreme(f.input.size());
    for (std::size_t i = 0; i < extreme.size(); i += 7)
        extreme[i] = std::numeric_limits<float>::max() / 4;
    const auto overflowed = render(*p, extreme, 2000, 300);
    check(
        std::all_of(overflowed.begin(), overflowed.end(), [](float v) { return std::isfinite(v); }),
        label + "overflowing output is silenced");
    check(p->status == static_cast<float>(lv2::Status::fault), label + "overflow: status fault");
    check(p->log.contains(LV2_LOG__Error, "too large to represent") &&
              p->log.contains(LV2_LOG__Error, "Lower the level going into Convolver"),
          label + "overflow: the worker explains in log:log");
    check(!p->failure(), label + "overflow is not a load failure");
    check(p->restore(state_of(a), chunk(world)) == LV2_STATE_SUCCESS, label + "load after fault");
    p->run_empty();
    check(p->status == static_cast<float>(lv2::Status::running), label + "a load clears Fault");
    {
        // A host whose worker gets round to requests late: however many runs
        // fault, one fault report is queued; once it has run, clean runs
        // queue nothing; a new fault reports again, also after a
        // reactivation that dropped a queued report. (Invalid input, as the
        // loud filter can overflow even on silence, depending on the FFT.)
        host::Features late;
        late.worker = host::Worker::deferred;
        auto d = instance(world, a, late);
        d->activate();
        check(d->restore(state_of(a), chunk(world)) == LV2_STATE_SUCCESS,
              label + "deferred: filter");
        std::vector<float> invalid(f.input.size());
        for (std::size_t i = 0; i < invalid.size(); i += 301)
            invalid[i] = std::numeric_limits<float>::quiet_NaN(); // One per run.
        const std::vector<float> clean(f.input.size());
        static_cast<void>(render(*d, invalid, 2000, 300));
        check(d->status == static_cast<float>(lv2::Status::fault) && d->queued_work() == 1,
              label + "deferred: one fault report however many runs");
        check(d->run_deferred_work() == 1 && d->log.contains(LV2_LOG__Error, "invalid samples"),
              label + "deferred: the report explains");
        static_cast<void>(render(*d, clean, 2000, 300));
        check(d->queued_work() == 0 && d->status == static_cast<float>(lv2::Status::fault),
              label + "deferred: clean runs schedule no more reports");
        static_cast<void>(render(*d, invalid, 2000, 300));
        check(d->queued_work() == 1, label + "deferred: a new fault reports again");
        d->deactivate();
        d->drop_queued_work();
        d->activate();
        static_cast<void>(render(*d, invalid, 2000, 300));
        check(d->queued_work() == 1 && d->run_deferred_work() == 1,
              label + "deferred: a fault after reactivation still reports");
    }

    // Without log:log, messages go to stderr and nothing breaks.
    host::Features quiet;
    quiet.log = false;
    auto q = instance(world, a, quiet);
    check(q->restore(state_of(wrong_rate(a)), chunk(world)) != LV2_STATE_SUCCESS,
          label + "rejected without log:log too");
}

// --- Presets and lilv's state -------------------------------------------------------
void presets(host::World& world, const Fixture& f, const fs::path& tool, const fs::path& work) {
    const auto label = f.name + ": ";
    const auto folder = work / (f.name + ".preset.lv2");
    fs::remove_all(folder);
    const auto command = "\"" + tool.string() + "\" --name \"" + f.name + " preset\" \"" +
                         f.source.string() + "\" \"" + folder.string() + "\"";
    if (std::system(command.c_str()) != 0) {
        check(false, label + "convolver-preset wrote no LV2 preset: " + command);
        return;
    }
    // As hosts do: load the bundle, find the plug-in's presets, load the one.
    world.load_bundle(folder);
    LilvNode* preset_class = lilv_new_uri(world.get(), LV2_PRESETS__Preset);
    LilvNodes* related = lilv_plugin_get_related(
        world.plugin(f.filter.input_channels, f.filter.output_channels), preset_class);
    LilvNode* uri =
        lilv_new_file_uri(world.get(), nullptr, (fs::absolute(folder) / "preset.ttl").c_str());
    check(related && lilv_nodes_contains(related, uri), label + "the plug-in lists the preset");
    lilv_world_load_resource(world.get(), uri);
    LilvState* preset = lilv_state_new_from_world(world.get(), world.urids().map_feature(), uri);
    lilv_node_free(uri);
    lilv_nodes_free(related);
    lilv_node_free(preset_class);
    check(preset != nullptr, label + "lilv reads the preset");
    if (!preset)
        return;
    check(std::string(lilv_node_as_uri(lilv_state_get_plugin_uri(preset))) ==
              std::string(identity::lv2_base_uri) + "discrete-" +
                  std::to_string(f.filter.input_channels) + "-" +
                  std::to_string(f.filter.output_channels),
          label + "the preset names the matching plug-in");
    auto p = instance(world, f.filter);
    lilv_state_restore(preset, p->instance(), nullptr, nullptr, 0, nullptr);
    check(same(decode_host_state(p->save().bytes).configuration, f.filter),
          label + "restoring the preset loads its filter");
    p->activate();
    check(matches(
              render(*p, f.input,
                     f.input.size() / f.filter.input_channels + test::tail(f.filter) + 2 * latency,
                     300),
              test::oracle(f.filter, f.input, latency)),
          label + "the preset plays the filter");
    lilv_state_free(preset);

    // lilv saves the instance's state, writes it as Turtle and reads it back.
    const auto get_port = [](const char*, void*, std::uint32_t*, std::uint32_t*) -> const void* {
        return nullptr;
    };
    LilvState* state = lilv_state_new_from_instance(
        p->plugin(), p->instance(), world.urids().map_feature(), nullptr, nullptr, nullptr, nullptr,
        get_port, nullptr, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, nullptr);
    check(state != nullptr, label + "lilv saves the instance");
    if (!state)
        return;
    char* text = lilv_state_to_string(world.get(), world.urids().map_feature(),
                                      world.urids().unmap_feature(), state,
                                      "urn:convolver:test-state", nullptr);
    lilv_state_free(state);
    LilvState* reread =
        text ? lilv_state_new_from_string(world.get(), world.urids().map_feature(), text) : nullptr;
    lilv_free(text);
    check(reread != nullptr, label + "lilv reads its saved state back");
    if (!reread)
        return;
    auto q = instance(world, f.filter);
    lilv_state_restore(reread, q->instance(), nullptr, nullptr, 0, nullptr);
    lilv_state_free(reread);
    check(same(decode_host_state(q->save().bytes).configuration, f.filter),
          label + "lilv's saved state restores the filter");
}

// --- Callback sizes -----------------------------------------------------------------
void callback_sizes(host::World& world, const Fixture& f) {
    const auto frames =
        f.input.size() / f.filter.input_channels + test::tail(f.filter) + 2 * latency;
    const auto play = [&](std::optional<std::int32_t> max_block, std::size_t block) {
        host::Features features;
        features.max_block = max_block;
        auto p = instance(world, f.filter, features);
        p->restore(state_of(f.filter), chunk(world));
        p->activate();
        return render(*p, f.input, frames, block);
    };
    const auto reference = play(1024, 300);
    check(matches(reference, test::oracle(f.filter, f.input, latency)),
          f.name + ": plays the filter");
    check(play(64, 300) == reference,
          f.name + ": calls longer than maxBlockLength are split exactly");
    check(play(std::nullopt, 300) == reference, f.name + ": no maxBlockLength option");
    check(play(std::nullopt, 10000) == reference,
          f.name + ": calls longer than the default capacity are split exactly");
}

// --- Lifecycle sequences ------------------------------------------------------------
// One instance goes through a seeded sequence of host operations. A small model
// says after each step what must be saved, what the status port shows and, at
// each fresh start (activation is LV2's reset), exactly what plays.
struct Model {
    std::optional<Configuration> saved;
    bool failed = false, error = false, bypass = false;
    // PLUG-8: requests made to this instance, and the failure it keeps.
    std::uint64_t requests = 0;
    std::optional<HostFailureKind> kind;
    std::uint64_t failed_request = 0;
    std::string path; // The file the saved filter came from, if it did.
    lv2::Status status() const {
        if (error)
            return lv2::Status::load_failed;
        return saved ? lv2::Status::running : lv2::Status::no_filter;
    }
};

// Operation codes: 0 load A, 1 load B, 2 failed load, 3 audio, 4 toggle bypass,
// 5 reset, 6 save and reopen, 7 restore an empty map (LV2's reset to defaults:
// an empty instance again), 8 choose A's file
// (through the worker, synchronously as when a host free-wheels), 9 choose a
// missing file. Seed 0 plays `script` instead of random steps.
void lifecycle(host::World& world, const Fixture& f, unsigned seed,
               const std::vector<int>& script = {}) {
    const auto& a = f.filter;
    const auto b = other(a);
    const HostLayout layout{a.sample_rate, a.input_channels, a.output_channels};
    const auto ni = a.input_channels, no = a.output_channels;
    const auto frames = f.input.size() / ni;
    std::mt19937 random(seed);
    auto p = instance(world, a);
    p->activate();
    Model m;
    std::ostringstream trace;
    const auto label = [&] {
        return f.name + " seed " + std::to_string(seed) + " after" + trace.str();
    };
    // What a fresh start plays: the saved filter, or what an instance without
    // one plays (identity for equal counts, silence otherwise or after a
    // failure), wet or bypassed.
    const auto fresh_output_ok = [&] {
        const auto c = m.saved ? *m.saved : unfiltered_configuration(layout, m.failed);
        const auto length = frames + test::tail(c) + 2 * latency;
        const auto out = render(*p, f.input, length, 300);
        if (m.bypass) {
            std::vector<double> dry(out.size());
            if (ni == no)
                for (std::size_t t = latency; t < length && t - latency < frames; ++t)
                    for (std::size_t ch = 0; ch < no; ++ch)
                        dry[t * no + ch] = f.input[(t - latency) * ni + ch];
            return matches(out, dry);
        }
        if (!m.saved && (m.failed || ni != no))
            return silent(out);
        return matches(out, test::oracle(c, f.input, latency));
    };
    const auto steps = script.empty() ? std::size_t(40) : script.size();
    for (std::size_t step = 0; step < steps; ++step) {
        const int op =
            script.empty() ? std::uniform_int_distribution<int>(0, 9)(random) : script[step];
        switch (op) {
        case 0:
        case 1: { // A valid load (A or B).
            const auto& c = op == 0 ? a : b;
            trace << (op == 0 ? " load-A" : " load-B");
            check(p->restore(state_of(c), chunk(world)) == LV2_STATE_SUCCESS,
                  label() + ": accepted");
            m.saved = c;
            m.failed = m.error = false;
            ++m.requests;
            m.kind.reset();
            m.path.clear(); // From state alone: no file.
            break;
        }
        case 8: // A's file, through the worker.
            trace << " file-A";
            p->set_filter(f.source.string());
            p->run_empty(); // The synchronous worker loads it inside this run().
            m.saved = a;
            m.failed = m.error = false;
            ++m.requests;
            m.kind.reset();
            m.path = f.source.string();
            break;
        case 9: // A file that does not exist.
            trace << " file-missing";
            p->set_filter((f.source.parent_path() / "missing.cfg").string());
            p->run_empty();
            m.failed = !m.saved;
            m.error = true;
            m.kind = HostFailureKind::filter_file;
            m.failed_request = ++m.requests;
            break;
        case 2: // A load that does not suit the host.
            trace << " load-wrong-rate";
            check(p->restore(state_of(wrong_rate(a)), chunk(world)) != LV2_STATE_SUCCESS,
                  label() + ": rejected");
            m.failed = !m.saved;
            m.error = true;
            m.kind = HostFailureKind::sample_rate;
            m.failed_request = ++m.requests;
            break;
        case 3: // Some audio, which may start a crossfade.
            trace << " process";
            render(*p, f.input, 700, 61);
            break;
        case 4:
            m.bypass = !m.bypass;
            trace << (m.bypass ? " bypass-on" : " bypass-off");
            p->enabled = m.bypass ? 0.f : 1.f;
            break;
        case 5: // Reset (deactivate and activate), then check what plays.
            trace << " reset";
            p->deactivate();
            p->activate();
            check(fresh_output_ok(), label() + ": output after a fresh start");
            p->deactivate();
            p->activate();
            break;
        case 6: { // Save, close and reopen the session.
            trace << " reopen";
            const auto saved = p->save();
            p = instance(world, a);
            p->enabled = m.bypass ? 0.f : 1.f; // The host restores its port values.
            check(p->restore(saved.all) == LV2_STATE_SUCCESS, label() + ": reopened");
            m.error = m.failed; // A saved failure is reported again; a kept filter is not.
            m.requests = 1;     // A new instance, whose first request is the restore.
            m.kind = m.failed ? std::optional(HostFailureKind::saved_failure) : std::nullopt;
            m.failed_request = 1;
            check(p->log.contains(LV2_LOG__Error, "failed to load before") == m.failed,
                  label() + ": a reopened failure is explained in the log, and only then");
            p->activate();
            break;
        }
        default: // An empty map: the host resets the plug-in to its defaults.
            trace << " restore-nothing";
            check(p->restore_empty() == LV2_STATE_SUCCESS, label() + ": empty restore");
            m.saved.reset();
            m.failed = m.error = false;
            ++m.requests;
            m.kind.reset();
            m.path.clear();
            break;
        }
        const auto saved = p->save();
        const auto s = decode_host_state(saved.bytes);
        check(same(s.configuration, m.saved) && s.failed == m.failed, label() + ": saved state");
        const auto path =
            saved.filter ? std::string(reinterpret_cast<const char*>(saved.filter->value.data()))
                         : std::string();
        check(path == m.path,
              label() + ": saved path \"" + path + "\", expected \"" + m.path + "\"");
        p->run_empty();
        check(p->status == static_cast<float>(m.status()),
              label() + ": status " + std::to_string(p->status) + ", expected " +
                  std::to_string(static_cast<int>(m.status())));
        check(p->latency == 256, label() + ": latency");
        const auto r = p->failure();
        check(r.has_value() == m.kind.has_value() &&
                  (!r || (r->kind == *m.kind && r->request.number == m.failed_request &&
                          r->silent == !m.saved)),
              label() + ": failure record");
    }
    p->deactivate();
    p->activate();
    check(fresh_output_ok(), label() + ": final output");
}

// --- Loading filter files through the worker (LV2-2) --------------------------------
// A 2-in, 2-out instance at 48 kHz and filters that only scale the input, so
// the output of a constant input shows which filter plays: the original at
// 0.5, A at 0.25 and B at 0.75. A crossfade from the original to B never
// goes below 0.5, so any output near 0.25 means A was audible.
struct Files {
    fs::path original, a, b, wrong_rate, mono_ir, missing;
};
void write_impulse(const fs::path& path, int rate) {
    fs::remove(path);
    AudioWriter writer(path, {rate, 1}, OutputFormat::wav_float);
    const std::array<float, 1> one{1};
    writer.write(one);
    writer.close();
}
void write_text(const fs::path& path, const std::string& text) {
    fs::remove(path);
    std::ofstream out(path);
    out << text;
    if (!out)
        throw std::runtime_error("cannot write " + path.string());
}
// `gain` is the fractional part of the route: "0.25" is input 0 at 0.25.
std::string scaling_cfg(int rate, const std::string& ir, const std::string& gain) {
    return std::to_string(rate) + " 2 2 0\n0 0\n0 0\n" + ir + "\n0\n0" + gain + "\n0\n" + ir +
           "\n0\n1" + gain + "\n1\n";
}
Files worker_files(const fs::path& work) {
    const auto folder = work / "worker";
    fs::create_directories(folder);
    write_impulse(folder / "impulse.wav", 48000);
    write_impulse(folder / "impulse-44.wav", 44100);
    Files f{folder / "original.cfg", folder / "a.cfg",       folder / "b.cfg",
            folder / "44100.cfg",    folder / "impulse.wav", folder / "missing.cfg"};
    write_text(f.original, scaling_cfg(48000, "impulse.wav", ".5"));
    write_text(f.a, scaling_cfg(48000, "impulse.wav", ".25"));
    write_text(f.b, scaling_cfg(48000, "impulse.wav", ".75"));
    write_text(f.wrong_rate, scaling_cfg(44100, "impulse-44.wav", ".5"));
    fs::remove(f.missing);
    return f;
}

std::unique_ptr<host::Plugin> worker_instance(host::World& world, host::Worker mode) {
    host::Features features;
    features.worker = mode;
    auto p = std::make_unique<host::Plugin>(world, world.plugin(2, 2), 48000, features);
    if (!p->instantiated())
        throw std::runtime_error("instantiation failed");
    return p;
}
// Lets the worker finish and its answers arrive, then reads the ports again
// (answers are delivered after a run(), so the second run shows them).
void settle(host::Plugin& p) {
    p.run_empty(); // Sends any message still queued.
    p.wait_for_worker();
    p.run_empty();
    p.run_empty();
}
float status_of(host::Plugin& p) {
    p.run_empty();
    return p.status;
}
constexpr float as_float(lv2::Status s) {
    return static_cast<float>(s);
}
// Plays a constant 1 on both inputs; returns output channel 0.
std::vector<float> play_constant(host::Plugin& p, std::size_t frames) {
    const std::vector<float> ones(frames * 2, 1.f);
    const auto out = render(p, ones, frames, 256);
    std::vector<float> left(frames);
    for (std::size_t t = 0; t < frames; ++t)
        left[t] = out[t * 2];
    return left;
}
bool near(float a, float b) {
    return std::abs(a - b) < 1e-5f;
}
std::string saved_path(host::Plugin& p) {
    const auto s = p.save();
    if (!s.filter)
        return "";
    const auto* text = reinterpret_cast<const char*>(s.filter->value.data());
    return std::string(text, strnlen(text, s.filter->value.size()));
}
// The path that patch:Get reports now.
std::string reported(host::Plugin& p) {
    p.notified.clear();
    p.get_filter();
    p.run_empty();
    return p.notified.empty() ? "<none>" : p.notified.back();
}

void worker_loads(host::World& world, const fs::path& work) {
    const auto files = worker_files(work);
    const auto original = files.original.string(), a = files.a.string(), b = files.b.string();
    using host::Worker;
    using lv2::Status;
    using lv2::WorkerPoint;

    // Every way a host runs the worker gives the same end state.
    for (const auto mode : {Worker::threaded, Worker::synchronous, Worker::immediate}) {
        const auto label =
            std::string("worker mode ") + std::to_string(static_cast<int>(mode)) + ": ";
        auto p = worker_instance(world, mode);
        p->activate();
        p->set_filter(original);
        p->run_empty();
        check(p->status == as_float(mode == Worker::immediate ? Status::running : Status::loading),
              label + "status while the request is outstanding");
        settle(*p);
        check(p->status == as_float(Status::running), label + "status running after the load");
        check(!p->notified.empty() && p->notified.back() == original,
              label + "patch:Set on the notify port names the loaded file");
        check(reported(*p) == original, label + "patch:Get names the loaded file");
        check(saved_path(*p) == original, label + "save() stores the file's path");
        const auto out = play_constant(*p, 2048);
        check(near(out[300], .5f) && near(out.back(), .5f),
              label + "the file's filter plays (installed directly before any audio)");
        p->set_filter(b);
        play_constant(*p, 256);
        settle(*p);
        const auto after = play_constant(*p, 4096);
        check(near(after.back(), .75f) &&
                  std::all_of(after.begin(), after.end(),
                              [](float v) { return v > .5f - 1e-5f && v < .75f + 1e-5f; }),
              label + "a second file crossfades in");
        check(reported(*p) == b && saved_path(*p) == b, label + "the path follows the new file");
    }

    // Loading while inactive: the next activation installs it.
    {
        auto p = worker_instance(world, Worker::threaded);
        p->set_filter(original);
        p->run_empty(); // Hosts may run the atom ports' messages only while active; we do not mind.
        settle(*p);
        p->activate();
        check(near(play_constant(*p, 1024)[300], .5f), "a file loaded while inactive plays");
    }

    // Failures: each is recorded, logged, shown, and leaves the original playing.
    {
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->set_filter(files.missing.string());
        settle(*p);
        check(p->status == as_float(Status::load_failed), "missing file: status load failed");
        auto r = p->failure();
        check(r && r->kind == HostFailureKind::filter_file && r->silent &&
                  r->request.source == HostFilterSource::file &&
                  r->request.path == files.missing.string() &&
                  p->log.contains(LV2_LOG__Error, r->message) &&
                  r->message.find("missing.cfg") != std::string::npos,
              "missing file: recorded as filter_file and logged with its path");
        check(silent(play_constant(*p, 1024)), "missing file as the first load plays silence");
        check(decode_host_state(p->save().bytes).failed && saved_path(*p).empty(),
              "missing file as the first load is saved as a failure, with no path");
        p->set_filter(original);
        settle(*p);
        check(p->status == as_float(Status::running) && !p->failure(),
              "a good file after a failure clears it");
        for (const auto& [path, kind, what] :
             {std::tuple{files.wrong_rate.string(), HostFailureKind::sample_rate, "44100"},
              std::tuple{files.mono_ir.string(), HostFailureKind::channels, "1 channel"},
              std::tuple{files.missing.string(), HostFailureKind::filter_file, "missing"}}) {
            p->log.messages.clear();
            p->set_filter(path);
            settle(*p);
            r = p->failure();
            check(p->status == as_float(Status::load_failed) && r && r->kind == kind &&
                      !r->silent &&
                      r->message.find("previous filter is kept") != std::string::npos &&
                      p->log.contains(LV2_LOG__Error, what),
                  "failed replacement with " + path + ": recorded, explained, original kept");
            // The refused file's own rate or counts are recorded for the short
            // status text, and the advice is not the command line's (jrp's
            // GarageBand run of the Audio Unit, which shares this step) and
            // names no window, as LV2 has none.
            check((kind != HostFailureKind::sample_rate || r->sample_rate == 44100) &&
                      (kind != HostFailureKind::channels || (r->inputs == 1 && r->outputs == 1)) &&
                      r->message.find("--") == std::string::npos &&
                      r->message.find("window") == std::string::npos,
                  "failed replacement with " + path + ": its rate and counts, host advice");
            check(near(play_constant(*p, 1024).back(), .5f) && reported(*p) == original &&
                      saved_path(*p) == original,
                  "failed replacement with " + path + ": the original plays and is reported");
        }
        // A path longer than the plug-in takes.
        p->set_filter(std::string(lv2::max_path_bytes + 10, 'x'));
        settle(*p);
        r = p->failure();
        check(p->status == as_float(Status::load_failed) && r &&
                  r->kind == HostFailureKind::filter_file &&
                  r->message.find("longer than") != std::string::npos,
              "too long a path: load failed, explained");
        // An atom:String value is accepted too.
        p->set_filter(b, world.urids().map(LV2_ATOM__String));
        settle(*p);
        check(p->status == as_float(Status::running) && reported(*p) == b,
              "a path sent as atom:String loads");
    }

    // No worker: the request fails at once, and the next control call
    // explains why.
    {
        auto p = worker_instance(world, Worker::none);
        p->activate();
        p->set_filter(original);
        p->run_empty();
        check(p->status == as_float(Status::load_failed), "no worker: status load failed at once");
        p->save();
        const auto r = p->failure();
        check(r && r->kind == HostFailureKind::filter_file &&
                  p->log.contains(LV2_LOG__Error, "worker thread"),
              "no worker: recorded and explained at the next control call");
    }

    // Rapid loads coalesce to the newest.
    {
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        for (int i = 0; i < 6; ++i) {
            p->set_filter(i % 2 ? a : original);
            if (i == 3)
                p->set_filter(files.missing.string());
            p->run_empty();
        }
        p->set_filter(b);
        p->run_empty();
        settle(*p);
        check(p->status == as_float(Status::running) && !p->failure() && reported(*p) == b &&
                  saved_path(*p) == b,
              "rapid loads: the newest wins");
        check(near(play_constant(*p, 4096).back(), .75f), "rapid loads: the newest plays");
    }

    // The preparation window (the plan's regression tests). A is paused
    // inside work() while B is requested.
    const auto with_original = [&] {
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->set_filter(original);
        settle(*p);
        play_constant(*p, 1024);
        return p;
    };
    // jrp's review of 0.4.43: run() goes on while the worker prepares a
    // replacement. A fault in that window is explained in the log, but not
    // shown as Fault against the filter that replaces it.
    {
        auto p = with_original();
        p->log.messages.clear();
        p->pause_at(WorkerPoint::prepared);
        p->set_filter(a);
        p->run_empty();
        p->wait_until_paused();
        std::vector<float> invalid(2 * 256);
        invalid[0] = std::numeric_limits<float>::quiet_NaN();
        static_cast<void>(render(*p, invalid, 256, 256));
        p->resume();
        settle(*p);
        static_cast<void>(play_constant(*p, 2048));
        settle(*p);
        check(p->status == as_float(Status::running) && reported(*p) == a,
              "a fault while the replacement was prepared shows as Fault against it");
        check(p->log.contains(LV2_LOG__Error, "invalid samples") &&
                  p->log.contains(LV2_LOG__Error, "does not show these") &&
                  !p->log.contains(LV2_LOG__Error, "status shows Fault"),
              "a fault while the replacement was prepared is not explained as earlier");
    }
    const auto never_a = [](const std::vector<float>& out) {
        return std::all_of(out.begin(), out.end(), [](float v) { return v > .5f - 1e-5f; });
    };
    for (const bool b_valid : {true, false}) {
        const auto label =
            std::string("paused after preparation, B ") + (b_valid ? "valid" : "invalid") + ": ";
        auto p = with_original();
        p->pause_at(WorkerPoint::prepared);
        p->set_filter(a);
        p->run_empty();
        p->wait_until_paused();
        p->pause_at(WorkerPoint::prepared); // B pauses too, so A's answer arrives first.
        p->set_filter(b_valid ? b : files.missing.string());
        auto out = play_constant(*p, 512);
        p->resume(); // A is superseded at its last check.
        if (b_valid)
            p->wait_until_paused();
        else
            p->wait_for_worker(); // B fails before preparation.
        auto more = play_constant(*p, 512);
        out.insert(out.end(), more.begin(), more.end());
        check(!b_valid || p->status == as_float(Status::loading),
              label + "A's answer leaves the status at loading");
        check(reported(*p) == original, label + "A's answer leaves the path alone");
        if (b_valid)
            p->resume();
        settle(*p);
        more = play_constant(*p, 4096);
        out.insert(out.end(), more.begin(), more.end());
        check(never_a(out), label + "A is never audible");
        if (b_valid) {
            check(near(out.back(), .75f) && p->status == as_float(Status::running) &&
                      reported(*p) == b && saved_path(*p) == b,
                  label + "B plays, and the status and path follow it");
        } else {
            check(near(out.back(), .5f) && p->status == as_float(Status::load_failed) &&
                      reported(*p) == original && saved_path(*p) == original,
                  label + "the original keeps playing, status load failed, path original");
        }
    }
    for (const bool b_valid : {true, false}) {
        const auto label =
            std::string("paused after the last check, B ") + (b_valid ? "valid" : "invalid") + ": ";
        auto p = with_original();
        p->pause_at(WorkerPoint::checked);
        p->set_filter(a);
        p->run_empty();
        p->wait_until_paused();
        p->pause_at(WorkerPoint::prepared);
        p->set_filter(b_valid ? b : files.missing.string());
        play_constant(*p, 512);
        p->resume(); // A is published.
        if (b_valid)
            p->wait_until_paused();
        else
            p->wait_for_worker();
        p->notified.clear();
        p->run_empty();
        p->run_empty();
        check(!b_valid || p->status == as_float(Status::loading),
              label + "A's answer leaves the status at loading");
        check(!p->notified.empty() && p->notified.front() == a,
              label + "A's answer sends patch:Set with A's path");
        if (b_valid)
            p->resume();
        settle(*p);
        const auto out = play_constant(*p, 4096);
        if (b_valid) {
            check(near(out.back(), .75f) && p->status == as_float(Status::running) &&
                      reported(*p) == b && saved_path(*p) == b,
                  label + "B plays, and the status and path follow it");
        } else {
            check(near(out.back(), .25f) && p->status == as_float(Status::load_failed) &&
                      reported(*p) == a && saved_path(*p) == a,
                  label + "A keeps playing, status load failed, path A");
        }
    }
    {
        // A fails to read, but B was requested meanwhile: A's failure is never
        // recorded, so it neither silences the empty instance nor is logged.
        const std::string label = "A's read fails after B is requested: ";
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->pause_at(WorkerPoint::read);
        p->set_filter(files.missing.string());
        p->run_empty();
        p->wait_until_paused();
        p->pause_at(WorkerPoint::prepared);
        p->set_filter(b);
        auto out = play_constant(*p, 512);
        p->resume();            // A is superseded before anything is recorded.
        p->wait_until_paused(); // B, after its preparation.
        auto more = play_constant(*p, 512);
        out.insert(out.end(), more.begin(), more.end());
        // (After the first block, which fades in from activation.)
        check(std::all_of(out.begin() + 256, out.end(), [](float v) { return near(v, 1.f); }) &&
                  p->status == as_float(Status::loading),
              label + "the empty instance passes audio through while B loads");
        p->resume();
        settle(*p);
        bool logged = false;
        for (const auto& m : p->log.messages)
            logged = logged || m.text.find(files.missing.filename().string()) != std::string::npos;
        check(!logged && !p->failure(), label + "A's failure is neither logged nor recorded");
        check(near(play_constant(*p, 4096).back(), .75f) &&
                  p->status == as_float(Status::running) && reported(*p) == b,
              label + "B plays");
    }
    {
        const std::string label = "scheduling failure for B while A is paused after its check: ";
        auto p = with_original();
        p->pause_at(WorkerPoint::checked);
        p->set_filter(a);
        p->run_empty();
        p->wait_until_paused();
        p->scheduling_fails = true;
        p->set_filter(b);
        p->run_empty();
        check(p->status == as_float(Status::load_failed), label + "load failed at once");
        p->scheduling_fails = false;
        p->notified.clear();
        p->resume();
        settle(*p);
        check(!p->notified.empty() && p->notified.front() == a && reported(*p) == a,
              label + "the path follows A when A's answer arrives");
        check(p->status == as_float(Status::load_failed), label + "the status stays load failed");
        p->save(); // The next control call records B's failure.
        const auto r = p->failure();
        check(r && r->kind == HostFailureKind::filter_file &&
                  p->log.contains(LV2_LOG__Error, "worker queue was full") &&
                  near(play_constant(*p, 4096).back(), .25f),
              label + "B's failure is recorded and explained; A plays");
    }
    {
        const std::string label = "restore with an answer outstanding: ";
        auto p = with_original();
        p->set_filter(a);
        p->run_empty();
        p->wait_for_worker(); // A is installed; its answer is not yet delivered.
        HostState state;
        state.configuration = load_configuration(files.b);
        // restore() runs between run() calls, and A's answer comes after it.
        check(p->restore(encode_host_state(state), chunk(world)) == LV2_STATE_SUCCESS,
              label + "restored");
        p->notified.clear();
        p->run_empty();
        p->run_empty();
        check(p->status == as_float(Status::running), label + "status from the restore");
        check(reported(*p).empty() && saved_path(*p).empty(),
              label + "the path is the restored state's (none), not A's");
        check(near(play_constant(*p, 4096).back(), .75f), label + "the restored filter plays");
    }

    // Restore while a load is queued or in progress: restore() waits for
    // work() and is the newer request either way.
    for (int i = 0; i < 4; ++i) {
        auto p = with_original();
        // Read before the load starts: this program has its own copy of the
        // reader, and libsndfile's globals are not guarded across copies.
        HostState state;
        state.configuration = load_configuration(files.b);
        p->set_filter(a);
        p->run_empty();
        check(p->restore(encode_host_state(state), chunk(world)) == LV2_STATE_SUCCESS,
              "restore during a load: restored");
        settle(*p);
        check(p->status == as_float(Status::running) && reported(*p).empty() &&
                  saved_path(*p).empty() && near(play_constant(*p, 4096).back(), .75f),
              "restore during a load: the restored filter wins");
    }

    // Saving on another thread while files load: each save is whole, its
    // filter and path from the same load.
    {
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->set_filter(original);
        settle(*p);
        const std::vector<std::pair<std::string, Configuration>> known{
            {original, load_configuration(files.original)},
            {a, load_configuration(files.a)},
            {b, load_configuration(files.b)}};
        std::atomic<bool> done{false};
        int inconsistent = 0, saves = 0;
        std::thread saver([&] {
            while (!done.load()) {
                const auto saved = p->save();
                const auto* text =
                    saved.filter ? reinterpret_cast<const char*>(saved.filter->value.data()) : "";
                const auto s = decode_host_state(saved.bytes);
                bool ok = false;
                for (const auto& [path, c] : known)
                    ok = ok || (path == text && same(s.configuration, c));
                inconsistent += ok ? 0 : 1;
                ++saves;
            }
        });
        for (int k = 0; k < 30; ++k) {
            p->set_filter(k % 3 == 0 ? a : k % 3 == 1 ? b : original);
            play_constant(*p, 256);
        }
        p->wait_for_worker();
        done = true;
        saver.join();
        check(saves > 0 && inconsistent == 0,
              "saving during loads: " + std::to_string(inconsistent) + " of " +
                  std::to_string(saves) + " saves mixed two loads");
    }

    // Cleanup and deactivation with work outstanding.
    for (int i = 0; i < 4; ++i) {
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        for (int k = 0; k < 4; ++k) {
            p->set_filter(k % 2 ? a : b);
            p->run_empty();
        }
        if (i % 2)
            p->deactivate();
        // The destructor deactivates, stops the worker and cleans up.
    }
}

// PLUG-12 (jrp's review of 6920a45): a filter file whose filter the saved
// state cannot hold is refused when it is chosen, not accepted and then
// lost when the host saves. Two paths of 8,388,608 samples are 16,777,216 in
// all, just over the limit; the refusal keeps the working filter, its path
// and its saved bytes, and an empty instance's first load fails into silence.
void oversized_files(host::World& world, const fs::path& work) {
    const auto files = worker_files(work);
    const auto folder = work / "oversized";
    fs::create_directories(folder);
    {
        fs::remove(folder / "long.wav");
        AudioWriter writer(folder / "long.wav", {48000, 1}, OutputFormat::wav_float);
        std::vector<float> chunk(1 << 16, 0.f);
        chunk[0] = 1;
        for (int i = 0; i < 128; ++i) {
            writer.write(chunk);
            chunk[0] = 0;
        }
        writer.close();
    }
    const auto oversized = (folder / "long.cfg").string();
    write_text(oversized, scaling_cfg(48000, "long.wav", ".5"));
    using lv2::Status;
    const auto refused = [&](host::Plugin& p, bool silent, const std::string& label) {
        p.log.messages.clear();
        p.set_filter(oversized);
        settle(p);
        const auto r = p.failure();
        check(p.status == as_float(Status::load_failed) && r &&
                  r->kind == HostFailureKind::too_large && r->silent == silent &&
                  r->message.find("too large to save") != std::string::npos &&
                  p.log.contains(LV2_LOG__Error, "too large to save"),
              label + "refused as too large to save, and explained");
    };
    {
        auto p = worker_instance(world, host::Worker::threaded);
        p->activate();
        p->set_filter(files.original.string());
        settle(*p);
        const auto before = p->save().bytes;
        refused(*p, false, "oversized replacement: ");
        check(near(play_constant(*p, 1024).back(), .5f) &&
                  reported(*p) == files.original.string() &&
                  saved_path(*p) == files.original.string() && p->save().bytes == before,
              "oversized replacement: the original plays and saves as before");
    }
    {
        auto p = worker_instance(world, host::Worker::threaded);
        p->activate();
        refused(*p, true, "oversized first load: ");
        check(silent(play_constant(*p, 1024)) && decode_host_state(p->save().bytes).failed &&
                  saved_path(*p).empty(),
              "oversized first load: plays silence and saves as a failed load");
    }
    fs::remove(folder / "long.wav");
}

// --- Expectations from the shared contract and the LV2 specification -------------
// Found in jrp's review of 0.4.30, where tests had followed the implementation.

// Shared contract, "Loading and failure": a first filter that cannot be used
// plays silence, never unfiltered audio, and a rejected replacement keeps the
// working filter. When run() itself refuses a file (no worker, or the host
// cannot queue it), the silence starts at once: no control call need follow.
void refused_first_loads(host::World& world, const fs::path& work) {
    const auto files = worker_files(work);
    using host::Worker;
    using lv2::Status;
    const auto all_zero_from = [](const std::vector<float>& out, std::size_t from) {
        return std::all_of(out.begin() + static_cast<std::ptrdiff_t>(from), out.end(),
                           [](float v) { return v == 0; });
    };
    for (const bool no_worker : {true, false}) {
        const auto label =
            std::string(no_worker ? "no worker" : "worker queue full") + ", first load: ";
        auto p = worker_instance(world, no_worker ? Worker::none : Worker::threaded);
        p->scheduling_fails = !no_worker;
        p->activate();
        check(near(play_constant(*p, 1024).back(), 1.f), label + "empty instance plays identity");
        p->set_filter(files.original.string());
        // Only audio from here on: no save, restore or other control call.
        const auto out = play_constant(*p, 4096);
        check(all_zero_from(out, 64) && out.front() <= 1.f,
              label + "silent within 64 frames of the refusal, without a control call");
        check(p->status == as_float(Status::load_failed), label + "status load failed");
        check(all_zero_from(play_constant(*p, 4096), 0), label + "stays silent");
        // Bypass still passes the dry signal, as with any failed first load.
        p->enabled = 0;
        check(near(play_constant(*p, 2048).back(), 1.f), label + "bypass passes the input");
        p->enabled = 1;
        check(all_zero_from(play_constant(*p, 2048), 64), label + "silent again after bypass");
        // The next control call records it; the instance stays silent, also
        // after a restart.
        check(decode_host_state(p->save().bytes).failed && p->failure() && p->failure()->silent,
              label + "recorded and saved as a failed first load");
        check(all_zero_from(play_constant(*p, 2048), 0), label + "silent after the record");
        p->deactivate();
        p->activate();
        check(all_zero_from(play_constant(*p, 2048), 0), label + "silent after a restart");
        // A filter accepted later plays; the refusal does not outlive it.
        HostState state;
        state.configuration = load_configuration(files.b);
        check(p->restore(encode_host_state(state), chunk(world)) == LV2_STATE_SUCCESS,
              label + "a later restore is accepted");
        check(near(play_constant(*p, 4096).back(), .75f), label + "the restored filter plays");
    }
    // A refusal while a filter plays keeps it playing.
    for (const bool no_worker : {true, false}) {
        const auto label =
            std::string(no_worker ? "no worker" : "worker queue full") + ", replacement: ";
        auto p = worker_instance(world, no_worker ? Worker::none : Worker::threaded);
        HostState state;
        state.configuration = load_configuration(files.original);
        check(p->restore(encode_host_state(state), chunk(world)) == LV2_STATE_SUCCESS,
              label + "restored");
        p->activate();
        play_constant(*p, 1024);
        p->scheduling_fails = !no_worker;
        p->set_filter(files.b.string());
        const auto out = play_constant(*p, 4096);
        check(std::all_of(out.begin(), out.end(), [](float v) { return near(v, .5f); }),
              label + "the working filter keeps playing");
        check(p->status == as_float(Status::load_failed) &&
                  !decode_host_state(p->save().bytes).failed,
              label + "status load failed; the working filter is saved");
    }
    // A refusal after a worker load has published its filter, before audio
    // has heard it: the accepted load plays and is saved.
    {
        const std::string label = "refusal while the first file is published: ";
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        play_constant(*p, 1024);
        p->pause_at(lv2::WorkerPoint::checked);
        p->set_filter(files.a.string());
        p->run_empty();
        p->wait_until_paused(); // A passed its last check; it publishes on resume.
        p->resume();
        p->wait_for_worker(); // Published, not yet heard.
        p->scheduling_fails = true;
        p->set_filter(files.b.string());
        p->run_empty();
        p->scheduling_fails = false;
        settle(*p);
        check(
            near(play_constant(*p, 4096).back(), .25f) &&
                same(decode_host_state(p->save().bytes).configuration, load_configuration(files.a)),
            label + "A plays and is saved");
    }
    // A refusal while a first filter loaded by the worker plays: it keeps
    // playing. This checks that the worker clears `unfiltered` before it
    // publishes (found by mutation in the independent review of PR #55).
    {
        const std::string label = "refusal after a worker-loaded first file: ";
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        play_constant(*p, 1024);
        p->set_filter(files.a.string());
        settle(*p);
        play_constant(*p, 1024);
        p->scheduling_fails = true;
        p->set_filter(files.b.string());
        const auto out = play_constant(*p, 4096);
        check(std::all_of(out.begin(), out.end(), [](float v) { return near(v, .25f); }),
              label + "A keeps playing");
        check(
            p->status == as_float(Status::load_failed) &&
                same(decode_host_state(p->save().bytes).configuration, load_configuration(files.a)),
            label + "status load failed; A is saved");
    }
}

// LV2 state, restore(): "The plugin MUST gracefully fall back to a default
// value when a value can not be retrieved. This allows the host to reset the
// plugin state with an empty map." The default is an empty instance: identity
// for equal counts, no failure, no path, and loads still outstanding lose.
void empty_state_resets(host::World& world, const fs::path& work) {
    const auto files = worker_files(work);
    using host::Worker;
    using lv2::Status;
    const auto is_empty_instance = [&](host::Plugin& p, const std::string& label) {
        const auto s = decode_host_state(p.save().bytes);
        check(!s.configuration && !s.failed, label + "saved as an empty instance");
        check(saved_path(p).empty() && reported(p).empty(), label + "no path saved or reported");
        check(!p.failure() && status_of(p) == as_float(Status::no_filter),
              label + "no failure, status no filter");
        check(near(play_constant(p, 4096).back(), 1.f), label + "plays identity");
    };
    {
        // jrp's reproduction: a gain-of-two filter, then an empty map.
        const std::string label = "empty map over a filter: ";
        auto p = worker_instance(world, Worker::threaded);
        HostState state;
        state.configuration = load_configuration(files.original);
        for (auto& path : state.configuration->paths)
            for (auto& v : path.impulse)
                v *= 4; // .5 to 2.
        check(p->restore(encode_host_state(state), chunk(world)) == LV2_STATE_SUCCESS,
              label + "restored");
        p->activate();
        check(near(play_constant(*p, 2048).back(), 2.f), label + "gain two plays");
        check(p->restore_empty() == LV2_STATE_SUCCESS, label + "empty map accepted");
        is_empty_instance(*p, label);
    }
    {
        const std::string label = "empty map over a file: ";
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->set_filter(files.original.string());
        settle(*p);
        check(saved_path(*p) == files.original.string(), label + "file loaded");
        p->notified.clear();
        check(p->restore_empty() == LV2_STATE_SUCCESS, label + "empty map accepted");
        p->run_empty();
        check(!p->notified.empty() && p->notified.front().empty(),
              label + "patch:Set reports that no file plays");
        is_empty_instance(*p, label);
    }
    {
        const std::string label = "empty map after a failed first load: ";
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->set_filter(files.missing.string());
        settle(*p);
        check(p->failure() && silent(play_constant(*p, 1024)), label + "failed and silent");
        check(p->restore_empty() == LV2_STATE_SUCCESS, label + "empty map accepted");
        is_empty_instance(*p, label);
    }
    // Loads still outstanding lose to the reset: one whose answer has not
    // arrived, and ones queued or running when restore() is called (restore()
    // waits for work() and is the newer request either way).
    {
        const std::string label = "empty map with an answer outstanding: ";
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->set_filter(files.a.string());
        p->run_empty();
        p->wait_for_worker(); // A is installed; its answer is not yet delivered.
        check(p->restore_empty() == LV2_STATE_SUCCESS, label + "empty map accepted");
        p->run_empty();
        p->run_empty();
        is_empty_instance(*p, label);
    }
    for (int i = 0; i < 4; ++i) {
        const std::string label = "empty map during a load: ";
        auto p = worker_instance(world, Worker::threaded);
        p->activate();
        p->set_filter(files.original.string());
        settle(*p);
        p->set_filter(files.a.string());
        p->run_empty();
        check(p->restore_empty() == LV2_STATE_SUCCESS, label + "empty map accepted");
        settle(*p);
        is_empty_instance(*p, label);
    }
    {
        // Unequal counts: an empty instance is silent.
        const std::string label = "empty map, 2 to 6: ";
        HostState state;
        state.configuration = Configuration{48000, 2, 6, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
        auto p = instance(world, *state.configuration);
        check(p->restore(encode_host_state(state), chunk(world)) == LV2_STATE_SUCCESS,
              label + "restored");
        p->activate();
        check(p->restore_empty() == LV2_STATE_SUCCESS && !p->failure() &&
                  !decode_host_state(p->save().bytes).configuration,
              label + "empty map accepted, nothing saved");
        std::vector<float> ones(4096 * 2, 1.f);
        check(silent(render(*p, ones, 4096, 256)), label + "plays silence");
    }
}

// --- State with a path (LV2-2) ------------------------------------------------------
void path_state(host::World& world, const fs::path& work) {
    const auto files = worker_files(work);
    const auto folder = files.original.parent_path();
    auto& urids = world.urids();
    const auto filter_key = urids.map(host::filter_uri().c_str());
    const auto state_key = urids.map(host::state_uri().c_str());
    const auto as_bytes = [](const std::string& text, bool nul) {
        std::vector<std::byte> v(text.size() + (nul ? 1 : 0));
        std::memcpy(v.data(), text.data(), text.size());
        return v;
    };
    const auto path_property = [&](const std::string& path, LV2_URID type, bool nul = true) {
        return host::Property{filter_key, type, as_bytes(path, nul),
                              LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE};
    };
    const auto atom_path = urids.map(LV2_ATOM__Path);

    // Saved with state:mapPath: a path inside the state folder is relative,
    // and restoring maps it back.
    {
        auto p = worker_instance(world, host::Worker::synchronous);
        p->set_state_folder(folder);
        p->activate();
        p->set_filter(files.b.string());
        settle(*p);
        const auto saved = p->save();
        check(saved.properties == 2 && saved.filter && saved.filter->type == atom_path &&
                  std::string(reinterpret_cast<const char*>(saved.filter->value.data())) ==
                      "b.cfg" &&
                  p->paths_freed() > 0,
              "the path is saved mapped (relative to the state folder), freed with freePath");
        auto q = worker_instance(world, host::Worker::synchronous);
        q->set_state_folder(folder);
        check(q->restore(saved.all) == LV2_STATE_SUCCESS, "state with a path restores");
        q->activate();
        const auto level = play_constant(*q, 2048).back();
        const auto path = reported(*q);
        check(near(level, .75f) && path == files.b.string() && saved_path(*q) == "b.cfg",
              "restored: the embedded filter plays and the absolute path is reported (" + path +
                  ", level " + std::to_string(level) + ")");
        // The file changes; the embedded copy still wins.
        auto r = worker_instance(world, host::Worker::synchronous);
        r->set_state_folder(folder);
        auto changed = saved.all;
        for (auto& property : changed)
            if (property.key == filter_key)
                property = path_property("a.cfg", atom_path);
        r->restore(changed);
        r->activate();
        check(near(play_constant(*r, 2048).back(), .75f),
              "the embedded copy is preferred to the file");
    }
    // A path alone, as PipeWire's filter-chain passes it (type 0, no NUL),
    // an atom:String or an atom:Path.
    for (const auto& [name, property] :
         {std::pair{"PipeWire's bare value", path_property(files.a.string(), 0, false)},
          std::pair{"atom:String", path_property(files.a.string(), urids.map(LV2_ATOM__String))},
          std::pair{"atom:Path", path_property(files.a.string(), atom_path)}}) {
        auto p = worker_instance(world, host::Worker::none);
        check(p->restore({property}) == LV2_STATE_SUCCESS,
              std::string(name) + ": a path alone restores");
        p->activate();
        check(near(play_constant(*p, 2048).back(), .25f) &&
                  status_of(*p) == as_float(lv2::Status::running) &&
                  saved_path(*p) == files.a.string(),
              std::string(name) + ": the file plays and is saved");
    }
    // An unusable embedded copy falls back on the file; neither is a failed load.
    {
        const std::vector<std::byte> garbage(40, std::byte{0x5a});
        auto p = worker_instance(world, host::Worker::none);
        check(p->restore({host::Property{state_key, chunk(world), garbage, 0},
                          path_property(files.a.string(), atom_path)}) == LV2_STATE_SUCCESS,
              "a damaged embedded copy falls back on the file");
        p->activate();
        check(near(play_constant(*p, 2048).back(), .25f) && !p->failure() &&
                  p->log.contains(LV2_LOG__Error, "could not be read"),
              "the file plays; the damaged copy was explained");
        auto q = worker_instance(world, host::Worker::none);
        check(q->restore({host::Property{state_key, chunk(world), garbage, 0},
                          path_property(files.missing.string(), atom_path)}) != LV2_STATE_SUCCESS,
              "neither copy usable: restore fails");
        q->activate();
        check(status_of(*q) == as_float(lv2::Status::load_failed) && q->failure() &&
                  q->failure()->kind == HostFailureKind::filter_file && q->failure()->silent &&
                  silent(play_constant(*q, 1024)),
              "neither copy usable: a failed first load, silent");
        check(q->restore({path_property(files.wrong_rate.string(), atom_path)}) !=
                      LV2_STATE_SUCCESS &&
                  q->failure()->kind == HostFailureKind::sample_rate,
              "a saved path to a filter for another rate is recorded as such");
    }
    // lilv saves the path in its own state files and maps it back.
    {
        auto p = worker_instance(world, host::Worker::synchronous);
        p->activate();
        p->set_filter(files.b.string());
        settle(*p);
        // As Jalv does: scratch, copy, link and save folders, so lilv links
        // to the file from the saved state.
        const auto save_dir = fs::absolute(work / "lilv-state");
        const auto scratch = fs::absolute(work / "lilv-scratch");
        for (const auto& d : {save_dir, scratch}) {
            fs::remove_all(d);
            fs::create_directories(d);
        }
        const auto get_port = [](const char*, void*, std::uint32_t*,
                                 std::uint32_t*) -> const void* { return nullptr; };
        LilvState* state = lilv_state_new_from_instance(
            p->plugin(), p->instance(), urids.map_feature(), scratch.c_str(), save_dir.c_str(),
            save_dir.c_str(), save_dir.c_str(), get_port, nullptr,
            LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, nullptr);
        check(state != nullptr, "lilv saves an instance with a path");
        if (state) {
            check(lilv_state_save(world.get(), urids.map_feature(), urids.unmap_feature(), state,
                                  "urn:convolver:test-path-state", save_dir.c_str(),
                                  "state.ttl") == 0,
                  "lilv writes the state with a path");
            lilv_state_free(state);
            LilvNode* subject = lilv_new_uri(world.get(), "urn:convolver:test-path-state");
            LilvState* reread = lilv_state_new_from_file(world.get(), urids.map_feature(), subject,
                                                         (save_dir / "state.ttl").c_str());
            lilv_node_free(subject);
            check(reread != nullptr, "lilv reads the state with a path");
            if (reread) {
                auto q = worker_instance(world, host::Worker::synchronous);
                lilv_state_restore(reread, q->instance(), nullptr, nullptr, 0, nullptr);
                lilv_state_free(reread);
                q->activate();
                const auto path = saved_path(*q);
                check(near(play_constant(*q, 2048).back(), .75f) && !path.empty() &&
                          fs::equivalent(path, files.b),
                      "lilv's state restores the filter and its path (" + path + ")");
            }
        }
    }
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        std::cerr << "usage: " << argv[0]
                  << " BUNDLE_DIRECTORY EXAMPLES/first-use CONVOLVER_PRESET WORK_DIRECTORY\n";
        return 2;
    }
    try {
        host::World world(argv[1]);
        const fs::path examples = argv[2], tool = argv[3], work = argv[4];
        fs::create_directories(work);
        std::vector<Fixture> fixtures;
        for (const auto& [name, source] :
             {std::pair<std::string, fs::path>{"mono", examples / "mono" / "impulse.wav"},
              {"stereo", examples / "stereo" / "filter.cfg"},
              {"crossover", examples / "crossover" / "filter.cfg"}})
            fixtures.push_back({name, source, load_configuration(source),
                                read_audio(source.parent_path() / "input.wav")});
        bundle(world);
        instantiation(world);
        worker_loads(world, work);
        oversized_files(world, work);
        path_state(world, work);
        refused_first_loads(world, work);
        empty_state_resets(world, work);
        for (const auto& f : fixtures) {
            status_and_log(world, f);
            presets(world, f, tool, work);
            callback_sizes(world, f);
            // Scripted first: a failed first load saved, reopened and reset;
            // then a failed replacement across reopen, reset and bypass.
            lifecycle(world, f, 0, {2, 6, 5, 3, 0, 3, 2, 5, 6, 4, 5, 3, 1, 2, 6, 4, 5, 7, 2, 6});
            // Files: a missing first file reopened, a file saved and reopened
            // (the embedded copy wins), a state load clearing the path.
            lifecycle(world, f, 0, {9, 6, 5, 8, 3, 6, 5, 2, 6, 1, 8, 9, 6, 5, 7, 0, 6, 8, 4, 5});
            for (unsigned seed = 1; seed <= 4; ++seed)
                lifecycle(world, f, seed);
        }
    } catch (const std::exception& e) {
        std::cout << "FAIL  " << e.what() << '\n';
        return 1;
    }
    std::cout << "LV2 adapter tests: " << failures << " failed; worst error against the oracle "
              << worst << '\n';
    return failures ? 1 : 0;
}
