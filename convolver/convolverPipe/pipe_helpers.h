// Convolver pipe front end, recovered from the historical 4.4 project.
// Copyright (C) 2006 jrp; GNU GPL version 2 or, at your option, later.
// See the repository COPYING file. Safety and streaming repairs are local additions.
#pragma once

#include <limits>
#include <cmath>
#include <sstream>
#include <stdexcept>

// Decimal digits only: stream extraction into an unsigned integer accepts -1.
static DWORD ParseCount(const TCHAR* text, const char* name, bool allowZero)
{
    if(!text || !*text)
        throw std::range_error(name);
    DWORD value = 0;
    const DWORD maximum = (std::numeric_limits<DWORD>::max)();
    for(const TCHAR* p = text; *p; ++p)
    {
        if(*p < TEXT('0') || *p > TEXT('9'))
            throw std::range_error(name);
        const DWORD digit = *p - TEXT('0');
        if(value > (maximum - digit) / 10)
            throw std::range_error(name);
        value = value * 10 + digit;
    }
    if(!allowZero && value == 0)
        throw std::range_error(name);
    return value;
}

static float ParseAttenuation(const TCHAR* text)
{
    std::basic_istringstream<TCHAR> input(text);
    float value = 0;
    if(!(input >> value))
        throw std::range_error("Invalid attenuation");
    input >> std::ws;
    // Ordered comparisons also reject NaN and infinity without newer C++ APIs.
    const float bound = static_cast<float>(MAX_ATTENUATION_DB);
    if(!input.eof() || !(value >= -bound && value <= bound))
        throw std::range_error("Invalid attenuation");
    const float gain = powf(10.0f, value / 20.0f);
    if(!(gain >= 0 && gain <= (std::numeric_limits<float>::max)()))
        throw std::range_error("Attenuation produces an unrepresentable gain");
    return value;
}

static sf_count_t AddFrames(sf_count_t first, sf_count_t second)
{
    if(first < 0 || second < 0 || first > (std::numeric_limits<sf_count_t>::max)() - second)
        throw std::range_error("Frame count overflow");
    return first + second;
}

// The engine emits its initial B-frame latency. Retain it, then feed enough
// zeros to emit the last connected path's delayed convolution sample. The
// FFT-padded nFilterLength is NOT the impulse response's duration.
static sf_count_t TailFrames(const ChannelPaths& mixer)
{
    sf_count_t tail = 0;
    for(unsigned int p = 0; p < mixer.nPaths(); ++p)
    {
        const ChannelPaths::ChannelPath& path = mixer.Paths()[p];
        if(path.inChannel.empty() || path.outChannel.empty())
            continue;
        const sf_count_t impulseFrames = path.filter.sf_FilterFormat().frames;
        if(impulseFrames <= 0)
            throw std::range_error("Empty impulse response");
        DWORD inputDelay = 0, outputDelay = 0;
        for(ChannelPaths::ChannelPath::size_type i = 0; i < path.inChannel.size(); ++i)
        {
            const DWORD delay = mixer.nInputSamplesDelay()[path.inChannel[i].nChannel];
            if(delay > inputDelay) inputDelay = delay;
        }
        for(ChannelPaths::ChannelPath::size_type i = 0; i < path.outChannel.size(); ++i)
        {
            const DWORD delay = mixer.nOutputSamplesDelay()[path.outChannel[i].nChannel];
            if(delay > outputDelay) outputDelay = delay;
        }
        const sf_count_t length = AddFrames(AddFrames(impulseFrames - 1, inputDelay), outputDelay);
        if(length > tail) tail = length;
    }
    return AddFrames(mixer.nHalfPartitionLength(), tail);
}

static DWORD ChunkFrames(const ChannelPaths& mixer, DWORD filterLengths)
{
    const DWORD inputs = mixer.nInputChannels(), outputs = mixer.nOutputChannels();
    const DWORD channels = inputs > outputs ? inputs : outputs;
    if(!channels || !inputs || !outputs || !mixer.nFilterLength() || !filterLengths)
        throw std::range_error("Invalid buffer dimensions");
    // At most 1 MiB per buffer, even for enormous nFilterLengths. Also preserve
    // the engine's DWORD byte counters on 32-bit Windows.
    DWORD limit = 262144 / channels;
    const DWORD byteLimit = (std::numeric_limits<DWORD>::max)() / sizeof(float) / channels;
    if(limit > byteLimit) limit = byteLimit;
    if(!limit) throw std::range_error("Too many channels");
    const DWORD length = mixer.nFilterLength();
    return length > limit / filterLengths ? limit : length * filterLengths;
}
