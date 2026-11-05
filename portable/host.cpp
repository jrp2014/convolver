// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include "advice.hpp"
#include "convolver_identities.hpp"
#include "host_log.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace convolver {
namespace {
std::string count(std::size_t n, const char* noun) {
    return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
}
std::string counts(std::size_t inputs, std::size_t outputs) {
    return count(inputs, "input") + " and " + count(outputs, "output");
}
using detail::Advice;
using detail::advice_text;
struct Mismatch {
    HostFailureKind kind;
    std::string why;
};
// DIAG-2: why a filter does not suit the host, or nothing if it does, as
// problem, cause and fix (advice.hpp). `filter` names it: "the filter", or
// "the saved filter" when the host starts one that was accepted before.
std::optional<Mismatch> mismatch(const Configuration& c, const HostLayout& host,
                                 const std::string& filter = "the filter") {
    if (c.sample_rate != host.sample_rate) {
        const auto made = std::to_string(c.sample_rate) + " Hz",
                   runs = std::to_string(host.sample_rate) + " Hz";
        return Mismatch{
            HostFailureKind::sample_rate,
            advice_text({filter + " is for " + made + ", and the host runs at " + runs,
                         "Convolver never resamples, so a filter plays only at the "
                         "rate it was made for.",
                         "Load a filter made for " + runs +
                             ", or set the host's project or audio device to " + made + ".",
                         {}})};
    }
    if (c.input_channels != host.inputs || c.output_channels != host.outputs) {
        const auto made = counts(c.input_channels, c.output_channels),
                   given = counts(host.inputs, host.outputs);
        Advice a{filter + " has " + made + ", and the plug-in has " + given,
                 "A filter plays only with the channel counts it was made for.",
                 "Give the plug-in " + made +
                     " in the host's track or routing settings, or load a filter made for " +
                     given + ".",
                 {}};
        // GarageBand runs effects with one channel on a mono track (jrp's
        // first GarageBand run, 25 October 2026). Only a stereo filter is
        // fixed by a stereo track.
        if (host.inputs == 1 && host.outputs == 1 && c.input_channels == 2 &&
            c.output_channels == 2)
            a.note = "Hosts run an effect with one channel on a mono track (GarageBand does): "
                     "put Convolver on a stereo track instead.";
        return Mismatch{HostFailureKind::channels, advice_text(a)};
    }
    return std::nullopt;
}
// WAV channel-mask bits 0 to 17, the speakers every format shares.
constexpr std::uint32_t shared_speakers = (1U << identity::speakers.size()) - 1;
static_assert(identity::speakers.size() == 18);
// A mask names speakers when it gives one known speaker per channel. One
// channel is never compared: a mono filter suits any single speaker.
bool names_speakers(std::uint32_t mask, std::size_t channels) {
    return channels > 1 && mask && !(mask & ~shared_speakers) &&
           static_cast<std::size_t>(std::popcount(mask)) == channels;
}
// What each WAV mask bit means, for explanations (abbreviations in the PLUG-4
// table: FL, FR, FC, ...).
constexpr std::array<const char*, 18> speaker_words{
    "front left",      "front right",      "front centre",      "subwoofer",
    "back left",       "back right",       "front left-centre", "front right-centre",
    "back centre",     "side left",        "side right",        "top centre",
    "top front left",  "top front centre", "top front right",   "top back left",
    "top back centre", "top back right"};
static_assert(speaker_words.size() == identity::speakers.size());
std::string speaker_name(std::size_t bit) {
    return std::string(speaker_words[bit]) + " (" + std::string(identity::speakers[bit].wav) + ")";
}
std::string speaker_list(std::uint32_t mask) {
    std::string names;
    for (std::size_t bit = 0; bit < identity::speakers.size(); ++bit)
        if (mask & (1U << bit)) {
            if (!names.empty())
                names += ' ';
            names += identity::speakers[bit].wav;
        }
    return names;
}
// The speaker of each channel, in bus order (bit order).
std::vector<std::size_t> speaker_bits(std::uint32_t mask) {
    std::vector<std::size_t> bits;
    for (std::size_t bit = 0; bit < identity::speakers.size(); ++bit)
        if (mask & (1U << bit))
            bits.push_back(bit);
    return bits;
}
// Layouts such as 5.1 and quadraphonic are written with back (BL BR) or side
// (SL SR) speakers for the same surround pair. In a layout with side speakers
// and no back ones, SL and SR are compared as BL and BR, channel by channel, so
// bus order still counts (6.1 FL FR FC LFE BC SL SR is not FL FR FC LFE BL BR BC).
std::size_t compared_as(std::size_t bit, std::uint32_t mask) {
    constexpr std::uint32_t back = 0x30;
    if (!(mask & back) && (bit == 9 || bit == 10))
        return bit - 5;
    return bit;
}
std::string hex(std::uint32_t n) {
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text;
    do {
        text.insert(text.begin(), digits[n % 16]);
        n /= 16;
    } while (n);
    return "0x" + text;
}
} // namespace

std::string preparation_message(const std::string& what) {
    return advice_text(
        {"the filter could not be prepared" +
             (what.empty() ? std::string() : ": " + detail::advice_problem(what)),
         "Memory may have run out, or the filter is too long for this computer.",
         "Close other projects or programs, then load the filter again; a shorter filter needs "
         "less memory. If it keeps failing, copy the diagnostics and report it.",
         {}});
}

const std::string& failure_message(const HostFailure& f) {
    static const std::string unrecorded =
        advice_text({"the load failed, and memory ran out before Convolver could record why",
                     "Memory had run out.",
                     "Close other projects or programs, then load the filter again.",
                     {}});
    return f.message.empty() ? unrecorded : f.message;
}

