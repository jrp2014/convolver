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
#include <limits>
#include <stdexcept>
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
void run(const fs::path& dir) {
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
        fs::remove_all(dir);
        std::cout << "native I/O tests passed\n";
    } catch (const std::exception& e) {
        fs::remove_all(dir);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
