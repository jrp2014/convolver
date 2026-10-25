// SPDX-License-Identifier: GPL-2.0-or-later
#include "dither_advice.hpp"
#include <array>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace convolver::cli {
namespace {
// The choices the command line offers, in the order of the measured columns.
struct Choice {
    Dither dither;
    NoiseShaping shaping;
};
constexpr std::array<Choice, 8> choices{{
    {Dither::triangular, NoiseShaping::none},
    {Dither::triangular, NoiseShaping::simple},
    {Dither::triangular, NoiseShaping::third_order},
    {Dither::triangular, NoiseShaping::ninth_order},
    {Dither::triangular_highpass, NoiseShaping::none},
    {Dither::triangular_highpass, NoiseShaping::simple},
    {Dither::triangular_highpass, NoiseShaping::third_order},
    {Dither::triangular_highpass, NoiseShaping::ninth_order},
}};
struct Row {
    unsigned rate;
    std::array<double, choices.size()> margin;
};
// Worst-band margin (dB) of the quantization noise below the threshold of
// hearing in quiet (ISO 226:2003), 16-bit output with full scale at 120 dB SPL,
// measured with this Quantizer on 25 October 2026. Only differences between
// columns are used, and they do not depend on the bit depth. The method is in
// docs/use/command-line.md#how-the-advice-was-measured.
constexpr std::array<Row, 14> measured{{
    {8000, {-22.5, -28.4, -32.1, -49.2, -24.6, -30.5, -34.3, -51.4}},
    {11025, {-21.2, -26.4, -29.0, -44.1, -22.8, -28.0, -30.7, -46.3}},
    {16000, {-19.6, -22.6, -22.5, -32.6, -19.6, -22.6, -23.3, -34.8}},
    {22050, {-18.2, -18.8, -15.6, -29.3, -16.7, -17.5, -15.3, -31.5}},
    {32000, {-16.5, -14.3, -11.5, -23.0, -13.8, -11.5, -13.3, -25.0}},
    {44100, {-15.2, -10.2, -5.9, 1.4, -11.6, -6.6, -6.5, 2.4}},
    {48000, {-14.9, -9.1, -4.1, 1.8, -11.0, -5.3, -4.4, 2.3}},
    {64000, {-13.6, -5.4, -2.7, 0.0, -9.4, -1.2, 1.8, 3.0}},
    {88200, {-12.1, -1.2, -1.9, -1.5, -7.8, 3.3, 2.5, 2.9}},
    {96000, {-11.7, -0.1, -1.7, -1.7, -7.3, 4.3, 2.9, 2.8}},
    {176400, {-9.0, 7.9, 0.4, -0.9, -4.4, 12.4, 5.1, 3.6}},
    {192000, {-8.9, 8.9, 0.6, -0.8, -4.1, 13.6, 5.6, 4.0}},
    {352800, {-6.0, 16.8, 3.3, 1.4, -1.4, 21.6, 8.2, 6.2}},
    {384000, {-5.8, 18.0, 3.7, 1.6, -1.0, 22.9, 8.2, 6.5}},
}};
// A better choice is pointed out only when it gains at least this much.
constexpr double clearly_better_db = 3.0;

const Row& nearest(unsigned rate) {
    const Row* best = &measured.front();
    const double target = std::log(static_cast<double>(rate ? rate : 1));
    for (const auto& row : measured)
        if (std::abs(std::log(static_cast<double>(row.rate)) - target) <
            std::abs(std::log(static_cast<double>(best->rate)) - target))
            best = &row;
    return *best;
}
std::optional<std::size_t> column(Dither d, NoiseShaping s) {
    for (std::size_t i = 0; i < choices.size(); ++i)
        if (choices[i].dither == d && choices[i].shaping == s)
            return i;
    return std::nullopt;
}
std::string options_text(Dither d, NoiseShaping s) {
    std::string text = "--dither " + dither_name(d);
    if (s != NoiseShaping::none)
        text += " --noise-shaping " + noise_shaping_name(s);
    return text;
}
std::string db(double value) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(0) << value;
    return text.str();
}
// The best measured choice at this rate, and how much less audible it is than
// the given one; nothing when no choice is clearly better.
std::optional<std::pair<Choice, double>> better(Dither d, NoiseShaping s, unsigned rate) {
    const auto& row = nearest(rate);
    const auto own = column(d, s);
    if (!own)
        return std::nullopt;
    std::size_t best = 0;
    for (std::size_t i = 1; i < choices.size(); ++i)
        if (row.margin[i] > row.margin[best])
            best = i;
    const double gain = row.margin[best] - row.margin[*own];
    if (gain < clearly_better_db)
        return std::nullopt;
    return std::pair{choices[best], gain};
}
} // namespace