std::string state_read_message(const std::exception& e) {
    auto a = detail::parse_advice(e.what());
    a.problem = "the saved state could not be read: " + a.problem;
    if (a.fix.empty()) {
        a.cause = "The project or preset is damaged, or was not saved by Convolver.";
        a.fix = "Load the filter again, then save the project.";
    }
    return advice_text(a);
}

std::string speaker_names(std::uint32_t mask, std::size_t channels) {
    if (!names_speakers(mask, channels))
        return {};
    std::string names;
    for (const auto bit : speaker_bits(mask))
        names += (names.empty() ? "" : ", ") + std::string(speaker_words[bit]);
    return names;
}

std::string speaker_notice(const Configuration& c, const HostLayout& host) {
    const auto filter = c.channel_mask, bus = host.output_speakers;
    if (c.output_channels != host.outputs || !names_speakers(filter, c.output_channels) ||
        !names_speakers(bus, host.outputs))
        return {};
    std::string differences;
    const auto ours = speaker_bits(filter), theirs = speaker_bits(bus);
    for (std::size_t i = 0; i < ours.size(); ++i)
        if (compared_as(ours[i], filter) != compared_as(theirs[i], bus))
            differences += std::string(differences.empty() ? "" : "; ") + "output " +
                           std::to_string(i + 1) + " is filtered for the " + speaker_name(ours[i]) +
                           " speaker but plays on the " + speaker_name(theirs[i]);
    if (differences.empty())
        return {};
    return advice_text(
        {"the filter's speaker labels differ from the host's output bus",
         "The filter's outputs are labelled for the speakers " + speaker_list(filter) +
             " (channel mask " + hex(filter) + " in its .cfg), and the host's output bus carries " +
             speaker_list(bus) + ". Convolver sends output channels to the bus in order, so " +
             differences + ".",
         "If the outputs feed speakers, set the host's track or bus to " + speaker_list(filter) +
             ". If they are not speakers (crossover bands, for example), set the channel mask at "
             "the end of the .cfg's first line to 0, then load the .cfg again (or make the preset "
             "again from it) and save the project.",
         "The filter plays as loaded either way."});
}

Configuration unfiltered_configuration(const HostLayout& host, bool failed) {
    Configuration c;
    c.sample_rate = host.sample_rate;
    c.input_channels = host.inputs;
    c.output_channels = host.outputs;
    if (failed || host.inputs != host.outputs) {
        // One all-zero response: exact silence on every output, and no tail.
        c.paths.push_back({{0.f}, {{0, 1}}, {{0, 1}}});
        return c;
    }
    for (std::size_t i = 0; i < host.inputs; ++i)
        c.paths.push_back({{1.f}, {{i, 1}}, {{i, 1}}});
    return c;
}

const std::string& HostFilter::error() const noexcept {
    static const std::string none;
    const auto& f = failure();
    return f ? f->message : none;
}

std::uint64_t HostFilter::next_request() noexcept {
    return ++requests_;
}

HostRequest HostFilter::numbered(HostRequest request) {
    request.number = request.number ? request.number : requests_ + 1;
    requests_ = std::max(requests_, request.number);
    return request;
}

void HostFilter::fail_request() noexcept {
    if (!filter_)
        failed_ = true;
}

HostFailure& HostFilter::record_unexplained(HostFailureKind kind, const HostRequest& request,
                                            const Configuration* carried) noexcept {
    // ROBUST-7: what needs no memory, so that running out afterwards still
    // leaves the request failed: an instance with no filter fails (plays
    // silence, saves the failure), and the record names the request and the
    // kind of failure, without the message or the file's path.
    fail_request();
    HostFailure& f = load_failure_.emplace(); // Empty strings: nothing allocated.
    f.kind = kind;
    f.request.source = request.source;
    f.request.number = request.number;
    if (carried) {
        f.sample_rate = carried->sample_rate;
        f.inputs = carried->input_channels;
        f.outputs = carried->output_channels;
    }
    f.silent = !filter_;
    return f;
}

void HostFilter::record(HostFailureKind kind, std::string why, const HostRequest& request,
                        const Configuration* carried) {
    auto& f = record_unexplained(kind, request, carried);
    // DIAG-2: what failed, why and what to do; what plays instead follows
    // from `silent` (failure_consequence() in host_log.hpp).
    f.message = std::move(why);
    f.request = request; // May run out of memory; the record above stands.
}

namespace {
// Counts only increase, except that a new processor starts again from 0.
std::uint64_t since(std::uint64_t now, std::uint64_t before) noexcept {
    return now >= before ? now - before : now;
}
HostFaults since(const HostFaults& now, const HostFaults& before) noexcept {
    HostFaults fresh;
    fresh.rejected_input = since(now.rejected_input, before.rejected_input);
    fresh.output_overflow = since(now.output_overflow, before.output_overflow);
    fresh.processing_failure = since(now.processing_failure, before.processing_failure);
    fresh.invalid_call = since(now.invalid_call, before.invalid_call);
    return fresh;
}
HostFaults& operator+=(HostFaults& a, const HostFaults& b) noexcept {
    a.rejected_input += b.rejected_input;
    a.output_overflow += b.output_overflow;
    a.processing_failure += b.processing_failure;
    a.invalid_call += b.invalid_call;
    return a;
}
// A rejection load() has already recorded, told apart from any other
// std::invalid_argument thrown while checking.
class Refused : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};
} // namespace

void HostFilter::refuse(HostFailureKind kind, std::string why, const HostRequest& request,
                        const Configuration* carried) {
    record(kind, std::move(why), request, carried);
    throw Refused(load_failure_->message);
}

