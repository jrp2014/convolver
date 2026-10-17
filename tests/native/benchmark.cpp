// SPDX-License-Identifier: GPL-2.0-or-later
#include "convolver/engine.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using Clock=std::chrono::steady_clock;
struct Case {const char* name;std::size_t block,ir,channels;bool matrix;};
constexpr Case cases[]={
    {"mono_short",64,256,1,false}, {"mono_room",256,4096,1,false},
    {"stereo_low_latency",128,16384,2,false}, {"stereo_long",256,65536,2,false},
    {"matrix_short",128,4096,2,true}, {"matrix_long",256,65536,2,true},
    {"stereo_large_block",1024,65536,2,false}, {"stereo_odd_size",192,8192,2,false}
};
int main(int argc,char** argv) {
    try {
        if(argc!=3) throw std::invalid_argument("usage: convolver_benchmark CASE SECONDS (CASE=0..7)");
        const auto index=std::stoul(argv[1]); const auto seconds=std::stod(argv[2]);
        if(index>=std::size(cases) || !std::isfinite(seconds) || seconds<=0 || seconds>60)
            throw std::invalid_argument("invalid case or duration");
        const auto c=cases[index];
        convolver::Configuration config{48000,c.channels,c.channels,0,{},{},{}};
        std::uint32_t state=12345;
        auto random=[&] {state=1664525*state+1013904223;return (float(state>>8)/16777216.f-.5f);};
        for(std::size_t in=0;in<c.channels;++in) for(std::size_t out=0;out<c.channels;++out) {
            if(!c.matrix && in!=out) continue;
            convolver::FilterPath path;path.inputs={{in,1}};path.outputs={{out,1}};path.impulse.resize(c.ir);
            for(auto& x:path.impulse)x=random()*.02f;
            config.paths.push_back(std::move(path));
        }
        std::vector<float> input(c.block*c.channels),output(input.size());
        for(auto& x:input)x=random();
        const auto setup_start=Clock::now();
        convolver::Engine engine(config,c.block);
        const double setup_ms=std::chrono::duration<double,std::milli>(Clock::now()-setup_start).count();
        for(std::size_t i=0;i<c.ir/c.block+16;++i)engine.process(input,output,c.block);
        std::size_t blocks=0;double checksum=0;
        const auto start=Clock::now();double elapsed;
        do {
            for(int batch=0;batch<64;++batch)engine.process(input,output,c.block);
            blocks+=64;checksum+=output[0];
            elapsed=std::chrono::duration<double>(Clock::now()-start).count();
        } while(elapsed<seconds);
        // Individual-block timing is separate from throughput measurement.
        std::vector<double> latency(512);
        for(auto& us:latency) {
            const auto before=Clock::now();engine.process(input,output,c.block);
            us=std::chrono::duration<double,std::micro>(Clock::now()-before).count();
        }
        std::sort(latency.begin(),latency.end());
        if(!std::isfinite(checksum))throw std::runtime_error("nonfinite benchmark output");
        std::cout<<"backend,case,block,ir,channels,paths,setup_ms,ns_per_frame,p99_block_us,frames,checksum\n"
                 <<std::setprecision(10)<<convolver::fft_backend_name()<<','<<c.name<<','<<c.block<<','<<c.ir<<','<<c.channels<<','
                 <<config.paths.size()<<','<<setup_ms<<','<<elapsed*1e9/double(blocks*c.block)<<','<<latency[506]<<','
                 <<blocks*c.block<<','<<checksum<<'\n';
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
