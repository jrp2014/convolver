// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-7 for VST3: the processor driven through its SDK interfaces by the
// lifecycle sequences of tests/plugins/lifecycle.hpp, against the reference
// model of the shared adapter contract.
#include "../plugins/audio_guard.hpp"
#include "../plugins/lifecycle.hpp"
#include "../plugins/load_timing.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
namespace lc = convolver::plugin_test::lifecycle;
namespace guard = convolver::plugin_test::audio_guard;
namespace timing = convolver::plugin_test::load_timing;

namespace {
void check(bool condition, const char* what) {
    if (!condition)
        throw std::runtime_error(std::string("VST3 ") + what + " failed");
}

class Vst3 final : public lc::Driver {
  public:
    lc::Format format() const override {
        using lc::Op;
        lc::Format f;
        f.name = "VST3";
        // Contract, loading and failure: VST3 counts its configuration as
        // known once activated with it and audio has played, while the rate
        // and buses stay the same.
        f.known = lc::Known::played;
        // Contract, bypass and settings: VST3 saves its bypass parameter and
        // its settings with the state.
        f.state_bypass = true;
        f.state_settings = true;
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
                        Op::setup,
                        Op::state_a_settings,
                        Op::gain,
                        Op::latency};
        return f;
    }
    void create(const lc::Host& host, bool configured) override {
        destroy();
        processor_ = std::make_unique<convolver::vst3::Processor>();
        check(processor_->initialize(&context_) == kResultOk, "initialize");
        if (configured) {
            // As PLUG-2's driver: a host commits to its configuration at activation.
            configure(host);
            check(start(), "first activation");
            stop();
        }
    }
    void configure(const lc::Host& host) override {
        auto in = arrangement(host.inputs), out = arrangement(host.outputs);
        check(processor_->setBusArrangements(&in, 1, &out, 1) == kResultOk, "bus arrangement");
        ProcessSetup setup{kRealtime, kSample32, static_cast<int32>(max_block()),
                           static_cast<SampleRate>(host.sample_rate)};
        check(processor_->setupProcessing(setup) == kResultOk, "setupProcessing");
        processor_->activateBus(kAudio, kInput, 0, true);
        processor_->activateBus(kAudio, kOutput, 0, true);
        inputs_ = host.inputs;
        outputs_ = host.outputs;
    }
    void destroy() override {
        if (!processor_)
            return;
        if (active_)
            stop();
        processor_->terminate();
        processor_.reset();
    }
    bool restore(std::span<const std::byte> bytes) override {
        MemoryStream stream;
        std::array<unsigned char, 4> length{};
        for (std::size_t i = 0; i < 4; ++i)
            length[i] = static_cast<unsigned char>(bytes.size() >> (8 * i));
        int32 written = 0;
        stream.write(length.data(), 4, &written);
        stream.write(const_cast<std::byte*>(bytes.data()), static_cast<int32>(bytes.size()),
                     &written);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        if (starve_) {
            guard::Starve starve(starve_->first, starve_->second);
            return processor_->setState(&stream) == kResultOk;
        }
        return processor_->setState(&stream) == kResultOk;
    }
    std::optional<bool> restore_starved(std::span<const std::byte> bytes, std::size_t allowed,
                                        bool once) override {
        starve_ = std::pair(allowed, once);
        const bool accepted = restore(bytes);
        starve_.reset();
        return accepted;
    }
    bool start() override {
        if (processor_->setActive(true) != kResultOk)
            return false;
        active_ = true;
        guard::Scope audio; // setProcessing may come from the processing thread.
        processor_->setProcessing(true);
        return true;
    }
    void stop() override {
        {
            guard::Scope audio;
            processor_->setProcessing(false);
        }
        processor_->setActive(false);
        active_ = false;
    }
    bool process(std::span<const float* const> in, std::span<float* const> out,
                 std::size_t frames) override {
        return call(in, out, frames, nullptr);
    }
    void set_bypass(bool on) override {
        // A host sends the parameter with the next process call; a call with
        // no audio delivers it at once, as hosts flush parameters.
        ParameterChanges changes;
        int32 index = 0;
        changes.addParameterData(convolver::vst3::bypass_id, index)->addPoint(0, on ? 1 : 0, index);
        check(call({}, {}, 0, &changes), "bypass parameter");
    }
    void set_gain(double db) override {
        ParameterChanges changes;
        int32 index = 0;
        changes.addParameterData(convolver::vst3::gain_id, index)
            ->addPoint(0, convolver::vst3::normalized_gain(db), index);
        check(call({}, {}, 0, &changes), "Gain parameter");
    }
    // The controller's latency_message, as a new Latency choice sends it.
    void set_latency(std::size_t frames) override {
        IPtr<HostMessage> message = owned(new HostMessage);
        message->setMessageID(convolver::vst3::latency_message);
        auto* attributes = message->getAttributes();
        check(attributes && attributes->setInt("frames", static_cast<int64>(frames)) == kResultOk,
              "latency message");
        attributes->setInt("request", ++requests_);
        check(processor_->notify(message) == kResultOk, "latency message");
    }
    std::optional<std::size_t> latency() override {
        return processor_->getLatencySamples();
    }
    void reset() override {
        guard::Scope audio;
        processor_->setProcessing(false);
        processor_->setProcessing(true);
    }
    bool reopen(const lc::Host& host, bool active, bool) override {
        MemoryStream saved;
        check(processor_->getState(&saved) == kResultOk, "getState");
        saved.seek(0, IBStream::kIBSeekSet, nullptr);
        // Hosts restore a project's state before activating, often before
        // negotiating; bypass returns with the state.
        create(host, false);
        const bool accepted = processor_->setState(&saved) == kResultOk;
        configure(host);
        if (active)
            check(start(), "activation after reopening");
        return accepted;
    }
    convolver::HostState saved() override {
        MemoryStream stream;
        check(processor_->getState(&stream) == kResultOk, "getState");
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        return convolver::vst3::read_state(&stream);
    }
    std::optional<convolver::HostFailure> failure() override {
        return processor_->failure();
    }
    std::size_t max_block() const override {
        return 512;
    }
    // The window's Choose Filter, as the controller sends it (load_message),
    // on the calling thread. True if the processor accepted the file.
    bool load_file(const std::string& path) {
        IPtr<HostMessage> message = owned(new HostMessage);
        message->setMessageID(convolver::vst3::load_message);
        auto* attributes = message->getAttributes();
        check(attributes && attributes->setBinary("path", path.data(),
                                                  static_cast<uint32>(path.size())) == kResultOk,
              "load message");
        attributes->setInt("request", ++requests_);
        return processor_->notify(message) == kResultOk;
    }

