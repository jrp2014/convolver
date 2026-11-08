// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include "host_report.hpp"
#include "pluginterfaces/vst/ivstparameterfunctionname.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/vsteditcontroller.h"
#include "sound_files.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
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
// VST3-3: the status_message also carries the PLUG-9 report for the window
// ("report": report_fields() as UTF-8 name and value pairs, each ended by a
// zero byte; "report text": report_text(); "format": the log tag), which
// the controller keeps a copy of. Internal between the two classes, which
// always ship together: not a stable interface.
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
// processor now has).
struct StatusAnswer {
    Steinberg::int64 load_request = 0;
    bool accepted = false;
    Steinberg::int64 latency_request = 0;
};
inline constexpr const char* load_message = "ConvolverLoad";
// The report's name and value pairs as the messages carry them, and back.
std::string encode_report(const std::vector<std::pair<std::string, std::string>>&);
// PLUG-22: the controller state (Controller::setState): the filter file's
// name and its filter's identity, and back. Decoding anything else (empty,
// short, another version, a name longer than max_filter_name_bytes) gives
// two empty strings, as does an empty name or identity.
struct ControllerState {
    std::string name, identity;
};
std::string encode_controller_state(const ControllerState&);
ControllerState decode_controller_state(std::string_view);
std::vector<std::pair<std::string, std::string>> decode_report(std::string_view);
// How the plug-in reads the sound files a chosen filter file names: macOS's
// AudioToolbox on macOS, as the Audio Unit does; none elsewhere until the
// Windows and Linux windows (VST3-12, VST3-13), so a load is refused there.
// For the tests only: a reader to use where the build has none. Set it only
// while no processor is loading.
inline const detail::SoundFiles* test_file_reader = nullptr;
// VST3-16, for tests: called before the processor for a new Latency is
// built while active; throwing from it makes that preparation fail.
inline void (*latency_preparation_for_tests)() = nullptr;
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
// in the component state (host state version 3 when not at the defaults).
// IDs from plugins/identities.json; never change them.
inline constexpr Steinberg::Vst::ParamID gain_id = identity::vst3_gain_parameter_id;
inline constexpr Steinberg::Vst::ParamID mix_id = identity::vst3_mix_parameter_id;
inline constexpr Steinberg::Vst::ParamID latency_id = identity::vst3_latency_parameter_id;
inline constexpr const char* latency_message = "ConvolverLatency";
// PLUG-22: the controller's bypass_message ("bypass": 0 or 1) when the host
// or the window changes Bypass, so the Status reads Bypassed at once rather
// than when the audio thread next sees the change.
inline constexpr const char* bypass_message = "ConvolverBypass";
// PLUG-22: the controller's name_message ("name" and "identity", binary
// UTF-8, or neither for no name) after it reads its state, so the processor's report names saved
// state of that filter (Processor::report_locked).
inline constexpr const char* name_message = "ConvolverName";
inline constexpr std::size_t default_latency_index = 2;
static_assert(host_latencies[default_latency_index] == default_host_latency);
// The settings' normalized values (0 to 1), as the host and the controller
// hold them, and back.
double gain_db(Steinberg::Vst::ParamValue) noexcept;
Steinberg::Vst::ParamValue normalized_gain(double gain_db) noexcept;
std::size_t latency_frames(Steinberg::Vst::ParamValue) noexcept;
Steinberg::Vst::ParamValue normalized_latency(std::size_t frames) noexcept;
// One input and one output bus, each with 1 to 8 channels (counts may differ).
inline constexpr int min_bus_channels = identity::min_channels;
inline constexpr int max_bus_channels = identity::max_channels;
// The WAV channel-mask bits of a VST3 arrangement: bits 0 to 17 have the same
// meaning in both, and VST3 orders channels by bit. 0 (no named speakers) for
// any arrangement using other speakers, kMono (kSpeakerM) included.
std::uint32_t wav_speakers(Steinberg::Vst::SpeakerArrangement) noexcept;
HostState read_state(Steinberg::IBStream*);
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
        return static_cast<Steinberg::uint32>(running ? running : latency_.load());
    }
    // How long the output keeps sounding after the input stops, measured at the
    // plug-in's output as Steinberg's tail test does: the filter's tail plus
    // the latency (VST3-8). Bypass and the empty instance's identity play the
    // input delayed by the latency, so even they report it.
    Steinberg::uint32 PLUGIN_API getTailSamples() override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
    // The controller's faults_message, latency_message, report_message and
    // load_message.
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

  private:
    void record_activation_failure(const char* why) noexcept;
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
    // VST3-16: the processor the audio thread plays. Under control_,
    // install_processor() makes `next` the one played and returns the
    // previous one; release_processor() then waits until process() or
    // setProcessing() is no longer using it, which takes at most one block,
    // before it is freed. Audio thread: hold() marks the processor it uses
    // for as long as the returned guard lives.
    std::unique_ptr<HostProcessor> install_processor(std::unique_ptr<HostProcessor> next) noexcept;
    void release_processor(std::unique_ptr<HostProcessor> previous) noexcept;
    struct Held {
        Held(std::atomic<HostProcessor*>& marked, HostProcessor* held) noexcept
            : mark(marked), host(held) {}
        std::atomic<HostProcessor*>& mark;
        HostProcessor* host;
        Held(const Held&) = delete;
        Held& operator=(const Held&) = delete;
        ~Held() {
            mark.store(nullptr);
        }
    };
    Held hold() noexcept;
    // Where a load comes from: a file chosen in the window, state the host
    // restores (setState), or state the host says (IStreamAttributes) is a
    // project being restored.
    enum class Origin { chosen_file, state, project };
    // What a filter that does not suit the host means for a load from
    // `origin` (HostLoadCheck), under control_.
    HostLoadCheck load_check(Origin) const noexcept;
    // VST3-3: the common step of setState and a chosen file, under control_.
    // Loads `next`; restored state's bypass and settings apply too, while a
    // chosen file keeps them. Throws if it is refused, which HostFilter has
    // recorded.
    void install(HostState next, const HostRequest&, Origin);
    // Under control_, after a refused request: a failed first load plays
    // silence, and the log says why.
    void refused(bool was_failed) noexcept;
    // VST3-3, a load_message: reads the filter file at `path` (UTF-8) for
    // the host's rate and channels, as convolver-preset does, and loads it
    // as a state restore would. True if it was accepted.
    bool load_file(std::string_view path);
    // VST3-3, under control_: the window's report. The report_message looks
    // for unexplained faults at most every report_fault_interval, so an open
    // window does not log a line twice a second, as in the Audio Unit.
    HostReport report_locked() const;
    static constexpr std::chrono::seconds report_fault_interval{10};
    std::chrono::steady_clock::time_point report_faults_at_{};
    const detail::SoundFiles* files_ = nullptr; // The reader, or none.
    std::mutex control_;
    HostFilter filter_{max_bus_channels}; // The shared adapter contract's control side.
    std::unique_ptr<HostProcessor> host_; // Under control_; played_host_ is the same.
    // VST3-16: host_ as the audio thread reads it, and the processor
    // process() or setProcessing() is using (hold()).
    std::atomic<HostProcessor*> played_host_{nullptr}, held_host_{nullptr};
    std::atomic<bool> bypass_{false};
    // PLUG-22: Bypass as the Status and the report show it: the audio
    // thread's value, or the controller's when it told the processor first
    // (bypass_message).
    std::atomic<bool> shown_bypass_{false};
    // PLUG-22, under control_: the name the report gives saved state whose
    // filter identity is saved_identity_: from the controller state
    // (name_message), or the file last accepted, as the controller records it.
    std::string saved_name_, saved_identity_;
    // VST3-4: Gain and Mix as process() last received them (or a state
    // restored), and the chosen Latency in frames.
    std::atomic<double> gain_db_{0}, mix_{1};
    std::atomic<std::size_t> latency_{default_host_latency};
    std::atomic<std::size_t> active_latency_{0}; // The running processor's; 0 while inactive.
    std::atomic<Steinberg::uint32> tail_{0};
    int inputs_ = 2, outputs_ = 2;
    Steinberg::Vst::SpeakerArrangement input_arrangement_ = Steinberg::Vst::SpeakerArr::kStereo,
                                       output_arrangement_ = Steinberg::Vst::SpeakerArr::kStereo;
    bool active_ = false;
    // The rate and buses of the last activation. The host's configuration counts
    // as known only while it still matches: setupProcessing alone is not binding.
    std::optional<HostLayout> activated_;
    // Whether process() has played audio with the rate and buses of the last
    // activation (any activation with them since they last changed). Restored
    // state is checked against the host only then: until the host plays,
    // it may still be changing its buses (REAPER starts the plug-in on
    // stereo buses before it restores a project and gives it the track's).
    std::atomic<bool> played_{false};
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
    // or an undo may bring another). Never the file's folder. The format:
    // "CVCS", a version (1), then the name and the identity, each as a
    // little-endian 32-bit length and UTF-8 bytes. setState sends both to the
    // processor (name_message), which names its report and the report's text
    // with them. Before PLUG-22 hosts
    // saved an empty controller state, so an empty stream, a shorter one or
    // another version reads as no name, never as a failure (VST3-8).
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
        return status_text_;
    }
    // VST3-3: the window's report, the copy the processor sent last (empty
    // before it has sent one), after asking it for a fresh one.
    std::vector<std::pair<std::string, std::string>> report();
    // Copy diagnostics: the report's text and this instance's recent log
    // lines. Reads the log file: on the window's thread only.
    std::string diagnostics();
    // Has the processor load the filter file at `path` (UTF-8); false if it
    // refused it. An accepted file changes the component state without a
    // parameter change, so the host is told the project changed
    // (IComponentHandler2::setDirty) when the processor's answer says so.
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
    // The host may restore state (and so send the status) on another thread
    // than the one reading parameter text.
    std::mutex status_lock_;
    std::string status_text_;
    int status_value_ = static_cast<int>(HostStatus::no_filter); // The status the text is for.
    std::vector<std::pair<std::string, std::string>> report_;    // VST3-3, under status_lock_.
    std::string report_text_, format_;                           // Under status_lock_.
    // VST3-15, under status_lock_: the last load_message's number, the last
    // one answered and whether it was accepted; the last latency_message's
    // number, and whether its answer is still to come.
    Steinberg::int64 load_requests_ = 0, load_answered_ = 0;
    bool load_accepted_ = false;
    Steinberg::int64 latency_requests_ = 0;
    bool latency_waiting_ = false;
    // PLUG-22, under status_lock_: the filter file's name and the identity
    // of the filter read from it, saved in the controller state.
    std::string name_, identity_;
};
} // namespace convolver::vst3
