// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-10: HostProcessor's settings (gain and mix), checked against the
// direct double-precision oracle, sample by sample through their ramps, and
// without allocation on the audio thread.
#include "cases.hpp"
#include "convolver/host.hpp"
#include "host_log.hpp"
#include "oracle.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

thread_local bool audio_thread = false;
void* operator new(std::size_t n) {
    if (audio_thread)
        std::abort();
    if (auto p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (audio_thread && p)
        std::abort();
    std::free(p);
}
#include "replaced_allocation.hpp"

using namespace convolver;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
double worst_relative = 0; // The worst oracle error, relative to the case's output peak.
std::string worst_case;
double worst_mode_relative = 0; // The same for the Estimate and No clipping cases.

// A random filter with cross paths and route gains, as the PERF-5 test uses.
Configuration random_filter(std::mt19937& random, std::size_t inputs, std::size_t outputs,
                            std::size_t taps) {
    std::uniform_real_distribution<float> sample(-.5f, .5f);
    Configuration c{48000, inputs, outputs, 0, {}, {}, {}};
    for (std::size_t o = 0; o < outputs; ++o) {
        FilterPath p;
        p.impulse.resize(taps + random() % 17);
        for (auto& x : p.impulse)
            x = sample(random);
        p.inputs = {{o % inputs, 1}};
        p.outputs = {{o, .5f + sample(random)}};
        c.paths.push_back(std::move(p));
    }
    // One cross path, so channels are not independent.
    FilterPath cross;
    cross.impulse = {.25f, -.125f, .0625f};
    cross.inputs = {{inputs - 1, 1}};
    cross.outputs = {{0, 1}};
    c.paths.push_back(cross);
    return c;
}

// Plays `input` (interleaved) through `host` in callbacks of `callback`
// frames and returns the interleaved output.
std::vector<float> play(HostProcessor& host, const std::vector<float>& input, std::size_t inputs,
                        std::size_t outputs, std::size_t callback) {
    const auto frames = input.size() / inputs;
    std::vector<float> output(frames * outputs);
    std::vector<float> in_block(inputs * callback), out_block(outputs * callback);
    std::vector<const float*> in(inputs);
    std::vector<float*> out(outputs);
    for (std::size_t c = 0; c < inputs; ++c)
        in[c] = in_block.data() + c * callback;
    for (std::size_t c = 0; c < outputs; ++c)
        out[c] = out_block.data() + c * callback;
    for (std::size_t done = 0; done < frames; done += callback) {
        const auto n = std::min(callback, frames - done);
        for (std::size_t t = 0; t < n; ++t)
            for (std::size_t c = 0; c < inputs; ++c)
                in_block[c * callback + t] = input[(done + t) * inputs + c];
        audio_thread = true;
        const bool ok = host.process(in, out, n);
        audio_thread = false;
        require(ok, "the host refused a callback");
        for (std::size_t t = 0; t < n; ++t)
            for (std::size_t c = 0; c < outputs; ++c)
                output[(done + t) * outputs + c] = out_block[c * callback + t];
    }
    return output;
}

std::vector<float> noise(std::mt19937& random, std::size_t samples, float level = .5f) {
    std::uniform_real_distribution<float> sample(-level, level);
    std::vector<float> v(samples);
    for (auto& x : v)
        x = sample(random);
    return v;
}

// The defaults leave every output sample exactly as before the settings
// existed: the processor built without settings and the one built with the
// default settings agree bit for bit, also through bypass and a replacement.
void defaults_unchanged() {
    std::mt19937 random(0x5e771);
    const auto first = random_filter(random, 2, 2, 300), second = random_filter(random, 2, 2, 200);
    HostProcessor plain(first, 100, default_host_latency);
    HostProcessor set(first, 100, HostSettings{});
    require(set.latency_frames() == default_host_latency, "default latency");
    const auto input = noise(random, 2 * 3000);
    const std::vector<float> part1(input.begin(), input.begin() + 2000),
        part2(input.begin() + 2000, input.end());
    auto a = play(plain, part1, 2, 2, 100), b = play(set, part1, 2, 2, 100);
    plain.set_bypass(true);
    set.set_bypass(true);
    plain.replace(second);
    set.replace(second);
    auto c = play(plain, part2, 2, 2, 37), d = play(set, part2, 2, 2, 37);
    a.insert(a.end(), c.begin(), c.end());
    b.insert(b.end(), d.begin(), d.end());
    for (std::size_t i = 0; i < a.size(); ++i)
        require(std::bit_cast<std::uint32_t>(a[i]) == std::bit_cast<std::uint32_t>(b[i]),
                "default settings changed sample " + std::to_string(i));
}

// Gain and mix from the first frame, at each latency the plug-ins offer (the
// block size), against the oracle: output c is
// gain * mix * filtered + (1 - mix) * input c delayed by the block size.
void gain_and_mix_against_oracle() {
    std::mt19937 random(0x6a1);
    struct Case {
        std::size_t block, callback;
        double gain_db, mix;
    };
    for (const auto& k : {Case{256, 256, -6, .3}, Case{64, 100, 12, 1}, Case{1024, 333, -40, .75},
                          Case{128, 1, 24, 0}, Case{4096, 4096, -100, .5}, Case{512, 480, -3, .6},
                          Case{2048, 1000, 0, 1}}) {
        const auto c = random_filter(random, 2, 2, 3 * k.block / 2 + 5);
        HostSettings s;
        s.gain_db = k.gain_db;
        s.mix = k.mix;
        s.latency = k.block;
        HostProcessor host(c, k.callback, s);
        require(host.latency_frames() == k.block, "block size not used");
        const auto input = noise(random, 2 * (4 * k.block + 700));
        const auto output = play(host, input, 2, 2, k.callback);
        const auto wet = test::oracle(c, input, k.block);
        const double gain = std::pow(10.0, k.gain_db / 20);
        double peak = 0, error = 0;
        const auto frames = input.size() / 2;
        for (std::size_t t = 0; t < frames; ++t)
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const double dry = t >= k.block ? input[(t - k.block) * 2 + ch] : 0;
                const double expected = k.mix * gain * wet[t * 2 + ch] + (1 - k.mix) * dry;
                peak = std::max(peak, std::abs(expected));
                error = std::max(error, std::abs(output[t * 2 + ch] - expected));
            }
        const double relative = error / std::max(peak, 1e-30);
        char label[96];
        std::snprintf(label, sizeof label, "block %zu, callback %zu, gain %g dB, mix %g", k.block,
                      k.callback, k.gain_db, k.mix);
        if (relative > worst_relative) {
            worst_relative = relative;
            worst_case = label;
        }
        require(relative < 2e-6, std::string("gain and mix differ from the oracle: ") + label +
                                     ", relative error " + std::to_string(relative));
    }
}

