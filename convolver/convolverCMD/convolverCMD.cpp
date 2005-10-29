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
	const WORD SAMPLES = 1; // how many filter lengths to convolve at a time{

	HRESULT hr = S_OK;
	Holder< CConvolution<float> > conv;
	Holder< Sample<float> > convertor(new Sample_ieeefloat<float>());

#if defined(DEBUG) | defined(_DEBUG)
	debugstream.sink (apDebugSinkConsole::sOnly);
#endif

	if (argc != 5)
	{
		std::wcerr << "Usage: convolverCMD nPartitions config.txt inputwavfile.wav outputwavfile.wav" << std::endl;
		std::wcerr << "       nPartitions = 0 for overlap-save, or the number of partitions to be used." << std:: endl;
		return 1;
	}

	try
	{
		std::wistringstream szPartitions(argv[1]);
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

#ifdef LIBSNDFILE
		SF_INFO sf_info; ::ZeroMemory(&sf_info, sizeof(sf_info));
		CWaveFileHandle WavIn(argv[3], SFM_READ, &sf_info);
		std::cerr << waveFormatDescription(sf_info, "Input file format: ") << std::endl;

		conv = new CConvolution<float>(argv[2], nPartitions == 0 ? 1 : nPartitions); // Sets conv. nPartitions==0 => use overlap-save
		const DWORD cBufferLength = conv->Mixer.nFilterLength * SAMPLES;  // frames
#else
		CWaveFileHandle WavIn;
		if( FAILED( hr = WavIn->Open( argv[3], NULL, WAVEFILE_READ ) ) )
		{
			std::wcerr << "Failed to open " << std::basic_string< _TCHAR >(argv[3], _tcslen(argv[3])) << " for reading" << std::endl;
			throw(hr);
		}

		WAVEFORMATEXTENSIBLE wfexFilterFormat;
		::ZeroMemory(&wfexFilterFormat, sizeof(wfexFilterFormat));
		wfexFilterFormat = *(WAVEFORMATEXTENSIBLE*) WavIn->GetFormat();
		std::cerr << waveFormatDescription(&wfexFilterFormat, WavIn->GetSize() / WavIn->GetFormat()->nBlockAlign, "Input file format: ") << std::endl;

		WAVEFORMATEX* pWave = WavIn->GetFormat();
		hr = SelectConvolution(pWave, conv, argv[2], nPartitions == 0 ? 1 : nPartitions); // Sets conv. nPartitions==0 => use overlap-save
		if (FAILED(hr))
		{
			throw(hr);
		}

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
		cdebug << "cBufferLength=" << cBufferLength << std::endl ;
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
		hr = calculateOptimumAttenuation(fAttenuation, argv[2], sf_info.channels, nPartitions);
		fElapsed = t.msec();
		if (FAILED(hr))
		{
			std::wcerr << "Failed to calculate optimum attenuation" << std::endl;
			throw (hr);
		}
		std::wcerr << "Optimum attenuation: " << fAttenuation << " calculated in " << fElapsed << " milliseconds" << std::endl;
		//fAttenuation = 0;
		std::wcerr << "Using attenuation of " << fAttenuation << std::endl;

#ifdef LIBSNDFILE
		std::vector<float> pfInputSamples(cBufferLength * sf_info.channels);
		std::vector<float> pfOutputSamples(cBufferLength * sf_info.channels);
#else
		std::vector<BYTE> pbInputSamples(cBufferLength);
		std::vector<BYTE> pbOutputSamples(cBufferLength);
#endif

#ifdef	LIBSNDFILE
		// Write out in the same format as the input file
		CWaveFileHandle WavOut(argv[4], SFM_WRITE, &sf_info);
#else
		CWaveFileHandle WavOut;
		if( FAILED( hr = WavOut->Open( argv[4], WavIn->GetFormat(), WAVEFILE_WRITE ) ) )
		{
			std::wcerr << "Failed to open " << std::basic_string< _TCHAR >(argv[4], _tcslen(argv[4])) << " for writing" << std::endl;
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
			// Pad with zeros, to flush
			for(int i = dwSizeRead * sf_info.channels; i < cBufferLength * sf_info.channels; ++i)
				pfInputSamples[i]=0;

			DWORD dwBlocksToProcess = cBufferLength;
			// Pad with zeros, to flush
			for(int i = dwSizeRead; i < cBufferLength; ++i)
				pfInputSamples[i]=0;
#else
			if (dwSizeRead % conv->Mixer.wfexFilterFormat.Format.nBlockAlign != 0)
			{
				std::wcerr << "Failed to read a whole number of blocks. Read " << dwSizeRead << " bytes." << std::endl;
				throw(std::length_error("Failed to read a whole number of blocks"));
			}

			// Pad with zeros, to flush
			for(int i = dwSizeRead; i < cBufferLength; ++i)
				pfInputSamples[i]=0;

			DWORD dwBlocksToProcess = cBufferLength / conv->Mixer.wfexFilterFormat.Format.nBlockAlign;

#endif

			// nPartitions == 0 => use overlap-save version
			DWORD dwBufferSizeGenerated = nPartitions == 0 ?
#ifdef LIBSNDFILE
				conv->doConvolution(reinterpret_cast<BYTE*>(&pfInputSamples[0]), reinterpret_cast<BYTE*>(&pfOutputSamples[0]),
#else
				conv->doConvolution(&pbInputSamples[0], &pbOutputSamples[0],
#endif
				convertor, convertor,
				/* dwBlocksToProcess */ dwBlocksToProcess,
				/* fAttenuation_db */ fAttenuation,
				/* fWetMix,*/ 1.0f,
				/* fDryMix */ 0.0f)
				:
#ifdef LIBSNDFILE
			conv->doPartitionedConvolution(reinterpret_cast<BYTE*>(&pfInputSamples[0]), reinterpret_cast<BYTE*>(&pfOutputSamples[0]),
#else
			conv->doPartitionedConvolution(&pbInputSamples[0], &pbOutputSamples[0],
#endif
				convertor, convertor,
				/* dwBlocksToProcess */ dwBlocksToProcess,
				/* fAttenuation_db */ fAttenuation,
				/* fWetMix,*/ 1.0f,
				/* fDryMix */ 0.0f);

#if defined(DEBUG) | defined(_DEBUG)
				//cdebug << "dwBufferSizeGenerated=" << dwBufferSizeGenerated << std::endl;
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
			<< std::basic_string< _TCHAR >(argv[4], _tcslen(argv[4]))
			<< " in " << fElapsed << " milliseconds" << std::endl;

#ifndef LIBSNDFILE
		WavIn->Close();
		WavOut->Close();
#endif

		return 0;

	}
	catch(const std::exception& error)
	{
		std::wcerr << "Standard exception: " << error.what() << std::endl;
	}
	catch(const HRESULT& hr)
	{	
		std::wcerr << "Failed (" << std::hex << hr	<< std::dec << ")" << std::endl;
	}
	catch (...)
	{
		std::wcerr << "Failed" <<std::endl;
	}

#if defined(DEBUG) | defined(_DEBUG)
	_CrtDumpMemoryLeaks();
#endif

	return hr;
}