void HostFilter::load(HostState state, const std::optional<HostLayout>& host,
                      const std::function<void(const Configuration&)>& install, HostRequest request,
                      HostProcessor* counting) {
    request = numbered(std::move(request));
    auto& next = state.configuration;
    const Configuration* carried = next ? &*next : nullptr;
    // Everything that is kept is built before `install` publishes anything, so
    // that running out of memory rejects the request with the previous state
    // whole, and nothing after publication can throw: what plays and what is
    // saved never part (jrp's review of 0.4.30). Without the host's
    // configuration, activation checks the speakers.
    std::string notice;
    std::optional<HostFailure> saved_failure;
    double peak = 0;
    try {
        if (state.failed && next)
            refuse(HostFailureKind::state_unreadable,
                   advice_text({"the saved state is inconsistent: it records a failed load and "
                                "also holds a filter",
                                "The state was damaged, or written by another program.",
                                "Load the filter again, then save the project.",
                                {}}),
                   request, carried);
        if (next && (!next->input_channels || !next->output_channels ||
                     next->input_channels > max_channels_ || next->output_channels > max_channels_))
            refuse(HostFailureKind::channels,
                   advice_text({"the filter has " +
                                    counts(next->input_channels, next->output_channels) +
                                    ", which this plug-in cannot take",
                                "This plug-in takes 1 to " + std::to_string(max_channels_) +
                                    " channels on each side.",
                                "Load a filter with 1 to " + std::to_string(max_channels_) +
                                    " inputs and outputs, or render with the command-line tool, "
                                    "convolver, which takes more.",
                                {}}),
                   request, carried);
        // PLUG-12: what is accepted is saved, so a filter the saved state
        // cannot hold is refused here, before it plays.
        if (next)
            if (auto why = host_state_size_error(state); !why.empty())
                refuse(HostFailureKind::too_large, std::move(why), request, carried);
        if (host && next) {
            if (auto m = mismatch(*next, *host))
                refuse(m->kind, std::move(m->why), request, carried);
            notice = speaker_notice(*next, *host);
        }
        // For the window's Loudest peak; on the control side with the rest of
        // the preparation, so a report never has to compute it.
        if (next)
            peak = worst_case_peak(*next);
        if (state.failed) {
            HostFailure f;
            f.kind = HostFailureKind::saved_failure;
            f.request = request;
            f.silent = true;
            f.message =
                advice_text({"a filter had failed to load when this project or preset was saved",
                             "The saved state records the failure, not a filter.",
                             "Load a filter made for the host's sample rate and channels, then "
                             "save again.",
                             {}});
            saved_failure = std::move(f);
        }
        if (host && install) {
            const auto unfiltered =
                next ? Configuration{} : unfiltered_configuration(*host, state.failed);
            try {
                install(next ? *next : unfiltered);
            } catch (const SupersededRequest&) {
                throw; // Abandoned: nothing recorded or changed.
            } catch (const std::exception& e) {
                refuse(HostFailureKind::preparation, preparation_message(e.what()), request,
                       carried);
            }
        }
    } catch (const Refused&) {
        throw; // Recorded above.
    } catch (const SupersededRequest&) {
        throw;
    } catch (...) {
        // Out of memory building a message, or a non-standard exception:
        // recorded first without a message, in case that fails too (ROBUST-7).
        record_unexplained(HostFailureKind::preparation, request, carried);
        refuse(HostFailureKind::preparation, preparation_message({}), request, carried);
    }
    // The commit, which cannot throw.
    static_assert(std::is_nothrow_move_assignable_v<std::optional<Configuration>> &&
                  std::is_nothrow_move_assignable_v<HostRequest> &&
                  std::is_nothrow_move_assignable_v<std::optional<HostFailure>> &&
                  std::is_nothrow_move_assignable_v<std::string>);
    filter_ = std::move(next);
    peak_ = peak;
    failed_ = state.failed;
    accepted_ = std::move(request);
    load_failure_ = std::move(saved_failure);
    activation_failure_.reset();
    notice_ = std::move(notice);
    // A new filter, or a new start for the same one: Fault clears. One
    // snapshot of the processor's counts is both the audio side's new
    // baseline and the one observe() measures from, so faults counted while
    // the filter was prepared are not shown later against the new filter
    // (jrp's review of 0.4.43). They are still explained by the next observe().
    faults_ = {};
    if (counting) {
        // Held first, so the audio side never shows Fault over a restored
        // load failure, even before the adapter updates its status.
        counting->hold_fault_signal(failure_shown());
        const auto snapshot = counting->clear_faults();
        unshown_ += since(snapshot, observed_);
        observed_ = snapshot;
    }
}

void HostFilter::reject(HostFailureKind kind, std::string why, HostRequest request,
                        const Configuration* carried) noexcept {
    try {
        record(kind, std::move(why), numbered(std::move(request)), carried);
    } catch (...) { // Out of memory for the path: recorded without it (ROBUST-7).
    }
}

Configuration HostFilter::activate(const HostLayout& host) {
    notice_.clear();
    activation_failure_.reset();
    faults_ = {};
    observed_ = {}; // The adapter builds a new processor, which counts from 0.
    if (!filter_)
        return unfiltered_configuration(host, failed_);
    if (auto m = mismatch(*filter_, host, "the saved filter")) {
        HostFailure f;
        f.kind = m->kind;
        f.request = accepted_;
        f.sample_rate = filter_->sample_rate;
        f.inputs = filter_->input_channels;
        f.outputs = filter_->output_channels;
        f.silent = true;
        f.message = std::move(m->why);
        activation_failure_ = std::move(f);
        return unfiltered_configuration(host, true);
    }
    notice_ = speaker_notice(*filter_, host);
    return *filter_;
}

void HostFilter::fail_activation(std::string why) {
    HostFailure f;
    f.kind = HostFailureKind::activation;
    f.request = accepted_;
    if (filter_) {
        f.sample_rate = filter_->sample_rate;
        f.inputs = filter_->input_channels;
        f.outputs = filter_->output_channels;
    }
    f.silent = true;
    f.message = advice_text(
        {"the filter could not be started when the host started the plug-in" +
             (why.empty() ? std::string() : ": " + detail::advice_problem(why)),
         why.empty() ? "Memory may have run out."
                     : "Usually memory has run out; the words after the colon are the reason "
                       "given.",
         "Close other projects or programs, then restart playback or load the filter again. If "
         "it keeps failing, copy the diagnostics and report it.",
         {}});
    notice_.clear();
    activation_failure_ = std::move(f);
}