// A single tap of `tap` on a mono path: output = tap * gain * input after the latency.
Configuration tap_filter(float tap, int rate = 48000) {
    return {rate, 1, 1, 0, {}, {}, {{{tap}, {{0, 1}}, {{0, 1}}}}};
}

// The output for constant input 1 through a one-tap filter, one call per
// sample, so that each frame shows the gain applied to it.
std::vector<float> per_sample(HostProcessor& host, std::size_t frames, float level = 1) {
    std::vector<float> out(frames);
    for (std::size_t t = 0; t < frames; ++t) {
        float in = level, result = 0;
        const float* ins[] = {&in};
        float* outs[] = {&result};
        audio_thread = true;
        const bool ok = host.process(ins, outs, 1);
        audio_thread = false;
        require(ok, "per-sample call refused");
        out[t] = result;
    }
    return out;
}

// A change of gain or mix ramps linearly over 64 frames, never in one step.
void ramps() {
    HostProcessor host(tap_filter(1), 1, HostSettings{0, 1, 64});
    per_sample(host, 200); // Past the latency: output 1.
    host.set_gain_db(-20);
    auto out = per_sample(host, 100);
    require(std::abs(out[0] - (1 - .9f / 64)) < 1e-6, "gain ramp does not start at once");
    for (std::size_t t = 1; t < 64; ++t)
        require(std::abs((out[t - 1] - out[t]) - .9f / 64) < 1e-5,
                "gain ramp not linear at frame " + std::to_string(t));
    for (std::size_t t = 63; t < out.size(); ++t)
        require(std::abs(out[t] - std::pow(10.0, -1.0)) < 1e-6, "gain not reached after 64 frames");
    // Mix: constant input, so wet and the delayed dry signal are both 0.1 and 1.
    host.set_mix(0);
    out = per_sample(host, 100);
    for (std::size_t t = 1; t < 64; ++t)
        require(out[t] > out[t - 1] && out[t] - out[t - 1] < .9f / 64 + 1e-5,
                "mix ramp not gradual at frame " + std::to_string(t));
    require(std::abs(out[99] - 1) < 1e-6, "mix 0 should play the dry input");
    // Out-of-range values are clamped.
    host.set_mix(1);
    host.set_gain_db(1000);
    out = per_sample(host, 100);
    require(std::abs(out[99] - std::pow(10.0, 24.0 / 20)) < 1e-4, "gain above +24 dB not clamped");
    host.set_gain_db(std::numeric_limits<double>::quiet_NaN());
    out = per_sample(host, 100);
    require(std::abs(out[99] - 1) < 1e-6, "a gain that is not a number should play 0 dB");
    // After a reset the stream starts again: a new gain applies at once.
    host.set_gain_db(-20);
    host.reset();
    out = per_sample(host, 200);
    require(out[63] == 0 && std::abs(out[64] - std::pow(10.0, -1.0)) < 1e-6,
            "gain after a reset should apply without a ramp");
}