  private:
    bool call(std::span<const float* const> in, std::span<float* const> out, std::size_t frames,
              ParameterChanges* changes) {
        AudioBusBuffers input{}, output{};
        input.numChannels = static_cast<int32>(inputs_);
        output.numChannels = static_cast<int32>(outputs_);
        // The SDK's buffer type is not const-correct; the adapter only reads inputs.
        std::array<float*, 8> ins{};
        for (std::size_t c = 0; c < in.size(); ++c)
            ins[c] = const_cast<float*>(in[c]);
        std::array<float*, 8> outs{};
        for (std::size_t c = 0; c < out.size(); ++c)
            outs[c] = out[c];
        input.channelBuffers32 = ins.data();
        output.channelBuffers32 = outs.data();
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = static_cast<int32>(frames);
        data.numInputs = data.numOutputs = 1;
        data.inputs = &input;
        data.outputs = &output;
        data.inputParameterChanges = changes;
        guard::Scope audio;
        return processor_->process(data) == kResultOk;
    }
    static SpeakerArrangement arrangement(std::size_t channels) {
        return (SpeakerArrangement(1) << channels) - 1;
    }
    HostApplication context_;
    std::unique_ptr<convolver::vst3::Processor> processor_;
    std::size_t inputs_ = 2, outputs_ = 2;
    bool active_ = false;
    std::optional<std::pair<std::size_t, bool>> starve_; // For restore_starved.
    int64 requests_ = 0; // load_file's and set_latency's message numbers.
};

