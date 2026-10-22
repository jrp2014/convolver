// SPDX-License-Identifier: GPL-2.0-or-later
// LV2 adapter tests through lilv, beyond the shared PLUG-2 cases:
// - the bundle: every PLUG-4 layout, port numbers and properties as plugin.hpp says;
// - instantiation: urid:map required, whole sample rates only;
// - status port, log messages and saved state after good and bad loads;
// - presets written by convolver-preset, and lilv's own state round trip;
// - run() calls longer than bufsz:maxBlockLength (or without it);
// - seeded lifecycle sequences checked against a small model after every step.
#include "../native/oracle.hpp"
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "convolver_identities.hpp"
#include "host.hpp"
#include "plugin.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <lv2/atom/atom.h>
#include <lv2/core/lv2.h>
#include <lv2/log/log.h>
#include <lv2/presets/presets.h>
#include <random>
#include <sstream>
#include <stdexcept>

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
    for (const auto& layout : identity::fixed_layouts) {
        const auto uri = std::string(identity::lv2_base_uri) + std::string(layout.id);
        const auto* p = world.plugin(uri);
        check(p != nullptr, uri + ": in the bundle");
        if (!p)
            continue;
        check(lilv_plugin_verify(p), uri + ": lilv_plugin_verify");
        const auto ni = static_cast<std::uint32_t>(layout.inputs);
        const auto no = static_cast<std::uint32_t>(layout.outputs);
        check(lilv_plugin_get_num_ports(p) == lv2::first_audio_port + ni + no,
              uri + ": port count");
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
        check(points && lilv_scale_points_size(points) == 4, uri + ": four status labels");
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
    for (auto* n :
         {enabled, latency_designation, reports, audio, control, input, output, state_interface})
        lilv_node_free(n);
}

// --- Instantiation ----------------------------------------------------------------
void instantiation(host::World& world) {
    const auto* p = world.plugin(2, 2);
    host::Features no_map;
    no_map.map = false;
    check(!host::Plugin(world, p, 48000, no_map).instantiated(), "refuses to run without urid:map");
    for (const double rate : {0., -48000., 44100.5, 1e9, std::nan("")})
        check(!host::Plugin(world, p, rate).instantiated(),
              "refuses a sample rate of " + std::to_string(rate));
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

    check(p->restore(state_of(wrong_rate(a)), chunk(world)) != LV2_STATE_SUCCESS,
          label + "first load at the wrong rate is rejected");
    p->run_empty();
    check(p->status == static_cast<float>(lv2::Status::load_failed),
          label + "failed first load: status load failed");
    check(p->log.contains(LV2_LOG__Error, "does not resample"),
          label + "failed first load: the explanation reaches log:log");
    {
        const auto s = decode_host_state(p->save().bytes);
        check(s.failed && !s.configuration, label + "failed first load is saved as a failure");
    }
    check(silent(render(*p, f.input, 2000, 300)), label + "failed first load plays silence");

    check(p->restore(state_of(a), chunk(world)) == LV2_STATE_SUCCESS, label + "valid load");
    p->run_empty();
    check(p->status == static_cast<float>(lv2::Status::running), label + "status running");

    p->log.messages.clear();
    check(p->restore(state_of(wrong_outputs(a)), chunk(world)) != LV2_STATE_SUCCESS,
          label + "replacement with the wrong outputs is rejected");
    p->run_empty();
    check(p->status == static_cast<float>(lv2::Status::load_failed),
          label + "failed replacement: status load failed");
    check(p->log.contains(LV2_LOG__Error, "previous filter is kept"),
          label + "failed replacement: logged, previous filter kept");
    check(same(decode_host_state(p->save().bytes).configuration, a),
          label + "failed replacement: saved state is the running filter");

    p->log.messages.clear();
    check(p->restore(state_of(a), world.urids().map(LV2_ATOM__String)) == LV2_STATE_ERR_BAD_TYPE,
          label + "state of the wrong type is rejected");
    check(p->log.contains(LV2_LOG__Error, "wrong type"), label + "wrong type is logged");
    const std::vector<std::byte> garbage(40, std::byte{0x5a});
    p->log.messages.clear();
    check(p->restore(garbage, chunk(world)) != LV2_STATE_SUCCESS, label + "corrupt state rejected");
    check(p->log.contains(LV2_LOG__Error, "could not be read"), label + "corrupt state is logged");
    check(same(decode_host_state(p->save().bytes).configuration, a),
          label + "rejections keep the running filter saved");
    check(p->restore_empty() == LV2_STATE_SUCCESS &&
              same(decode_host_state(p->save().bytes).configuration, a),
          label + "state without our property changes nothing");

    const auto saved = p->save();
    check(saved.type == chunk(world) && (saved.flags & LV2_STATE_IS_POD) &&
              (saved.flags & LV2_STATE_IS_PORTABLE),
          label + "saved as a portable plain chunk");

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
    lv2::Status status() const {
        if (error)
            return lv2::Status::load_failed;
        return saved ? lv2::Status::running : lv2::Status::no_filter;
    }
};

void lifecycle(host::World& world, const Fixture& f, unsigned seed) {
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
    for (int step = 0; step < 40; ++step) {
        const int op = std::uniform_int_distribution<int>(0, 7)(random);
        switch (op) {
        case 0:
        case 1: { // A valid load (A or B).
            const auto& c = op == 0 ? a : b;
            trace << (op == 0 ? " load-A" : " load-B");
            check(p->restore(state_of(c), chunk(world)) == LV2_STATE_SUCCESS,
                  label() + ": accepted");
            m.saved = c;
            m.failed = m.error = false;
            break;
        }
        case 2: // A load that does not suit the host.
            trace << " load-wrong-rate";
            check(p->restore(state_of(wrong_rate(a)), chunk(world)) != LV2_STATE_SUCCESS,
                  label() + ": rejected");
            m.failed = !m.saved;
            m.error = true;
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
            check(p->restore(saved.bytes, saved.type) == LV2_STATE_SUCCESS, label() + ": reopened");
            m.error = m.failed; // A saved failure is reported again; a kept filter is not.
            p->activate();
            break;
        }
        default: // The host restores port values only.
            trace << " restore-nothing";
            check(p->restore_empty() == LV2_STATE_SUCCESS, label() + ": empty restore");
            break;
        }
        const auto s = decode_host_state(p->save().bytes);
        check(same(s.configuration, m.saved) && s.failed == m.failed, label() + ": saved state");
        p->run_empty();
        check(p->status == static_cast<float>(m.status()),
              label() + ": status " + std::to_string(p->status) + ", expected " +
                  std::to_string(static_cast<int>(m.status())));
        check(p->latency == 256, label() + ": latency");
    }
    p->deactivate();
    p->activate();
    check(fresh_output_ok(), label() + ": final output");
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
        for (const auto& f : fixtures) {
            status_and_log(world, f);
            presets(world, f, tool, work);
            callback_sizes(world, f);
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
