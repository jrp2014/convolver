// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "convolver/advice.hpp"
#include "convolver/engine.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace convolver {
// PLUG-10: the settings every adapter offers as host parameters
// (docs/develop/plugin-contract.md#settings). The defaults leave the sound as
// it was before the settings existed.
inline constexpr double min_gain_db = -100, max_gain_db = 24; // jrp, 25 October 2026.
// The latencies adapters offer, in frames: HostProcessor's and the engine's
// latency (the CLI's --block-size). Larger can use less processor time.
// jrp, 25 October 2026: powers of two from 64 to 4096; 7 November 2026
// (PERF-2): zero latency added first, and the default.
inline constexpr std::array<std::size_t, 8> host_latencies{0, 64, 128, 256, 512, 1024, 2048, 4096};
inline constexpr std::size_t default_host_latency = 0;
// PLUG-10: which gain plays. Manual plays the Gain setting; Estimated and
// Clip-safe play the filter's own figure from estimate_attenuation() (its
// noise estimate and its no-clipping bound), computed when the filter is
// accepted, and leave the Gain setting as it is. Saved as these numbers
// (VST3 state version 4, the Audio Unit's parameter 1002 and LV2's
// gain_mode port; jrp, 12 November 2026).
enum class GainMode : std::uint8_t { set = 0, estimate = 1, no_clipping = 2 };
inline constexpr std::size_t gain_mode_count = 3;
struct HostSettings {
    // The filtered signal's gain in decibels, within min_gain_db and
    // max_gain_db (clamped there by HostProcessor).
    double gain_db = 0;
    // The share of the filtered signal, 0 to 1; the rest is the input delayed
    // by the latency. Ignored (always 1) when there is no dry signal
    // (dry_signal(): the filter's input and output counts differ, or the
    // host's, or a load failed and no filter is kept), as for bypass.
    // Clamped to 0..1.
    double mix = 1;
    // The latency in frames, one of host_latencies. Unlike gain and mix it
    // cannot change while a processor runs: it is the block size the
    // processor is built with.
    std::size_t latency = default_host_latency;
    // Which gain plays (GainMode); gain_db keeps the Set value meanwhile.
    GainMode gain_mode = GainMode::set;
    bool operator==(const HostSettings&) const = default;
};

// PLUG-10: the gains in decibels that Estimated and Clip-safe play for a
// filter: estimate_attenuation()'s noise_attenuation_db and
// worst_case_attenuation_db, applied as they are (never clamped to the Gain
// range). 0 dB for what plays without a filter (identity or silence).
// The bound only adds up the taps, so it is always found; it is absent only
// when it exceeds the largest float, as the CLI's --gain-db no-clip refuses.
// The estimate filters noise for ten filter lengths, which takes time for a
// long filter, so HostFilter finds it only while Estimated is chosen; it is
// absent until then, and when the noise peaks beyond the largest float
// (estimate_failed), as --gain-db auto refuses. The noise peak is never above
// the bound, so a filter whose estimate failed has no bound either (FFT-5).
// Where the estimate is absent, Estimated plays the bound instead, which is
// never louder; where the bound is absent too, the filter plays silence.
struct FilterGains {
    std::optional<double> estimate_db = 0.0;
    std::optional<double> no_clipping_db = 0.0;
    bool estimate_failed = false;
    bool operator==(const FilterGains&) const = default;
};
// Control thread: the figures for this filter, the estimate only when
// `estimate` is set. Never throws for a figure it cannot compute: that figure
// is absent.
FilterGains filter_gains(const Configuration&, bool estimate = true);
// The gain in decibels that plays under `settings` with these figures: the
// Gain setting (clamped to its range) in Manual mode, the filter's figure
// otherwise (the bound for an absent estimate); absent when the filter plays
// silence.
std::optional<double> applied_gain_db(const HostSettings&, const FilterGains&) noexcept;
const char* gain_mode_name(GainMode) noexcept; // "Manual", "Estimated", "Clip-safe".

// Versioned self-contained host state. All codec calls are control-thread only.
struct HostState {
    // Absent means an empty instance: identity at the host's rate and layout for
    // equal counts, otherwise (or when failed is set) silence.
    std::optional<Configuration> configuration;
    // Saved and applied by the VST3 adapter only. The LV2 and AU adapters leave
    // bypass to the host, as those formats do, and ignore this flag.
    bool bypass = false;
    // A load failed and nothing was accepted, so the instance plays silence;
    // saved so that reopening a project stays silent. Never with a configuration.
    // Encoded as version 2; all other states stay version 1.
    bool failed = false;
    // Saved and applied by the VST3 adapter only (VST3-4): the settings its
    // parameters held. Encoded as version 3 whenever present, defaults
    // included (PLUG-35), so a preset saved at the defaults restores them,
    // or as version 4 when the gain mode is not Set (PLUG-10); every state
    // without settings keeps its older version. Absent in
    // older states, presets from convolver-preset and the other formats'
    // states; the Audio Unit and LV2 save their settings as parameters and
    // ports, and ignore these.
    std::optional<HostSettings> settings = std::nullopt;
    // PLUG-53 (jrp, 26 November 2026: one shared field for every format):
    // the preset's own name, which heads the plug-in window in place of the
    // filter file's name. Empty for a filter that came from no named preset.
    // A state with a name is encoded as version 5, which earlier versions of
    // Convolver refuse as newer; every state without one keeps its older
    // version. Only a storable_preset_name() is encoded.
    std::string preset_name{};
};
// PERF-26 (jrp's review of 7bd024f): a state to save that borrows its
// filter and name. A plug-in captures what it saves under its lock as a
// HostSnapshot, which shares the filter instead of copying it, and encodes
// this view of it once the lock is released. It has no default, so
// encode_host_state({}) still means an empty HostState.
struct HostStateView {
    HostStateView(const Configuration* configuration_, bool bypass_, bool failed_,
                  std::optional<HostSettings> settings_, std::string_view preset_name_) noexcept
        : configuration(configuration_), bypass(bypass_), failed(failed_), settings(settings_),
          preset_name(preset_name_) {}
    const Configuration* configuration; // HostState::configuration
    bool bypass;
    bool failed;
    std::optional<HostSettings> settings;
    std::string_view preset_name;
};
HostStateView view_of(const HostState&) noexcept;
// What a plug-in saves, taken in one step (HostFilter::snapshot()): the
// filter that plays, shared, with its preset name and the failed flag from
// the same moment; the adapter adds what it saves itself (VST3's bypass and
// settings).
struct HostSnapshot {
    std::shared_ptr<const Configuration> configuration;
    bool bypass = false;
    bool failed = false;
    std::optional<HostSettings> settings = std::nullopt;
    std::string preset_name{};
    HostStateView view() const noexcept {
        return {configuration.get(), bypass, failed, settings, preset_name};
    }
};
// PLUG-53: at most this many bytes of UTF-8, so the name fits in the room a
// filter's state keeps (host_settings_room_bytes) beside the settings.
inline constexpr std::size_t max_preset_name_bytes = 200;
// Whether `name` can be saved as HostState::preset_name: not empty, at most
// max_preset_name_bytes, valid UTF-8 and without control characters.
bool storable_preset_name(std::string_view name) noexcept;
inline constexpr std::size_t max_host_state_bytes = 64U * 1024 * 1024;
// PLUG-13: the room a filter's state keeps free for the settings, more than
// version 3's 20 bytes so that settings added later (such as PLUG-10's gain
// mode) fit without changing which filters are accepted. PLUG-70 (jrp, 27
// November 2026, before 1.0): 4096 bytes, up from 256, which a preset name
// and the settings had nearly filled, so that later metadata fits without
// refusing filters 1.0 accepts. A filter within 960 samples of the old limit,
// which previews up to 0.19 accepted, is refused.
inline constexpr std::size_t host_settings_room_bytes = 4096;
// Throws std::length_error when the encoding would exceed max_host_state_bytes
// (encoded_host_state_size()), before allocating it.
std::vector<std::byte> encode_host_state(const HostState&);
std::vector<std::byte> encode_host_state(const HostStateView&);
// PLUG-38: whether two filters are the same in every stored value, each
// float compared by its bits as configuration_identity() hashes it and the
// state stores it, so a signed-zero route gain differs from a positive zero.
bool same_configuration(const Configuration&, const Configuration&) noexcept;
// VST3-31 (jrp's review of 7bd024f, §2.1): whether encode_host_state() would
// write the same bytes for both, compared in place, without encoding or
// allocating. Both must be states it can encode: neither is checked.
bool same_host_state(const HostStateView&, const HostStateView&) noexcept;
// PLUG-12: the size in bytes encode_host_state() writes for this state,
// counted without encoding or allocating. It may exceed max_host_state_bytes;
// such state cannot be saved (saturates at SIZE_MAX).
std::size_t encoded_host_state_size(const HostState&) noexcept;
std::size_t encoded_host_state_size(const HostStateView&) noexcept;
// PLUG-12: why this state is too large to save and what to change, or empty
// when it fits. HostFilter::load refuses such a filter
// before accepting it, so a plug-in never plays a filter it cannot save, and
// convolver-preset refuses to write it. PLUG-13: a filter fits only with
// host_settings_room_bytes to spare, counted in place of any settings the
// state holds, so changing a VST3 setting never makes an accepted filter
// unsaveable.
Advice host_state_size_error(const HostState&);
// Throws NewerHostState for state from a newer Convolver, std::length_error
// above the size limit, and std::invalid_argument for anything else malformed.
HostState decode_host_state(std::span<const std::byte>);
// PLUG-58 (jrp's review of 7bd024f, §2.2): what a saved state says before
// its filter, for a reader that needs only that (a VST3 controller restored
// off the UI thread keeps only the settings and bypass). `filter` says
// whether one follows; it is neither read nor checked.
struct HostStateHeader {
    bool bypass = false;
    bool failed = false;
    bool filter = false;
    std::optional<HostSettings> settings = std::nullopt;
    std::string preset_name{};
};
// The bytes before the filter in any state: the magic, version and flags,
// then at most the room kept for a preset name and the settings.
inline constexpr std::size_t host_state_header_bytes = 12 + host_settings_room_bytes;
// The header of a state of `size` bytes, from `start`, at least its first
// host_state_header_bytes (or the whole state, when shorter). Throws as
// decode_host_state() does for the same parts, and for bytes after the
// header of a state without a filter.
HostStateHeader decode_host_state_header(std::span<const std::byte> start, std::size_t size);
// Host state with a version this build does not read; what() explains.
class NewerHostState : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};

