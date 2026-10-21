// SPDX-License-Identifier: GPL-2.0-or-later
// Independent linear convolution oracle for the extracted production routines.
static int checks=0;
static double worst_error=0;
static void compare(const std::vector<std::vector<float>>& actual,
                    const std::vector<std::vector<double>>& expected,const std::string& label) {
    double error=0;
    for(size_t c=0;c<actual.size();++c) for(size_t i=0;i<actual[c].size();++i) {
        if(!std::isfinite(actual[c][i]) || !std::isfinite(expected[c][i]))
            throw std::runtime_error(label+": non-finite sample");
        error=std::max(error,std::abs(double(actual[c][i])-expected[c][i]));
    }
    worst_error=std::max(error,worst_error); ++checks;
    if(error>3e-5) throw std::runtime_error(label+": max absolute error="+std::to_string(error));
}
static void run(DWORD parts,DWORD length,DWORD delay,bool mixed,bool impulse,bool plain=false,bool unity=false) {
    input_channels=mixed?2:1; output_channels=mixed?3:1;
    input_delays.assign(input_channels,delay); output_delays.assign(output_channels,delay);
    if(mixed) { input_delays[0]=0; output_delays[0]=0; }
    impulse_responses.assign(mixed?2:1,std::vector<float>(length));
    std::mt19937 rng(387+length); std::uniform_real_distribution<float> random(-0.25,0.25);
    for(auto& h:impulse_responses) for(auto& v:h) v=impulse?0:random(rng);
    if(impulse) for(auto& h:impulse_responses) h[length>17?17:length-1]=1;
    // Scale 0.5 avoids the separately tested legacy copy overread in baseline DSP cases.
    path_fixtures={PathFixture{0,{{0,0.5f}},{{0,1.0f}}}};
    if(unity) path_fixtures[0].inputs[0].fScale=1;
    if(mixed) {
        path_fixtures[0].inputs.push_back({1,-0.25f});
        path_fixtures[0].outputs.push_back({1,0.75f});
        path_fixtures.push_back({1,{{1,0.25f}},{{0,-0.5f}}});
    }
    Convolution<float> conv("fixture",parts,0);
    const DWORD B=conv.Mixer.nHalfPartitionLength();
    if(delay>=B) throw std::runtime_error("invalid test delay");
    const size_t input_length=length*3+7,total=input_length+length+3*B+2*delay;
    std::vector<std::vector<float>> in(input_channels,std::vector<float>(total)),out(output_channels,std::vector<float>(total));
    for(auto& x:in) {
        for(size_t i=0;i<input_length;++i) x[i]=impulse?0:random(rng);
        if(impulse) x[3]=1;
    }
    std::vector<std::vector<double>> expected(output_channels,std::vector<double>(total));
    for(const auto& p:path_fixtures) for(const auto& ic:p.inputs) for(const auto& oc:p.outputs)
        for(size_t i=0;i<input_length;++i) for(size_t j=0;j<length;++j) {
            size_t k=i+j+B+input_delays[ic.nChannel]+output_delays[oc.nChannel];
            if(k<total) expected[oc.nChannel][k]+=double(in[ic.nChannel][i])*impulse_responses[p.ir][j]*ic.fScale*oc.fScale;
        }
    const size_t chunks[]={1,3,19,2,7,61};
    for(size_t pos=0,c=0;pos<total;++c) {
        size_t n=std::min(chunks[c%6],total-pos); SampleIO<float> io(in,out,pos);
        DWORD bytes=plain?conv.doConvolution(io,n,0):conv.doPartitionedConvolution(io,n,0);
        if(bytes!=n*output_channels*sizeof(float)) throw std::runtime_error("incorrect output byte count");
        pos+=n;
    }
    compare(out,expected,"P="+std::to_string(parts)+" L="+std::to_string(length)+" d="+std::to_string(delay));
    conv.Flush();
    for(auto& x:in) std::fill(x.begin(),x.end(),0);
    SampleIO<float> silence(in,out,0);
    if(plain) conv.doConvolution(silence,total,0); else conv.doPartitionedConvolution(silence,total,0);
    std::vector<std::vector<double>> zeros(output_channels,std::vector<double>(total));
    compare(out,zeros,"Flush leaves silence");
}
int main(int argc,char** argv) {
    const std::string mode=argc>1?argv[1]:"all";
    try {
        if(mode=="multiply" || mode=="all") {
            impulse_responses={{1}}; path_fixtures={{0,{{0,1}},{{0,1}}}};
            input_delays={0};output_delays={0};
            Convolution<float> conv("fixture",1,0);
            fftwf_complex a[2]={{10000,0.0001f},{10000,0.0001f}};
            fftwf_complex b[2]={{10000,0.0001f},{10000,0.0001f}};
            fftwf_complex c[2]={{0,0},{0,0}};
            conv.complex_mul(a,b,c,2);
            if(!std::isfinite(c[0][1]) || std::abs(c[0][1]-2.0f)>1e-6f) throw std::runtime_error("complex multiply loses imaginary component: "+std::to_string(c[0][1]));
            conv.complex_mul_add(a,b,c,2);
            if(!std::isfinite(c[0][1]) || std::abs(c[0][1]-4.0f)>1e-6f) throw std::runtime_error("complex multiply-add loses imaginary component");
            checks+=2;
        }
        if(mode=="copy" || mode=="all") {
            ChannelBuffer source(8),destination(10); source=0.75f; destination=source;
            for(size_t i=0;i<8;++i) if(destination[i]!=0.75f) throw std::runtime_error("copy changed samples");
            ++checks;
        }
        if(mode=="empty" || mode=="all") {
            impulse_responses={{}}; bool rejected=false;
            try {Filter empty("0",1,0,48000,0);} catch(const filterException&) {rejected=true;}
            if(!rejected) throw std::runtime_error("empty IR accepted"); ++checks;
        }
        if(mode=="padding" || mode=="all") {run(2,34,0,false,true);run(3,51,0,false,true);}
        if(mode=="delay" || mode=="all") {
            for(DWORD d=0;d<4;++d) {run(1,4,d,false,true);run(2,8,d,false,true);}
        }
        if(mode=="unity" || mode=="all") {run(2,34,0,false,false,false,true);run(1,17,0,false,false,true,true);}
        if(mode=="control") {run(1,16,0,false,false,true);run(2,32,0,false,false);}
        if(mode=="matrix" || mode=="all") {
            for(DWORD length:{1u,2u,3u,4u,7u,16u,17u,31u,34u,51u,64u,65u,127u})
                for(DWORD parts:{1u,2u,3u,4u,8u}) {
                    run(parts,length,0,false,false);
                    run(parts,length,0,true,false);
                    run(parts,length,1,true,false);
                    if(parts==1) run(parts,length,0,false,false,true);
                }
        }
        std::cout<<"PASS "<<checks<<" checks; worst absolute error "<<worst_error<<"\n";
        return 0;
    } catch(const std::exception& e) {std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}
}