// PLUG-17: the ramps and the crossfade last the same time at every rate:
// 64 and 256 frames at 48 kHz, so 59 and 235 at 44.1 kHz and 128 and 512 at
// 96 kHz. Each ramp's last step reaches its target, and none before.
void ramps_follow_time() {
    struct Rate {
        int hz;
        std::size_t ramp, crossfade;
    };
    for (const auto r : {Rate{44100, 59, 235}, Rate{48000, 64, 256}, Rate{96000, 128, 512}}) {
        const auto at = " at " + std::to_string(r.hz) + " Hz";
        // A gain ramp: the output falls each frame until frame ramp - 1.
        HostProcessor host(tap_filter(1, r.hz), 1, HostSettings{0, 1, 64});
        per_sample(host, 200);
        host.set_gain_db(-20);
        auto out = per_sample(host, r.ramp + 10);
        for (std::size_t t = 1; t + 1 < r.ramp; ++t)
            require(out[t] < out[t - 1] && out[t] > .1f + 1e-6f,
                    "gain ramp ended early" + at + ", frame " + std::to_string(t));
        require(std::abs(out[r.ramp - 1] - .1f) < 1e-6, "gain ramp not done after its length" + at);
        // Bypass: the output moves from the wet 0.1 to the dry input 1 over
        // the same length (a step a frame, so to within a frame of rounding).
        host.set_bypass(true);
        out = per_sample(host, r.ramp + 10);
        require(out[r.ramp - 3] < 1 - 1e-4f && std::abs(out[r.ramp] - 1) < 1e-5,
                "bypass ramp length" + at);
        host.set_bypass(false);
        per_sample(host, r.ramp + 10);
        // A replacement plays the old filter for one Latency block (64), then
        // crossfades for the rate's crossfade length.
        host.set_gain_db(0);
        per_sample(host, 200);
        host.replace(tap_filter(.5f, r.hz));
        out = per_sample(host, 64 + r.crossfade + 10);
        require(std::abs(out[63] - 1) < 1e-6,
                "the old filter did not play for the Latency block" + at);
        require(out[64 + r.crossfade - 2] > .5f + 1e-6f &&
                    std::abs(out[64 + r.crossfade - 1] - .5f) < 1e-6,
                "crossfade length" + at);
        host.collect();
    }
}

// PLUG-18, PLUG-19: bypass and mute end exactly at silence or the dry
// signal, at every rate and when a ramp is reversed midway, and do not end
// early. From the external review of 0.8.13: at 176.4 kHz a summed float step
// left about 2.4e-6 of unit input (-112 dB) for one frame after the ramp's 235
// frames. jrp (5 November 2026): that frame is inaudible, so the endpoint may
// come one frame after the ramp's last; reported latency and channel
// alignment have no such allowance.
void ramp_endpoints() {
    for (const int hz : {8000, 44100, 48000, 96000, 176400, 192000, 352800, 768000}) {
        const auto at = " at " + std::to_string(hz) + " Hz";
        const std::size_t ramp = (static_cast<std::size_t>(hz) * 64 + 24000) / 48000;
        // Output 1 of a one-input, two-output filter for constant input 1.
        const auto split = [&](HostProcessor& host, std::size_t frames) {
            std::vector<float> out(frames);
            for (std::size_t t = 0; t < frames; ++t) {
                float in = 1, a = 0, b = 0;
                const float* ins[] = {&in};
                float* outs[] = {&a, &b};
                audio_thread = true;
                const bool ok = host.process(ins, outs, 1);
                audio_thread = false;
                require(ok, "per-sample call refused" + at);
                out[t] = a;
            }
            return out;
        };
        // A ramp of `frames` frames, starting at out[0]: still moving on
        // its last frame but one, and exactly at `target` from one frame
        // after its last.
        const auto ends = [&](const std::vector<float>& out, std::size_t frames, float target,
                              const std::string& what) {
            require(out[frames - 2] != target, what + " ended early" + at);
            for (std::size_t t = frames; t < out.size(); ++t)
                require(out[t] == target, what + " not exactly at its end at frame " +
                                              std::to_string(t) + " of " + std::to_string(frames) +
                                              at);
        };
        // Unequal counts: bypass is silence (no dry signal).
        const Configuration unequal{hz, 1, 2, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}, {1, 1}}}}};
        HostProcessor host(unequal, 1, HostSettings{0, 1, 64});
        split(host, 200);
        host.set_bypass(true);
        ends(split(host, ramp + 10), ramp, 0, "unequal bypass");
        // Reversed after a third, then bypassed again: back to silence after
        // as many frames as it came out.
        const auto third = ramp / 3; // At least 3 (8 kHz: 11 frames).
        host.set_bypass(false);
        require(split(host, third).back() != 0, "bypass did not reverse" + at);
        host.set_bypass(true);
        ends(split(host, third + 10), third, 0, "reversed bypass");
        // Equal counts: bypass ends at the dry input, also when reversed.
        HostProcessor equal(tap_filter(.5f, hz), 1, HostSettings{0, 1, 64});
        per_sample(equal, 200);
        equal.set_bypass(true);
        ends(per_sample(equal, ramp + 10), ramp, 1, "bypass");
        equal.set_bypass(false);
        per_sample(equal, third);
        equal.set_bypass(true);
        ends(per_sample(equal, third + 10), third, 1, "reversed bypass");
        // Mute: the filter ramps to silence.
        equal.set_bypass(false);
        per_sample(equal, ramp + 10);
        audio_thread = true;
        equal.mute();
        audio_thread = false;
        ends(per_sample(equal, ramp + 10), ramp, 0, "mute");
    }
}

// With unequal counts there is no dry signal, so the mix stays 100% wet.
void unequal_mix() {
    Configuration c{48000, 1, 2, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}, {1, 1}}}}};
    HostSettings s;
    s.mix = 0;
    s.latency = 64;
    HostProcessor host(c, 1, s);
    for (int t = 0; t < 200; ++t) {
        float in = .5f, a = 0, b = 0;
        const float* ins[] = {&in};
        float* outs[] = {&a, &b};
        audio_thread = true;
        host.process(ins, outs, 1);
        audio_thread = false;
        if (t >= 64)
            require(std::abs(a - .5f) < 1e-6 && std::abs(b - .5f) < 1e-6,
                    "mix applied with unequal counts");
    }
}

