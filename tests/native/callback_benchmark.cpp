// SPDX-License-Identifier: GPL-2.0-or-later
// Callback-level measurements for the costly moments as well as warmed
// throughput: the host callback size is chosen independently of the partition
// size, and startup, reset and live replacement are timed through
// HostProcessor, as the plug-ins drive the engine. The load scenario times
// load_configuration on a crossover that names one multichannel file per path.
#include "convolver/engine.hpp"
#include "convolver/host.hpp"
#include "convolver/io.hpp"
#include "kernels.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
double microseconds(Clock::duration d) {
    return std::chrono::duration<double, std::micro>(d).count();
}
struct Options {
    std::string scenario = "warm", layout = "matrix";
    std::size_t taps = 65536, inputs = 6, outputs = 6, block = 256, callback = 64;
    double seconds = 2;
};
const char* usage =
    "usage: convolver_callback_benchmark [--scenario warm|startup|reset|replace|load]\n"
    "  [--taps N] [--inputs N] [--outputs N] [--layout diagonal|matrix|crossover]\n"
    "  [--block B] [--callback FRAMES] [--seconds S]\n"
    "warm: callbacks after the history is full. startup: the first callbacks of a new\n"
    "processor. reset: reset() and the callbacks after it. replace: prepare, publish and\n"
    "the callbacks after it. Each costly-moment scenario times the callbacks that cover\n"
    "the filter length plus two blocks, repeated for the given seconds. load: decode a\n"
    "crossover whose paths each name a channel of one outputs-channel WAV file.";
Options parse(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc)
            throw std::invalid_argument(usage);
        const std::string key = argv[i], value = argv[i + 1];
        if (key == "--scenario")
            o.scenario = value;
        else if (key == "--layout")
            o.layout = value;
        else if (key == "--taps")
            o.taps = std::stoul(value);
        else if (key == "--inputs")
            o.inputs = std::stoul(value);
        else if (key == "--outputs")
            o.outputs = std::stoul(value);
        else if (key == "--block")
            o.block = std::stoul(value);
        else if (key == "--callback")
            o.callback = std::stoul(value);
        else if (key == "--seconds")
            o.seconds = std::stod(value);
        else
            throw std::invalid_argument(usage);
    }
    if (!o.taps || !o.inputs || !o.outputs || !o.block || !o.callback ||
        !std::isfinite(o.seconds) || o.seconds <= 0 || o.seconds > 600)
        throw std::invalid_argument(usage);
    return o;
}
std::uint32_t state = 12345;
float random_value() {
    state = 1664525 * state + 1013904223;
    return float(state >> 8) / 16777216.f - .5f;
}
bool connected(const Options& o, std::size_t in, std::size_t out) {
    if (o.layout == "diagonal")
        return in == out;
    if (o.layout == "crossover")
        return out % o.inputs == in;
    if (o.layout == "matrix")
        return true;
    throw std::invalid_argument(usage);
}
convolver::Configuration configuration(const Options& o, float scale) {
    convolver::Configuration config{48000, o.inputs, o.outputs, 0, {}, {}, {}};
    for (std::size_t in = 0; in < o.inputs; ++in)
        for (std::size_t out = 0; out < o.outputs; ++out) {
            if (!connected(o, in, out))
                continue;
            convolver::FilterPath path;
            path.inputs = {{in, 1}};
            path.outputs = {{out, 1}};
            path.impulse.resize(o.taps);
            for (auto& x : path.impulse)
                x = random_value() * scale;
            config.paths.push_back(std::move(path));
        }
    return config;
}
struct Callbacks {
    std::vector<double> us;
    double frames = 0, total_us = 0;
    void add(double t, std::size_t n) {
        us.push_back(t);
        total_us += t;
        frames += double(n);
    }
};
class Driver {
  public:
    explicit Driver(const Options& o)
        : o_(o), input_(o.inputs * o.callback), output_(o.outputs * o.callback) {
        for (auto& x : input_)
            x = random_value();
        for (std::size_t c = 0; c < o.inputs; ++c)
            in_.push_back(input_.data() + c * o.callback);
        for (std::size_t c = 0; c < o.outputs; ++c)
            out_.push_back(output_.data() + c * o.callback);
    }
    double call(convolver::HostProcessor& p) {
        const auto before = Clock::now();
        if (!p.process(std::span<const float* const>(in_), std::span<float* const>(out_),
                       o_.callback))
            throw std::runtime_error("processing refused");
        const auto t = microseconds(Clock::now() - before);
        checksum_ += output_[0];
        return t;
    }
    double checksum() const {
        return checksum_;
    }

