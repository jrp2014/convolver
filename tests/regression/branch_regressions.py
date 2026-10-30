#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise additional review-branch fixes using extracted production C++.

Runs both route-scaling statements, the complete list-display method, dither
constant definitions, and the filter's full/tail FFT preparation blocks. The
Ooura check uses the bundled FFT, with only Windows threading disabled in a
temporary header. It checks normalization/padding round trips, not the complete
convolution engine. No Windows application or Boost random generator is run.

Use --revision 61592a3 to reproduce the pre-fix failures from Git.
"""

import argparse
import os
import re
import subprocess
import tempfile
from pathlib import Path

HARNESS = r"""
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "fftsg_h.h"
using DWORD = std::uint32_t;
using TCHAR = char;
using BaseT = float;
using std::abs;
#define TEXT(x) x
#define _copysign std::copysign
#define OOURA
int failures = 0;
void check(bool ok, const std::string& label) {
    if (!ok) { ++failures; std::cerr << "FAIL: " << label << '\n'; }
}
std::runtime_error channelPathsException(const char* message, const char*) {
    return std::runtime_error(message);
}
float route(float scale, bool input) {
    const DWORD nInputChannels_ = 4, nOutputChannels_ = 4;
    const char* szChannelPathsFileName = "fixture";
    if (input) {
        @INPUT_ROUTE@
        return scale;
    } else {
        @OUTPUT_ROUTE@
        return scale;
    }
}
struct MixerStub {
    std::string name;
    std::string DisplayChannelPaths() const { return name; }
};
struct Entry { MixerStub Mixer; };
template<typename T> struct ConvolutionList {
    using size_type = std::size_t;
    std::vector<Entry> ConvolutionList_;
    size_type nConvolutionList_ = 0, selectedConvolutionIndex_ = 0;
    bool selected = false;
    bool ConvolutionSelected() const { return selected; }
    const std::basic_string<TCHAR> DisplayConvolutionList() const;
};
@DISPLAY@
template<typename T, unsigned short validBits,
         typename distribution_type = void, typename base_generator_type = void>
