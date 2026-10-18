// SPDX-License-Identifier: GPL-2.0-or-later
#include "bytes.hpp"
#include "convolver/host.hpp"
#include <bit>
#include <limits>
#include <stdexcept>

namespace convolver {
namespace {
constexpr std::uint32_t magic = 0x53564e43, version = 1;
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
};
} // namespace
std::vector<std::byte> encode_host_state(const HostState& state) {
    if (state.configuration)
        validate_configuration(*state.configuration);
    Writer w;
    w.word(magic);
    w.word(version);
    w.word((state.configuration ? 1 : 0) | (state.bypass ? 2 : 0));
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
    if (r.word() != magic || r.word() != version)
        throw std::invalid_argument("unsupported host state");
    auto flags = r.word();
    if (flags > 3)
        throw std::invalid_argument("invalid host state flags");
    HostState state;
    state.bypass = (flags & 2) != 0;
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
