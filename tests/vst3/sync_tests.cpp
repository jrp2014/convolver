// SPDX-License-Identifier: GPL-2.0-or-later
// VST3-25: generated sequences that check the two halves of the VST3 plug-in
// end up agreeing with each other, with the host and with what the user did
// last.
//
// The processor and the edit controller are separate objects that the host
// restores separately, in either order, and that talk through messages a
// host may pass on at once or later (VST3-15). Each fix from PLUG-48 to
// VST3-24 was found by jrp's sequence matrix, not by a test written for it.
// This runner generates those sequences instead: every sequence of up to two
// steps in every delivery schedule, and seeded random longer ones, each
// judged by one oracle rather than by expectations written per case.
//
// Steps (one letter each, so a failure prints a sequence to paste back):
//   L  the host's generic view chooses the next Latency
//   M  the host's generic view chooses the next Gain mode
//   G  host automation of Gain; m  of Mix; B  of Bypass (setParamNormalized,
//      then the same value in the next process() call, as hosts do)
//   W  the window chooses preset W (.vstpreset with settings, PLUG-26)
//   w  the window chooses a file that does not exist (refused)
//   R  the host restores state R (accepted); X  state made for 44.1 kHz,
//      refused while the host plays at 48 kHz
//
// Variations: the connection (direct, a proxy that forwards at once, one
// that delivers later), and for the delaying proxy what is delivered after
// each step (nothing, the processor's queue, the controller's, both) and
// whether messages are delivered between the two calls of a restore; the
// restore order (processor first, as the SDK requires, or controller
// first); and whether the host plays a block after every step or only at
// the end.
//
// The oracle, once the host has delivered everything (messages, and its
// queued parameter changes in a process() call): the model's result of the
// latest step that succeeded (a refusal changes nothing) is what
// - the controller's parameters show, and the window's report says;
// - the host's own view of the parameters holds (its sets, the edits the
//   plug-in reported, and what it reads back when asked to restart);
// - the processor plays: its settings, the latency it reports, and its
//   impulse response, compared with a fresh processor restored from the
//   model's state;
// - the host last read as the latency, when asked to restart;
// - the processor saves, and a fresh processor restored from that save plays.
// And every host callback came on the thread that made the host call (the
// UI thread here). Gain, Mix and Bypass reach the processor in process(),
// so they are judged only after the host has delivered them (jrp's review
// of the shared-processor proposal, 24 November 2026); the save between an
// edit and its delivery is its own case with its own expectation.
//
// Named cases cover what sequences of one pair cannot: two instances at
// once and closing one while the other plays, a processor with no
// controller, and a restore made on another thread (an offline render).
//
// Usage: convolver_vst3_sync [--random N] [--seed S] [--length K] [--tsv FILE]
// (defaults 300 random sequences of 3 to 5 steps, seed 1). Each run prints
// how many sequences ran and which checks failed.
#include "../plugins/test_log.hpp"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "presets.hpp"
#include "processor.hpp"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
using namespace Steinberg;
using namespace Steinberg::Vst;
using namespace convolver::vst3;
using convolver::GainMode;
using convolver::HostSettings;
using convolver::HostState;

