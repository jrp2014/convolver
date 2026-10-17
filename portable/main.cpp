// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include "convolver/io.hpp"
#include <fftw3.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <csignal>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <io.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
constexpr auto help = R"(Convolver portable CLI
Usage: convolver --filter IR_OR_CFG --input FILE --output FILE [options]
  --block-size N       Partition payload in frames (default 256)
  --gain-db DB         Output gain in decibels (default 0)
  --keep-latency       Keep the leading block of algorithmic latency
  --output-format F   wav-float (default), wav-pcm16, wav-pcm24, wav-pcm32,
                      raw-float (little-endian IEEE float32)
  --raw-input RATE CHANNELS
                      Read little-endian IEEE float32 with explicit metadata
  --help              Show this help
  --version           Show the linked FFTW and libsndfile versions
FILE may be '-' for stdin/stdout; standard streams require RAW float format.
Files retain the entire FIR/routing tail. Algorithmic latency is trimmed unless
--keep-latency is used. Sample-rate/channel mismatches are errors; no resampling.
)";
std::size_t count(std::string_view s) {
    std::size_t n=0;
    const auto [end,error]=std::from_chars(s.data(),s.data()+s.size(),n);
    if(error!=std::errc{} || end!=s.data()+s.size() || n==0)
        throw std::invalid_argument("expected a positive integer: "+std::string(s));
    return n;
}
std::filesystem::path utf8_path(const std::string& text) {
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
bool same_file(const std::filesystem::path& a, const std::filesystem::path& b) {
    if(a=="-" || b=="-") return false;
    std::error_code ec;
    if(std::filesystem::equivalent(a,b,ec) && !ec) return true;
    return std::filesystem::weakly_canonical(a)==std::filesystem::weakly_canonical(b);
}
bool same_stdin(const std::filesystem::path& output) {
    if(output=="-") return false; // Shell redirection has already opened stdout.
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION input_info{}, output_info{};
    const auto input=reinterpret_cast<HANDLE>(_get_osfhandle(0));
    if(!GetFileInformationByHandle(input,&input_info)) return false;
    const auto target=CreateFileW(output.c_str(),FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    if(target==INVALID_HANDLE_VALUE) return false;
    const bool valid=GetFileInformationByHandle(target,&output_info)!=0;
    CloseHandle(target);
    return valid && input_info.dwVolumeSerialNumber==output_info.dwVolumeSerialNumber &&
        input_info.nFileIndexHigh==output_info.nFileIndexHigh &&
        input_info.nFileIndexLow==output_info.nFileIndexLow;
#else
    struct stat input_info{}, output_info{};
    return fstat(STDIN_FILENO,&input_info)==0 && S_ISREG(input_info.st_mode) &&
        stat(output.c_str(),&output_info)==0 && input_info.st_dev==output_info.st_dev &&
        input_info.st_ino==output_info.st_ino;
#endif
}
}
int run(int argc, char** argv) {
    try {
#ifdef SIGPIPE
        std::signal(SIGPIPE,SIG_IGN); // Surface closed consumers as I/O failures.
#endif
        std::filesystem::path filter, input, output;
        std::optional<convolver::RawSpec> raw;
        convolver::OutputFormat format=convolver::OutputFormat::wav_float;
        std::size_t block=256;
        float gain=1;
        bool keep=false;
        for(int i=1;i<argc;++i) {
            std::string option=argv[i];
            auto value=[&]() -> std::string {
                if(++i>=argc) throw std::invalid_argument("missing value for "+option);
                return argv[i];
            };
            if(option=="--help") { std::cout<<help; return 0; }
            if(option=="--version") {
                std::cout<<"Convolver portable\nFFTW "<<fftwf_version<<'\n'
                         <<convolver::sndfile_version()<<'\n'; return 0;
            }
            if(option=="--filter") filter=utf8_path(value());
            else if(option=="--input") input=utf8_path(value());
            else if(option=="--output") output=utf8_path(value());
            else if(option=="--block-size") block=count(value());
            else if(option=="--keep-latency") keep=true;
            else if(option=="--raw-input") {
                const auto rate=count(value()); const auto channels=count(value());
                if(rate>768000 || channels>256) throw std::invalid_argument("RAW metadata exceeds supported limits");
                raw=convolver::RawSpec{static_cast<int>(rate),channels};
            } else if(option=="--gain-db") {
                auto text=value(); std::size_t end=0;
                const double db=std::stod(text,&end);
                const double linear=std::pow(10.,db/20.);
                if(end!=text.size() || !std::isfinite(db) || !std::isfinite(linear) ||
                   linear>std::numeric_limits<float>::max())
                    throw std::invalid_argument("gain must be finite and representable");
                gain=static_cast<float>(linear);
            } else if(option=="--output-format") {
                const auto name=value();
                if(name=="wav-float") format=convolver::OutputFormat::wav_float;
                else if(name=="wav-pcm16") format=convolver::OutputFormat::wav_pcm16;
                else if(name=="wav-pcm24") format=convolver::OutputFormat::wav_pcm24;
                else if(name=="wav-pcm32") format=convolver::OutputFormat::wav_pcm32;
                else if(name=="raw-float") format=convolver::OutputFormat::raw_float;
                else throw std::invalid_argument("unsupported output format: "+name);
            } else throw std::invalid_argument("unknown option: "+option);
        }
        if(filter.empty() || input.empty() || output.empty())
            throw std::invalid_argument("--filter, --input and --output are required (see --help)");
        if(same_file(input,output) || same_file(filter,output) || (input=="-" && same_stdin(output)))
            throw std::invalid_argument("output must differ from input and filter");
        convolver::AudioReader reader(input,raw);
        const auto info=reader.info();
        std::vector<std::filesystem::path> dependencies;
        const auto config=convolver::load_configuration(filter,info.sample_rate,info.channels,&dependencies);
        for(const auto& source:dependencies)
            if(same_file(source,output))
                throw std::invalid_argument("output must differ from referenced configuration and impulse files");
        convolver::Engine engine(config,block);
        convolver::AudioWriter writer(output,{info.sample_rate,engine.output_channels()},format);
        constexpr std::size_t chunk=4096;
        std::vector<float> in(chunk*engine.input_channels()), out(chunk*engine.output_channels());
        std::size_t skip=keep?0:engine.latency_frames();
        auto emit=[&](std::size_t frames) {
            const auto drop=std::min(skip,frames); skip-=drop;
            auto samples=std::span<float>(out).subspan(drop*engine.output_channels(),
                                                     (frames-drop)*engine.output_channels());
            for(float& v:samples) {
                v*=gain;
                if(!std::isfinite(v)) throw std::runtime_error("nonfinite output (input/filter/gain overflow)");
            }
            if(!samples.empty()) writer.write(samples);
        };
        bool any=false;
        while(const auto frames=reader.read(in)) {
            any=true;
            engine.process(std::span<const float>(in).first(frames*engine.input_channels()),out,frames);
            emit(frames);
        }
        if(any) {
            auto remaining=engine.latency_frames()+engine.tail_frames();
            while(remaining) {
                const auto n=std::min(chunk,remaining);
                engine.process({},out,n); emit(n); remaining-=n;
            }
        }
        reader.close(); writer.close();
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"convolver: "<<error.what()<<'\n';
        return 1;
    }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> arguments;
        std::vector<char*> pointers;
        arguments.reserve(argc); pointers.reserve(argc);
        for(int i=0;i<argc;++i) {
            const int size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,nullptr,0,nullptr,nullptr);
            if(!size) throw std::runtime_error("invalid Unicode command line");
            std::string text(static_cast<std::size_t>(size),'\0');
            if(!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,argv[i],-1,text.data(),size,nullptr,nullptr))
                throw std::runtime_error("cannot decode command line");
            text.pop_back(); arguments.push_back(std::move(text));
        }
        for(auto& text:arguments) pointers.push_back(text.data());
        return run(argc,pointers.data());
    } catch(const std::exception& error) {
        std::cerr<<"convolver: "<<error.what()<<'\n'; return 1;
    }
}
#else
int main(int argc, char** argv) { return run(argc,argv); }
#endif
