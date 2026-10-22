// SPDX-License-Identifier: GPL-2.0-or-later
// Portable boundary scaffolding only. Production adapters are inserted by Python.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

typedef unsigned char BYTE;
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef int32_t INT32;
typedef float BaseT;
template<typename T> struct NoiseShaper { enum NoiseShapingType { None }; };
template<typename T> struct Ditherer { enum DitherType { None }; };
template<typename T> class TestHolder {
    std::unique_ptr<T> ptr_;
public:
    explicit TestHolder(T* ptr) : ptr_(ptr) {}
    T* operator->() const { return ptr_.get(); }
    T* get_ptr() const { return ptr_.get(); }
};
// Portable substitute for Boost's Floor conversion boundary. Real production
// NoDither, NoNoiseShape, its scaling constants, and PCM methods are extracted.
// In-range finite values have the same floor-towards-negative-infinity result.
template<typename IntType, typename T> IntType Floor(T value) {
    const double rounded = std::floor(static_cast<double>(value));
    if (!std::isfinite(rounded) || rounded < std::numeric_limits<IntType>::min() ||
        rounded > std::numeric_limits<IntType>::max())
        throw std::runtime_error("quantization outside integer range");
    return static_cast<IntType>(rounded);
}
typedef std::vector<float> ChannelBuffer;
typedef std::vector<ChannelBuffer> SampleBuffer;
struct ChannelPaths {
    std::vector<DWORD> input_delay, output_delay;
    ChannelPaths() : input_delay(2, 0), output_delay(2, 0) {}
    unsigned nInputChannels() const { return 2; }
    unsigned nOutputChannels() const { return 2; }
    unsigned nPartitionLength() const { return 8; }
    const std::vector<DWORD>& nInputSamplesDelay() const { return input_delay; }
    const std::vector<DWORD>& nOutputSamplesDelay() const { return output_delay; }
};

// INSERT_SOURCE_ADAPTERS

static void equal(double actual, double expected, const char* message, double tolerance = 1e-6) {
    if (!std::isfinite(actual) || std::fabs(actual - expected) > tolerance) {
        std::cerr << message << ": expected " << expected << ", got " << actual << '\n';
        throw std::runtime_error(message);
    }
}

template<int Bits, int Container> static void pcm_case() {
    // Signed PCM is left-aligned in a little-endian 24-bit container.
    const BYTE bytes[][3] = {{0, 0, 128}, {0, 0, 64}, {0, 0, 192}, {0, 0, 0}, {255, 255, 127}};
    // Retain Convolver's documented symmetric mapping, (integer + 0.5) / q.
    // In particular, integer zero intentionally is not floating-point zero.
    const double expected16[] = {-1.0, 0.500022888532845, -0.499992370489052, 0.0000152590218966964, 1.0};
    const double expected20[] = {-1.0, 0.500001430512839, -0.499999523162387, 0.000000953675225902, 1.0};
    const double expected24[] = {-1.0, 0.500000089406973, -0.499999970197676, 0.0000000596046483281, 1.0};
    const double* expected = Bits == 16 ? expected16 : Bits == 20 ? expected20 : expected24;
    Pcm24Adapter<Bits> packed24;
    Pcm32Adapter<Bits> packed32;
    for (unsigned n = 0; n < 5; ++n) {
        // Deliberately unaligned: PCM containers are byte sequences.
        alignas(4) BYTE storage[] = {0xa5, 0, bytes[n][0], bytes[n][1], bytes[n][2], 0xa5};
        const BYTE* start = Container == 24 ? &storage[2] : &storage[1];
        const BYTE* cursor = start;
        DWORD processed = 0;
        float sample = 99;
        if (Container == 24) packed24.GetSample(sample, cursor, 1, processed);
        else packed32.GetSample(sample, cursor, 1, processed);
        equal(sample, expected[n], "PCM signed sample", 1e-7);
        equal(processed, Container / 8, "PCM byte count");
        equal(cursor - start, Container / 8, "PCM cursor");
    }
    const BYTE minimum[] = {0, 0, 0, 128};
    const BYTE* cursor = Container == 24 ? &minimum[1] : minimum;
    DWORD processed = 0;
    float sample = 99;
    if (Container == 24) packed24.GetSample(sample, cursor, 0.25f, processed);
    else packed32.GetSample(sample, cursor, 0.25f, processed);
    equal(sample, -0.25, "PCM attenuation");
}