namespace {
constexpr int rate = 48000;
constexpr int block = 512;

// What the user and the host have asked for, as the model keeps it: the
// filter (a single tap, which identifies it) and the settings.
struct Model {
    float tap = 1;
    HostSettings settings{};
    bool bypass = false;
};

// The steps' values. Each L, M, G, m and B takes the next value in its list,
// so the latest choice can be told from earlier ones.
constexpr std::size_t latencies[] = {256, 1024, 64};
constexpr GainMode modes[] = {GainMode::no_clipping, GainMode::estimate, GainMode::set};
constexpr double gains[] = {-6, -12, 3};
constexpr double mixes[] = {.5, .25, .75};

HostState state_with(int at_rate, float tap, const HostSettings& settings, bool bypass) {
    HostState state;
    state.configuration = convolver::Configuration{at_rate, 1, 1, 0, {}, {}, {}};
    state.configuration->paths.push_back({{tap}, {{0, 1}}, {{0, 1}}});
    state.settings = settings;
    state.bypass = bypass;
    return state;
}
HostSettings settings_of(double gain, double mix, std::size_t latency, GainMode mode) {
    HostSettings s;
    s.gain_db = gain;
    s.mix = mix;
    s.latency = latency;
    s.gain_mode = mode;
    return s;
}
// Preset W (chosen in the window), state R (restored) and state X (refused).
const Model preset_w{.125f, settings_of(-3, .75, 2048, GainMode::estimate), false};
const Model state_r{.5f, settings_of(-9, 1, 512, GainMode::set), false};
const Model initial{1, HostSettings{}, false};

std::unique_ptr<MemoryStream> stream_of(const HostState& state) {
    auto stream = std::make_unique<MemoryStream>();
    if (!write_state(stream.get(), state))
        throw std::runtime_error("encode a state");
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    return stream;
}

// A proxy between the halves: forwards each message at once, or keeps it
// until the test delivers it (VST3-15).
class Proxy final : public IConnectionPoint {
  public:
    Proxy(IConnectionPoint& to, bool delay) : to_(to), delay_(delay) {}
    tresult PLUGIN_API connect(IConnectionPoint*) override {
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(IConnectionPoint*) override {
        return kResultOk;
    }
    tresult PLUGIN_API notify(IMessage* message) override {
        if (!delay_)
            return to_.notify(message);
        kept_.emplace_back(message);
        return kResultOk;
    }
    std::size_t deliver() {
        std::size_t n = 0;
        while (!kept_.empty()) {
            const auto message = kept_.front();
            kept_.pop_front();
            to_.notify(message);
            ++n;
        }
        return n;
    }
    void drop() {
        kept_.clear();
    }
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        QUERY_INTERFACE(_iid, obj, FUnknown::iid, IConnectionPoint)
        QUERY_INTERFACE(_iid, obj, IConnectionPoint::iid, IConnectionPoint)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return 1;
    }
    uint32 PLUGIN_API release() override {
        return 1;
    }

