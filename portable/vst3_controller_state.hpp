// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// PLUG-22: the VST3 controller state (Controller::setState and getState):
// the name of the filter file, with its extension and never its folder, and
// the identity of the filter it names (HostReport::filter_identity), so the
// window names a restored filter only while that filter plays. PLUG-31:
// convolver-preset writes the same bytes as a .vstpreset's 'Cont' chunk, so
// it needs no plug-in SDK. The format: "CVCS", a version (1), then the name
// and the identity, each as a little-endian 32-bit length and UTF-8 bytes.
// Decoding anything else (empty, short, another version, a name longer than
// max_filter_name_bytes, an identity longer than 64 bytes) gives two empty
// strings, as does an empty name or identity: before PLUG-22 hosts saved an
// empty controller state, which reads as no name, never as a failure.
#include "bytes.hpp"
#include "convolver/host.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace convolver::vst3 {
struct ControllerState {
    std::string name, identity;
};
inline constexpr std::string_view controller_magic = "CVCS";
inline constexpr std::uint32_t controller_version = 1;
inline constexpr std::size_t max_controller_identity_bytes = 64;

namespace controller_detail {
inline void append_le32(std::string& out, std::uint32_t value) {
    std::array<std::byte, 4> bytes{};
    detail::write_le32(bytes, value);
    for (const auto byte : bytes)
        out.push_back(static_cast<char>(byte));
}
inline std::optional<std::uint32_t> take_le32(std::string_view& in) {
    if (in.size() < 4)
        return std::nullopt;
    const auto value = detail::read_le32(std::as_bytes(std::span(in.data(), 4)));
    in.remove_prefix(4);
    return value;
}
inline std::optional<std::string> take_text(std::string_view& in, std::size_t most) {
    const auto size = take_le32(in);
    if (!size || *size > most || *size > in.size())
        return std::nullopt;
    std::string text(in.substr(0, *size));
    in.remove_prefix(*size);
    return text;
}
} // namespace controller_detail

inline std::string encode_controller_state(const ControllerState& state) {
    using controller_detail::append_le32;
    std::string out(controller_magic);
    append_le32(out, controller_version);
    append_le32(out, static_cast<std::uint32_t>(state.name.size()));
    out += state.name;
    append_le32(out, static_cast<std::uint32_t>(state.identity.size()));
    out += state.identity;
    return out;
}
inline ControllerState decode_controller_state(std::string_view in) {
    using controller_detail::take_le32;
    using controller_detail::take_text;
    if (!in.starts_with(controller_magic))
        return {};
    in.remove_prefix(controller_magic.size());
    if (take_le32(in) != controller_version)
        return {};
    auto name = take_text(in, max_filter_name_bytes);
    auto identity = take_text(in, max_controller_identity_bytes);
    if (!name || !identity || name->empty() || identity->empty())
        return {};
    return {std::move(*name), std::move(*identity)};
}
} // namespace convolver::vst3