// PLUG-10: the latency is one of the block sizes the plug-ins offer; any
// other is refused, so an adapter cannot build a processor it does not list.
void latency_choices() {
    for (const auto frames : host_latencies) {
        HostSettings s;
        s.latency = frames;
        HostProcessor host(tap_filter(1), 512, s);
        require(host.latency_frames() == frames, "latency " + std::to_string(frames) + " not used");
    }
    for (const std::size_t frames :
         {std::size_t{1}, std::size_t{32}, std::size_t{100}, std::size_t{8192}}) {
        HostSettings s;
        s.latency = frames;
        bool refused = false;
        try {
            HostProcessor host(tap_filter(1), 512, s);
        } catch (const std::invalid_argument& e) {
            refused = std::string(e.what()).find("64 to 4096") != std::string::npos;
        }
        require(refused, "latency " + std::to_string(frames) + " not refused with a reason");
    }
}

void settings_text() {
    require(settings_words(HostSettings{}, HostDrySignal::present) ==
                "gain 0 dB, mix 100% wet, latency 0 frames",
            "settings words: " + settings_words(HostSettings{}, HostDrySignal::present));
    HostSettings s{-6, .5, 1024};
    require(settings_words(s, HostDrySignal::present) ==
                "gain -6 dB, mix 50% wet, latency 1024 frames",
            "settings words: " + settings_words(s, HostDrySignal::present));
    require(settings_words(s, HostDrySignal::host_counts) ==
                "gain -6 dB, mix 100% wet (inputs and outputs differ, so there is no dry "
                "signal), latency 1024 frames",
            "settings words for unequal counts: " + settings_words(s, HostDrySignal::host_counts));
    // PLUG-10: the Gain mode's words, with the figure each filter plays.
    {
        HostSettings mode{-6, 1, 0, GainMode::estimate};
        const FilterGains both{-12.34, -6.02, false}, unestimated{std::nullopt, -6.02, false},
            too_loud{std::nullopt, -6.02, true}, none{std::nullopt, std::nullopt, true};
        for (const auto& [gains, words, note] :
             {std::tuple{&both, "gain -12.3 dB (Estimate)", "-12.3 dB for this filter"},
              std::tuple{&unestimated,
                         "gain -6 dB (No clipping, until the filter's Estimate is found)",
                         "-6 dB (No clipping until estimated)"},
              std::tuple{&too_loud,
                         "gain -6 dB (No clipping: the filter is too loud for an Estimate)",
                         "-6 dB (No clipping: too loud to estimate)"},
              std::tuple{&none,
                         "gain by Estimate: silence, as the filter's peak is beyond the range of a "
                         "no-clipping figure",
                         "silence: no figure for this filter"}}) {
            require(gain_words(mode, gains) == words, "gain words: " + gain_words(mode, gains));
            require(gain_note(mode, *gains, true) == note,
                    "gain note: " + gain_note(mode, *gains, true));
        }
        require(gain_words(mode, nullptr) == "gain by Estimate" &&
                    gain_note(mode, both, false) == "each filter plays at its own figure",
                "gain words without a filter");
        mode.gain_mode = GainMode::no_clipping;
        require(settings_words(mode, HostDrySignal::present, &both) ==
                    "gain -6 dB (No clipping), mix 100% wet, latency 0 frames",
                "settings words in No clipping: " +
                    settings_words(mode, HostDrySignal::present, &both));
        require(gain_mode_line(mode, both, true) == "Gain mode set to No clipping: gain -6 dB (No "
                                                    "clipping).",
                "gain mode line: " + gain_mode_line(mode, both, true));
        mode.gain_mode = GainMode::set;
        require(gain_note(mode, both, true).empty() &&
                    gain_mode_line(mode, both, true) ==
                        "Gain mode set to Set: gain -6 dB, the Gain setting.",
                "Set mode words: " + gain_mode_line(mode, both, true));
    }
    // GarageBand's sliders give values like these (jrp's run of AU-6).
    for (const auto& [settings, words] :
         {std::pair{HostSettings{-75.967, .561413}, "gain -76 dB, mix 56.1% wet"},
          std::pair{HostSettings{-12.04, .508}, "gain -12 dB, mix 50.8% wet"},
          std::pair{HostSettings{-.01, .9996}, "gain -0.01 dB, mix 99.96% wet"},
          std::pair{HostSettings{-99.99, .99999}, "gain -99.99 dB, mix 99.999% wet"},
          std::pair{HostSettings{-.04, .0004}, "gain -0.04 dB, mix 0.04% wet"}})
        require(settings_words(settings, HostDrySignal::present) ==
                    std::string(words) + ", latency 0 frames",
                "settings words: " + settings_words(settings, HostDrySignal::present));
    // The window's slider text: never an exponent, never a range end it is not.
    for (const auto& [value, text] :
         {std::pair{23.97, "23.97"}, std::pair{24.0, "24"}, std::pair{-1e-9, "0"},
          std::pair{1e-5, "0.00001"}, std::pair{-.0, "0"}, std::pair{-12.04, "-12"}})
        require(setting_text(value, min_gain_db, max_gain_db) == text,
                "setting text: " + setting_text(value, min_gain_db, max_gain_db));
    HostFilter filter;
    const auto line = started_line(filter, {48000, 2, 2}, 512, &s);
    require(line.find("Settings: gain -6 dB, mix 50% wet, latency 1024 frames.") !=
                std::string::npos,
            "started line without settings: " + line);
}