// The sample rate and channel counts a host has negotiated, and the speakers
// its buses carry where the format says: WAV channel-mask bits 0 to 17 (the
// speaker table in plugins/identities.json), one bit per channel in bus order,
// or 0 when the host names no positions a WAV mask can express.
struct HostLayout {
    int sample_rate = 0;
    std::size_t inputs = 0, outputs = 0;
    std::uint32_t input_speakers = 0, output_speakers = 0;
    // VST3-9: the host's rate when it is not a whole number of hertz (a
    // pull-down rate such as 48000/1.001), else 0; sample_rate then holds
    // its whole part. Filters are made for whole hertz, so none suits it.
    double fractional_rate = 0;
};

// Sets a layout's rate from a host's rate in hertz (finite and positive):
// whole hertz in sample_rate, or the whole part there and the rate itself in
// fractional_rate. Strict: only a rate within a millionth of a millionth of
// whole hertz, floating-point rounding rather than another rate, counts as
// whole.
void set_host_rate(HostLayout&, double hertz) noexcept;
// The host's rate for messages: "48000 Hz", or "47952.04795 Hz".
std::string host_rate_text(const HostLayout&);
// The same rate: equal whole parts and equal fractional rates.
bool same_rate(const HostLayout&, const HostLayout&) noexcept;
// The same configuration for checking a filter: the same rate and the same
// input and output counts (speakers aside). Adapters count the host's
// configuration as known while it stays the same as one they started with.
bool same_configuration(const HostLayout&, const HostLayout&) noexcept;

// Advisory check of the speakers a filter's outputs are labelled for (its
// channel mask) against the host's output speakers. Empty when they agree, or
// when either side names no speakers: a mask of 0, one that does not give one
// speaker per channel, and one-channel buses are never compared. Channels are
// compared in bus order; in a layout with side (SL SR) and no back (BL BR)
// speakers, SL and SR count as BL and BR (5.1 is written as 0x3F or 0x60F).
// Never a reason to reject a filter: channels are routed by bus index whatever
// the labels say (docs/develop/plugin-identities.md#channel-layouts).
Advice speaker_notice(const Configuration&, const HostLayout&);

// What a HostProcessor plays: a filter, whether it has a dry signal (input c
// to output c, which bypass and Mix's dry share pass) and, for a mono filter,
// the channels it plays on (PLUG-57). Decided once,
// by HostFilter (dry_signal_rule()), and carried with the filter, so that
// processing, bypass, Mix and diagnostics never read it differently from a
// configuration's counts (jrp's review, 7 November 2026).
struct HostPlayback {
    // PLUG-39 step 4a: shared with HostFilter::filter(), never copied; never
    // null (a processor refuses a null one with std::invalid_argument). A
    // processor reads it while it is built and keeps no reference.
    std::shared_ptr<const Configuration> filter;
    bool dry = false;
    // PLUG-10: what Estimated and Clip-safe play for this filter
    // (HostFilter fills them in from the accepted filter's filter_gains()).
    FilterGains gains;
    // PLUG-38: HostFilter::load() found `filter` to be the one it had
    // accepted and was playing, compared in full, so an adapter may keep the
    // processor and its history (HostProcessor::keep()) instead of replacing
    // it. False everywhere else, and for a session the host is opening
    // (HostRequestKind::session), which keeps nothing from before.
    bool playing = false;
    // EDGE-3, PLUG-57: 0, or the number of channels a filter with one input
    // and one output plays on, each through the filter (on_each_channel()).
    // `filter` stays the accepted one, shared; a processor lays it out only
    // when it is built or replaced from this, so keep() copies nothing.
    std::size_t each_channel = 0;
};
// A filter that plays: a dry signal only when the filter itself has as many
// inputs as outputs (PLUG-23), never because the host's counts are equal.
HostPlayback filter_playback(std::shared_ptr<const Configuration>);
HostPlayback filter_playback(Configuration);