template<int Bits, int Container> static void pcm_output_case() {
    const float samples[] = {0, 1, -1, 0.5f, -0.5f, 2, -2};
    // Literal left-aligned 32-bit words for the existing floor quantization.
    const uint32_t expected16[] = {0, 0x7fff0000, 0x80000000, 0x3fff0000, 0xc0000000, 0x7fff0000, 0x80000000};
    const uint32_t expected20[] = {0, 0x7ffff000, 0x80000000, 0x3ffff000, 0xc0000000, 0x7ffff000, 0x80000000};
    const uint32_t expected24[] = {0, 0x7fffff00, 0x80000000, 0x3fffff00, 0xc0000000, 0x7fffff00, 0x80000000};
    const uint32_t* expected = Bits == 16 ? expected16 : Bits == 20 ? expected20 : expected24;
    Pcm24Adapter<Bits> packed24;
    Pcm32Adapter<Bits> packed32;
    for (unsigned n = 0; n < 7; ++n) {
        alignas(4) BYTE storage[] = {0xa5, 0xa5, 0xa5, 0xa5, 0xa5, 0xa5};
        BYTE* cursor = &storage[1];
        DWORD generated = 0;
        if (Container == 24) packed24.PutSample(cursor, samples[n], 0, generated);
        else packed32.PutSample(cursor, samples[n], 0, generated);
        for (unsigned byte = 0; byte < Container / 8; ++byte) {
            const unsigned shift = byte * 8 + 32 - Container;
            equal(storage[1 + byte], (expected[n] >> shift) & 0xff, "PCM output byte");
        }
        equal(generated, Container / 8, "PCM output byte count");
        equal(cursor - &storage[1], Container / 8, "PCM output cursor");
        equal(storage[0], 0xa5, "PCM leading canary");
        equal(storage[1 + Container / 8], 0xa5, "PCM trailing canary");
    }
}

template<typename SampleT> static void vst_case(const char* name, const ConvertSample<float>& converter) {
    ChannelPaths mixer;
    SampleBuffer delay(2, std::vector<float>(8, 0));
    SampleBuffer input(2, std::vector<float>(8, -99));
    SampleBuffer output(2, std::vector<float>(8, 0));
    output[0][1] = 0.25f;
    output[1][1] = -0.5f;
    SampleT first[] = {0.75, -0.75};
    SampleT second[] = {-0.25, 0.25};
    SampleT first_output[] = {10, 11};
    SampleT second_output[] = {20, 21};
    SampleT* inputs[] = {first, second};
    SampleT* outputs[] = {first_output, second_output};
    const bool disconnected = !std::strcmp(name, "null_input");
    if (disconnected) inputs[1] = NULL;
    if (!std::strcmp(name, "inplace")) { outputs[0] = first; outputs[1] = second; }
    if (!std::strcmp(name, "cross_alias")) { outputs[0] = second; outputs[1] = first; }
    const bool delayed = !std::strcmp(name, "delays");
    if (delayed) {
        mixer.input_delay[0] = 1;
        delay[0][0] = 0.375f;
        mixer.output_delay[1] = 2;
        output[1][7] = -0.5f;
    }
    const bool null_output = !std::strcmp(name, "null_output");
    if (null_output) outputs[1] = NULL;
    VSTIO<SampleT, float> io(inputs, outputs, &converter, &converter);
    DWORD processed = 0, generated = 0;
    io.TransferFrame(mixer, input, output, 0.5f, 1, processed, generated, delay, 1);
    equal(input[0][1], 0.375, "preserve first input before writing output");
    equal(input[1][1], disconnected ? 0 : -0.125, "preserve second input or provide silence");
    const bool additive = !std::strcmp(name, "accumulate");
    equal(outputs[0][0], additive ? 10.25 : 0.25, "first output");
    if (!null_output) equal(outputs[1][0], additive ? 19.5 : -0.5, "second output");
    equal(processed, (disconnected ? 1 : 2) * sizeof(SampleT), "VST input byte count");
    equal(generated, (null_output ? 1 : 2) * sizeof(SampleT), "VST output byte count");
    io.TransferFrame(mixer, input, output, 1.0f, 2, processed, generated, delay, 2);
    equal(input[0][2], delayed ? 0.375 : -0.75, "advance first input cursor");
    equal(input[1][2], disconnected ? 0 : 0.25, "advance second input or provide silence");
    equal(processed, (disconnected ? 2 : 4) * sizeof(SampleT), "VST second input byte count");
    equal(generated, (null_output ? 2 : 4) * sizeof(SampleT), "VST second output byte count");
}


