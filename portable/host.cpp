// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include "advice.hpp"
#include "convolver_identities.hpp"
#include "host_common.hpp"
#include "host_log.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace convolver {
using detail::clamp_gain_db;
using detail::clamp_mix;
using detail::gain_from_db;
using detail::own_dry;
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
    Advice why;
};
// DIAG-2: why a filter does not suit the host, or nothing if it does, as
// problem, cause and fix (advice.hpp). `filter` names it: "the filter", or
// "the saved filter" when the host starts one that was accepted before.
std::optional<Mismatch> mismatch(const Configuration& c, const HostLayout& host,
                                 const std::string& filter = "the filter") {
    if (c.sample_rate != host.sample_rate || host.fractional_rate != 0) {
        const auto made = std::to_string(c.sample_rate) + " Hz", runs = host_rate_text(host);
        if (host.fractional_rate != 0) // VST3-9: no filter can be made for it.
            return Mismatch{
                HostFailureKind::sample_rate,
                {filter + " is for " + made + ", and the host runs at " + runs,
                 "Convolver never resamples, and filters are made for a whole number of hertz, "
                 "so none plays at the host's rate.",
                 "Set the host's project or audio device to " + made + ". " +
                     std::string(detail::host_playback_rate_fix),
                 {}}};
        return Mismatch{
            HostFailureKind::sample_rate,
            {filter + " is for " + made + ", and the host runs at " + runs,
             "Convolver never resamples, so a filter plays only at the rate it was made for.",
             "Load a filter made for " + runs + ", or set the host's project or audio device to " +
                 made + ". " + std::string(detail::host_playback_rate_fix),
             {}}};
    }
    // PLUG-23: more inputs than the filter's are fine (it reads the first
    // ones); fewer are not, and the outputs must match.
    if (c.input_channels > host.inputs || c.output_channels != host.outputs) {
        const auto made = counts(c.input_channels, c.output_channels),
                   given = counts(host.inputs, host.outputs);
        Advice a{filter + " has " + made + ", and the host gives the plug-in " + given,
                 "A filter plays only with the outputs it was made for, and with at least its "
                 "inputs: it reads the first ones and ignores any others.",
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
        return Mismatch{HostFailureKind::channels, std::move(a)};
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

Advice preparation_message(const std::string& what) {
    return {"the filter could not be prepared" + (what.empty() ? std::string() : ": " + what),
            "Memory may have run out, or the filter is too long for this computer.",
            "Close other projects or programs, then load the filter again; a shorter filter needs "
            "less memory. If it keeps failing, copy the diagnostics and report it.",
            {}};
}

const Advice& failure_message(const HostFailure& f) {
    static const Advice unrecorded{
        "the load failed, and memory ran out before Convolver could record why",
        "Memory had run out.",
        "Close other projects or programs, then load the filter again.",
        {}};
    return f.message.empty() ? unrecorded : f.message;
}

std::string settings_now_known(const HostLayout& host) {
    return "The host's settings are now known (" + host_rate_text(host) + ", " +
           ports(host.inputs, host.outputs) + "): choose the configuration list again.";
}

Advice shown_failure_message(const HostFailure& f, const std::optional<HostLayout>& host) {
    auto a = failure_message(f);
    if (f.kind == HostFailureKind::settings_unknown && host) {
        a.cause = "A configuration list's entry is chosen for the host's sample rate and "
                  "channel counts, which were not known when it was chosen.";
        a.fix = settings_now_known(*host);
    }
    return a;
}

Advice state_read_message(const std::exception& e) {
    auto a = detail::advice_of(e);
    a.problem = "the saved project or preset could not be read: " + a.problem;
    if (a.fix.empty()) {
        a.cause = "The project or preset is damaged, or was not saved by Convolver.";
        a.fix = "Load the filter again, then save the project.";
    }
    return a;
}

std::string speaker_names(std::uint32_t mask, std::size_t channels) {
    if (!names_speakers(mask, channels))
        return {};
    std::string names;
    for (const auto bit : speaker_bits(mask))
        names += (names.empty() ? "" : ", ") + std::string(speaker_words[bit]);
    return names;
}

Advice speaker_notice(const Configuration& c, const HostLayout& host) {
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
    return Advice{
        "the filter's speaker labels differ from the host's output bus",
        "The filter's outputs are labelled for the speakers " + speaker_list(filter) +
            " (channel mask " + hex(filter) + " in its .cfg), and the host's output bus carries " +
            speaker_list(bus) + ". Convolver sends output channels to the bus in order, so " +
            differences + ".",
        "If the outputs feed speakers, set the host's track or bus to " + speaker_list(filter) +
            ". If they are not speakers (crossover bands, for example), set the channel mask at "
            "the end of the .cfg's first line to 0, then load the .cfg again (or make the preset "
            "again from it) and save the project.",
        "The filter plays as loaded either way."};
}

void set_host_rate(HostLayout& host, double hertz) noexcept {
    const double whole = std::round(hertz);
    if (std::abs(hertz - whole) <= 1e-12 * hertz) {
        host.sample_rate = static_cast<int>(whole);
        host.fractional_rate = 0;
    } else {
        host.sample_rate = static_cast<int>(std::floor(hertz));
        host.fractional_rate = hertz;
    }
}

std::string host_rate_text(const HostLayout& host) {
    if (host.fractional_rate == 0)
        return std::to_string(host.sample_rate) + " Hz";
    // Ten significant digits: 47952.04795 for 48000/1.001. A rate so close
    // to whole hertz that those read as whole gets every digit it needs, so
    // it never reads as the whole rate.
    std::array<char, 32> text{};
    auto* const last = text.data() + text.size();
    auto written =
        std::to_chars(text.data(), last, host.fractional_rate, std::chars_format::general, 10);
    if (written.ec == std::errc() && std::find(text.data(), written.ptr, '.') == written.ptr)
        written = std::to_chars(text.data(), last, host.fractional_rate);
    return std::string(text.data(), written.ec == std::errc() ? written.ptr : text.data()) + " Hz";
}

bool same_rate(const HostLayout& a, const HostLayout& b) noexcept {
    return a.sample_rate == b.sample_rate && a.fractional_rate == b.fractional_rate;
}

bool same_configuration(const HostLayout& a, const HostLayout& b) noexcept {
    return same_rate(a, b) && a.inputs == b.inputs && a.outputs == b.outputs;
}

namespace {
// PLUG-10: a filter's figure in decibels for a peak, as the CLI's --gain-db
// auto and no-clip choose it for float output; absent when the gain to play
// is not a finite float.
std::optional<double> figure_db(double peak) {
    const double db = peak > 0 ? -20 * std::log10(peak) : 0.0;
    if (!std::isfinite(db) || !std::isfinite(static_cast<float>(std::pow(10.0, db / 20))))
        return std::nullopt;
    return db;
}
// The noise estimate's figure, or absent with estimate_failed set when the
// noise overflowed single precision.
void find_estimate(FilterGains& g, const Configuration& c) {
    try {
        g.estimate_db = figure_db(estimate_attenuation(c).noise_peak);
        g.estimate_failed = !g.estimate_db;
    } catch (const std::overflow_error&) {
        g.estimate_db.reset();
        g.estimate_failed = true;
    }
}
// filter_gains() with the bound already found (HostFilter has it).
FilterGains gains_for(const Configuration& c, double worst_peak, bool estimate) {
    FilterGains g;
    // As the CLI's no-clip refuses a bound beyond the largest float.
    g.no_clipping_db =
        worst_peak <= std::numeric_limits<float>::max() ? figure_db(worst_peak) : std::nullopt;
    g.estimate_db.reset();
    if (estimate)
        find_estimate(g, c);
    return g;
}
} // namespace

FilterGains filter_gains(const Configuration& c, bool estimate) {
    validate_configuration(c);
    return gains_for(c, worst_case_peak(c), estimate);
}

const char* gain_mode_name(GainMode mode) noexcept {
    switch (mode) {
    case GainMode::estimate:
        return "Estimate";
    case GainMode::no_clipping:
        return "No clipping";
    case GainMode::set:
        break;
    }
    return "Set";
}

HostPlayback filter_playback(std::shared_ptr<const Configuration> c) {
    if (!c)
        throw std::invalid_argument("no filter to play");
    const bool dry = own_dry(*c);
    return {std::move(c), dry, {}};
}

HostPlayback filter_playback(Configuration c) {
    return filter_playback(std::make_shared<const Configuration>(std::move(c)));
}

HostPlayback unfiltered_playback(const HostLayout& host, bool failed, bool dry) {
    return {std::make_shared<const Configuration>(unfiltered_configuration(host, failed)),
            dry && host.inputs == host.outputs,
            {}};
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

std::uint64_t configuration_identity(const Configuration& c) noexcept {
    // FNV-1a over 32-bit words: every sample counts, at a quarter of the
    // byte-wise cost.
    std::uint64_t hash = 14695981039346656037ULL;
    const auto add = [&](std::uint32_t word) {
        hash ^= word;
        hash *= 1099511628211ULL;
    };
    const auto add64 = [&](std::uint64_t value) {
        add(static_cast<std::uint32_t>(value));
        add(static_cast<std::uint32_t>(value >> 32));
    };
    const auto add_float = [&](float value) { add(std::bit_cast<std::uint32_t>(value)); };
    add64(static_cast<std::uint64_t>(static_cast<std::int64_t>(c.sample_rate)));
    add64(c.input_channels);
    add64(c.output_channels);
    add(c.channel_mask);
    for (const auto* delays : {&c.input_delays, &c.output_delays}) {
        add64(delays->size());
        for (const auto d : *delays)
            add64(static_cast<std::uint64_t>(d));
    }
    add64(c.paths.size());
    for (const auto& path : c.paths) {
        add64(path.impulse.size());
        for (const auto sample : path.impulse)
            add_float(sample);
        for (const auto* routes : {&path.inputs, &path.outputs}) {
            add64(routes->size());
            for (const auto& route : *routes) {
                add64(route.channel);
                add_float(route.gain);
            }
        }
    }
    return hash;
}

namespace {
// PLUG-38: whether two filters are the same in every stored value, each
// float compared by its bits as configuration_identity() hashes it, so a
// signed-zero route gain differs from a positive zero. The identity picks
// the candidates; only this proves them equal.
bool same_configuration(const Configuration& a, const Configuration& b) noexcept {
    const auto same_floats = [](float x, float y) {
        return std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
    };
    const auto same_routes = [&](const std::vector<Route>& x, const std::vector<Route>& y) {
        return std::equal(x.begin(), x.end(), y.begin(), y.end(),
                          [&](const Route& r, const Route& t) {
                              return r.channel == t.channel && same_floats(r.gain, t.gain);
                          });
    };
    return a.sample_rate == b.sample_rate && a.input_channels == b.input_channels &&
           a.output_channels == b.output_channels && a.channel_mask == b.channel_mask &&
           a.input_delays == b.input_delays && a.output_delays == b.output_delays &&
           std::equal(a.paths.begin(), a.paths.end(), b.paths.begin(), b.paths.end(),
                      [&](const FilterPath& x, const FilterPath& y) {
                          return std::equal(x.impulse.begin(), x.impulse.end(), y.impulse.begin(),
                                            y.impulse.end(), same_floats) &&
                                 same_routes(x.inputs, y.inputs) &&
                                 same_routes(x.outputs, y.outputs);
                      });
}
} // namespace

std::string ignored_inputs(const Configuration& c, const HostLayout& host, const char* dash) {
    if (host.inputs <= c.input_channels)
        return {};
    const auto first = std::to_string(c.input_channels + 1);
    if (host.inputs == c.input_channels + 1)
        return "input " + first;
    return "inputs " + first + dash + std::to_string(host.inputs);
}

std::string HostFilter::error() const {
    const auto& f = failure();
    return f ? advice_text(f->message) : std::string();
}

std::uint64_t HostFilter::next_request() noexcept {
    return ++requests_;
}

namespace {
// The name a request gives its filter: a file's own name with its extension,
// never its folder, or the name saved beside state (empty when none).
std::string shown_name(const HostRequest& r) {
    if (r.source == HostFilterSource::file && !r.path.empty())
        return detail::utf8_string(detail::utf8_path(r.path).filename());
    return r.source == HostFilterSource::state ? r.name : std::string();
}
} // namespace

void HostFilter::offer_name(std::string name, std::uint64_t identity) {
    OfferedName next{std::move(name), identity};
    // State of this filter is playing without a name: it takes this one.
    std::string shown;
    const bool names_playing =
        filter_ && identity == identity_ && accepted_.source == HostFilterSource::state;
    if (names_playing)
        shown = next.name;
    offered_name_ = std::move(next);
    if (names_playing)
        accepted_.name = std::move(shown);
}

std::string HostFilter::take_unsettled_list() {
    const auto& r = load_failure_;
    if (!r || r->kind != HostFailureKind::settings_unknown ||
        r->request.source != HostFilterSource::file || r->request.path.empty() ||
        r->request.number == list_retaken_)
        return {};
    auto path = r->request.path;
    list_retaken_ = r->request.number;
    return path;
}

HostRequest HostFilter::numbered(HostRequest request) {
    request.number = request.number ? request.number : requests_ + 1;
    requests_ = std::max(requests_, request.number);
    return request;
}

void HostFilter::fail_request(HostProcessor* playing) noexcept {
    if (filter_)
        return;
    failed_ = true;
    // Silencing again, after an earlier failed request, changes nothing.
    if (playing)
        playing->silence();
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

void HostFilter::record(HostFailureKind kind, Advice why, const HostRequest& request,
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
// A rejection load() has already recorded, told apart from any other
// std::invalid_argument thrown while checking.
class Refused : public std::invalid_argument {
  public:
    using std::invalid_argument::invalid_argument;
};
} // namespace

namespace {
// The dry signal when no filter plays (dry_signal_rule()), from what is kept:
// a filter that does not suit (its counts decide too), or none after a failed
// load (jrp, 7 November 2026): a kept crossover must not send full-range audio
// to a driver, and nothing shows that a filter that failed to load was not
// one. The same with one output: after a failed load Convolver stays silent
// (jrp's answer, 7 November 2026, 19:42). An untouched instance passes its
// input.
HostDrySignal unplayed_dry(const Configuration* kept, bool failed,
                           const HostLayout& host) noexcept {
    if (host.inputs != host.outputs)
        return HostDrySignal::host_counts;
    if (kept)
        return kept->input_channels == kept->output_channels ? HostDrySignal::present
                                                             : HostDrySignal::filter_counts;
    return failed ? HostDrySignal::failed_load : HostDrySignal::present;
}
} // namespace

void HostFilter::refuse(HostFailureKind kind, Advice why, const HostRequest& request,
                        const Configuration* carried) {
    record(kind, std::move(why), request, carried);
    throw detail::AdvisedError<Refused>(load_failure_->message);
}

HostChange HostFilter::load(HostState state, const std::optional<HostLayout>& host,
                            const std::function<void(const HostPlayback&)>& install,
                            HostRequest request, HostProcessor* counting, HostLoadCheck check) {
    const bool keep_mismatch = check == HostLoadCheck::keep_until_suited;
    request = numbered(std::move(request));
    auto& next = state.configuration;
    // PLUG-39: what the request changes besides the filter, decided here once
    // for every format (the scope table). Its own settings apply, and their
    // Gain mode decides whether this load finds the estimate; restored state
    // brings its bypass, a chosen file none.
    HostChange change;
    change.settings = state.settings;
    if (request.kind != HostRequestKind::chosen_file)
        change.bypass = state.bypass;
    const bool estimating =
        state.settings ? state.settings->gain_mode == GainMode::estimate : estimating_;
    const Configuration* carried = next ? &*next : nullptr;
    // Everything that is kept is built before `install` publishes anything, so
    // that running out of memory rejects the request with the previous state
    // whole, and nothing after publication can throw: what plays and what is
    // saved never part (jrp's review of 0.4.30). Without the host's
    // configuration, activation checks the speakers.
    Advice notice;
    std::optional<HostFailure> saved_failure, kept_failure;
    double peak = 0;
    std::size_t tail = 0;
    std::uint64_t identity = 0;
    FilterGains gains;
    std::optional<OfferedName> offered;
    std::shared_ptr<const Configuration> accepted;
    try {
        if (state.failed && next)
            refuse(HostFailureKind::state_unreadable,
                   {"the saved project or preset is inconsistent: it records a failed load and "
                    "also holds a filter",
                    "The project or preset is damaged, or was not saved by Convolver.",
                    "Load the filter again, then save the project.",
                    {}},
                   request, carried);
        if (next && (!next->input_channels || !next->output_channels ||
                     next->input_channels > max_channels_ || next->output_channels > max_channels_))
            refuse(HostFailureKind::channels,
                   {"the filter has " + counts(next->input_channels, next->output_channels) +
                        ", which this plug-in cannot take",
                    "This plug-in takes 1 to " + std::to_string(max_channels_) +
                        " channels on each side.",
                    "Load a filter with 1 to " + std::to_string(max_channels_) +
                        " inputs and outputs, or render with the command-line tool, convolver, "
                        "which takes more.",
                    {}},
                   request, carried);
        // PLUG-12: what is accepted is saved, so a filter the saved state
        // cannot hold is refused here, before it plays.
        if (next)
            if (auto why = host_state_size_error(state); !why.empty())
                refuse(HostFailureKind::too_large, std::move(why), request, carried);
        if (host && next) {
            if (auto m =
                    mismatch(*next, *host, keep_mismatch ? "the saved filter" : "the filter")) {
                if (!keep_mismatch)
                    refuse(m->kind, std::move(m->why), request, carried);
                // Kept, as activation() keeps a filter that does not suit.
                HostFailure f;
                f.kind = m->kind;
                f.request = request;
                f.sample_rate = next->sample_rate;
                f.inputs = next->input_channels;
                f.outputs = next->output_channels;
                f.silent = true;
                f.message = std::move(m->why);
                kept_failure = std::move(f);
            } else {
                notice = speaker_notice(*next, *host);
            }
        }
        // PLUG-39: whether the request holds the accepted filter, equal in
        // every stored value, not by name or identity alone (PLUG-38). Decided
        // once, before any analysis: the peak and gains depend on the filter
        // alone, so the accepted filter's figures stand, and only an estimate newly
        // needed is found. A restore that changes only the settings no longer
        // filters noise again for a long filter.
        bool same = false;
        if (next) {
            identity = configuration_identity(*next);
            same = filter_ && identity == identity_ && same_configuration(*filter_, *next);
        }
        // PLUG-39 step 4a (jrp, 20 November 2026): the accepted filter is
        // shared, immutable, by HostFilter, what it gives the processor and
        // any copy an adapter keeps for its diagnostics, so none of them
        // copies it. Built whole here, before anything is published; the
        // same filter keeps the one already shared.
        if (same) {
            accepted = filter_;
        } else if (next) {
            accepted = std::make_shared<const Configuration>(std::move(*next));
            carried = accepted.get();
        }
        // PLUG-22, PLUG-39 step 4: a name shows only while its filter plays.
        // State that names nothing takes the name offered for its filter
        // (offer_name()), or keeps the one the same filter shows now; an
        // accepted file's own name is offered for the state saved from it.
        if (next && request.source == HostFilterSource::state && request.name.empty()) {
            if (offered_name_ && offered_name_->identity == identity)
                request.name = offered_name_->name;
            else if (same)
                request.name = shown_name(accepted_);
        }
        if (next && request.source == HostFilterSource::file)
            offered = OfferedName{shown_name(request), identity};
        // For the window's Loudest peak; on the control side with the rest of
        // the preparation, so a report never has to compute it.
        if (same) {
            peak = peak_;
            tail = tail_;
            gains = gains_;
            if (estimating && !gains.estimate_db && !gains.estimate_failed)
                find_estimate(gains, *accepted);
        } else if (accepted) {
            peak = worst_case_peak(*accepted);
            tail = configuration_tail_frames(*accepted);
            gains = gains_for(*accepted, peak, estimating);
        }
        if (state.failed) {
            HostFailure f;
            f.kind = HostFailureKind::saved_failure;
            f.request = request;
            f.silent = true;
            f.message = {"a filter had failed to load when this project or preset was saved",
                         "It records the failure, not a filter.",
                         "Load a filter made for the host's sample rate and channels, then save "
                         "again.",
                         {}};
            saved_failure = std::move(f);
        }
        if (host && install) {
            // A kept filter that does not suit plays silence, with the dry
            // signal dry_signal_rule() gives it once committed.
            const auto unfiltered =
                kept_failure ? unfiltered_playback(*host, true,
                                                   unplayed_dry(accepted.get(), true, *host) ==
                                                       HostDrySignal::present)
                : accepted   ? HostPlayback{}
                             : unfiltered_playback(*host, state.failed, !state.failed);
            try {
                if (accepted && !kept_failure) {
                    // Shared, not copied: a filter can be large.
                    const bool dry = own_dry(*accepted);
                    // PLUG-38: the filter that plays now, unchanged, may play
                    // on with its history: one accepted, suiting the host
                    // (no failure found at activation keeps it silent), and
                    // equal in full, not by name or identity alone. A
                    // session the host is opening keeps nothing from before.
                    const bool unchanged = same && !failed_ && !activation_failure_ &&
                                           request.kind != HostRequestKind::session;
                    install(HostPlayback{accepted, dry, gains, unchanged});
                } else {
                    install(unfiltered);
                }
            } catch (const SupersededRequest&) {
                throw; // Abandoned: nothing recorded or changed.
            } catch (const std::exception& e) {
                refuse(HostFailureKind::preparation,
                       preparation_message(detail::advice_of(e).problem), request, carried);
            }
        }
    } catch (const Refused&) {
        throw; // Recorded above.
    } catch (const SupersededRequest&) {
        throw;
    } catch (...) {
        // Out of memory building a message, or a non-standard exception:
        // recorded first without a message, in case that fails too (ROBUST-7).
        // A record refuse() made for this request stands: only copying its
        // message into the exception, or its path, ran out.
        if (load_failure_ && load_failure_->request.number == request.number)
            throw;
        record_unexplained(HostFailureKind::preparation, request, carried);
        refuse(HostFailureKind::preparation, preparation_message({}), request, carried);
    }
    // The commit, which cannot throw.
    static_assert(std::is_nothrow_move_assignable_v<std::shared_ptr<const Configuration>> &&
                  std::is_nothrow_move_assignable_v<HostRequest> &&
                  std::is_nothrow_move_assignable_v<std::optional<OfferedName>> &&
                  std::is_nothrow_move_assignable_v<std::optional<HostFailure>> &&
                  std::is_nothrow_move_assignable_v<Advice>);
    filter_ = std::move(accepted);
    peak_ = peak;
    tail_ = tail;
    gains_ = gains;
    estimating_ = estimating;
    identity_ = identity;
    failed_ = state.failed;
    awaiting_ = request.kind == HostRequestKind::chosen_file && keep_mismatch;
    accepted_ = std::move(request);
    if (offered)
        offered_name_ = std::move(offered);
    load_failure_ = std::move(saved_failure);
    activation_failure_ = std::move(kept_failure);
    notice_ = std::move(notice);
    // A new filter, or a new start for the same one: Fault clears. One
    // snapshot of the processor's counts is the baseline of both the audio
    // side's signal and faults(), so faults counted while the filter was
    // prepared are not shown against the new filter (jrp's review of 0.4.43).
    // The next observe() still logs them, as met around a filter change.
    faults_ = {};
    if (counting) {
        // Held first, so the audio side never shows Fault over a restored
        // load failure, even before the adapter updates its status.
        counting->hold_fault_signal(failure_shown());
        cleared_ = counting->clear_faults();
        changed_ = changed_ || since(cleared_, observed_).any();
    } else {
        cleared_ = observed_; // faults() counts on from what observe() last saw.
    }
    return change;
}

void HostFilter::reject(HostFailureKind kind, Advice why, HostRequest request,
                        const Configuration* carried) noexcept {
    try {
        record(kind, std::move(why), numbered(std::move(request)), carried);
    } catch (...) { // Out of memory for the path: recorded without it (ROBUST-7).
    }
}

HostActivation HostFilter::activation(const HostLayout& host) const {
    HostActivation a;
    if (!filter_) {
        a.playback = unfiltered_playback(host, failed_, !failed_);
        return a;
    }
    if (auto m = mismatch(*filter_, host, "the saved filter")) {
        HostFailure f;
        f.kind = m->kind;
        f.request = accepted_;
        f.sample_rate = filter_->sample_rate;
        f.inputs = filter_->input_channels;
        f.outputs = filter_->output_channels;
        f.silent = true;
        f.message = std::move(m->why);
        a.failure = std::move(f);
        // What dry_signal() gives once this failure is recorded.
        a.playback = unfiltered_playback(
            host, true, unplayed_dry(&*filter_, true, host) == HostDrySignal::present);
        return a;
    }
    a.notice = speaker_notice(*filter_, host);
    a.playback = filter_playback(filter_); // Shared, not copied.
    a.playback.gains = gains_;
    return a;
}

static_assert(std::is_nothrow_move_assignable_v<std::optional<HostFailure>> &&
                  std::is_nothrow_move_assignable_v<Advice>,
              "HostFilter::activate() must not throw");

void HostFilter::activate(HostActivation&& a) noexcept {
    activation_failure_ = std::move(a.failure);
    notice_ = std::move(a.notice);
    faults_ = cleared_ = observed_ = {}; // The new processor counts from 0.
    changed_ = false;
}

bool HostFilter::estimate(bool on, const std::function<void(const FilterGains&)>& apply) {
    estimating_ = on;
    if (!on || !filter_ || gains_.estimate_db || gains_.estimate_failed)
        return false;
    auto gains = gains_;
    find_estimate(gains, *filter_);
    if (activation_failure_ || failed_) {
        gains_ = gains;
        return false; // It does not suit the host, or failed: silence plays.
    }
    if (apply)
        apply(gains);
    gains_ = gains; // After everything that may throw, `apply` included.
    return true;
}

void HostFilter::fail_activation(const std::exception* error) {
    fail_activation(error ? detail::advice_of(*error).problem
                          : std::string("the processor could not be built."));
}

void HostFilter::fail_activation(std::string why) {
    // First, before anything that may throw, as for any new start: nothing
    // counts on from the processor that is gone.
    notice_ = {};
    activation_failure_.reset();
    faults_ = cleared_ = observed_ = {};
    changed_ = false;
    HostFailure f;
    f.kind = HostFailureKind::activation;
    f.request = accepted_;
    if (filter_) {
        f.sample_rate = filter_->sample_rate;
        f.inputs = filter_->input_channels;
        f.outputs = filter_->output_channels;
    }
    f.silent = true;
    f.message = {"the filter could not be started when the host started the plug-in" +
                     (why.empty() ? std::string() : ": " + why),
                 why.empty() ? "Memory may have run out."
                             : "Usually memory has run out; the words after the colon are the "
                               "reason given.",
                 "Close other projects or programs, then restart playback or load the filter "
                 "again. If it keeps failing, copy the diagnostics and report it.",
                 {}};
    activation_failure_ = std::move(f);
}

namespace {
std::string times(std::uint64_t n) {
    return n == 1 ? "once" : std::to_string(n) + " times";
}
} // namespace

HostFaultReport HostFilter::observe(const HostFaults& totals) noexcept {
    HostFaultReport report{since(totals, observed_), changed_, failure_shown()};
    observed_ = totals;
    faults_ = since(totals, cleared_);
    changed_ = false;
    return report;
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
        text += (text.empty() ? "" : " ") + detail::advice_line(kind);
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

Advice fault_advice(const HostFaults& f, bool held) {
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
    if (!r.any())
        return {};
    if (r.held)
        return "While audio played: " + fault_lines(r.faults) + " " + fault_held;
    if (r.changed)
        return "While audio played around a filter change: " + fault_lines(r.faults) +
               " The new filter started afresh, so the status shows only those met after it took "
               "over.";
    return fault_message(r.faults);
}

std::optional<double> applied_gain_db(const HostSettings& settings,
                                      const FilterGains& gains) noexcept {
    switch (settings.gain_mode) {
    case GainMode::estimate:
        return gains.estimate_db ? gains.estimate_db : gains.no_clipping_db;
    case GainMode::no_clipping:
        return gains.no_clipping_db;
    case GainMode::set:
        break;
    }
    return clamp_gain_db(settings.gain_db);
}

UnfilteredSound unfiltered_sound(const HostSettings& settings, bool identity) {
    // Without a filter, Estimate and No clipping play 0 dB (FilterGains).
    const double mix = clamp_mix(settings.mix),
                 db = applied_gain_db(settings, FilterGains{}).value_or(0);
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
// The short reason of a failure, completing "Silent: " or "Load failed, ...: ".
std::string short_reason(const HostFailure& f, const std::optional<HostLayout>& host) {
    switch (f.kind) {
    case HostFailureKind::filter_file:
        return "the filter file could not be read.";
    case HostFailureKind::state_unreadable:
        return "the saved project or preset could not be read.";
    case HostFailureKind::state_newer:
        return "the project or preset was saved by a newer Convolver.";
    case HostFailureKind::sample_rate:
        if (f.sample_rate <= 0) // Not recorded: say only what is known.
            return host ? "the filter is not for the host's " + host_rate_text(*host) + "."
                        : "the filter is for another rate.";
        // A refused load's rate may suit the host now (it changed its rate
        // since): say only what was refused.
        if (host && host->fractional_rate == 0 && host->sample_rate == f.sample_rate)
            return "the filter is for " + std::to_string(f.sample_rate) +
                   " Hz; the rate has changed since.";
        return "the filter is for " + std::to_string(f.sample_rate) + " Hz" +
               (host ? ", the host runs at " + host_rate_text(*host) + "." : ".");
    case HostFailureKind::channels:
        if (f.inputs == 0 && f.outputs == 0) // Not recorded (jrp's GarageBand run).
            return host ? "the filter does not fit the host's " +
                              ports(host->inputs, host->outputs) + "."
                        : "the filter has other channel counts.";
        // PLUG-23: a refused load's counts may suit the host now (it
        // changed its buses since): then say only what was refused, not
        // counts that look as if they still conflict (jrp's crossover
        // retest, 7 November 2026).
        if (host && f.inputs <= host->inputs && f.outputs == host->outputs)
            return "the filter has " + ports(f.inputs, f.outputs) +
                   "; the channels have changed since.";
        return "the filter has " + ports(f.inputs, f.outputs) +
               (host ? ", the host " + ports(host->inputs, host->outputs) + "." : ".");
    case HostFailureKind::preparation:
        return "the filter could not be prepared.";
    case HostFailureKind::activation:
        return "the plug-in could not start the filter.";
    case HostFailureKind::saved_failure:
        return "a load had failed when this was saved.";
    case HostFailureKind::too_large:
        return "the filter is too large to save with the project.";
    case HostFailureKind::settings_unknown:
        // PLUG-40: what to do now, given a configuration the host was seen
        // playing with (HostFilter::played_with()). With the longest prefix
        // and the log's pointer it fits max_status_text except with a
        // fractional rate and ten or more channels, where the pointer is cut.
        // The window's Fix says it in full (settings_now_known()).
        if (host)
            return "now known: " + host_rate_text(*host) + ", " +
                   ports(host->inputs, host->outputs) + "; choose the list again.";
        return "no list entry yet; play audio once, then choose it again.";
    }
    return "the filter could not be loaded.";
}
} // namespace

HostDrySignal dry_signal_rule(const HostFilter& filter, const HostLayout& host) noexcept {
    const auto& f = filter.failure();
    const auto& c = filter.filter();
    const auto counts = [](std::size_t inputs, std::size_t outputs) {
        return inputs == outputs ? HostDrySignal::present : HostDrySignal::filter_counts;
    };
    if (c && !(f && f->silent)) // A filter plays: its own counts (PLUG-23).
        return counts(c->input_channels, c->output_channels);
    // No filter plays; an empty instance passes its input.
    return unplayed_dry(c ? &*c : nullptr, filter.failed() || (f && f->silent), host);
}

HostDrySignal dry_signal_before_host(const HostFilter& filter) noexcept {
    const auto& f = filter.failure();
    const auto& c = filter.filter();
    if (!c)
        return filter.failed() || (f && f->silent) ? HostDrySignal::failed_load
                                                   : HostDrySignal::present;
    return c->input_channels == c->output_channels ? HostDrySignal::present
                                                   : HostDrySignal::filter_counts;
}

const char* no_dry_reason(HostDrySignal rule) noexcept {
    switch (rule) {
    case HostDrySignal::present:
        return "";
    case HostDrySignal::filter_counts:
        return "the filter's inputs and outputs differ";
    case HostDrySignal::failed_load:
        return "a load failed and no filter is kept";
    case HostDrySignal::host_counts:
        return "inputs and outputs differ";
    }
    return "";
}

HostStatus host_status_value(const HostFilter& filter, bool host_known, bool bypassed) noexcept {
    host_known = host_known && !filter.waiting(); // PLUG-27: awaiting validation.
    if (filter.shown_failure())
        return HostStatus::load_failed;
    if (filter.faults().any() && !filter.failed())
        return HostStatus::fault;
    if (bypassed && filter.filter())
        return HostStatus::bypassed;
    if (filter.filter())
        return host_known ? HostStatus::active : HostStatus::unchecked;
    if (filter.failed())
        return HostStatus::load_failed;
    return HostStatus::no_filter;
}

HostStatusLine host_status(const HostFilter& filter, const std::optional<HostLayout>& known,
                           bool log_written, const HostSettings& settings, bool bypassed,
                           bool chooses_files, std::optional<std::size_t> latency_in_use) {
    // PLUG-27: a chosen filter awaiting validation is not judged against a
    // start that may have been at a placeholder rate.
    const auto host = filter.waiting() ? std::optional<HostLayout>() : known;
    const char* details =
        log_written ? " Details in the Convolver log." : " The log could not be written.";
    HostStatusLine line;
    // PLUG-10: Mix lets the dry input through only with equal counts, which
    // are known once the host has started the instance (PLUG-23: the
    // filter's own counts while it plays). A failed load with no filter
    // kept never has one, whatever the host (jrp, 7 November 2026).
    const bool dry_known = host && dry_signal(filter, *host);
    const auto& failure = filter.shown_failure();
    const bool never_dry = dry_signal_before_host(filter) == HostDrySignal::failed_load;
    const bool dry_unknown = !host && !never_dry && clamp_mix(settings.mix) < 1;
    // PLUG-36 (jrp, 20 November 2026): bypass plays the untouched input
    // wherever there is a dry signal, so when no filter is heard the line
    // keeps the failure visible and says that bypass is on and what it plays.
    const bool bypass_dry = bypassed && dry_known;
    // What plays with no filter heard, before the reason: "Silent: " only
    // when Mix lets no dry signal through.
    const auto silent_words = [&]() -> std::string {
        if (dry_unknown)
            return "Load failed, no filter plays: ";
        if (dry_known && !unfiltered_sound(settings, false).silent)
            return "Load failed, only the dry signal plays: ";
        return "Silent: ";
    };
    // Which status, in the one order host_status_value() gives; then its words.
    line.status = host_status_value(filter, host.has_value(), bypassed);
    const auto& f = failure;
    switch (line.status) {
    case HostStatus::load_failed:
        if (!f) {
            // A failed load: no filter is kept, so no dry signal plays either.
            line.text = "Silent: an earlier load failed." + std::string(details);
            break;
        }
        {
            // A filter the adapter could not start has no processor, so nothing
            // plays, not even Mix's dry share (the independent review).
            if (f->silent && f->kind != HostFailureKind::activation && bypass_dry) {
                auto reason = f->kind == HostFailureKind::settings_unknown
                                  ? short_reason(*f, filter.played_with(host))
                                  : short_reason(*f, host);
                if (!reason.empty())
                    reason[0] =
                        static_cast<char>(std::toupper(static_cast<unsigned char>(reason[0])));
                // No room for the log's pointer: the window's report and the log
                // give the details.
                line.text = "Load failed. Bypass is on: only the dry signal plays. " + reason;
            } else {
                line.text = (!f->silent ? "Load failed, previous filter kept: "
                             : f->kind == HostFailureKind::activation ? std::string("Silent: ")
                                                                      : silent_words()) +
                            (f->kind == HostFailureKind::settings_unknown
                                 ? short_reason(*f, filter.played_with(host))
                                 : short_reason(*f, host)) +
                            details;
            }
        }
        break;
    case HostStatus::fault:
        line.text = "Fault: " + short_fault(filter.faults()) + details;
        break;
    case HostStatus::bypassed: {
        // As HostProcessor bypasses: the delayed input on each output when
        // there is a dry signal, otherwise silence (dry_signal_rule).
        const auto rule = host ? dry_signal_rule(filter, *host) : HostDrySignal::present;
        line.text = !host ? "Bypassed: no filter is applied."
                    : rule == HostDrySignal::present
                        ? "Bypassed: the input plays unfiltered."
                        : std::string("Bypassed: silent, as ") + no_dry_reason(rule) + ".";
        break;
    }
    case HostStatus::active:
    case HostStatus::unchecked: {
        const auto& c = filter.filter();
        if (!host && filter.awaiting()) { // PLUG-27, jrp's words (20 November 2026).
            line.text = "Filter selected. Waiting for the host's audio settings.";
            break;
        }
        const auto ignored = host ? ignored_inputs(*c, *host, "-") : std::string();
        line.text = "Filter loaded: " + std::to_string(c->sample_rate) + " Hz, " +
                    ports(c->input_channels, c->output_channels) +
                    (!host             ? "; waiting for audio settings from the host."
                     : ignored.empty() ? "."
                                       : "; plug-in " + ignored + " ignored.");
        const bool mix_dry_only = dry_known && clamp_mix(settings.mix) == 0;
        // DIAG-10: what the window's Gain mode note says, in short, before
        // the notes below, so the 127-character limit cuts those first.
        // Without a figure the filter is silent, but Mix's dry share still
        // plays where there is one (PLUG-36's rule for "Silent: ").
        if (!applied_gain_db(settings, filter.gains())) {
            if (!mix_dry_only)
                line.text += dry_known && clamp_mix(settings.mix) < 1
                                 ? " No gain figure for this filter: only the dry signal plays."
                                 : " Silent: no gain figure for this filter.";
        } else if (settings.gain_mode == GainMode::estimate && !filter.gains().estimate_db) {
            line.text += filter.gains().estimate_failed
                             ? " Too loud to estimate: No clipping's gain plays."
                             : " No estimate: No clipping's gain plays.";
        }
        if (host && !filter.notice().empty())
            line.text += log_written ? " Speaker labels differ; see the Convolver log."
                                     : " Speaker labels differ.";
        if (mix_dry_only)
            line.text += " Mix 0%: only the dry signal plays.";
        break;
    }
    case HostStatus::no_filter: {
        const std::string load = chooses_files ? " Choose a filter or load a Convolver preset."
                                               : " Load a Convolver preset.";
        if (!host)
            line.text = "No filter loaded." + load;
        else if (!dry_known)
            line.text = "No filter: silent, as inputs and outputs differ." + load;
        else if (bypassed)
            line.text = "No filter. Bypass is on: audio passes unchanged." + load;
        else if (const auto sound = unfiltered_sound(settings, true); !sound.unchanged)
            line.text = "No filter: audio passes at " + sound.level + " (Gain and Mix)." + load;
        else
            line.text = "No filter: audio passes unchanged." + load;
        break;
    }
    }
    // DIAG-10: a Latency chosen but not in use yet, after anything that needs
    // attention (a failure or a fault says more than this).
    if (latency_in_use && *latency_in_use != settings.latency &&
        line.status != HostStatus::load_failed && line.status != HostStatus::fault)
        line.text += " Latency " + std::to_string(settings.latency) + " frames not in use yet.";
    if (line.text.size() > max_status_text)
        line.text.resize(max_status_text);
    return line;
}

} // namespace convolver
