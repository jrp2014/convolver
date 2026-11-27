// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "advice.hpp"
#include "bytes.hpp"
#include "chosen_file.hpp"
#include "convolver_sdk.hpp"
#include "editor.hpp"
#include "host_log.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include "recent_files.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace convolver::vst3 {
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace {
bool transfer(IBStream* stream, void* data, std::size_t size, bool writing) {
    if (!stream)
        return false;
    auto p = static_cast<char*>(data);
    while (size) {
        int32 count = 0;
        auto n = static_cast<int32>(size);
        auto status = writing ? stream->write(p, n, &count) : stream->read(p, n, &count);
        if (count <= 0 || count > n ||
            (status != kResultOk && static_cast<std::size_t>(count) != size))
            return false;
        p += count;
        size -= count;
    }
    return true;
}
} // namespace
std::uint32_t wav_speakers(SpeakerArrangement arrangement) noexcept {
    constexpr SpeakerArrangement shared = (SpeakerArrangement(1) << 18) - 1;
    return arrangement & ~shared ? 0 : static_cast<std::uint32_t>(arrangement);
}
HostState read_state(IBStream* stream) {
    std::array<std::byte, 4> header{};
    if (!transfer(stream, header.data(), 4, false))
        throw std::invalid_argument("missing state length");
    const auto size = detail::read_le32(header);
    if (size > max_host_state_bytes || size < 12)
        throw std::invalid_argument("invalid state length");
    std::vector<std::byte> bytes(size);
    if (!transfer(stream, bytes.data(), size, false))
        throw std::invalid_argument("truncated state");
    auto state = decode_host_state(bytes);
    return state;
}
HostStateHeader read_state_header(IBStream* stream) {
    std::array<std::byte, 4> length{};
    if (!transfer(stream, length.data(), 4, false))
        throw std::invalid_argument("missing state length");
    const std::size_t size = detail::read_le32(length);
    if (size > max_host_state_bytes || size < 12)
        throw std::invalid_argument("invalid state length");
    std::array<std::byte, host_state_header_bytes> start{};
    const auto taken = std::min(size, start.size());
    if (!transfer(stream, start.data(), taken, false))
        throw std::invalid_argument("truncated state");
    auto header = decode_host_state_header(std::span(start).first(taken), size);
    // The rest is read past, a block at a time, so that a state read ends
    // the stream where read_state() leaves it and a truncated one is
    // refused as there.
    std::array<std::byte, 16384> rest{};
    for (auto left = size - taken; left;) {
        const auto n = std::min(left, rest.size());
        if (!transfer(stream, rest.data(), n, false))
            throw std::invalid_argument("truncated state");
        left -= n;
    }
    return header;
}
bool write_state(IBStream* stream, const HostState& state) {
    return write_state(stream, view_of(state));
}
bool write_state(IBStream* stream, const HostStateView& state) {
    auto bytes = encode_host_state(state);
    std::array<std::byte, 4> header{};
    detail::write_le32(header, static_cast<std::uint32_t>(bytes.size()));
    return transfer(stream, header.data(), 4, true) &&
           transfer(stream, bytes.data(), bytes.size(), true);
}
double gain_db(ParamValue v) noexcept {
    const double gain = min_gain_db + std::clamp(v, 0.0, 1.0) * (max_gain_db - min_gain_db);
    // Hosts that keep normalized values as float send 0.80645162f for 0 dB,
    // which is 7e-7 dB: that is the default, so a project reset to it still
    // saves as state version 1 or 2.
    return std::abs(gain) < 1e-5 ? 0.0 : gain;
}
ParamValue normalized_gain(double gain) noexcept {
    return (std::clamp(gain, min_gain_db, max_gain_db) - min_gain_db) / (max_gain_db - min_gain_db);
}
// A list parameter of n items maps item i to i / (n - 1), as the SDK's
// StringListParameter does; the nearest item is taken.
std::size_t latency_frames(ParamValue v) noexcept {
    const auto last = static_cast<double>(host_latencies.size() - 1);
    const auto i = static_cast<std::size_t>(std::lround(std::clamp(v, 0.0, 1.0) * last));
    return host_latencies[i];
}
ParamValue normalized_latency(std::size_t frames) noexcept {
    const auto at = std::find(host_latencies.begin(), host_latencies.end(), frames);
    const auto i = at == host_latencies.end()
                       ? default_latency_index
                       : static_cast<std::size_t>(at - host_latencies.begin());
    return static_cast<ParamValue>(i) / static_cast<ParamValue>(host_latencies.size() - 1);
}
GainMode gain_mode(ParamValue v) noexcept {
    const auto last = static_cast<double>(gain_mode_count - 1);
    return static_cast<GainMode>(std::lround(std::clamp(v, 0.0, 1.0) * last));
}
ParamValue normalized_gain_mode(GainMode mode) noexcept {
    const auto i = std::min(static_cast<std::size_t>(mode), gain_mode_count - 1);
    return static_cast<ParamValue>(i) / static_cast<ParamValue>(gain_mode_count - 1);
}
namespace {
std::string next_tag() {
    static std::atomic<unsigned> instances{0};
    return "VST3 #" + std::to_string(++instances);
}
} // namespace
Processor::Processor() : tag_(next_tag()) {
    set_host_log_sdk(CONVOLVER_SDK_DESCRIPTION); // The Convolver log names the SDK (CI-6).
    setControllerClass(controller_id);
#ifdef __APPLE__
    files_ = &detail::audio_toolbox_files(); // As the Audio Unit reads them.
#else
    files_ = &detail::libsndfile_files(); // VST3-12: as the tools and LV2 read them.
#endif
}
Processor::~Processor() {
    detach_bridge(); // If the host did not terminate first.
}
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    auto result = AudioEffect::initialize(context);
    if (result == kResultOk) {
        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
        if (!bridge_)
            try {
                bridge_ = std::make_shared<Bridge>(static_cast<ProcessorLink&>(*this));
                bridge_key_ = Bridges::add(bridge_);
            } catch (...) {
                bridge_.reset();
                return kOutOfMemory;
            }
    }
    return result;
}
tresult PLUGIN_API Processor::terminate() {
    detach_bridge();
    return AudioEffect::terminate();
}
void Processor::detach_bridge() noexcept {
    // A controller's call in progress finishes first; later ones refuse.
    if (!bridge_)
        return;
    Bridges::remove(bridge_key_);
    bridge_->detach();
    bridge_.reset();
}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* in, int32 ni,
                                                 SpeakerArrangement* out, int32 no) {
    const auto result = arrange(in, ni, out, no);
    if (result == kResultOk)
        signal_change(); // Whether the filter has been checked may have changed.
    write_log();
    return result;
}
tresult Processor::arrange(SpeakerArrangement* in, int32 ni, SpeakerArrangement* out, int32 no) {
    const Locked lock(*this);
    // DIAG-1: say why. A layout Convolver cannot take is logged once per
    // instance: hosts offer layouts one after another until one is accepted,
    // and only the first declined offer is news. log() skips a repeated line.
    // DIAG-11: an offer Convolver declines is ordinary (REAPER offers every
    // instance 64 channels each way first), so its line says what was offered
    // and that another may follow, not that something failed.
    const auto refuse = [&](auto&& why) noexcept {
        try {
            log("Refused the host's channel layout: " + why());
        } catch (...) { // Out of memory for the message: refused all the same.
        }
        return kResultFalse;
    };
    const auto decline = [&](auto&& offer) noexcept {
        try {
            // An offer, not a problem (false), so macOS's log keeps it out of its errors.
            if (!std::exchange(layout_refusal_logged_, true))
                log(offer() +
                        " Hosts often offer several layouts before one is taken; the next "
                        "\"Started at\" line gives the one in use. Only the first declined layout "
                        "is logged.",
                    false);
        } catch (...) { // Out of memory for the message: declined all the same.
        }
        return kResultFalse;
    };
    if (active_)
        return refuse(
            [] { return std::string("the host changed it while the plug-in was active."); });
    if (ni != 1 || no != 1 || !in || !out)
        return decline([&] {
            if (ni == 1 && no == 1) // Counts that fit, without their arrangements.
                return std::string("The host offered a layout without its speaker arrangements; "
                                   "Convolver needs both, so it declined.");
            return "The host offered " + std::to_string(ni) + " input and " + std::to_string(no) +
                   " output buses; Convolver takes one of each, so it declined.";
        });
    const auto inputs = SpeakerArr::getChannelCount(in[0]);
    const auto outputs = SpeakerArr::getChannelCount(out[0]);
    if (inputs < min_bus_channels || inputs > max_bus_channels || outputs < min_bus_channels ||
        outputs > max_bus_channels || (!identity::unequal_counts && inputs != outputs))
        return decline([&] {
            return "The host offered " + std::to_string(inputs) + " input and " +
                   std::to_string(outputs) + " output channels; Convolver takes " +
                   std::to_string(min_bus_channels) + " to " + std::to_string(max_bus_channels) +
                   " of each, so it declined." +
                   (inputs > max_bus_channels || outputs > max_bus_channels
                        ? " For a filter with more than " + std::to_string(max_bus_channels) +
                              " channels, use the command-line tool."
                        : "");
        });
    auto result = AudioEffect::setBusArrangements(in, ni, out, no);
    if (result == kResultOk) {
        inputs_ = inputs;
        outputs_ = outputs;
        input_arrangement_ = in[0];
        output_arrangement_ = out[0];
    }
    return result;
}
uint32 PLUGIN_API Processor::getTailSamples() {
    const auto published = tail_.load();
    std::uint64_t tail = published.frames;
    // The filter's own frames while inactive, in the host's samples (rounded
    // up) when its rate differs (jrp's review, 21 November 2026).
    const auto own = published.rate;
    const auto host_rate = host_rate_.load();
    if (own > 0 && host_rate > 0 && host_rate != own)
        tail = static_cast<std::uint64_t>(
            std::min(std::ceil(static_cast<double>(tail) * host_rate / own),
                     static_cast<double>(kInfiniteTail)));
    const auto latency = getLatencySamples();
    return tail >= kInfiniteTail - latency ? kInfiniteTail - 1 : tail + latency;
}
void Processor::publish_timing() noexcept {
    const auto* const p = host();
    tail_.store({static_cast<std::uint32_t>(p ? p->tail_frames() : filter_.filter_tail()),
                 !p && filter_.filter() ? filter_.filter()->sample_rate : 0});
    active_latency_.store(p ? p->latency_frames() : inactive);
}
HostSettings Processor::settings() const noexcept {
    return {gain_db_.load(), mix_.load(), latency_.load(), gain_mode_.load()};
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size) {
    return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    tresult result = kInvalidArgument;
    {
        const Locked lock(*this);
        // DIAG-1: say why the host's settings were refused (log() skips a
        // repeat of the same line, as hosts retry).
        std::string why;
        if (active_)
            why = "the host changed them while the plug-in was active.";
        // VST3-9: a rate that is not a whole number of hertz (a pull-down
        // rate) is accepted; no filter suits it, so the plug-in plays
        // silence and says why, keeping its filter for a matching rate.
        else if (!std::isfinite(setup.sampleRate) || setup.sampleRate < 1 ||
                 setup.sampleRate > max_sample_rate)
            why = "the sample rate must be from 1 to " + std::to_string(max_sample_rate) + " Hz.";
        else if (setup.maxSamplesPerBlock < 1 ||
                 static_cast<std::size_t>(setup.maxSamplesPerBlock) > max_block_frames)
            why =
                "the largest block must be 1 to " + std::to_string(max_block_frames) + " samples.";
        else if (canProcessSampleSize(setup.symbolicSampleSize) != kResultOk)
            why = "samples must be 32-bit or 64-bit floating point.";
        if (!why.empty()) {
            try {
                // Every digit the rate needs (VST3-9): 768000.4 Hz is not 768000 Hz.
                std::array<char, 32> rate{};
                const auto [end, error] =
                    std::to_chars(rate.data(), rate.data() + rate.size(), setup.sampleRate);
                log(std::string("Refused the host's processing settings (") +
                    std::string(rate.data(), error == std::errc() ? end : rate.data()) +
                    " Hz, blocks of up to " + std::to_string(setup.maxSamplesPerBlock) +
                    " samples): " + why +
                    " The host may report the plug-in as unusable, or offer other settings.");
            } catch (...) { // Out of memory for the message: refused all the same.
            }
        } else {
            result = AudioEffect::setupProcessing(setup);
            if (result == kResultOk)
                host_rate_.store(setup.sampleRate);
        }
    }
    write_log();
    if (result == kResultOk)
        signal_change(); // Whether the filter has been checked may have changed.
    return result;
}
HostLayout Processor::layout() const {
    HostLayout host{0, static_cast<std::size_t>(inputs_), static_cast<std::size_t>(outputs_),
                    wav_speakers(input_arrangement_), wav_speakers(output_arrangement_)};
    set_host_rate(host, processSetup.sampleRate); // VST3-9: kept exactly, never truncated.
    return host;
}
// The host's configuration is known once the plug-in has been activated with
// it, and stays known while the rate and buses are unchanged. setupProcessing
// is not binding on its own: JUCE-based hosts (pluginval among them) call it
// with placeholder values before restoring a project's state at another rate.
// Until then, restored state is kept and checked at activation (the shared
// adapter contract's "state that arrives before the host is configured").
// For restored state, install() also waits until audio has played with them
// (HostSettling, HostBinding::activation; PLUG-39 step 2).
std::optional<HostLayout> Processor::known_layout() const {
    return settling_.known(layout());
}
tresult PLUGIN_API Processor::setActive(TBool state) {
    const auto result = activate(state);
    write_log();
    signal_change();
    return result;
}
tresult Processor::activate(TBool state) {
    const Locked lock(*this);
    try {
        observe_faults(); // Before the processor that counted them goes.
        if (state) {
            // A filter that does not suit the host plays silence, and is kept so
            // that correcting the rate or layout brings it back.
            const auto chosen = settings();
            const auto now = layout();
            // Read once, so the log says the bypass the processor starts with.
            const bool bypassed = bypass_.load();
            // Not playing: the host does not process while it activates.
            playing_.offer(filter_.start(now, processSetup.maxSamplesPerBlock, chosen, bypassed));
            publish_timing();
            // Audio played with these rate and buses before still counts:
            // only a new configuration needs the host to play again.
            settling_.started(now);
            // What plays and why: a filter restored before the host's rate
            // and buses were known may not suit them.
            try { // Started: nothing here may turn that into a failure.
                const auto& f = filter_.failure();
                log(started_line(filter_, now,
                                 static_cast<std::size_t>(processSetup.maxSamplesPerBlock), &chosen,
                                 bypassed),
                    (f && f->silent) || !filter_.notice().empty());
            } catch (...) { // Out of memory for the line: it is dropped.
            }
        } else {
            // No audio runs while inactive: every processor is freed.
            playing_.clear();
            publish_timing();
            settling_.stopped();
        }
        active_ = state != 0;
        return AudioEffect::setActive(state);
    } catch (const std::exception& e) {
        record_activation_failure(&e);
        return kResultFalse;
    } catch (...) {
        record_activation_failure(nullptr);
        return kResultFalse;
    }
}
void Processor::record_activation_failure(const std::exception* error) noexcept {
    try {
        filter_.fail_activation(error);
        hold_fault_signal();
        if (const auto& f = filter_.failure())
            log(detail::advice_line(failure_message(*f)));
    } catch (...) { // Out of memory for the message: nothing more to keep.
    }
}
void Processor::log(const std::string& message, bool problem) noexcept {
    try {
        if (message.empty() || message == logged_)
            return;
        unlogged_.emplace_back(message, problem);
        logged_ = message;
    } catch (...) { // Out of memory: the line is dropped.
    }
}
bool Processor::observe_faults() noexcept {
    if (!host())
        return false;
    const auto fresh = filter_.observe(host()->faults());
    if (!fresh.any())
        return false;
    try {
        logged_.clear(); // A repeat episode is logged too: the log counts them.
        log(fault_message(fresh));
    } catch (...) { // Out of memory for the message: the status still shows it.
    }
    return true;
}
void Processor::hold_fault_signal() const noexcept {
    if (host())
        host()->hold_fault_signal(filter_.failure_shown());
}
void Processor::write_log() noexcept {
    std::vector<std::pair<std::string, bool>> lines;
    {
        const Locked lock(*this);
        lines.swap(unlogged_);
    }
    for (const auto& [line, problem] : lines)
        host_log(tag_.c_str(), line, problem);
}
bool Processor::chooses_files() const noexcept {
    return files_ != nullptr && has_editor();
}
HostStatusLine Processor::status_locked() const {
    // DIAG-10: the latency that plays, as report_locked() gives it.
    return host_status(filter_, known_layout(), host_log_written(), settings(),
                       shown_bypass_.load(), chooses_files(),
                       host() ? host()->latency_frames() : settings().latency);
}
HostStatusLine Processor::status() {
    const Locked lock(*this);
    if (settling_.playing()) // PLUG-27: the host plays the filter.
        filter_.confirm(known_layout());
    return status_locked();
}
HostReport Processor::report_locked() const {
    HostReportFacts facts;
    facts.format = tag_;
    facts.host = known_layout();
    facts.max_frames = host() ? static_cast<std::size_t>(processSetup.maxSamplesPerBlock) : 0;
    facts.running = host() != nullptr; // setActive(false) frees every processor.
    facts.settings = settings();
    facts.latency_frames = host() ? host()->latency_frames() : facts.settings.latency;
    const auto tail = tail_.load();
    facts.tail_frames = tail.frames;
    facts.tail_rate = tail.rate;
    facts.bypassed = shown_bypass_.load();
    facts.log_written = host_log_written();
    facts.chooses_files = chooses_files();
#ifdef __APPLE__
    facts.file_reader = "macOS AudioToolbox";
#else
    facts.file_reader = "libsndfile";
#endif
    return host_report(filter_, facts);
}
void Processor::signal_change() noexcept {
    if (bridge_)
        bridge_->changed();
    if (!getPeer())
        return;
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(changed_message);
        sendMessage(message);
    }
}
void Processor::send_key(std::uint64_t connection) noexcept {
    if (!getPeer() || !bridge_ || connection == 0)
        return; // Unconnected, or no request to answer: the controller asks when it connects.
    auto message = owned(allocateMessage());
    if (!message)
        return;
    message->setMessageID(pair_message);
    if (auto* attributes = message->getAttributes()) {
        attributes->setInt("key", static_cast<int64>(bridge_key_));
        attributes->setInt("connection", static_cast<int64>(connection));
        sendMessage(message);
    }
}
tresult PLUGIN_API Processor::connect(IConnectionPoint* other) {
    const auto result = AudioEffect::connect(other);
    if (result != kResultOk)
        return result;
    // A request that came before the connection is answered; and the
    // controller is asked to ask again, as the host may have dropped a later
    // one (an answer to an earlier connection is refused).
    if (const auto connection = pair_connection_.load())
        send_key(connection);
    ask_for_request();
    return result;
}
void Processor::ask_for_request() noexcept {
    if (!getPeer())
        return;
    if (auto message = owned(allocateMessage())) {
        message->setMessageID(pair_ask_message);
        sendMessage(message);
    }
}
tresult PLUGIN_API Processor::notify(IMessage* message) {
    if (!message || !message->getMessageID() ||
        std::string_view(message->getMessageID()) != pair_request_message)
        return AudioEffect::notify(message);
    int64 connection = 0;
    auto* attributes = message->getAttributes();
    if (!attributes || attributes->getInt("connection", connection) != kResultOk || connection == 0)
        return kInvalidArgument;
    pair_connection_.store(static_cast<std::uint64_t>(connection));
    send_key(static_cast<std::uint64_t>(connection));
    return kResultOk;
}
PairView Processor::view() {
    const Locked lock(*this);
    if (settling_.playing()) // PLUG-27: the host plays the filter.
        filter_.confirm(known_layout());
    // PLUG-39 step 5: the report holds the Status line too.
    return {report_locked(),      tag_,        settings(),
            shown_bypass_.load(), generation_, getLatencySamples(),
            restored_at_.load()};
}
void Processor::chosen_latency(std::size_t frames) {
    {
        const Locked lock(*this);
        choose_latency(frames);
    }
    write_log();
}
void Processor::chosen_gain_mode(GainMode mode) {
    {
        const Locked lock(*this);
        choose_gain_mode(mode);
    }
    write_log();
}
LoadAnswer Processor::load_chosen(const std::string& path) {
    LoadAnswer answer;
    answer.tried = true;
    answer.accepted = load_file(path, answer.settings);
    write_log();
    return answer;
}
LoadAnswer Processor::read_waiting_list() {
    std::string list;
    {
        const Locked lock(*this);
        // PLUG-40: a list refused before the host's settings were known is
        // read again once the window sees the host playing audio, as if
        // chosen again; the window's read comes on the UI thread, as a
        // chosen file does.
        if (settling_.playing())
            try {
                list = filter_.take_unsettled_list();
            } catch (...) { // Out of memory: the refusal's advice stays.
            }
    }
    if (list.empty())
        return {};
    auto answer = load_chosen(list);
    // PLUG-47: the list joins the windows' recent files, as when a window's
    // choice is accepted, also when the window that chose it has closed
    // since (jrp's review, 22 November 2026).
    if (answer.accepted)
        remember_recent_file(list);
    return answer;
}
void Processor::show_bypass(bool bypass) {
    shown_bypass_.store(bypass);
}
void Processor::offer_name(const std::string& name, const std::string& identity) {
    // Offered to the filter, which shows it only while its filter plays
    // (HostFilter::offer_name()): a host sends a refused preset's controller
    // state too, and that names a filter that does not play. An identity this
    // version could not have written names none.
    if (name.empty() || name.size() > max_filter_name_bytes ||
        identity.size() > max_controller_identity_bytes)
        return;
    if (const auto filter_identity = parse_filter_identity_text(identity))
        try {
            const Locked lock(*this);
            filter_.offer_name(name, *filter_identity);
        } catch (...) { // Out of memory: the report names no file.
        }
}
void Processor::explain_faults(bool only_if_due) {
    bool faulted = false;
    {
        const Locked lock(*this);
        const auto now = std::chrono::steady_clock::now();
        if (!only_if_due || now - report_faults_at_ >= report_fault_interval) {
            faulted = observe_faults();
            report_faults_at_ = now;
        }
    }
    if (faulted)
        write_log();
}
// VST3-16: while active, the new Latency plays at once, before the
// controller asks the host to restart (kLatencyChanged): REAPER 7.85 reads
// getLatencySamples() inside that request and does not restart the
// component, so the latency must already be the one that plays. Built off
// the audio thread, as an activation builds it; process() switches to it at
// its next block, which starts empty, so the sound pauses briefly, as in the
// Audio Unit (apply_latency()).
void Processor::choose_latency(std::size_t frames) noexcept {
    // VST3-32 (pre-1.0 review of 0.19.21): the same Latency again, from a
    // restored state, retries one whose processor could not be built, as
    // the Audio Unit does; while nothing plays it, the next activation
    // builds it anyway.
    if (latency_.exchange(frames) == frames &&
        (!host() || host()->latency_frames() == frames || !settling_.last_start()))
        return;
    const auto before = host() ? host()->latency_frames() : frames;
    const auto& started = settling_.last_start();
    if (!host() || !started || before == frames) {
        try {
            log("Latency set to " + std::to_string(frames) + " frames.", false);
        } catch (...) { // Out of memory for the line: the choice stands.
        }
        return;
    }
    observe_faults(); // Before the processor that counted them goes.
    bool changed = false;
    std::string why;
    try {
        if (latency_preparation_for_tests)
            latency_preparation_for_tests();
        // Nothing after start() can fail: what plays, what the host reads
        // and what the status says change together (ROBUST-8). process()
        // plays it from its next block; the one it replaces is freed by a
        // later control call once process() has let it go.
        playing_.offer(
            filter_.start(*started, processSetup.maxSamplesPerBlock, settings(), bypass_.load()));
        publish_timing();
        changed = true;
    } catch (const std::exception& e) {
        try {
            why = detail::advice_line(detail::advice_of(e));
        } catch (...) {
        }
    } catch (...) {
    }
    // If it failed, the running processor keeps playing at its latency, and
    // the window's report says the chosen one is not in use.
    try { // Best effort: out of memory for the line changes nothing else.
        log(changed ? "Latency changed from " + std::to_string(before) + " to " +
                          std::to_string(frames) +
                          " frames; the sound paused briefly while the new processor started."
                    : "Latency " + std::to_string(frames) +
                          " frames could not be applied, so the plug-in keeps " +
                          std::to_string(before) + " frames" +
                          (why.empty() ? std::string(".") : ": " + why),
            !changed);
    } catch (...) {
    }
}
void Processor::choose_gain_mode(GainMode mode) noexcept {
    const bool estimate = mode == GainMode::estimate;
    if (gain_mode_.load() == mode && filter_.estimating() == estimate)
        return;
    // Played from the next audio call: until the estimate below arrives,
    // Estimated plays the no-clipping figure.
    gain_mode_.store(mode);
    // Off the audio thread: the filter's estimate when Estimated needs it,
    // given to the playing filter in place (PLUG-64).
    const auto change = change_gain_mode(filter_, settings(), [this] { return host(); });
    log(change.line, change.problem); // An empty line (out of memory) logs nothing.
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    // Not a place to explain faults: hosts may call this on the audio
    // thread, where nothing may lock, allocate or write the log.
    // VST3-16: a new Latency may replace it meanwhile.
    const auto held = playing_.hold<starting_reader>();
    if (state && held) {
        held->set_bypass(bypass_.load());
        held->reset();
    }
    return kResultOk;
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    const auto result = load_state(stream);
    write_log();
    signal_change();
    return result;
}
namespace {
// What the host says the state is (VST 3.6 IStreamAttributes, optional):
// "Project", "Default" or another of PresetAttributes::kStateType's values,
// or empty when it does not say.
std::string state_type(IBStream* stream) {
    FUnknownPtr<IStreamAttributes> attributes(stream);
    IAttributeList* list = attributes ? attributes->getAttributes() : nullptr;
    String128 text{};
    if (!list || list->getString(PresetAttributes::kStateType, text, sizeof text) != kResultTrue)
        return {};
    std::string type;
    for (std::size_t i = 0; i < std::size(text) - 1 && text[i]; ++i)
        type += text[i] > 0 && text[i] < 128 ? static_cast<char>(text[i]) : '?';
    return type;
}
} // namespace
tresult Processor::load_state(IBStream* stream) {
    const Locked lock(*this);
    observe_faults(); // Explained under the filter that played; an accepted load clears them.
    try {
        // Only reading the state counts as unreadable: `next` is built from
        // what read_state() returns, with no move that a debug library's
        // allocation could fail in that catch (VST3-20).
        HostState next = [&] {
            try {
                return read_state(stream);
            } catch (const NewerHostState& e) {
                filter_.reject(HostFailureKind::state_newer, state_read_message(e));
                throw;
            } catch (const std::exception& e) {
                filter_.reject(HostFailureKind::state_unreadable, state_read_message(e));
                throw;
            }
        }();
        // VST3-4: restored state without settings (an older state, or a
        // preset from convolver-preset) restores the defaults.
        if (!next.settings)
            next.settings.emplace();
        HostRequest request;
        request.kind = state_type(stream) == StateType::kProject ? HostRequestKind::session
                                                                 : HostRequestKind::restore;
        // PLUG-49, PLUG-40 in Audacity 4.0.2 (jrp's live session, 22
        // November 2026): at every playback start Audacity restores the
        // state it last read
        // from the plug-in (VST3Wrapper::Initialize, FetchSettings) before
        // the audio starts. A configuration list refused because the host's
        // settings were not known is not in that state, so restoring it
        // forgot the list, the window never read it again, and the export
        // was silent. State that is exactly what the plug-in would save now
        // changes nothing, so the list keeps waiting.
        if (request.kind == HostRequestKind::restore && filter_.list_waits() &&
            unchanged_state(next)) {
            restored_at_.store(Bridges::stamp()); // Newer than a choice before it.
            return kResultOk;
        }
        install(std::move(next), request);
        restored_at_.store(Bridges::stamp()); // Under control_, as view() reads it.
        return kResultOk;
    } catch (...) {
        refused();
        return kResultFalse;
    }
}
HostLoadCheck Processor::load_check(HostRequestKind kind) const noexcept {
    // A file chosen in the window is refused for not suiting the host only
    // while the host plays audio through the plug-in (PLUG-27); otherwise it
    // is kept and checked at the next activation, as a preset is.
    // Restored state is refused for not suiting the host only once the host
    // has also played audio with it, and never when the host says it is
    // restoring a project: hosts start the plug-in on placeholder buses
    // before they restore a project (REAPER: stereo, then the track's six
    // channels; jrp's crossover retest, 7 November 2026). Until then a
    // restored filter that does not suit is kept, silent, and checked again
    // at the next activation (HostSettling::check()).
    return settling_.check(kind);
}
std::optional<HostSettings> Processor::install(HostState next, const HostRequest& request) {
    // While active, a prepared replacement is published; inactive, the next
    // activation installs it. load_check() says what a filter that does not
    // suit the host means.
    const auto check = load_check(request.kind);
    const auto known = known_layout();
    // PLUG-38: state or a preset holding the filter that plays keeps it and
    // its history, and the settings below ramp to theirs; a project the host
    // says it is opening starts clean (HostFilter::load() decides both).
    const auto change = filter_.load(
        std::move(next), known,
        [&](const HostPlayback& c) {
            if (host())
                host()->play(c);
        },
        request, host(), check);
    // Nothing from here on can throw: the request is accepted.
    hold_fault_signal();
    publish_timing();
    // VST3-26: the controller takes these settings and bypass.
    if (change.bypass || change.settings)
        ++generation_;
    if (change.bypass) {
        bypass_.store(*change.bypass);
        shown_bypass_.store(*change.bypass);
    }
    if (const auto& saved = change.settings) {
        gain_db_.store(saved->gain_db);
        mix_.store(saved->mix);
        // The estimate, if the mode needs it, was found as the filter loaded.
        if (gain_mode_.exchange(saved->gain_mode) != saved->gain_mode)
            try {
                log(gain_mode_line(settings(), filter_.gains(), filter_.filter() != nullptr),
                    false);
            } catch (...) { // Out of memory for the line: the mode stands.
            }
        choose_latency(saved->latency);
    }
    logged_.clear(); // A later failure is logged even if it repeats an earlier one.
    try {            // Accepted: nothing here may turn that into a rejection.
        // A restored failed load says why it plays silence (PLUG-62).
        log(accepted_line(filter_, known, layout(), request.kind, check,
                          change.settings.has_value(), settings(), bypass_.load()),
            filter_.failure().has_value() || !filter_.notice().empty());
    } catch (...) { // Out of memory for the line: it is dropped.
    }
    return change.settings;
}
void Processor::refused() noexcept {
    // A failed first load plays silence rather than the unfiltered input,
    // without allocating: memory may have run out (ROBUST-3), maybe before
    // the failure could be recorded (ROBUST-7).
    filter_.fail_request(host());
    publish_timing();
    hold_fault_signal();
    try {
        log(rejected_line(filter_, host() != nullptr, bypass_.load()));
    } catch (...) { // Out of memory for the line: it is dropped.
    }
}
bool Processor::load_file(std::string_view chosen, std::optional<HostSettings>& settings) {
    settings.reset();
    const Locked lock(*this);
    observe_faults();
    // A refusal is recorded where it is found and answered below; copying
    // the path needs memory too, and a first load that runs out fails as a
    // refusal does (ROBUST-7).
    const bool accepted = [&] {
        try {
            return install_chosen(std::string(chosen), settings);
        } catch (...) {
            return false;
        }
    }();
    if (!accepted) {
        refused();
        return false;
    }
    return true;
}
bool Processor::install_chosen(const std::string& path, std::optional<HostSettings>& settings) {
    HostRequest request{HostFilterSource::file, path, 0};
    request.kind = HostRequestKind::chosen_file;
    // PLUG-27: a file that does not suit the host is refused only while the
    // host plays audio through the plug-in. Otherwise its rate and channels
    // may be a placeholder (Audacity sets its window's copy up at 44.1 kHz
    // whatever the project's rate), so a single filter is read as it is and
    // kept, checked when the host starts the plug-in; a list chooses its
    // entry for the configuration the host last started with, or waits for
    // one (read_filter_file()).
    const bool settled = load_check(request.kind) != HostLoadCheck::keep_until_suited;
    // PLUG-42: settled, with nothing suiting playing, a single filter that
    // does not suit is kept as a preset would be, not refused.
    const bool keep_unsuited = settled && !filter_.plays_suiting();
    const auto host = known_layout();
    const auto* reader = files_;
    if (!reader) {
        filter_.reject(HostFailureKind::filter_file,
                       {"this build of Convolver cannot read filter files in the plug-in, so " +
                            path + " was not loaded",
                        {},
                        "Make a preset from it with convolver-preset, and load the preset "
                        "instead.",
                        {}},
                       request);
        return false;
    }
    FilterFileFailure why;
    // The files it was read from, so the window can reload it (VST3-11).
    // PLUG-26: a Convolver .vstpreset too.
    // PLUG-53: a chosen .vstpreset's filter file name, shown beside the
    // preset's own name (HostState::preset_name).
    auto next = read_chosen_file(path, host, *reader, why, &request.files, &request.wider_entry,
                                 settled, keep_unsuited, &request.name);
    if (!next) {
        filter_.reject(why.kind, why.message, request, why.filter());
        return false;
    }
    settings = install(std::move(*next), request);
    return true;
}
Advice Processor::notice() {
    const Locked lock(*this);
    return filter_.notice();
}
std::optional<HostFailure> Processor::failure() {
    const Locked lock(*this);
    return filter_.failure();
}
// Whether `next` is what getState() would save now (the lock held).
// VST3-31 (jrp's review of 7bd024f, §2.1): compared in place, field by
// field as the bytes would be, so a large filter is neither encoded twice
// nor copied, and the answer never depends on memory.
bool Processor::unchanged_state(const HostState& next) const noexcept {
    auto now = filter_.saved_view();
    now.bypass = bypass_.load();
    now.settings = settings(); // As saved_state() takes them.
    return same_host_state(view_of(next), now);
}
// What getState() saves, taken with the lock held: the filter shared, not
// copied (PERF-26).
HostSnapshot Processor::saved_state() const {
    auto state = filter_.snapshot();
    state.bypass = bypass_.load();
    state.settings = settings(); // Version 3, defaults included (PLUG-35).
    return state;
}
tresult PLUGIN_API Processor::getState(IBStream* stream) {
    bool faulted = false;
    tresult result = kResultFalse;
    std::optional<HostSnapshot> state;
    {
        const Locked lock(*this);
        // Hosts save often (autosave too): faults are explained here as well.
        faulted = observe_faults();
        try {
            state = saved_state();
            if (host())
                host()->collect();
        } catch (...) {
        }
    }
    // PERF-26 (jrp's review of 7bd024f): encoded and written outside the
    // lock, so a long filter's save does not hold up the controls or a
    // replacement; the snapshot keeps every part from one moment.
    if (saving_for_tests)
        saving_for_tests();
    try {
        if (state)
            result = write_state(stream, state->view()) ? kResultOk : kResultFalse;
    } catch (...) {
    }
    if (result != kResultOk) { // DIAG-1: the host may not say that saving failed.
        const Locked lock(*this);
        try {
            log(save_failure_message);
        } catch (...) { // Out of memory for the message.
        }
    }
    if (faulted || result != kResultOk)
        write_log();
    if (faulted)
        signal_change();
    return result;
}
template <class T>
bool Processor::segment(HostProcessor& host, ProcessData& d, int32 start, int32 count) noexcept {
    std::array<const T*, max_bus_channels> in{};
    std::array<T*, max_bus_channels> out{};
    if (d.numInputs && d.inputs && getAudioInput(0)->isActive()) {
        auto& bus = d.inputs[0];
        if (bus.numChannels != inputs_)
            return false;
        // Input silence flags are not trusted: a flagged buffer must hold
        // zeros, so reading it costs nothing, and a host that flags audio by
        // mistake (the SDK validator's tail test does) is not silenced (VST3-8).
        for (int c = 0; c < inputs_; ++c) {
            if constexpr (std::is_same_v<T, float>) {
                if (bus.channelBuffers32 && bus.channelBuffers32[c])
                    in[c] = bus.channelBuffers32[c] + start;
            } else {
                if (bus.channelBuffers64 && bus.channelBuffers64[c])
                    in[c] = bus.channelBuffers64[c] + start;
            }
        }
    }
    if (d.numOutputs && d.outputs && getAudioOutput(0)->isActive()) {
        auto& bus = d.outputs[0];
        if (bus.numChannels != outputs_)
            return false;
        for (int c = 0; c < outputs_; ++c) {
            if constexpr (std::is_same_v<T, float>) {
                if (bus.channelBuffers32 && bus.channelBuffers32[c])
                    out[c] = bus.channelBuffers32[c] + start;
            } else {
                if (bus.channelBuffers64 && bus.channelBuffers64[c])
                    out[c] = bus.channelBuffers64[c] + start;
            }
        }
    }
    host.set_bypass(bypass_.load());
    host.set_gain_db(gain_db_.load());
    host.set_gain_mode(gain_mode_.load());
    host.set_mix(mix_.load());
    return host.process(std::span(in).first(inputs_), std::span(out).first(outputs_), count);
}
tresult PLUGIN_API Processor::process(ProcessData& d) {
    if (d.numSamples < 0 || d.numSamples > processSetup.maxSamplesPerBlock || d.numInputs < 0 ||
        d.numInputs > 1 || d.numOutputs < 0 || d.numOutputs > 1)
        return kInvalidArgument;
    // VST3-16: the processor this call plays, which a new Latency may
    // replace on another thread meanwhile (choose_latency()).
    const auto held = playing_.hold<processing_reader>();
    auto* host = held.get();
    if (holding_for_tests)
        holding_for_tests();
    if (d.numSamples && (!host || canProcessSampleSize(d.symbolicSampleSize) != kResultOk))
        return kInvalidArgument;
    if (d.numSamples) // Restored state is now checked against the host (install()).
        settling_.played();
    if (d.numOutputs && d.outputs)
        d.outputs[0].silenceFlags = 0; // Set again below once the output is known.
    auto render = [&](int32 first, int32 count) {
        if (!count)
            return true;
        return d.symbolicSampleSize == kSample32 ? segment<float>(*host, d, first, count)
                                                 : segment<double>(*host, d, first, count);
    };
    int32 position = 0;
    if (d.inputParameterChanges) {
        // VST3-4: Gain and Mix take the last value of the block, as the
        // SDK's examples do; HostProcessor ramps each change over 1.33 ms.
        // Latency is not read here: the controller gives it to the processor.
        for (int32 i = 0; i < d.inputParameterChanges->getParameterCount(); ++i) {
            auto* queue = d.inputParameterChanges->getParameterData(i);
            if (!queue || (queue->getParameterId() != gain_id && queue->getParameterId() != mix_id))
                continue;
            int32 offset = 0;
            ParamValue value = 0;
            const auto points = queue->getPointCount();
            if (points <= 0 || queue->getPoint(points - 1, offset, value) != kResultOk ||
                !std::isfinite(value))
                continue;
            if (queue->getParameterId() == gain_id)
                gain_db_.store(gain_db(value));
            else
                mix_.store(std::clamp(value, 0.0, 1.0));
        }
        for (int32 i = 0; i < d.inputParameterChanges->getParameterCount(); ++i) {
            auto* queue = d.inputParameterChanges->getParameterData(i);
            if (!queue || queue->getParameterId() != bypass_id)
                continue;
            for (int32 j = 0; j < queue->getPointCount(); ++j) {
                int32 offset = 0;
                ParamValue value = 0;
                if (queue->getPoint(j, offset, value) != kResultOk || !std::isfinite(value))
                    continue;
                offset = std::clamp(offset, position, d.numSamples);
                if (!render(position, offset - position))
                    return signal_faults(host, d), kInvalidArgument;
                position = offset;
                bypass_.store(value >= .5);
                shown_bypass_.store(value >= .5);
            }
        }
    }
    const bool rendered = render(position, d.numSamples - position);
    if (rendered)
        mark_silence(d);
    signal_faults(host, d);
    return rendered ? kResultOk : kInvalidArgument;
}
// VST3-10: flag each output channel that this call left all zero, so a host
// can skip work downstream. Input flags are never copied: a filter's tail and
// the latency outlast silent input, so an output is flagged only once it has
// drained. Bypass and a muted filter keep running the convolution, so the
// history stays current while the flags are set.
void Processor::mark_silence(ProcessData& d) noexcept {
    if (!d.numOutputs || !d.outputs)
        return;
    auto& bus = d.outputs[0];
    if (!d.numSamples || bus.numChannels != outputs_ || !getAudioOutput(0)->isActive())
        return;
    const auto silent = [&](const auto* channel) {
        return channel &&
               std::all_of(channel, channel + d.numSamples, [](auto v) { return v == 0; });
    };
    for (int c = 0; c < outputs_ && c < 64; ++c) {
        const bool zero = d.symbolicSampleSize == kSample32
                              ? bus.channelBuffers32 && silent(bus.channelBuffers32[c])
                              : bus.channelBuffers64 && silent(bus.channelBuffers64[c]);
        if (zero)
            bus.silenceFlags |= uint64(1) << c;
    }
}
// DIAG-1: a new processing fault shows at once in hosts that pass output
// parameter changes on, without logging or allocating here: Status becomes
// Fault, and the controller asks the control side to explain.
void Processor::signal_faults(HostProcessor* host, ProcessData& d) noexcept {
    if (!host || !d.outputParameterChanges || !host->fault_to_signal())
        return;
    int32 index = 0, point = 0;
    if (auto* queue = d.outputParameterChanges->addParameterData(status_id, index);
        queue && queue->addPoint(0, normalized_status(HostStatus::fault), point) == kResultOk)
        host->fault_signalled();
}
} // namespace convolver::vst3