// VST3-20 (jrp's review of 0.18.14). Contract, loading and failure: a valid
// saved project or preset is called unreadable only when reading it is what
// ran out of memory. PLUG-48's digest encoded a second copy of the filter
// after the read, and memory running out there refused the state as "the
// saved project or preset could not be read: std::bad_alloc". Memory runs
// out once at each allocation from the end of the read to the end of a
// restore that succeeds; a refusal for that names another cause.
void restore_short_of_memory() {
    convolver::HostState state;
    convolver::Configuration x{48000, 1, 1, 0, {}, {}, {}};
    x.paths.push_back({std::vector<float>(4096, 0.f), {{0, 1}}, {{0, 1}}});
    x.paths[0].impulse[0] = 1;
    state.configuration = x;
    MemoryStream stream;
    check(convolver::vst3::write_state(&stream, state), "short of memory: encode");
    HostApplication context;
    // Diagnostic: an attempt that waits 20 s says so and exits.
    std::atomic<int> progress{0};
    std::atomic<bool> finished{false};
    std::thread watchdog([&] {
        int seen = -1;
        auto since = std::chrono::steady_clock::now();
        while (!finished.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (const int now = progress.load(); now != seen) {
                seen = now;
                since = std::chrono::steady_clock::now();
            } else if (std::chrono::steady_clock::now() - since > std::chrono::seconds(20)) {
                std::cerr << "VST3 short of memory: an attempt has waited 20 s" << std::endl;
                std::_Exit(4);
            }
        }
    });
    struct Join {
        std::atomic<bool>& finished;
        std::thread& watchdog;
        ~Join() {
            finished = true;
            watchdog.join();
        }
    } join{finished, watchdog};
    // A fresh, inactive processor for each attempt, so each starts alike;
    // memory runs out after `allowed` allocations, once or from then on.
    const auto restore = [&](std::size_t allowed, bool once,
                             std::optional<convolver::HostFailure>* failure = nullptr) {
        const auto step = [&](const char* what) {
            ++progress;
            std::cerr << "  " << allowed << (once ? " once: " : ": ") << what << std::endl;
        };
        step("initialize");
        convolver::vst3::Processor p;
        SpeakerArrangement mono = SpeakerArr::kMono;
        check(p.initialize(&context) == kResultOk &&
                  p.setBusArrangements(&mono, 1, &mono, 1) == kResultOk,
              "short of memory: initialize");
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        bool accepted = false;
        step("setState");
        {
            guard::Starve starve(allowed, once);
            accepted = p.setState(&stream) == kResultOk;
        }
        step(accepted ? "accepted; failure" : "refused; failure");
        if (failure)
            *failure = p.failure();
        step("terminate");
        p.terminate();
        return accepted;
    };
    // The allocations reading the state makes, and those a restore makes.
    // An inactive processor allocates nothing in setState before it reads
    // the state, so the first `read` of a restore's allocations are the
    // read's; one that allocated earlier would need `read` counted again.
    const auto needed = [](auto&& attempt) {
        for (std::size_t allowed = 0; allowed < 2000; ++allowed)
            if (attempt(allowed))
                return allowed;
        throw std::runtime_error("VST3 short of memory: never succeeded");
    };
    const auto read = needed([&](std::size_t allowed) {
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        guard::Starve starve(allowed);
        try {
            convolver::vst3::read_state(&stream);
            return true;
        } catch (const std::bad_alloc&) {
            return false;
        }
    });
    // Each step on standard error as it starts, so a timeout shows where.
    std::cerr << "VST3 short of memory: reading the state takes " << read
              << " allocations; finding a whole restore's" << std::endl;
    const auto whole = needed([&](std::size_t allowed) { return restore(allowed, false); });
    check(whole > read, "short of memory: a restore allocating after the read");
    std::cerr << "VST3 short of memory: a restore takes " << whole << std::endl;
    for (std::size_t allowed = read; allowed < whole; ++allowed) {
        std::optional<convolver::HostFailure> failure;
        if (restore(allowed, true, &failure) || !failure)
            continue;
        if (failure->kind == convolver::HostFailureKind::state_unreadable ||
            failure->kind == convolver::HostFailureKind::state_newer)
            throw std::runtime_error(
                "VST3 short of memory: a valid state was called unreadable when memory ran out "
                "at allocation " +
                std::to_string(allowed) + ", after the " + std::to_string(read) +
                " reading it makes: " + failure->message.problem);
    }
}

