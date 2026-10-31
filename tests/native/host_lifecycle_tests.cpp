// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-7 for the shared host layer: HostFilter and HostProcessor driven by a
// minimal adapter that does only what the shared adapter contract
// (docs/develop/plugin-contract.md) tells every adapter to do, through the
// lifecycle sequences of tests/plugins/lifecycle.hpp. A failure here is a
// defect in the host layer (or in the contract), before any format is involved.
#include "../plugins/audio_guard.hpp"
#include "../plugins/lifecycle.hpp"
#include "convolver/host.hpp"
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

using namespace convolver;
namespace lc = convolver::plugin_test::lifecycle;
namespace guard = convolver::plugin_test::audio_guard;

namespace {
class Reference final : public lc::Driver {
  public:
    lc::Format format() const override {
        using lc::Op;
        lc::Format f;
        f.name = "host layer";
        // As VST3: the configuration binds at activation and stays known while
        // it is unchanged.
        f.known = lc::Known::activation;
        f.operations = {Op::state_a,
                        Op::state_b,
                        Op::state_other_rate,
                        Op::state_malformed,
                        Op::state_empty,
                        Op::state_failed,
                        Op::state_b_starved,
                        Op::state_b_shortage,
                        Op::state_malformed_starved,
                        Op::play,
                        Op::bypass,
                        Op::reset,
                        Op::stop,
                        Op::start,
                        Op::reopen,
                        Op::rate,
                        Op::layout,
                        Op::setup};
        return f;
    }
    void create(const lc::Host& host, bool configured) override {
        destroy();
        filter_.emplace(8);
        host_ = host;
        bound_ = configured ? std::optional(host) : std::nullopt;
        bypass_ = false;
    }
    void configure(const lc::Host& host) override {
        host_ = host;
    }
    void destroy() override {
        processor_.reset();
        filter_.reset();
    }
    bool restore(std::span<const std::byte> bytes) override {
        const bool was_failed = filter_->failed();
        try {
            HostState state;
            try {
                state = decode_host_state(bytes);
            } catch (const NewerHostState& e) {
                filter_->reject(HostFailureKind::state_newer, e.what());
                throw;
            } catch (const std::exception& e) {
                filter_->reject(HostFailureKind::state_unreadable, e.what());
                throw;
            }
            // Contract, replacement and threads: while active, a prepared
            // replacement is published; stopped, activation installs it.
            const auto host = known();
            filter_->load(std::move(state), host, [&](const Configuration& c) {
                if (processor_)
                    processor_->replace(c);
            });
            return true;
        } catch (...) {
            // Contract, loading and failure: a failed first load plays
            // silence, never the unfiltered input.
            // ROBUST-3: without allocating, as memory may have run out.
            if (processor_ && filter_->failed() && !was_failed)
                processor_->silence();
            return false;
        }
    }
    std::optional<bool> restore_starved(std::span<const std::byte> bytes, std::size_t allowed,
                                        bool once) override {
        guard::Starve starve(allowed, once);
        return restore(bytes);
    }
    bool start() override {
        processor_ = std::make_unique<HostProcessor>(filter_->activate(layout()), max_block());
        processor_->set_bypass(bypass_);
        processor_->reset();
        bound_ = host_;
        return true;
    }
    void stop() override {
        processor_.reset();
    }
    bool process(std::span<const float* const> in, std::span<float* const> out,
                 std::size_t frames) override {
        guard::Scope audio;
        processor_->set_bypass(bypass_);
        return processor_->process(in, out, frames);
    }
    void set_bypass(bool on) override {
        bypass_ = on;
    }
    void reset() override {
        guard::Scope audio;
        processor_->reset();
    }
    bool reopen(const lc::Host& host, bool active, bool bypass) override {
        const auto bytes = encode_host_state(saved());
        create(host, false);
        const bool accepted = restore(bytes);
        configure(host);
        bypass_ = bypass;
        if (active)
            start();
        return accepted;
    }
    HostState saved() override {
        if (processor_)
            processor_->collect();
        HostState s;
        s.configuration = filter_->filter();
        s.failed = filter_->failed();
        return s;
    }
    std::optional<HostFailure> failure() override {
        return filter_->failure();
    }
    std::size_t max_block() const override {
        return 512;
    }

  private:
    HostLayout layout() const {
        return {host_.sample_rate, host_.inputs, host_.outputs};
    }
    std::optional<HostLayout> known() const {
        if (processor_ || bound_ == host_)
            return layout();
        return std::nullopt;
    }
    std::optional<HostFilter> filter_;
    std::unique_ptr<HostProcessor> processor_;
    lc::Host host_;
    std::optional<lc::Host> bound_; // The configuration of the last activation.
    bool bypass_ = false;
};
} // namespace

int main(int argc, char** argv) {
    try {
        lc::Options options;
        options.length = argc > 1 ? std::stoul(argv[1]) : 5;
        // Fixed sequences: the defects found by review, as regressions.
        for (const char* s : {
                 // 0.4.19: a filter loaded after a reset during a crossfade
                 // waited for the interrupted transition.
                 "start state-A play state-B play reset state-A listen",
                 // 0.4.30: a failed first load must play silence.
                 "start state-other-rate listen play state-A listen",
                 // Out of memory at each allocation of a load: first, as a
                 // replacement, inactive, and before the host is configured.
                 "start state-B-starved listen",
                 "start state-A play state-B-starved listen",
                 "state-A state-B-starved start listen",
                 "state-B-starved setup start listen",
                 // ROBUST-3: memory runs out just after a failed first load
                 // is recorded, before audio and while it plays.
                 "start state-malformed-starved listen",
                 "start play state-malformed-starved listen",
             })
            options.fixed.push_back(lc::parse(s));
        std::size_t failures = 0;
        for (const lc::Host host : {lc::Host{48000, 2, 2}, lc::Host{48000, 1, 2}})
            failures += lc::run(*std::make_unique<Reference>(), lc::make_filters(host), options);
        std::cout << "host lifecycle sequences: " << failures << " failed\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
