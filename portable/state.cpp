// SPDX-License-Identifier: GPL-2.0-or-later
#include "bytes.hpp"
#include "convolver/host.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace convolver {
namespace {
// Version 2 adds flag 4, a failed initial load (owner's decision, 22 October
// 2026). Version 3 (VST3-4; owner's decision, 25 October 2026) is a superset
// of version 2 that also holds the settings, in five words after the flags:
// the gain in decibels and the mix as 64-bit doubles (low word first), then
// the latency in frames. Encoders write the lowest version that holds the
// state: 3 only for settings that differ from the defaults, 2 for a failed
// load without them, 1 otherwise, so builds that read only version 1 keep
// reading every other state.
constexpr std::uint32_t magic = 0x53564e43, version = 3;
constexpr std::size_t settings_words = 5;
// The version encode_host_state() writes for this state.
std::uint32_t state_version(const HostState& state) noexcept {
    if (state.settings && *state.settings != HostSettings{})
        return 3;
    return state.failed ? 2 : 1;
}
struct Writer {
    std::vector<std::byte> data;
    void word(std::size_t n) {
        if (n > std::numeric_limits<std::uint32_t>::max() || data.size() > max_host_state_bytes - 4)
            throw std::length_error("host state exceeds limit");
        const auto offset = data.size();
        data.resize(offset + 4);
        detail::write_le32(std::span(data).subspan(offset), static_cast<std::uint32_t>(n));
    }
    void sample(float v) {
        word(std::bit_cast<std::uint32_t>(v));
    }
    void number(double v) {
        const auto bits = std::bit_cast<std::uint64_t>(v);
        word(static_cast<std::uint32_t>(bits));
        word(static_cast<std::uint32_t>(bits >> 32));
    }
};
struct Reader {
    std::span<const std::byte> data;
    std::uint32_t word() {
        if (data.size() < 4)
            throw std::invalid_argument("truncated host state");
        const auto n = detail::read_le32(data);
        data = data.subspan(4);
        return n;
    }
    std::uint32_t count(std::size_t limit, std::size_t bytes_per_item = 4) {
        auto n = word();
        if (n > limit || n > data.size() / bytes_per_item)
            throw std::invalid_argument("invalid host state count");
        return n;
    }
    float sample() {
        return std::bit_cast<float>(word());
    }
    double number() {
        const std::uint64_t low = word();
        return std::bit_cast<double>(low | (std::uint64_t{word()} << 32));
    }
};
std::size_t add(std::size_t a, std::size_t b) noexcept {
    return b > std::numeric_limits<std::size_t>::max() - a ? std::numeric_limits<std::size_t>::max()
                                                           : a + b;
}
// The words encode_host_state() writes, saturating.
std::size_t encoded_words(const HostState& state) noexcept {
    std::size_t words = 3; // Magic, version, flags.
    if (state_version(state) == 3)
        words += settings_words;
    if (!state.configuration)
        return words;
    const auto& c = *state.configuration;
    // Rate, channels, mask; the delay counts and delays; the path count.
    words = add(words, 4 + 1 + 1 + 1);
    words = add(add(words, c.input_delays.size()), c.output_delays.size());
    for (const auto& p : c.paths) {
        // The impulse's length and samples; each route list's count, and a
        // channel and a gain per route.
        words = add(words, 3);
        words = add(words, p.impulse.size());
        words = add(add(words, p.inputs.size()), p.inputs.size());
        words = add(add(words, p.outputs.size()), p.outputs.size());
    }
    return words;
}
std::size_t samples(const Configuration& c) noexcept {
    std::size_t n = 0;
    for (const auto& p : c.paths)
        n = add(n, p.impulse.size());
    return n;
}
// Four bytes a word, saturating.
std::size_t word_bytes(std::size_t words) noexcept {
    return words > std::numeric_limits<std::size_t>::max() / 4
               ? std::numeric_limits<std::size_t>::max()
               : words * 4;
}
} // namespace
std::size_t encoded_host_state_size(const HostState& state) noexcept {
    return word_bytes(encoded_words(state));
}
std::string host_state_size_error(const HostState& state) {
    // PLUG-13: a fixed room for the settings is kept whether or not the
    // state holds them yet, so a filter accepted at default settings still
    // saves once a VST3 setting changes (jrp's review of 276c7dc), now and
    // after later settings are added.
    static_assert(settings_words * 4 <= host_settings_room_bytes);
    auto words = encoded_words(state);
    if (state_version(state) == 3)
        words -= settings_words;
    if (state.configuration)
        words = add(words, host_settings_room_bytes / 4);
    const auto size = word_bytes(words);
    if (size <= max_host_state_bytes)
        return {};
    // Each sample takes 4 bytes; the rest (rates, counts, routes) is small.
    constexpr auto limit_samples = max_host_state_bytes / 4;
    const auto n = state.configuration ? samples(*state.configuration) : 0;
    return "The filter is too large to save: its " + std::to_string(n) +
           " samples (all its impulse responses together) would take " + std::to_string(size) +
           " bytes in the saved state with the plug-in's settings, and plug-in state and presets "
           "hold at most " +
           std::to_string(max_host_state_bytes) + " bytes (64 MiB), just under " +
           std::to_string(limit_samples) +
           " samples, with room kept for the plug-in's settings. Plug-ins refuse it so that a "
           "project never holds a filter it cannot save. Shorten "
           "the impulse responses (most of a long one's end is usually silence) or use fewer "
           "of them; the convolver command-line tool can still render with it.";
}
std::vector<std::byte> encode_host_state(const HostState& state) {
    if (state.configuration)
        validate_configuration(*state.configuration);
    if (state.failed && state.configuration)
        throw std::invalid_argument("a failed load holds no filter");
    // Refused before anything is allocated; reserved exactly otherwise, so a
    // large state is never copied while it grows.
    const auto size = encoded_host_state_size(state);
    if (size > max_host_state_bytes)
        throw std::length_error("host state exceeds limit");
    Writer w;
    w.data.reserve(size);
    const auto format = state_version(state);
    w.word(magic);
    w.word(format);
    w.word((state.configuration ? 1 : 0) | (state.bypass ? 2 : 0) | (state.failed ? 4 : 0));
    if (format == 3) {
        w.number(state.settings->gain_db);
        w.number(state.settings->mix);
        w.word(state.settings->latency);
    }
    if (state.configuration) {
        const auto& c = *state.configuration;
        w.word(c.sample_rate);
        w.word(c.input_channels);
        w.word(c.output_channels);
        w.word(c.channel_mask);
        for (const auto* delays : {&c.input_delays, &c.output_delays}) {
            w.word(delays->size());
            for (auto d : *delays)
                w.word(d);
        }
        w.word(c.paths.size());
        for (const auto& p : c.paths) {
            w.word(p.impulse.size());
            for (auto v : p.impulse)
                w.sample(v);
            for (const auto* routes : {&p.inputs, &p.outputs}) {
                w.word(routes->size());
                for (const auto& r : *routes) {
                    w.word(r.channel);
                    w.sample(r.gain);
                }
            }
        }
    }
    return std::move(w.data);
}
HostState decode_host_state(std::span<const std::byte> bytes) {
    if (bytes.size() > max_host_state_bytes)
        throw std::length_error("host state exceeds limit");
    Reader r{bytes};
    if (r.word() != magic)
        throw std::invalid_argument("it is not Convolver plug-in state");
    const auto format = r.word();
    if (format > version)
        throw NewerHostState("it was saved by a newer version of Convolver (state version " +
                             std::to_string(format) + "; this version reads versions 1 to " +
                             std::to_string(version) +
                             "). Update Convolver to load it, or make the preset again "
                             "from its .cfg with this version's convolver-preset.");
    if (format < 1)
        throw std::invalid_argument("unsupported host state version " + std::to_string(format));
    auto flags = r.word();
    // Version 2 only ever marks a failed load (flag 4); a failed load never
    // holds a filter. Version 3 may mark one or not.
    if (flags > (format == 1 ? 3U : 7U) || (flags & 5) == 5 || (format == 2 && !(flags & 4)))
        throw std::invalid_argument("invalid host state flags");
    HostState state;
    state.bypass = (flags & 2) != 0;
    state.failed = (flags & 4) != 0;
    if (format == 3) {
        HostSettings settings;
        settings.gain_db = r.number();
        settings.mix = r.number();
        settings.latency = r.word();
        if (!std::isfinite(settings.gain_db) || settings.gain_db < min_gain_db ||
            settings.gain_db > max_gain_db || !std::isfinite(settings.mix) || settings.mix < 0 ||
            settings.mix > 1 ||
            std::find(host_latencies.begin(), host_latencies.end(), settings.latency) ==
                host_latencies.end())
            throw std::invalid_argument("invalid host state settings");
        state.settings = settings;
    }
    if (flags & 1) {
        Configuration c;
        auto rate = r.word();
        if (rate > 768000)
            throw std::invalid_argument("invalid sample rate");
        c.sample_rate = static_cast<int>(rate);
        c.input_channels = r.word();
        c.output_channels = r.word();
        c.channel_mask = r.word();
        for (auto* delays : {&c.input_delays, &c.output_delays}) {
            auto n = r.count(256);
            delays->reserve(n);
            while (n--)
                delays->push_back(r.word());
        }
        auto paths = r.count(4096, 12);
        c.paths.reserve(paths);
        while (paths--) {
            FilterPath p;
            auto n = r.count(max_host_state_bytes / 4);
            p.impulse.reserve(n);
            while (n--)
                p.impulse.push_back(r.sample());
            for (auto* routes : {&p.inputs, &p.outputs}) {
                auto count = r.count(4096, 8);
                routes->reserve(count);
                while (count--) {
                    auto channel = r.word();
                    auto gain = r.sample();
                    routes->push_back({channel, gain});
                }
            }
            c.paths.push_back(std::move(p));
        }
        validate_configuration(c);
        state.configuration = std::move(c);
    }
    if (!r.data.empty())
        throw std::invalid_argument("trailing host state bytes");
    return state;
}
} // namespace convolver
