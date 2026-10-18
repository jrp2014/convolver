// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/io.hpp"
#include <bit>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sndfile.h>
#include <stdexcept>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using namespace convolver;
void require(bool b, const char* message) {
    if (!b)
        throw std::runtime_error(message);
}
template <class F> void rejects(F f) {
    bool threw = false;
    try {
        f();
    } catch (const std::exception&) {
        threw = true;
    }
    require(threw, "expected rejection");
}
void text_file(const fs::path& p, const std::string& s) {
    std::ofstream f(p);
    f << s;
    if (!f)
        throw std::runtime_error("fixture write failed");
}
void wave(const fs::path& p, int rate, std::size_t channels, std::vector<float> samples) {
    AudioWriter w(p, {rate, channels}, OutputFormat::wav_float);
    w.write(samples);
    w.close();
}
#ifndef _WIN32
struct Redirect {
    int fd, saved;
    Redirect(int target, int source) : fd(target), saved(dup(target)) {
        require(saved >= 0 && dup2(source, target) >= 0, "redirect descriptor");
    }
    ~Redirect() {
        dup2(saved, fd);
        close(saved);
    }
};
void descriptor_tests() {
    const std::string bytes("\0\0\x80\x3f\0\0\0\xbf", 8);
    for (const auto length : {8u, 7u, 5u}) {
        int fds[2];
        require(pipe(fds) == 0, "create input pipe");
        require(write(fds[1], bytes.data(), length) == length, "fill input pipe");
        close(fds[1]);
        {
            Redirect redirect(0, fds[0]);
            close(fds[0]);
            AudioReader r("-", RawSpec{48000, 2});
            std::vector<float> x(8);
            if (length == 8) {
                require(r.read(x) == 1 && x[0] == 1 && x[1] == -.5f, "RAW pipe read");
                require(r.read(x) == 0, "RAW pipe EOF");
            } else
                rejects([&] { r.read(x); });
            r.close();
            require(fcntl(0, F_GETFD) >= 0, "reader must not close stdin");
        }
    }
    int fds[2];
    require(pipe(fds) == 0, "create output pipe");
    {
        Redirect redirect(1, fds[1]);
        close(fds[1]);
        AudioWriter w("-", {48000, 2}, OutputFormat::raw_float);
        std::vector<float> x{1, -.5f};
        w.write(x);
        w.close();
        require(fcntl(1, F_GETFD) >= 0, "writer must not close stdout");
    }
    char buffer[20]{};
    const auto count = read(fds[0], buffer, sizeof buffer);
    close(fds[0]);
    require(count == 8 && std::string(buffer, 8) == bytes, "RAW pipe bytes");
    require(pipe(fds) == 0, "create fragmented input pipe");
    const auto child = fork();
    require(child >= 0, "fork fragmented writer");
    if (!child) {
        close(fds[0]);
        for (const auto byte : bytes) {
            if (write(fds[1], &byte, 1) != 1)
                _exit(1);
            usleep(1000);
        }
        close(fds[1]);
        _exit(0);
    }
    close(fds[1]);
    {
        Redirect redirect(0, fds[0]);
        close(fds[0]);
        AudioReader r("-", RawSpec{48000, 2});
        std::vector<float> x(8);
        require(r.read(x) == 1 && x[0] == 1 && x[1] == -.5f, "fragmented RAW pipe read");
        r.close();
    }
    int status = 0;
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "fragmented writer exit");
    require(pipe(fds) == 0, "create broken output pipe");
    close(fds[0]);
    const auto previous = std::signal(SIGPIPE, SIG_IGN);
    {
        Redirect redirect(1, fds[1]);
        close(fds[1]);
        rejects([&] {
            AudioWriter w("-", {48000, 1}, OutputFormat::raw_float);
            std::vector<float> x{1};
            w.write(x);
            w.close();
        });
    }
    std::signal(SIGPIPE, previous);
}
#endif
// The fmt chunk of a RIFF/WAVE file, parsed without libsndfile.
struct WaveFormat {
    unsigned tag = 0, channels = 0, bits = 0, sub_format = 0;
    std::optional<std::uint32_t> mask;
};
WaveFormat wave_format(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(f)), {});
    const auto u = [&](std::size_t at, int n) {
        std::uint32_t v = 0;
        for (int i = n - 1; i >= 0; --i)
            v = v << 8 | static_cast<unsigned char>(bytes.at(at + i));
        return v;
    };
    require(bytes.compare(0, 4, "RIFF") == 0 && bytes.compare(8, 4, "WAVE") == 0, "RIFF header");
    for (std::size_t at = 12; at + 8 <= bytes.size();) {
        const auto length = u(at + 4, 4);
        if (bytes.compare(at, 4, "fmt ") == 0) {
            WaveFormat w;
            w.tag = u(at + 8, 2);
            w.channels = u(at + 10, 2);
            w.bits = u(at + 22, 2);
            if (w.tag == 0xfffe) {
                require(length >= 40 && u(at + 24, 2) == 22, "extensible fmt size");
                w.mask = u(at + 28, 4);
                w.sub_format = u(at + 32, 2);
            }
            return w;
        }
        at += 8 + length + (length & 1);
    }
    throw std::runtime_error("no fmt chunk");
}
// Opt-in speaker masks: exact dwChannelMask in the header, samples in channel order.
void speaker_tests(const fs::path& dir) {
    struct Case {
        std::size_t channels;
        std::uint32_t mask;
        OutputFormat format;
        unsigned tag, bits;
    };
    for (const auto& c : {Case{2, 0x3, OutputFormat::wav_float, 3, 32},
                          Case{2, 0x3, OutputFormat::wav_pcm16, 1, 16},
                          Case{6, 0x3f, OutputFormat::wav_float, 3, 32},
                          Case{6, 0x3f, OutputFormat::wav_pcm24, 1, 24},
                          Case{4, 0x603, OutputFormat::wav_pcm32, 1, 32},
                          Case{1, 0x20000, OutputFormat::wav_float, 3, 32},
                          Case{18, 0x3ffff, OutputFormat::wav_float, 3, 32}}) {
        const auto out = dir / ("speakers-" + std::to_string(c.channels) + "-" +
                                std::to_string(static_cast<int>(c.format)) + ".wav");
        // Two frames per channel: an impulse identifying the channel, then its negation.
        std::vector<float> samples;
        for (int frame = 0; frame < 2; ++frame)
            for (std::size_t ch = 0; ch < c.channels; ++ch)
                samples.push_back((frame ? -1.f : 1.f) * static_cast<float>(ch + 1) / 8);
        {
            AudioWriter w(out, {48000, c.channels}, c.format, {}, {c.mask});
            w.write(samples);
            w.close();
        }
        const auto fmt = wave_format(out);
        require(fmt.tag == 0xfffe && fmt.mask == c.mask && fmt.sub_format == c.tag &&
                    fmt.bits == c.bits && fmt.channels == c.channels,
                "WAVE_FORMAT_EXTENSIBLE header carries the exact channel mask");
        AudioReader r(out);
        std::vector<float> back(samples.size());
        require(r.info().channels == c.channels && r.read(back) == 2 && back == samples,
                "speaker-labelled samples keep their channel order");
    }
    // Zero leaves the speakers unspecified: the same file as without metadata.
    const std::vector<float> stereo{.5f, -.25f};
    const auto plain = dir / "speakers-default.wav", zero = dir / "speakers-zero.wav";
    for (const auto& [path, metadata] :
         {std::pair{plain, OutputMetadata{}}, std::pair{zero, OutputMetadata{0}}}) {
        AudioWriter w(path, {48000, 2}, OutputFormat::wav_float, {}, metadata);
        w.write(stereo);
        w.close();
    }
    const auto bytes = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), {});
    };
    require(bytes(plain) == bytes(zero) && wave_format(zero).tag == 3 && !wave_format(zero).mask,
            "a zero mask writes the unchanged plain WAV");
    // Invalid explicit requests fail before the output exists.
    struct Bad {
        std::size_t channels;
        std::uint32_t mask;
        OutputFormat format;
    };
    for (const auto& b : {Bad{6, 0x3, OutputFormat::wav_float},        // too few speakers
                          Bad{2, 0x7, OutputFormat::wav_pcm16},        // too many speakers
                          Bad{1, 0x40000, OutputFormat::wav_float},    // unknown bit
                          Bad{1, 0x80000000, OutputFormat::wav_float}, // SPEAKER_ALL
                          Bad{2, 0x3, OutputFormat::raw_float}}) {     // RAW has no header
        const auto out = dir / "speakers-bad.wav";
        bool threw = false;
        try {
            AudioWriter(out, {48000, b.channels}, b.format, {}, {b.mask});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        require(threw && !fs::exists(out), "invalid speaker mask rejected before opening");
    }
}
std::vector<std::int16_t> pcm16_samples(const fs::path& p) {
    SF_INFO info{};
    SNDFILE* f = sf_open(p.string().c_str(), SFM_READ, &info);
    require(f && (info.format & SF_FORMAT_SUBMASK) == SF_FORMAT_PCM_16, "open PCM16 fixture");
    std::vector<std::int16_t> s(static_cast<std::size_t>(info.frames * info.channels));
    const auto got = sf_readf_short(f, s.data(), info.frames);
    sf_close(f);
    require(got == info.frames, "read PCM16 fixture");
    return s;
}
std::vector<std::int16_t> quantize(Dither dither, std::uint64_t seed, const std::vector<float>& x,
                                   std::size_t channels = 1) {
    Pcm16Quantizer q(channels, {dither, seed});
    std::vector<std::int16_t> y(x.size());
    q.quantize(x, y);
    return y;
}
// Error statistics in LSBs, measured independently of the convolution engine.
struct ErrorStats {
    double mean = 0, variance = 0, lag1 = 0, max_abs = 0;
};
ErrorStats error_stats(const std::vector<float>& x, const std::vector<std::int16_t>& y) {
    std::vector<double> e(x.size());
    ErrorStats s;
    for (std::size_t i = 0; i < x.size(); ++i) {
        e[i] = y[i] - static_cast<double>(x[i]) * 32768.0;
        s.mean += e[i];
        s.max_abs = std::max(s.max_abs, std::abs(e[i]));
    }
    s.mean /= static_cast<double>(e.size());
    double covariance = 0;
    for (std::size_t i = 0; i < e.size(); ++i) {
        s.variance += (e[i] - s.mean) * (e[i] - s.mean);
        if (i)
            covariance += (e[i] - s.mean) * (e[i - 1] - s.mean);
    }
    s.lag1 = covariance / s.variance;
    s.variance /= static_cast<double>(e.size());
    return s;
}
void dither_tests(const fs::path& dir) {
    // Plain rounding: nearest LSB, ties toward +infinity, symmetric clipping bounds.
    const std::vector<float> exact{0.4f / 32768,
                                   0.5f / 32768,
                                   0.6f / 32768,
                                   -0.4f / 32768,
                                   -0.5f / 32768,
                                   -0.6f / 32768,
                                   1.5f / 32768,
                                   -1.5f / 32768,
                                   1,
                                   -1,
                                   2,
                                   -2,
                                   0.999f,
                                   -0.999f,
                                   0};
    const std::vector<std::int16_t> expected{0,     1,      1,     0,      0,     -1,     2, -1,
                                             32767, -32768, 32767, -32768, 32735, -32735, 0};
    require(quantize(Dither::none, 0, exact) == expected, "PCM16 rounding policy");
    for (auto d : {Dither::rectangular, Dither::triangular, Dither::triangular_highpass}) {
        const auto y = quantize(d, 7, {2, -2, 1, -1, 1, -1});
        require(y[0] == 32767 && y[1] == -32768 && y[2] == 32767 && y[3] <= -32767 &&
                    y[4] >= 32766 && y[5] <= -32767,
                "dithered PCM16 clipping");
    }
    // The seed fixes the output on every compiler. Values from an independent
    // splitmix64 model; triangular draws the minuend first.
    {
        std::vector<float> small(12);
        for (std::size_t i = 0; i < small.size(); ++i)
            small[i] = (static_cast<float>(i) - 6) / 65536.0f;
        using V = std::vector<std::int16_t>;
        require(quantize(Dither::rectangular, 12345, small, 2) ==
                    V{-3, -3, -2, -2, -1, -1, 0, 0, 1, 2, 2, 2},
                "rectangular dither sequence for seed 12345");
        require(quantize(Dither::triangular, 12345, small, 2) ==
                    V{-3, -3, -2, -2, -1, -1, 0, 1, 1, 2, 2, 2},
                "triangular dither sequence for seed 12345");
        require(quantize(Dither::triangular_highpass, 12345, small, 2) ==
                    V{-3, -3, -2, -1, -1, 0, 0, 1, 1, 1, 3, 3},
                "high-pass triangular dither sequence for seed 12345");
    }
    // Determinism: seed-dependent, chunk-independent, reproducible after reset.
    std::vector<float> ramp(4096 * 3 + 6);
    for (std::size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = 0.3f * std::sin(0.001f * static_cast<float>(i)) + 1e-5f;
    for (auto d : {Dither::rectangular, Dither::triangular, Dither::triangular_highpass}) {
        const auto whole = quantize(d, 42, ramp, 2);
        require(whole == quantize(d, 42, ramp, 2), "dither is reproducible");
        require(whole != quantize(d, 43, ramp, 2), "dither depends on seed");
        Pcm16Quantizer q(2, {d, 42});
        std::vector<std::int16_t> pieces(ramp.size());
        for (std::size_t offset = 0, step = 2; offset < ramp.size(); step = step * 3 % 998 + 2) {
            const auto n = std::min(step, ramp.size() - offset);
            q.quantize(std::span<const float>(ramp).subspan(offset, n),
                       std::span<std::int16_t>(pieces).subspan(offset, n));
            offset += n;
        }
        require(whole == pieces, "dither is independent of chunk boundaries");
        q.reset();
        q.quantize(ramp, pieces);
        require(whole == pieces, "reset restarts the dither sequence");
    }
    // A constant 0.25 LSB signal: plain rounding loses it (error -0.25 LSB); dither
    // keeps it on average. RPDF error power still depends on the signal (3/16 LSB^2
    // here); TPDF error power is 1/4 LSB^2 at every level. The high-pass TPDF has
    // lag-1 error correlation -1/3, that is (-1/12) / (1/4).
    const std::vector<float> quarter(200000, 0.25f / 32768);
    const auto plain = error_stats(quarter, quantize(Dither::none, 0, quarter));
    require(std::abs(plain.mean + 0.25) < 1e-9, "undithered rounding bias");
    const auto rpdf = error_stats(quarter, quantize(Dither::rectangular, 1, quarter));
    require(std::abs(rpdf.mean) < 0.01 && rpdf.max_abs <= 1.0 &&
                std::abs(rpdf.variance - 0.1875) < 0.01,
            "rectangular dither error statistics");
    const auto tpdf = error_stats(quarter, quantize(Dither::triangular, 1, quarter));
    require(std::abs(tpdf.mean) < 0.01 && tpdf.max_abs <= 1.5 &&
                std::abs(tpdf.variance - 0.25) < 0.01 && std::abs(tpdf.lag1) < 0.02,
            "triangular dither error statistics");
    const auto highpass = error_stats(quarter, quantize(Dither::triangular_highpass, 1, quarter));
    require(std::abs(highpass.mean) < 0.01 && highpass.max_abs <= 1.5 &&
                std::abs(highpass.variance - 0.25) < 0.01 &&
                std::abs(highpass.lag1 + 1. / 3) < 0.02,
            "high-pass triangular dither error statistics");
    // Error variance stays the same for other signal levels (no noise modulation).
    for (float level : {0.f, 0.5f, 0.75f, 100.125f}) {
        const std::vector<float> x(100000, level / 32768);
        const auto s = error_stats(x, quantize(Dither::triangular, 5, x));
        require(std::abs(s.mean) < 0.01 && std::abs(s.variance - 0.25) < 0.015,
                "triangular dither error is signal independent");
    }
    // The writer produces exactly the quantizer's samples, over several writes.
    const auto p = dir / "dither.wav";
    {
        AudioWriter w(p, {44100, 2}, OutputFormat::wav_pcm16, {Dither::triangular, 9});
        w.write(std::span<const float>(ramp).first(10));
        w.write(std::span<const float>(ramp).subspan(10));
        w.close();
    }
    require(pcm16_samples(p) == quantize(Dither::triangular, 9, ramp, 2), "writer dither samples");
    {
        AudioWriter w(p, {44100, 2}, OutputFormat::wav_pcm16);
        w.write(ramp);
        w.close();
    }
    require(pcm16_samples(p) == quantize(Dither::none, 0, ramp, 2), "writer default rounding");
    for (auto format : {OutputFormat::wav_float, OutputFormat::wav_pcm24, OutputFormat::wav_pcm32,
                        OutputFormat::raw_float})
        rejects([&] { AudioWriter w(dir / "x.wav", {48000, 1}, format, {Dither::triangular}); });
    rejects([&] { Pcm16Quantizer q(0); });
    rejects([&] { Pcm16Quantizer q(1, {static_cast<Dither>(99)}); });
}
void run(const fs::path& dir) {
    dither_tests(dir);
    const std::vector<float> samples{-1, 1, -.5f, .5f, 0, .125f, -.75f, .75f};
    for (auto format : {OutputFormat::wav_float, OutputFormat::wav_pcm16, OutputFormat::wav_pcm24,
                        OutputFormat::wav_pcm32, OutputFormat::raw_float}) {
        const auto p = dir / "roundtrip.audio";
        {
            AudioWriter w(p, {48000, 2}, format);
            w.write(samples);
            w.close();
            w.close();
            rejects([&] { w.write(samples); });
        }
        AudioReader r(p, format == OutputFormat::raw_float ? std::optional<RawSpec>({48000, 2})
                                                           : std::nullopt);
        require(r.info().sample_rate == 48000 && r.info().channels == 2, "metadata roundtrip");
        std::vector<float> result(12, 99);
        require(r.read(std::span<float>(result).first(6)) == 3, "chunk read");
        require(r.read(std::span<float>(result).subspan(6)) == 1, "short final read");
        std::vector<float> eof_buffer(2);
        require(r.read(eof_buffer) == 0, "EOF");
        for (std::size_t i = 0; i < samples.size(); ++i)
            if (std::abs(result[i] - samples[i]) >= .00004f)
                throw std::runtime_error(
                    "sample roundtrip format=" + std::to_string(static_cast<int>(format)) +
                    " index=" + std::to_string(i) + " got=" + std::to_string(result[i]));
        rejects([&] { r.read(std::span<float>(result).first(3)); });
        r.close();
        r.close();
        rejects([&] { r.read(result); });
    }
    for (auto format :
         {OutputFormat::wav_pcm16, OutputFormat::wav_pcm24, OutputFormat::wav_pcm32}) {
        const auto p = dir / "clip.wav";
        {
            AudioWriter w(p, {48000, 1}, format);
            const std::vector<float> x{-2, -1, 1, 2};
            w.write(x);
            w.close();
        }
        AudioReader r(p);
        std::vector<float> x(4);
        require(r.read(x) == 4, "clip frames");
        require(x[0] <= -.9999f && x[1] <= -.9999f && x[2] >= .9999f && x[3] >= .9999f,
                "PCM clipping endpoints");
    }
    rejects([&] { AudioReader r(dir / "missing.wav"); });
    rejects([&] { AudioReader r("-"); });
    rejects([&] { AudioWriter w("-", {48000, 1}, OutputFormat::wav_float); });
    rejects([&] { AudioWriter w(dir / "bad.wav", {0, 1}, OutputFormat::wav_float); });
    rejects([&] { AudioWriter w(dir / "bad.wav", {48000, 0}, OutputFormat::wav_float); });
    {
        AudioWriter w(dir / "invalid.wav", {48000, 2}, OutputFormat::wav_float);
        std::vector<float> x(3);
        rejects([&] { w.write(x); });
        x.resize(2);
        x[0] = std::numeric_limits<float>::infinity();
        rejects([&] { w.write(x); });
    }
    wave(dir / fs::path(u8"ir ü.wav"), 48000, 2, {1, .5f, .25f, -.25f, 0, .125f});
    const auto diagonal = load_configuration(dir / fs::path(u8"ir ü.wav"), 48000, 2);
    require(diagonal.paths.size() == 2 && diagonal.paths[1].inputs[0].channel == 1 &&
                diagonal.paths[1].impulse[2] == .125f,
            "diagonal audio IR");
    rejects([&] { load_configuration(dir / fs::path(u8"ir ü.wav"), 44100, 2); });
    rejects([&] { load_configuration(dir / fs::path(u8"ir ü.wav"), 48000, 1); });
    const std::string valid = "48000 2 2 3 // header comment\n0.04 1\n0 .02\nir ü.wav // relative "
                              "path\n1\n-0 1.5\n0 -1\n";
    text_file(dir / "good.cfg", valid);
    const auto cfg = load_configuration(dir / "good.cfg", 48000, 2);
    require(cfg.channel_mask == 3 && cfg.input_delays == std::vector<std::size_t>({1, 48}) &&
                cfg.output_delays == std::vector<std::size_t>({0, 0}),
            "delay truncation");
    require(cfg.paths[0].impulse == std::vector<float>({.5f, -.25f, .125f}), "filter channel");
    require(cfg.paths[0].inputs[0].gain == -1 && cfg.paths[0].inputs[1].gain == .5f &&
                cfg.paths[0].outputs[1].gain == -1,
            "signed route fractions");
    std::vector<fs::path> provenance;
    load_configuration(dir / "good.cfg", 48000, 2, &provenance);
    require(provenance.size() == 2 && provenance[0] == fs::weakly_canonical(dir / "good.cfg") &&
                provenance[1] == fs::weakly_canonical(dir / fs::path(u8"ir ü.wav")),
            "configuration source provenance");
    fs::create_directory(dir / "irs");
    fs::create_directory(dir / "configs");
    wave(dir / "irs" / fs::path(u8"ir ü.wav"), 48000, 1, {1, .5f, -.25f});
    for (const auto& name : {"..\\irs\\ir ü.wav", "../irs/ir ü.wav", "..\\irs/ir ü.wav"}) {
        text_file(dir / "configs" / "windows.cfg",
                  "48000 1 1 0\n0\n0\n" + std::string(name) + "\n0\n0\n0\n");
        text_file(dir / "windows-list.txt", "configs\\windows.cfg\n");
        std::vector<fs::path> sources;
        const auto imported = load_configuration(dir / "windows-list.txt", 48000, 1, &sources);
        require(imported.paths[0].impulse == std::vector<float>({1, .5f, -.25f}),
                "Windows/mixed separators in configs and lists");
        require(sources.back() == fs::weakly_canonical(dir / "irs" / fs::path(u8"ir ü.wav")),
                "normalized Windows path provenance");
    }
#ifndef _WIN32
    for (const auto& name :
         {"C:\\irs\\ir.wav", "C:ir.wav", "\\\\server\\share\\ir.wav", "\\irs\\ir.wav"}) {
        text_file(dir / "windows-list.txt", std::string(name) + "\n");
        bool diagnosed = false;
        try {
            load_configuration(dir / "windows-list.txt");
        } catch (const std::runtime_error& e) {
            diagnosed =
                std::string(e.what()).find("Windows drive/rooted paths") != std::string::npos;
        }
        require(diagnosed, "nonportable Windows absolute paths need an actionable diagnostic");
    }
#endif
    for (const auto& bad :
         {"48000 999999999 2 0\n", "0 1 1 0\n0\n0\nir ü.wav\n0\n0\n0\n",
          "48000 1 1 0\n-1\n0\nir ü.wav\n0\n0\n0\n", "48000 1 1 0\nnan\n0\nir ü.wav\n0\n0\n0\n",
          "48000 1 1 0\n0\n0\nir ü.wav\n2\n0\n0\n", "48000 1 1 0\n0\n0\nir ü.wav\n0\n1\n0\n",
          "48000 1 1 0\n0\n0\nir ü.wav\n0\n0\nnan\n", "48000 1 1 0\n0\n0\nir ü.wav\n0\n0\n",
          "48000 1 1 0 trailing\n0\n0\nir ü.wav\n0\n0\n0\n"}) {
        text_file(dir / "bad.cfg", bad);
        rejects([&] { load_configuration(dir / "bad.cfg"); });
    }
    wave(dir / "44100.wav", 44100, 1, {1});
    text_file(dir / "list.txt", "44100.wav\ngood.cfg\n");
    require(load_configuration(dir / "list.txt", 48000, 2).sample_rate == 48000, "list selection");
    rejects([&] { load_configuration(dir / "list.txt", 96000, 1); });
    text_file(dir / "unrelated.cfg", "44100 1 1 0\n0\n0\nmissing-unused.wav\n0\n0\n0\n");
    text_file(dir / "list.txt", "unrelated.cfg\ngood.cfg\n");
    require(load_configuration(dir / "list.txt", 48000, 2).sample_rate == 48000,
            "skip unrelated configuration IR");
    wave(dir / "empty.wav", 48000, 1, {});
    rejects([&] { load_configuration(dir / "empty.wav"); });
    text_file(dir / "mismatch.cfg", "44100 1 1 0\n0\n0\nir ü.wav\n0\n0\n0\n");
    rejects([&] { load_configuration(dir / "mismatch.cfg"); });
    wave(dir / "short.wav", 48000, 1, {1});
    text_file(dir / "unequal.cfg", valid + "short.wav\n0\n-0.25 0.5 -1.0\n0\n");
    const auto unequal = load_configuration(dir / "unequal.cfg");
    require(unequal.paths[0].impulse.size() == 3 && unequal.paths[1].impulse.size() == 1 &&
                unequal.paths[1].inputs[0].gain == -.25f && unequal.paths[1].inputs[1].gain == .5f,
            "unequal IR extension and route fractions");
    text_file(dir / "a.txt", "b.txt\n");
    text_file(dir / "b.txt", "a.txt\n");
    rejects([&] { load_configuration(dir / "a.txt"); });
    text_file(dir / "garbage.wav", std::string("RIFF\0\1\2\3", 8));
    rejects([&] { load_configuration(dir / "garbage.wav"); });
    text_file(dir / "truncated.raw", "12345");
    rejects([&] { AudioReader r(dir / "truncated.raw", RawSpec{48000, 2}); });
    text_file(dir / "nonfinite.raw", std::string("\0\0\xc0\x7f", 4));
    rejects([&] {
        AudioReader r(dir / "nonfinite.raw", RawSpec{48000, 1});
        std::vector<float> x(1);
        r.read(x);
    });
    const unsigned char nan_wave[] = {'R',  'I',  'F', 'F', 40, 0,    0, 0, 'W', 'A', 'V',  'E',
                                      'f',  'm',  't', ' ', 16, 0,    0, 0, 3,   0,   1,    0,
                                      0x80, 0xbb, 0,   0,   0,  0xee, 2, 0, 4,   0,   32,   0,
                                      'd',  'a',  't', 'a', 4,  0,    0, 0, 0,   0,   0xc0, 0x7f};
    text_file(dir / "nonfinite.wav",
              std::string(reinterpret_cast<const char*>(nan_wave), sizeof nan_wave));
    rejects([&] { load_configuration(dir / "nonfinite.wav"); });
    {
        const auto p = dir / "truncated.wav";
        AudioWriter w(p, {48000, 1}, OutputFormat::wav_pcm16);
        std::vector<float> x{1, .5f, 0};
        w.write(x);
        w.close();
        fs::resize_file(p, fs::file_size(p) - 1);
        rejects([&] {
            AudioReader r(p);
            std::vector<float> y(5);
            r.read(y);
        });
    }
    {
        const auto p = dir / "bad-alignment.wav";
        auto bytes = std::string(reinterpret_cast<const char*>(nan_wave), sizeof nan_wave);
        bytes[40] = 3;
        text_file(p, bytes);
        rejects([&] { AudioReader r(p); });
    }
    {
        const auto p = dir / "odd-metadata.wav";
        auto bytes = std::string(reinterpret_cast<const char*>(nan_wave), sizeof nan_wave);
        bytes[46] = std::bit_cast<char>(std::uint8_t{0x80});
        bytes[47] = char(0x3f);
        bytes.insert(12, std::string("JUNK\3\0\0\0abc\0", 12));
        bytes[4] = 52;
        text_file(p, bytes);
        AudioReader r(p);
        std::vector<float> x(1);
        require(r.read(x) == 1 && x[0] == 1, "odd-sized RIFF chunk padding");
    }
    {
        // Out-of-range metadata in a file is a data error (runtime_error), so list
        // traversal and inspection diagnose it instead of failing as a caller error.
        auto bytes = std::string(reinterpret_cast<const char*>(nan_wave), sizeof nan_wave);
        bytes[27] = 1; // Sample rate 0x0100bb80.
        text_file(dir / "rate.wav", bytes);
        bool data_error = false;
        try {
            AudioReader r(dir / "rate.wav");
        } catch (const std::invalid_argument&) {
        } catch (const std::runtime_error&) {
            data_error = true;
        }
        require(data_error, "out-of-range file sample rate is a runtime_error");
    }
    text_file(dir / "huge.cfg", std::string(1024 * 1024 + 1, 'x'));
    rejects([&] { load_configuration(dir / "huge.cfg"); });
    for (int i = 0; i < 17; ++i)
        text_file(dir / ("depth" + std::to_string(i)), "depth" + std::to_string(i + 1) + "\n");
    rejects([&] { load_configuration(dir / "depth0"); });
    {
        // Diagnostics name the physical line/column and the list include chain.
        auto message = [](const fs::path& p) -> std::string {
            try {
                load_configuration(p, 48000, 1);
            } catch (const std::exception& e) {
                return e.what();
            }
            throw std::runtime_error("expected configuration rejection");
        };
        auto contains = [](const std::string& text, const std::string& part) {
            return text.find(part) != std::string::npos;
        };
        text_file(dir / "located.cfg",
                  "\xef\xbb\xbf// c\n\n48000 1 1 0\n0\n0\n\nir.wav\n0\n 1\n0\n");
        require(
            contains(message(dir / "located.cfg"), "located.cfg:9:2: route channel out of range"),
            "physical line/column after BOM, comments and blank lines");
        fs::create_directory(dir / "nest");
        text_file(dir / "nest" / "broken.cfg", "48000 1 1 0\n0\n0\nmissing-ir.wav\n0\n0\n0\n");
        text_file(dir / "nest" / "inner.txt", "// inner\nbroken.cfg\n");
        text_file(dir / "outer.txt", "44100.wav\nnest\\inner.txt\n");
        const auto chained = message(dir / "outer.txt");
        require(contains(chained, "broken.cfg:4:1: filter 'missing-ir.wav'") &&
                    contains(chained, "inner.txt:2; included from ") &&
                    chained.ends_with("outer.txt:2"),
                "nested list failures carry the include chain");
#ifndef _WIN32
        text_file(dir / "drive.cfg", "48000 1 1 0\n0\n0\nD:\\filters\\irs\\ir ü.wav\n0\n0\n0\n");
        const auto drive = message(dir / "drive.cfg");
        require(contains(drive, "drive.cfg:4:1: Windows drive/rooted paths") &&
                    contains(drive, "a file exists at 'irs/ir ü.wav'"),
                "nonportable path diagnostics name the line and an existing replacement");
#endif
        // Overwrite protection sees every visited text file and referenced IR, including
        // unselected entries whose IRs are absent, in visiting order.
        std::vector<fs::path> sources;
        load_configuration(dir / "list.txt", 48000, 2, &sources);
        require(sources ==
                    std::vector<fs::path>{fs::weakly_canonical(dir / "list.txt"),
                                          fs::weakly_canonical(dir / "unrelated.cfg"),
                                          fs::weakly_canonical(dir / "missing-unused.wav"),
                                          fs::weakly_canonical(dir / "good.cfg"),
                                          fs::weakly_canonical(dir / fs::path(u8"ir ü.wav"))},
                "list provenance order");
        // Selection stops at the first match; later entries are not read.
        text_file(dir / "later-broken.cfg", "48000 x\n");
        text_file(dir / "early.txt", "good.cfg\nlater-broken.cfg\n");
        require(load_configuration(dir / "early.txt", 48000, 2, &sources).sample_rate == 48000 &&
                    sources.size() == 3,
                "first-match early exit");
    }
#ifndef _WIN32
    descriptor_tests();
    {
        const auto p = dir / "stdin-truncated.raw";
        text_file(p, "12345");
        const auto fd = open(p.c_str(), O_RDONLY);
        require(fd >= 0, "open redirected RAW input");
        Redirect redirect(0, fd);
        close(fd);
        rejects([&] {
            AudioReader r("-", RawSpec{48000, 1});
            std::vector<float> x(1);
            r.read(x);
            r.read(x);
        });
    }
    {
        const auto p = dir / "close-error.wav";
        AudioWriter w(p, {48000, 1}, OutputFormat::wav_float);
        std::vector<float> x{1};
        w.write(x);
        const int readonly = open(p.c_str(), O_RDONLY);
        require(readonly >= 0, "open close-error fixture");
        struct stat target {};
        require(fstat(readonly, &target) == 0, "stat close-error fixture");
        int writer_fd = -1;
        for (int fd = 3; fd < 256; ++fd) {
            struct stat candidate {};
            const int flags = fcntl(fd, F_GETFL);
            if (fd != readonly && flags >= 0 && (flags & O_ACCMODE) != O_RDONLY &&
                fstat(fd, &candidate) == 0 && candidate.st_dev == target.st_dev &&
                candidate.st_ino == target.st_ino) {
                writer_fd = fd;
                break;
            }
        }
        require(writer_fd >= 0 && dup2(readonly, writer_fd) >= 0, "inject header rewrite failure");
        close(readonly);
        rejects([&] { w.close(); });
    }
#endif
}
int main() {
    const auto dir = fs::temp_directory_path() /
                     ("convolver-io-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(dir);
        run(dir);
        speaker_tests(dir);
        fs::remove_all(dir);
        std::cout << "native I/O tests passed\n";
    } catch (const std::exception& e) {
        fs::remove_all(dir);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
