// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-7 for Audio Units: the unit registered in-process and driven through
// the Audio Unit API by the lifecycle sequences of tests/plugins/lifecycle.hpp,
// against the reference model of the shared adapter contract. Filters arrive
// as ClassInfo dictionaries, as .aupreset files and saved sessions hold them.
#include "au/au_host.hpp"
#include "plugins/audio_guard.hpp"
#include "plugins/lifecycle.hpp"
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

using namespace convolver;
namespace lc = convolver::plugin_test::lifecycle;
namespace guard = convolver::plugin_test::audio_guard;
namespace host = convolver::au_test;

namespace {
class AudioUnitDriver final : public lc::Driver {
  public:
    lc::Format format() const override {
        using lc::Op;
        lc::Format f;
        f.name = "AU";
        // The unit counts the host's formats as known while initialized, and
        // afterwards while they match the last initialization that rendered
        // (plugins/au/effect.hpp: a JUCE host's layout probe does not bind).
        f.known = lc::Known::rendering;
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
    void create(const lc::Host& h, bool configured) override {
        destroy();
        unit_ = std::make_unique<host::Unit>();
        if (!configured)
            return;
        // As PLUG-2's driver: the formats bind once the unit has rendered.
        configure(h);
        host::check(unit_->initialize(), "first initialization");
        std::vector<float> in(h.inputs), out(h.outputs);
        std::vector<const float*> ins;
        std::vector<float*> outs;
        for (auto& v : in)
            ins.push_back(&v);
        for (auto& v : out)
            outs.push_back(&v);
        bool silent = false;
        host::check(unit_->render(ins, outs, 1, false, silent), "first render");
        unit_->uninitialize();
    }
    void configure(const lc::Host& h) override {
        host::check(unit_->configure(h.sample_rate, static_cast<UInt32>(h.inputs),
                                     static_cast<UInt32>(h.outputs),
                                     static_cast<UInt32>(max_block())),
                    "format negotiation");
    }
    void destroy() override {
        unit_.reset();
    }
    bool restore(std::span<const std::byte> bytes) override {
        return restore(bytes, std::nullopt);
    }
    std::optional<bool> restore_starved(std::span<const std::byte> bytes, std::size_t allowed,
                                        bool once) override {
        return restore(bytes, std::pair(allowed, once));
    }
    bool restore(std::span<const std::byte> bytes,
                 std::optional<std::pair<std::size_t, bool>> starve) {
        // A Convolver ClassInfo dictionary holding these bytes as its state.
        const auto base = host::class_info(HostState{});
        const auto dict =
            CFDictionaryCreateMutableCopy(nullptr, 0, static_cast<CFDictionaryRef>(base));
        CFRelease(base);
        const auto data = CFDataCreate(nullptr, reinterpret_cast<const UInt8*>(bytes.data()),
                                       static_cast<CFIndex>(bytes.size()));
        const auto key = host::state_key();
        CFDictionarySetValue(dict, key, data);
        CFRelease(key);
        CFRelease(data);
        OSStatus status = noErr;
        if (starve) {
            // Only the unit's own C++ allocations fail; Core Foundation's
            // do not go through operator new.
            guard::Starve limit(starve->first, starve->second);
            status = unit_->restore(dict);
        } else
            status = unit_->restore(dict);
        CFRelease(dict);
        return status == noErr;
    }
    bool start() override {
        return unit_->initialize() == noErr;
    }
    void stop() override {
        unit_->uninitialize();
    }
    bool process(std::span<const float* const> in, std::span<float* const> out,
                 std::size_t frames) override {
        bool silent = false;
        guard::Scope audio;
        return unit_->render(in, out, static_cast<UInt32>(frames), false, silent) == noErr;
    }
    void set_bypass(bool on) override {
        unit_->set_bypass(on);
    }
    void reset() override {
        unit_->reset();
    }
    bool reopen(const lc::Host& h, bool active, bool bypass) override {
        const auto saved = unit_->class_info_now();
        // Hosts restore a project's ClassInfo before setting its formats, and
        // restore the bypass property themselves.
        create(h, false);
        const bool accepted = unit_->restore(saved) == noErr;
        CFRelease(saved);
        configure(h);
        unit_->set_bypass(bypass);
        if (active)
            host::check(unit_->initialize(), "initialization after reopening");
        return accepted;
    }
    HostState saved() override {
        return unit_->saved();
    }
    std::optional<HostFailure> failure() override {
        const auto t = unit_->failure();
        if (!t.present)
            return std::nullopt;
        HostFailure f;
        f.kind = static_cast<HostFailureKind>(t.kind);
        f.request.number = t.request;
        f.silent = t.silent != 0;
        return f;
    }
    std::size_t max_block() const override {
        return 512;
    }

  private:
    std::unique_ptr<host::Unit> unit_;
};
} // namespace

int main(int argc, char** argv) {
    try {
        lc::Options options;
        if (argc > 1)
            options.length = std::stoul(argv[1]);
        // AU-specific: state restored before the formats are set, at another
        // rate, then the rate corrected (contract, loading and failure); an
        // initialization that never rendered does not bind the formats.
        for (const char* s : {
                 "state-other-rate setup start listen stop rate start listen",
                 "setup start stop state-other-rate start listen",
                 // Out of memory at each allocation of a restore: a first
                 // load, a replacement, uninitialized, before the formats.
                 // ROBUST-3: memory runs out just after a failed first load
                 // is recorded, before audio and while it plays.
                 "start state-malformed-starved listen",
                 "start play state-malformed-starved listen",
                 "start state-B-starved listen",
                 "start state-A play state-B-starved listen",
                 "state-A state-B-starved start listen",
                 "state-B-starved setup start listen",
             })
            options.fixed.push_back(lc::parse(s));
        std::size_t failures = 0;
        AudioUnitDriver driver;
        for (const lc::Host h : {lc::Host{48000, 2, 2}, lc::Host{48000, 1, 2}})
            failures += lc::run(driver, lc::make_filters(h), options);
        std::cout << "AU lifecycle sequences: " << failures << " failed\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
