// convolverPipe.cpp: stdin -> convolution -> stdout.
// Restored from the corroborated historical Convolver 4.4 project in homes.zip.
// Copyright (C) 2006 jrp; GNU GPL version 2 or, at your option, later.
// See the repository COPYING file. Argument and stream safety repairs are local.
#include "convolution\config.h"
#include "stdafx.h"
#include "convolution\convolution.h"
#include "convolution\waveformat.h"
#include "convolution\wavefile.h"
#include <algorithm>
#include <iostream>
#include <vector>
#include <io.h>
#include <fcntl.h>
#include "pipe_helpers.h"

int _tmain(int argc, _TCHAR* argv[])
{
    try
    {
		if (argc != 10)
		{
			std::wcerr << "Usage: convolverPipe nPartitions nTuningRigour fAttenuationDB nFilterLengths" << std::endl;
			std::wcerr << "                     MajorFormatIn MinorFormatIn MajorFormatOut MinorFormatOut config.txt|IR.wav" << std::endl;
			std::wcerr << "       nPartitions = 0 for overlap-save, or the number of partitions to be used" << std::endl;
			std::wcerr << "       nTuningRigour = 0-" << PlanningRigour::nDegrees-1 << " (";
			for(unsigned int i = 0; i < PlanningRigour::nDegrees - 1; ++i)
				std::wcerr << PlanningRigour::Rigour[i] << "|";
			std::wcerr <<  PlanningRigour::Rigour[PlanningRigour::nDegrees - 1] << ")" << std::endl;
			std::wcerr << "       fAttenuationDB = the attenuation to be applied in dB" << std::endl;
			std::wcerr << "       nFilterLengths = the number of filter lengths to be processed at a time" << std::endl;

			std::wcerr << "       MajorFormat = ";
			for(unsigned int i = 0; i < SndFileFormat::nMajor - 1; ++i)
			{
				if(i  % 8 == 0)
					std::wcerr << std::endl << "          ";
				std::wcerr << SndFileFormat::Major[i].id << "|";
			}
			std::wcerr <<  SndFileFormat::Major[SndFileFormat::nMajor - 1].id << std::endl;

			std::wcerr << "       MinorFormat = ";
			for(unsigned int i = 0; i < SndFileFormat::nMinor - 1; ++i)
			{
				if(i  % 8 == 0)
					std::wcerr << std::endl << "          ";
				std::wcerr << SndFileFormat::Minor[i].id << "|";

			}
			std::wcerr <<  SndFileFormat::Minor[SndFileFormat::nMinor - 1].id << std::endl;

			std::wcerr << "       config.txt|IR.wav = a config text file specifying a single filter path" << std::endl;
			std::wcerr << "                           or a sound file to be used as a filter" << std::endl;
			return 1;
		}

        const DWORD nPartitions = ParseCount(argv[1], "Invalid nPartitions", true);
        const DWORD nPlanningRigour = ParseCount(argv[2], "Invalid nTuningRigour", true);
        const float fAttenuationDB = ParseAttenuation(argv[3]);
        const DWORD nFilterLengths = ParseCount(argv[4], "nFilterLengths must be positive", false);
        if(nPlanningRigour >= PlanningRigour::nDegrees)
            throw std::range_error("nTuningRigour is out of range");
        // Bound the per-partition allocation before constructing the engine.
        if(nPartitions > 4096)
            throw std::range_error("nPartitions must not exceed 4096");
        const int inputFormat = SndFileFormat::Lookup(argv[5], argv[6]);
        const int outputFormat = SndFileFormat::Lookup(argv[7], argv[8]);

        ConvolutionList<BaseT> conv(argv[9], nPartitions == 0 ? 1 : nPartitions, nPlanningRigour);
        if(conv.nConvolutionList() != 1)
            throw convolutionException("Only a single filter configuration is acceptable");
        conv.selectConvolutionIndex(0);
        const ChannelPaths& mixer = conv.SelectedConvolution().Mixer;
        const DWORD frames = ChunkFrames(mixer, nFilterLengths);
        const sf_count_t tailFrames = TailFrames(mixer);
        std::wcerr << "Filter format: " << conv.DisplayConvolutionList() << std::endl;

        // Windows text mode translates LF/CR and treats Ctrl-Z as EOF.
        if(_setmode(_fileno(stdin), _O_BINARY) == -1 ||
           _setmode(_fileno(stdout), _O_BINARY) == -1)
            throw std::runtime_error("Could not set binary standard streams");

        SF_INFO inputInfo; ::ZeroMemory(&inputInfo, sizeof(inputInfo));
        inputInfo.channels = mixer.nInputChannels();
        inputInfo.samplerate = mixer.nSamplesPerSec();
        inputInfo.format = inputFormat;
        CWaveFileHandle input(_fileno(stdin), SFM_READ, &inputInfo);
        if(inputInfo.channels != mixer.nInputChannels())
            throw convolutionException("Input channel count does not match the filter configuration");
        if(inputInfo.samplerate != mixer.nSamplesPerSec())
            throw convolutionException("Input sample rate does not match the filter configuration");

        SF_INFO outputInfo; ::ZeroMemory(&outputInfo, sizeof(outputInfo));
        outputInfo.channels = mixer.nOutputChannels();
        outputInfo.samplerate = mixer.nSamplesPerSec();
        outputInfo.format = outputFormat;
        CWaveFileHandle output(_fileno(stdout), SFM_WRITE, &outputInfo);
        if(outputInfo.channels != mixer.nOutputChannels() || outputInfo.samplerate != mixer.nSamplesPerSec())
            throw convolutionException("Output format does not match the filter configuration");

        std::vector<float> inputBuffer(size_t(frames) * mixer.nInputChannels());
        std::vector<float> outputBuffer(size_t(frames) * mixer.nOutputChannels());
        Holder< ConvertSample<float> > convertor(new ConvertSample_ieeefloat<BaseT>());
        sf_count_t totalRead = 0, totalWritten = 0, remainingTail = 0;
        bool eof = false;
        for(;;)
        {
            sf_count_t count = 0;
            if(!eof)
            {
                count = input.readf_float(&inputBuffer[0], frames);
                if(count < 0 || count > frames || input.error())
                    throw std::runtime_error("Error reading standard input");
                totalRead = AddFrames(totalRead, count);
                if(count == 0)
                {
                    eof = true;
                    remainingTail = totalRead ? tailFrames : 0;
                }
            }
            if(eof)
            {
                if(!remainingTail) break;
                count = remainingTail < frames ? remainingTail : frames;
                std::fill(inputBuffer.begin(), inputBuffer.end(), 0.0f);
                remainingTail -= count;
            }

            InterleavedIO<BaseT> io(reinterpret_cast<BYTE*>(&inputBuffer[0]),
                reinterpret_cast<BYTE*>(&outputBuffer[0]), convertor.get_ptr(), convertor.get_ptr());
            const DWORD bytes = nPartitions == 0 ?
                conv.SelectedConvolution().doConvolution(io, DWORD(count), fAttenuationDB) :
                conv.SelectedConvolution().doPartitionedConvolution(io, DWORD(count), fAttenuationDB);
            const DWORD frameBytes = sizeof(float) * mixer.nOutputChannels();
            if(bytes != DWORD(count) * frameBytes)
                throw std::runtime_error("Unexpected convolution output size");
            const sf_count_t written = output.writef_float(&outputBuffer[0], count);
            if(written != count || output.error())
                throw std::runtime_error("Error writing standard output");
            totalWritten = AddFrames(totalWritten, written);
        }
        // Header finalization and buffered stream failures must affect exit status.
        if(output.close()) throw std::runtime_error("Error closing standard output");
        if(input.close()) throw std::runtime_error("Error closing standard input");
        std::wcerr << "Read " << totalRead << " frames; wrote " << totalWritten << " frames" << std::endl;
        return 0;
    }
    catch(const std::exception& error)
    {
        std::wcerr << "convolverPipe: " << error.what() << std::endl;
    }
    catch(...)
    {
        std::wcerr << "convolverPipe: convolution or stream failure" << std::endl;
    }
    return 1;
}
