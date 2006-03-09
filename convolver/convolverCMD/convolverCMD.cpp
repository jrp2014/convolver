// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005  John Pavel
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
/////////////////////////////////////////////////////////////////////////////
//
// convolverCMD.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "convolution\config.h"
#include "convolution\convolution.h"
#include "convolution\waveformat.h"

#ifndef LIBSNDFILE
#include "Common\dxstdafx.h"
#endif
#include <sstream>
#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#endif
#include "debugging\fasttiming.h"
#include "convolution\wavefile.h"

int _tmain(int argc, _TCHAR* argv[])
{
#if defined(DEBUG) | defined(_DEBUG)
	const int	mode =   (1 * _CRTDBG_MODE_DEBUG) | (0 * _CRTDBG_MODE_WNDW);
	::_CrtSetReportMode (_CRT_WARN, mode);
	::_CrtSetReportMode (_CRT_ERROR, mode);
	::_CrtSetReportMode (_CRT_ASSERT, mode);

	const int	old_flags = ::_CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);
	::_CrtSetDbgFlag (  old_flags
		| (1 * _CRTDBG_LEAK_CHECK_DF)
		| (1 * _CRTDBG_CHECK_ALWAYS_DF)
		| (1 * _CRTDBG_ALLOC_MEM_DF));
#endif

	const DWORD SAMPLES = 1; // how many filter lengths to convolve at a time

	HRESULT hr = S_OK;
	Holder< ConvertSample<float> > convertor(new ConvertSample_ieeefloat<float>());

	PlanningRigour pr;

#if defined(DEBUG) | defined(_DEBUG)
	debugstream.sink (apDebugSinkConsole::sOnly);
#endif

	if (argc != 6)
	{
		USES_CONVERSION;

		std::wcerr << "Usage: convolverCMD nPartitions nTuningRigour config.txt|IR.wav infile outfile" << std::endl;
		std::wcerr << "       nPartitions = 0 for overlap-save, or the number of partitions to be used" << std::endl;
		std::wcerr << "       nTuningRigour = 0-" << pr.nDegrees-1 << " (";
		for(int i = 0; i < pr.nDegrees - 1; ++i)
			std::wcerr << pr.Rigour[i] << "|";
		std::wcerr <<  pr.Rigour[pr.nDegrees - 1] << ")" << std::endl;
		std::wcerr << "       config.txt|IR.wav = a config text file specifying a single filter path" << std::endl;
		std::wcerr << "                           or a sound file to be used as a filter" << std::endl;
		std::wcerr << "       input and output sound files are, typically, .wav" << std::endl;
		return 1;
	}
