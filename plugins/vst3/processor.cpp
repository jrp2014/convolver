// SPDX-License-Identifier: GPL-2.0-or-later
#include "processor.hpp"
#include "bytes.hpp"
#include "convolver_sdk.hpp"
#include "filter_file.hpp"
#include "host_log.hpp"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
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
        else if (!std::isfinite(setup.sampleRate) || setup.sampleRate < 1 ||
                 setup.sampleRate > max_sample_rate ||
                 std::floor(setup.sampleRate) != setup.sampleRate)
            why = "the sample rate must be a whole number of hertz from 1 to " +
                  std::to_string(max_sample_rate) + " Hz.";
        else if (setup.maxSamplesPerBlock < 1 ||
                 static_cast<std::size_t>(setup.maxSamplesPerBlock) > max_block_frames)
            why =
                "the largest block must be 1 to " + std::to_string(max_block_frames) + " samples.";
        else if (canProcessSampleSize(setup.symbolicSampleSize) != kResultOk)
            why = "samples must be 32-bit or 64-bit floating point.";
        if (!why.empty()) {
            try {
                char rate[32];
                std::snprintf(rate, sizeof rate, "%.6g", setup.sampleRate);
                log(std::string("Refused the host's processing settings (") + rate +
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
    return {static_cast<int>(processSetup.sampleRate), static_cast<std::size_t>(inputs_),
            static_cast<std::size_t>(outputs_), wav_speakers(input_arrangement_),
            wav_speakers(output_arrangement_)};
}
// The host's configuration is known once the plug-in has been activated with
// it, and stays known while the rate and buses are unchanged. setupProcessing
// is not binding on its own: JUCE-based hosts (pluginval among them) call it
// with placeholder values before restoring a project's state at another rate.
// Until then, restored state is kept and checked at activation (the shared
// adapter contract's "state that arrives before the host is configured").
std::optional<HostLayout> Processor::known_layout() const {
    const auto now = layout();
    if (!activated_ || activated_->sample_rate != now.sample_rate ||
        activated_->inputs != now.inputs || activated_->outputs != now.outputs)
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
            auto next = std::make_unique<HostProcessor>(filter_.activate(layout()),
                                                        processSetup.maxSamplesPerBlock, chosen);
            tail_.store(static_cast<uint32>(next->tail_frames()));
            next->set_bypass(bypass_.load());
            next->reset();
            host_ = std::move(next);
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
            host_.reset();
            active_latency_.store(0);
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
HostStatusLine Processor::status_locked() const {
    return host_status(filter_, host_ ? std::optional(layout()) : known_layout(),
                       host_log_written(), settings());
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
    facts.log_written = host_log_written();
    facts.chooses_files = files_ != nullptr || test_file_reader != nullptr;
    if (files_)
        facts.file_reader = "macOS AudioToolbox";
    return host_report(filter_, facts);
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
        bool accepted = false;
        try {
            accepted = load_file(std::string(static_cast<const char*>(data), size));
        } catch (...) { // Out of memory for the path: nothing changed.
        }
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
// The host is asked to restart the component (the controller's
// kLatencyChanged), which deactivates and activates it: the new processor is
// built then, off the audio thread, as for any activation. Until a host
// does, the running processor keeps its block size.
void Processor::choose_latency(std::size_t frames) {
    if (latency_.exchange(frames) == frames)
        return;
    try {
        log("Latency set to " + std::to_string(frames) + " frames" +
                (host_ && host_->latency_frames() != frames
                     ? "; it is used when the host next starts the plug-in, which it is asked "
                       "to do now."
                     : "."),
            false);
    } catch (...) { // Out of memory for the line: the choice stands.
    }
}
tresult PLUGIN_API Processor::setProcessing(TBool state) {
    // Not a place to explain faults: hosts may call this on the audio
    // thread, where nothing may lock, allocate or write the log.
    if (state && host_) {
        host_->set_bypass(bypass_.load());
        host_->reset();
    }
    return kResultOk;
}
tresult PLUGIN_API Processor::setState(IBStream* stream) {
    const auto result = load_state(stream);
    write_log();
    send_status();
    return result;
}
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
        install(std::move(next), {}, true);
        return kResultOk;
    } catch (...) {
        refused(was_failed);
        return kResultFalse;
    }
}
void Processor::install(HostState next, const HostRequest& request, bool restore) {
    // While active, the host's configuration is known and a prepared
    // replacement is published; inactive, the next activation installs it.
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
        [&](const Configuration& c) {
            if (host_)
                host_->replace(c);
        },
        request, host_.get());
    // Nothing from here on can throw: the request is accepted.
    hold_fault_signal();
    tail_.store(host_ ? static_cast<uint32>(host_->tail_frames()) : inactive_tail);
    if (restore) {
        bypass_.store(bypass);
        gain_db_.store(saved.gain_db);
        mix_.store(saved.mix);
        choose_latency(saved.latency);
    }
    logged_.clear(); // A later failure is logged even if it repeats an earlier one.
    try {            // Accepted: nothing here may turn that into a rejection.
        // A restored failed load says why it plays silence.
        log(loaded_line(filter_, host.has_value()),
            filter_.failure().has_value() || !filter_.notice().empty());
    } catch (...) { // Out of memory for the line: it is dropped.
    }
}
void Processor::refused(bool was_failed) noexcept {
    // A failed first load plays silence rather than the unfiltered input,
    // without allocating: memory may have run out (ROBUST-3).
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
bool Processor::load_file(const std::string& path) {
    std::lock_guard lock(control_);
    observe_faults();
    const bool was_failed = filter_.failed();
    HostRequest request{HostFilterSource::file, path, 0};
    try {
        // Before the host has started the plug-in, a list chooses its entry
        // for the rate and channels the host has proposed so far, and the
        // filter is checked again at activation, as for restored state.
        const auto host = known_layout().value_or(layout());
        const auto* reader = files_ ? files_ : test_file_reader;
        if (!reader) {
            filter_.reject(HostFailureKind::filter_file,
                           detail::advice_text({"this build of Convolver cannot read filter "
                                                "files in the plug-in, so " +
                                                    path + " was not loaded",
                                                {},
                                                "Make a preset from it with convolver-preset, "
                                                "and load the preset instead.",
                                                {}}),
                           request);
            throw std::runtime_error("no reader");
        }
        FilterFileFailure why;
        // The files it was read from, so the window can reload it (VST3-11).
        auto next = read_filter_file(path, host, *reader, why, &request.files);
        if (!next) {
            filter_.reject(why.kind, why.message, request, why.filter());
            throw std::runtime_error("refused");
        }
        install(std::move(*next), request, false);
        return true;
    } catch (...) {
        refused(was_failed);
        return false;
    }
}
std::string Processor::notice() {
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
template <class T> bool Processor::segment(ProcessData& d, int32 start, int32 count) noexcept {
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
    host_->set_bypass(bypass_.load());
    host_->set_gain_db(gain_db_.load());
    host_->set_mix(mix_.load());
    return host_->process(std::span(in).first(inputs_), std::span(out).first(outputs_), count);
}
tresult PLUGIN_API Processor::process(ProcessData& d) {
    if (d.numSamples < 0 || d.numSamples > processSetup.maxSamplesPerBlock || d.numInputs < 0 ||
        d.numInputs > 1 || d.numOutputs < 0 || d.numOutputs > 1)
        return kInvalidArgument;
    if (d.numSamples && (!host_ || canProcessSampleSize(d.symbolicSampleSize) != kResultOk))
        return kInvalidArgument;
    if (d.numOutputs && d.outputs)
        d.outputs[0].silenceFlags = 0; // Set again below once the output is known.
    auto render = [&](int32 first, int32 count) {
        if (!count)
            return true;
        return d.symbolicSampleSize == kSample32 ? segment<float>(d, first, count)
                                                 : segment<double>(d, first, count);
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
                    return signal_faults(d), kInvalidArgument;
                position = offset;
                bypass_.store(value >= .5);
            }
        }
    }
    const bool rendered = render(position, d.numSamples - position);
    if (rendered)
        mark_silence(d);
    signal_faults(d);
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
void Processor::signal_faults(ProcessData& d) noexcept {
    if (!host_ || !d.outputParameterChanges || !host_->fault_to_signal())
        return;
    int32 index = 0, point = 0;
    if (auto* queue = d.outputParameterChanges->addParameterData(status_id, index);
        queue && queue->addPoint(0, normalized_status(HostStatus::fault), point) == kResultOk)
        host_->fault_signalled();
}
} // namespace convolver::vst3
