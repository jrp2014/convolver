// SPDX-License-Identifier: GPL-2.0-or-later
// The Audio Unit adapter driven through the Audio Unit API by the shared PLUG-2
// harness. Filters load as ClassInfo dictionaries made by the .aupreset writer
// of convolver-preset (AU-2); latency and tail are read in seconds and
// converted to frames.
#include "au/au_host.hpp"
#include "plugins/harness.hpp"
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace test = convolver::plugin_test;
namespace host = convolver::au_test;

namespace {
class AudioUnitAdapter final : public test::Adapter {
  public:
    std::string format() const override {
        return "AU";
    }
    void create(const test::Setup& setup) override {
        create_unconfigured();
        configure(setup);
        // Like the VST3 driver: a host commits to its formats once it has
        // rendered with them (an initialization alone may be a layout probe),
        // so a configured instance has rendered one block. Before that,
        // restored state is checked at initialization (the
        // restore-before-setup cases).
        if (!activate())
            throw std::runtime_error("AU first initialization failed");
        std::vector<float> in(setup.inputs), out(setup.outputs);
        std::vector<const float*> ins;
        std::vector<float*> outs;
        for (auto& v : in)
            ins.push_back(&v);
        for (auto& v : out)
            outs.push_back(&v);
        bool silent = false;
        host::check(unit_->render(ins, outs, 1, false, silent), "first render");
        deactivate();
    }
    bool create_unconfigured() override {
        destroy();
        unit_ = std::make_unique<host::Unit>();
        rate_ = 44100; // AUBase's default format until the host sets one.
        return true;
    }
    void configure(const test::Setup& setup) override {
        host::check(unit_->configure(setup.sample_rate, static_cast<UInt32>(setup.inputs),
                                     static_cast<UInt32>(setup.outputs),
                                     static_cast<UInt32>(setup.max_block)),
                    "format negotiation");
        rate_ = setup.sample_rate;
    }
    void destroy() override {
        unit_.reset();
    }
    bool load(const test::Filter& filter) override {
        convolver::HostState state;
        state.configuration = filter.configuration;
        // Bypass off, as convolver-preset writes it: the host owns bypass.
        return unit_->load(state, filter.name) == noErr;
    }
    bool activate() override {
        return unit_->initialize() == noErr;
    }
    void deactivate() override {
        unit_->uninitialize();
    }
    bool process(const test::Block& block) override {
        // An AU input carries one silence flag for the whole bus.
        const auto all = block.inputs.size() >= 64 ? ~std::uint64_t(0)
                                                   : (std::uint64_t(1) << block.inputs.size()) - 1;
        const bool silent_input = !block.inputs.empty() && (block.silent_inputs & all) == all;
        bool silent_output = false;
        const auto status =
            unit_->render(block.inputs, block.outputs, static_cast<UInt32>(block.frames),
                          silent_input, silent_output);
        silent_outputs_ = silent_output ? ~std::uint64_t(0) : 0;
        return status == noErr;
    }
    void set_bypass(bool on) override {
        unit_->set_bypass(on);
    }
    void reset() override {
        unit_->reset();
    }
    std::size_t latency_frames() override {
        return frames(unit_->seconds(kAudioUnitProperty_Latency));
    }
    std::size_t tail_frames() override {
        return frames(unit_->seconds(kAudioUnitProperty_TailTime));
    }
    std::optional<convolver::Configuration> saved() override {
        return unit_->saved().configuration;
    }
    std::optional<std::uint64_t> silent_outputs() override {
        return silent_outputs_;
    }

  private:
    std::size_t frames(Float64 seconds) const {
        return static_cast<std::size_t>(std::llround(seconds * rate_));
    }
    std::unique_ptr<host::Unit> unit_;
    int rate_ = 44100;
    std::uint64_t silent_outputs_ = 0;
};
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: " << argv[0] << " FIRST_USE_DIRECTORY/fixtures.txt\n";
        return 2;
    }
    try {
        // The adapter follows the shared adapter contract
        // (docs/develop/plugin-contract.md), so
        // no case is listed as a known deviation.
        const test::Options options;
        AudioUnitAdapter adapter;
        return test::run(adapter, argv[1], options);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
