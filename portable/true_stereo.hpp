// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// EDGE-3: the four routes of a four-channel IR file played as true stereo,
// written once for the loader, --inspect and the plug-ins' Routing line
// (jrp's review of 547e2dd, §2). Private to Convolver.
#include <array>
#include <cstddef>

namespace convolver::detail {
struct TrueStereoRoute {
    std::size_t input, output;
};
// The file's channel i, in order: left to left, left to right, right to left,
// right to right (Steinberg's REVerence, SIR3, IR.lv2's convert4chan).
inline constexpr std::array<TrueStereoRoute, 4> true_stereo_routes{
    {{0, 0}, {0, 1}, {1, 0}, {1, 1}}};
} // namespace convolver::detail
