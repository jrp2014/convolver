// SPDX-License-Identifier: GPL-2.0-or-later
#include "advice.hpp"
#include "bytes.hpp"
#include "convolver/host.hpp"
#include "utf8.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace convolver {
namespace {
// Version 2 adds flag 4, a failed initial load (owner's decision, 22 October
// 2026). Version 3 (VST3-4; owner's decision, 25 October 2026) is a superset
// of version 2 that also holds the settings, in five words after the flags:
// the gain in decibels and the mix as 64-bit doubles (low word first), then
// the latency in frames. Encoders write the lowest version that holds the
// state: 3 whenever it records settings, defaults included (PLUG-35; jrp,
// 12 November 2026: a preset a host saves at the defaults then resets them
// when chosen in the window), 2 for a failed load without them, 1 otherwise,
// so builds that read only version 1 keep reading every state without
// settings (presets from convolver-preset and the other formats' states).
// Version 4 (PLUG-10; jrp, 12 November 2026) is version 3 with one more word
// after the latency, the gain mode (GainMode's number), written only when the
// mode is not Set.
// Version 5 (PLUG-53; jrp, 26 November 2026: one preset name shared by every
// format) is written only for a state with a preset name. After the flags
// come the name's length in bytes and its bytes, padded with zeros to whole
// words; then, when flag 8 is set, the settings as in version 4, with any
// gain mode (Set too); then the filter as before.
constexpr std::uint32_t magic = 0x53564e43, version = 5;
constexpr std::size_t settings_words = 5;
constexpr std::uint32_t settings_flag = 8; // Version 5: the settings follow.
// The words the settings take in this version (none before version 3).
constexpr std::size_t settings_words_in(std::uint32_t format) noexcept {
    return format >= 4 ? settings_words + 1 : format == 3 ? settings_words : 0;
}
// The words a preset name takes: its length, then its bytes in whole words.
constexpr std::size_t name_words(std::size_t bytes) noexcept {
    return bytes ? 1 + (bytes + 3) / 4 : 0;
}
// A preset name and the settings fit in the room kept for them, so naming a
// filter never makes it too large to save.
static_assert((name_words(max_preset_name_bytes) + settings_words + 1) * 4 <=
              host_settings_room_bytes);
// PLUG-58: everything before the filter fits in host_state_header_bytes.
static_assert((3 + name_words(max_preset_name_bytes) + settings_words + 1) * 4 <=
              host_state_header_bytes);
// The version encode_host_state() writes for this state.
std::uint32_t state_version(const HostStateView& state) noexcept {
    if (!state.preset_name.empty())
        return 5;
    if (state.settings && state.settings->gain_mode != GainMode::set)
        return 4;
    if (state.settings)
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
    // PERF-25 (jrp's review of 7bd024f): an impulse's samples in one append.
    // The state is little-endian, so on a little-endian system each float's
    // bytes are already the bytes sample() writes and are copied as they lie;
    // other systems write them a word at a time. Same bytes, same limit.
    void samples(std::span<const float> values) {
        const auto bytes = values.size_bytes();
        if (bytes > max_host_state_bytes || data.size() > max_host_state_bytes - bytes)
            throw std::length_error("host state exceeds limit");
        if constexpr (std::endian::native == std::endian::little) {
            const auto raw = std::as_bytes(values);
            data.insert(data.end(), raw.begin(), raw.end());
        } else {
            for (auto v : values)
                sample(v);
        }
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
std::size_t encoded_words(const HostStateView& state) noexcept {
    std::size_t words = 3; // Magic, version, flags.
    const auto format = state_version(state);
    if (format >= 5)
        words += name_words(state.preset_name.size()) + (state.settings ? settings_words + 1 : 0);
    else
        words += settings_words_in(format);
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
std::size_t encoded_host_state_size(const HostStateView& state) noexcept {
    return word_bytes(encoded_words(state));
}
HostStateView view_of(const HostState& state) noexcept {
    return {state.configuration ? &*state.configuration : nullptr, state.bypass, state.failed,
            state.settings, state.preset_name};
}
std::size_t encoded_host_state_size(const HostState& state) noexcept {
    return encoded_host_state_size(view_of(state));
}
std::vector<std::byte> encode_host_state(const HostState& state) {
    return encode_host_state(view_of(state));
}
Advice host_state_size_error(const HostState& whole) {
    const auto state = view_of(whole);
    // PLUG-13: a fixed room for the settings is kept whether or not the
    // state holds them yet, so a filter accepted at default settings still
    // saves once a VST3 setting changes (jrp's review of 276c7dc), now and
    // after later settings are added.
    static_assert(settings_words_in(version) * 4 <= host_settings_room_bytes);
    // PLUG-53: the preset name's words come out of the same room.
    auto words = encoded_words(state);
    const auto format = state_version(state);
    words -= format >= 5
                 ? name_words(state.preset_name.size()) + (state.settings ? settings_words + 1 : 0)
                 : settings_words_in(format);
    if (state.configuration)
        words = add(words, host_settings_room_bytes / 4);
    const auto size = word_bytes(words);
    if (size <= max_host_state_bytes)
        return {};
    // Each sample takes 4 bytes, less the room kept; the rest (rates,
    // counts, routes) is small.
    constexpr auto limit_samples = (max_host_state_bytes - host_settings_room_bytes) / 4;
    const auto n = state.configuration ? samples(*state.configuration) : 0;
    return {"the filter is too large to save: its " + std::to_string(n) + " samples would take " +
                std::to_string(size) + " bytes",
            "Projects and presets hold at most " + std::to_string(max_host_state_bytes) +
                " bytes (64 MiB) of Convolver's data, with " +
                std::to_string(host_settings_room_bytes) +
                " bytes kept for the plug-in's settings and a preset name: just under " +
                std::to_string(limit_samples) +
                " samples of all the impulse responses together. Previews up to 0.19 kept less "
                "room, so a project or preset they saved within 960 samples of their limit no "
                "longer opens.",
            "Shorten the impulse responses (most of a long one's end is usually silence) or use "
            "fewer of them.",
            "Plug-ins refuse it so that a project never holds a filter it cannot save; the "
            "convolver command-line tool can still render with it."};
}
std::vector<std::byte> encode_host_state(const HostStateView& state) {
    if (state.configuration)
        validate_configuration(*state.configuration);
    if (state.failed && state.configuration)
        throw std::invalid_argument("a failed load holds no filter");
    if (!state.preset_name.empty() && !storable_preset_name(state.preset_name))
        throw std::invalid_argument("the preset name cannot be saved");
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
    w.word((state.configuration ? 1 : 0) | (state.bypass ? 2 : 0) | (state.failed ? 4 : 0) |
           (format >= 5 && state.settings ? settings_flag : 0));
    if (format >= 5) {
        const auto& name = state.preset_name;
        w.word(name.size());
        for (std::size_t i = 0; i < name.size(); i += 4) {
            std::uint32_t packed = 0;
            for (std::size_t j = 0; j < 4 && i + j < name.size(); ++j)
                packed |= std::uint32_t{static_cast<unsigned char>(name[i + j])} << (8 * j);
            w.word(packed);
        }
    }
    if (format >= 3 && (format < 5 || state.settings)) {
        w.number(state.settings->gain_db);
        w.number(state.settings->mix);
        w.word(state.settings->latency);
        if (format >= 4)
            w.word(static_cast<std::size_t>(state.settings->gain_mode));
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
            w.samples(p.impulse);
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
namespace {
// Everything before the filter, for both decoders.
HostStateHeader read_header(Reader& r) {
    if (r.word() != magic)
        throw std::invalid_argument("it was not saved by Convolver");
    const auto format = r.word();
    if (format > version)
        throw detail::AdvisedError<NewerHostState>(
            {"it was saved by a newer version of Convolver (state version " +
                 std::to_string(format) + "; this version reads versions 1 to " +
                 std::to_string(version) + ")",
             {},
             "Update Convolver to load it, or make the preset again from its .cfg with this "
             "version's convolver-preset.",
             {}});
    if (format < 1)
        throw std::invalid_argument("unsupported host state version " + std::to_string(format));
    auto flags = r.word();
    // Version 2 only ever marks a failed load (flag 4); a failed load never
    // holds a filter. Versions 3 to 5 may mark one or not; only version 5
    // has flag 8 (the settings follow).
    if (flags > (format == 1  ? 3U
                 : format < 5 ? 7U
                              : 15U) ||
        (flags & 5) == 5 || (format == 2 && !(flags & 4)))
        throw std::invalid_argument("invalid host state flags");
    HostStateHeader header;
    header.filter = (flags & 1) != 0;
    header.bypass = (flags & 2) != 0;
    header.failed = (flags & 4) != 0;
    if (format >= 5) {
        const auto size = r.count(max_preset_name_bytes, 1);
        std::string name(size, '\0');
        for (std::size_t i = 0; i < size; i += 4) {
            const auto packed = r.word();
            for (std::size_t j = 0; j < 4; ++j) {
                const auto byte = static_cast<char>((packed >> (8 * j)) & 0xff);
                if (i + j < size)
                    name[i + j] = byte;
                else if (byte != 0)
                    throw std::invalid_argument("invalid host state preset name");
            }
        }
        // Version 5 is written only for a name, and only a storable one.
        if (!storable_preset_name(name))
            throw std::invalid_argument("invalid host state preset name");
        header.preset_name = std::move(name);
    }
    if (format >= 3 && (format < 5 || (flags & settings_flag))) {
        HostSettings settings;
        settings.gain_db = r.number();
        settings.mix = r.number();
        settings.latency = r.word();
        if (format >= 4) {
            const auto mode = r.word();
            // Version 4 is written only for a mode other than Set; version 5
            // for any.
            if ((format == 4 && mode == 0) || mode >= gain_mode_count)
                throw std::invalid_argument("invalid host state settings");
            settings.gain_mode = static_cast<GainMode>(mode);
        }
        if (!std::isfinite(settings.gain_db) || settings.gain_db < min_gain_db ||
            settings.gain_db > max_gain_db || !std::isfinite(settings.mix) || settings.mix < 0 ||
            settings.mix > 1 ||
            std::find(host_latencies.begin(), host_latencies.end(), settings.latency) ==
                host_latencies.end())
            throw std::invalid_argument("invalid host state settings");
        header.settings = settings;
    }
    return header;
}
} // namespace
HostStateHeader decode_host_state_header(std::span<const std::byte> start, std::size_t size) {
    if (size > max_host_state_bytes)
        throw std::length_error("host state exceeds limit");
    if (start.size() > size)
        throw std::invalid_argument("host state header beyond the state");
    Reader r{start.first(std::min(start.size(), host_state_header_bytes))};
    auto header = read_header(r);
    // With no filter the state ends with its header, as decode_host_state()
    // requires.
    const auto read = std::min(start.size(), host_state_header_bytes) - r.data.size();
    if (!header.filter && read != size)
        throw std::invalid_argument("trailing host state bytes");
    return header;
}
HostState decode_host_state(std::span<const std::byte> bytes) {
    if (bytes.size() > max_host_state_bytes)
        throw std::length_error("host state exceeds limit");
    Reader r{bytes};
    auto header = read_header(r);
    HostState state;
    state.bypass = header.bypass;
    state.failed = header.failed;
    state.settings = header.settings;
    state.preset_name = std::move(header.preset_name);
    if (header.filter) {
        Configuration c;
        auto rate = r.word();
        if (rate > static_cast<std::uint32_t>(max_sample_rate))
            throw std::invalid_argument("invalid sample rate");
        c.sample_rate = static_cast<int>(rate);
        c.input_channels = r.word();
        c.output_channels = r.word();
        c.channel_mask = r.word();
        for (auto* delays : {&c.input_delays, &c.output_delays}) {
            auto n = r.count(max_channel_count);
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
// In state.cpp, beside the codec it matches, so the state's fuzz target
// needs nothing of HostFilter (CI-34).
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

bool same_host_state(const HostStateView& a, const HostStateView& b) noexcept {
    // What encode_host_state() writes: the version (from the name and the
    // settings), the flags, the name, the settings it keeps (doubles by
    // their bits, as it stores them) and the filter.
    const auto bits = [](double v) { return std::bit_cast<std::uint64_t>(v); };
    const auto format = state_version(a);
    if (format != state_version(b) || a.bypass != b.bypass || a.failed != b.failed ||
        a.preset_name != b.preset_name || a.settings.has_value() != b.settings.has_value() ||
        (a.configuration != nullptr) != (b.configuration != nullptr))
        return false;
    if (format >= 3 && a.settings) {
        const auto &x = *a.settings, &y = *b.settings;
        if (bits(x.gain_db) != bits(y.gain_db) || bits(x.mix) != bits(y.mix) ||
            x.latency != y.latency || (format >= 4 && x.gain_mode != y.gain_mode))
            return false;
    }
    return !a.configuration || a.configuration == b.configuration ||
           same_configuration(*a.configuration, *b.configuration);
}

bool storable_preset_name(std::string_view name) noexcept {
    return !name.empty() && name.size() <= max_preset_name_bytes && detail::valid_utf8(name) &&
           std::none_of(name.begin(), name.end(), [](char c) {
               const auto byte = static_cast<unsigned char>(c);
               return byte < 0x20 || byte == 0x7f;
           });
}
} // namespace convolver
