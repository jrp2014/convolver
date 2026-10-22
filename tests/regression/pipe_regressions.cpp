// SPDX-License-Identifier: GPL-2.0-or-later
// Portable adapters for the actual restored convolverPipe entry point.
// This tests CLI/stream control flow; the DSP kernel is independently covered
// by core_regressions.py and its direct-convolution oracle.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using DWORD = uint32_t;
using BYTE = unsigned char;
using TCHAR = wchar_t;
using _TCHAR = wchar_t;
using BaseT = float;
using sf_count_t = int64_t;
#define TEXT(x) L##x
#define ZeroMemory(p,n) std::memset(p,0,n)
constexpr DWORD MAX_ATTENUATION_DB = 1000;
constexpr int SFM_READ = 1, SFM_WRITE = 2, _O_BINARY = 32768;
struct SF_INFO { sf_count_t frames; int channels, samplerate, format; };
struct PlanningRigour { static const unsigned nDegrees = 3; static const char* Rigour[3]; };
const char* PlanningRigour::Rigour[] = {"Estimate", "Measure", "Patient"};
struct SndFileFormat {
    struct Name { const char* id; };
    static const unsigned nMajor = 2, nMinor = 1;
    static const Name Major[2], Minor[1];
    static int Lookup(const wchar_t* major, const wchar_t* minor) {
        if(std::wstring(minor) != L"FLOAT" || (std::wstring(major) != L"RAW" && std::wstring(major) != L"WAV"))
            throw std::invalid_argument("format");
        return std::wstring(major) == L"RAW" ? 1 : 2;
    }
};
const SndFileFormat::Name SndFileFormat::Major[] = {{"RAW"},{"WAV"}};
const SndFileFormat::Name SndFileFormat::Minor[] = {{"FLOAT"}};
struct convolutionException : std::runtime_error { explicit convolutionException(const char* s):std::runtime_error(s) {} };
struct Fixture {
    std::vector<float> source, written;
    size_t pos = 0;
    int maxRead = 1000000, readFailure = 0, writeFailure = 0, closeFailure = 0;
    bool rateMismatch = false, channelsMismatch = false, binaryFailure = false, engineFailure = false;
    unsigned binaryCalls = 0, constructions = 0, processes = 0, lastFrames = 0;
    unsigned partitions = 0, configurations = 1;
    DWORD padded = 8, block = 4;
    sf_count_t ir = 3;
    DWORD inputDelay = 1, outputDelay = 2;
} fixture;
static int _fileno(FILE* f) { return f == stdin ? 0 : 1; }
static int _setmode(int, int mode) { assert(mode == _O_BINARY); ++fixture.binaryCalls; return fixture.binaryFailure ? -1 : 0; }
struct ChannelPaths {
    struct Filter { SF_INFO info; const SF_INFO& sf_FilterFormat() const {return info;} };
    struct ChannelPath {
        typedef size_t size_type;
        struct Channel { unsigned nChannel; };
        std::vector<Channel> inChannel, outChannel;
        Filter filter;
    };
    std::vector<ChannelPath> paths;
    std::vector<DWORD> inputs, outputs;
    ChannelPaths():inputs(1,fixture.inputDelay),outputs(1,fixture.outputDelay) {
        ChannelPath p; p.inChannel.push_back({0}); p.outChannel.push_back({0});
        p.filter.info.frames=fixture.ir; paths.push_back(p);
    }
    unsigned nInputChannels() const {return inputs.size();}
    unsigned nOutputChannels() const {return outputs.size();}
    unsigned nSamplesPerSec() const {return 48000;}
    unsigned nPaths() const {return paths.size();}
    DWORD nFilterLength() const {return fixture.padded;}
    DWORD nHalfPartitionLength() const {return fixture.block;}
    const std::vector<ChannelPath>& Paths() const {return paths;}
    const std::vector<DWORD>& nInputSamplesDelay() const {return inputs;}
    const std::vector<DWORD>& nOutputSamplesDelay() const {return outputs;}
};
template<class T> struct ConvertSample {virtual ~ConvertSample() {}};
template<class T> struct ConvertSample_ieeefloat : ConvertSample<T> {};
template<class T> struct Holder {std::unique_ptr<T> p; explicit Holder(T* v):p(v) {} T* get_ptr(){return p.get();}};
template<class T> struct InterleavedIO {
    float *in, *out;
    InterleavedIO(BYTE* i,BYTE* o,ConvertSample<T>*,ConvertSample<T>*):in(reinterpret_cast<float*>(i)),out(reinterpret_cast<float*>(o)) {}
};
template<class T> struct Engine {
    ChannelPaths Mixer;
    std::vector<float> history;
    DWORD process(InterleavedIO<T>& io,DWORD count) {
        ++fixture.processes; fixture.lastFrames=count;
        if(fixture.engineFailure) return 0;
        const size_t delay=fixture.block+fixture.inputDelay+fixture.outputDelay;
        for(DWORD i=0;i<count;++i) {
            history.push_back(io.in[i]);
            const size_t position=history.size()-1;
            float sample=0;
            for(sf_count_t k=0;k<fixture.ir;++k)
                if(position >= delay+size_t(k)) sample += history[position-delay-size_t(k)]*float(k+1);
            io.out[i]=sample;
        }
        return count*sizeof(float);
    }
    DWORD doConvolution(InterleavedIO<T>& io,DWORD count,float) {fixture.partitions=0;return process(io,count);}
    DWORD doPartitionedConvolution(InterleavedIO<T>& io,DWORD count,float) {return process(io,count);}
};
template<class T> struct ConvolutionList {
    Engine<T> engine;
    ConvolutionList(const TCHAR*,DWORD p,DWORD) {++fixture.constructions;fixture.partitions=p;}
    unsigned nConvolutionList() const {return fixture.configurations;}
    void selectConvolutionIndex(int) {}
    Engine<T>& SelectedConvolution() {return engine;}
    const char* DisplayConvolutionList() const {return "fixture";}
};
struct CWaveFileHandle {
    int mode;
    CWaveFileHandle(int fd,int m,SF_INFO* info):mode(m) {
        assert(fd==(m==SFM_READ?0:1));
        assert(info->format==1 || info->format==2);
        if(mode==SFM_READ) {
            if(fixture.rateMismatch) info->samplerate=44100;
            if(fixture.channelsMismatch) info->channels=2;
        }
    }
    sf_count_t readf_float(float* data,sf_count_t requested) {
        if(fixture.readFailure==1) return -1;
        if(fixture.readFailure==3) return requested+1;
        sf_count_t count=std::min<sf_count_t>(fixture.source.size()-fixture.pos,std::min<sf_count_t>(requested,fixture.maxRead));
        std::copy_n(fixture.source.begin()+fixture.pos,count,data);fixture.pos+=size_t(count); return count;
    }
    sf_count_t writef_float(float* data,sf_count_t count) {
        fixture.written.insert(fixture.written.end(),data,data+count);
        return fixture.writeFailure==1 ? count-1 : count;
    }
    int error() const {return mode==SFM_READ ? fixture.readFailure==2 : fixture.writeFailure==2;}
    int close() {return fixture.closeFailure==mode;}
};