// jrp's review of 0.5.5: Status and the start line said "audio passes
// unchanged" with no filter at Gain -20 dB (the output was a tenth of the
// input), and "Silent" after a refused first load at Mix 50% (the dry half
// played). Each case plays a steady 0.5 through what the instance plays and
// checks the words against the level heard.
// PLUG-33: with no filter, Status names choosing a filter first where the
// plug-in has a window that chooses files, and only the preset otherwise,
// in every no-filter case, within the Status parameter's length.
void no_filter_names_choosing() {
    HostFilter empty;
    const HostLayout mono{48000, 1, 1}, split{48000, 1, 2};
    struct Case {
        std::optional<HostLayout> host;
        HostSettings settings;
        const char* start;
    };
    const std::vector<Case> cases{
        {std::nullopt, {}, "No filter loaded."},
        {split, {}, "No filter: silent, as inputs and outputs differ."},
        {mono, {-100, 1}, "No filter: audio passes at -100.0 dB (Gain and Mix)."},
        {mono, {}, "No filter: audio passes unchanged."},
    };
    for (const auto& c : cases) {
        const auto window = host_status(empty, c.host, true, c.settings, false, true).text;
        const auto none = host_status(empty, c.host, true, c.settings, false, false).text;
        require(window == std::string(c.start) + " Choose a filter or load a Convolver preset.",
                "with a window: " + window);
        require(none == std::string(c.start) + " Load a Convolver preset.",
                "without a window: " + none);
        require(window.size() <= max_status_text, "too long for Status: " + window);
    }
}
void diagnostics_follow_settings() {
    const auto contains = [](const std::string& text, const std::string& part) {
        return text.find(part) != std::string::npos;
    };
    struct Case {
        const char* what;
        HostLayout host;
        bool refused_first_load, filter_loaded;
        HostSettings settings;
        float heard;                  // The steady output for an input of 0.5.
        const char *status, *started; // Parts of the Status line and of started_line().
        HostStatus value;
    };
    const HostLayout mono{48000, 1, 1}, split{48000, 1, 2};
    const std::vector<Case> cases{
        {"no filter, defaults",
         mono,
         false,
         false,
         {},
         .5f,
         "No filter: audio passes unchanged.",
         "No filter, so audio passes unchanged.",
         HostStatus::no_filter},
        {"no filter, gain -20 dB",
         mono,
         false,
         false,
         {-20, 1},
         .05f,
         "No filter: audio passes at -20.0 dB (Gain and Mix).",
         "No filter, so audio passes at -20.0 dB, set by Gain and Mix.",
         HostStatus::no_filter},
        {"no filter, gain -20 dB, mix 50%",
         mono,
         false,
         false,
         {-20, .5},
         .275f,
         "audio passes at -5.2 dB",
         "audio passes at -5.2 dB",
         HostStatus::no_filter},
        {"no filter, gain -0.01 dB",
         mono,
         false,
         false,
         {-.01, 1},
         static_cast<float>(.5 * std::pow(10.0, -.01 / 20)),
         "audio passes at -0.01 dB",
         "audio passes at -0.01 dB",
         HostStatus::no_filter},
        {"no filter, gain -20 dB, mix 0%",
         mono,
         false,
         false,
         {-20, 0},
         .5f,
         "audio passes unchanged",
         "audio passes unchanged",
         HostStatus::no_filter},
        {"no filter, 1 in 2 out, mix 50%",
         split,
         false,
         false,
         {-20, .5},
         0,
         "No filter: silent, as inputs and outputs differ.",
         "Convolver plays silence.",
         HostStatus::no_filter},
        {"refused first load, defaults",
         mono,
         true,
         false,
         {},
         0,
         "Silent: the filter is for 44100 Hz",
         "Convolver plays silence. The filter is for 44100 Hz",
         HostStatus::load_failed},
        // No dry signal after a failed load with no filter kept, with one
        // input and one output too (jrp, 7 November 2026).
        {"refused first load, mix 50%",
         mono,
         true,
         false,
         {0, .5},
         0,
         "Silent: the filter is for 44100 Hz",
         "Convolver plays silence. The filter is for 44100 Hz",
         HostStatus::load_failed},
        {"refused first load, gain +24 dB, mix 0%",
         mono,
         true,
         false,
         {24, 0},
         0,
         "Silent: the filter is for 44100 Hz",
         "Convolver plays silence.",
         HostStatus::load_failed},
        {"refused first load, 2 in 2 out, mix 0%",
         {48000, 2, 2},
         true,
         false,
         {0, 0},
         0,
         "Silent: the filter is for 44100 Hz",
         "Convolver plays silence.",
         HostStatus::load_failed},
        {"refused first load, 1 in 2 out, mix 50%",
         split,
         true,
         false,
         {0, .5},
         0,
         "Silent: ",
         "Convolver plays silence.",
         HostStatus::load_failed},
        {"filter, mix 0%",
         mono,
         false,
         true,
         {-20, 0},
         .5f,
         "Filter loaded: 48000 Hz, 1 in, 1 out. Mix 0%: only the dry signal plays.",
         "Playing ",
         HostStatus::active},
        {"filter, mix 50%",
         mono,
         false,
         true,
         {0, .5},
         .5f * (.5f * .25f + .5f),
         "Filter loaded: 48000 Hz, 1 in, 1 out.",
         "Playing ",
         HostStatus::active},
    };
    for (const auto& c : cases) {
        HostFilter filter;
        if (c.refused_first_load) {
            Configuration wrong_rate{44100, c.host.inputs, c.host.outputs, 0, {}, {}, {}};
            for (std::size_t o = 0; o < c.host.outputs; ++o)
                wrong_rate.paths.push_back({{1}, {{0, 1}}, {{o, 1}}});
            try {
                filter.load(HostState{wrong_rate}, c.host);
            } catch (const std::invalid_argument&) {
            }
            require(filter.failed(), std::string(c.what) + ": the first load was not refused");
        }
        if (c.filter_loaded)
            filter.load(
                HostState{Configuration{48000, 1, 1, 0, {}, {}, {{{.25f}, {{0, 1}}, {{0, 1}}}}}},
                c.host);
        auto settings = c.settings;
        settings.latency = 64;
        HostProcessor host(filter.activate(c.host), 64, settings);
        const auto output = play(host, std::vector<float>(2048 * c.host.inputs, .5f), c.host.inputs,
                                 c.host.outputs, 64);
        for (std::size_t i = output.size() - 64 * c.host.outputs; i < output.size(); ++i)
            require(std::abs(output[i] - c.heard) < 1e-5f,
                    std::string(c.what) + ": heard " + std::to_string(output[i]) + ", expected " +
                        std::to_string(c.heard));
        const auto line = host_status(filter, c.host, true, c.settings);
        require(line.status == c.value && contains(line.text, c.status) &&
                    line.text.size() <= max_status_text,
                std::string(c.what) + ": Status " + line.text);
        const auto started = started_line(filter, c.host, 64, &c.settings);
        require(contains(started, c.started), std::string(c.what) + ": started line " + started);
        // The audible words never contradict the level heard.
        require(c.heard == 0 || (!contains(line.text, "Silent") &&
                                 (!contains(started, "plays silence") ||
                                  contains(started, "only the dry signal plays"))),
                std::string(c.what) + ": says silent, but " + std::to_string(c.heard) +
                    " is heard");
        require(c.heard == .5f || c.filter_loaded || !contains(line.text, "unchanged"),
                std::string(c.what) + ": says unchanged, but the level changed");
    }
    // A filter the adapter could not start has no processor: nothing plays,
    // whatever Mix says (the independent review).
    HostFilter unstarted;
    unstarted.load(HostState{Configuration{48000, 1, 1, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}}},
                   mono);
    unstarted.fail_activation("no memory");
    const auto failed_start = host_status(unstarted, mono, true, HostSettings{0, .5});
    require(contains(failed_start.text, "Silent: the plug-in could not start the filter."),
            "activation failure at Mix 50%: " + failed_start.text);
    // Before the host is known, a refused first load already knows that no
    // dry signal will play (jrp, 7 November 2026).
    HostFilter unknown;
    try {
        unknown.load(HostState{Configuration{44100, 1, 1, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}}},
                     mono);
    } catch (const std::invalid_argument&) {
    }
    const auto line = host_status(unknown, std::nullopt, true, HostSettings{0, .5});
    require(contains(line.text, "Silent: the filter is for 44100 Hz."),
            "unknown host, mix 50%: " + line.text);
    require(contains(host_status(unknown, std::nullopt).text, "Silent: "),
            "unknown host, defaults: " + host_status(unknown, std::nullopt).text);
}