// A disconnected delayed channel must drain its history and then stay silent,
// including after both the delay ring and input partition wrap.
static void delay_history_case(bool mixed) {
    ChannelPaths mixer;
    mixer.input_delay[0] = mixed ? 0 : 2;
    mixer.input_delay[1] = mixed ? 3 : 2;
    SampleBuffer input(2, ChannelBuffer(8, -99));
    SampleBuffer output(2, ChannelBuffer(8, 0));
    SampleBuffer delay(2, ChannelBuffer(4, 0));
    float first[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    float second[] = {-1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12};
    float* inputs[] = {first, second};
    float* outputs[] = {NULL, NULL};
    ConvertSample_ieeefloat<float> converter;
    VSTIO<float, float> io(inputs, outputs, &converter, &converter);
    DWORD processed = 0, generated = 0;
    const float disconnected_expected[] = {0, 0, 1, 2, 3, 0, 0, 0, 0, 0, 0, 0};
    const float delayed_second[] = {0, 0, 0, -1, -2, -3, -4, -5, -6, -7, -8, -9};
    for (unsigned frame = 0; frame != 12; ++frame) {
        if (!mixed && frame == 3) { inputs[0] = NULL; inputs[1] = NULL; }
        io.TransferFrame(mixer, input, output, 1, frame % 8, processed, generated, delay, frame % 4);
        equal(input[0][frame % 8], mixed ? first[frame] : disconnected_expected[frame], "first delayed history");
        equal(input[1][frame % 8], mixed ? delayed_second[frame] : -disconnected_expected[frame], "second delayed history");
    }
    equal(processed, (mixed ? 24 : 6) * sizeof(float), "history byte count");
    equal(generated, 0, "disconnected outputs generate no bytes");
}

// Partially overlapping channel views: output one aliases the current sample
// of input two. This fixture is one frame; arbitrary future-frame overlap is
// outside the per-frame host adapter's guarantee.
static void overlap_case() {
    ChannelPaths mixer;
    SampleBuffer input(2, ChannelBuffer(8, -99)), output(2, ChannelBuffer(8, 0));
    SampleBuffer delay(2, ChannelBuffer(1, 0));
    float shared[] = {0.75f, -0.25f, 0.5f};
    float* inputs[] = {shared, shared + 1};
    float* outputs[] = {shared + 1, shared + 2};
    output[0][0] = 0.125f;
    output[1][0] = -0.5f;
    ConvertSample_ieeefloat<float> converter;
    VSTIO<float, float> io(inputs, outputs, &converter, &converter);
    DWORD processed = 0, generated = 0;
    io.TransferFrame(mixer, input, output, 1, 0, processed, generated, delay, 0);
    equal(input[0][0], 0.75, "overlap first input");
    equal(input[1][0], -0.25, "overlap second input");
    equal(shared[1], 0.125, "overlap first output");
    equal(shared[2], -0.5, "overlap second output");
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    try {
        const char* test = argv[1];
        if (!std::strcmp(test, "null_delayed_history")) delay_history_case(false);
        else if (!std::strcmp(test, "mixed_delays")) delay_history_case(true);
        else if (!std::strcmp(test, "overlap")) overlap_case();
        else if (!std::strcmp(test, "pcm24")) pcm_case<24, 24>();
        else if (!std::strcmp(test, "pcm20")) pcm_case<20, 24>();
        else if (!std::strcmp(test, "pcm16")) pcm_case<16, 24>();
        else if (!std::strcmp(test, "pcm32_16")) pcm_case<16, 32>();
        else if (!std::strcmp(test, "pcm32_20")) pcm_case<20, 32>();
        else if (!std::strcmp(test, "pcm32_24")) pcm_case<24, 32>();
        else if (!std::strcmp(test, "out24_16")) pcm_output_case<16, 24>();
        else if (!std::strcmp(test, "out24_20")) pcm_output_case<20, 24>();
        else if (!std::strcmp(test, "out24_24")) pcm_output_case<24, 24>();
        else if (!std::strcmp(test, "out32_16")) pcm_output_case<16, 32>();
        else if (!std::strcmp(test, "out32_20")) pcm_output_case<20, 32>();
        else if (!std::strcmp(test, "out32_24")) pcm_output_case<24, 32>();
        else if (!std::strcmp(test, "double")) vst_case<double>(test, ConvertSample_ieeedouble<float>());
        else if (!std::strcmp(test, "accumulate")) vst_case<float>(test, ConvertAddSample_ieeefloat<float>());
        else vst_case<float>(test, ConvertSample_ieeefloat<float>());
    } catch (const std::exception&) { return 1; }
    return 0;
}