struct SimpleDither { static const T Q, Qover2; };
template<typename T, typename IntType, int validBits>
struct NoNoiseShape { static const T Q_, Qover2_, q_; };
@CONSTANTS@
template<typename T, int bits> void check_quantum() {
    const double expected = 1.0 / (std::ldexp(1.0, bits - 1) - 0.5);
    const double tolerance = 4 * std::numeric_limits<T>::epsilon();
    const std::string label = "dither " + std::to_string(bits) + " bits, " +
                              std::to_string(sizeof(T)) + " byte sample";
    check(std::abs(SimpleDither<T,bits>::Q / expected - 1) <= tolerance,
          label + " full quantum");
    check(std::abs(SimpleDither<T,bits>::Qover2 / (expected / 2) - 1) <= tolerance,
          label + " half quantum");
    check(SimpleDither<T,bits>::Q == NoNoiseShape<T,int,bits>::Q_ &&
          SimpleDither<T,bits>::Qover2 == NoNoiseShape<T,int,bits>::Qover2_,
          label + " agrees with quantizer");
}
struct ChannelBuffer : std::vector<float> {
    using std::vector<float>::vector;
    void operator*=(float value) { for (float& sample : *this) sample *= value; }
};
float* c_ptr(std::vector<ChannelBuffer>& buffers, std::size_t index) {
    return buffers[index].data();
}
void fft_roundtrip(bool tail, DWORD length, DWORD taps) {
    const DWORD nPaddedPartitionLength = length, nHalfPartitionLength_ = length / 2;
    const DWORD nFrame = taps;
    DWORD nPartition = 0;
    std::vector<ChannelBuffer> coeffs_(1, ChannelBuffer(length, 999));
    std::vector<float> expected(length, 0);
    for (DWORD i = 0; i < taps; ++i)
        coeffs_[0][i] = expected[i] = (i % 2 ? -0.25f : 0.75f) * (i + 1);
    std::vector<int> ip_(length + 2, 0);
    std::vector<float> w_(length, 0);
    if (tail) {
        @TAIL_FFT@
    } else {
        @FULL_FFT@
    }
    check(nPartition == 1, "FFT preparation advances partition");
    rdft(length, OouraRBackward, coeffs_[0].data(), ip_.data(), w_.data());
    for (DWORD i = 0; i < length; ++i) {
        check(std::abs(coeffs_[0][i] - expected[i]) < 2e-5f,
              std::string(tail ? "tail" : "full") + " Ooura roundtrip N=" +
              std::to_string(length) + " taps=" + std::to_string(taps) +
              " sample=" + std::to_string(i));
    }
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string group = argv[1];
    if (group == "routes") {
        const char* tokens[] = {"0", "-0", "1", "-1", "0.5", "-0.5", "2.25", "-2.25"};
        const float expected[] = {1, -1, 1, -1, .5f, -.5f, .25f, -.25f};
        for (bool input : {true, false}) {
            for (unsigned i = 0; i < 8; ++i) {
                float parsed;
                std::istringstream(tokens[i]) >> parsed;
                check(route(parsed, input) == expected[i],
                      std::string(input ? "input " : "output ") + tokens[i]);
            }
        }
    } else if (group == "display") {
        for (unsigned count = 0; count <= 4; ++count) {
            for (int selected = -1; selected < static_cast<int>(count); ++selected) {
                ConvolutionList<float> list;
                list.nConvolutionList_ = count;
                list.selected = selected >= 0;
                list.selectedConvolutionIndex_ = selected >= 0 ? selected : 0;
                std::string expected = count ? "" : "Invalid or empty filter";
                for (unsigned i = 0; i < count; ++i) {
                    const std::string name = "filter" + std::to_string(i);
                    list.ConvolutionList_.push_back({{name}});
                    if (i) expected += '\n';
                    if (static_cast<int>(i) == selected) expected += '>';
                    expected += name;
                }
                check(list.DisplayConvolutionList() == expected,
                      "list count=" + std::to_string(count) +
                      " selected=" + std::to_string(selected));
            }
        }
    } else if (group == "dither") {
        check_quantum<float,8>(); check_quantum<float,16>();
        check_quantum<float,24>(); check_quantum<float,32>();
        check_quantum<double,8>(); check_quantum<double,16>();
        check_quantum<double,24>(); check_quantum<double,32>();
    } else if (group == "ooura") {
        for (DWORD length : {8u, 16u, 32u}) {
            fft_roundtrip(false, length, length / 2);
            fft_roundtrip(true, length, 1);
            fft_roundtrip(true, length, length / 2 - 1);
        }
    } else return 2;
    return failures ? 1 : 0;
}
"""


def body_after(source, marker):
    """Extract an existing brace-balanced body without rewriting statements."""
    start = source.index('{', source.index(marker))
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start + 1 : end - 1]


def generate(read):
    paths = read('convolution/channelpaths.cpp')
    replacements = {}
    for kind in ('in', 'out'):
        end = paths.index(kind + 'Channel.push_back')
        start = paths.rindex('float fchannel=0;', 0, end)
        replacements['INPUT_ROUTE' if kind == 'in' else 'OUTPUT_ROUTE'] = paths[start:end]
    convolution = read('convolution/convolution.cpp')
    marker = 'const std::basic_string<TCHAR> ConvolutionList<T>::DisplayConvolutionList() const'
    replacements['DISPLAY'] = 'template<typename T>\n' + marker + ' {\n' + body_after(convolution, marker) + '\n}\n'
    dither = read('convolution/dither.h')
    constants = []
    for cls, fields in (('SimpleDither', ('Q', 'Qover2')), ('NoNoiseShape', ('Q_', 'Qover2_', 'q_'))):
        for field in fields:
            match = re.search(r'const T\s+' + cls + r'<[^;]+>::' + field + r'\s*=.*?;', dither)
            if not match:
                raise ValueError(f'missing {cls}::{field}')
            start = dither.rindex('template <', 0, match.start())
            constants.append(dither[start : match.end()])
    replacements['CONSTANTS'] = '\n'.join(constants)
    filters = read('convolution/filter.cpp')
    replacements['FULL_FFT'] = body_after(filters, 'if (nFrame % nHalfPartitionLength_ == 0)')
    replacements['TAIL_FFT'] = body_after(filters, 'if (nFrame % nHalfPartitionLength_ != 0)')
    result = HARNESS
    for key, value in replacements.items():
        result = result.replace('@' + key + '@', value)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        '--source-root',
        type=Path,
        default=Path(__file__).resolve().parents[2],
        help='repository root containing historical/',
    )
    parser.add_argument('--revision', help='read production sources from this Git revision')
    args = parser.parse_args()

    def read(name):
        if args.revision:
            # The historical tree was called convolver/ before 21 October 2026.
            for folder in ('historical', 'convolver'):
                shown = subprocess.run(
                    ['git', 'show', args.revision + ':' + folder + '/' + name],
                    cwd=args.source_root,
                    capture_output=True,
                    check=False,
                )
                if shown.returncode == 0:
                    return shown.stdout.decode('latin1').replace('\r\n', '\n')
            raise SystemExit('Neither historical/ nor convolver/ has ' + name + ' at ' + args.revision)
        return (args.source_root / 'historical' / name).read_text(encoding='latin1')

    with tempfile.TemporaryDirectory(prefix='convolver-branch-') as directory:
        scratch = Path(directory)
        cpp, binary = scratch / 'branch.cpp', scratch / 'branch'
        cpp.write_text(generate(read))
        header = read('fft/fftsg_h.h')
        # Serial FFT arithmetic stays verbatim; the shipped header otherwise
        # unconditionally enables Windows threads and includes windows.h.
        header, count = re.subn(r'^#define USE_CDFT_WINTHREADS[^\n]*', '', header, flags=re.MULTILINE)
        if count != 1:
            raise ValueError('expected one Windows threading definition')
        (scratch / 'fftsg_h.h').write_text(header)
        fft = scratch / 'fftsg.cpp'
        fft.write_text(read('fft/fftsg.cpp'))
        subprocess.run(
            [
                os.environ.get('CXX', 'clang++'),
                '-std=c++11',
                '-O1',
                '-g',
                '-fsanitize=address,undefined',
                '-fno-sanitize-recover=all',
                '-fno-omit-frame-pointer',
                str(cpp),
                str(fft),
                '-o',
                str(binary),
            ],
            check=True,
        )
        failed = False
        for group in ('routes', 'display', 'dither', 'ooura'):
            result = subprocess.run([str(binary), group], capture_output=True, text=True, check=False)
            print(f'{"PASS" if result.returncode == 0 else "FAIL"}: {group}')
            if result.returncode:
                failed = True
                print(result.stdout + result.stderr, end='')
        return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
