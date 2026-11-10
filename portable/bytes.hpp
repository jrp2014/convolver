// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

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
inline std::uint64_t read_le64(std::span<const std::byte> bytes) {
    if (bytes.size() < 8)
        throw std::invalid_argument("truncated 64-bit value");
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= std::to_integer<std::uint64_t>(bytes[i]) << (8 * i);
    return value;
}
inline void write_le32(std::span<std::byte> bytes, std::uint32_t value) {
    if (bytes.size() < 4)
        throw std::invalid_argument("short 32-bit destination");
    for (unsigned i = 0; i < 4; ++i)
        bytes[i] = std::byte((value >> (8 * i)) & 255);
}
inline void append_le32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        out.push_back(std::byte((value >> (8 * i)) & 255));
}
inline void append_le64(std::vector<std::byte>& out, std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i)
        out.push_back(std::byte((value >> (8 * i)) & 255));
}
// SplitMix64 (Steele, Lea and Flood, 2014): a small, fast generator for
// seeded noise (dither, and the attenuation estimate's test signal), the
// same sequence on every system.
inline std::uint64_t splitmix64(std::uint64_t& state) noexcept {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
} // namespace convolver::detail
