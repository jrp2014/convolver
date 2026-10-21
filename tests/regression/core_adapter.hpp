// SPDX-License-Identifier: GPL-2.0-or-later
// Test-only substitutes for Windows, file input and FFTW. GPL-2.0-or-later.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <iterator>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#define FFTW 1
#define LIBSNDFILE 1
#define RESTRICT
#define MAX_PATH 260
#define __single_inheritance
#define ALIGNED_MALLOC(n) std::malloc(n)
#define ALIGNED_FREE(p) std::free(p)
#define CopyMemory(d,s,n) std::memcpy(d,s,n)
#define ZeroMemory(d,n) std::memset(d,0,n)
using DWORD=uint32_t; using WORD=uint16_t; using TCHAR=char; using HRESULT=int;
using BaseT=float;
struct filterException : std::runtime_error { filterException(const char* s,const char*) : std::runtime_error(s) {} };
struct convolutionException : std::runtime_error { using std::runtime_error::runtime_error; };
// INSERT BUFFERS
using fftwf_complex = float[2];
struct FFTPlan { int n; };
using fftwf_plan = FFTPlan*;
void fftwf_plan_with_nthreads(int) {}
void fftwf_set_timelimit(double) {}
fftwf_plan fftwf_plan_dft_r2c_1d(int n,float*,fftwf_complex*,unsigned) { return new FFTPlan{n}; }
fftwf_plan fftwf_plan_dft_c2r_1d(int n,fftwf_complex*,float*,unsigned) { return new FFTPlan{n}; }
void fftwf_destroy_plan(fftwf_plan p) { delete p; }
void fftwf_execute_dft_r2c(fftwf_plan p,float* input,fftwf_complex* output) {
    std::vector<float> in(input,input+p->n);
    const double pi=std::acos(-1.0);
    for(int k=0;k<=p->n/2;++k) {
        std::complex<double> sum=0;
        for(int j=0;j<p->n;++j) sum+=double(in[j])*std::polar(1.0,-2*pi*k*j/p->n);
        output[k][0]=float(sum.real()); output[k][1]=float(sum.imag());
    }
}
void fftwf_execute_dft_c2r(fftwf_plan p,fftwf_complex* input,float* output) {
    std::vector<std::complex<double>> in(p->n);
    for(int k=0;k<=p->n/2;++k) in[k]={input[k][0],input[k][1]};
    for(int k=p->n/2+1;k<p->n;++k) in[k]=std::conj(in[p->n-k]);
    const double pi=std::acos(-1.0);
    for(int j=0;j<p->n;++j) {
        std::complex<double> sum=0;
        for(int k=0;k<p->n;++k) sum+=in[k]*std::polar(1.0,2*pi*k*j/p->n);
        output[j]=float(sum.real()); // FFTW backward transform is unnormalised.
    }
}
struct SF_INFO { int64_t frames; int samplerate; int channels; };
constexpr int SFM_READ=0;
static std::vector<std::vector<float>> impulse_responses;
struct CWaveFileHandle {
    size_t pos=0,index;
    CWaveFileHandle(const char* name,int,SF_INFO* info,DWORD rate):index(std::stoul(name)) {
        *info={int64_t(impulse_responses.at(index).size()),int(rate),1};
    }
    int readf_float(float* x,int) {
        if(pos==impulse_responses[index].size()) return 0;
        *x=impulse_responses[index][pos++]; return 1;
    }
};
struct OptimalDFT {
    static constexpr DWORD HalfLargestDFTSize=1000000;
    static const DWORD OptimalDFTSize[];
    static DWORD GetOptimalDFTSize(DWORD n) {
        const DWORD* p=OptimalDFTSize; while(*p<n) ++p; return *p;
    }
};
struct PlanningRigour { enum { Measure=1,TimeLimited=4,nTimeLimit=30 }; static constexpr unsigned Flag[5]={0,0,0,0,0}; };
// INSERT FILTER
struct Route { WORD nChannel; float fScale; };
struct PathFixture { size_t ir; std::vector<Route> inputs,outputs; };
static std::vector<PathFixture> path_fixtures;
static DWORD input_channels=1,output_channels=1;
static std::vector<DWORD> input_delays,output_delays;
struct ChannelPaths {
    struct ChannelPath {
        using size_type=size_t;
        Filter filter;
        std::vector<Route> inChannel,outChannel;
        ChannelPath(const PathFixture& p,DWORD n,unsigned rigor):
            filter(std::to_string(p.ir).c_str(),n,0,48000,rigor),inChannel(p.inputs),outChannel(p.outputs) {}
    };
    struct PathsHolder {
        std::vector<std::unique_ptr<ChannelPath>> p;
        const ChannelPath& operator[](size_t i) const {return *p[i];}
    } paths;
    ChannelPaths(const char*,DWORD n,unsigned rigor) {
        for(const auto& p:path_fixtures) paths.p.push_back(std::make_unique<ChannelPath>(p,n,rigor));
    }
    DWORD nInputChannels() const {return input_channels;}
    DWORD nOutputChannels() const {return output_channels;}
    DWORD nPaths() const {return paths.p.size();}
    DWORD nHalfPartitionLength() const {return paths[0].filter.nHalfPartitionLength();}
    DWORD nPartitionLength() const {return paths[0].filter.nPartitionLength();}
    DWORD nFFTWPartitionLength() const {return paths[0].filter.nFFTWPartitionLength();}
    DWORD nFilterLength() const {return paths[0].filter.nFilterLength();}
    const std::vector<DWORD>& nInputSamplesDelay() const {return input_delays;}
    const std::vector<DWORD>& nOutputSamplesDelay() const {return output_delays;}
    const PathsHolder& Paths() const {return paths;}
};
template<class T> struct ConvertSample { unsigned nContainerSize() const {return sizeof(T);} };
template<class T> struct SampleIO {
    const std::vector<std::vector<float>>& in;
    std::vector<std::vector<float>>& out;
    size_t pos;
    // INSERT STORE DELAYED
    SampleIO(const std::vector<std::vector<float>>& i,std::vector<std::vector<float>>& o,size_t start):in(i),out(o),pos(start) {}
    void TransferFrame(const ChannelPaths& m,SampleBuffer& inputs,SampleBuffer& outputs,float gain,DWORD idx,DWORD& ni,DWORD& no,SampleBuffer& delayline,DWORD delayidx) {
        // Actual production StoreDelayed below; this fixture only supplies sample I/O.
        for(DWORD c=0;c<m.nOutputChannels();++c) {
            int j=int(idx)-int(m.nOutputSamplesDelay()[c]); if(j<0) j+=m.nPartitionLength();
            out[c][pos]=outputs[c][j]; no+=sizeof(T);
        }
        for(DWORD c=0;c<m.nInputChannels();++c) {
            StoreDelayed(m,inputs,delayline,c,idx,delayidx,in[c][pos]*gain); ni+=sizeof(T);
        }
        ++pos;
    }
};
