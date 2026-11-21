// SPDX-License-Identifier: GPL-2.0-or-later
// COMPAT-1: save and restore an LV2 host's state for Convolver, and render
// audio through the bundle as a host does. The bundle is loaded from its
// folder, so the same program captures state and audio with a published
// package's plug-in and checks the plug-in just built against them.
//
// Usage:
//   convolver_lv2_render capture BUNDLE STATE_DIR FILTER RATE INPUTS OUTPUTS GAIN MIX LATENCY
//   GAIN_MODE convolver_lv2_render render BUNDLE STATE_DIR RATE INPUTS OUTPUTS IN.f32 OUT.f32
//   FRAMES
//
// capture: instantiates the discrete INPUTS in, OUTPUTS out layout at RATE,
// sets its settings ports (Gain in dB, Mix in percent, Latency in frames,
// Gain mode as its port value), chooses FILTER through the filter parameter
// as a host's file chooser does, lets the worker load it, and saves the
// instance's state with lilv, as Jalv and other lilv hosts save a session,
// into STATE_DIR (state.ttl and manifest.ttl). FILTER must be inside
// STATE_DIR, so lilv stores its path relative to the state.
//
// render: reads STATE_DIR/state.ttl with lilv, restores it into a new
// instance (ports included), activates it, plays IN.f32 (interleaved 32-bit
// floats in native byte order) followed by silence in host blocks of
// irregular sizes, drops the latency the plug-in reports on its latency port
// and writes FRAMES interleaved output frames to OUT.f32. It prints the
// reported latency on stdout.
#include "host.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <lilv/lilv.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace host = convolver::lv2_test;

namespace {
void require(bool ok, const std::string& message) {
    if (!ok)
        throw std::runtime_error(message);
}

// The subject lilv gives the saved state; any URI works, so long as capture
// and render agree.
constexpr const char* state_subject = "urn:convolver:upgrade-state";
// LV2_ATOM__Float, spelt out: the atom header's include path names the
// format's folder, which only the LV2 build may name (tests/ci_scope_tests.py).
constexpr const char* atom_float_uri = "http://lv2plug.in/ns/ext/atom#Float";

// Host blocks of irregular sizes, as convolver_vst3_render plays them.
constexpr std::array<std::uint32_t, 8> block_sizes{1, 73, 137, 3, 129, 512, 64, 137};
constexpr std::int32_t max_block = 512;
// The status port's Running, the same in every published preview's Turtle.
constexpr float running = static_cast<float>(convolver::lv2::Status::running);

std::unique_ptr<host::Plugin> instance(host::World& world, int rate, int inputs, int outputs) {
    const LilvPlugin* plugin =
        world.plugin(static_cast<std::size_t>(inputs), static_cast<std::size_t>(outputs));
    require(plugin != nullptr, "the bundle has no discrete " + std::to_string(inputs) + " in, " +
                                   std::to_string(outputs) + " out layout");
    host::Features features;
    features.max_block = max_block;
    features.worker = host::Worker::synchronous;
    auto p = std::make_unique<host::Plugin>(world, plugin, rate, features);
    require(p->instantiated(), "the plug-in did not instantiate");
    return p;
}

// Messages sent, work done and its answers delivered, as a host's next few
// cycles do.
void settle(host::Plugin& p) {
    for (int i = 0; i < 4; ++i)
        p.run_empty();
}

// The settings ports, by symbol, as lilv saves and restores them.
float* port(host::Plugin& p, const char* symbol) {
    const std::string s = symbol;
    if (s == "enabled")
        return &p.enabled;
    if (s == "gain")
        return &p.gain;
    if (s == "mix")
        return &p.mix;
    if (s == "latency_frames")
        return &p.latency_frames;
    if (s == "gain_mode")
        return &p.gain_mode;
    return nullptr;
}

struct PortContext {
    host::Plugin* plugin = nullptr;
    LV2_URID atom_float = 0;
};

const void* get_port(const char* symbol, void* data, std::uint32_t* size, std::uint32_t* type) {
    auto* context = static_cast<PortContext*>(data);
    const float* value = port(*context->plugin, symbol);
    if (!value)
        return nullptr; // Output ports are not saved.
    *size = sizeof(float);
    *type = context->atom_float;
    return value;
}

void set_port(const char* symbol, void* data, const void* value, std::uint32_t size,
              std::uint32_t type) {
    auto* context = static_cast<PortContext*>(data);
    float* target = port(*context->plugin, symbol);
    require(target != nullptr, std::string("the saved state sets an unknown port ") + symbol);
    require(type == context->atom_float && size == sizeof(float),
            std::string("the saved state's value for ") + symbol + " is not a float");
    std::memcpy(target, value, sizeof(float));
}

std::vector<float> read_floats(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    require(file.good(), "cannot open " + path);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(file), {}};
    require(bytes.size() % sizeof(float) == 0, path + " is not a whole number of floats");
    std::vector<float> values(bytes.size() / sizeof(float));
    std::memcpy(values.data(), bytes.data(), bytes.size());
    return values;
}

