// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
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
        // as doubles, low word first, then the latency), and is written only
        // when they differ from the defaults.
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
        // Default settings keep the older versions, byte for byte.
        auto defaults = identity;
        defaults.settings = HostSettings{};
        require(encode_host_state(defaults) == encode_host_state(identity),
                "default settings must keep version 1");
        auto failed_defaults = failed;
        failed_defaults.settings = HostSettings{};
        require(encode_host_state(failed_defaults) == failed_bytes,
                "default settings must keep version 2");
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
        for (const unsigned char format : {4, 0xff}) {
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
        std::cout << "state codec passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