Dither parse_dither(std::string_view name) {
    if (name == "none")
        return Dither::none;
    if (name == "triangular")
        return Dither::triangular;
    if (name == "triangular-highpass")
        return Dither::triangular_highpass;
    if (name == "rectangular")
        throw std::invalid_argument(
            "--dither rectangular is no longer offered: its noise rises and falls with the "
            "signal (noise modulation), which triangular dither removes for 1.8 dB more noise. "
            "Use triangular (the default below 20 bits) or triangular-highpass");
    throw std::invalid_argument("unsupported dither: " + std::string(name) +
                                " (choose none, triangular or triangular-highpass)");
}
NoiseShaping parse_noise_shaping(std::string_view name) {
    if (name == "none")
        return NoiseShaping::none;
    if (name == "simple")
        return NoiseShaping::simple;
    if (name == "third-order")
        return NoiseShaping::third_order;
    if (name == "ninth-order")
        return NoiseShaping::ninth_order;
    if (name == "second-order")
        throw std::invalid_argument(
            "--noise-shaping second-order is no longer offered: third-order gives the same "
            "result (within 1 dB at every sample rate measured, 8 to 384 kHz). Use third-order");
    if (name == "sony-sbm")
        throw std::invalid_argument(
            "--noise-shaping sony-sbm is no longer offered: at every sample rate another choice "
            "keeps the noise at least 3 dB less audible (measured from 8 to 384 kHz; for example "
            "ninth-order at 44.1 and 48 kHz, simple from 88.2 kHz). Leave it out and Convolver "
            "will name a better choice for your sample rate if there is one");
    throw std::invalid_argument("unsupported noise shaping: " + std::string(name) +
                                " (choose none, simple, third-order or ninth-order)");
}
std::string dither_name(Dither d) {
    switch (d) {
    case Dither::none:
        return "none";
    case Dither::rectangular:
        return "rectangular";
    case Dither::triangular:
        return "triangular";
    case Dither::triangular_highpass:
        return "triangular-highpass";
    }
    return "unknown";
}
std::string noise_shaping_name(NoiseShaping s) {
    switch (s) {
    case NoiseShaping::none:
        return "none";
    case NoiseShaping::simple:
        return "simple";
    case NoiseShaping::second_order:
        return "second-order";
    case NoiseShaping::third_order:
        return "third-order";
    case NoiseShaping::ninth_order:
        return "ninth-order";
    case NoiseShaping::sony_sbm:
        return "sony-sbm";
    }
    return "unknown";
}
double audibility_gain_db(Dither d, NoiseShaping s, unsigned rate) {
    const auto own = column(d, s);
    if (!own)
        throw std::invalid_argument("no measurements for this dither and noise shaping");
    const auto& row = nearest(rate);
    return row.margin[*own] - row.margin[0];
}

QuantizationPlan plan_quantization(unsigned container_bits, unsigned valid_bits,
                                   unsigned sample_rate, std::optional<Dither> dither,
                                   std::optional<NoiseShaping> shaping,
                                   std::optional<std::uint64_t> seed) {
    QuantizationPlan plan;
    const unsigned bits = valid_bits ? valid_bits : container_bits;
    const bool audible = bits && bits < inaudible_bits;
    const auto d = dither.value_or(audible ? Dither::triangular : Dither::none);
    const auto s = shaping.value_or(NoiseShaping::none);
    plan.options = {d, seed.value_or(0), s, valid_bits};
    const auto rate = std::to_string(sample_rate) + " Hz";
    const auto depth = std::to_string(bits) + "-bit";

    if (bits && !audible && (d != Dither::none || s != NoiseShaping::none)) {
        std::string text = "warning: dither and noise shaping are pointless at " +
                           std::to_string(bits) +
                           " significant bits: plain rounding is already inaudible, even with "
                           "peaks at 120 dB SPL, so they only add noise";
        if (bits == 32)
            text += " (and from Convolver's float output, 32-bit rounding is exact above "
                    "-48 dBFS)";
        plan.notes.push_back(text + ". Leave them off, the default from " +
                             std::to_string(inaudible_bits) + " bits");
    } else if (audible && d == Dither::none) {
        plan.notes.push_back(
            "warning: without dither, " + depth +
            " output turns fades, reverb tails and quiet passages into distortion instead of "
            "steady hiss, which can be heard at high playback levels. Leave --dither at its "
            "default, triangular, unless you need plain rounding");
    } else if (audible && !dither) {
        // Say so whenever dither was added without being asked for, with or
        // without --noise-shaping.
        std::string text = depth + " output: adding triangular (TPDF) dither, the default below " +
                           std::to_string(inaudible_bits) + " bits (--dither none turns it off)";
        if (!shaping)
            if (const auto b = better(d, s, sample_rate))
                text += ". At " + rate + ", " + options_text(b->first.dither, b->first.shaping) +
                        " would keep the noise about " + db(b->second) + " dB less audible";
        plan.notes.push_back(text);
    }
    // A choice made explicitly is compared with the others at this rate.
    if (audible && d != Dither::none && (dither || shaping)) {
        if (const auto b = better(d, s, sample_rate))
            plan.notes.push_back("warning: at " + rate + ", " +
                                 options_text(b->first.dither, b->first.shaping) +
                                 " keeps the noise about " + db(b->second) +
                                 " dB less audible than " + options_text(d, s));
    }
    if (seed && d == Dither::none)
        plan.notes.push_back("note: --dither-seed has no effect without dither");
    return plan;
}

} // namespace convolver::cli
