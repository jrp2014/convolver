// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-7 for LV2: the plug-in loaded through lilv and driven by the lifecycle
// sequences of tests/plugins/lifecycle.hpp, against the reference model of the
// shared adapter contract and of the LV2 state and worker specifications.
// Beyond the other formats: an empty state map, filter files chosen on the
// filter parameter and queued for the host's worker (which this host runs only
// when a sequence says so, so requests can be outstanding across any other
// operation, teardown included), a host that cannot queue work, a request
// that overtakes one the worker is reading, and a host with no worker.
#include "../plugins/audio_guard.hpp"
#include "../plugins/lifecycle.hpp"
#include "convolver/io.hpp"
#include "host.hpp"
#include "plugin.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <lv2/atom/atom.h>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace convolver;
namespace lc = convolver::plugin_test::lifecycle;
namespace guard = convolver::plugin_test::audio_guard;
namespace host = convolver::lv2_test;
namespace fs = std::filesystem;

namespace {
std::vector<std::byte> encoded(const std::optional<Configuration>& c) {
    HostState s;
    s.configuration = c;
    return encode_host_state(s);
}

// A as a filter file: one WAV channel per path and a .cfg routing them. The
// file must load as exactly A, or the sequences would compare A's file
// with something else.
void write_filter(const fs::path& cfg, const Configuration& c) {
    const auto wav = cfg.parent_path() / (cfg.stem().string() + ".wav");
    std::size_t frames = 0;
    for (const auto& p : c.paths)
        frames = std::max(frames, p.impulse.size());
    std::vector<float> samples(frames * c.paths.size());
    for (std::size_t k = 0; k < c.paths.size(); ++k)
        for (std::size_t t = 0; t < c.paths[k].impulse.size(); ++t)
            samples[t * c.paths.size() + k] = c.paths[k].impulse[t];
    fs::remove(wav);
    AudioWriter writer(wav, {c.sample_rate, c.paths.size()}, OutputFormat::wav_float);
    writer.write(samples);
    writer.close();
    const auto routes = [](const std::vector<Route>& list) {
        std::string text;
        for (const auto& r : list) {
            if (!text.empty())
                text += ' ';
            // "channel.fraction": the fraction is the gain; whole gain 1.
            text += std::to_string(r.channel);
            if (r.gain != 1) {
                auto g = std::to_string(r.gain);
                text += g.substr(g.find('.'));
            }
        }
        return text;
    };
    std::ofstream out(cfg);
    out << c.sample_rate << ' ' << c.input_channels << ' ' << c.output_channels << " 0\n";
    for (std::size_t i = 0; i < c.input_channels; ++i)
        out << (i ? " 0" : "0");
    out << '\n';
    for (std::size_t o = 0; o < c.output_channels; ++o)
        out << (o ? " 0" : "0");
    out << '\n';
    for (std::size_t k = 0; k < c.paths.size(); ++k)
        out << wav.filename().string() << '\n'
            << k << '\n'
            << routes(c.paths[k].inputs) << '\n'
            << routes(c.paths[k].outputs) << '\n';
    out.close();
    if (!out || encoded(load_configuration(cfg, c.sample_rate)) != encoded(c))
        throw std::runtime_error("the filter file " + cfg.string() + " does not load as A");
}

class Lv2 final : public lc::Driver {
  public:
    Lv2(host::World& world, host::Worker worker) : world_(world), worker_(worker) {}
    lc::Format format() const override {
        using lc::Op;
        lc::Format f;
        f.name = worker_ == host::Worker::none ? "LV2 without a worker" : "LV2";
        // Contract, loading and failure: LV2 knows the rate at instantiation
        // and its ports are fixed, so both kinds of check run in restore().
        f.known = lc::Known::always;
        f.worker = worker_ != host::Worker::none;
        f.status = true;
        f.path = true;
        f.operations = {Op::state_a,
                        Op::state_b,
                        Op::state_other_rate,
                        Op::state_malformed,
                        Op::state_empty,
                        Op::state_failed,
                        Op::play,
                        Op::bypass,
                        Op::reset,
                        Op::stop,
                        Op::start,
                        Op::reopen,
                        Op::empty_map,
                        Op::file_a,
                        Op::file_missing,
                        Op::file_refused};
        if (f.worker) {
            f.operations.push_back(Op::work);
            f.operations.push_back(Op::overtake);
        }
        return f;
    }
    void create(const lc::Host& h, bool) override {
        destroy();
        host_ = h;
        plugin_ = instance();
    }
    void configure(const lc::Host&) override {
        throw std::logic_error("LV2 fixes the rate and channels at instantiation");
    }
    void destroy() override {
        plugin_.reset(); // Drops queued work, as hosts stop their worker first.
    }
    bool restore(std::span<const std::byte> bytes) override {
        return plugin_->restore(bytes, world_.urids().map(LV2_ATOM__Chunk)) == LV2_STATE_SUCCESS;
    }
    bool restore_empty_map() override {
        return plugin_->restore_empty() == LV2_STATE_SUCCESS;
    }
    bool start() override {
        plugin_->activate();
        return true;
    }
    void stop() override {
        plugin_->deactivate();
    }
    bool process(std::span<const float* const> in, std::span<float* const> out,
                 std::size_t frames) override {
        plugin_->run(in, out, static_cast<std::uint32_t>(frames));
        return true;
    }
    void set_bypass(bool on) override {
        plugin_->enabled = on ? 0.f : 1.f; // lv2:enabled, read at the next run().
    }
    void reset() override {
        // Contract, format notes: activation is LV2's reset.
        plugin_->deactivate();
        plugin_->activate();
    }
    bool reopen(const lc::Host& h, bool active, bool bypass) override {
        const auto saved = plugin_->save();
        create(h, true);
        set_bypass(bypass); // The host restores its port values.
        const bool accepted = plugin_->restore(saved.all) == LV2_STATE_SUCCESS;
        if (active)
            plugin_->activate();
        return accepted;
    }
    HostState saved() override {
        return decode_host_state(plugin_->save().bytes);
    }
    std::string saved_path() override {
        const auto saved = plugin_->save();
        if (!saved.filter)
            return {};
        const auto& v = saved.filter->value;
        return {reinterpret_cast<const char*>(v.data()),
                strnlen(reinterpret_cast<const char*>(v.data()), v.size())};
    }
    int status() override {
        // The first run() delivers the worker's answers after it, the
        // second shows them, as a host's next two cycles would.
        plugin_->run_empty();
        plugin_->run_empty();
        return static_cast<int>(plugin_->status);
    }
    std::optional<HostFailure> failure() override {
        return plugin_->failure();
    }
    std::size_t max_block() const override {
        return 512;
    }
    void choose_file(const fs::path& path, bool refused) override {
        plugin_->scheduling_fails = refused;
        plugin_->set_filter(path.string());
        plugin_->run_empty();
        plugin_->scheduling_fails = false;
    }
    void work() override {
        plugin_->run_deferred_work();
    }
    void overtake(const fs::path& path) override {
        // The worker reads the queued request on its own thread and pauses
        // once it has read the file; meanwhile run() receives a new request.
        plugin_->pause_at(lv2::WorkerPoint::read);
        std::thread worker([&] { plugin_->run_deferred_work(1); });
        plugin_->wait_until_paused();
        plugin_->set_filter(path.string());
        plugin_->run_empty();
        plugin_->resume();
        worker.join();
    }

