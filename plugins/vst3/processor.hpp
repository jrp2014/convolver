// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include "host_report.hpp"
#include "host_settling.hpp"
#include "playing_processor.hpp"
#include "pluginterfaces/vst/ivstparameterfunctionname.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "sound_files.hpp"
#include "vst3_controller_state.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace convolver::vst3 {
// Permanent class IDs from plugins/identities.json (PLUG-4); never change them.
inline const Steinberg::FUID processor_id(identity::vst3_processor_class_id[0],
                                          identity::vst3_processor_class_id[1],
                                          identity::vst3_processor_class_id[2],
                                          identity::vst3_processor_class_id[3]);
inline const Steinberg::FUID controller_id(identity::vst3_controller_class_id[0],
                                           identity::vst3_controller_class_id[1],
                                           identity::vst3_controller_class_id[2],
                                           identity::vst3_controller_class_id[3]);
// Parameter IDs from plugins/identities.json; never change them.
inline constexpr Steinberg::Vst::ParamID bypass_id = identity::vst3_bypass_parameter_id;
// DIAG-1: read-only, never automated or saved. Its value is a HostStatus and
// its text the instance's HostStatusLine, which the processor sends to the
// controller in a status_message whenever it may have changed.
inline constexpr Steinberg::Vst::ParamID status_id = identity::vst3_status_parameter_id;
inline constexpr const char* status_message = "ConvolverStatus";
// The Status parameter's values, in HostStatus order, and a status as the
// parameter's normalized value.
inline constexpr auto& status_names = host_status_names;
constexpr Steinberg::Vst::ParamValue normalized_status(HostStatus status) {
    return static_cast<Steinberg::Vst::ParamValue>(status) /
           static_cast<Steinberg::Vst::ParamValue>(status_names.size() - 1);
}
// The status value nearest `value`: a host that keeps normalized values as
// float hands back 0.8 (Fault, since PLUG-22 added Bypassed) as 0.80000001.
inline long status_index(Steinberg::Vst::ParamValue value) {
    return std::lround(value * static_cast<Steinberg::Vst::ParamValue>(status_names.size() - 1));
}
// VST3-3: the status_message carries the PLUG-9 report for the window
// ("report": encode_report(), which holds the Status line too, PLUG-39
// step 5; "format": the log tag), which the controller keeps a copy of and
// derives the Status text and Copy diagnostics from. Internal between the
// two classes, which always ship together: not a stable interface.
// The controller asks for a fresh report with a report_message while the
// window is open (Gain and Mix change in process(), which sends nothing),
// and has a filter file loaded with a load_message ("path": UTF-8 bytes;
// "request": a number); the processor answers kResultOk if it accepted the
// file, and sends the report either way.
// VST3-15: a host may put a proxy between the components that delivers a
// message after sendMessage() returns and answers kResultOk itself
// (IConnectionPoint), so the controller acts on the processor's answers in
// its status_message (StatusAnswer), not on sendMessage()'s result.
inline constexpr const char* report_message = "ConvolverReport";
// What the status_message that follows a request says about it: "load
// request" (the load_message's number) and "accepted" (1 or 0), or
// "latency request" (the latency_message's number, whose frames the
// processor now has). PLUG-26: an accepted .vstpreset that holds settings
// adds them, which the processor now plays: "gain" (dB) and "mix" (0 to 1)
// as floats and "latency frames", for the controller to report to the host
// as edits.
// PLUG-40: "retried" 1 when the processor read a refused configuration list
// again itself (HostFilter::take_unsettled_list()), with "accepted", so the
// controller marks the project changed as for a file the user chose.
// PLUG-48: the controller's setComponentState sends a report_message with
// "follow state", the state_digest() of the state it mirrored. The status
// that follows the processor's outcome for that state (its own restore of
// it, or the follow request when it had already refused it) adds "followed
// state" (that digest) and the settings that play, "playing gain", "playing
// mix", "playing latency" and "playing gain mode", which the controller's
// parameters take if it still mirrors that state. VST3-18: with them, the
// numbers of the last latency_message and gain_mode_message the processor
// had heard ("heard latency request", "heard gain mode request"), so the
// controller can tell a choice still on its way from one the answer
// already includes. VST3-19: the report_message also carries the
// controller's "latency requests" and "gain mode requests" when it mirrored
// the state; a processor that accepted that state and then played a choice
// sent before it plays the state's setting again, and its status says
// "latency restored" when that changed the Latency, and "restored after
// mirror" when the outcome came from its own restore.
struct StatusAnswer {
    Steinberg::int64 load_request = 0;
    bool retried = false;
    bool accepted = false;
    Steinberg::int64 latency_request = 0;
    std::optional<HostSettings> settings = std::nullopt;
};
inline constexpr const char* load_message = "ConvolverLoad";
// PLUG-22: the controller state (ControllerState, encode_controller_state
// and decode_controller_state) is in vst3_controller_state.hpp, which
// convolver-preset shares (PLUG-31).
// How the plug-in reads the sound files a chosen filter file names: macOS's
// AudioToolbox on macOS, as the Audio Unit does; libsndfile on Windows and
// Linux, as the command-line tool and the LV2 plug-in do (VST3-12).
// VST3-16, for tests: called before the processor for a new Latency is
// built while active; throwing from it makes that preparation fail.
inline void (*latency_preparation_for_tests)() = nullptr;
// VST3-16, for tests: process() calls it once it holds its processor.
inline void (*holding_for_tests)() = nullptr;
// DIAG-1: the controller asks the processor to collect processing faults
// when the host shows it the Fault value the audio thread wrote (an output
// parameter change), so the log explains and the status text follows.
inline constexpr const char* faults_message = "ConvolverFaults";
// VST3-4: PLUG-10's settings as parameters, with the Audio Unit's IDs
// (docs/develop/plugin-contract.md#settings). Gain in dB (min_gain_db to
// max_gain_db, linear in decibels) and Mix in percent are automatable; the
// processor reads them from process()'s parameter changes, as Bypass.
// Latency is a list of host_latencies, not automatable: the controller
// passes a new choice to the processor in a latency_message ("frames", an
// integer; "request", a number) and, once the processor's answer says it
// has it (StatusAnswer), asks the host to restart the component
// (kLatencyChanged). While active, the processor builds a processor with the
// new Latency and plays it before it answers (VST3-16: REAPER reads
// getLatencySamples() inside restartComponent and does not restart the
// component), so the latency the host reads is the one that plays. All three are saved
// in the component state (host state version 3, defaults included: PLUG-35).
// IDs from plugins/identities.json; never change them.
inline constexpr Steinberg::Vst::ParamID gain_id = identity::vst3_gain_parameter_id;
inline constexpr Steinberg::Vst::ParamID mix_id = identity::vst3_mix_parameter_id;
inline constexpr Steinberg::Vst::ParamID latency_id = identity::vst3_latency_parameter_id;
inline constexpr const char* latency_message = "ConvolverLatency";
// PLUG-10: Gain mode (Set, Estimate or No clipping) is a list, not
// automatable, as Latency: Estimate may filter noise for a long filter
// before it can play, which the audio thread must not do. The controller
// passes a new choice to the processor in a gain_mode_message ("mode", the
// GainMode as an integer, and "request", its number, VST3-18); the processor finds the estimate if
// it needs it, publishes the filter with it (the crossfade covers the change) and then plays the
// new mode. Saved in the component state (host state version 4 when not Set).
inline constexpr Steinberg::Vst::ParamID gain_mode_id = identity::vst3_gain_mode_parameter_id;
inline constexpr const char* gain_mode_message = "ConvolverGainMode";
// PLUG-22: the controller's bypass_message ("bypass": 0 or 1) when the host
// or the window changes Bypass, so the Status reads Bypassed at once rather
// than when the audio thread next sees the change.
inline constexpr const char* bypass_message = "ConvolverBypass";
// PLUG-22: the controller's name_message ("name" and "identity", binary
// UTF-8, or neither for no name) after it reads its state, so the processor's report names saved
// state of that filter (Processor::report_locked).
inline constexpr const char* name_message = "ConvolverName";
inline constexpr std::size_t default_latency_index = 0;
static_assert(host_latencies[default_latency_index] == default_host_latency);
// The settings' normalized values (0 to 1), as the host and the controller
// hold them, and back.
double gain_db(Steinberg::Vst::ParamValue) noexcept;
Steinberg::Vst::ParamValue normalized_gain(double gain_db) noexcept;
std::size_t latency_frames(Steinberg::Vst::ParamValue) noexcept;
Steinberg::Vst::ParamValue normalized_latency(std::size_t frames) noexcept;
GainMode gain_mode(Steinberg::Vst::ParamValue) noexcept;
Steinberg::Vst::ParamValue normalized_gain_mode(GainMode) noexcept;
// One input and one output bus, each with 1 to 8 channels (counts may differ).
inline constexpr int min_bus_channels = identity::min_channels;
inline constexpr int max_bus_channels = identity::max_channels;
// The WAV channel-mask bits of a VST3 arrangement: bits 0 to 17 have the same
// meaning in both, and VST3 orders channels by bit. 0 (no named speakers) for
// any arrangement using other speakers, kMono (kSpeakerM) included.
std::uint32_t wav_speakers(Steinberg::Vst::SpeakerArrangement) noexcept;
HostState read_state(Steinberg::IBStream*);
// PLUG-48: a digest of restored state as decoded (never 0), which the
// processor and the controller compute alike for the same stream, so the
// controller knows which state a refusal was of.
std::uint64_t state_digest(const HostState&);
bool write_state(Steinberg::IBStream*, const HostState&);
class Processor final : public Steinberg::Vst::AudioEffect {
  public:
    Processor();
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement*,
                                                     Steinberg::int32,
                                                     Steinberg::Vst::SpeakerArrangement*,
                                                     Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup&) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData&) override;
    // The latency the processor runs at while active, otherwise the chosen
    // Latency (VST3-4). A new choice plays at once (VST3-16), so a host that
    // reads this when asked to restart (kLatencyChanged) gets the new value,
    // as does one that restarts the component first, as the SDK describes.
    Steinberg::uint32 PLUGIN_API getLatencySamples() override {
        const auto running = active_latency_.load();
        return static_cast<Steinberg::uint32>(running != inactive ? running : latency_.load());
    }
    // How long the output keeps sounding after the input stops, measured at the
    // plug-in's output as Steinberg's tail test does: the filter's tail plus
    // the latency (VST3-8). Bypass and the empty instance's identity play the
    // input delayed by the latency, so even they report it.
    Steinberg::uint32 PLUGIN_API getTailSamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
    // The controller's faults_message, latency_message, gain_mode_message,
    // report_message and load_message.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
    // The filter's advisory notice (HostFilter::notice), for example output
    // speakers that differ from the host's arrangement; logged when it changes.
    Advice notice();
    // Why the last load failed or the instance plays silence (PLUG-8), as
    // HostFilter::failure() keeps it; logged in full, summarised by status().
    std::optional<HostFailure> failure();
    // What the Status parameter shows (DIAG-1).
    HostStatusLine status();
    // VST3-4: the settings the parameters hold (the latency as chosen).
    HostSettings settings() const noexcept;
    HostState saved_state() const;
    bool unchanged_state(const HostState&) const noexcept;

  private:
    // `error` is null when something other than a std::exception was thrown.
    void record_activation_failure(const std::exception* error) noexcept;
    // Queues a line for the Convolver log under control_, skipping a repeat
    // of the line this instance queued last, as hosts restart plug-ins often;
    // write_log() writes the queue (host_log) after control_ is released, so
    // file access never holds up the host's other control calls.
    void log(const std::string&, bool problem = true) noexcept;
    void write_log() noexcept;
    // DIAG-1, under control_: takes the processor's fault counts into
    // filter_ and queues their explanation for the log. True if there were
    // new ones, so the status has changed.
    bool observe_faults() noexcept;
    // Under control_, after every load, rejection and activation: the audio
    // thread must not show Fault over a load failure (failure_shown()).
    void hold_fault_signal() const noexcept;
    // Audio thread: writes Fault to the Status parameter for new faults.
    void signal_faults(HostProcessor*, Steinberg::Vst::ProcessData&) noexcept;
    Steinberg::tresult arrange(Steinberg::Vst::SpeakerArrangement*, Steinberg::int32,
                               Steinberg::Vst::SpeakerArrangement*, Steinberg::int32);
    // Sends status() to the controller, with the answer to its request if
    // any; outside control_, as the controller may call back into the host.
    void send_status(const StatusAnswer& answer = StatusAnswer{}) noexcept;
    HostStatusLine status_locked() const;
    Steinberg::tresult load_state(Steinberg::IBStream*);
    Steinberg::tresult activate(Steinberg::TBool);
    HostLayout layout() const;
    std::optional<HostLayout> known_layout() const;
    template <class T>
    bool segment(HostProcessor&, Steinberg::Vst::ProcessData&, Steinberg::int32,
                 Steinberg::int32) noexcept;
    void mark_silence(Steinberg::Vst::ProcessData&) noexcept;
    // VST3-4, under control_: a Latency chosen in the controller or restored.
    // While active, a processor with it replaces the running one at once
    // (VST3-16); if it cannot be built, the running one keeps playing and the
    // log says why. Never throws.
    void choose_latency(std::size_t frames) noexcept;
    // PLUG-10, under control_: a Gain mode chosen in the controller or
    // restored. Estimate finds the filter's estimate first if it has none
    // (HostFilter::estimate) and, while active, gives it to the playing filter
    // in place (HostProcessor::update_gains).
    // If finding it fails, Estimate plays the no-clipping figure and the log
    // says why. Never throws.
    void choose_gain_mode(GainMode) noexcept;
    // The newest processor (null while inactive), which control calls use
    // under control_; process() may still play the one it replaced until its
    // next block (playing_).
    HostProcessor* host() const noexcept {
        return playing_.newest();
    }
    // PLUG-39 step 6: under control_, after every change to the newest
    // processor or the accepted filter, the one place that writes the latency
    // and tail the host reads, from host() or, with none, filter_. Before the
    // host is notified, so it reads at once what the next block uses.
    void publish_timing() noexcept;
    // control_, held for this object's lifetime. Taking it first frees the
    // replaced processors neither process() nor setProcessing() still
    // holds, so a processor a new Latency replaced lives until the first
    // control call after its reader lets go. Only deactivation waits for a
    // callback the host is still running (PlayingProcessor::clear()).
    struct Locked {
        explicit Locked(Processor& p) : lock(p.control_) {
            p.playing_.reclaim();
        }
        std::lock_guard<std::mutex> lock;
    };
    // What a filter that does not suit the host means for a request of this
    // kind (HostLoadCheck), under control_: a file chosen in the window,
    // state the host restores (setState), or state the host says
    // (IStreamAttributes) is a project being restored.
    HostLoadCheck load_check(HostRequestKind) const noexcept;
    // VST3-3: the common step of setState and a chosen file, under control_.
    // Loads `next` as `request.kind` and applies what HostFilter::load()
    // returns (HostChange): restored state's bypass and settings, or a
    // chosen .vstpreset's Gain, Mix and Latency when it holds them (PLUG-26).
    // Returns the settings it applied. Throws if it is refused, which
    // HostFilter has recorded.
    std::optional<HostSettings> install(HostState next, const HostRequest&);
    // Under control_, after a refused request: a failed first load plays
    // silence, and the log says why.
    void refused() noexcept;
    // VST3-3, a load_message: reads the filter file at `path` (UTF-8) for
    // the host's rate and channels, as convolver-preset does, and loads it
    // as a state restore would; PLUG-26: or a Convolver .vstpreset
    // (read_chosen_file()). True if it was accepted. `settings` receives the
    // settings an accepted preset applied, or none.
    bool load_file(std::string_view path, std::optional<HostSettings>& settings);
    // load_file()'s reading and install(): false after recording why the
    // file was refused; throws when install() refuses it, or when memory runs
    // out.
    bool install_chosen(const std::string& path, std::optional<HostSettings>& settings);
    // VST3-3, under control_: the window's report. The report_message looks
    // for unexplained faults at most every report_fault_interval, so an open
    // window does not log a line twice a second, as in the Audio Unit.
    HostReport report_locked() const;
    // PLUG-33: whether the window can choose filter files here (a reader
    // and a window on this system), for the report and the Status text.
    bool chooses_files() const noexcept;
    static constexpr std::chrono::seconds report_fault_interval{10};
    std::chrono::steady_clock::time_point report_faults_at_{};
    const detail::SoundFiles* files_ = nullptr; // The reader, or none.
    std::mutex control_;
    HostFilter filter_{max_bus_channels}; // The shared adapter contract's control side.
    // PLUG-39 step 3 (VST3-16): the processor the audio side plays. The
    // control side offers a replacement and returns; process() and
    // setProcessing(), which hosts may call on different threads, each
    // adopt the newest offer at their next call, holding it in their own
    // mark for the length of that call; a later control call (Locked) frees
    // a replaced one that neither holds.
    enum Reader : std::size_t { processing_reader, starting_reader, readers };
    PlayingProcessor<HostProcessor, readers> playing_;
    std::atomic<bool> bypass_{false};
    // PLUG-22: Bypass as the Status and the report show it: the audio
    // thread's value, or the controller's when it told the processor first
    // (bypass_message).
    std::atomic<bool> shown_bypass_{false};
    // VST3-4: Gain and Mix as process() last received them (or a state
    // restored), and the chosen Latency in frames.
    std::atomic<double> gain_db_{0}, mix_{1};
    std::atomic<std::size_t> latency_{default_host_latency};
    // PLUG-10: the Gain mode the audio thread plays (choose_gain_mode()).
    std::atomic<GainMode> gain_mode_{GainMode::set};
    static_assert(std::atomic<GainMode>::is_always_lock_free);
    // What getLatencySamples() and getTailSamples() read without control_,
    // written only by publish_timing(): the newest processor's latency,
    // `inactive` with none (0 is a latency since PERF-2), and its tail, or
    // with none the accepted filter's.
    static constexpr std::size_t inactive = std::numeric_limits<std::size_t>::max();
    std::atomic<std::size_t> active_latency_{inactive};
    // With the rate it counts at when it is not the host's (PLUG-46): the
    // accepted filter's while no processor plays (its own tail, in its own
    // frames); 0 while one plays. getTailSamples() converts it to the rate
    // of the host's accepted setupProcessing (host_rate_, 0 before one).
    std::atomic<HostTail> tail_{};
    static_assert(std::atomic<HostTail>::is_always_lock_free);
    std::atomic<double> host_rate_{0};
    int inputs_ = 2, outputs_ = 2;
    Steinberg::Vst::SpeakerArrangement input_arrangement_ = Steinberg::Vst::SpeakerArr::kStereo,
                                       output_arrangement_ = Steinberg::Vst::SpeakerArr::kStereo;
    bool active_ = false;
    // PLUG-39 step 2: when the host has settled its rate and buses. A start
    // binds them while they stay the same (setupProcessing alone is not
    // binding), and restored state is refused for not suiting them only once
    // process() has played audio with them: until then the host may still be
    // changing its buses (REAPER starts the plug-in on stereo buses before it
    // restores a project and gives it the track's). Under control_, except
    // played(), which process() calls.
    HostSettling settling_{HostBinding::activation};
    // PLUG-48, under the lock: state_digest() of the newest state the host
    // restored if it was refused, 0 otherwise (or once a load is accepted);
    // the state the controller says it mirrored last ("follow state"); and
    // whether the next status says what plays, after an outcome for that
    // state, so the controller's parameters show what plays.
    std::uint64_t refused_state_ = 0;
    std::uint64_t mirrored_state_ = 0;
    bool answer_mirrored_ = false;
    // VST3-18, under the lock: the "request" of the last latency_message and
    // gain_mode_message heard, which a status after a mirrored state carries.
    Steinberg::int64 heard_latency_request_ = 0, heard_gain_mode_request_ = 0;
    // VST3-19, under the lock: the digest of the state the last restore
    // accepted (0 once a report request named it, or after a refusal), the
    // Latency and Gain mode it set, and the request numbers heard then; and
    // what the next status after a mirrored state says besides.
    std::uint64_t accepted_state_ = 0;
    std::size_t accepted_latency_ = default_host_latency;
    GainMode accepted_gain_mode_ = GainMode::set;
    Steinberg::int64 latency_heard_at_accept_ = 0, gain_mode_heard_at_accept_ = 0;
    bool restored_after_mirror_ = false, latency_restored_ = false;
    bool layout_refusal_logged_ = false; // Refused layouts are logged once per instance.
    std::string logged_;                 // The last line log() queued.
    std::vector<std::pair<std::string, bool>> unlogged_; // Lines and whether each is a problem.
    const std::string tag_; // "VST3 #n": this instance in the Convolver log.
};
// The edit controller. It also tells hosts which parameter is the wet/dry
// mix (IParameterFunctionName, kDryWetMix), as the SDK's examples do.
class Controller final : public Steinberg::Vst::EditController,
                         public Steinberg::Vst::IParameterFunctionName {
  public:
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    // Bypass and the settings from the processor's state (defaults when the
    // state has none); a Latency that changed asks the host to restart.
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream*) override;
    // Bypass reads "Off" and "On"; accept them back, as hosts that let users
    // type a value need (VST3-8), with 0 and 1, and refuse other text.
    Steinberg::tresult PLUGIN_API getParamValueByString(Steinberg::Vst::ParamID,
                                                        Steinberg::Vst::TChar*,
                                                        Steinberg::Vst::ParamValue&) override;
    // VST3-11: the window (editor.hpp) for ViewType::kEditor; none on
    // systems without one yet.
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) override;
    // PLUG-22: the controller state holds the name of the filter file last
    // chosen in the window, with its extension, and that filter's identity
    // (HostReport::filter_identity), so the window names the filter after
    // the project is reopened, but only while that filter plays (a preset
    // or an undo may bring another). Never the file's folder. A preset made
    // by convolver-preset holds the same for the file it was made from
    // (PLUG-31). The format is in vst3_controller_state.hpp. setState sends
    // both to the processor (name_message), which names its report and the
    // report's text with them. Before PLUG-22 hosts saved an empty
    // controller state, so an empty stream, a shorter one or another
    // version reads as no name, never as a failure (VST3-8).
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;

    // The processor's status_message: the Status parameter's value and text.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
    // A Status value from the host: the processor's audio thread writes
    // Fault as an output parameter change, and the host passes it on here.
    // The controller then asks the processor (faults_message) for the text.
    // A new Latency, from the host or the window, goes to the processor
    // (latency_message), and the host is asked to restart the component.
    Steinberg::tresult PLUGIN_API setParamNormalized(Steinberg::Vst::ParamID,
                                                     Steinberg::Vst::ParamValue) override;
    // The Status parameter's current value reads as the processor's line; other
    // values (a host asking how a value would read) as their status name.
    Steinberg::tresult PLUGIN_API getParamStringByValue(Steinberg::Vst::ParamID,
                                                        Steinberg::Vst::ParamValue,
                                                        Steinberg::Vst::String128) override;
    std::string status_text() {
        std::lock_guard lock(status_lock_);
        return report_ ? report_->status.text : std::string();
    }
    // VST3-3: the window's report, the copy the processor sent last (none
    // before it has sent one), after asking it for a fresh one.
    std::optional<HostReport> report();
    // Copy diagnostics: the report's text and this instance's recent log
    // lines. Reads the log file: on the window's thread only.
    std::string diagnostics();
    // Has the processor load the filter file at `path` (UTF-8); false if it
    // refused it. An accepted file changes the component state without a
    // parameter change, so the host is told the project changed
    // (IComponentHandler2::setDirty) when the processor's answer says so;
    // a host without setDirty is shown an edit of Latency that keeps its
    // value instead (VST3-17, notify).
    // A host that delivers the message later (VST3-15) brings the answer
    // after this returns true: the report then says what plays.
    bool load_file(const std::string& path);
    // IParameterFunctionName: Mix is the kDryWetMix parameter.
    Steinberg::tresult PLUGIN_API getParameterIDFromFunctionName(Steinberg::Vst::UnitID,
                                                                 Steinberg::FIDString,
                                                                 Steinberg::Vst::ParamID&) override;

    OBJ_METHODS(Controller, EditController)
    DEFINE_INTERFACES
    DEF_INTERFACE(IParameterFunctionName)
    END_DEFINE_INTERFACES(EditController)
    REFCOUNT_METHODS(EditController)

  private:
    // Sends `frames` to the processor; the host is asked to restart when the
    // processor answers that it has them (notify), since the host reads the
    // latency during the restart.
    void latency_chosen(std::size_t frames);
    // VST3-18: Gain and Mix as bits of edited_since_restore_ (0 for any
    // other parameter), and recording that the host or the window set one.
    static unsigned setting_bit(Steinberg::Vst::ParamID) noexcept;
    void edited_since_restore(Steinberg::Vst::ParamID);
    // The host may restore state (and so send the status) on another thread
    // than the one reading parameter text.
    std::mutex status_lock_;
    // VST3-3, under status_lock_: the report the processor sent last, from
    // which the Status text and Copy diagnostics come (PLUG-39 step 5), and
    // its log tag.
    std::optional<HostReport> report_;
    std::string format_;
    // VST3-15, under status_lock_: the last load_message's number, the last
    // one answered and whether it was accepted; the last latency_message's
    // number, and whether its answer is still to come.
    Steinberg::int64 load_requests_ = 0, load_answered_ = 0;
    bool load_accepted_ = false;
    Steinberg::int64 latency_requests_ = 0;
    bool latency_waiting_ = false;
    // VST3-18, under status_lock_: the last gain_mode_message's number.
    Steinberg::int64 gain_mode_requests_ = 0;
    // PLUG-22, under status_lock_: the name and filter identity the last
    // setState brought. getState saves the name the last report gives the
    // filter that plays ("filter saved name"), and this pair only when that
    // filter has none.
    std::string name_, identity_;
    // PLUG-48, under status_lock_: state_digest() of the state the last
    // setComponentState mirrored; a status that says what plays after the
    // processor's outcome for that state sets the parameters (notify). 0: none.
    std::uint64_t follow_state_ = 0;
    // VST3-18, under status_lock_: Gain and Mix (setting_bit()) as the host
    // or the window set them since that setComponentState, changed or not. The status
    // that says what plays leaves them as they are: they reach the processor
    // in process(), whose order against the status cannot be told, so the
    // host's latest value counts. Latency and Gain mode go as messages, in
    // order, and the status says which it had heard.
    unsigned edited_since_restore_ = 0;
    // VST3-18, under status_lock_: the last Latency and Gain mode request
    // numbers at that setComponentState; only later choices can keep theirs.
    Steinberg::int64 latency_at_restore_ = 0, gain_mode_at_restore_ = 0;
    // VST3-19, under status_lock_: that setComponentState asked the host to
    // read a changed latency, perhaps before the processor restored the
    // state (the controller first); the status after the processor's own
    // restore asks again.
    bool restart_at_restore_ = false;
};
} // namespace convolver::vst3
