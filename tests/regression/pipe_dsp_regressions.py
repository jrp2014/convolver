#!/usr/bin/env python3
"""Check recovered pipe EOF length against the real extracted convolution engine.

Uses the portable FFTW-compatible DFT adapter from core_regressions; this tests
DSP/tail agreement, not Windows stdin or libsndfile codecs.
"""
import os
from pathlib import Path
import subprocess
import tempfile
import sys
sys.dont_write_bytecode = True
import core_regressions

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]

HARNESS = r'''
static int checks=0;
static double worst_error=0;
static void run(DWORD parts, DWORD taps, DWORD count, bool plain) {
    input_channels=2; output_channels=3;
    input_delays={0,1}; output_delays={1,0,1};
    impulse_responses.assign(2,std::vector<float>(taps));
    std::mt19937 rng(123+taps+parts+count);
    std::uniform_real_distribution<float> random(-0.25,0.25);
    for(auto& ir:impulse_responses) for(auto& x:ir) x=random(rng);
    // Last sample and last tap are nonzero, exposing premature EOF truncation.
    path_fixtures={{0,{{0,1},{1,-0.5f}},{{0,1},{1,0.5f}}},
                   {1,{{1,0.75f}},{{2,1}}}};
    Convolution<float> conv("fixture",parts,0);
    const size_t B=conv.Mixer.nHalfPartitionLength();
    const size_t tail=TailFrames(conv.Mixer);
    if(tail!=B+taps-1+2) throw std::runtime_error("wrong tail length");
    const size_t emitted=count?count+tail:0;
    const size_t total=emitted+2*B;
    std::vector<std::vector<float>> in(2,std::vector<float>(total));
    std::vector<std::vector<float>> out(3,std::vector<float>(total));
    for(auto& channel:in) for(size_t n=0;n<count;++n) channel[n]=random(rng);
    std::vector<std::vector<double>> expected(3,std::vector<double>(total));
    for(const auto& p:path_fixtures) for(const auto& ic:p.inputs) for(const auto& oc:p.outputs)
        for(size_t i=0;i<count;++i) for(size_t j=0;j<taps;++j) {
            const size_t at=i+j+B+input_delays[ic.nChannel]+output_delays[oc.nChannel];
            expected[oc.nChannel][at]+=double(in[ic.nChannel][i])*impulse_responses[p.ir][j]*ic.fScale*oc.fScale;
        }
    // Stop input exactly at EOF, drain exactly TailFrames, then check an extra
    // two blocks independently: there must be no omitted convolution samples.
    const size_t boundaries[]={count,emitted,total};
    size_t position=0;
    for(size_t end:boundaries) while(position<end) {
        const size_t chunk=std::min(size_t(7),end-position);
        SampleIO<float> io(in,out,position);
        if(plain) conv.doConvolution(io,chunk,0); else conv.doPartitionedConvolution(io,chunk,0);
        position+=chunk;
    }
    for(size_t c=0;c<3;++c) for(size_t n=0;n<total;++n) {
        if(!std::isfinite(out[c][n])) throw std::runtime_error("nonfinite output");
        const double err=std::abs(double(out[c][n])-expected[c][n]);
        worst_error=std::max(worst_error,err);
        if(err>3e-5) throw std::runtime_error("convolution mismatch");
        if(n>=emitted && std::abs(out[c][n])>3e-5) throw std::runtime_error("omitted tail");
    }
    ++checks;
}
int main() {
    try {
        for(DWORD taps:{1u,4u,17u,34u,51u}) for(DWORD parts:{1u,2u,3u,8u})
            for(DWORD count:{0u,1u,7u,28u}) {
                run(parts,taps,count,false);
                if(parts==1) run(parts,taps,count,true);
            }
        std::cout<<"PASS "<<checks<<" pipe/DSP EOF cases; worst absolute error "<<worst_error<<'\n';
        return 0;
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 1;}
}
'''


def main():
    code = core_regressions.generate(None).split('// Independent linear convolution oracle')[0]
    code += '\nusing sf_count_t = int64_t;\n#define TEXT(x) x\nconst DWORD MAX_ATTENUATION_DB=1000;\n'
    code += (ROOT / 'convolver/convolverPipe/pipe_helpers.h').read_text().replace('#pragma once', '')
    code += HARNESS
    with tempfile.TemporaryDirectory(prefix='convolver-pipe-dsp-') as tmp:
        source = Path(tmp) / 'test.cpp'; executable = Path(tmp) / 'test'
        source.write_text(code)
        subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++17', '-O1', '-g', '-fms-extensions',
                        '-Wno-unknown-pragmas', '-Wno-ignored-attributes',
                        '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                        str(source), '-o', str(executable)], check=True)
        return subprocess.run([str(executable)]).returncode


if __name__ == '__main__':
    raise SystemExit(main())