// PLUG-10: Estimate and No clipping play the filter's own figures, as the
// CLI's --gain-db auto and no-clip choose them, and leave the Gain setting
// alone; each filter of a replacement plays at its own figure.
void gain_modes() {
    // The figures are estimate_attenuation()'s, the estimate only on request.
    std::mt19937 random(0x9a1e);
    const auto c = random_filter(random, 2, 2, 700);
    const auto figures = estimate_attenuation(c);
    const auto gains = filter_gains(c);
    require(gains.estimate_db == figures.noise_attenuation_db &&
                gains.no_clipping_db == figures.worst_case_attenuation_db && !gains.estimate_failed,
            "filter_gains differs from estimate_attenuation");
    const auto bound_only = filter_gains(c, false);
    require(!bound_only.estimate_db && !bound_only.estimate_failed &&
                bound_only.no_clipping_db == gains.no_clipping_db,
            "the estimate should be found only on request");
    // Against the oracle: the filter times its figure, whatever Gain holds.
    for (const auto mode : {GainMode::estimate, GainMode::no_clipping}) {
        HostSettings s;
        s.gain_db = -30; // Kept as the Set value, not played.
        s.mix = .4;
        s.latency = 128;
        s.gain_mode = mode;
        HostProcessor host(HostPlayback{c, true, gains}, 100, s);
        const auto input = noise(random, 2 * 2000);
        const auto output = play(host, input, 2, 2, 100);
        const auto wet = test::oracle(c, input, s.latency);
        const double gain = std::pow(10.0, *applied_gain_db(s, gains) / 20);
        double peak = 0, error = 0;
        for (std::size_t t = 0; t < input.size() / 2; ++t)
            for (std::size_t ch = 0; ch < 2; ++ch) {
                const double dry = t >= s.latency ? input[(t - s.latency) * 2 + ch] : 0;
                const double expected = s.mix * gain * wet[t * 2 + ch] + (1 - s.mix) * dry;
                peak = std::max(peak, std::abs(expected));
                error = std::max(error, std::abs(output[t * 2 + ch] - expected));
            }
        const double relative = error / peak;
        const std::string label = std::string(gain_mode_name(mode)) + ", mix 0.4";
        if (relative > worst_relative) {
            worst_relative = relative;
            worst_case = label;
        }
        worst_mode_relative = std::max(worst_mode_relative, relative);
        require(relative < 2e-6, "gain mode differs from the oracle: " + label +
                                     ", relative error " + std::to_string(relative));
    }
    // A one-tap filter of 0.5: No clipping plays it at +6.02 dB, so constant
    // input 1 comes out at 1; back in Set mode, the Gain setting ramps in.
    const auto half = tap_filter(.5f);
    const auto half_gains = filter_gains(half);
    HostSettings s{-20, 1, 64};
    s.gain_mode = GainMode::no_clipping;
    HostProcessor host(HostPlayback{half, true, half_gains}, 1, s);
    auto out = per_sample(host, 200);
    require(out[63] == 0 && std::abs(out[64] - 1) < 1e-6 && std::abs(out[199] - 1) < 1e-6,
            "No clipping should play the one-tap filter at its bound");
    host.set_gain_mode(GainMode::set);
    out = per_sample(host, 100);
    require(out[0] < 1 && out[0] > .05f && std::abs(out[63] - .05) < 1e-6 &&
                std::abs(out[99] - .05) < 1e-6,
            "a change of mode should ramp to the Gain setting");
    // Each filter of a replacement at its own figure: 0.5 then 0.25, both
    // normalised to 1, so the output never moves while they crossfade.
    host.set_gain_mode(GainMode::no_clipping);
    per_sample(host, 100);
    const auto quarter = tap_filter(.25f);
    host.replace(HostPlayback{quarter, true, filter_gains(quarter, false)});
    out = per_sample(host, 600);
    for (std::size_t t = 0; t < out.size(); ++t)
        require(std::abs(out[t] - 1) < 1e-5,
                "the replacement changed the level at frame " + std::to_string(t));
    // A mode change during a replacement (as a host's restore makes, which
    // sets the mode after publishing the filter): each filter ramps to its
    // own new gain, so no sample jumps. At 0 dB, Set plays 0.5 and then
    // 0.125, where No clipping played both at 1.
    for (const std::size_t into : {std::size_t{0}, std::size_t{70}}) {
        HostSettings at_zero{0, 1, 64};
        at_zero.gain_mode = GainMode::no_clipping;
        HostProcessor changing(HostPlayback{half, true, half_gains}, 1, at_zero);
        per_sample(changing, 200);
        const auto eighth = tap_filter(.125f);
        changing.replace(HostPlayback{eighth, true, filter_gains(eighth, false)});
        auto heard = per_sample(changing, into);
        changing.set_gain_mode(GainMode::set);
        const auto rest = per_sample(changing, 600);
        heard.insert(heard.end(), rest.begin(), rest.end());
        float last = 1;
        for (std::size_t t = 0; t < heard.size(); ++t) {
            require(std::abs(heard[t] - last) < .03f,
                    "Set " + std::to_string(into) + " frames into a replacement jumped from " +
                        std::to_string(last) + " to " + std::to_string(heard[t]) + " at frame " +
                        std::to_string(t));
            last = heard[t];
        }
        require(std::abs(heard.back() - .125f) < 1e-6,
                "Set should end at the new filter's tap: " + std::to_string(heard.back()));
    }
    // Where the estimate is absent, Estimate plays the bound; where both are
    // absent, silence.
    host.set_gain_mode(GainMode::estimate);
    out = per_sample(host, 100);
    require(std::abs(out[99] - 1) < 1e-5, "Estimate without an estimate should play the bound");
    host.replace(HostPlayback{half, true, FilterGains{std::nullopt, std::nullopt, true}});
    out = per_sample(host, 600);
    require(out[599] == 0, "a filter without figures should be silent in Estimate");
    host.set_gain_mode(GainMode::set);
    out = per_sample(host, 100);
    require(std::abs(out[99] - .05) < 1e-6, "Set should still play the filter at the Gain setting");
    // The text and figures for no filter: 0 dB in both modes.
    HostSettings estimate;
    estimate.gain_db = -20;
    estimate.gain_mode = GainMode::estimate;
    require(applied_gain_db(estimate, FilterGains{}) == 0.0 &&
                unfiltered_sound(estimate, true).unchanged,
            "without a filter Estimate plays 0 dB");
    estimate.gain_mode = GainMode::set;
    require(applied_gain_db(estimate, FilterGains{}) == -20.0, "Set plays the Gain setting");
}

