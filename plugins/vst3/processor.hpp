// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include "host_report.hpp"
#include "host_settling.hpp"
#include "link.hpp"
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
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
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
// its text the instance's HostStatusLine, which the controller reads from the
// processor (link.hpp).
inline constexpr Steinberg::Vst::ParamID status_id = identity::vst3_status_parameter_id;
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
// VST3-4: PLUG-10's settings as parameters, with the Audio Unit's IDs
// (docs/develop/plugin-contract.md#settings). Gain in dB (min_gain_db to
// max_gain_db, linear in decibels) and Mix in percent are automatable; the
// processor reads them from process()'s parameter changes, as Bypass.
// Latency is a list of host_latencies, not automatable: the controller
// gives a new choice to the processor (ProcessorLink::chosen_latency) and
// then asks the host to restart the component (kLatencyChanged). While
// active, the processor builds a processor with the new Latency and plays
// it before the call returns (VST3-16: REAPER reads getLatencySamples()
// inside restartComponent and does not restart the component), so the
// latency the host reads is the one that plays. All three are saved
// in the component state (host state version 3, defaults included: PLUG-35).
// IDs from plugins/identities.json; never change them.
inline constexpr Steinberg::Vst::ParamID gain_id = identity::vst3_gain_parameter_id;
inline constexpr Steinberg::Vst::ParamID mix_id = identity::vst3_mix_parameter_id;
inline constexpr Steinberg::Vst::ParamID latency_id = identity::vst3_latency_parameter_id;
// PLUG-10: Gain mode (Set, Estimate or No clipping) is a list, not
// automatable, as Latency: Estimate may filter noise for a long filter
// before it can play, which the audio thread must not do. The controller
// gives a new choice to the processor (ProcessorLink::chosen_gain_mode); the
// processor finds the estimate if it needs it, publishes the filter with it
// (the crossfade covers the change) and then plays the new mode. Saved in
// the component state (host state version 4 when not Set).
inline constexpr Steinberg::Vst::ParamID gain_mode_id = identity::vst3_gain_mode_parameter_id;
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
// Reads restored state.
HostState read_state(Steinberg::IBStream*);
bool write_state(Steinberg::IBStream*, const HostState&);
class Processor final : public Steinberg::Vst::AudioEffect, public ProcessorLink {
  public:
    Processor();
    ~Processor() override;
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor);
    }
    // VST3-26: registers the Bridge the controller pairs through (link.hpp);
    // terminate() detaches it.
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API terminate() override;
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
    // VST3-26: sends the Bridge's key (pair_message) on connecting, and
    // again when the controller asks (pair_request_message).
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
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
    // ProcessorLink (link.hpp), for the controller, which reaches them
    // through that interface only.
    PairView view() override;
    void chosen_latency(std::size_t frames) override;
    void chosen_gain_mode(GainMode) override;
    LoadAnswer load_chosen(const std::string& path) override;
    LoadAnswer read_waiting_list() override;
    void show_bypass(bool) override;
    void offer_name(const std::string& name, const std::string& identity) override;
    void explain_faults(bool only_if_due) override;
    // After a host call that may have changed what the controller shows,
    // outside control_: the Bridge is marked and the controller told
    // (changed_message), with nothing in the message to go stale.
    void signal_change() noexcept;
    void send_key() noexcept;
    void detach_bridge() noexcept;
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
    // VST3-3, a file the window chose: reads the filter file at `path` (UTF-8) for
    // the host's rate and channels, as convolver-preset does, and loads it
    // as a state restore would; PLUG-26: or a Convolver .vstpreset
    // (read_chosen_file()). True if it was accepted. `settings` receives the
    // settings an accepted preset applied, or none.
    bool load_file(std::string_view path, std::optional<HostSettings>& settings);
    // load_file()'s reading and install(): false after recording why the
    // file was refused; throws when install() refuses it, or when memory runs
    // out.
    bool install_chosen(const std::string& path, std::optional<HostSettings>& settings);
    // VST3-3, under control_: the window's report. The window looks for
    // unexplained faults at most every report_fault_interval, so an open
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
    // (show_bypass()).
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
    // VST3-26, under control_: PairView::generation. PairView::restored_at.
    std::uint64_t generation_ = 0;
    std::atomic<std::uint64_t> restored_at_{0};
    std::shared_ptr<Bridge> bridge_;
    std::uint64_t bridge_key_ = 0;
    bool layout_refusal_logged_ = false; // Refused layouts are logged once per instance.
    std::string logged_;                 // The last line log() queued.
    std::vector<std::pair<std::string, bool>> unlogged_; // Lines and whether each is a problem.
    const std::string tag_; // "VST3 #n": this instance in the Convolver log.
};
// The edit controller. It also tells hosts which parameter is the wet/dry
// mix (IParameterFunctionName, kDryWetMix), as the SDK's examples do.
// VST3-26: it reads and asks the processor of its instance directly through
// the Bridge it pairs with (link.hpp), and reads it again (refresh()) at its
// next call on the UI thread after the processor says it changed. Latency and
// Gain mode are the processor's, so the controller always shows the
// processor's. Gain, Mix and Bypass are the host's, which passes them to the
// processor in process(); the controller takes the processor's only when
// something else set them (PairView::generation), so a late refresh never
// undoes the host's newer value. Every host callback comes from a call on the
// UI thread (the thread that initialized the controller).
class Controller final : public Steinberg::Vst::EditController,
                         public Steinberg::Vst::IParameterFunctionName {
  public:
    static Steinberg::FUnknown* create(void*) {
        return static_cast<Steinberg::Vst::IEditController*>(new Controller);
    }
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown*) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    // Paired, the parameters show what the processor plays, which the host
    // restored first (the SDK's order), or, restoring the controller first,
    // once the processor's own restore says it changed. Unpaired, the
    // parameters show the state's values.
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
    // (PLUG-31). The format is in vst3_controller_state.hpp. setState offers
    // both to the processor, which names its report and the report's text
    // with them. Before PLUG-22 hosts saved an empty controller state, so an
    // empty stream, a shorter one or another version reads as no name, never
    // as a failure (VST3-8).
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream*) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream*) override;
    // VST3-26: asks the processor for its key (pair_request_message), so
    // pairing works in either connection order.
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint*) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint*) override;
    // The processor's pair_message and changed_message.
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage*) override;
    // A Status value from the host: the processor's audio thread writes
    // Fault as an output parameter change, and the host passes it on here;
    // the processor then explains it. A new Latency or Gain mode, from the
    // host or the window, goes to the processor, and a new Latency asks the
    // host to restart the component.
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
    // VST3-3: the window's report, read from the processor (none while
    // unpaired). PLUG-40: first a configuration list waiting for the host's
    // settings is read again, once the host plays audio; an accepted one
    // marks the project changed as a chosen file does.
    std::optional<HostReport> report();
    // Copy diagnostics: the report's text and this instance's recent log
    // lines. Reads the log file: on the window's thread only.
    std::string diagnostics();
    // Has the processor load the filter file at `path` (UTF-8); false if it
    // refused it, or the controller is unpaired. An accepted file changes the
    // component state without a parameter change, so the host is told the
    // project changed (IComponentHandler2::setDirty); a host without setDirty
    // is shown an edit of Latency that keeps its value instead (VST3-17).
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
    // Calls `f` with the processor; false while unpaired, or once the
    // processor has gone (the controller is then unpaired).
    template <class F> bool with_processor(F&& f);
    // The Bridge, under bridge_lock_: a host may restore state (and so call
    // the processor) on another thread than the one that pairs.
    std::shared_ptr<Bridge> bridge() const;
    void set_bridge(std::shared_ptr<Bridge>);
    bool on_ui_thread() const noexcept;
    // On the UI thread: reads the processor if it said it changed.
    void refresh_if_changed();
    // On the UI thread: the parameters, the report and the latency the host
    // last read take the processor's (the class comment says which).
    // `adopt_all`: Gain, Mix and Bypass too (pairing, setComponentState).
    // `tell_host`: values that changed are announced (kParamValuesChanged);
    // setComponentState leaves that to the host, which reads them after it.
    void refresh(bool adopt_all, bool tell_host = true);
    // IComponentHandler::restartComponent, never inside another: flags asked
    // for while the host is in one are sent after it returns.
    void restart(Steinberg::int32 flags);
    // PLUG-26 and VST3-17 after an accepted file: a preset's settings as
    // edits, and the project marked changed.
    void accepted_file(const LoadAnswer&);
    mutable std::mutex bridge_lock_;
    std::shared_ptr<Bridge> bridge_; // Null while unpaired.
    std::thread::id ui_thread_;
    // A Latency or Gain mode chosen while unpaired, and when
    // (Bridges::stamp()), sent once paired if the processor accepted no
    // restore after it.
    std::optional<std::size_t> unsent_latency_;
    std::optional<GainMode> unsent_gain_mode_;
    std::uint64_t unsent_at_ = 0;
    // The processor's generation when it was last read; the latency the host
    // was last asked to read (none before pairing).
    std::uint64_t generation_ = 0;
    std::optional<std::uint32_t> latency_told_;
    // restart(), on the UI thread.
    bool restarting_ = false;
    Steinberg::int32 restart_waiting_ = 0;
    // The host may restore state (and so refresh) on another thread than the
    // one reading parameter text.
    std::mutex status_lock_;
    // VST3-3, under status_lock_: the report read last, from which the
    // Status text and Copy diagnostics come (PLUG-39 step 5), and its log tag.
    std::optional<HostReport> report_;
    std::string format_;
    // PLUG-22, under status_lock_: the name and filter identity the last
    // setState brought. getState saves the name the last report gives the
    // filter that plays ("filter saved name"), and this pair only when that
    // filter has none.
    std::string name_, identity_;
};
} // namespace convolver::vst3
