// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <algorithm>
#include <array>
#include <cmath>
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