// INSERT PRODUCTION ENTRY POINT

static int checks=0;
static void check(bool ok,const char* message) {++checks;if(!ok)throw std::runtime_error(message);}
static int run(unsigned arg=0,const std::wstring& replacement=L"",int argc=10) {
    std::vector<std::wstring> values={L"convolverPipe",L"0",L"0",L"0",L"1",L"RAW",L"FLOAT",L"RAW",L"FLOAT",L"filter.wav"};
    if(arg) values[arg]=replacement;
    std::vector<wchar_t*> args;for(auto& s:values)args.push_back(&s[0]);
    return _tmain(argc,args.data());
}
static void reset() {fixture=Fixture();fixture.source={1,2,3,4,5};}
int main() {
    std::wostringstream diagnostics;auto* old=std::wcerr.rdbuf(diagnostics.rdbuf());
    try {
        for(int argc: {0,1,9,11}) {reset();check(run(0,L"",argc)!=0,"argc");}
        for(unsigned arg: {1u,2u,4u}) for(const wchar_t* s: {L"",L"-1",L"1x",L"1.5",L"4294967296",L"9999999999999999999999",L"+1",L" 1"}) {
            reset();check(run(arg,s)!=0,"invalid integer");check(fixture.constructions==0,"invalid integer constructed engine");
        }
        for(const wchar_t* s: {L"",L"nan",L"inf",L"-inf",L"1x",L"1001",L"-1001",L"1000",L"1e999"}) {
            reset();check(run(3,s)!=0,"invalid attenuation");check(fixture.constructions==0,"invalid attenuation constructed engine");
        }
        for(const wchar_t* s: {L"-6",L"0",L"6",L"-1000"}) {reset();check(run(3,s)==0,"valid attenuation");}
        reset();check(run(2,L"3")!=0,"rigor range");
        reset();check(run(1,L"4097")!=0 && fixture.constructions==0,"partition guard");
        reset();check(run(4,L"0")!=0,"zero chunk count");
        for(unsigned arg: {5u,6u,7u,8u}) {reset();check(run(arg,L"invalid")!=0,"invalid format");}
        for(unsigned n: {0u,1u,3u,8u,16u,19u}) for(unsigned partitions: {0u,2u}) for(int chunk: {1,3,100}) {
            reset();fixture.source.assign(n,0);if(n)fixture.source[n-1]=1;fixture.maxRead=chunk;
            check(run(1,std::to_wstring(partitions))==0,"stream success");
            check(fixture.written.size()==(n?n+9:0),"exact output length including tail");
            check(fixture.binaryCalls==2,"binary stdio");
            if(n) {
                check(fixture.written[n+6]==1 && fixture.written[n+7]==2 && fixture.written[n+8]==3,"last impulse tail");
                check(fixture.written[0]==0,"leading latency");
            } else check(fixture.processes==0,"empty input engine call");
        }
        reset();check(run(4,L"4294967295")==0,"large batch is bounded");
        reset();fixture.rateMismatch=true;check(run()!=0 && fixture.processes==0,"sample rate mismatch");
        reset();fixture.channelsMismatch=true;check(run()!=0 && fixture.processes==0,"channel mismatch");
        reset();fixture.binaryFailure=true;check(run()!=0,"binary mode failure");
        reset();fixture.configurations=2;check(run()!=0,"multiple configurations");
        for(int fault: {1,2,3}) {reset();fixture.readFailure=fault;check(run()!=0,"read error");}
        for(int fault: {1,2}) {reset();fixture.writeFailure=fault;check(run()!=0,"write error");}
        for(int mode: {SFM_READ,SFM_WRITE}) {reset();fixture.closeFailure=mode;check(run()!=0,"close error");}
        reset();fixture.engineFailure=true;check(run()!=0,"engine output mismatch");
        reset();fixture.ir=0;check(run()!=0,"empty IR rejected");
        reset();fixture.ir=(std::numeric_limits<sf_count_t>::max)();check(run()!=0,"tail overflow rejected");
        reset();ChannelPaths mixer;mixer.inputs.push_back(900);mixer.outputs.push_back(800);
        check(TailFrames(mixer)==9,"unconnected delays excluded");
        auto path=mixer.paths[0];path.inChannel[0].nChannel=1;path.outChannel[0].nChannel=1;path.filter.info.frames=7;mixer.paths.push_back(path);
        check(TailFrames(mixer)==1710,"maximum connected tail");
        mixer.paths[1].outChannel.clear();check(TailFrames(mixer)==9,"disconnected path excluded");
        fixture.padded=0xffffffffu;check(ChunkFrames(mixer,0xffffffffu)==131072,"chunk overflow bounded");
        std::wcerr.rdbuf(old);std::cout<<"pipe regressions: "<<checks<<" checks passed\n";return 0;
    } catch(const std::exception& e) {std::wcerr.rdbuf(old);std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
