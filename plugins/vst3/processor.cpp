// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "bytes.hpp"
#include "convolver_sdk.hpp"
#include "editor.hpp"
#include "filter_file.hpp"
#include "host_log.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/vstpresetkeys.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
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
    return decode_host_state(bytes);
}
bool write_state(IBStream* stream, const HostState& state) {
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
std::string encode_report(const std::vector<std::pair<std::string, std::string>>& fields) {
    std::string bytes;
    for (const auto& [name, value] : fields) {
        bytes += name;
        bytes += '\0';
        bytes += value;
        bytes += '\0';
    }
    return bytes;
}
std::vector<std::pair<std::string, std::string>> decode_report(std::string_view bytes) {
    std::vector<std::pair<std::string, std::string>> fields;
    while (!bytes.empty()) {
        const auto name_end = bytes.find('\0');
        if (name_end == std::string_view::npos)
            break;
        const auto value_end = bytes.find('\0', name_end + 1);
        if (value_end == std::string_view::npos)
            break;
        fields.emplace_back(bytes.substr(0, name_end),
                            bytes.substr(name_end + 1, value_end - name_end - 1));
        bytes.remove_prefix(value_end + 1);
    }
    return fields;
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
tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    auto result = AudioEffect::initialize(context);
    if (result == kResultOk) {
        addAudioInput(STR16("Input"), SpeakerArr::kStereo);
        addAudioOutput(STR16("Output"), SpeakerArr::kStereo);
    }
    return result;
}
tresult PLUGIN_API Processor::setBusArrangements(SpeakerArrangement* in, int32 ni,
                                                 SpeakerArrangement* out, int32 no) {
    const auto result = arrange(in, ni, out, no);
    if (result == kResultOk)
        send_status(); // Whether the filter has been checked may have changed.
    else
        write_log();
    return result;
}
tresult Processor::arrange(SpeakerArrangement* in, int32 ni, SpeakerArrangement* out, int32 no) {
    std::lock_guard lock(control_);
    // DIAG-1: say why. A layout Convolver cannot take is logged once per
    // instance: hosts offer layouts one after another until one is accepted,
    // and only the first refusal is news. log() skips a repeated line.
    const auto refuse = [&](auto&& why, bool once) noexcept {
        try {
            if (once && std::exchange(layout_refusal_logged_, true))
                return kResultFalse;
            log("Refused the host's channel layout: " + why() +
                (once ? " The host may go on to offer another layout; only the first refusal "
                        "is logged."
                      : ""));
        } catch (...) { // Out of memory for the message: refused all the same.
        }
        return kResultFalse;
    };
    if (active_)
        return refuse(
            [] { return std::string("the host changed it while the plug-in was active."); }, false);
    if (ni != 1 || no != 1 || !in || !out)
        return refuse(
            [&] {
                return "it has " + std::to_string(ni) + " input and " + std::to_string(no) +
                       " output buses; Convolver takes one of each.";
            },
            true);
    const auto inputs = SpeakerArr::getChannelCount(in[0]);
    const auto outputs = SpeakerArr::getChannelCount(out[0]);
    if (inputs < min_bus_channels || inputs > max_bus_channels || outputs < min_bus_channels ||
        outputs > max_bus_channels || (!identity::unequal_counts && inputs != outputs))
        return refuse(
            [&] {
                return "it has " + std::to_string(inputs) + " input and " +
                       std::to_string(outputs) + " output channels; Convolver takes " +
                       std::to_string(min_bus_channels) + " to " +
                       std::to_string(max_bus_channels) + " of each." +
                       (inputs > max_bus_channels || outputs > max_bus_channels
                            ? " Use the command-line tool for filters with more channels."
                            : "");
            },
            true);
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
    const auto tail = tail_.load();
    const auto latency = getLatencySamples();
    return tail >= kInfiniteTail - latency ? kInfiniteTail - 1 : tail + latency;
}
HostSettings Processor::settings() const noexcept {
    return {gain_db_.load(), mix_.load(), latency_.load()};
}
tresult PLUGIN_API Processor::canProcessSampleSize(int32 size) {
    return size == kSample32 || size == kSample64 ? kResultOk : kResultFalse;
}
tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup) {
    tresult result = kInvalidArgument;
    {
        std::lock_guard lock(control_);
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
        } else
            result = AudioEffect::setupProcessing(setup);
    }
    if (result == kResultOk)
        send_status(); // Whether the filter has been checked may have changed.
    else
        write_log();
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
// For restored state, install() also waits until audio has played with them.
std::optional<HostLayout> Processor::known_layout() const {
    const auto now = layout();
    if (!activated_ || !same_configuration(*activated_, now))
        return std::nullopt;
    return now;
}
tresult PLUGIN_API Processor::setActive(TBool state) {
    const auto result = activate(state);
    write_log();
    send_status();
    return result;
}
tresult Processor::activate(TBool state) {
    std::lock_guard lock(control_);
    try {
        observe_faults(); // Before the processor that counted them goes.
        if (state) {
            // A filter that does not suit the host plays silence, and is kept so
            // that correcting the rate or layout brings it back.
            const auto chosen = settings();
            const auto host = layout();
            auto next = std::make_unique<HostProcessor>(
                filter_.activate(host), processSetup.maxSamplesPerBlock, chosen, host.inputs);
            tail_.store(static_cast<uint32>(next->tail_frames()));
            next->set_bypass(bypass_.load());
            next->reset();
            // Audio played with these rate and buses before still counts:
            // only a new configuration needs the host to play again.
            if (!activated_ || !same_configuration(*activated_, host))
                played_.store(false);
            // Not playing: the host does not process while it activates.
            release_processor(install_processor(std::move(next)));
            active_latency_.store(host_->latency_frames());
            hold_fault_signal();
            activated_ = layout();
            // What plays and why: a filter restored before the host's rate
            // and buses were known may not suit them.
            try { // Started: nothing here may turn that into a failure.
                const auto& f = filter_.failure();
                log(started_line(filter_, *activated_,
                                 static_cast<std::size_t>(processSetup.maxSamplesPerBlock),
                                 &chosen),
                    (f && f->silent) || !filter_.notice().empty());
            } catch (...) { // Out of memory for the line: it is dropped.
            }
        } else {
            release_processor(install_processor(nullptr));
            active_latency_.store(inactive);
        }
        active_ = state != 0;
        return AudioEffect::setActive(state);
    } catch (const std::exception& e) {
        record_activation_failure(e.what());
        return kResultFalse;
    } catch (...) {
        record_activation_failure("the processor could not be built.");
        return kResultFalse;
    }
}
void Processor::record_activation_failure(const char* why) noexcept {
    try {
        filter_.fail_activation(why);
        hold_fault_signal();
        log(filter_.error());
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
    if (!host_)
        return false;
    const auto fresh = filter_.observe(host_->faults());
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
    if (host_)
        host_->hold_fault_signal(filter_.failure_shown());
}
void Processor::write_log() noexcept {
    std::vector<std::pair<std::string, bool>> lines;
    {
        std::lock_guard lock(control_);
        lines.swap(unlogged_);
    }
    for (const auto& [line, problem] : lines)
        host_log(tag_.c_str(), line, problem);
}
bool Processor::chooses_files() const noexcept {
    return files_ != nullptr && has_editor();
}
HostStatusLine Processor::status_locked() const {
    return host_status(filter_, host_ ? std::optional(layout()) : known_layout(),
                       host_log_written(), settings(), shown_bypass_.load(), chooses_files());
}
HostStatusLine Processor::status() {
    std::lock_guard lock(control_);
    return status_locked();
}
HostReport Processor::report_locked() const {
    HostReportFacts facts;
    facts.format = tag_;
    facts.host = host_ ? std::optional(layout()) : known_layout();
    facts.max_frames = host_ ? static_cast<std::size_t>(processSetup.maxSamplesPerBlock) : 0;
    facts.settings = settings();
    facts.latency_frames = host_ ? host_->latency_frames() : facts.settings.latency;
    facts.tail_frames = tail_.load();
    facts.bypassed = shown_bypass_.load();
    facts.log_written = host_log_written();
    facts.chooses_files = chooses_files();
#ifdef __APPLE__
    facts.file_reader = "macOS AudioToolbox";
#else
    facts.file_reader = "libsndfile";
#endif
    auto report = host_report(filter_, facts);
    // PLUG-22: saved state (no file path) of the filter the saved name
    // belongs to is named, in the fields and the text alike.
    if (report.filter_path.empty() && !report.filter_identity.empty()) {
        if (!saved_name_.empty() && report.filter_identity == saved_identity_)
            report.filter_name = saved_name_;
        else if (!kept_name_.empty() && report.filter_identity == kept_identity_)
            report.filter_name = kept_name_;
    }
    return report;
}
void Processor::send_status(const StatusAnswer& answer) noexcept {
    try {
        HostStatusLine line;
        std::string fields, text;
        {
            std::lock_guard lock(control_);
            line = status_locked();
            const auto report = report_locked();
            fields = encode_report(report_fields(report));
            text = report_text(report);
        }
        if (!getPeer())
            return;
        auto message = owned(allocateMessage());
        if (!message)
            return;
        message->setMessageID(status_message);
        auto* attributes = message->getAttributes();
        if (!attributes)
            return;
        attributes->setInt("status", static_cast<int64>(line.status));
        attributes->setBinary("text", line.text.data(), static_cast<uint32>(line.text.size()));
        attributes->setBinary("report", fields.data(), static_cast<uint32>(fields.size()));
        attributes->setBinary("report text", text.data(), static_cast<uint32>(text.size()));
        attributes->setBinary("format", tag_.data(), static_cast<uint32>(tag_.size()));
        if (answer.load_request) {
            attributes->setInt("load request", answer.load_request);
            attributes->setInt("accepted", answer.accepted ? 1 : 0);
        }
        if (answer.latency_request)
            attributes->setInt("latency request", answer.latency_request);
        sendMessage(message);
    } catch (...) { // Out of memory: the controller keeps the previous status.
    }
}
tresult PLUGIN_API Processor::connect(IConnectionPoint* other) {
    const auto result = AudioEffect::connect(other);
    if (result == kResultOk)
        send_status();
    return result;
}
tresult PLUGIN_API Processor::notify(IMessage* message) {
    if (!message || !message->getMessageID())
        return AudioEffect::notify(message);
    const std::string_view id = message->getMessageID();
    if (id == latency_message) {
        int64 frames = 0;
        auto* attributes = message->getAttributes();
        if (!attributes || attributes->getInt("frames", frames) != kResultOk ||
            std::find(host_latencies.begin(), host_latencies.end(), frames) == host_latencies.end())
            return kInvalidArgument;
        int64 request = 0;
        attributes->getInt("request", request);
        {
            std::lock_guard lock(control_);
            choose_latency(static_cast<std::size_t>(frames));
        }
        write_log();
        send_status({.latency_request = request});
        return kResultOk;
    }
    if (id == load_message) {
        const void* data = nullptr;
        uint32 size = 0;
        auto* attributes = message->getAttributes();
        if (!attributes || attributes->getBinary("path", data, size) != kResultOk || !size || !data)
            return kInvalidArgument;
        int64 request = 0;
        attributes->getInt("request", request);
        const bool accepted = load_file({static_cast<const char*>(data), size});
        write_log();
        send_status({.load_request = request, .accepted = accepted});
        return accepted ? kResultOk : kResultFalse;
    }
    if (id == report_message) {
        bool faulted = false;
        {
            std::lock_guard lock(control_);
            const auto now = std::chrono::steady_clock::now();
            if (now - report_faults_at_ >= report_fault_interval) {
                faulted = observe_faults();
                report_faults_at_ = now;
            }
        }
        if (faulted)
            write_log();
        send_status();
        return kResultOk;
    }
    if (id == name_message) {
        // PLUG-22: the controller state's name and identity (Controller::setState).
        const void* name = nullptr;
        const void* identity = nullptr;
        uint32 name_size = 0, identity_size = 0;
        auto* attributes = message->getAttributes();
        if (!attributes)
            return kInvalidArgument;
        // Neither: no name (an empty or older controller state).
        const bool has_name = attributes->getBinary("name", name, name_size) == kResultOk;
        const bool has_identity =
            attributes->getBinary("identity", identity, identity_size) == kResultOk;
        if (has_name != has_identity || (name_size && !name) || (identity_size && !identity) ||
            name_size > max_filter_name_bytes || identity_size > max_controller_identity_bytes)
            return kInvalidArgument;
        try {
            std::lock_guard lock(control_);
            // The pair that names the playing filter is kept: a host sends
            // a refused preset's controller state too, and that names a
            // filter that does not play.
            const auto playing = report_locked().filter_identity;
            if (!saved_identity_.empty() && saved_identity_ == playing) {
                kept_name_ = saved_name_;
                kept_identity_ = saved_identity_;
            }
            saved_name_.assign(static_cast<const char*>(name), name_size);
            saved_identity_.assign(static_cast<const char*>(identity), identity_size);
        } catch (...) {
            return kOutOfMemory;
        }
        send_status();
        return kResultOk;
    }
    if (id == bypass_message) {
        int64 bypass = 0;
        auto* attributes = message->getAttributes();
        if (!attributes || attributes->getInt("bypass", bypass) != kResultOk)
            return kInvalidArgument;
        shown_bypass_.store(bypass != 0);
        send_status();
        return kResultOk;
    }
    if (id != faults_message)
        return AudioEffect::notify(message);
    {
        std::lock_guard lock(control_);
        observe_faults();
    }
    write_log();
    send_status();
    return kResultOk;
}
// VST3-16: while active, the new Latency plays at once, before the
// controller asks the host to restart (kLatencyChanged): REAPER 7.85 reads
// getLatencySamples() inside that request and does not restart the
// component, so the latency must already be the one that plays. Built off
// the audio thread, as an activation builds it; process() switches to it at
// its next block, which starts empty, so the sound pauses briefly, as in the
// Audio Unit (apply_latency()).
void Processor::choose_latency(std::size_t frames) noexcept {
    if (latency_.exchange(frames) == frames)
        return;
    const auto before = host_ ? host_->latency_frames() : frames;
    if (!host_ || !activated_ || before == frames) {
        try {
            log("Latency set to " + std::to_string(frames) + " frames.", false);
        } catch (...) { // Out of memory for the line: the choice stands.
        }
        return;
    }
    observe_faults(); // Before the processor that counted them goes.
    std::unique_ptr<HostProcessor> previous;
    std::string why;
    try {
        const auto playback = filter_.activate(*activated_);
        if (latency_preparation_for_tests)
            latency_preparation_for_tests();
        auto next = std::make_unique<HostProcessor>(playback, processSetup.maxSamplesPerBlock,
                                                    settings(), activated_->inputs);
        next->set_bypass(bypass_.load());
        next->reset();
        // Before it plays, so its first block cannot show Fault over a load
        // failure that is showing.
        next->hold_fault_signal(filter_.failure_shown());
        // Nothing below can fail: what plays and what the host reads change
        // together.
        tail_.store(static_cast<uint32>(next->tail_frames()));
        active_latency_.store(next->latency_frames());
        previous = install_processor(std::move(next));
    } catch (const std::exception& e) {
        try {
            why = e.what();
        } catch (...) {
        }
    } catch (...) {
    }
    const bool changed = previous != nullptr;
    // The previous processor plays until process() has switched.
    release_processor(std::move(previous));
    if (!changed) {
        // The running processor keeps playing at its latency, and the
        // window's report says the chosen one is not in use. activate()
        // started the fault count again for a new processor, so count from
        // the running one's totals (logged above) rather than log them again.
        (void)filter_.observe(host_->faults());
    }
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
std::unique_ptr<HostProcessor>
Processor::install_processor(std::unique_ptr<HostProcessor> next) noexcept {
    played_host_.store(next.get());
    std::swap(host_, next);
    return next; // The previous processor.
}
void Processor::release_processor(std::unique_ptr<HostProcessor> previous) noexcept {
    // hold() marks its processor and then checks that it is still
    // played_host_ (sequentially consistent), so once the previous one is
    // marked by neither, process() and setProcessing() are not using it and
    // never will.
    while (previous &&
           (processing_host_.load() == previous.get() || starting_host_.load() == previous.get())) {
        if (releasing_for_tests)
            releasing_for_tests();
        std::this_thread::yield();
    }
}
Processor::Held Processor::hold(std::atomic<HostProcessor*>& marker) noexcept {
    HostProcessor* host = nullptr;
    do {
        host = played_host_.load();
        marker.store(host);
    } while (host != played_host_.load());
    return {marker, host};
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    // Not a place to explain faults: hosts may call this on the audio
    // thread, where nothing may lock, allocate or write the log.
    // VST3-16: a new Latency may replace it meanwhile.
    const auto held = hold(starting_host_);
    if (state && held.host) {
        held.host->set_bypass(bypass_.load());
        held.host->reset();
    }
    return kResultOk;
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    const auto result = load_state(stream);
    write_log();
    send_status();
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
    std::lock_guard lock(control_);
    observe_faults(); // Explained under the filter that played; an accepted load clears them.
    const bool was_failed = filter_.failed();
    try {
        HostState next;
        try {
            next = read_state(stream);
        } catch (const NewerHostState& e) {
            filter_.reject(HostFailureKind::state_newer, state_read_message(e));
            throw;
        } catch (const std::exception& e) {
            filter_.reject(HostFailureKind::state_unreadable, state_read_message(e));
            throw;
        }
        install(std::move(next), {},
                state_type(stream) == StateType::kProject ? Origin::project : Origin::state);
        return kResultOk;
    } catch (...) {
        refused(was_failed);
        return kResultFalse;
    }
}
HostLoadCheck Processor::load_check(Origin origin) const noexcept {
    // A file chosen in the window is checked against the host's configuration
    // once the host has started the plug-in with it (known_layout()).
    // Restored state is refused for not suiting the host only once the host
    // has also played audio with it, and never when the host says it is
    // restoring a project: hosts start the plug-in on placeholder buses
    // before they restore a project (REAPER: stereo, then the track's six
    // channels; jrp's crossover retest, 7 November 2026). Until then a
    // restored filter that does not suit is kept, silent, and checked again
    // at the next activation.
    switch (origin) {
    case Origin::chosen_file:
        return HostLoadCheck::refuse_mismatch;
    case Origin::state:
        return played_.load() ? HostLoadCheck::refuse_mismatch : HostLoadCheck::keep_until_suited;
    case Origin::project:
        return HostLoadCheck::keep_until_suited;
    }
    return HostLoadCheck::refuse_mismatch;
}
void Processor::install(HostState next, const HostRequest& request, Origin origin) {
    // While active, a prepared replacement is published; inactive, the next
    // activation installs it. load_check() says what a filter that does not
    // suit the host means.
    const bool restore = origin != Origin::chosen_file;
    const auto check = load_check(origin);
    const auto host = host_ ? std::optional(layout()) : known_layout();
    const bool bypass = next.bypass;
    // VST3-4: the state's settings, or the defaults for a state without
    // them (an older state, or a preset from convolver-preset).
    const auto saved = next.settings.value_or(HostSettings{});
    // The inactive tail is worked out before anything changes: it needs
    // memory, and a request that runs out is rejected whole (PLUG-7).
    uint32 inactive_tail = 0;
    if (!host_ && next.configuration) {
        try {
            inactive_tail = static_cast<uint32>(configuration_tail_frames(*next.configuration));
        } catch (...) {
            filter_.reject(HostFailureKind::preparation, preparation_message({}), request);
            throw;
        }
    }
    filter_.load(
        std::move(next), host,
        [&](const HostPlayback& c) {
            if (host_)
                host_->replace(c);
        },
        request, host_.get(), check);
    // Nothing from here on can throw: the request is accepted.
    hold_fault_signal();
    tail_.store(host_ ? static_cast<uint32>(host_->tail_frames()) : inactive_tail);
    if (restore) {
        bypass_.store(bypass);
        shown_bypass_.store(bypass);
        gain_db_.store(saved.gain_db);
        mix_.store(saved.mix);
        choose_latency(saved.latency);
    }
    logged_.clear(); // A later failure is logged even if it repeats an earlier one.
    try {            // Accepted: nothing here may turn that into a rejection.
        // A restored failed load says why it plays silence.
        auto line = loaded_line(filter_, host);
        const auto& f = filter_.failure();
        const bool kept = check == HostLoadCheck::keep_until_suited;
        if (kept && host && f && f->silent && filter_.filter())
            line += " It does not suit the host's present configuration, so it is kept and plays "
                    "silence until it does: " +
                    detail::advice_line(f->message);
        if (kept)
            line += origin == Origin::project
                        ? " The host said it is restoring a project, so its channels and rate "
                          "may still change."
                        : " The host has not played audio since it started the plug-in, so "
                          "its channels and rate may still change.";
        log(line, f.has_value() || !filter_.notice().empty());
    } catch (...) { // Out of memory for the line: it is dropped.
    }
}
void Processor::refused(bool was_failed) noexcept {
    // A failed first load plays silence rather than the unfiltered input,
    // without allocating: memory may have run out (ROBUST-3), maybe before
    // the failure could be recorded (ROBUST-7).
    filter_.fail_request();
    if (host_ && filter_.failed() && !was_failed)
        host_->silence();
    if (!host_ && filter_.failed())
        tail_.store(0);
    hold_fault_signal();
    try {
        log(rejected_line(filter_, host_ != nullptr));
    } catch (...) { // Out of memory for the line: it is dropped.
    }
}
bool Processor::load_file(std::string_view chosen) {
    std::lock_guard lock(control_);
    observe_faults();
    const bool was_failed = filter_.failed();
    try { // Copying the path needs memory too: a first load that runs out fails (ROBUST-7).
        const std::string path(chosen);
        HostRequest request{HostFilterSource::file, path, 0};
        // Before the host has started the plug-in, a list chooses its entry
        // for the rate and channels the host has proposed so far, and the
        // filter is checked again at activation, as for restored state.
        const auto host = known_layout().value_or(layout());
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
            throw std::runtime_error("no reader");
        }
        FilterFileFailure why;
        // The files it was read from, so the window can reload it (VST3-11).
        auto next =
            read_filter_file(path, host, *reader, why, &request.files, &request.wider_entry);
        if (!next) {
            filter_.reject(why.kind, why.message, request, why.filter());
            throw std::runtime_error("refused");
        }
        install(std::move(*next), request, Origin::chosen_file);
    } catch (...) {
        refused(was_failed);
        return false;
    }
    // PLUG-22: the file's name and its filter's identity, as the controller
    // records them from the report for its state.
    try {
        const auto report = report_locked();
        saved_name_ = report.filter_name;
        saved_identity_ = report.filter_identity;
        kept_name_.clear();
        kept_identity_.clear();
    } catch (...) {
        saved_name_.clear(); // Out of memory: no name rather than another filter's.
        saved_identity_.clear();
        kept_name_.clear();
        kept_identity_.clear();
    }
    return true;
}
Advice Processor::notice() {
    std::lock_guard lock(control_);
    return filter_.notice();
}
std::optional<HostFailure> Processor::failure() {
    std::lock_guard lock(control_);
    return filter_.failure();
}
tresult PLUGIN_API Processor::getState(IBStream* stream) {
    bool faulted = false;
    tresult result = kResultFalse;
    {
        std::lock_guard lock(control_);
        // Hosts save often (autosave too): faults are explained here as well.
        faulted = observe_faults();
        try {
            HostState state;
            state.configuration = filter_.filter();
            state.bypass = bypass_.load();
            state.failed = filter_.failed(); // Reopening stays silent (state version 2).
            state.settings = settings();     // Version 3 only when not at the defaults.
            if (host_)
                host_->collect();
            result = write_state(stream, state) ? kResultOk : kResultFalse;
        } catch (...) {
        }
        if (result != kResultOk) // DIAG-1: the host may not say that saving failed.
            try {
                log(save_failure_message);
            } catch (...) { // Out of memory for the message.
            }
    }
    if (faulted || result != kResultOk)
        write_log();
    if (faulted)
        send_status();
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
    host.set_mix(mix_.load());
    return host.process(std::span(in).first(inputs_), std::span(out).first(outputs_), count);
}
tresult PLUGIN_API Processor::process(ProcessData& d) {
    if (d.numSamples < 0 || d.numSamples > processSetup.maxSamplesPerBlock || d.numInputs < 0 ||
        d.numInputs > 1 || d.numOutputs < 0 || d.numOutputs > 1)
        return kInvalidArgument;
    // VST3-16: the processor this call plays, which a new Latency may
    // replace on another thread meanwhile (choose_latency()).
    const auto held = hold(processing_host_);
    auto* host = held.host;
    if (holding_for_tests)
        holding_for_tests();
    if (d.numSamples && (!host || canProcessSampleSize(d.symbolicSampleSize) != kResultOk))
        return kInvalidArgument;
    if (d.numSamples) // Restored state is now checked against the host (install()).
        played_.store(true, std::memory_order_relaxed);
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
        // Latency is not read here: the controller sends it as a message.
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
