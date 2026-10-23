// SPDX-License-Identifier: GPL-2.0-or-later
// The LV2 adapter hosted by lilv and driven by the shared PLUG-2 harness.
#include "../plugins/harness.hpp"
#include "convolver/host.hpp"
#include "host.hpp"
#include <iostream>
#include <lv2/atom/atom.h>
#include <memory>
#include <stdexcept>

namespace test = convolver::plugin_test;
namespace host = convolver::lv2_test;

namespace {
class Lv2 final : public test::Adapter {
  public:
    explicit Lv2(const std::filesystem::path& bundle) : world_(bundle) {}
    std::string format() const override {
        return "LV2";
    }
    void create(const test::Setup& setup) override {
        destroy();
        const auto* plugin = world_.plugin(setup.inputs, setup.outputs);
        if (!plugin)
            throw std::runtime_error("no LV2 plug-in for " + std::to_string(setup.inputs) +
                                     " inputs and " + std::to_string(setup.outputs) + " outputs");
        // LV2 fixes the rate and channels at instantiation; the host passes its
        // largest callback as bufsz:maxBlockLength.
        host::Features features;
        features.max_block = static_cast<std::int32_t>(setup.max_block);
        plugin_ = std::make_unique<host::Plugin>(world_, plugin, setup.sample_rate, features);
        if (!plugin_->instantiated())
            throw std::runtime_error("LV2 instantiation failed");
        ran_ = false;
    }
    void destroy() override {
        plugin_.reset();
    }
    bool load(const test::Filter& filter) override {
        convolver::HostState state;
        state.configuration = filter.configuration;
        const auto bytes = convolver::encode_host_state(state);
        return plugin_->restore(bytes, world_.urids().map(LV2_ATOM__Chunk)) == LV2_STATE_SUCCESS;
    }
    bool activate() override {
        plugin_->activate();
        ran_ = false;
        return true;
    }
    void deactivate() override {
        plugin_->deactivate();
    }
    bool process(const test::Block& block) override {
        // LV2 has no silence flags: silent inputs are just zeros.
        plugin_->run(block.inputs, block.outputs, static_cast<std::uint32_t>(block.frames));
        ran_ = true;
        return true;
    }
    void set_bypass(bool on) override {
        plugin_->enabled = on ? 0.f : 1.f; // lv2:enabled, read at the start of run().
    }
    void reset() override {
        // LV2 has no separate reset: activate() clears all history.
        plugin_->deactivate();
        plugin_->activate();
    }
    std::size_t latency_frames() override {
        // The latency output port is written in run(); hosts that need it
        // before audio run the plug-in for zero frames.
        if (!ran_)
            plugin_->run_empty();
        return static_cast<std::size_t>(plugin_->latency);
    }
    std::size_t tail_frames() override {
        return 0;
    }
    bool reports_tail() const override {
        return false; // LV2 core has no tail property.
    }
    std::optional<convolver::Configuration> saved() override {
        const auto saved = plugin_->save();
        if (saved.status != LV2_STATE_SUCCESS || saved.type != world_.urids().map(LV2_ATOM__Chunk))
            throw std::runtime_error("LV2 save failed");
        return convolver::decode_host_state(saved.bytes).configuration;
    }

  private:
    host::World world_;
    std::unique_ptr<host::Plugin> plugin_;
    bool ran_ = false;
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: " << argv[0] << " BUNDLE_DIRECTORY FIRST_USE_DIRECTORY/fixtures.txt\n";
        return 2;
    }
    try {
        // The adapter follows the shared adapter contract, so no case is a
        // known deviation. Restore-before-setup cases are skipped: LV2 knows
        // its rate and channels from instantiation. Tail checks are skipped:
        // LV2 core cannot report one.
        Lv2 adapter(argv[1]);
        return test::run(adapter, argv[2]);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