// What an instance plays without a usable filter, under the shared adapter
// contract: identity (input c to output c) for an empty instance with equal
// counts; exact silence for unequal counts or when `failed` (a failed initial
// load or a filter that does not suit the host). It carries no answer on the
// dry signal, so give a processor unfiltered_playback().
Configuration unfiltered_configuration(const HostLayout&, bool failed);
// The same, with a dry signal only when `dry` and the host's counts are equal
// (HostFilter passes dry_signal() for a kept filter, none after a failed load
// with no filter kept, and a dry signal for an empty instance).
HostPlayback unfiltered_playback(const HostLayout&, bool failed, bool dry);
// PLUG-23: a filter may have fewer inputs than the host gives the plug-in
// (REAPER gives a VST3 plug-in as many inputs as outputs, so a 2-in, 6-out
// crossover gets 6 of each); it reads the first ones and the rest are
// ignored. The ignored ones in words, "inputs 3–6" or "input 2" with `dash`
// between two numbers; empty when the filter reads every input the host gives.
std::string ignored_inputs(const Configuration&, const HostLayout&, const char* dash = "–");
// EDGE-3 (jrp, 25 November 2026): a filter with one input and one output plays
// on each channel of a host that gives as many outputs as inputs, two or more
// (on_each_channel()), rather than not suiting it. The filter stays as it is
// (it is saved so, and suits a mono track too); what plays is laid out for the
// host at each start.
bool plays_on_each_channel(const Configuration&, const HostLayout&) noexcept;
// EDGE-3: whether a filter routes two inputs to two outputs as true stereo,
// four paths left to left, left to right, right to left and right to right at
// unit gain: how a four-channel IR file used directly plays on two channels,
// and what a .cfg routing them so does.
bool true_stereo_routing(const Configuration&) noexcept;
// EDGE-3: a sentence for the log saying how the filter plays on `host` when
// that is one of those routings ("The filter has one input and one output, so
// it plays on each of the host's 2 channels."), empty otherwise.
std::string routing_sentence(const Configuration&, const HostLayout&);
// PLUG-22: which filter this is, so a name saved beside it (VST3's
// controller state) is shown only while it plays: a 64-bit FNV-1a hash of
// every stored value (rate, counts, mask, delays, and each path's routes,
// gains and samples as their bits), the same on every machine. Two filters
// that differ anywhere differ here, but for a 1 in 2^64 chance. Reads every
// sample, so it runs once, when a filter is accepted (HostFilter).
std::uint64_t configuration_identity(const Configuration&) noexcept;

class HostProcessor;

// What a filter that does not suit the host's configuration means for a load
// (the shared adapter contract, "state that arrives before the host is
// configured"). The adapter decides which applies; HostFilter applies it.
enum class HostLoadCheck {
    // The host's configuration is settled and cannot change (LV2: a layout is
    // its own plug-in): the load is refused, and what was accepted before
    // stays (a failed first load if nothing was).
    refuse_mismatch,
    // PLUG-42 (jrp, 21 November 2026): the host's configuration is settled
    // now, but the host may start the plug-in with another later (VST3, the
    // Audio Unit). While an accepted filter suits it and plays, the load is
    // refused, so that one plays on. Otherwise a refusal would protect
    // nothing: the filter is kept, playing silence with the reason, until a
    // start suits it, and is saved as the filter rather than as a failed
    // load. Ardour 8.4 restarts the plug-in with the outputs set in its Pin
    // Connections a little after they are set; a preset loaded in between
    // was refused, never tried at the outputs that suited it, and saved as
    // a failure (HOST-10).
    keep_unless_one_plays,
    // The host may still change its configuration (it is restoring a project):
    // the filter is accepted all the same and kept, playing silence with the
    // reason as failure() until an activation suits it, as an activation keeps
    // one. VST3: REAPER starts the plug-in on stereo buses, restores the
    // project, then gives it the track's channels (jrp's crossover retest,
    // 7 November 2026).
    keep_until_suited,
};

// PLUG-8: why a request to load a filter failed, or why an instance plays
// silence, kept as a value on the control side and never touched by the audio
// thread (docs/develop/plugin-contract.md#failure-reasons). Adapters
// read it from HostFilter::failure(), show it with host_status() and log it
// (docs/develop/plugin-contract.md#status-and-log).
enum class HostFailureKind {
    filter_file,      // The filter file is missing or cannot be read.
    state_unreadable, // The saved state or preset is malformed or the wrong type.
    state_newer,      // The saved state comes from a newer Convolver.
    sample_rate,      // The filter was made for another sample rate.
    channels,         // The filter's channel counts or layout do not suit the plug-in.
    preparation,      // The filter could not be prepared for playing.
    activation,       // The processor could not be built when the host activated.
    saved_failure,    // The restored state records an earlier failed load.
    too_large,        // The filter is too large to save in the plug-in's state (PLUG-12).
    // PLUG-27, PLUG-40: a configuration list chosen before the host's rate
    // and channels were known, so no entry could be chosen.
    settings_unknown,
};
// Where a filter came from. Plug-ins cannot tell a preset from a saved
// session: both arrive as state.
enum class HostFilterSource { state, file };
// PLUG-39: what kind of request a load is, which HostFilter::load() uses to
// decide what an accepted one changes (HostChange; the scope table in
// docs/develop/plugin-contract.md#scope-of-each-change).
enum class HostRequestKind {
    // State the host restores, or a preset it chooses: the filter already
    // playing plays on with its history, and the state's settings and bypass
    // apply.
    restore,
    // State the host says comes from a session it is opening (VST3's
    // kProject): as restore, but even the filter already playing is replaced,
    // so the session keeps nothing from the one before.
    session,
    // A filter file or preset chosen in the plug-in: the filter already
    // playing plays on, and the settings apply only when the preset holds
    // them; bypass stays.
    chosen_file,
};
struct HostRequest {
    HostFilterSource source = HostFilterSource::state;
    std::string path; // The filter file, for HostFilterSource::file.
    // Numbers requests in order, so a later request's outcome is never
    // mistaken for this one's. 0 asks HostFilter to number it. An adapter
    // that numbers requests itself (LV2's generations, counted on the audio
    // thread) numbers all of them, each larger than every earlier one. An
    // adapter that needs one number before it calls load() takes it from
    // HostFilter::next_request(), so numbers never repeat.
    // A failure at activation carries the accepted filter's request, which is
    // 0 for an instance that never loaded one.
    std::uint64_t number = 0;
    // VST3-11: every file an accepted file's filter was read from (the file
    // itself, then for a .cfg or list the lists and configurations visited
    // and the chosen configuration's IRs, but not the IRs of list entries
    // passed over), as read_filter_file() gives them, so a window can reload
    // the filter when one changes. Empty for state and for a refused file.
    std::vector<std::string> files{};
    // PLUG-22: for state, the name saved beside it for the window to show
    // (UTF-8, at most max_filter_name_bytes): the file's name with its
    // extension, never its folder, as the Audio Unit's preset name keeps it.
    // Empty when unknown; a file's request leaves it empty, as the window
    // takes the name from `path`. VST3 keeps the name in the controller
    // state, apart from the component's, and offers it to the filter
    // (HostFilter::offer_name()), which gives accepted() the name of the
    // filter that plays.
    std::string name{};
    // PLUG-53: the preset's own name, which heads the window in place of
    // the filter file's name: HostFilter::load() sets it from the state's
    // HostState::preset_name, so a filter file chosen directly (whose state
    // has none) clears the name of the preset before it. For a .vstpreset
    // chosen in the window, `name` holds the filter file's name it keeps.
    std::string preset{};
    // PLUG-33: for a configuration list chosen where no entry has the host's
    // input count, the entry read instead, the one with the most inputs below
    // the host's (its path, UTF-8, as read_filter_file() gives it), so the
    // log and the window say which entry plays and why. Empty otherwise.
    std::string wider_entry{};
    // PLUG-39: what the request is, which decides what an accepted one
    // changes (HostChange).
    HostRequestKind kind = HostRequestKind::restore;
};
inline constexpr std::size_t max_filter_name_bytes = 1024;
// PLUG-53: the filter file's name a request gives its filter, which is saved
// beside its state: a chosen file's own name with its extension, never its
// folder (for a chosen preset, the filter file's name it keeps), or the name
// saved beside state; empty when it has none.
std::string filter_file_name(const HostRequest&);
struct HostFailure {
    HostFailureKind kind = HostFailureKind::state_unreadable;
    HostRequest request;
    // The filter the request carried, when it could be read: rate and counts.
    int sample_rate = 0;
    std::size_t inputs = 0, outputs = 0;
    // True: the instance plays silence. False: a working filter keeps playing.
    bool silent = false;
    // What failed, why and what to change, as the CLI explains its errors.
    // Empty when memory ran out before it could be recorded (ROBUST-7).
    Advice message;
};

