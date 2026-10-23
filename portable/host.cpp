// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include "convolver_identities.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace convolver {
namespace {
std::string count(std::size_t n, const char* noun) {
    return std::to_string(n) + " " + noun + (n == 1 ? "" : "s");
}
std::string counts(std::size_t inputs, std::size_t outputs) {
    return count(inputs, "input") + " and " + count(outputs, "output");
}
struct Mismatch {
    HostFailureKind kind;
    std::string why;
};
// Why a filter does not suit the host, or nothing if it does.
std::optional<Mismatch> mismatch(const Configuration& c, const HostLayout& host) {
    if (c.sample_rate != host.sample_rate)
        return Mismatch{HostFailureKind::sample_rate,
                        "The filter was made for " + std::to_string(c.sample_rate) +
                            " Hz, but the host runs at " + std::to_string(host.sample_rate) +
                            " Hz. Convolver does not resample: load a filter made for " +
                            std::to_string(host.sample_rate) + " Hz, or set the host to " +
                            std::to_string(c.sample_rate) + " Hz."};
    if (c.input_channels != host.inputs || c.output_channels != host.outputs)
        return Mismatch{HostFailureKind::channels,
                        "The filter has " + counts(c.input_channels, c.output_channels) +
                            ", but the plug-in is set up with " +
                            counts(host.inputs, host.outputs) + ". Give the plug-in " +
                            counts(c.input_channels, c.output_channels) +
                            " in the host's track or routing settings, or load a filter made for " +
                            counts(host.inputs, host.outputs) + "."};
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
    return "The filter's outputs are labelled for the speakers " + speaker_list(filter) +
           " (channel mask " + hex(filter) +
           " in its .cfg), but the host's output bus "
           "carries " +
           speaker_list(bus) +
           ". Convolver sends output channels to the bus in "
           "order, so " +
           differences + ". If the outputs feed speakers, set the host's track or bus to " +
           speaker_list(filter) +
           ". If they are not speakers (crossover bands, for example), set the channel mask "
           "at the end of the .cfg's first line to 0, then load the .cfg again (or make the "
           "preset again from it) and save the project. The filter plays as loaded either "
           "way.";
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

void HostFilter::record(HostFailureKind kind, std::string why, const HostRequest& request,
                        const Configuration* carried) {
    // Build everything first, so running out of memory changes nothing.
    HostFailure f;
    f.kind = kind;
    f.request = request;
    if (carried) {
        f.sample_rate = carried->sample_rate;
        f.inputs = carried->input_channels;
        f.outputs = carried->output_channels;
    }
    f.silent = !filter_;
    f.message = (filter_ ? "Filter not loaded; the previous filter is kept. "
                         : "Filter not loaded, so Convolver plays silence. ") +
                why;
    failed_ = f.silent;
    load_failure_ = std::move(f);
}

namespace {
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
                      const std::function<void(const Configuration&)>& install,
                      HostRequest request) {
    request = numbered(std::move(request));
    auto& next = state.configuration;
    const Configuration* carried = next ? &*next : nullptr;
    try {
        if (state.failed && next)
            refuse(HostFailureKind::state_unreadable, "A failed load cannot hold a filter.",
                   request, carried);
        if (next && (!next->input_channels || !next->output_channels ||
                     next->input_channels > max_channels_ || next->output_channels > max_channels_))
            refuse(HostFailureKind::channels,
                   "The filter has " + counts(next->input_channels, next->output_channels) +
                       "; this plug-in supports 1 to " + std::to_string(max_channels_) +
                       " channels on each side.",
                   request, carried);
        if (host) {
            if (next) {
                if (auto m = mismatch(*next, *host))
                    refuse(m->kind, std::move(m->why), request, carried);
            }
            if (install) {
                const auto unfiltered =
                    next ? Configuration{} : unfiltered_configuration(*host, state.failed);
                try {
                    install(next ? *next : unfiltered);
                } catch (const SupersededRequest&) {
                    throw; // Abandoned: nothing recorded or changed.
                } catch (const std::exception& e) {
                    refuse(HostFailureKind::preparation,
                           std::string("The filter could not be prepared: ") + e.what(), request,
                           carried);
                }
            }
        }
    } catch (const Refused&) {
        throw; // Recorded above.
    } catch (const SupersededRequest&) {
        throw;
    } catch (...) {
        // Out of memory building a message, or a non-standard exception.
        refuse(HostFailureKind::preparation, "The filter could not be prepared.", request, carried);
    }
    // Build what is kept before changing anything, so running out of memory
    // leaves the previous state whole. Without the host's configuration,
    // activation checks the speakers.
    auto notice = host && next ? speaker_notice(*next, *host) : std::string();
    std::optional<HostFailure> saved_failure;
    if (state.failed) {
        HostFailure f;
        f.kind = HostFailureKind::saved_failure;
        f.request = request;
        f.silent = true;
        f.message = "A filter failed to load before this state was saved, so Convolver plays "
                    "silence. Load a filter made for the host's sample rate and channels.";
        saved_failure = std::move(f);
    }
    filter_ = std::move(next);
    failed_ = state.failed;
    accepted_ = std::move(request);
    load_failure_ = std::move(saved_failure);
    activation_failure_.reset();
    notice_ = std::move(notice);
}

void HostFilter::reject(HostFailureKind kind, std::string why, HostRequest request) {
    record(kind, std::move(why), numbered(std::move(request)), nullptr);
}

Configuration HostFilter::activate(const HostLayout& host) {
    notice_.clear();
    activation_failure_.reset();
    if (!filter_)
        return unfiltered_configuration(host, failed_);
    if (auto m = mismatch(*filter_, host)) {
        HostFailure f;
        f.kind = m->kind;
        f.request = accepted_;
        f.sample_rate = filter_->sample_rate;
        f.inputs = filter_->input_channels;
        f.outputs = filter_->output_channels;
        f.silent = true;
        f.message = "The saved filter does not suit the host, so Convolver plays silence until "
                    "it does. " +
                    m->why;
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
    f.message = "The filter could not be prepared when the host started the plug-in, so "
                "Convolver does not play it: " +
                why;
    notice_.clear();
    activation_failure_ = std::move(f);
}

struct HostProcessor::Impl {
    struct Prepared {
        Engine engine;
        Prepared* next_retired = nullptr;
        explicit Prepared(const Configuration& c, std::size_t block) : engine(c, block) {}
    };
    static_assert(std::atomic<Prepared*>::is_always_lock_free);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    const int rate;
    const std::size_t inputs, outputs, maximum, block;
    Prepared* current = nullptr;
    Prepared* previous = nullptr;
    std::atomic<Prepared*> pending{nullptr}, retired{nullptr};
    std::atomic<std::size_t> tail{0};
    std::vector<float> input, wet, old_wet, dry;
    std::size_t dry_position = 0, warm = 0, fade = 0;
    float dry_mix = 0;
    bool bypass = false;
    bool started = false; // Audio processed since construction or reset.
    Impl(const Configuration& c, std::size_t maximum_, std::size_t block_)
        : rate(c.sample_rate), inputs(c.input_channels), outputs(c.output_channels),
          maximum(maximum_), block(block_) {
        if (!inputs || !outputs || inputs > 256 || outputs > 256 || !maximum ||
            maximum > (1U << 20) || std::max(inputs, outputs) * maximum > (1U << 24))
            throw std::invalid_argument("invalid host channel count or callback capacity");
        auto prepared = std::make_unique<Prepared>(c, block);
        input.resize(inputs * maximum);
        wet.resize(outputs * maximum);
        old_wet.resize(wet.size());
        dry.resize(outputs * block);
        tail.store(prepared->engine.tail_frames());
        current = prepared.release();
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
        std::fill(dry.begin(), dry.end(), 0.f);
        dry_position = 0;
        warm = 0;
        fade = 0;
        dry_mix = bypass ? 1.f : 0.f;
    }
    template <class T>
    bool process(std::span<const T* const> in, std::span<T* const> out,
                 std::size_t frames) noexcept {
        if (frames > maximum || in.size() > inputs || out.size() > outputs)
            return false;
        if (!frames)
            return true;
        if (!previous) {
            if (auto next = pending.exchange(nullptr, std::memory_order_acq_rel)) {
                if (started) {
                    previous = current;
                    warm = block;
                    fade = 0;
                } else {
                    // Nothing has been heard yet (initial restoration): install
                    // directly, so output follows the filter from the first frame.
                    retire(current);
                }
                current = next;
            }
        }
        started = true;
        for (std::size_t t = 0; t < frames; ++t)
            for (std::size_t c = 0; c < inputs; ++c) {
                double v = c < in.size() && in[c] ? in[c][t] : 0;
                input[t * inputs + c] =
                    std::isfinite(v) && std::abs(v) <= std::numeric_limits<float>::max()
                        ? static_cast<float>(v)
                        : 0.f;
            }
        // Prepared, valid nonoverlapping spans and finite input satisfy Engine's
        // checked entry point; defensive containment keeps exceptions inside host.
        try {
            current->engine.process(input, wet, frames);
            if (previous)
                previous->engine.process(input, old_wet, frames);
        } catch (...) {
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
                    fade = std::min<std::size_t>(256, fade + 1);
                    blend = static_cast<float>(fade) / 256;
                }
            }
            dry_mix = std::clamp(dry_mix + (bypass ? 1.f / 64 : -1.f / 64), 0.f, 1.f);
            for (std::size_t c = 0; c < outputs; ++c) {
                // Bypass passes input c to output c when the counts are equal. With
                // unequal counts it plays silence on every output (owner's decision,
                // 22 October 2026): no channel has an unfiltered counterpart, and a
                // crossover must not send full-range audio to a driver.
                auto index = t * outputs + c;
                auto& delayed = dry[dry_position * outputs + c];
                float dry_value = delayed;
                delayed = inputs == outputs ? input[t * inputs + c] : 0.f;
                double value = wet[index];
                if (previous)
                    value = (1.0 - blend) * old_wet[index] + blend * value;
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
        if (invalid_output)
            reset();
        if (previous && !warm && fade == 256) {
            retire(previous);
            previous = nullptr;
        }
        return true;
    }
};
HostProcessor::HostProcessor(const Configuration& c, std::size_t maximum, std::size_t block)
    : impl_(std::make_unique<Impl>(c, maximum, block)) {}
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
std::size_t HostProcessor::latency_frames() const noexcept {
    return impl_->block;
}
std::size_t HostProcessor::tail_frames() const noexcept {
    return impl_->tail.load();
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