#define PARTITIONS argv[1]
#define PLANNINGRIGOUR argv[2]
#define CONFIG argv[3]
#define INPUTFILE argv[4]
#define OUTPUTFILE argv[5]

	try
	{
		std::wistringstream szPartitions(PARTITIONS);
		DWORD nPartitions;
		szPartitions >> nPartitions;

		if (nPartitions == 0)
		{
			std::wcerr << "Using overlap-save convolution" << std::endl;
		}
		else
		{
			std::wcerr << "Using partitioned convolution with " << nPartitions << " partition(s)" << std::endl;
		}

		std::wistringstream szPlanningRigour(PLANNINGRIGOUR);
		DWORD nPlanningRigour;
		szPlanningRigour >> nPlanningRigour;

		ConvolutionList<float> conv(CONFIG, nPartitions == 0 ? 1 : nPartitions, 
			nPlanningRigour); // Sets conv. nPartitions==0 => use overlap-save

		// TODO:: allow SF_INFO as well as WAVEFILEEX to select convolutions
		if(conv.nConvolutionList() != 1)
			throw convolutionException("Only single filter path specification acceptable");

#ifdef LIBSNDFILE
		SF_INFO sf_info; ::ZeroMemory(&sf_info, sizeof(sf_info));
		// TODO: The following uses the sample rate of the first filter path for .PCM files
		conv.selectConvolutionIndex(0);  // Select the one and only filter path
		CWaveFileHandle WavIn(INPUTFILE, SFM_READ, &sf_info, conv.SelectedConvolution().Mixer.nSamplesPerSec());
		std::cerr << waveFormatDescription(sf_info, "Input file format: ") << std::endl;

		const DWORD cBufferLength = conv.SelectedConvolution().Mixer.nFilterLength() * SAMPLES;  // frames
#else
		CWaveFileHandle WavIn;
		if( FAILED( hr = WavIn->Open(INPUTFILE, NULL, WAVEFILE_READ ) ) )
		{
			std::wcerr << "Failed to open " << std::basic_string< _TCHAR >(INPUTFILE, _tcslen(INPUTFILE)) << " for reading" << std::endl;
			throw(hr);
		}

		WAVEFORMATEXTENSIBLE wfexFilterFormat;
		::ZeroMemory(&wfexFilterFormat, sizeof(wfexFilterFormat));
		wfexFilterFormat = *(WAVEFORMATEXTENSIBLE*) WavIn->GetFormat();
		std::cerr << waveFormatDescription(&wfexFilterFormat, WavIn->GetSize() / WavIn->GetFormat()->nBlockAlign, "Input file format: ") << std::endl;

		WAVEFORMATEX* pWave = WavIn->GetFormat();

		std::cerr << waveFormatDescription(&(conv->FIR.wfexFilterFormat), conv->FIR.nFilterLength, "Filter format: ") << std::endl;

		if (conv->FIR.wfexFilterFormat.Format.nChannels < WavIn->GetFormat()->nChannels)
		{
			std::wcerr << "Filter format has fewer channels than the Input file" << std::endl;
			throw(hr);
		}

		if (conv->FIR.wfexFilterFormat.Format.nSamplesPerSec!= WavIn->GetFormat()->nSamplesPerSec)
		{
			std::wcerr << "Warning: Filter format and Input file have different sample rates" << std::endl;
		}

		const DWORD cBufferLength = conv->FIR.nFilterLength * conv->FIR.wfexFilterFormat.Format.nBlockAlign * SAMPLES;  // bytes
#endif		

#if defined(DEBUG) | defined(_DEBUG)
		//cdebug << "cBufferLength=" << cBufferLength << std::endl ;
#endif

		UINT dwSizeWrote = 0;
		DWORD dwSizeRead = 0;
		DWORD dwTotalSizeRead = 0;
		DWORD dwTotalSizeWritten = 0;
#ifdef LIBSNDFILE
		const DWORD dwTotalSizeToRead = sf_info.frames;
#else
		const DWORD dwTotalSizeToRead = WavIn->GetSize(); // bytes
#endif
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "dwTotalSizeToRead=" << dwTotalSizeToRead << std::endl;
#endif

		float fAttenuation = 0;
		double fElapsed = 0;
		apHiResElapsedTime t;
		hr = conv.SelectedConvolution().calculateOptimumAttenuation(fAttenuation, nPartitions == 0);
		fElapsed = t.msec();
		if (FAILED(hr))
		{
			std::wcerr << "Failed to calculate optimum attenuation" << std::endl;
			throw (hr);
		}
		std::wcerr << "Optimum attenuation: " << fAttenuation << " calculated in " << fElapsed << " milliseconds" << std::endl;
		//fAttenuation = 0;
		std::wcerr << "Using attenuation of " << fAttenuation << std::endl;

		const int nInputChannels = conv.SelectedConvolution().Mixer.nInputChannels();

#ifdef LIBSNDFILE
		std::vector<float> pfInputSamples(cBufferLength * nInputChannels);
		std::vector<float> pfOutputSamples(cBufferLength * nInputChannels);
#else
		std::vector<BYTE> pbInputSamples(cBufferLength,0);
		std::vector<BYTE> pbOutputSamples(cBufferLength,0);
#endif

#ifdef	LIBSNDFILE
		// Write out in the same format as the input file, but with the right number of output channels
		sf_info.channels = conv.SelectedConvolution().Mixer.nOutputChannels();	// select the first filter path
		CWaveFileHandle WavOut(OUTPUTFILE, SFM_WRITE, &sf_info, sf_info.samplerate);
#else
		CWaveFileHandle WavOut;
		if( FAILED( hr = WavOut->Open(OUTPUTFILE, WavIn->GetFormat(), WAVEFILE_WRITE ) ) )  // TODO:: fix for when different no of i/o channels
		{
			std::wcerr << "Failed to open " << std::basic_string< _TCHAR >(OUTPUTFILE, _tcslen(OUTPUTFILE)) << " for writing" << std::endl;
			throw(hr);
		}
#endif

		t.reset(); // Start timing
		while (dwTotalSizeRead != dwTotalSizeToRead)
		{
#ifdef LIBSNDFILE
			dwSizeRead = WavIn.readf_float(&pfInputSamples[0], cBufferLength);
			if (dwSizeRead == 0)
			{
				throw std::length_error("Failed to read input buffer");
			}
#else
			hr = WavIn->Read(&pbInputSamples[0], cBufferLength, &dwSizeRead);
			if (FAILED(hr))
			{
				std::wcerr << "Failed to read " << cBufferLength << "bytes." << std::endl;
				throw(std::length_error("Failed to read"));
			}
#endif
			dwTotalSizeRead += dwSizeRead;

#ifdef LIBSNDFILE
			// Pad with zeros, to flush.  TODO: Won't work with 8-bit samples
			for(unsigned int i = dwSizeRead * nInputChannels; i < cBufferLength * nInputChannels; ++i)
				pfInputSamples[i]=0;

			DWORD dwBlocksToProcess = cBufferLength;
#else
			if (dwSizeRead % conv.Mixer.wfexFilterFormat.Format.nBlockAlign != 0)
			{
				std::wcerr << "Failed to read a whole number of blocks. Read " << dwSizeRead << " bytes." << std::endl;
				throw(std::length_error("Failed to read a whole number of blocks"));
			}

			// Pad with zeros, to flush
			for(int i = dwSizeRead; i < cBufferLength; ++i)
				pfInputSamples[i]=0;

			DWORD dwBlocksToProcess = cBufferLength / conv.Mixer.wfexFilterFormat.Format.nBlockAlign;

#endif

			// nPartitions == 0 => use overlap-save version
			DWORD dwBufferSizeGenerated = nPartitions == 0 ?
#ifdef LIBSNDFILE
				conv.SelectedConvolution().doConvolution(reinterpret_cast<BYTE*>(&pfInputSamples[0]),
				reinterpret_cast<BYTE*>(&pfOutputSamples[0]),
#else
				conv.SelectedConvolution().doConvolution(&pbInputSamples[0], &pbOutputSamples[0],
#endif
				convertor.get_ptr(), convertor.get_ptr(),
				/* dwBlocksToProcess */ dwBlocksToProcess,
				/* fAttenuation_db */ fAttenuation)
				:
#ifdef LIBSNDFILE
			conv.SelectedConvolution().doPartitionedConvolution(reinterpret_cast<BYTE*>(&pfInputSamples[0]),
				reinterpret_cast<BYTE*>(&pfOutputSamples[0]),
#else
			conv.SelectedConvolution().doPartitionedConvolution(&pbInputSamples[0], &pbOutputSamples[0],
#endif
				convertor.get_ptr(), convertor.get_ptr(),
				/* dwBlocksToProcess */ dwBlocksToProcess,
				/* fAttenuation_db */ fAttenuation);

#if defined(DEBUG) | defined(_DEBUG)
				cdebug << "dwBufferSizeGenerated=" << dwBufferSizeGenerated << std::endl;
#endif


#ifdef LIBSNDFILE
			dwSizeWrote = WavOut.writef_float(&pfOutputSamples[0], dwBufferSizeGenerated / (sizeof(float) * sf_info.channels)); // dwSizeWrote is in frames
			if (dwSizeWrote != dwBufferSizeGenerated / (sizeof(float) * sf_info.channels))
#else
			hr = WavOut->Write(dwBufferSizeGenerated, &pbOutputSamples[0], &dwSizeWrote);
			if (FAILED(hr))
			{
				std::wcerr << "Failed to write " << dwBufferSizeGenerated << " bytes." << std::endl;
				throw(std::length_error("Failed to write"));
			}
			if (dwSizeWrote != dwBufferSizeGenerated)
#endif
			{
				std::wcerr << "Failed to write " << dwBufferSizeGenerated << " bytes. Only wrote " 
					<< dwSizeWrote << std::endl;
				throw(std::length_error("Failed to write"));
			}

			dwTotalSizeWritten += dwSizeWrote;
		} 

		fElapsed = t.msec();

		std::wcerr << "Convolved and wrote " << dwTotalSizeWritten <<
#ifdef LIBSNDFILE
			" frames to "
#else
			" bytes to " 
#endif
			<< std::basic_string< _TCHAR >(OUTPUTFILE, _tcslen(OUTPUTFILE))
			<< " in " << fElapsed << " milliseconds" << std::endl;

#ifndef LIBSNDFILE
		WavIn->Close();
		WavOut->Close();
#endif

		return 0;

	}
	catch(const convolutionException& error)
	{
		std::wcerr << "Convolution error: " << error.what() << std::endl;
	}
	catch(const std::exception& error)
	{
		std::wcerr << "Standard exception: " << error.what() << std::endl;
	}
	catch(const HRESULT& hr)
	{	
		std::wcerr << "Failed (" << std::hex << hr	<< std::dec << ")" << std::endl;
	}
	catch (const char* what)
	{
		std::wcerr << "Failed: " << what << std::endl;
	}
	catch (const TCHAR* what)
	{
		std::wcerr << "Failed: " << what << std::endl;
	}
	catch (...)
	{
		std::wcerr << "Failed" <<std::endl;
	}

#if defined(DEBUG) | defined(_DEBUG)
	::_CrtDumpMemoryLeaks();
#endif

	return hr;
}

