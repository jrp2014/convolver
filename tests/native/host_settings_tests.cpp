// SPDX-License-Identifier: GPL-2.0-or-later
// PLUG-10: HostProcessor's settings (gain and mix), checked against the
// direct double-precision oracle, sample by sample through their ramps, and
// without allocation on the audio thread.
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
void* operator new[](std::size_t n) {
    return ::operator new(n);
}
void operator delete(void* p) noexcept {
    if (audio_thread && p)
        std::abort();
    std::free(p);
}
void operator delete[](void* p) noexcept {
    ::operator delete(p);
}
void operator delete(void* p, std::size_t) noexcept {
    ::operator delete(p);
}
void operator delete[](void* p, std::size_t) noexcept {
    ::operator delete(p);
}

using namespace convolver;
namespace {
void require(bool value, const std::string& message) {
    if (!value)
        throw std::runtime_error(message);
}
double worst_relative = 0; // The worst oracle error, relative to the case's output peak.
std::string worst_case;

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
    HostProcessor plain(first, 100, 256);
    HostProcessor set(first, 100, HostSettings{});
    require(set.latency_frames() == 256, "default latency");
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

// Gain and mix from the first frame, against the oracle: output c is
// gain * mix * filtered + (1 - mix) * input c delayed by the block size.
void gain_and_mix_against_oracle() {
    std::mt19937 random(0x6a1);
    struct Case {
        std::size_t block, callback;
        double gain_db, mix;
    };
    for (const auto& k : {Case{256, 256, -6, .3}, Case{64, 100, 12, 1}, Case{1024, 333, -40, .75},
                          Case{128, 1, 24, 0}, Case{4096, 4096, -100, .5}}) {
        const auto c = random_filter(random, 2, 2, 3 * k.block / 2 + 5);
        HostSettings s;
        s.gain_db = k.gain_db;
        s.mix = k.mix;
        HostProcessor host(c, k.callback, s, k.block);
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
    HostProcessor host(tap_filter(1), 1, HostSettings{}, 64);
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

// With unequal counts there is no dry signal, so the mix stays 100% wet.
void unequal_mix() {
    Configuration c{48000, 1, 2, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}, {1, 1}}}}};
    HostSettings s;
    s.mix = 0;
    HostProcessor host(c, 1, s, 64);
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

void settings_text() {
    require(settings_words(HostSettings{}, true) == "gain 0 dB, mix 100% wet",
            "settings words: " + settings_words(HostSettings{}, true));
    HostSettings s{-6, .5};
    require(settings_words(s, true) == "gain -6 dB, mix 50% wet",
            "settings words: " + settings_words(s, true));
    require(settings_words(s, false).find("no dry signal") != std::string::npos,
            "settings words for unequal counts");
    // GarageBand's sliders give values like these (jrp's run of AU-6).
    for (const auto& [settings, words] :
         {std::pair{HostSettings{-75.967, .561413}, "gain -76 dB, mix 56.1% wet"},
          std::pair{HostSettings{-12.04, .508}, "gain -12 dB, mix 50.8% wet"},
          std::pair{HostSettings{-.01, .9996}, "gain -0.01 dB, mix 99.96% wet"},
          std::pair{HostSettings{-99.99, .99999}, "gain -99.99 dB, mix 99.999% wet"},
          std::pair{HostSettings{-.04, .0004}, "gain -0.04 dB, mix 0.04% wet"}})
        require(settings_words(settings, true) == words,
                "settings words: " + settings_words(settings, true));
    // The window's slider text: never an exponent, never a range end it is not.
    for (const auto& [value, text] :
         {std::pair{23.97, "23.97"}, std::pair{24.0, "24"}, std::pair{-1e-9, "0"},
          std::pair{1e-5, "0.00001"}, std::pair{-.0, "0"}, std::pair{-12.04, "-12"}})
        require(setting_text(value, min_gain_db, max_gain_db) == text,
                "setting text: " + setting_text(value, min_gain_db, max_gain_db));
    HostFilter filter;
    const auto line = started_line(filter, {48000, 2, 2}, 512, &s);
    require(line.find("Settings: gain -6 dB, mix 50% wet.") != std::string::npos,
            "started line without settings: " + line);
}

// jrp's review of 0.5.5: Status and the start line said "audio passes
// unchanged" with no filter at Gain -20 dB (the output was a tenth of the
// input), and "Silent" after a refused first load at Mix 50% (the dry half
// played). Each case plays a steady 0.5 through what the instance plays and
// checks the words against the level heard.
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
         "so Convolver plays silence",
         HostStatus::load_failed},
        {"refused first load, mix 50%",
         mono,
         true,
         false,
         {0, .5},
         .25f,
         "Load failed, only the dry signal plays: the filter is for 44100 Hz",
         "With Mix at 50% wet, though, the dry signal still plays instead of silence: the input "
         "at -6.0 dB.",
         HostStatus::load_failed},
        {"refused first load, gain +24 dB, mix 0%",
         mono,
         true,
         false,
         {24, 0},
         .5f,
         "Load failed, only the dry signal plays:",
         "With Mix at 0% wet, though, the dry signal still plays instead of silence: the input "
         "unchanged.",
         HostStatus::load_failed},
        {"refused first load, 1 in 2 out, mix 50%",
         split,
         true,
         false,
         {0, .5},
         0,
         "Silent: ",
         "so Convolver plays silence",
         HostStatus::load_failed},
        {"filter, mix 0%",
         mono,
         false,
         true,
         {-20, 0},
         .5f,
         "Filter loaded: 48000 Hz, 1 in, 1 out. Mix 0%: only the dry signal plays.",
         "Playing ",
         HostStatus::playing},
        {"filter, mix 50%",
         mono,
         false,
         true,
         {0, .5},
         .5f * (.5f * .25f + .5f),
         "Filter loaded: 48000 Hz, 1 in, 1 out.",
         "Playing ",
         HostStatus::playing},
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
        HostProcessor host(filter.activate(c.host), 64, c.settings, 64);
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
                                  contains(started, "still plays instead of silence"))),
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
    // Before the host is known, a refused first load cannot say whether
    // Mix's dry share will play (it does only with equal counts).
    HostFilter unknown;
    try {
        unknown.load(HostState{Configuration{44100, 1, 1, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}}},
                     mono);
    } catch (const std::invalid_argument&) {
    }
    const auto line = host_status(unknown, std::nullopt, true, HostSettings{0, .5});
    require(contains(line.text, "Load failed, no filter plays: "),
            "unknown host, mix 50%: " + line.text);
    require(contains(host_status(unknown, std::nullopt).text, "Silent: "),
            "unknown host, defaults: " + host_status(unknown, std::nullopt).text);
}
} // namespace

int main() {
    try {
        defaults_unchanged();
        gain_and_mix_against_oracle();
        ramps();
        unequal_mix();
        settings_text();
        diagnostics_follow_settings();
        std::printf("worst gain and mix oracle error %.3g of the output peak (%s)\n",
                    worst_relative, worst_case.c_str());
        std::cout << "host settings passed\n";
    } catch (const std::exception& e) {
        audio_thread = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