// Thrown by a HostFilter::load install callback to abandon a request that a
// newer one has overtaken: load() rethrows it and changes neither the filter
// nor the failure, though the request's number is used up.
class SupersededRequest : public std::exception {
  public:
    const char* what() const noexcept override {
        return "superseded by a newer request";
    }
};

// DIAG-1: problems HostProcessor::process meets while it plays, counted per
// call on the audio thread without locking or allocating, and read on the
// control side (HostProcessor::faults), where adapters explain and log them.
// Each count is the number of process() calls the problem affected.
struct HostFaults {
    // Input samples that were not finite or beyond the float range: played
    // as silence (the call itself succeeds).
    std::uint64_t rejected_input = 0;
    // Output that was not finite or beyond the float range, from extreme but
    // finite input or filter values: those samples are silenced and the
    // filter restarts (reset) after the call, so its tail is lost.
    std::uint64_t output_overflow = 0;
    // The engine failed: the call is silenced, the filter restarts and
    // process() returns false.
    std::uint64_t processing_failure = 0;
    // The host passed more frames or channels than were set up: the call is
    // refused (false), its output not written.
    std::uint64_t invalid_call = 0;
    std::uint64_t total() const noexcept {
        return rejected_input + output_overflow + processing_failure + invalid_call;
    }
    bool any() const noexcept {
        return total() != 0;
    }
};
// What happened, why and what to change, for the log: one sentence per kind
// of fault. Empty when there are none.
std::string fault_message(const HostFaults&);
// What HostFilter::observe() found: the faults no earlier call returned.
// `changed`: an accepted load came between the last call and this one, and
// some of `faults` were counted before it, so they belong to the filter it
// replaced and the status does not show them (DIAG-3: they are reported as
// met around a filter change, not divided between the filters). `held`: the
// status shows a load failure, which comes before Fault
// (HostFilter::failure_shown()), so it shows none of them.
struct HostFaultReport {
    HostFaults faults;
    bool changed = false, held = false;
    bool any() const noexcept {
        return faults.any();
    }
};
// The log text, whose ending never says the status shows Fault for faults
// it does not show.
std::string fault_message(const HostFaultReport&);

// PLUG-39: what an accepted HostFilter::load() requires of the adapter
// besides what plays (HostPlayback, which its install callback receives):
// one value decided inside the load's transaction from the request's kind and
// the state, so that no adapter decides it again. Rows of the scope table
// (docs/develop/plugin-contract.md#scope-of-each-change).
struct HostChange {
    // The settings to apply: the request's own when it holds them, none to
    // leave them as they are. The load has already found the estimate their
    // Gain mode needs. A format whose state always restores its settings
    // fills in its defaults before the load (VST3-4).
    std::optional<HostSettings> settings;
    // The state's bypass, for restored state; none for a chosen file. Only
    // VST3 applies it: the Audio Unit and LV2 leave bypass to the host.
    std::optional<bool> bypass;
};

// ROBUST-8: what HostFilter::activation() found for the host, which
// HostFilter::activate() records once the processor built from `playback`
// will play.
struct HostActivation {
    HostPlayback playback;
    // A filter that does not suit the host: why it plays silence.
    std::optional<HostFailure> failure;
    // The speaker notice (speaker_notice()), empty if none.
    Advice notice;
};