  private:
    IConnectionPoint& to_;
    const bool delay_;
    std::deque<IPtr<IMessage>> kept_;
};

// The host: its view of the parameters, the changes it will pass to the
// next process() call, the latencies it read when asked to restart, and
// whether every callback came on the UI thread.
class Host final : public IComponentHandler, public IComponentHandler2 {
  public:
    tresult PLUGIN_API beginEdit(ParamID) override {
        note_thread();
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override {
        note_thread();
        values[id] = value;
        queue(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override {
        note_thread();
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        note_thread();
        if ((flags & kLatencyChanged) && processor)
            latency_read = processor->getLatencySamples();
        if ((flags & kParamValuesChanged) && controller)
            rescan();
        return kResultOk;
    }
    tresult PLUGIN_API setDirty(TBool) override {
        note_thread();
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString) override {
        return kNotImplemented;
    }
    tresult PLUGIN_API startGroupEdit() override {
        return kNotImplemented;
    }
    tresult PLUGIN_API finishGroupEdit() override {
        return kNotImplemented;
    }
    tresult PLUGIN_API queryInterface(const TUID _iid, void** obj) override {
        QUERY_INTERFACE(_iid, obj, FUnknown::iid, IComponentHandler)
        QUERY_INTERFACE(_iid, obj, IComponentHandler::iid, IComponentHandler)
        QUERY_INTERFACE(_iid, obj, IComponentHandler2::iid, IComponentHandler2)
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override {
        return 1;
    }
    uint32 PLUGIN_API release() override {
        return 1;
    }
    // What the host reads from the controller: all settings parameters.
    void rescan() {
        for (const auto id : {gain_id, mix_id, latency_id, gain_mode_id, bypass_id})
            values[id] = controller->getParamNormalized(id);
    }
    // An automatable parameter the host sets: in the controller now, and in
    // the processor at the next process() call.
    void automate(ParamID id, ParamValue value) {
        controller->setParamNormalized(id, value);
        values[id] = value;
        queue(id, value);
    }
    void queue(ParamID id, ParamValue value) {
        if (id == gain_id || id == mix_id || id == bypass_id)
            pending[id] = value;
    }
    void note_thread() {
        if (std::this_thread::get_id() != ui_thread)
            off_thread = true;
    }
    Processor* processor = nullptr;
    Controller* controller = nullptr;
    std::map<ParamID, ParamValue> values, pending;
    std::optional<uint32> latency_read;
    std::thread::id ui_thread = std::this_thread::get_id();
    bool off_thread = false;
};

// One block through `p`: silence, or `input`, with the parameter changes
// the host has queued. Returns the output.
std::vector<float> process(Processor& p, std::map<ParamID, ParamValue>* changes,
                           const std::vector<float>& input = {}) {
    std::vector<float> in(block), out(block);
    std::copy_n(input.begin(), std::min<std::size_t>(input.size(), block), in.begin());
    float* ins[] = {in.data()};
    float* outs[] = {out.data()};
    AudioBusBuffers input_bus{}, output_bus{};
    input_bus.numChannels = output_bus.numChannels = 1;
    input_bus.channelBuffers32 = ins;
    output_bus.channelBuffers32 = outs;
    ParameterChanges parameters(8);
    if (changes) {
        for (const auto& [id, value] : *changes) {
            int32 index = 0;
            if (auto* queue = parameters.addParameterData(id, index)) {
                int32 point = 0;
                queue->addPoint(0, value, point);
            }
        }
        changes->clear();
    }
    ProcessData d{};
    d.processMode = kRealtime;
    d.symbolicSampleSize = kSample32;
    d.numInputs = d.numOutputs = 1;
    d.inputs = &input_bus;
    d.outputs = &output_bus;
    d.numSamples = block;
    d.inputParameterChanges = &parameters;
    if (p.process(d) != kResultOk)
        throw std::runtime_error("process() failed");
    return out;
}
// The impulse response of what `p` plays now: silence until any ramp,
// crossfade or new Latency has settled, then a unit impulse, long enough to
// cover the longest Latency.
std::vector<float> impulse_response(Processor& p) {
    for (int i = 0; i < 4; ++i)
        process(p, nullptr);
    std::vector<float> response, impulse(block);
    impulse[0] = 1;
    for (int i = 0; i < 10; ++i) {
        const auto out = process(p, nullptr, i == 0 ? impulse : std::vector<float>{});
        response.insert(response.end(), out.begin(), out.end());
    }
    return response;
}
void prepare(Processor& p, ProcessModes mode = kRealtime) {
    SpeakerArrangement mono = SpeakerArr::kMono;
    if (p.setBusArrangements(&mono, 1, &mono, 1) != kResultOk)
        throw std::runtime_error("mono buses");
    p.activateBus(kAudio, kInput, 0, true);
    p.activateBus(kAudio, kOutput, 0, true);
    ProcessSetup setup{mode, kSample32, block, rate};
    if (p.setupProcessing(setup) != kResultOk)
        throw std::runtime_error("setupProcessing");
}
// What a fresh processor with no controller plays after restoring `stream`.
std::vector<float> fresh_response(IBStream& stream) {
    HostApplication context;
    Processor p;
    if (p.initialize(&context) != kResultOk)
        throw std::runtime_error("initialize a fresh processor");
    prepare(p);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    if (p.setState(&stream) != kResultOk)
        throw std::runtime_error("a fresh processor refused the state");
    if (p.setActive(true) != kResultOk)
        throw std::runtime_error("activate a fresh processor");
    auto response = impulse_response(p);
    p.setActive(false);
    p.terminate();
    return response;
}
std::vector<float> model_response(const Model& m) {
    auto stream = stream_of(state_with(rate, m.tap, m.settings, m.bypass));
    return fresh_response(*stream);
}
bool same_response(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size())
        return false;
    float peak = 0;
    for (const auto v : b)
        peak = std::max(peak, std::abs(v));
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::abs(a[i] - b[i]) > 1e-5f * std::max(peak, 1e-3f))
            return false;
    return true;
}
bool same_settings(const HostSettings& a, const HostSettings& b) {
    return std::abs(a.gain_db - b.gain_db) < 1e-3 && std::abs(a.mix - b.mix) < 1e-6 &&
           a.latency == b.latency && a.gain_mode == b.gain_mode;
}
std::string describe(const HostSettings& s) {
    return "Gain " + std::to_string(s.gain_db) + " dB, Mix " + std::to_string(s.mix) +
           ", Latency " + std::to_string(s.latency) + ", Gain mode " +
           std::to_string(static_cast<int>(s.gain_mode));
}
// The settings parameters as the controller or the host holds them.
HostSettings settings_from(const std::function<ParamValue(ParamID)>& value) {
    return settings_of(gain_db(value(gain_id)), value(mix_id), latency_frames(value(latency_id)),
                       gain_mode(value(gain_mode_id)));
}

enum class Connection { direct, forwarding, delaying };
// After a step, with a delaying proxy: what is delivered.
enum Delivery : int { none = 0, processor_queue = 1, controller_queue = 2, both_queues = 3 };
struct Variant {
    Connection connection = Connection::direct;
    bool controller_first = false;
    bool playing = true;         // A process() block after every step.
    bool split_delivery = false; // Deliver between the two calls of a restore.
    std::vector<Delivery> after; // After each step but the last (delaying only).
};
std::string describe(const Variant& v, std::string_view steps) {
    std::string text = v.connection == Connection::direct       ? "direct"
                       : v.connection == Connection::forwarding ? "forwarding proxy"
                                                                : "delaying proxy";
    text += v.controller_first ? ", controller first" : ", processor first";
    text += v.playing ? ", playing" : ", stopped";
    if (v.split_delivery)
        text += ", delivered mid-restore";
    text += ": ";
    static const char* const names[] = {"none", "processor's", "controller's", "both"};
    for (std::size_t i = 0; i < steps.size(); ++i) {
        text += steps[i];
        if (i + 1 < steps.size())
            text += v.connection == Connection::delaying && i < v.after.size()
                        ? std::string(" [") + names[v.after[i]] + "] "
                        : std::string(" ");
    }
    return text;
}

// One processor and controller pair as a host holds them.
struct Pair {
    explicit Pair(Connection connection, ProcessModes mode = kRealtime)
        : delaying(connection == Connection::delaying), to_controller(c, delaying),
          to_processor(p, delaying) {
        host.processor = &p;
        host.controller = &c;
        if (p.initialize(&context) != kResultOk || c.initialize(&context) != kResultOk ||
            c.setComponentHandler(&host) != kResultOk)
            throw std::runtime_error("initialize");
        const bool direct = connection == Connection::direct;
        if (p.connect(direct ? static_cast<IConnectionPoint*>(&c) : &to_controller) != kResultOk ||
            c.connect(direct ? static_cast<IConnectionPoint*>(&p) : &to_processor) != kResultOk)
            throw std::runtime_error("connect");
        this->direct = direct;
        prepare(p, mode);
    }
    ~Pair() {
        close();
    }
    Pair(const Pair&) = delete;
    Pair& operator=(const Pair&) = delete;
    void close() {
        if (closed)
            return;
        closed = true;
        p.setActive(false);
        p.disconnect(direct ? static_cast<IConnectionPoint*>(&c) : &to_controller);
        c.disconnect(direct ? static_cast<IConnectionPoint*>(&p) : &to_processor);
        to_controller.drop();
        to_processor.drop();
        c.terminate();
        p.terminate();
    }
    std::size_t deliver(Delivery what = both_queues) {
        std::size_t n = 0;
        if (what & processor_queue)
            n += to_processor.deliver();
        if (what & controller_queue)
            n += to_controller.deliver();
        return n;
    }
    // A restore of `state` in the host's order; true if the processor took it.
    bool restore(const HostState& state, bool controller_first, bool split_delivery) {
        auto stream = stream_of(state);
        tresult result = kResultFalse;
        // A host passes automation queued before a restore before it, never
        // after it (the race JUCE's host fixed in a37f5f23).
        if (!host.pending.empty())
            play();
        if (!controller_first)
            result = p.setState(stream.get());
        if (split_delivery)
            while (deliver() > 0) {
            }
        stream->seek(0, IBStream::kIBSeekSet, nullptr);
        c.setComponentState(stream.get());
        host.rescan(); // The SDK: the host reads the parameters again.
        if (controller_first) {
            if (split_delivery)
                while (deliver() > 0) {
                }
            stream->seek(0, IBStream::kIBSeekSet, nullptr);
            result = p.setState(stream.get());
        }
        return result == kResultOk;
    }
    void play() {
        process(p, &host.pending);
    }
    // Everything the host and the proxies still hold, delivered.
    void settle() {
        for (int i = 0; i < 20; ++i) {
            const auto delivered = deliver();
            const bool queued = !host.pending.empty();
            play();
            if (!delivered && !queued && deliver() == 0)
                break;
        }
        c.report();
        while (deliver() > 0) {
        }
    }
    void start(const Model& first) {
        restore(state_with(rate, first.tap, first.settings, first.bypass), false, false);
        while (deliver() > 0) {
        }
        if (p.setActive(true) != kResultOk)
            throw std::runtime_error("activate");
        host.latency_read = p.getLatencySamples(); // What a host reads at activation.
        play(); // The host has played audio with its configuration (PLUG-39 step 2).
        while (deliver() > 0) {
        }
        host.rescan();
    }

    HostApplication context;
    Host host;
    Processor p;
    Controller c;
    bool delaying = false, direct = true, closed = false;
    Proxy to_controller, to_processor;
};

// What a pair that only ever restored the model's state shows and plays:
// the window's settings line and the impulse response. Cached: sequences
// end in few distinct models.
struct Reference {
    std::string report_settings;
    std::vector<float> response;
};
const Reference& reference(const Model& m);
// The checks of the oracle; each failure names one.
struct Failures {
    std::map<std::string, std::size_t> counts;
    std::vector<std::string> examples;
    void add(const std::string& check, const std::string& detail) {
        if (counts[check]++ < 3)
            examples.push_back(check + ": " + detail);
    }
};
const Reference& reference(const Model& m) {
    static std::map<std::string, Reference> cache;
    const auto key =
        std::to_string(m.tap) + " " + describe(m.settings) + (m.bypass ? " bypassed" : "");
    if (const auto found = cache.find(key); found != cache.end())
        return found->second;
    Pair pair(Connection::direct);
    pair.start(m);
    pair.settle();
    const auto report = pair.c.report();
    Reference r{report ? report->settings : std::string(), impulse_response(pair.p)};
    if (!same_response(r.response, model_response(m)))
        throw std::runtime_error("the reference pair plays something other than its state");
    return cache.emplace(key, std::move(r)).first->second;
}
std::vector<std::string> judge(Pair& pair, const Model& m) {
    std::vector<std::string> failed;
    const auto fail = [&](const std::string& check, const std::string& detail) {
        failed.push_back(check + " (" + detail + ")");
    };
    const auto& want = m.settings;
    const auto shown = settings_from([&](ParamID id) { return pair.c.getParamNormalized(id); });
    if (!same_settings(shown, want) || (pair.c.getParamNormalized(bypass_id) >= .5) != m.bypass)
        fail("shown", "controller shows " + describe(shown));
    const auto host_view = settings_from([&](ParamID id) { return pair.host.values[id]; });
    if (!same_settings(host_view, want) || (pair.host.values[bypass_id] >= .5) != m.bypass)
        fail("host", "host holds " + describe(host_view));
    const auto& expected = reference(m);
    const auto report = pair.c.report();
    if (!report || report->settings != expected.report_settings)
        fail("report", report ? "window reports \"" + report->settings + "\", not \"" +
                                    expected.report_settings + "\""
                              : "no report");
    const auto playing = pair.p.settings();
    if (!same_settings(playing, want) || pair.p.getLatencySamples() != want.latency)
        fail("playing", "processor has " + describe(playing) + ", reports latency " +
                            std::to_string(pair.p.getLatencySamples()));
    if (pair.host.latency_read != pair.p.getLatencySamples())
        fail("host latency",
             "host last read " + (pair.host.latency_read ? std::to_string(*pair.host.latency_read)
                                                         : std::string("nothing")));
    if (pair.host.off_thread)
        fail("thread", "a host callback came off the UI thread");
    MemoryStream saved;
    if (pair.p.getState(&saved) != kResultOk) {
        fail("saved", "getState failed");
        return failed;
    }
    saved.seek(0, IBStream::kIBSeekSet, nullptr);
    const auto state = read_state(&saved);
    const auto& config = state.configuration;
    const bool saved_filter = config && config->paths.size() == 1 &&
                              config->paths.front().impulse == std::vector<float>{m.tap};
    const auto saved_settings = state.settings.value_or(HostSettings{});
    if (!saved_filter || !same_settings(saved_settings, want) || state.bypass != m.bypass)
        fail("saved",
             std::string(saved_filter ? "" : "another filter, ") + describe(saved_settings));
    if (!same_response(impulse_response(pair.p), expected.response))
        fail("audio", "the impulse response differs from tap " + std::to_string(m.tap) + " with " +
                          describe(want));
    if (!same_response(fresh_response(saved), expected.response))
        fail("reopened", "a processor restored from the save plays something else");
    return failed;
}

// Runs one sequence; returns the failed checks.
std::vector<std::string> run(std::string_view steps, const Variant& v,
                             const std::filesystem::path& preset, const std::string& missing) {
    Pair pair(v.connection);
    pair.start(initial);
    Model m = initial;
    std::size_t next_latency = 0, next_mode = 0, next_gain = 0, next_mix = 0;
    // Bypass is the host's and a refused state does not correct it (the
    // contract's PLUG-48 paragraph), so X leaves Bypass as it is.
    const auto x_state = state_with(44100, .75f, settings_of(-1, .5, 64, GainMode::set), false);
    for (std::size_t i = 0; i < steps.size(); ++i) {
        switch (steps[i]) {
        case 'L': {
            const auto frames = latencies[next_latency++ % std::size(latencies)];
            pair.c.setParamNormalized(latency_id, normalized_latency(frames));
            pair.host.values[latency_id] = normalized_latency(frames);
            m.settings.latency = frames;
            break;
        }
        case 'M': {
            const auto mode = modes[next_mode++ % std::size(modes)];
            pair.c.setParamNormalized(gain_mode_id, normalized_gain_mode(mode));
            pair.host.values[gain_mode_id] = normalized_gain_mode(mode);
            m.settings.gain_mode = mode;
            break;
        }
        case 'G': {
            const auto gain = gains[next_gain++ % std::size(gains)];
            pair.host.automate(gain_id, normalized_gain(gain));
            m.settings.gain_db = gain;
            break;
        }
        case 'm': {
            const auto mix = mixes[next_mix++ % std::size(mixes)];
            pair.host.automate(mix_id, mix);
            m.settings.mix = mix;
            break;
        }
        case 'B':
            m.bypass = !m.bypass;
            pair.host.automate(bypass_id, m.bypass ? 1 : 0);
            break;
        case 'W':
            if (!pair.c.load_file(preset.string()))
                return {"step (the window's preset was refused)"};
            m.tap = preset_w.tap;
            m.settings = preset_w.settings;
            break;
        case 'w':
            pair.c.load_file(missing);
            break;
        case 'R':
            if (!pair.restore(state_with(rate, state_r.tap, state_r.settings, state_r.bypass),
                              v.controller_first, v.split_delivery))
                return {"step (state R was refused)"};
            m = state_r;
            break;
        case 'X':
            if (pair.restore(x_state, v.controller_first, v.split_delivery))
                return {"step (the 44.1 kHz state was accepted)"};
            break;
        default:
            throw std::runtime_error("unknown step");
        }
        if (v.playing)
            pair.play();
        if (pair.delaying && i < v.after.size())
            pair.deliver(v.after[i]);
    }
    pair.settle();
    return judge(pair, m);
}

constexpr std::string_view alphabet = "LMGmBWwRX";

// Every variant of a sequence of `length` steps.
std::vector<Variant> every_variant(std::size_t length) {
    std::vector<Variant> all;
    for (const auto connection : {Connection::direct, Connection::forwarding, Connection::delaying})
        for (const bool controller_first : {false, true})
            for (const bool playing : {true, false}) {
                if (connection != Connection::delaying) {
                    all.push_back({connection, controller_first, playing, false, {}});
                    continue;
                }
                std::size_t schedules = 1;
                for (std::size_t i = 1; i < length; ++i)
                    schedules *= 4;
                for (const bool split : {false, true})
                    for (std::size_t s = 0; s < schedules; ++s) {
                        Variant v{connection, controller_first, playing, split, {}};
                        for (std::size_t i = 1, t = s; i < length; ++i, t /= 4)
                            v.after.push_back(static_cast<Delivery>(t % 4));
                        all.push_back(v);
                    }
            }
    return all;
}

// --- Named cases -----------------------------------------------------------

void require(bool v, const std::string& what) {
    if (!v)
        throw std::runtime_error(what);
}
// Two instances at once, each with its own filter and settings; closing one
// while the other plays leaves the other working.
void two_instances(const std::filesystem::path& preset) {
    auto a = std::make_unique<Pair>(Connection::direct);
    Pair b(Connection::direct);
    a->start(initial);
    b.start(initial);
    require(a->c.load_file(preset.string()), "two instances: A's preset");
    b.restore(state_with(rate, state_r.tap, state_r.settings, state_r.bypass), false, false);
    a->settle();
    b.settle();
    auto failed = judge(*a, preset_w);
    auto failed_b = judge(b, state_r);
    failed.insert(failed.end(), failed_b.begin(), failed_b.end());
    require(failed.empty(), "two instances: " + (failed.empty() ? "" : failed.front()));
    a->close();
    a.reset();
    b.play();
    b.c.setParamNormalized(latency_id, normalized_latency(1024));
    b.host.values[latency_id] = normalized_latency(1024);
    b.settle();
    auto still = state_r;
    still.settings.latency = 1024;
    failed = judge(b, still);
    require(failed.empty(), "the instance left open: " + (failed.empty() ? "" : failed.front()));
}
// A processor with no controller (a render that loads the processor only)
// restores, plays and saves.
void headless_processor() {
    HostApplication context;
    Processor p;
    require(p.initialize(&context) == kResultOk, "headless: initialize");
    prepare(p);
    auto stream = stream_of(state_with(rate, state_r.tap, state_r.settings, state_r.bypass));
    require(p.setState(stream.get()) == kResultOk, "headless: restore");
    require(p.setActive(true) == kResultOk, "headless: activate");
    require(same_response(impulse_response(p), model_response(state_r)), "headless: what plays");
    MemoryStream saved;
    require(p.getState(&saved) == kResultOk, "headless: save");
    require(same_response(fresh_response(saved), model_response(state_r)),
            "headless: what the save plays");
    p.setActive(false);
    p.terminate();
}
// An offline render restores the processor on its processing thread
// (Steinberg's processing FAQ). The host's callbacks still come on the UI
// thread, and the halves agree once the host has delivered everything.
void offline_restore() {
    for (const auto connection : {Connection::direct, Connection::delaying}) {
        Pair pair(connection, kOffline);
        pair.start(initial);
        std::thread render([&] {
            auto stream =
                stream_of(state_with(rate, state_r.tap, state_r.settings, state_r.bypass));
            pair.p.setState(stream.get());
        });
        render.join();
        auto stream = stream_of(state_with(rate, state_r.tap, state_r.settings, state_r.bypass));
        pair.c.setComponentState(stream.get());
        pair.host.rescan();
        pair.settle();
        const auto failed = judge(pair, state_r);
        require(failed.empty(), std::string("offline restore (") +
                                    (connection == Connection::direct ? "direct" : "delaying") +
                                    "): " + (failed.empty() ? "" : failed.front()));
    }
}
// A save between a Gain edit and the block that delivers it saves the Gain
// that plays; the save after that block saves the new one.
void save_before_delivery() {
    Pair pair(Connection::direct);
    pair.start(initial);
    pair.host.automate(gain_id, normalized_gain(-6));
    MemoryStream early;
    require(pair.p.getState(&early) == kResultOk, "save before delivery: save");
    early.seek(0, IBStream::kIBSeekSet, nullptr);
    require(std::abs(read_state(&early).settings.value_or(HostSettings{}).gain_db) < 1e-3,
            "save before delivery: the save holds a Gain the processor does not play yet");
    pair.settle();
    auto m = initial;
    m.settings.gain_db = -6;
    const auto failed = judge(pair, m);
    require(failed.empty(), "save after delivery: " + (failed.empty() ? "" : failed.front()));
}

std::filesystem::path private_temp(const char* name) {
    return std::filesystem::temp_directory_path() /
           (std::string(name) + "-" + std::to_string(std::random_device{}()));
}
} // namespace

int main(int argc, char** argv) {
    convolver::test_log::use_test_log("convolver-vst3-sync.log");
    std::size_t random_count = 300, max_length = 5;
    std::uint32_t seed = 1;
    std::optional<std::filesystem::path> tsv;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view flag = argv[i];
        if (flag == "--random")
            random_count = std::stoul(argv[i + 1]);
        else if (flag == "--seed")
            seed = static_cast<std::uint32_t>(std::stoul(argv[i + 1]));
        else if (flag == "--length")
            max_length = std::max<std::size_t>(3, std::stoul(argv[i + 1]));
        else if (flag == "--tsv")
            tsv = argv[i + 1];
    }
    const auto folder = private_temp("convolver-vst3-sync");
    std::filesystem::create_directories(folder);
    const auto preset = folder / "W.vstpreset";
    {
        const auto bytes = convolver::presets::vst3_preset(
            state_with(rate, preset_w.tap, preset_w.settings, preset_w.bypass));
        std::ofstream out(preset, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
    }
    const auto missing = (folder / "missing.cfg").string();

    Failures failures;
    std::size_t runs = 0, failed_runs = 0;
    std::ofstream table;
    if (tsv) {
        table.open(*tsv);
        table << "sequence\tresult\n";
    }
    const auto one = [&](std::string_view steps, const Variant& v) {
        const auto how = describe(v, steps);
        std::vector<std::string> failed;
        try {
            failed = run(steps, v, preset, missing);
        } catch (const std::exception& e) {
            failed = {std::string("exception (") + e.what() + ")"};
        }
        ++runs;
        if (!failed.empty()) {
            ++failed_runs;
            for (const auto& f : failed)
                failures.add(f.substr(0, f.find(" (")), how + ": " + f);
        }
        if (table.is_open()) {
            std::string result = failed.empty() ? "pass" : "";
            for (const auto& f : failed)
                result += (result.empty() ? "" : "; ") + f;
            table << how << '\t' << result << '\n';
        }
    };
    // Every sequence of one and two steps, in every variant.
    for (std::size_t length = 1; length <= 2; ++length) {
        const auto variants = every_variant(length);
        std::string steps(length, ' ');
        std::size_t combinations = 1;
        for (std::size_t i = 0; i < length; ++i)
            combinations *= alphabet.size();
        for (std::size_t n = 0; n < combinations; ++n) {
            for (std::size_t i = 0, t = n; i < length; ++i, t /= alphabet.size())
                steps[i] = alphabet[t % alphabet.size()];
            for (const auto& v : variants)
                one(steps, v);
        }
    }
    // Seeded random longer sequences, each in a random variant.
    std::mt19937 random(seed);
    for (std::size_t n = 0; n < random_count; ++n) {
        const auto length = 3 + random() % (max_length - 2);
        std::string steps;
        for (std::size_t i = 0; i < length; ++i)
            steps += alphabet[random() % alphabet.size()];
        const auto variants = every_variant(1);
        auto v = variants[random() % variants.size()];
        if (v.connection == Connection::delaying)
            for (std::size_t i = 1; i < length; ++i)
                v.after.push_back(static_cast<Delivery>(random() % 4));
        one(steps, v);
    }
    // The named cases.
    const std::pair<const char*, std::function<void()>> cases[] = {
        {"two_instances", [&] { two_instances(preset); }},
        {"headless_processor", headless_processor},
        {"offline_restore", offline_restore},
        {"save_before_delivery", save_before_delivery},
    };
    std::size_t failed_cases = 0;
    for (const auto& [name, test] : cases) {
        try {
            test();
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception& e) {
            ++failed_cases;
            std::cout << "FAIL " << name << ": " << e.what() << '\n';
        }
    }
    std::filesystem::remove_all(folder);
    std::cout << runs << " sequences, " << failed_runs << " failed (seed " << seed << ")\n";
    for (const auto& [check, count] : failures.counts)
        std::cout << "  " << check << ": " << count << '\n';
    for (const auto& example : failures.examples)
        std::cout << "  - " << example << '\n';
    return failed_runs || failed_cases ? 1 : 0;
}