namespace {
std::string times(std::uint64_t n) {
    return n == 1 ? "once" : std::to_string(n) + " times";
}
} // namespace

HostFaultReport HostFilter::observe(const HostFaults& totals) noexcept {
    const auto current = since(totals, observed_);
    observed_ = totals;
    faults_ += current;
    const auto earlier = unshown_; // Counted before the accepted load: explained, not shown.
    unshown_ = {};
    return {current, earlier, failure_shown()};
}

namespace {
// DIAG-2: each kind of fault as problem, cause and fix, the most serious
// first (as short_fault() ranks them).
std::vector<Advice> fault_kinds(const HostFaults& f) {
    std::vector<Advice> kinds;
    if (f.processing_failure)
        kinds.push_back({"the filter failed while processing (" + times(f.processing_failure) + ")",
                         "This is a fault in Convolver, not in your audio or settings. Convolver "
                         "silenced the audio and restarted the filter each time.",
                         "Copy the diagnostics and report it, with a line on what was playing.",
                         {}});
    if (f.output_overflow)
        kinds.push_back(
            {"the output became too large to represent (" + times(f.output_overflow) + ")",
             "The input level or the filter's gain is extreme, so Convolver silenced "
             "the output and restarted the filter.",
             "Lower the level going into Convolver, or the filter's gain (Gain, or "
             "the attenuation in its .cfg), then load the filter again.",
             {}});
    if (f.invalid_call)
        kinds.push_back({"the host passed more frames or channels than it had set up (" +
                             times(f.invalid_call) + ")",
                         "Convolver refused those calls, which played silence. This is a fault "
                         "in the host.",
                         "Report it to the host's developers, with the diagnostics.",
                         {}});
    if (f.rejected_input)
        kinds.push_back({"the host sent invalid samples (" + times(f.rejected_input) + ")",
                         "Samples that are not a number, infinite or beyond the 32-bit float "
                         "range usually come from a plug-in or source before Convolver. Convolver "
                         "played them as silence.",
                         "Bypass the plug-ins before Convolver one at a time to find the one "
                         "that makes them.",
                         {}});
    return kinds;
}
// Each kind on the log's one line.
std::string fault_lines(const HostFaults& f) {
    std::string text;
    for (const auto& kind : fault_kinds(f))
        text += (text.empty() ? "" : " ") + detail::advice_line(advice_text(kind));
    return text;
}
constexpr const char* fault_clears =
    "The status shows Fault until a filter is loaded again or the host restarts the plug-in.";
// The current filter's faults while the status shows Load failed, which comes
// first (the 0.5.2 review: the log said Fault was shown).
constexpr const char* fault_held =
    "The status shows Load failed instead of Fault, because a load failure comes first; these "
    "faults are cleared when a filter is loaded or the host restarts the plug-in.";
} // namespace

detail::Advice fault_advice(const HostFaults& f, bool held) {
    auto kinds = fault_kinds(f);
    if (kinds.empty())
        return {};
    auto advice = kinds.front();
    std::string also;
    for (std::size_t i = 1; i < kinds.size(); ++i)
        also += (also.empty() ? "" : "; ") + kinds[i].problem;
    if (!also.empty())
        advice.note = detail::as_sentence("Also: " + also) + " ";
    advice.note += held ? fault_held : fault_clears;
    return advice;
}

std::string fault_message(const HostFaults& f) {
    if (!f.any())
        return {};
    return "While audio played: " + fault_lines(f) + " " + fault_clears;
}

std::string fault_message(const HostFaultReport& r) {
    std::string text;
    if (r.earlier.any())
        text = "While the filter now playing was being loaded, the audio before it met a "
               "problem: " +
               fault_lines(r.earlier) +
               " The new filter starts afresh, so the status does not show these.";
    if (r.current.any())
        text += (text.empty() ? "" : " ") +
                (r.held ? "While audio played: " + fault_lines(r.current) + " " + fault_held
                        : fault_message(r.current));
    return text;
}

namespace {
float gain_from_db(double db) noexcept {
    return static_cast<float>(std::pow(10.0, db / 20));
}
double clamp_gain_db(double db) noexcept {
    return std::isnan(db) ? 0 : std::clamp(db, min_gain_db, max_gain_db);
}
double clamp_mix(double mix) noexcept {
    return std::isnan(mix) ? 1 : std::clamp(mix, 0.0, 1.0);
}
} // namespace

UnfilteredSound unfiltered_sound(const HostSettings& settings, bool identity) {
    const double mix = clamp_mix(settings.mix), db = clamp_gain_db(settings.gain_db);
    UnfilteredSound sound;
    sound.silent = !identity && mix == 1;
    sound.unchanged = mix == 0 || (identity && db == 0);
    if (sound.silent || sound.unchanged)
        return sound;
    // As HostProcessor::process() mixes them: Gain on the filtered path (the
    // input itself, or silence), and the rest of Mix as the dry input.
    const double level = (identity ? mix * gain_from_db(db) : 0.0) + (1 - mix);
    // One decimal, or two significant digits for a change too small to show
    // in one ("-0.0087 dB", not "-0.0 dB").
    const double db_out = 20 * std::log10(level);
    char text[48] = {};
    std::snprintf(text, sizeof text, std::abs(db_out) < .05 ? "%+.2g dB" : "%+.1f dB", db_out);
    sound.level = text;
    return sound;
}