  private:
    const Options& o_;
    std::vector<float> input_, output_;
    std::vector<const float*> in_;
    std::vector<float*> out_;
    double checksum_ = 0;
};
void report(const Options& o, std::size_t paths, Callbacks& c, double event_us, double prepare_ms,
            std::size_t repetitions, double checksum) {
    if (!std::isfinite(checksum))
        throw std::runtime_error("nonfinite benchmark output");
    std::sort(c.us.begin(), c.us.end());
    const auto percentile = [&](double q) {
        return c.us.empty() ? 0.0 : c.us[static_cast<std::size_t>(q * double(c.us.size() - 1))];
    };
    std::cout << "backend,kernels,scenario,block,callback,taps,channels,paths,repetitions,"
                 "ns_per_frame,p50_us,p99_us,p999_us,max_us,event_us,prepare_ms\n"
              << std::setprecision(6) << convolver::fft_backend_name() << ','
              << convolver::detail::kernels().name << ',' << o.scenario << ',' << o.block << ','
              << o.callback << ',' << o.taps << ',' << o.inputs << "->" << o.outputs << ',' << paths
              << ',' << repetitions << ',' << c.total_us * 1e3 / c.frames << ',' << percentile(.5)
              << ',' << percentile(.99) << ',' << percentile(.999) << ','
              << (c.us.empty() ? 0.0 : c.us.back()) << ',' << event_us << ',' << prepare_ms << '\n';
}
void write_crossover(const Options& o, const std::filesystem::path& dir) {
    std::filesystem::create_directories(dir);
    std::vector<float> samples(o.taps * o.outputs);
    for (auto& x : samples)
        x = random_value() * .02f;
    {
        convolver::AudioWriter writer(dir / "crossover.wav", convolver::AudioInfo{48000, o.outputs},
                                      convolver::OutputFormat::wav_float);
        writer.write(samples);
        writer.close();
    }
    std::ofstream cfg(dir / "filter.cfg");
    cfg << "48000 " << o.inputs << ' ' << o.outputs << " 0\n";
    for (std::size_t i = 0; i < o.inputs; ++i)
        cfg << (i ? " 0" : "0");
    cfg << '\n';
    for (std::size_t i = 0; i < o.outputs; ++i)
        cfg << (i ? " 0" : "0");
    cfg << '\n';
    for (std::size_t out = 0; out < o.outputs; ++out)
        cfg << "crossover.wav\n" << out << '\n' << out % o.inputs << '\n' << out << '\n';
}
} // namespace

int main(int argc, char** argv) {
    try {
        const auto o = parse(argc, argv);
        if (o.scenario == "load") {
            const auto dir = std::filesystem::temp_directory_path() /
                             ("convolver-load-benchmark-" + std::to_string(o.taps) + "-" +
                              std::to_string(o.inputs) + "-" + std::to_string(o.outputs));
            write_crossover(o, dir);
            Callbacks loads;
            std::size_t repetitions = 0, paths = 0;
            const auto start = Clock::now();
            do {
                const auto before = Clock::now();
                const auto config = convolver::load_configuration(dir / "filter.cfg");
                loads.add(microseconds(Clock::now() - before), o.taps * config.paths.size());
                paths = config.paths.size();
                ++repetitions;
            } while (std::chrono::duration<double>(Clock::now() - start).count() < o.seconds);
            std::filesystem::remove_all(dir);
            // ns_per_frame here is per decoded path tap; p50..max are whole loads.
            report(o, paths, loads, 0, 0, repetitions, 0);
            return 0;
        }
        const auto config = configuration(o, .02f);
        // Callbacks covering the whole filter and two blocks: long enough for
        // the history to fill after a costly moment.
        const auto span = (o.taps + 2 * o.block + o.callback - 1) / o.callback;
        Driver driver(o);
        Callbacks callbacks;
        double event_us = 0, prepare_ms = 0;
        std::size_t repetitions = 0;
        const auto start = Clock::now();
        const auto elapsed = [&] {
            return std::chrono::duration<double>(Clock::now() - start).count();
        };
        if (o.scenario == "startup") {
            do {
                const auto before = Clock::now();
                convolver::HostProcessor p(config, o.callback, o.block);
                prepare_ms += microseconds(Clock::now() - before) / 1e3;
                for (std::size_t i = 0; i < span; ++i)
                    callbacks.add(driver.call(p), o.callback);
                ++repetitions;
            } while (elapsed() < o.seconds);
            prepare_ms /= double(repetitions);
        } else {
            convolver::HostProcessor p(config, o.callback, o.block);
            for (std::size_t i = 0; i < span; ++i)
                driver.call(p);
            if (o.scenario == "warm") {
                do {
                    for (int i = 0; i < 64; ++i)
                        callbacks.add(driver.call(p), o.callback);
                    ++repetitions;
                } while (elapsed() < o.seconds);
            } else if (o.scenario == "reset" || o.scenario == "replace") {
                const auto replacement = configuration(o, .01f);
                do {
                    const auto before = Clock::now();
                    if (o.scenario == "reset") {
                        p.reset();
                        event_us += microseconds(Clock::now() - before);
                    } else {
                        auto prepared = p.prepare(replacement);
                        prepare_ms += microseconds(Clock::now() - before) / 1e3;
                        p.publish(std::move(prepared));
                    }
                    for (std::size_t i = 0; i < span; ++i)
                        callbacks.add(driver.call(p), o.callback);
                    p.collect();
                    ++repetitions;
                } while (elapsed() < o.seconds);
                event_us /= double(repetitions);
                prepare_ms /= double(repetitions);
            } else {
                throw std::invalid_argument(usage);
            }
        }
        report(o, config.paths.size(), callbacks, event_us, prepare_ms, repetitions,
               driver.checksum());
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