// PLUG-10: choosing Estimate while a filter plays changes its gain in place.
// jrp's review: replacing the processor's filter to carry the estimate lost
// the engine's history, so a filter with a quarter-second delay fell silent
// for about 245 ms. Here the delayed tap plays a steady 1 (its bound, +6 dB),
// and Estimate must ramp to its estimate without any gap.
void estimate_keeps_history() {
    Configuration c{48000, 1, 1, 0, {}, {}, {}};
    std::vector<float> delayed(12001, 0.f); // 0.25 s at 48 kHz.
    delayed.back() = .5f;
    c.paths.push_back({delayed, {{0, 1}}, {{0, 1}}});
    HostFilter filter;
    std::optional<HostPlayback> installed;
    filter.load(HostState{c}, HostLayout{48000, 1, 1},
                [&](const HostPlayback& p) { installed = p; });
    require(installed && !installed->gains.estimate_db, "the delayed filter did not load");
    HostSettings s{0, 1, 0, GainMode::estimate};
    HostProcessor host(*installed, 64, s);
    const auto steady = play(host, std::vector<float>(13000, 1.f), 1, 1, 64);
    require(std::abs(steady.back() - 1) < 1e-5,
            "the bound should play the delayed tap at 1: " + std::to_string(steady.back()));
    require(filter.estimate(true, [&](const FilterGains& g) { host.update_gains(g); }),
            "Estimate should find the delayed filter's estimate");
    const auto after = play(host, std::vector<float>(4000, 1.f), 1, 1, 64);
    const float target = .5f * std::pow(10.f, static_cast<float>(*filter.gains().estimate_db) / 20);
    float lowest = after[0];
    std::size_t at = 0;
    for (std::size_t t = 0; t < after.size(); ++t)
        if (after[t] < lowest) {
            lowest = after[t];
            at = t;
        }
    require(lowest >= std::min(1.f, target) - 1e-5f,
            "choosing Estimate interrupted the filter: " + std::to_string(lowest) + " at frame " +
                std::to_string(at) + " (between 1 and " + std::to_string(target) + " expected)");
    require(std::abs(after.back() - target) < 1e-5,
            "Estimate should end at the filter's estimate: " + std::to_string(after.back()));
}