// Control side of the shared adapter contract, for every plug-in format
// (docs/develop/plugin-contract.md): which
// filter a load replaces, which filter is saved, and what plays. It does no
// audio work; adapters call it under their control lock.
class HostFilter {
  public:
    // `max_channels` is the format's channel bound, a check on the state itself.
    explicit HostFilter(std::size_t max_channels = max_channel_count)
        : max_channels_(max_channels) {}
    // The accepted filter, which a host saves. Null for an empty instance and
    // after a failed initial load. PLUG-39 step 4a (jrp, 20 November 2026):
    // immutable and shared, by the processor's HostPlayback and by any copy
    // of this HostFilter an adapter keeps for its diagnostics, so a copy is
    // cheap and stays whole however this one changes later.
    const std::shared_ptr<const Configuration>& filter() const noexcept {
        return current_.filter;
    }
    // PERF-26: what a host saves now: filter(), the accepted preset name
    // with it, and failed(), without copying the filter.
    HostSnapshot snapshot() const;
    // VST3-31: the same, borrowed (valid while this HostFilter is unchanged)
    // without the adapter's bypass and settings, for comparing in place.
    HostStateView saved_view() const noexcept;
    // worst_case_peak() of filter(), found when it was accepted; 0 without one.
    double filter_peak() const noexcept {
        return current_.peak;
    }
    // PLUG-52: filter()'s gain for steady noise on every input (the largest
    // output's root sum of squared taps), found when it was accepted; 0
    // without one. Far below filter_peak() for a long reverb, whose bound
    // music rarely nears.
    double filter_noise_gain() const noexcept {
        return current_.noise;
    }
    // configuration_tail_frames() of filter(), found when it was accepted; 0
    // without one. The tail an adapter reports while no processor plays; a
    // processor reports its own (HostProcessor::tail_frames()), which also
    // covers a filter it is still fading out (PLUG-39 step 6).
    std::size_t filter_tail() const noexcept {
        return current_.tail;
    }
    // PLUG-10: filter_gains() of filter(), found when it was accepted (the
    // estimate once found while estimating()); 0 dB for both without one.
    // Accepting the same filter again keeps them, finding only an estimate
    // newly needed (PLUG-39).
    // HostPlayback carries them to the processor.
    const FilterGains& gains() const noexcept {
        return current_.gains;
    }
    // PLUG-10: whether load() finds the noise estimate, which Estimated needs
    // and which takes time for a long filter. Adapters set it while their
    // gain mode is Estimated, with estimate(); a load that brings settings
    // sets it from their Gain mode as it is accepted. Turning it on
    // also finds the estimate of the filter already accepted (control thread;
    // filters noise, may throw std::bad_alloc) and then calls `apply` with
    // the new figures, for the adapter to pass to the playing filter with
    // HostProcessor::update_gains(), which keeps its engine and history (a
    // replacement would restart it, silent for the filter's delay). gains()
    // changes only after `apply` returns: if anything throws, the estimate is
    // not kept, and choosing Estimated again finds it again. Returns whether
    // it applied new figures: false when gains() did not change, or when no
    // filter plays (none accepted, or it does not suit the host), and then
    // `apply` is not called.
    bool estimate(bool on, const std::function<void(const FilterGains&)>& apply = {});
    bool estimating() const noexcept {
        return estimating_;
    }
    // PLUG-22, PLUG-39 step 4: a name kept apart from the filter's state
    // (VST3's controller state), offered with the identity of the filter it
    // names (filter_identity()). A name shows only while its filter plays:
    // accepted() takes it when that filter plays now or when state that
    // names nothing brings it later, in whichever order the host sends the
    // two, and a filter that is refused never renames the one that plays.
    // An accepted file's own name is offered the same way, and state of the
    // filter that plays keeps the name it shows. May throw std::bad_alloc,
    // and then changes nothing.
    void offer_name(std::string name, std::uint64_t identity);
    // PLUG-22: configuration_identity() of filter(), found when it was
    // accepted, so a report never computes it; 0 without one.
    std::uint64_t filter_identity() const noexcept {
        return current_.identity;
    }
    // True after a failed initial load: nothing accepted, and a load failed.
    bool failed() const noexcept {
        return failed_;
    }
    // PLUG-42: an accepted filter plays that suits the host as last checked
    // (no failure found at activation keeps it silent). Only then does
    // HostLoadCheck::keep_unless_one_plays refuse a filter that does not
    // suit; adapters ask it to read a chosen file the same way
    // (read_chosen_file()'s `keep_unsuited`).
    bool plays_suiting() const noexcept {
        return current_.filter && !failed_ && !activation_failure_;
    }
    // The contract's Kept, unsuited state: an accepted filter that does not
    // suit the host's rate or channels, so it plays silence (with any dry
    // share) until a start suits it. After an accepted load(), the filter it
    // kept for that reason (PLUG-42: why depends on the check the adapter
    // passed).
    bool kept_unsuited() const noexcept {
        return current_.filter && activation_failure_ &&
               activation_failure_->kind != HostFailureKind::activation;
    }
    // PLUG-44 (jrp, 21 November 2026): kept_unsuited() with no newer request
    // refused, so the instance only waits for audio settings that suit its
    // filter. The status shows Waiting, not Load failed: nothing failed to
    // load, and the filter plays once a start suits it.
    bool waits_for_settings() const noexcept {
        return kept_unsuited() && !load_failure_;
    }
    // DIAG-1: whether host_status() shows a load failure, which comes before
    // Fault. Adapters pass it to HostProcessor::hold_fault_signal() after
    // every load and rejection, and start() after every activation, so the
    // audio thread does not show Fault over it either.
    bool failure_shown() const noexcept {
        return activation_failure_.has_value() || load_failure_.has_value() || failed_;
    }
    // Why the instance plays silence or rejected its last load (PLUG-8).
    // A failure found at activation comes first; it clears at the next
    // activation that suits the filter. A rejected request's failure stays
    // until a later request succeeds, also across activations.
    const std::optional<HostFailure>& failure() const noexcept {
        return activation_failure_ ? activation_failure_ : load_failure_;
    }
    // PLUG-27 (jrp, 20 November 2026): a file chosen while the host was not
    // playing audio through the plug-in (accepted with keep_until_suited)
    // awaits validation until the host plays it, which the adapter reports
    // with confirm(). A start before then may be at a placeholder rate
    // (Audacity 4.0.2 starts its window's copy at 44.1 kHz whatever the
    // project's), too little evidence to tell the user the filter is wrong:
    // while waiting(), the status and the window say the filter is selected
    // and waiting for the host's audio settings (shown_failure()), though it
    // plays silence at that start and the log says so.
    bool awaiting() const noexcept {
        return awaiting_ && current_.filter != nullptr;
    }
    // A start that could not be done at all (fail_activation()) is shown:
    // it is not a check against a rate that may be a placeholder.
    bool waiting() const noexcept {
        return awaiting() && activation_failure_.has_value() &&
               activation_failure_->kind != HostFailureKind::activation;
    }
    // `host`: the configuration the host plays with now, which the adapter
    // passes whenever it sees the host playing (HostSettling::playing()) on a
    // control call. PLUG-40, from the independent review: only such a
    // configuration counts as the host's settings for a refused list
    // (played_with()), not one a brief start passed, such as Audacity
    // 4.0.2's FlushParameters at its placeholder 44.1 kHz.
    void confirm(const std::optional<HostLayout>& host = std::nullopt) noexcept {
        awaiting_ = false;
        if (host)
            heard_ = host;
    }
    // `host` if a control call saw the host playing with that configuration
    // (confirm()); nullopt otherwise.
    std::optional<HostLayout> played_with(const std::optional<HostLayout>& host) const noexcept {
        if (host && heard_ && same_configuration(*heard_, *host))
            return host;
        return std::nullopt;
    }
    // failure() as the status and the window show it: without the failure a
    // filter that is waiting() found at a start.
    const std::optional<HostFailure>& shown_failure() const noexcept {
        return waiting() ? load_failure_ : failure();
    }
    // failure()'s message as text, one labelled line per part after the
    // problem (as an exception's what() gives it), for a report or a test;
    // empty if none. The adapters log the parts on one line instead.
    std::string error() const;
    // Why the newest request failed (or the saved failure it restored), even
    // while a failure found at activation comes first in failure(). This is
    // what to log when a load is rejected.
    const std::optional<HostFailure>& rejection() const noexcept {
        return load_failure_;
    }
    // PLUG-40 (jrp, 21 November 2026: "don't make the user choose again
    // unnecessarily"): the configuration list the newest request refused
    // because the host's settings were not known
    // (HostFailureKind::settings_unknown), which an adapter reads again, as
    // if chosen, when its window reads the report while the host plays audio
    // through the plug-in (HostSettling::playing()), on that thread. Given
    // once for each refused request, so a read again that fails no other way
    // (out of memory) is not repeated at every read; empty otherwise.
    std::string take_unsettled_list();
    // Whether take_unsettled_list() would give a list now (PLUG-40), so an
    // adapter can keep it waiting when the host hands back the plug-in's own
    // state unchanged, as Audacity 4.0.2 does at every playback start.
    bool list_waits() const noexcept;
    // The request that supplied the accepted filter (or the saved failure);
    // number 0 before any was accepted.
    const HostRequest& accepted() const noexcept {
        return accepted_;
    }
    // Reserves the next request number, for a request numbered before load().
    // The in-tree adapters do not use it (LV2 numbers every request itself).
    std::uint64_t next_request() noexcept;
    // A possible problem that does not stop the accepted filter playing, from
    // the last check against the host (speaker_notice); empty if none. It
    // describes the host as of the last load() with a host or activate(); an
    // adapter that shows it must recheck after the host changes its buses.
    const Advice& notice() const noexcept {
        return notice_;
    }
    // Load decoded state: a filter, none to return to an empty instance, or a
    // saved failed load (plays silence). Returns what else the accepted
    // request requires (HostChange: the settings and bypass to apply),
    // decided from `request.kind` and the
    // state; the install callback receives what to play, which for a
    // request holding the filter already playing says so
    // (HostPlayback::playing), and passes it to HostProcessor::play(). A
    // request bringing settings finds the estimate their Gain mode needs and
    // sets estimating() from it as it is accepted. Checks on the state itself always run, including
    // that it fits the saved-state limit (PLUG-12), so whatever is accepted can be saved. Checks
    // against the host run when `host` is given (its configuration is known); `install` then
    // receives what to play and may throw to reject, or throw SupersededRequest to abandon the
    // request with nothing recorded or changed (a newer request has arrived). Without `host`,
    // well-formed state is accepted and checked at activation. On rejection this records the error,
    // keeps the accepted filter (a failed initial load if there was none) and throws
    // std::invalid_argument with an explanation, or std::bad_alloc when memory runs out even for
    // that; failed() is set before anything is allocated for the record (ROBUST-7). A transaction:
    // everything kept is built before `install` is called and nothing after it throws, so `install`
    // must publish as its last step (prepare, which may throw, then publish, which cannot). What
    // plays is then always what is saved, even when memory runs out. `request` says where the state
    // came from (numbered if 0). `counting`: the processor that plays (if any). An accepted load
    // clears its fault baseline (HostProcessor::clear_faults()) and observes from the same
    // snapshot, so faults shown afterwards are only the new filter's. `check` says what a filter
    // that does not suit `host` means (HostLoadCheck).
    HostChange load(HostState, const std::optional<HostLayout>& host,
                    const std::function<void(const HostPlayback&)>& install = {},
                    HostRequest request = {}, HostProcessor* counting = nullptr,
                    HostLoadCheck check = HostLoadCheck::refuse_mismatch);
    // A load that failed before decoding (malformed state, unreadable file):
    // recorded as load() records a rejection. Does not throw: if memory runs
    // out, the record keeps the kind and the request's number and source
    // without its path (ROBUST-7).
    // `carried`, when given, is the filter the request held (a readable file
    // made for another rate or other channels): the failure keeps its rate
    // and counts for the short status text. Its paths are not used.
    void reject(HostFailureKind, Advice why, HostRequest request = {},
                const Configuration* carried = nullptr) noexcept;
    // The same, as unreadable state.
    void reject(Advice why) noexcept {
        reject(HostFailureKind::state_unreadable, std::move(why));
    }
    // ROBUST-7: a request failed, recorded or not. Without an accepted filter
    // the instance now fails (failed()): it plays silence, and saved state
    // records the failure. Allocates nothing, so it cannot fail. reject() and
    // a refused load() do this before they record anything. Adapters call it
    // with `playing`, the processor that plays (if any), after every request
    // that failed, also one that failed before its failure could be recorded
    // (memory ran out building the message): an instance with no accepted
    // filter then silences `playing` (HostProcessor::silence()), so a failed
    // first load plays silence rather than the unfiltered input. failure()
    // may then be missing, or describe an earlier request.
    void fail_request(HostProcessor* playing = nullptr) noexcept;
    // What to play under `host` at activation: the accepted filter when it
    // suits the host. Otherwise silence and an error, keeping the filter, so
    // correcting the host's rate or layout brings it back. It may throw
    // (memory for the reason) and changes nothing. start() builds the
    // processor from `playback` and calls activate() only once nothing can
    // stop that processor from playing, so one that cannot be built leaves
    // the failure, the notice and the fault count as they were (ROBUST-8).
    HostActivation activation(const HostLayout& host) const;
    // Records an activation whose processor will play: its failure and
    // notice, and the fault count starts again for the new processor, which
    // counts from 0. Takes `failure` and `notice`, leaving `playback`.
    // Allocates nothing, so it cannot fail.
    void activate(HostActivation&&) noexcept;
    // PLUG-61: every adapter's processor start, for a host start or a new
    // Latency. Builds the processor for what activation(host) finds, sets
    // its bypass and resets it (fully bypassed or fully wet, without a ramp),
    // then records the activation (activate()) and holds the processor's
    // fault signal while a failure is shown (failure_shown()). The adapter
    // hands it to the audio thread, which must not fail (ROBUST-8). Throws,
    // changing nothing, when the processor cannot be built.
    std::unique_ptr<HostProcessor> start(const HostLayout& host, std::size_t max_frames,
                                         const HostSettings& settings, bool bypassed);
    // The adapter could not build a processor for what activation() returned
    // when the host started it, so it plays silence; recorded until the next
    // activate(). The fault count starts again, as for a new processor.
    // `why` is the reason as a phrase, shown after "could not be started
    // ...: ".
    void fail_activation(std::string why);
    // The same for the exception building the processor threw: its reason is
    // the problem of the advice it carries, without its cause or fix, so no
    // adapter picks that part; null, for one that is not a std::exception, a
    // reason that says the processor could not be built.
    void fail_activation(const std::exception* error);
    // DIAG-1: processing faults since the filter that plays was accepted or
    // the instance last started (activate()), whichever is later. host_status()
    // shows them as HostStatus::fault after any load failure.
    const HostFaults& faults() const noexcept {
        return faults_;
    }
    // Takes the processor's counts (HostProcessor::faults(), totals since it
    // was built), updates faults() from them and returns the faults that no
    // earlier call returned, for the adapter to log. activate() starts the
    // count again for a new processor, so call this before discarding the old
    // one. An accepted load clears faults(); the faults it leaves unreturned
    // are returned by the next call with `changed` set.
    HostFaultReport observe(const HostFaults& totals) noexcept;

