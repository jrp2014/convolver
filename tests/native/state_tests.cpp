// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
using namespace convolver;
void require(bool v, const char* m) {
    if (!v)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f) {
    try {
        f();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error("invalid state accepted");
}
int main() {
    try {
        // Literal version-1 wire fixture: independently checks both directions,
        // including nontrivial rate and IEEE-754 coefficient bytes.
        const auto fixture = std::to_array<unsigned char>(
            {0x43, 0x4e, 0x56, 0x53, 1, 0, 0, 0, 1,    0,    0, 0, 0x80, 0xbb, 0,   0, 1, 0, 0,
             0,    1,    0,    0,    0, 0, 0, 0, 0,    0,    0, 0, 0,    0,    0,   0, 0, 1, 0,
             0,    0,    1,    0,    0, 0, 0, 0, 0x80, 0x3f, 1, 0, 0,    0,    0,   0, 0, 0, 0,
             0,    0x80, 0x3f, 1,    0, 0, 0, 0, 0,    0,    0, 0, 0,    0x80, 0x3f});
        const auto wire = std::as_bytes(std::span(fixture));
        const HostState identity{
            Configuration{48000, 1, 1, 0, {}, {}, {{{1.f}, {{0, 1.f}}, {{0, 1.f}}}}}, false};
        const auto encoded = encode_host_state(identity);
        require(std::equal(encoded.begin(), encoded.end(), wire.begin(), wire.end()),
                "version-1 wire encoding compatibility");
        const auto restored = decode_host_state(wire);
        require(!restored.bypass && restored.configuration->sample_rate == 48000 &&
                    restored.configuration->paths[0].impulse == std::vector<float>{1.f} &&
                    restored.configuration->paths[0].inputs[0].gain == 1.f &&
                    restored.configuration->paths[0].outputs[0].gain == 1.f,
                "version-1 wire decoding compatibility");
        // Version 2 adds only flag 4, a saved failed load; everything else is
        // still written as version 1 so older builds read it.
        HostState failed;
        failed.failed = true;
        failed.bypass = true;
        const auto failed_bytes = encode_host_state(failed);
        const auto failed_wire =
            std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 2, 0, 0, 0, 6, 0, 0, 0});
        require(std::equal(failed_bytes.begin(), failed_bytes.end(),
                           std::as_bytes(std::span(failed_wire)).begin(),
                           std::as_bytes(std::span(failed_wire)).end()),
                "version-2 failed-load wire encoding");
        const auto failed_back = decode_host_state(failed_bytes);
        require(failed_back.failed && failed_back.bypass && !failed_back.configuration,
                "version-2 failed-load decoding");
        require(std::to_integer<int>(encode_host_state(identity)[4]) == 1 &&
                    std::to_integer<int>(encode_host_state(HostState{})[4]) == 1,
                "states without a failed load must stay version 1");
        auto contradictory = failed;
        contradictory.configuration = identity.configuration;
        rejects([&] { (void)encode_host_state(contradictory); });
        for (auto bad :
             {std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 1, 0, 0, 0, 4, 0, 0, 0}),
              std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 2, 0, 0, 0, 8, 0, 0, 0}),
              // Version 2 without the failed flag is never written.
              std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 2, 0, 0, 0, 0, 0, 0, 0}),
              std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 2, 0, 0, 0, 2, 0, 0, 0}),
              // Version 3 without its settings words.
              std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 3, 0, 0, 0, 0, 0, 0, 0})})
            rejects([&] { (void)decode_host_state(std::as_bytes(std::span(bad))); });
        // VST3-4: version 3 adds the settings after the flags (gain and mix
        // as doubles, low word first, then the latency), and is written
        // whenever the state holds them (PLUG-35: defaults included).
        HostState set;
        set.bypass = true;
        set.settings = HostSettings{-6.5, .25, 1024};
        const auto set_bytes = encode_host_state(set);
        const auto set_wire = std::to_array<unsigned char>(
            {0x43, 0x4e, 0x56, 0x53, 3, 0, 0,    0,    2, 0, 0, 0, // Magic, version, bypass.
             0,    0,    0,    0,    0, 0, 0x1a, 0xc0,             // Gain -6.5 dB.
             0,    0,    0,    0,    0, 0, 0xd0, 0x3f,             // Mix 0.25.
             0,    4,    0,    0});                                // Latency 1024.
        require(std::equal(set_bytes.begin(), set_bytes.end(),
                           std::as_bytes(std::span(set_wire)).begin(),
                           std::as_bytes(std::span(set_wire)).end()),
                "version-3 settings wire encoding");
        const auto set_back = decode_host_state(set_bytes);
        require(set_back.bypass && !set_back.failed && !set_back.configuration &&
                    set_back.settings == set.settings,
                "version-3 settings decoding");
        auto set_filter = set;
        set_filter.configuration = identity.configuration;
        const auto set_filter_back = decode_host_state(encode_host_state(set_filter));
        require(set_filter_back.settings == set.settings &&
                    set_filter_back.configuration->sample_rate == 48000 &&
                    set_filter_back.configuration->paths[0].impulse == std::vector<float>{1.f},
                "version-3 settings with a filter");
        auto set_failed = set;
        set_failed.failed = true;
        const auto set_failed_back = decode_host_state(encode_host_state(set_failed));
        require(set_failed_back.failed && set_failed_back.settings == set.settings,
                "version-3 settings with a failed load");
        // PLUG-10: version 4 is version 3 with the gain mode after the
        // latency, written only when the mode is not Set (jrp, 12 November
        // 2026); the Set value of Gain is kept beside it.
        auto moded = set;
        moded.settings->gain_mode = GainMode::no_clipping;
        const auto moded_bytes = encode_host_state(moded);
        auto moded_wire = std::vector<unsigned char>(set_wire.begin(), set_wire.end());
        moded_wire[4] = 4;
        moded_wire.insert(moded_wire.end(), {2, 0, 0, 0}); // Clip-safe.
        require(std::equal(moded_bytes.begin(), moded_bytes.end(),
                           std::as_bytes(std::span(moded_wire)).begin(),
                           std::as_bytes(std::span(moded_wire)).end()),
                "version-4 gain mode wire encoding");
        require(decode_host_state(moded_bytes).settings == moded.settings,
                "version-4 gain mode decoding");
        auto moded_filter = moded;
        moded_filter.configuration = identity.configuration;
        moded_filter.settings = HostSettings{};
        moded_filter.settings->gain_mode = GainMode::estimate;
        const auto moded_filter_back = decode_host_state(encode_host_state(moded_filter));
        require(moded_filter_back.settings == moded_filter.settings &&
                    moded_filter_back.configuration->paths[0].impulse == std::vector<float>{1.f},
                "version-4 gain mode with a filter and otherwise default settings");
        // Set is never written as version 4, and a mode out of range is damage.
        for (const unsigned char bad_mode : std::to_array<unsigned char>({0, 3, 0xff})) {
            auto bytes_bad = moded_bytes;
            bytes_bad[32] = std::byte{bad_mode};
            rejects([&] { (void)decode_host_state(bytes_bad); });
        }
        rejects([&] {
            auto truncated = moded_bytes;
            truncated.resize(32);
            (void)decode_host_state(truncated);
        });
        // PLUG-53 (jrp, 26 November 2026): version 5 is written only for a
        // state with a preset name: after the flags, the name's length and
        // its bytes padded to whole words, then the settings when flag 8 is
        // set (with any gain mode), then the filter.
        HostState echo;
        echo.preset_name = "Echo";
        const auto echo_wire = std::to_array<unsigned char>(
            {0x43, 0x4e, 0x56, 0x53, 5, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0, 'E', 'c', 'h', 'o'});
        const auto echo_bytes = encode_host_state(echo);
        require(std::equal(echo_bytes.begin(), echo_bytes.end(),
                           std::as_bytes(std::span(echo_wire)).begin(),
                           std::as_bytes(std::span(echo_wire)).end()),
                "version-5 preset name wire encoding");
        require(decode_host_state(echo_bytes).preset_name == "Echo",
                "version-5 preset name decoding");
        auto named = identity;
        named.preset_name = "Stereo \xC3\xA9"
                            "cho";                  // 12 bytes: no padding.
        named.settings = HostSettings{-3, .5, 256}; // Set, written in version 5.
        const auto named_back = decode_host_state(encode_host_state(named));
        require(named_back.preset_name == named.preset_name &&
                    named_back.settings == named.settings &&
                    named_back.configuration->paths[0].impulse == std::vector<float>{1.f} &&
                    encode_host_state(named_back) == encode_host_state(named),
                "version-5 preset name with settings and a filter");
        auto odd = identity;
        odd.preset_name = "Halls"; // 5 bytes: three zero bytes of padding.
        const auto odd_bytes = encode_host_state(odd);
        require(odd_bytes.size() == encode_host_state(identity).size() + 12 &&
                    decode_host_state(odd_bytes).preset_name == "Halls" &&
                    !decode_host_state(odd_bytes).settings,
                "version-5 preset name padded, without settings");
        require(std::to_integer<int>(encode_host_state(identity)[4]) == 1 &&
                    std::to_integer<int>(moded_bytes[4]) == 4 &&
                    decode_host_state(encode_host_state(identity)).preset_name.empty(),
                "a state without a preset name must keep its older version");
        // The settings flag only in version 5; padding that is not zero, an
        // empty name, one longer than 200 bytes, invalid UTF-8 or a control
        // character is damage; an unstorable name is never written.
        auto settings_flag_v4 = moded_bytes;
        settings_flag_v4[8] = std::byte{8};
        rejects([&] { (void)decode_host_state(settings_flag_v4); });
        auto padding = odd_bytes;
        padding[16 + 5] = std::byte{'x'};
        rejects([&] { (void)decode_host_state(padding); });
        for (const auto& bad_wire :
             {std::vector<unsigned char>{0x43, 0x4e, 0x56, 0x53, 5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                         0},
              std::vector<unsigned char>{0x43, 0x4e, 0x56, 0x53, 5, 0, 0,    0,    0, 0,
                                         0,    0,    2,    0,    0, 0, 0xC3, 0x28, 0, 0},
              std::vector<unsigned char>{0x43, 0x4e, 0x56, 0x53, 5, 0, 0,   0,    0, 0,
                                         0,    0,    2,    0,    0, 0, 'a', '\t', 0, 0},
              std::vector<unsigned char>{0x43, 0x4e, 0x56, 0x53, 5, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0,
                                         'E', 'c'}})
            rejects([&] { (void)decode_host_state(std::as_bytes(std::span(bad_wire))); });
        auto long_name = std::vector<unsigned char>{0x43, 0x4e, 0x56, 0x53, 5,   0, 0, 0,
                                                    0,    0,    0,    0,    201, 0, 0, 0};
        long_name.resize(long_name.size() + 204, 'a');
        long_name.resize(long_name.size() - 3);
        long_name.insert(long_name.end(), {0, 0, 0});
        rejects([&] { (void)decode_host_state(std::as_bytes(std::span(long_name))); });
        HostState longest;
        longest.preset_name = std::string(max_preset_name_bytes, 'a');
        require(decode_host_state(encode_host_state(longest)).preset_name == longest.preset_name,
                "a 200-byte preset name must round-trip");
        for (const std::string& bad : {std::string(max_preset_name_bytes + 1, 'a'),
                                       std::string("a\tb"), std::string("\xC3\x28")}) {
            require(!storable_preset_name(bad) && storable_preset_name("Stereo echo"),
                    "storable preset names");
            HostState unstorable;
            unstorable.preset_name = bad;
            rejects([&] { (void)encode_host_state(unstorable); });
        }
        // PLUG-35: settings at the defaults are recorded too (version 3), so
        // a preset saved at the defaults is told apart from one without
        // settings; a state without settings keeps the older versions.
        auto defaults = identity;
        defaults.settings = HostSettings{};
        const auto defaults_bytes = encode_host_state(defaults);
        require(std::to_integer<int>(defaults_bytes[4]) == 3 &&
                    defaults_bytes.size() == encode_host_state(identity).size() + 20,
                "default settings must be written as version 3");
        const auto defaults_back = decode_host_state(defaults_bytes);
        require(defaults_back.settings == HostSettings{} &&
                    defaults_back.configuration->paths[0].impulse == std::vector<float>{1.f} &&
                    encode_host_state(defaults_back) == defaults_bytes,
                "default settings must round-trip as version 3");
        auto failed_defaults = failed;
        failed_defaults.settings = HostSettings{};
        const auto failed_defaults_back = decode_host_state(encode_host_state(failed_defaults));
        require(std::to_integer<int>(encode_host_state(failed_defaults)[4]) == 3 &&
                    failed_defaults_back.failed && failed_defaults_back.settings == HostSettings{},
                "default settings with a failed load must be written as version 3");
        require(!decode_host_state(encode_host_state(identity)).settings,
                "older states hold no settings");
        for (const HostSettings bad :
             {HostSettings{max_gain_db + 1, 1, 256}, HostSettings{0, 1.5, 256},
              HostSettings{0, 1, 300}, HostSettings{NAN, 1, 256}}) {
            // Written with the settings in range, then damaged on the wire.
            auto bytes_bad = set_bytes;
            const auto patch = [&](std::size_t at, std::uint64_t bits, int words) {
                for (int b = 0; b < 4 * words; ++b)
                    bytes_bad[at + static_cast<std::size_t>(b)] =
                        std::byte{static_cast<unsigned char>(bits >> (8 * b))};
            };
            patch(12, std::bit_cast<std::uint64_t>(bad.gain_db), 2);
            patch(20, std::bit_cast<std::uint64_t>(bad.mix), 2);
            patch(28, bad.latency, 1);
            rejects([&] { (void)decode_host_state(bytes_bad); });
        }
        // PLUG-8: a newer state version is told apart from damaged state.
        for (const unsigned char format : std::to_array<unsigned char>({6, 0xff})) {
            const auto newer =
                std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, format, 0, 0, 0, 0, 0, 0, 0});
            try {
                (void)decode_host_state(std::as_bytes(std::span(newer)));
                throw std::runtime_error("newer state version accepted");
            } catch (const NewerHostState& e) {
                const std::string why = e.what();
                require(why.find("newer version of Convolver") != std::string::npos &&
                            why.find("state version " + std::to_string(format)) !=
                                std::string::npos,
                        "newer state version not explained");
            }
        }
        for (auto other : {std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 0, 0, 0, 0}),
                           std::to_array<unsigned char>({1, 2, 3, 4, 1, 0, 0, 0})}) {
            try {
                (void)decode_host_state(std::as_bytes(std::span(other)));
                throw std::runtime_error("malformed state accepted");
            } catch (const NewerHostState&) {
                throw std::runtime_error("malformed state reported as newer");
            } catch (const std::invalid_argument&) {
            }
        }
        auto failed_with_filter = encode_host_state(identity);
        failed_with_filter[4] = std::byte{2};
        failed_with_filter[8] = std::byte{5};
        rejects([&] { (void)decode_host_state(failed_with_filter); });
        failed_with_filter[8] = std::byte{1};
        rejects([&] { (void)decode_host_state(failed_with_filter); });
        HostState state;
        state.bypass = true;
        state.configuration =
            Configuration{48000,
                          2,
                          2,
                          3,
                          {1, 2},
                          {3, 4},
                          {{{1, .5f, -.25f}, {{0, .75f}, {1, -1.f}}, {{1, .5f}}}}};
        auto bytes = encode_host_state(state);
        auto decoded = decode_host_state(bytes);
        auto& c = *decoded.configuration;
        require(decoded.bypass && c.sample_rate == 48000 && c.channel_mask == 3, "state metadata");
        require(c.input_delays == std::vector<std::size_t>{1, 2} &&
                    c.output_delays == std::vector<std::size_t>{3, 4},
                "state delays");
        require(c.paths[0].impulse == std::vector<float>{1, .5f, -.25f} &&
                    c.paths[0].inputs[1].gain == -1.f && c.paths[0].outputs[0].channel == 1,
                "state routing/coefficients");
        for (std::size_t n = 0; n < bytes.size(); ++n)
            rejects([&] { decode_host_state(std::span(bytes).first(n)); });
        auto corrupt = bytes;
        corrupt[0] = std::byte{0};
        rejects([&] { decode_host_state(corrupt); });
        corrupt = bytes;
        corrupt.push_back(std::byte{0});
        rejects([&] { decode_host_state(corrupt); });
        corrupt = bytes;
        for (std::size_t i = 8; i < 12; ++i)
            corrupt[i] = std::byte{255};
        rejects([&] { decode_host_state(corrupt); });
        state.configuration->paths[0].impulse[0] = NAN;
        rejects([&] { encode_host_state(state); });
        require(!decode_host_state(encode_host_state({})).configuration, "default state");
        // PLUG-58 (jrp's review of 7bd024f, §2.2): the header a VST3
        // controller reads alone says what the whole state says before its
        // filter, read from the whole state or from its start, in every
        // version; and for a state with no filter, so all header, it accepts
        // exactly what decode_host_state() accepts, whichever byte changes
        // (apart from one that marks a filter, which it does not read).
        {
            const auto agrees = [](const HostStateHeader& h, const HostState& s) {
                return h.bypass == s.bypass && h.failed == s.failed && h.settings == s.settings &&
                       h.preset_name == s.preset_name && h.filter == s.configuration.has_value();
            };
            const Configuration one{48000, 1, 1, 0, {}, {}, {{{.5f, .25f}, {{0, 1}}, {{0, 1}}}}};
            std::vector<HostState> states;
            for (const bool filter : {false, true})
                for (const bool bypass : {false, true})
                    for (const int kind : {0, 1, 2, 3}) { // None, Set, Estimated, named.
                        HostState s;
                        if (filter)
                            s.configuration = one;
                        else
                            s.failed = kind == 0;
                        s.bypass = bypass;
                        if (kind >= 1)
                            s.settings = HostSettings{
                                -6, .5, 512, kind == 1 ? GainMode::set : GainMode::estimate};
                        if (kind == 3)
                            s.preset_name = std::string(max_preset_name_bytes, 'h');
                        states.push_back(s);
                    }
            for (const auto& s : states) {
                const auto bytes = encode_host_state(s);
                const auto start =
                    std::span(bytes).first(std::min(bytes.size(), host_state_header_bytes));
                require(agrees(decode_host_state_header(bytes, bytes.size()), s) &&
                            agrees(decode_host_state_header(start, bytes.size()), s),
                        "the header does not say what the state says before its filter");
                // A start longer than the state, or a size past the limit,
                // is refused even when a filter follows the header.
                rejects([&] { (void)decode_host_state_header(bytes, bytes.size() - 1); });
                rejects([&] { (void)decode_host_state_header(start, max_host_state_bytes + 1); });
                if (s.configuration)
                    continue;
                for (std::size_t i = 0; i < bytes.size(); ++i)
                    for (const auto value : {0x00, 0x01, 0x10, 0x7f, 0xff}) {
                        auto changed = bytes;
                        changed[i] = static_cast<std::byte>(value);
                        std::optional<HostState> whole;
                        try {
                            whole = decode_host_state(changed);
                        } catch (const std::exception&) {
                        }
                        std::optional<HostStateHeader> header;
                        try {
                            header = decode_host_state_header(changed, changed.size());
                        } catch (const std::exception&) {
                        }
                        // A change that marks a filter leaves the header
                        // whole; only the full decode finds it missing.
                        if (header && header->filter && !whole)
                            continue;
                        require(whole.has_value() == header.has_value() &&
                                    (!whole || agrees(*header, *whole)),
                                "the header and the whole state differ on a changed byte");
                    }
                for (std::size_t n = 0; n < bytes.size(); ++n)
                    rejects([&] { (void)decode_host_state_header(std::span(bytes).first(n), n); });
                auto longer = bytes;
                longer.push_back(std::byte{0});
                rejects([&] { (void)decode_host_state_header(longer, longer.size()); });
            }
        }
        std::cout << "state codec passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
