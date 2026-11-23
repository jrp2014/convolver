// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "legacy.inc"
#include <chrono>
#include <fstream>
#include <iomanip>
#include <span>

using Clock = std::chrono::steady_clock;
using Samples = std::vector<float>;
using Duration = std::chrono::duration<double>;
constexpr unsigned taps = 65536;
constexpr unsigned batch =
    20 * taps; // NSAMPLES (10) times the padded legacy length (131072 at power-of-two partitions).
struct Stimulus {
    uint32_t state = 9817;
    float next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (1.0f / 8388608.0f) - 1.0f;
    }
};
struct Legacy {
    Convolution<float> engine;
    ConvertSample<float> convert;
    bool plain;
    Legacy(unsigned p, bool measure)
        : engine("fixture", std::max(p, 1u), measure ? 1 : 0), plain(p == 0) {}
    unsigned latency() const {
        return engine.Mixer.nHalfPartitionLength();
    }
    void process(const float* in, float* out, unsigned frames) {
        InterleavedIO<float> io(reinterpret_cast<const BYTE*>(in), reinterpret_cast<BYTE*>(out),
                                &convert, &convert);
        auto bytes = plain ? engine.doConvolution(io, frames, 0)
                           : engine.doPartitionedConvolution(io, frames, 0);
        if (bytes != frames * output_channels * sizeof(float))
            throw std::runtime_error("legacy size");
    }
    unsigned score_frames() const {
        return NSAMPLES * engine.Mixer.nFilterLength();
    }
    float score(unsigned) {
        float db = 0;
        if (engine.calculateOptimumAttenuation(db, plain) != S_OK)
            throw std::runtime_error("legacy attenuation failed");
        return db;
    }
    void reset() {
        engine.Flush();
    }
};
struct Portable {
    convolver::Engine engine;
    Portable(const convolver::Configuration& c, unsigned latency, bool measure)
        : engine(c, latency,
                 {measure ? convolver::FftPlanning::measure : convolver::FftPlanning::estimate}) {}
    unsigned latency() const {
        return engine.latency_frames();
    }
    void process(const float* in, float* out, unsigned frames) {
        engine.process({in, size_t(frames) * input_channels},
                       {out, size_t(frames) * output_channels}, frames);
    }
    unsigned score_frames() const {
        return batch;
    }
    float score(unsigned frames) {
        reset();
        Samples in(size_t(frames) * input_channels), out(size_t(frames) * output_channels);
        archival_noise(in.data(), in.size());
        std::fill(out.begin(), out.end(), 0);
        process(in.data(), out.data(), frames);
        float peak = 0;
        for (size_t i = latency(); i < out.size(); ++i)
            peak = std::max(peak, std::abs(out[i]));
        float db = std::clamp(float(-20 * std::log10(peak)), -1000.f, 1000.f);
        reset();
        return db;
    }
    void reset() {
        engine.reset();
    }
};
template <class Engine> void feed(Engine& e, const Samples& in, Samples& out, unsigned callback) {
    const unsigned frames = in.size() / input_channels;
    for (unsigned pos = 0; pos < frames; pos += callback)
        e.process(in.data() + pos * input_channels, out.data() + pos * output_channels,
                  std::min(callback, frames - pos));
}
template <class Engine> double verify(Engine& e, const convolver::Configuration& c, unsigned cb) {
    const unsigned frames = taps * 2 + e.latency() + 137;
    Samples input(size_t(frames) * input_channels), output(size_t(frames) * output_channels);
    std::vector<double> expected(output.size());
    for (unsigned ch = 0; ch < input_channels; ++ch) {
        for (unsigned start : {ch * 7, 16381 + ch * 3, 65552 + ch * 5}) {
            float amp = (ch + 1) * (start % 2 ? -0.125f : 0.09375f);
            input[start * input_channels + ch] = amp;
            for (const auto& p : c.paths)
                for (const auto& ir : p.inputs)
                    if (ir.channel == ch)
                        for (const auto& dest : p.outputs)
                            for (unsigned k = 0; k < p.impulse.size(); ++k)
                                expected[(start + e.latency() + k) * output_channels +
                                         dest.channel] +=
                                    double(amp) * ir.gain * dest.gain * p.impulse[k];
        }
    }
    feed(e, input, output, cb);
    double err = 0, peak = 0;
    for (size_t i = 0; i < output.size(); ++i) {
        if (!std::isfinite(output[i]))
            throw std::runtime_error("nonfinite");
        err = std::max(err, std::abs(output[i] - expected[i]));
        peak = std::max(peak, std::abs(expected[i]));
    }
    if (peak == 0 || err / peak > 5e-5)
        throw std::runtime_error("oracle error " + std::to_string(err / peak));
    e.reset();
    return err / peak;
}
template <class Engine>
void run(Engine& e, const convolver::Configuration& c, unsigned cb, const std::string& method,
         double seconds) {
    const double error = verify(e, c, cb);
    double elapsed = 0, checksum = 0;
    unsigned runs = 0;
    if (method == "warm") {
        Samples in(size_t(batch) * input_channels), out(size_t(batch) * output_channels);
        Stimulus g;
        for (auto& x : in)
            x = g.next();
        feed(e, in, out, cb); // Fill every latency line and IR partition.
        do {
            const auto t = Clock::now();
            feed(e, in, out, cb);
            elapsed += Duration(Clock::now() - t).count();
            checksum += out[2390] + out.back();
            ++runs;
        } while (elapsed < seconds);
    } else {
        do {
            const auto t = Clock::now();
            checksum += e.score(e.score_frames());
            elapsed += Duration(Clock::now() - t).count();
            ++runs;
        } while (elapsed < seconds);
    }
    if (!std::isfinite(checksum) || checksum == 0)
        throw std::runtime_error("unusable timing output");
    const double audio_seconds =
        double(method == "warm" ? batch : e.score_frames()) * runs / c.sample_rate;
    std::cout << std::setprecision(10) << e.latency() << "," << runs << "," << elapsed << ","
              << audio_seconds / elapsed << "," << 100 * elapsed / audio_seconds << "," << error
              << "," << checksum << "\n";
}
int main(int argc, char** argv) {
    try {
        if (argc != 10)
            throw std::runtime_error(
                "bench rawir channels rate stereo|matrix engine setting cb method seconds");
        const unsigned channels = std::stoul(argv[2]), rate = std::stoul(argv[3]),
                       setting = std::stoul(argv[6]), cb = std::stoul(argv[7]);
        const std::string layout = argv[4], kind = argv[5], method = argv[8];
        const bool six = layout == "matrix", measure = method == "legacy-style";
        if (measure)
            fftwf_import_wisdom_from_filename("plans.wisdom");
        fixture_rate = rate;
        input_channels = output_channels = six ? 6 : channels;
        Samples raw(size_t(channels) * taps);
        std::ifstream f(argv[1], std::ios::binary);
        if (!f.read(reinterpret_cast<char*>(raw.data()), raw.size() * sizeof(float)) ||
            f.peek() != EOF)
            throw std::runtime_error("IR length");
        input_delays.assign(input_channels, 0);
        output_delays.assign(output_channels, 0);
        convolver::Configuration cfg{int(rate), input_channels, output_channels, 0, {}, {}, {}};
        for (unsigned i = 0; i < input_channels; ++i) {
            Samples ir(taps);
            for (unsigned t = 0; t < taps; ++t)
                ir[t] = raw[t * channels + (six ? 0 : i)];
            PathFixture p{i, {}, {}};
            convolver::FilterPath port{ir, {}, {}};
            if (six) {
                // The six historical route tokens are 0.16667, 1.16667, ..., 5.16667
                // (single-precision modff).
                for (unsigned ch = 0; ch < 6; ++ch) {
                    float gain = std::stof(std::to_string(ch) + ".16667") - float(ch);
                    p.inputs.push_back({WORD(ch), gain});
                    port.inputs.push_back({ch, gain});
                }
                unsigned dest = 5 - i;
                float gain = std::stof(std::to_string(dest) + ".16667") - float(dest);
                p.outputs.push_back({WORD(dest), gain});
                port.outputs.push_back({dest, gain});
            } else {
                p.inputs.push_back({WORD(i), 1});
                p.outputs.push_back({WORD(i), 1});
                port.inputs.push_back({i, 1});
                port.outputs.push_back({i, 1});
            }
            impulse_responses.push_back(ir);
            path_fixtures.push_back(p);
            cfg.paths.push_back(port);
        }
        if (kind == "old") {
            Legacy engine(setting, measure);
            run(engine, cfg, cb, method, std::stod(argv[9]));
        } else {
            Portable engine(cfg, setting, measure && convolver::fft_supports_wisdom());
            run(engine, cfg, cb, method, std::stod(argv[9]));
        }
        if (measure)
            fftwf_export_wisdom_to_filename("plans.wisdom");
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