  private:
    HostRequest numbered(HostRequest);
    [[noreturn]] void refuse(HostFailureKind, Advice why, const HostRequest&, const Configuration*);
    void record(HostFailureKind, Advice why, const HostRequest&, const Configuration*);
    HostFailure& record_unexplained(HostFailureKind, const HostRequest&,
                                    const Configuration*) noexcept;
    // PLUG-63: the accepted filter with the figures found for it when it
    // was accepted, replaced whole by an accepted load's commit. A new
    // figure is a member here, found in find_figures(), with its accessor.
    struct AcceptedFilter {
        std::shared_ptr<const Configuration> filter; // filter()
        double peak = 0;                             // filter_peak()
        double noise = 0;                            // filter_noise_gain()
        std::size_t tail = 0;                        // filter_tail()
        FilterGains gains;                           // gains()
        std::uint64_t identity = 0;                  // filter_identity()
        // Every figure of `filter`, gains() with the estimate only when
        // `estimating` (control thread; may throw std::bad_alloc).
        void find_figures(bool estimating);
    };
    std::size_t max_channels_;
    AcceptedFilter current_;
    bool estimating_ = false; // estimating()
    HostRequest accepted_;    // The request that supplied current_.filter (or the saved failure).
    // offer_name(): the name offered last, and the identity of its filter.
    struct OfferedName {
        std::string name;
        std::uint64_t identity = 0;
    };
    std::optional<OfferedName> offered_name_;
    std::uint64_t requests_ = 0;
    bool failed_ = false;
    bool awaiting_ = false;           // awaiting()
    std::uint64_t list_retaken_ = 0;  // take_unsettled_list()'s last request
    std::optional<HostLayout> heard_; // confirm()'s configuration
    std::optional<HostFailure> load_failure_, activation_failure_;
    Advice notice_;
    // faults(); the processor's totals at the last clear (the audio side's
    // baseline too) and at the last observe().
    HostFaults faults_, cleared_, observed_;
    bool changed_ = false; // HostFaultReport::changed for the next observe().
};