namespace {
// The short reason of a processing fault, the most serious first.
std::string short_fault(const HostFaults& f) {
    if (f.processing_failure)
        return "the filter failed while playing and was restarted.";
    if (f.output_overflow)
        return "output too large, silenced and the filter restarted.";
    if (f.invalid_call)
        return "the host passed buffers that do not match its setup.";
    return "invalid input samples (NaN or infinite) played as silence.";
}
std::string ports(std::size_t inputs, std::size_t outputs) {
    return std::to_string(inputs) + " in, " + std::to_string(outputs) + " out";
}
// The short reason of a failure, completing "Silent: " or "Load failed, ...: ".
std::string short_reason(const HostFailure& f, const std::optional<HostLayout>& host) {
    switch (f.kind) {
    case HostFailureKind::filter_file:
        return "the filter file could not be read.";
    case HostFailureKind::state_unreadable:
        return "the preset or saved state could not be read.";
    case HostFailureKind::state_newer:
        return "the preset was saved by a newer Convolver.";
    case HostFailureKind::sample_rate:
        if (f.sample_rate <= 0) // Not recorded: say only what is known.
            return host ? "the filter is not for the host's " + std::to_string(host->sample_rate) +
                              " Hz."
                        : "the filter is for another rate.";
        return "the filter is for " + std::to_string(f.sample_rate) + " Hz" +
               (host ? ", the host runs at " + std::to_string(host->sample_rate) + " Hz." : ".");
    case HostFailureKind::channels:
        if (f.inputs == 0 && f.outputs == 0) // Not recorded (jrp's GarageBand run).
            return host ? "the filter does not suit the plug-in's " +
                              ports(host->inputs, host->outputs) + "."
                        : "the filter has other channel counts.";
        return "the filter has " + ports(f.inputs, f.outputs) +
               (host && (host->inputs != f.inputs || host->outputs != f.outputs)
                    ? ", the plug-in " + ports(host->inputs, host->outputs) + "."
                    : ".");
    case HostFailureKind::preparation:
        return "the filter could not be prepared.";
    case HostFailureKind::activation:
        return "the plug-in could not start the filter.";
    case HostFailureKind::saved_failure:
        return "a load had failed when this was saved.";
    case HostFailureKind::too_large:
        return "the filter is too large to save with the project.";
    }
    return "the filter could not be loaded.";
}
} // namespace

HostStatusLine host_status(const HostFilter& filter, const std::optional<HostLayout>& host,
                           bool log_written, const HostSettings& settings) {
    const char* details =
        log_written ? " Details in the Convolver log." : " The log could not be written.";
    HostStatusLine line;
    // PLUG-10: Mix lets the dry input through only with equal counts, which
    // are known once the host has started the instance.
    const bool dry_known = host && host->inputs == host->outputs;
    const bool dry_unknown = !host && clamp_mix(settings.mix) < 1;
    // What plays with no filter heard, before the reason: "Silent: " only
    // when Mix lets no dry signal through.
    const auto silent_words = [&](bool newest) -> std::string {
        if (dry_unknown)
            return newest ? "Load failed, no filter plays: " : "No filter plays: ";
        if (dry_known && !unfiltered_sound(settings, false).silent)
            return newest ? "Load failed, only the dry signal plays: "
                          : "Only the dry signal plays: ";
        return "Silent: ";
    };
    if (const auto& f = filter.failure()) {
        line.status = HostStatus::load_failed;
        // A filter the adapter could not start has no processor, so nothing
        // plays, not even Mix's dry share (the independent review).
        line.text = (!f->silent ? "Load failed, previous filter kept: "
                     : f->kind == HostFailureKind::activation ? std::string("Silent: ")
                                                              : silent_words(true)) +
                    short_reason(*f, host) + details;
    } else if (filter.faults().any() && !filter.failed()) {
        line.status = HostStatus::fault;
        line.text = "Fault: " + short_fault(filter.faults()) + details;
    } else if (const auto& c = filter.filter()) {
        line.status = host ? HostStatus::playing : HostStatus::unchecked;
        line.text = "Filter loaded: " + std::to_string(c->sample_rate) + " Hz, " +
                    ports(c->input_channels, c->output_channels) +
                    (host ? "." : "; checked when the host starts it.");
        if (host && !filter.notice().empty())
            line.text += log_written ? " Speaker labels differ; see the Convolver log."
                                     : " Speaker labels differ.";
        if (dry_known && clamp_mix(settings.mix) == 0)
            line.text += " Mix 0%: only the dry signal plays.";
    } else if (filter.failed()) {
        line.status = HostStatus::load_failed;
        line.text = silent_words(false) + "an earlier load failed." + details;
    } else {
        line.status = HostStatus::no_filter;
        if (!host)
            line.text = "No filter loaded. Load a Convolver preset.";
        else if (!dry_known)
            line.text = "No filter: silent, as inputs and outputs differ. Load a Convolver preset.";
        else if (const auto sound = unfiltered_sound(settings, true); !sound.unchanged)
            line.text = "No filter: audio passes at " + sound.level +
                        " (Gain and Mix). Load a Convolver preset.";
        else
            line.text = "No filter: audio passes unchanged. Load a Convolver preset.";
    }
    if (line.text.size() > max_status_text)
        line.text.resize(max_status_text);
    return line;
}

