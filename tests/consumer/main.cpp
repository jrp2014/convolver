// SPDX-License-Identifier: GPL-2.0-or-later
#ifdef CONSUMER_HOST
#include "convolver/host.hpp"
#endif
#include <convolver/engine.hpp>
#ifdef CONSUMER_IO
#include <convolver/inspection.hpp>
#include <convolver/io.hpp>
#include <filesystem>
#endif
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
#ifdef CONSUMER_HOST
    convolver::Configuration hc{48000, 1, 1, 0, {}, {}, {{{1}, {{0, 1}}, {{0, 1}}}}};
    convolver::HostProcessor host(hc, 8, 4);
    float input[8] = {1}, output[8] = {};
    const float* inputs[] = {input};
    float* outputs[] = {output};
    if (!host.process(inputs, outputs, 8) || std::abs(output[4] - 1) > 1e-5)
        return 1;
#endif
    try {
        const std::vector<float> h = {1, -.5f, .25f}, x = {1, .5f, -1, 0, .25f};
        convolver::Configuration c{48000, 1, 1, 0, {}, {}, {{h, {{0, 1}}, {{0, 1}}}}};
#ifdef CONSUMER_IO
        const auto path = std::filesystem::path("consumer-impulse.wav");
        convolver::AudioWriter writer(path, {48000, 1}, convolver::OutputFormat::wav_float);
        writer.write(h);
        writer.close();
        c = convolver::load_configuration(path, 48000, 1);
        const auto report = convolver::inspect_configuration(path);
        if (!report.valid || report.configurations.at(0).analysis->tail_frames != h.size() - 1)
            throw std::runtime_error("installed inspection API mismatch");
        std::filesystem::remove(path);
#endif
        convolver::Engine engine(c, 3);
        const auto latency = engine.latency_frames();
        std::vector<float> out(x.size() + engine.tail_frames() + latency);
        engine.process(x, std::span<float>(out).first(x.size()), x.size());
        engine.process({}, std::span<float>(out).subspan(x.size()), out.size() - x.size());
        std::vector<double> expected(out.size());
        for (std::size_t i = 0; i < x.size(); ++i)
            for (std::size_t j = 0; j < h.size(); ++j)
                expected[latency + i + j] += double(x[i]) * h[j];
        for (std::size_t i = 0; i < out.size(); ++i)
            if (!std::isfinite(out[i]) || std::abs(out[i] - expected[i]) > 2e-5)
                throw std::runtime_error("external consumer convolution mismatch");
        std::cout << convolver::fft_backend_name() << ": external consumer passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