// The loading-responsiveness timings (tests/plugins/load_timing.hpp): the
// host's setState and the window's file, each on the host's thread.
int load_timing(int argc, char** argv) {
    timing::Options options;
    options.work_directory = argc > 2 ? argv[2] : "vst3-load-timing";
    timing::read_flags(options, argc, argv, 3);
    Vst3 driver;
    std::vector<timing::Entry> entries{{"setState", [&](const timing::Filter& f, timing::Audio&) {
                                            return timing::Outcome{driver.restore(f.state),
                                                                   std::nullopt};
                                        }}};
    entries.push_back({"window file", [&](const timing::Filter& f, timing::Audio&) {
                           return timing::Outcome{driver.load_file(f.file.string()), std::nullopt};
                       }});
    return timing::exit_code(timing::run(driver, entries, timing::workloads(options), options));
}
} // namespace

int main(int argc, char** argv) {
    // An exception leaving a noexcept function says so and exits at once,
    // rather than waiting on Windows' error report until CTest's timeout.
    std::set_terminate([] {
        std::cerr << "VST3 lifecycle: std::terminate";
        if (const auto e = std::current_exception()) {
            try {
                std::rethrow_exception(e);
            } catch (const std::exception& x) {
                std::cerr << ": " << x.what();
            } catch (...) {
            }
        }
        std::cerr << std::endl;
        std::_Exit(3);
    });
    try {
        if (argc > 1 && std::strcmp(argv[1], "--load-timing") == 0)
            return load_timing(argc, argv);
        restore_short_of_memory();
        lc::Options options;
        if (argc > 1)
            options.length = std::stoul(argv[1]);
        // VST3-specific: state restored before the host is configured, at
        // another rate, then the rate corrected (contract, loading and failure).
        for (const char* s : {
                 "state-other-rate setup start listen stop rate start listen",
                 "state-A setup rate start listen stop rate start listen reopen listen",
                 // Out of memory at each allocation of setState: a first
                 // load, a replacement, inactive and before setupProcessing.
                 "start state-B-starved listen",
                 "start state-A play state-B-starved listen",
                 "state-A state-B-starved start listen",
                 "state-B-starved setup start listen",
                 // ROBUST-3: memory runs out just after a failed first load
                 // is recorded, before audio and while it plays.
                 "start state-malformed-starved listen",
                 "start play state-malformed-starved listen",
                 // Audio played at one rate or layout does not settle the
                 // next: restored state after the host changes them, before
                 // it plays again, is kept (jrp's REAPER retest; c3dbf0d).
                 "start play stop rate start state-A listen stop rate start listen",
                 "start play stop layout start state-A listen stop layout start listen",
                 // PLUG-39 step 0: state's settings before the host is
                 // configured, then changed and restored again while the
                 // same filter plays, and kept by a reopen.
                 "state-A-settings setup start listen latency gain listen reopen listen",
                 "start state-A play gain latency play state-A-settings listen state-A listen",
             })
            options.fixed.push_back(lc::parse(s));
        std::size_t failures = 0;
        Vst3 driver;
        for (const lc::Host host : {lc::Host{48000, 2, 2}, lc::Host{48000, 1, 2}})
            failures += lc::run(driver, lc::make_filters(host), options);
        std::cout << "VST3 lifecycle sequences: " << failures << " failed\n";
        return failures ? 1 : 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