// PLUG-17: HostProcessor's transitions last the same time at every sample
// rate. Gain, mix, bypass and mute ramp linearly for 64 frames' worth at
// 48 kHz (1.33 ms), and a replacement, after playing for the Latency block,
// fades in for 256 frames' worth (5.33 ms), so 48 kHz plays exactly as the
// fixed frame counts did (PLUG-16).
namespace {
constexpr int reference_rate = 48000;
constexpr std::size_t setting_ramp_at_reference = 64;
constexpr std::size_t crossfade_at_reference = 256;
// Frames at `rate` lasting as long as `frames` at the reference rate, to the
// nearest frame and at least 1.
constexpr std::size_t frames_at(int rate, std::size_t frames) noexcept {
    if (rate <= 0)
        return frames; // The engine refuses the rate anyway.
    const auto scaled = (static_cast<std::uint64_t>(rate) * frames + reference_rate / 2) /
                        static_cast<std::uint64_t>(reference_rate);
    return std::max<std::size_t>(1, static_cast<std::size_t>(scaled));
}
static_assert(frames_at(48000, 64) == 64 && frames_at(48000, 256) == 256);
static_assert(frames_at(44100, 64) == 59 && frames_at(96000, 256) == 512);
static_assert(frames_at(8000, 64) == 11 && frames_at(768000, 256) == 4096);
} // namespace
struct HostProcessor::Impl {
    struct Prepared {
        Engine engine;
        Prepared* next_retired = nullptr;
        std::uint64_t number = 0; // Publications are numbered from 1, in order.
        explicit Prepared(const Configuration& c, std::size_t block) : engine(c, block) {}
    };
    static_assert(std::atomic<Prepared*>::is_always_lock_free);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    const int rate;
    const std::size_t inputs, outputs, maximum, block;
    // PLUG-17: the ramps' and the crossfade's lengths at this rate.
    const std::size_t setting_ramp = frames_at(rate, setting_ramp_at_reference);
    const std::size_t crossfade = frames_at(rate, crossfade_at_reference);
    const float setting_ramp_step = 1.f / static_cast<float>(setting_ramp);
    Prepared* current = nullptr;
    Prepared* previous = nullptr;
    std::atomic<Prepared*> pending{nullptr}, retired{nullptr};
    std::atomic<std::size_t> tail{0};
    std::atomic<std::uint64_t> published{0}; // Publications so far (control writes).
    // DIAG-1: HostFaults, counted by the audio thread and read anywhere.
    std::atomic<std::uint64_t> rejected_input{0}, output_overflow{0}, processing_failure{0},
        invalid_call{0};
    // The fault total at the last clear_faults() (control writes), and at the
    // last fault_signalled() (audio thread writes).
    std::atomic<std::uint64_t> faults_cleared{0}, faults_signalled{0};
    std::atomic<bool> signal_held{false};
    std::uint64_t fault_total() const noexcept {
        return rejected_input.load(std::memory_order_relaxed) +
               output_overflow.load(std::memory_order_relaxed) +
               processing_failure.load(std::memory_order_relaxed) +
               invalid_call.load(std::memory_order_relaxed);
    }
    static void count(std::atomic<std::uint64_t>& n) noexcept {
        // One writer (the audio thread), so a load and a store suffice.
        n.store(n.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
    }
    std::vector<float> input, wet, old_wet, dry;
    std::size_t dry_position = 0, warm = 0, fade = 0;
    // PLUG-10: the gain and mix the adapter set (audio thread), and the values
    // playing, which ramp to them over setting_ramp frames.
    double set_db = 0;
    std::size_t gain_ramp = 0, mix_ramp = 0;
    float set_gain = 1, gain = 1, gain_target = 1, gain_step = 0;
    float mix_setting = 1, mix = 1, mix_target = 1, mix_step = 0;
    float dry_mix = 0;
    // mute(): the current filter's level ramps to 0; the level of the filter
    // being faded out keeps ramping if it was muted. Exactly 1 otherwise, so
    // an unmuted filter's output is unchanged.
    float level = 1, previous_level = 1;
    bool muted = false, previous_muted = false;
    // Publications up to this number were made before mute(): adopting one
    // keeps the silence. Only a filter published after it ends the silence.
    std::uint64_t muted_through = 0;
    // silence() (ROBUST-3): the publications to mute, plus one, for the
    // audio thread to apply at its next call; 0 when none is waiting.
    std::atomic<std::uint64_t> silence_through{0};
    bool bypass = false;
    bool started = false; // Audio processed since construction or reset.
    Impl(const Configuration& c, std::size_t maximum_, std::size_t block_,
         const HostSettings& settings = {})
        : rate(c.sample_rate), inputs(c.input_channels), outputs(c.output_channels),
          maximum(maximum_), block(block_) {
        if (!inputs || !outputs || inputs > max_channel_count || outputs > max_channel_count ||
            !maximum || maximum > max_block_frames ||
            std::max(inputs, outputs) * maximum > (1U << 24))
            throw std::invalid_argument("invalid host channel count or callback capacity");
        auto prepared = std::make_unique<Prepared>(c, block);
        input.resize(inputs * maximum);
        wet.resize(outputs * maximum);
        old_wet.resize(wet.size());
        dry.resize(outputs * block);
        tail.store(prepared->engine.tail_frames());
        current = prepared.release();
        set_db = clamp_gain_db(settings.gain_db);
        set_gain = gain = gain_target = gain_from_db(set_db);
        mix_setting = static_cast<float>(clamp_mix(settings.mix));
        mix = mix_target = target_mix();
    }
    float target_mix() const noexcept {
        return inputs == outputs ? mix_setting : 1.f;
    }
    // Audio thread, at the start of a call: a new gain or mix starts a ramp.
    void retarget() noexcept {
        if (set_gain != gain_target) {
            gain_target = set_gain;
            gain_step = (gain_target - gain) / setting_ramp;
            gain_ramp = setting_ramp;
        }
        if (const auto t = target_mix(); t != mix_target) {
            mix_target = t;
            mix_step = (t - mix) / setting_ramp;
            mix_ramp = setting_ramp;
        }
    }
    ~Impl() {
        delete current;
        delete previous;
        delete pending.load();
        collect();
    }
    void collect() noexcept {
        auto* item = retired.exchange(nullptr, std::memory_order_acq_rel);
        while (item) {
            auto* next = item->next_retired;
            delete item;
            item = next;
        }
    }
    std::unique_ptr<Prepared> prepare(const Configuration& c) const {
        if (c.sample_rate != rate || c.input_channels != inputs || c.output_channels != outputs)
            throw std::invalid_argument("replacement must preserve sample rate and channels");
        return std::make_unique<Prepared>(c, block);
    }
    void publish(std::unique_ptr<Prepared> prepared) noexcept {
        collect();
        // Conservative tail never shrinks during a session, including transitions.
        tail.store(std::max(tail.load(), prepared->engine.tail_frames()));
        // Counted before it is handed over: an audio thread that sees the new
        // count also sees what the control side did before publishing.
        prepared->number = published.load(std::memory_order_relaxed) + 1;
        published.store(prepared->number, std::memory_order_release);
        delete pending.exchange(prepared.release(), std::memory_order_acq_rel);
    }
    void replace(const Configuration& c) {
        publish(prepare(c));
    }
    // The audio producer only links; the control consumer owns deletion.
    // Collection must never hold up adoption of another pending filter.
    void retire(Prepared* item) noexcept {
        auto* head = retired.load(std::memory_order_relaxed);
        do {
            item->next_retired = head;
        } while (!retired.compare_exchange_strong(head, item, std::memory_order_release,
                                                  std::memory_order_relaxed));
    }
    void reset() noexcept {
        started = false;
        // A reset breaks the stream, so a transition in progress has nothing
        // left to fade: the newer filter plays at once, and a filter published
        // before the next audio is installed directly. The older filter is only
        // linked here; the control thread deletes it.
        if (previous) {
            retire(previous);
            previous = nullptr;
        }
        current->engine.reset();
        level = muted ? 0.f : 1.f;
        previous_level = 1;
        previous_muted = false;
        std::fill(dry.begin(), dry.end(), 0.f);
        dry_position = 0;
        warm = 0;
        fade = 0;
        dry_mix = bypass ? 1.f : 0.f;
        // The stream starts again: the settings apply at once, without ramps.
        gain = gain_target = set_gain;
        mix = mix_target = target_mix();
        gain_ramp = mix_ramp = 0;
    }
    // Audio thread. Before any audio since construction or reset there is
    // nothing to ramp from, so the silence starts at once: the first frame
    // heard is already silent, as a filter published then is installed
    // directly.
    void mute(std::uint64_t through) noexcept {
        // A filter published after `through` is already playing: nothing to mute.
        if (current && current->number > through)
            return;
        muted = true;
        muted_through = std::max(muted_through, through);
        if (!started)
            level = 0;
    }
    template <class T>
    bool process(std::span<const T* const> in, std::span<T* const> out,
                 std::size_t frames) noexcept {
        if (frames > maximum || in.size() > inputs || out.size() > outputs) {
            count(invalid_call);
            return false;
        }
        // Before adopting a pending filter, so that one published after
        // silence() ends it and one published before stays silent.
        if (const auto through = silence_through.exchange(0, std::memory_order_acquire))
            mute(through - 1);
        if (!frames)
            return true;
        if (!previous) {
            if (auto next = pending.exchange(nullptr, std::memory_order_acq_rel)) {
                if (started) {
                    previous = current;
                    previous_level = level;
                    previous_muted = muted;
                    warm = block;
                    fade = 0;
                } else {
                    // Nothing has been heard yet (initial restoration): install
                    // directly, so output follows the filter from the first frame.
                    retire(current);
                }
                current = next;
                // A filter published before mute() is as silenced as the
                // one it replaces (jrp's contract: silent until the next
                // published filter takes over; PLUG-7 found the adoption of an
                // earlier publication lifting it).
                muted = muted && next->number <= muted_through;
                level = muted ? 0.f : 1.f;
            }
        }
        retarget();
        started = true;
        bool invalid_input = false;
        for (std::size_t t = 0; t < frames; ++t)
            for (std::size_t c = 0; c < inputs; ++c) {
                double v = c < in.size() && in[c] ? in[c][t] : 0;
                const bool valid =
                    std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max();
                invalid_input = invalid_input || !valid;
                input[t * inputs + c] = valid ? static_cast<float>(v) : 0.f;
            }
        if (invalid_input)
            count(rejected_input);
        // Prepared, valid nonoverlapping spans and finite input satisfy Engine's
        // checked entry point; defensive containment keeps exceptions inside host.
        try {
            current->engine.process(input, wet, frames);
            if (previous)
                previous->engine.process(input, old_wet, frames);
        } catch (...) {
            count(processing_failure);
            reset();
            for (auto channel : out)
                if (channel)
                    std::fill_n(channel, frames, T(0));
            return false;
        }
        bool invalid_output = false;
        for (std::size_t t = 0; t < frames; ++t) {
            float blend = 1;
            if (previous) {
                if (warm) {
                    --warm;
                    blend = 0;
                } else {
                    fade = std::min(crossfade, fade + 1);
                    blend = static_cast<float>(fade) / crossfade;
                }
            }
            dry_mix =
                std::clamp(dry_mix + (bypass ? setting_ramp_step : -setting_ramp_step), 0.f, 1.f);
            if (gain_ramp && --gain_ramp)
                gain += gain_step;
            else
                gain = gain_target;
            if (mix_ramp && --mix_ramp)
                mix += mix_step;
            else
                mix = mix_target;
            if (muted)
                level = std::max(0.f, level - setting_ramp_step);
            if (previous_muted)
                previous_level = std::max(0.f, previous_level - setting_ramp_step);
            for (std::size_t c = 0; c < outputs; ++c) {
                // Bypass passes input c to output c when the counts are equal. With
                // unequal counts it plays silence on every output (owner's decision,
                // 22 October 2026): no channel has an unfiltered counterpart, and a
                // crossover must not send full-range audio to a driver.
                auto index = t * outputs + c;
                auto& delayed = dry[dry_position * outputs + c];
                float dry_value = delayed;
                delayed = inputs == outputs ? input[t * inputs + c] : 0.f;
                double value = level * static_cast<double>(wet[index]);
                if (previous)
                    value = (1.0 - blend) * previous_level * old_wet[index] + blend * value;
                // Gain 1 and mix 1 (the defaults) leave every value exactly
                // as it was before the settings existed.
                value *= gain;
                if (mix != 1)
                    value = mix * value + (1.0 - mix) * dry_value;
                value = (1.0 - dry_mix) * value + dry_mix * dry_value;
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
                    value = 0;
                    invalid_output = true;
                }
                if (c < out.size() && out[c])
                    out[c][t] = static_cast<T>(value);
            }
            if (++dry_position == block)
                dry_position = 0;
        }
        if (invalid_output) {
            count(output_overflow);
            reset();
        }
        if (previous && !warm && fade == crossfade) {
            retire(previous);
            previous = nullptr;
            previous_level = 1;
            previous_muted = false;
        }
        return true;
    }
};
HostProcessor::HostProcessor(const Configuration& c, std::size_t maximum, std::size_t block)
    : impl_(std::make_unique<Impl>(c, maximum, block)) {}