// PLUG-10: HostFilter finds the noise estimate only while estimating, and
// finds it for the accepted filter when estimating starts.
void filter_estimates() {
    std::mt19937 random(0xe57);
    const auto c = random_filter(random, 2, 2, 400);
    const HostLayout layout{48000, 2, 2};
    HostFilter filter;
    std::optional<HostPlayback> installed;
    filter.load(HostState{c}, layout, [&](const HostPlayback& p) { installed = p; });
    require(installed && !installed->gains.estimate_db && installed->gains.no_clipping_db &&
                !filter.gains().estimate_db,
            "a load in Set mode should not find the estimate");
    // An update that fails keeps nothing: the window and log never show an
    // estimate that does not play, and the next try finds it again.
    bool threw = false;
    try {
        filter.estimate(true, [](const FilterGains&) { throw std::bad_alloc(); });
    } catch (const std::bad_alloc&) {
        threw = true;
    }
    require(threw && !filter.gains().estimate_db && filter.estimating(),
            "a failed update should not keep the estimate");
    std::optional<FilterGains> applied;
    require(filter.estimate(true, [&](const FilterGains& g) { applied = g; }) && applied &&
                applied->estimate_db == filter_gains(c).estimate_db && filter.gains() == *applied &&
                filter.estimating(),
            "estimate(true) should find and apply the accepted filter's estimate");
    require(!filter.estimate(true), "a known estimate needs no new figures");
    installed.reset();
    filter.load(HostState{c}, layout, [&](const HostPlayback& p) { installed = p; });
    require(installed && installed->gains.estimate_db == filter_gains(c).estimate_db,
            "a load while estimating should find the estimate");
    require(!filter.estimate(false) && !filter.estimating(),
            "estimate(false) changes nothing played");
    HostFilter empty;
    require(!empty.estimate(true), "no filter, nothing to play");
    // Coefficients so large that the noise overflows: the estimate fails,
    // and the bound is beyond the largest float, so both are absent.
    Configuration huge{48000, 1, 1, 0, {}, {}, {}};
    for (int i = 0; i < 2; ++i)
        huge.paths.push_back({{3e38f}, {{0, 1}}, {{0, 1}}});
    const auto none = filter_gains(huge);
    require(!none.estimate_db && none.estimate_failed && !none.no_clipping_db,
            "an overflowing filter has no figures");
}
} // namespace

int main() {
    const auto failed = convolver::test::run_cases(
        {
            {"defaults_unchanged", defaults_unchanged},
            {"gain_and_mix_against_oracle", gain_and_mix_against_oracle},
            {"ramps", ramps},
            {"ramps_follow_time", ramps_follow_time},
            {"ramp_endpoints", ramp_endpoints},
            {"unequal_mix", unequal_mix},
            {"latency_choices", latency_choices},
            {"settings_text", settings_text},
            {"diagnostics_follow_settings", diagnostics_follow_settings},
            {"no_filter_names_choosing", no_filter_names_choosing},
            {"gain_modes", gain_modes},
            {"filter_estimates", filter_estimates},
            {"estimate_keeps_history", estimate_keeps_history},
        },
        [] { audio_thread = false; });
    std::printf("worst Estimate and No clipping oracle error %.3g of the output peak\n",
                worst_mode_relative);
    std::printf("worst gain and mix oracle error %.3g of the output peak (%s)\n", worst_relative,
                worst_case.c_str());
    if (failed)
        return 1;
    std::cout << "host settings passed\n";
}
