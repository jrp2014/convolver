// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/host.hpp"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>
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
using namespace convolver;
void require(bool value, const char* message) {
    if (!value)
        throw std::runtime_error(message);
}
Configuration mono(float gain = 1) {
    return {48000, 1, 1, 0, {}, {}, {{{gain, .25f}, {{0, 1}}, {{0, 1}}}}};
}
template <class T> void numeric() {
    HostProcessor host(mono(), 67, 8);
    std::array<T, 67> data{};
    std::vector<T> result;
    std::size_t position = 0;
    for (auto count : {3, 0, 17, 1, 67, 29}) {
        for (int j = 0; j < count; ++j)
            data[j] = position + j == 0 ? T(1) : T(0);
        const T* in[] = {data.data()};
        T* out[] = {data.data()};
        audio_thread = true;
        bool ok = host.process(in, out, count);
        audio_thread = false;
        require(ok, "host rejected valid block");
        result.insert(result.end(), data.begin(), data.begin() + count);
        position += count;
    }
    for (std::size_t i = 0; i < result.size(); ++i)
        require(std::abs(result[i] - (i == 8   ? 1
                                      : i == 9 ? .25
                                               : 0)) < 1e-5,
                "planar/in-place convolution or latency");
    host.reset();
    data.fill(0);
    data[0] = std::numeric_limits<T>::quiet_NaN();
    data[1] = std::numeric_limits<T>::infinity();
    const T* in[] = {data.data()};
    T* out[] = {data.data()};
    audio_thread = true;
    bool ok = host.process(in, out, 67);
    audio_thread = false;
    require(ok, "nonfinite input must be contained");
    for (auto v : data)
        require(v == 0, "invalid samples poisoned state");
    require(!host.process(in, out, 68), "overlarge callback accepted");
}
void lifecycle() {
    auto c = mono();
    c.paths[0].impulse = {0};
    HostProcessor host(c, 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    host.set_bypass(true);
    for (int k = 0; k < 3; ++k) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
    }
    for (auto v : output)
        require(v == 1, "bypass must pass latency-matched dry signal");
    host.set_bypass(false);
    for (int k = 0; k < 3; ++k)
        host.process(in, out, 64);
    for (auto v : output)
        require(v == 0, "bypass does not return to wet");
    c.paths[0].impulse = {1};
    host.replace(c);
    for (int k = 0; k < 8; ++k) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
        for (auto v : output)
            require(v >= 0 && v <= 1.00001f, "filter transition overshoot");
    }
    for (auto v : output)
        require(std::abs(v - 1) < 1e-5, "filter replacement not applied");
    host.collect();
    audio_thread = true;
    host.reset();
    host.process({}, out, 64);
    audio_thread = false;
    for (auto v : output)
        require(v == 0, "restart retains old history");
    std::atomic<bool> done = false;
    std::thread producer([&] {
        for (int k = 0; k < 100; ++k) {
            auto next = mono(k % 2 ? .5f : 1.f);
            host.replace(next);
        }
        done = true;
    });
    while (!done.load()) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
        for (auto v : output)
            require(std::isfinite(v), "concurrent replacement nonfinite");
    }
    producer.join();
    host.collect();
}
void queued_replacements() {
    auto c = mono();
    c.paths[0].impulse = {1};
    HostProcessor host(c, 64, 8);
    std::array<float, 64> input{}, output{};
    input.fill(1);
    const float* in[] = {input.data()};
    float* out[] = {output.data()};
    c.paths[0].impulse = {2};
    host.replace(c);
    audio_thread = true;
    host.process(in, out, 64);
    audio_thread = false;
    c.paths[0].impulse = {3};
    host.replace(c); // Queued during the first transition; no later control calls.
    for (int k = 0; k < 32; ++k) {
        audio_thread = true;
        host.process(in, out, 64);
        audio_thread = false;
    }
    for (auto v : output)
        require(std::abs(v - 3) < 1e-5, "queued replacement waits for control collection");
}
int main() {
    try {
        numeric<float>();
        numeric<double>();
        lifecycle();
        queued_replacements();
        std::cout << "host lifecycle passed\n";
    } catch (const std::exception& e) {
        audio_thread = false;
        std::cerr << e.what() << '\n';
        return 1;
    }
}