// DIAG-1: what a host's status display shows for an instance, for formats
// with no editor (a read-only parameter in VST3 and AU). The status follows
// failure(): a refused replacement shows "load failed" although the previous
// filter keeps playing, as LV2's status port does.
enum class HostStatus {
    // A filter is accepted and suits the host (PLUG-22: "Active", not
    // "Playing": the plug-in cannot tell whether the host's transport runs).
    active = 0,
    no_filter = 1, // An empty instance: identity, or silence for unequal counts.
    // The newest request failed, a saved failure was restored, or the
    // accepted filter could not start, so a refusal or silence needs the
    // user's attention.
    load_failed = 2,
    // "Waiting": a filter is accepted but not playing yet, either because
    // the host has sent no audio settings, or (PLUG-44) because they do not
    // suit it and it is kept until they do (HostFilter::waits_for_settings()).
    unchecked = 3,
    fault = 4, // HostFilter::faults(): the audio met a problem while playing.
    // PLUG-22: the host or the window bypasses an instance with a filter, and
    // nothing is wrong: the input plays unfiltered, or silence for unequal
    // counts. Without a filter the status stays No filter.
    bypassed = 5,
};
struct HostStatusLine {
    HostStatus status = HostStatus::no_filter;
    // One line of at most max_status_text characters (ASCII), saying what the
    // instance plays and, after a failure, why, in short; the full
    // explanation is HostFilter::failure()'s message, which adapters log.
    std::string text;
    bool operator==(const HostStatusLine&) const = default;
};
inline constexpr std::size_t max_status_text = 127; // VST3's String128, less its terminator.
// `host` is the configuration the instance plays under, or nullopt while it
// is not known (VST3 before activation, AU before initialization).
// `log_written` false (host_log_written()) says the Convolver log could not
// be written, instead of pointing to it. `settings` are the instance's Gain
// and Mix (PLUG-10): without a filter, or after a failed load, the text says
// what they let through (the input at a changed level, or the dry signal),
// not only what the filter does. `bypassed`: the instance is bypassed; a load
// failure or a fault still comes first, as both need attention, and when no
// filter is heard the text says what bypass plays (the untouched input where
// there is a dry signal; PLUG-36).
// `chooses_files`: the plug-in has a window that chooses filter files, so
// with no filter the text names choosing one before a preset (PLUG-33).
// `latency_in_use`: the latency the processor plays at, where the adapter
// knows it; when it differs from settings.latency, the Latency chosen is not
// in use yet, and the line says so (DIAG-10). The line also says when Estimated
// is chosen and the filter has no estimate, so Clip-safe's gain plays, or
// when the filter has no gain figure at all and plays silence.
// The status alone, in the order host_status() gives it (PLUG-39 step 4): a
// load failure (Waiting instead for a filter that waits for settings that
// suit it, HostFilter::waits_for_settings()), then a fault (unless a failed
// load left no filter), then Bypassed with a filter, then the filter (Active
// once the host's configuration is known, Waiting before), then a failed
// load, then No filter. `host_known`: the instance plays under a known configuration.
// Adapters with a status of their own (LV2's port) map this value rather
// than ordering the record again.
HostStatus host_status_value(const HostFilter&, bool host_known, bool bypassed = false) noexcept;
HostStatusLine host_status(const HostFilter&, const std::optional<HostLayout>& host,
                           bool log_written = true, const HostSettings& settings = {},
                           bool bypassed = false, bool chooses_files = false,
                           std::optional<std::size_t> latency_in_use = std::nullopt);
// Whether a dry signal exists under `host` (Mix's dry share and bypass pass
// input c to output c), and if not, why.
enum class HostDrySignal {
    present,
    // The filter's inputs and outputs differ: the one playing (PLUG-23: its
    // own counts, not the host's), or one kept that does not suit the host.
    filter_counts,
    // A load failed and no filter is kept (HostFilter::failed()): Convolver
    // cannot tell that the failed filter was not a crossover (jrp, 7
    // November 2026).
    failed_load,
    // No filter plays and the host's inputs and outputs differ.
    host_counts,
};
// A dry signal exists only when what plays has as many inputs as outputs:
// the filter when one plays; with none playing, the host, and a kept filter
// too. None after a failed load with no filter kept, with one output too
// (jrp, 7 November 2026). Allocates nothing.
HostDrySignal dry_signal_rule(const HostFilter&, const HostLayout& host) noexcept;
inline bool dry_signal(const HostFilter& filter, const HostLayout& host) noexcept {
    return dry_signal_rule(filter, host) == HostDrySignal::present;
}
// Before the host is known: failed_load when nothing can give a dry signal
// whatever the host (no filter kept after a failure), otherwise the filter's
// own counts (present with no filter). Status and the report both use it.
HostDrySignal dry_signal_before_host(const HostFilter&) noexcept;
// Why there is no dry signal, in words completing "..., so there is no dry
// signal" ("the filter's inputs and outputs differ"); empty when there is one.
const char* no_dry_reason(HostDrySignal) noexcept;

// Prepared planar host boundary. Float DSP with float/double host buffers.
// Input and output channel counts follow the configuration and may differ.
// PLUG-23: the processor may take more inputs than the configuration has
// (`inputs`, the host's input count); the filter reads the first ones and the
// others are ignored. Each filter keeps its own rule for the dry signal
// (Mix's dry share and bypass): input c to output c only when the filter
// itself has as many inputs as outputs, never because the host's counts are
// equal, so a 2-in, 6-out crossover on 6 inputs never sends unfiltered audio
// to an output.
// Control: construction, replace, prepare, publish, silence, collect,
// destruction; serialize these calls. Audio: process, set_bypass, reset, mute,
// publications; one audio thread per instance. Control and audio may overlap
// only for replace, prepare, publish, silence and collect. Teardown requires
// stopped audio.
class HostProcessor {
  public:
    // What `playback` says to play, with the dry signal it carries
    // (HostFilter's decision, or filter_playback()'s); never decided from the
    // channel counts here. A mono filter is laid out on each channel when
    // the playback's each_channel is set (PLUG-57). PLUG-10: the settings
    // apply from the first frame: gain and mix, and the latency as the block
    // size; throws std::invalid_argument if the latency is not one of
    // host_latencies.
    // PLUG-23: takes `inputs` input channels (at least the filter's; 0 means
    // the filter's). Replacements may have any input count from 1 to
    // `inputs`, and the same rate and outputs.
    HostProcessor(const HostPlayback& playback, std::size_t max_frames, const HostSettings& = {},
                  std::size_t inputs = 0);
    // A latency of any number of frames, not only one the plug-ins offer, for
    // tests and measurements: HostProcessor(playback, 64, 8). A class rather
    // than a bare count, so that HostProcessor(playback, 64, {}) means the
    // default settings, not a latency of 0 (found by PLUG-60's review).
    struct AnyLatency {
        AnyLatency(std::size_t frames) noexcept : frames(frames) {} // Implicit by design.
        std::size_t frames;
    };
    // The default settings at that latency.
    HostProcessor(const HostPlayback& playback, std::size_t max_frames, AnyLatency block);
    ~HostProcessor();
    HostProcessor(const HostProcessor&) = delete;
    HostProcessor& operator=(const HostProcessor&) = delete;
    // Prepare off-thread; latest pending state wins. A filter that is published
    // before any frame has been processed since construction or reset is
    // installed directly; otherwise the new filter warms up for one block and
    // crossfades over 5.33 ms (256 frames at 48 kHz). reset() ends a transition in progress (the
    // newer filter plays at once), so a filter published before audio resumes
    // is installed directly.
    void replace(const HostPlayback&);
    // replace() in two steps, so an adapter can check between them that the
    // request is still the newest (LV2-2). prepare() builds and plans the
    // filter off the audio thread and may throw; publish() hands it to the
    // audio thread exactly as replace() does and cannot fail. A Replacement
    // that is never published is simply discarded. Control calls, serialized
    // with replace() and collect(); publish only to the processor that
    // prepared it.
    class Replacement {
      public:
        Replacement(Replacement&&) noexcept;
        Replacement& operator=(Replacement&&) noexcept;
        ~Replacement();