int capture(const std::vector<std::string>& args) {
    require(args.size() == 10, "capture takes 10 arguments");
    const fs::path bundle = args[0];
    const fs::path state_dir = fs::absolute(args[1]);
    const fs::path filter = fs::absolute(args[2]);
    const int rate = std::stoi(args[3]);
    const int inputs = std::stoi(args[4]);
    const int outputs = std::stoi(args[5]);
    const auto [dir_end, _] =
        std::mismatch(state_dir.begin(), state_dir.end(), filter.begin(), filter.end());
    require(dir_end == state_dir.end(), "the filter must be inside the state folder");

    host::World world(bundle);
    auto p = instance(world, rate, inputs, outputs);
    p->gain = std::stof(args[6]);
    p->mix = std::stof(args[7]);
    p->latency_frames = std::stof(args[8]);
    p->gain_mode = std::stof(args[9]);
    p->activate();
    settle(*p);
    p->set_filter(filter.string());
    settle(*p);
    require(p->status == running,
            "the filter did not load (status port " + std::to_string(p->status) + ")");

    // As a host saves a session into its own folder. The filter is already
    // inside the state folder, so lilv stores its path relative to the
    // state and needs no folders to copy or link files into.
    PortContext context{p.get(), world.urids().map(atom_float_uri)};
    const std::string dir = state_dir.string() + "/";
    LilvState* state = lilv_state_new_from_instance(
        p->plugin(), p->instance(), world.urids().map_feature(), nullptr, nullptr, nullptr,
        dir.c_str(), get_port, &context, LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE, nullptr);
    require(state != nullptr, "lilv could not save the instance's state");
    const int saved =
        lilv_state_save(world.get(), world.urids().map_feature(), world.urids().unmap_feature(),
                        state, state_subject, dir.c_str(), "state.ttl");
    lilv_state_free(state);
    require(saved == 0, "lilv could not write the state");
    p->deactivate();
    std::cout << "saved " << (state_dir / "state.ttl").string() << '\n';
    return 0;
}

int render(const std::vector<std::string>& args) {
    require(args.size() == 8, "render takes 8 arguments");
    const fs::path bundle = args[0];
    const fs::path state_dir = fs::absolute(args[1]);
    const int rate = std::stoi(args[2]);
    const int inputs = std::stoi(args[3]);
    const int outputs = std::stoi(args[4]);
    const long long frames = std::stoll(args[7]);
    require(inputs > 0 && inputs <= 32 && outputs > 0 && outputs <= 32 && frames > 0 && rate > 0,
            "bad rate, channel counts or frame count");

    host::World world(bundle);
    LilvNode* subject = lilv_new_uri(world.get(), state_subject);
    LilvState* state = lilv_state_new_from_file(world.get(), world.urids().map_feature(), subject,
                                                (state_dir / "state.ttl").string().c_str());
    lilv_node_free(subject);
    require(state != nullptr, "lilv could not read " + (state_dir / "state.ttl").string());
    auto p = instance(world, rate, inputs, outputs);
    const std::string wanted = lilv_node_as_uri(lilv_plugin_get_uri(p->plugin()));
    const std::string saved = lilv_node_as_uri(lilv_state_get_plugin_uri(state));
    if (saved != wanted) {
        lilv_state_free(state);
        throw std::runtime_error("the state is for " + saved + ", not " + wanted);
    }
    // As a host opening a session: restore, ports included, then activate.
    PortContext context{p.get(), world.urids().map(atom_float_uri)};
    lilv_state_restore(state, p->instance(), set_port, &context, 0, nullptr);
    lilv_state_free(state);
    p->activate();
    settle(*p);
    require(p->status == running,
            "the restored state does not play (status port " + std::to_string(p->status) + ")");
    const long long latency = static_cast<long long>(p->latency);
    require(latency >= 0 && static_cast<float>(latency) == p->latency,
            "the latency port is not a frame count");

    const auto input = read_floats(args[5]);
    require(input.size() % static_cast<std::size_t>(inputs) == 0,
            "the input is not a whole number of frames");
    const long long input_frames = static_cast<long long>(input.size()) / inputs;
    std::vector<std::vector<float>> in(static_cast<std::size_t>(inputs),
                                       std::vector<float>(max_block));
    std::vector<std::vector<float>> out(static_cast<std::size_t>(outputs),
                                        std::vector<float>(max_block));
    std::vector<const float*> in_ptrs;
    std::vector<float*> out_ptrs;
    for (auto& c : in)
        in_ptrs.push_back(c.data());
    for (auto& c : out)
        out_ptrs.push_back(c.data());
    std::vector<float> result;
    result.reserve(static_cast<std::size_t>(frames * outputs));
    const long long total = latency + frames;
    long long frame = 0;
    for (std::size_t block = 0; frame < total; ++block) {
        const auto size = static_cast<long long>(
            std::min<long long>(block_sizes[block % block_sizes.size()], total - frame));
        for (long long i = 0; i < size; ++i)
            for (int c = 0; c < inputs; ++c)
                in[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] =
                    frame + i < input_frames
                        ? input[static_cast<std::size_t>((frame + i) * inputs + c)]
                        : 0.f;
        p->run(in_ptrs, out_ptrs, static_cast<std::uint32_t>(size));
        require(static_cast<long long>(p->latency) == latency,
                "the reported latency changed while playing");
        for (long long i = 0; i < size; ++i)
            if (frame + i >= latency)
                for (int c = 0; c < outputs; ++c)
                    result.push_back(out[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)]);
        frame += size;
    }
    p->deactivate();
    std::ofstream file(args[6], std::ios::binary);
    file.write(reinterpret_cast<const char*>(result.data()),
               static_cast<std::streamsize>(result.size() * sizeof(float)));
    require(file.good(), "cannot write " + args[6]);
    std::cout << "latency " << latency << '\n';
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::vector<std::string> args(argv + std::min(argc, 2), argv + argc);
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "capture")
            return capture(args);
        if (command == "render")
            return render(args);
        std::cerr << "usage: convolver_lv2_render capture BUNDLE STATE_DIR FILTER RATE INPUTS "
                     "OUTPUTS GAIN MIX "
                     "LATENCY GAIN_MODE\n"
                     "       convolver_lv2_render render BUNDLE STATE_DIR RATE INPUTS OUTPUTS "
                     "IN.f32 OUT.f32 "
                     "FRAMES\n";
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "convolver_lv2_render: " << error.what() << '\n';
        return 1;
    }
}
