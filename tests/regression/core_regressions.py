#!/usr/bin/env python3
"""Compile original DSP bodies on macOS using a small platform/DFT adapter.

No Windows host or FFTW binary is exercised. DFT normalization follows FFTW;
the oracle is independent, double-precision time-domain linear convolution.
--revision reads tracked files at a Git revision for reproducible comparisons.
Requires the review branch delay-line API (not the original 4.3 release API).
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def generate(revision):
    def read(name):
        if revision:
            return subprocess.check_output(['git', 'show', revision + ':convolver/convolution/' + name], cwd=ROOT).decode('latin1').replace('\r\n','\n')
        return (ROOT / 'convolver/convolution' / name).read_text(encoding='latin1')

    buf = read('samplebuffer.h')
    aligned = buf[buf.index('template <class T>'):buf.index('// comparisons')]
    fast = buf[buf.index('template <typename T> \nstruct FastArray'):buf.index('// The following is unused')]
    # C++ dependent-base lookup and two unused legacy accessors need syntax repair.
    fast = fast.replace('struct FastArray : AlignedArray<T>\n{', 'struct FastArray : AlignedArray<T>\n{\nusing AlignedArray<T>::first_; using AlignedArray<T>::size_; using AlignedArray<T>::bsize_;')
    fast = fast.replace('rangecheck(i);', 'rangecheck(n);').replace('static void rangecheck', 'void rangecheck')
    fast = fast.replace('for(pointer p = first_; p != last)', 'for(pointer p = first_; p != last;)')
    buffers = aligned + fast + '''
using ChannelBuffer = FastArray<float>;
using SampleBuffer = std::vector<ChannelBuffer>;
using PartitionedBuffer = std::vector<SampleBuffer>;
float* c_ptr(const SampleBuffer& b, size_t r=0) { return b[r].c_ptr(); }
float* c_ptr(const PartitionedBuffer& b,size_t r=0,size_t c=0) { return b[r][c].c_ptr(); }
void Zero(ChannelBuffer& x) { x=0; }
void Zero(SampleBuffer& x) { for(auto& a:x) Zero(a); }
void Zero(PartitionedBuffer& x) { for(auto& a:x) Zero(a); }
'''
    ffthelp = read('ffthelp.cpp')
    table = re.search(r'const DWORD OptimalDFT::OptimalDFTSize\[\].*?\};', ffthelp, re.S).group()
    filter_header = re.sub(r'^(#include.*|#pragma once)$', '', read('filter.h'), flags=re.M)
    filter_body = read('filter.cpp')
    filter_body = filter_body[filter_body.index('Filter::Filter'):]
    conv_header = read('convolution.h')
    conv_header = conv_header[conv_header.index('template <typename T>\nclass Convolution'):conv_header.index('template <typename T>\nclass ConvolutionList')]
    # Expose the multiplication kernels for a direct numerical-conditioning probe.
    conv_header = conv_header.replace('private:', 'public:')
    conv_body = read('convolution.cpp')
    conv_body = conv_body[conv_body.index('// Convolution Constructor'):conv_body.index('#elif !(defined(__ICC) || defined(__INTEL_COMPILER))')]
    # The final selected section opens #ifdef FFTW.
    conv_body += '\n#endif\n'
    shim = (HERE / 'core_adapter.hpp').read_text()
    sample = read('sample.h')
    start = sample.index('\tstatic void StoreDelayed(')
    end = sample.index('\nprotected:', start)
    shim = shim.replace('// INSERT STORE DELAYED', sample[start:end])
    before, after = shim.split('// INSERT BUFFERS')
    middle, mixer = after.split('// INSERT FILTER')
    return '\n'.join([before, buffers, middle, table, filter_header, filter_body, mixer, conv_header, conv_body, (HERE / 'core_scenarios.cpp').read_text()])


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--revision')
    p.add_argument('--case', default='all')
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='convolver-core-') as temp:
        source = Path(temp) / 'core.cpp'
        source.write_text(generate(args.revision))
        binary = Path(temp) / 'core'
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1', '-g', '-fms-extensions',
                        '-Wno-unknown-pragmas', '-Wno-ignored-attributes',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                        str(source), '-o', str(binary)], check=True)
        return subprocess.run([str(binary), args.case]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