    // The guard must see the plug-in's own allocations and locks, which only
    // happens if the plug-in library resolves them to this executable.
    void check_guard() {
        auto p = instance();
        const auto* state = static_cast<const LV2_State_Interface*>(
            lilv_instance_get_extension_data(p->instance(), LV2_STATE__interface));
        const auto store = [](LV2_State_Handle, std::uint32_t, const void*, std::size_t,
                              std::uint32_t, std::uint32_t) { return LV2_STATE_SUCCESS; };
        guard::take();
        {
            guard::Scope watched; // save() locks, and encodes the state.
            state->save(lilv_instance_get_handle(p->instance()), store, nullptr, 0, nullptr);
        }
        const auto seen = guard::take();
        if (!seen.allocations || (guard::counts_locks() && !seen.locks))
            throw std::runtime_error(
                "the plug-in's allocations or locks do not reach the audio-thread guard (is "
                "the test executable built with ENABLE_EXPORTS?)");
    }

  private:
    std::unique_ptr<host::Plugin> instance() {
        host::Features features;
        features.worker = worker_;
        features.max_block = static_cast<std::int32_t>(max_block());
        auto p = std::make_unique<host::Plugin>(world_, world_.plugin(host_.inputs, host_.outputs),
                                                host_.sample_rate, features);
        if (!p->instantiated())
            throw std::runtime_error("instantiation failed");
        return p;
    }
    host::World& world_;
    host::Worker worker_;
    lc::Host host_{48000, 2, 2};
    std::unique_ptr<host::Plugin> plugin_;
};
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " BUNDLE_DIRECTORY WORK_DIRECTORY [LENGTH]\n";
        return 2;
    }
    try {
        host::World world(argv[1]);
        const fs::path work = argv[2];
        lc::Options options;
        options.length = argc > 3 ? std::stoul(argv[3]) : 3;
        // Found by these sequences: a refused first load right after an empty
        // state was restored while active played the unfiltered input
        // (HostProcessor adopted the empty instance's identity after mute()).
        for (const char* s : {"start state-A play state-empty file-refused listen",
                              "start state-A play empty-map file-refused listen",
                              "start file-A state-empty file-refused listen"})
            options.fixed.push_back(lc::parse(s));
        std::size_t failures = 0;
        for (const lc::Host h : {lc::Host{48000, 2, 2}, lc::Host{48000, 1, 2}}) {
            auto filters = lc::make_filters(h);
            const auto folder =
                work / ("lifecycle-" + std::to_string(h.inputs) + "-" + std::to_string(h.outputs));
            fs::create_directories(folder);
            filters.a_file = folder / "a.cfg";
            filters.missing_file = folder / "missing.cfg";
            fs::remove(filters.missing_file);
            write_filter(filters.a_file, filters.a);
            for (const auto worker : {host::Worker::deferred, host::Worker::none}) {
                Lv2 driver(world, worker);
                driver.check_guard();
                auto o = options;
                if (worker == host::Worker::none)
                    o.length = std::min<std::size_t>(o.length, 2);
                else
                    // Starts with a request queued for the worker, so it can be
                    // outstanding across every other operation.
                    o.starts = {{true, {}},
                                {true, {lc::Op::start, lc::Op::state_a, lc::Op::play}},
                                {true, {lc::Op::start, lc::Op::file_a}}};
                failures += lc::run(driver, filters, o);
            }
        }
        std::cout << "LV2 lifecycle sequences: " << failures << " failed\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