      private:
        friend class HostProcessor;
        struct Data;
        explicit Replacement(std::unique_ptr<Data>) noexcept;
        std::unique_ptr<Data> data_;
    };
    Replacement prepare(const HostPlayback&) const;
    void publish(Replacement&&) noexcept;
    void collect() noexcept; // Retire old FFT plans off-thread.
    void reset() noexcept;
    void set_bypass(bool) noexcept;
    // PLUG-10, audio thread, like set_bypass: the gain in decibels (clamped
    // to min_gain_db..max_gain_db) and the mix (0 to 1, ignored for unequal
    // counts). Each change ramps over 1.33 ms (64 frames at 48 kHz, PLUG-17).
    void set_gain_db(double) noexcept;
    void set_mix(double) noexcept;
    // PLUG-10, audio thread: which gain plays. Estimated and Clip-safe play
    // the gains the playing filter's HostPlayback carried (0 dB where it
    // carried none, as filter_playback() gives), ramping like a Gain change;
    // a filter whose figure is absent plays silence in that mode. During a replacement each filter
    // plays at its own figure, so the crossfade covers the change.
    void set_gain_mode(GainMode) noexcept;
    // PLUG-10, control thread, serialized with replace() and publish(): new
    // figures for the filter published last (or built with), such as its
    // noise estimate found after it was published. Its engine keeps playing with its history, so
    // nothing is interrupted; the gain ramps like a Gain change. Does not
    // allocate or block.
    void update_gains(const FilterGains&) noexcept;
    // PLUG-38, control thread, serialized with replace() and publish(): for a
    // `playback` whose `playing` says HostFilter found its filter to be the
    // one this processor already plays, and a processor that has counted no
    // faults since the last clear_faults(), passes its gains to
    // update_gains() and returns true: the filter plays on with its history,
    // and settings applied afterwards ramp as usual. Otherwise changes
    // nothing and returns false, and the adapter calls replace(). Adapters
    // call it where a restored state or preset may hold the filter already
    // playing (docs/develop/plugin-contract.md#scope-of-each-change). Does
    // not allocate or block.
    bool keep(const HostPlayback& playback) noexcept;
    // PLUG-39: what an install callback does with what HostFilter::load()
    // gives it: keep() when it can, otherwise replace(), which may throw.
    void play(const HostPlayback& playback);
    // Audio thread: the current filter ramps to silence over 1.33 ms (or is
    // silent at once, before any audio since construction or reset) and
    // stays silent until a filter published after this call takes over,
    // which then fades in from silence as any replacement does; one published
    // before it (still waiting for the audio thread) is silenced too. The
    // filter's dry signal (bypass and Mix's dry share) ramps and stays silent
    // with it, with one output too: a failed first load passes nothing
    // (PLUG-23, dry_signal()).
    // For a failure the audio thread finds itself: LV2's run() refusing an
    // empty instance's first load (no worker, or a full worker queue). The
    // control side uses silence().
    void mute() noexcept;
    // The same for publications up to `through`, a count publications()
    // returned. An adapter that decides to mute from a flag its control side
    // clears before publishing reads the count first, then the flag, so a
    // filter published after the flag was cleared is never silenced. A count
    // older than the filter already playing mutes only the filter it is
    // still fading out, if that was published up to the count.
    void mute(std::uint64_t through) noexcept;
    // Control side (ROBUST-3): mute() for a failure the control side finds,
    // a failed first load, without allocating, locking or throwing, so the
    // silence cannot fail when memory has run out. The audio thread mutes at
    // the start of its next process() call, before it adopts a pending
    // filter: one published before this call stays silent, one published
    // after it takes over. With no audio since construction or reset, that
    // next call is silent from its first frame; otherwise the filter ramps to
    // silence over 1.33 ms (64 frames at 48 kHz). A call already running when this one is made
    // may still play a filter it adopted.
    void silence() noexcept;
    // The same for publications up to `through`, a count publications()
    // returned before a later publication (PLUG-42: HostFilter::load()
    // keeping a filter that does not suit the host reads the count, publishes
    // its silent playback, then calls this): what played before mutes over
    // 1.33 ms, also when the audio thread has already begun fading it out
    // for the later publication, which takes over as any replacement does.
    void silence(std::uint64_t through) noexcept;
    // Audio thread: how many filters replace() and publish() have handed
    // over so far.
    std::uint64_t publications() const noexcept;
    std::size_t latency_frames() const noexcept;
    std::size_t tail_frames() const noexcept;
    // DIAG-1: the faults process() has counted since construction. Any thread,
    // also while audio runs: each count is read on its own, so a snapshot
    // taken during a call may show one count of it and not another.
    HostFaults faults() const noexcept;
    // DIAG-1, the one place where an accepted load starts the fault count
    // afresh. Control side: takes a snapshot of faults() and makes it the
    // baseline that the audio-side calls below measure from, and returns it
    // for HostFilter::load(), which observes from the same snapshot.
    HostFaults clear_faults() noexcept;
    // Control side: while the status shows a load failure, which comes first
    // (host_status()), the audio thread must not show Fault over it.
    void hold_fault_signal(bool held) noexcept;
    // Any thread: faults counted since the last clear_faults() (all of them,
    // for a processor never cleared).
    bool faults_since_clear() const noexcept;
    // Audio thread: true when faults arrived since the last clear_faults(),
    // the adapter has not shown them yet, and no load failure holds the
    // signal. After the adapter has shown Fault, it calls fault_signalled(),
    // so each clear shows Fault at most once.
    bool fault_to_signal() const noexcept;
    void fault_signalled() noexcept;
    // Missing input channels mean silence; null output channels are discarded.
    // In-place input/output is supported. Nonfinite input/output is silenced.
    // False means invalid dimensions, with no state advance or output guarantee,
    // or an engine failure, with the output silenced. Each problem is counted
    // in faults(). The whole call flushes subnormal floats to zero (x86-64 and
    // AArch64) and restores the host thread's floating-point mode on return.
    bool process(std::span<const float* const>, std::span<float* const>, std::size_t) noexcept;
    bool process(std::span<const double* const>, std::span<double* const>, std::size_t) noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace convolver