namespace {
std::size_t offered_latency(std::size_t frames) {
    if (std::find(host_latencies.begin(), host_latencies.end(), frames) == host_latencies.end())
        throw std::invalid_argument("the latency " + std::to_string(frames) +
                                    " frames is not one the plug-ins offer (64 to 4096 frames, "
                                    "in powers of two)");
    return frames;
}
} // namespace
HostProcessor::HostProcessor(const Configuration& c, std::size_t maximum,
                             const HostSettings& settings)
    : impl_(std::make_unique<Impl>(c, maximum, offered_latency(settings.latency), settings)) {}
HostProcessor::~HostProcessor() = default;
void HostProcessor::replace(const Configuration& c) {
    impl_->replace(c);
}
struct HostProcessor::Replacement::Data {
    const Impl* owner = nullptr;
    std::unique_ptr<Impl::Prepared> prepared;
};
HostProcessor::Replacement::Replacement(std::unique_ptr<Data> data) noexcept
    : data_(std::move(data)) {}
HostProcessor::Replacement::Replacement(Replacement&&) noexcept = default;
HostProcessor::Replacement& HostProcessor::Replacement::operator=(Replacement&&) noexcept = default;
HostProcessor::Replacement::~Replacement() = default;
HostProcessor::Replacement HostProcessor::prepare(const Configuration& c) const {
    auto prepared = impl_->prepare(c);
    // Member by member: Apple Clang lacks parenthesized aggregate initialization.
    auto data = std::make_unique<Replacement::Data>();
    data->owner = impl_.get();
    data->prepared = std::move(prepared);
    return Replacement(std::move(data));
}
void HostProcessor::publish(Replacement&& r) noexcept {
    // A Replacement from another processor, or one already published, is ignored.
    if (r.data_ && r.data_->owner == impl_.get() && r.data_->prepared)
        impl_->publish(std::move(r.data_->prepared));
    r.data_.reset();
}
void HostProcessor::collect() noexcept {
    impl_->collect();
}
void HostProcessor::reset() noexcept {
    impl_->reset();
}
void HostProcessor::set_bypass(bool value) noexcept {
    impl_->bypass = value;
}
void HostProcessor::set_gain_db(double db) noexcept {
    db = clamp_gain_db(db);
    if (db == impl_->set_db)
        return;
    impl_->set_db = db;
    impl_->set_gain = gain_from_db(db);
}
void HostProcessor::set_mix(double mix) noexcept {
    impl_->mix_setting = static_cast<float>(clamp_mix(mix));
}
std::uint64_t HostProcessor::publications() const noexcept {
    return impl_->published.load(std::memory_order_acquire);
}
void HostProcessor::mute() noexcept {
    mute(publications());
}
void HostProcessor::mute(std::uint64_t through) noexcept {
    impl_->mute(through);
}
void HostProcessor::silence() noexcept {
    // The control side is the only publisher, so this count is exact.
    impl_->silence_through.store(impl_->published.load(std::memory_order_relaxed) + 1,
                                 std::memory_order_release);
}
std::size_t HostProcessor::latency_frames() const noexcept {
    return impl_->block;
}
std::size_t HostProcessor::tail_frames() const noexcept {
    return impl_->tail.load();
}
HostFaults HostProcessor::faults() const noexcept {
    HostFaults f;
    f.rejected_input = impl_->rejected_input.load(std::memory_order_relaxed);
    f.output_overflow = impl_->output_overflow.load(std::memory_order_relaxed);
    f.processing_failure = impl_->processing_failure.load(std::memory_order_relaxed);
    f.invalid_call = impl_->invalid_call.load(std::memory_order_relaxed);
    return f;
}
HostFaults HostProcessor::clear_faults() noexcept {
    const auto snapshot = faults();
    impl_->faults_cleared.store(snapshot.total(), std::memory_order_release);
    return snapshot;
}
void HostProcessor::hold_fault_signal(bool held) noexcept {
    impl_->signal_held.store(held, std::memory_order_release);
}
bool HostProcessor::faults_since_clear() const noexcept {
    return impl_->fault_total() > impl_->faults_cleared.load(std::memory_order_acquire);
}
bool HostProcessor::fault_to_signal() const noexcept {
    const auto cleared = impl_->faults_cleared.load(std::memory_order_acquire);
    return impl_->fault_total() > cleared &&
           impl_->faults_signalled.load(std::memory_order_relaxed) <= cleared &&
           !impl_->signal_held.load(std::memory_order_acquire);
}
void HostProcessor::fault_signalled() noexcept {
    impl_->faults_signalled.store(impl_->fault_total(), std::memory_order_relaxed);
}
bool HostProcessor::process(std::span<const float* const> in, std::span<float* const> out,
                            std::size_t n) noexcept {
    return impl_->process(in, out, n);
}
bool HostProcessor::process(std::span<const double* const> in, std::span<double* const> out,
                            std::size_t n) noexcept {
    return impl_->process(in, out, n);
}
} // namespace convolver
