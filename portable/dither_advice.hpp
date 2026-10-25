// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// The command line's dither and noise-shaping defaults and advice. The library's
// Quantizer keeps every mode; the CLI offers only those that some sample rate
// favours, chooses the default by output resolution, and points to a better
// choice when the one given is pointless or clearly beaten. The reasons and
// measurements are in docs/use/command-line.md#integer-formats-dither-and-noise-shaping.
#include "convolver/io.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace convolver::cli {

// From this many significant bits, plain rounding is already inaudible.
inline constexpr unsigned inaudible_bits = 20;

// Parse a --dither or --noise-shaping name. Removed names throw
// std::invalid_argument saying why and what to use instead.
Dither parse_dither(std::string_view);
NoiseShaping parse_noise_shaping(std::string_view);
std::string dither_name(Dither);
std::string noise_shaping_name(NoiseShaping);

// How much further below the threshold of hearing a choice keeps the
// quantization noise than plain TPDF dither does, at a sample rate (dB, from
// measurements at the nearest measured rate; larger is less audible).
double audibility_gain_db(Dither, NoiseShaping, unsigned sample_rate);

struct QuantizationPlan {
    QuantizationOptions options;
    std::vector<std::string> notes; // lines for stderr, without the "convolver: " prefix
};
// container_bits is zero for float output. Options left empty were not given.
QuantizationPlan plan_quantization(unsigned container_bits, unsigned valid_bits,
                                   unsigned sample_rate, std::optional<Dither>,
                                   std::optional<NoiseShaping>, std::optional<std::uint64_t> seed);

} // namespace convolver::cli
