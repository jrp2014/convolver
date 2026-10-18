// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace convolver::detail {
// Byte operations avoid host-endian and alignment assumptions. A short span is
// rejected before reading or writing any byte; trailing bytes are left alone.
inline std::uint16_t read_le16(std::span<const std::byte> bytes) {
    if (bytes.size() < 2)
        throw std::invalid_argument("truncated 16-bit value");
    return static_cast<std::uint16_t>(std::to_integer<std::uint16_t>(bytes[0]) |
                                      (std::to_integer<std::uint16_t>(bytes[1]) << 8));
}
inline std::uint32_t read_le32(std::span<const std::byte> bytes) {
    if (bytes.size() < 4)
        throw std::invalid_argument("truncated 32-bit value");
    return std::to_integer<std::uint32_t>(bytes[0]) |
           (std::to_integer<std::uint32_t>(bytes[1]) << 8) |
           (std::to_integer<std::uint32_t>(bytes[2]) << 16) |
           (std::to_integer<std::uint32_t>(bytes[3]) << 24);
}
inline void write_le32(std::span<std::byte> bytes, std::uint32_t value) {
    if (bytes.size() < 4)
        throw std::invalid_argument("short 32-bit destination");
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = std::byte((value >> (8 * i)) & 255);
}
} // namespace convolver::detail
