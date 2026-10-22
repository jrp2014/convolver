// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <stdexcept>
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
              std::to_array<unsigned char>({0x43, 0x4e, 0x56, 0x53, 3, 0, 0, 0, 0, 0, 0, 0})})
            rejects([&] { (void)decode_host_state(std::as_bytes(std::span(bad))); });
        auto failed_with_filter = encode_host_state(identity);
        failed_with_filter[4] = std::byte{2};
        failed_with_filter[8] = std::byte{5};
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
