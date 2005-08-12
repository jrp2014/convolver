// convolverCMD.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "Common\dxstdafx.h"
#include <sstream>
#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#endif
#include "debugging\fasttiming.h"
#include "convolution\wavefile.h"

int _tmain(int argc, _TCHAR* argv[])
{

	HRESULT hr = S_OK;
	Holder<CConvolution> conv;

#if defined(DEBUG) | defined(_DEBUG)
	debugstream.sink (apDebugSinkConsole::sOnly);
#endif

	if (argc != 5)
	{
		std::wcerr << "Usage: convolverCMD nPartitions filter.wav inputwavfile.wav outputwavfile.wav" << std::endl;
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

		CWaveFileHandle WavIn;
		if( FAILED( hr = WavIn->Open( argv[3], NULL, WAVEFILE_READ ) ) )
		{
			std::wcerr << "Failed to open " << std::basic_string< _TCHAR >(argv[3], _tcslen(argv[3])) << " for reading" << std::endl;
			throw(hr);
		}
		std::cerr << waveFormatDescription(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(WavIn->GetFormat()), WavIn->GetSize() / WavIn->GetFormat()->nBlockAlign, "Input file format: ") << std::endl;

		WAVEFORMATEX *pWave = WavIn->GetFormat();
		hr = SelectConvolution(pWave, conv, argv[2], nPartitions == 0 ? 1 : nPartitions); // Sets conv. nPartitions==0 => use overlap-save
		if (FAILED(hr))
		{
			throw(hr);
		}

		std::cerr << waveFormatDescription(&(conv->FIR.wfexFilterFormat), conv->FIR.nFilterLength, "Filter format: ") << std::endl;

		if (conv->FIR.wfexFilterFormat.Format.nAvgBytesPerSec != WavIn->GetFormat()->nAvgBytesPerSec ||
			conv->FIR.wfexFilterFormat.Format.nBlockAlign != WavIn->GetFormat()->nBlockAlign ||
			conv->FIR.wfexFilterFormat.Format.nChannels != WavIn->GetFormat()->nChannels ||
			conv->FIR.wfexFilterFormat.Format.nSamplesPerSec!= WavIn->GetFormat()->nSamplesPerSec ||
			conv->FIR.wfexFilterFormat.Format.wBitsPerSample != WavIn->GetFormat()->wBitsPerSample ||
			conv->FIR.wfexFilterFormat.Format.wFormatTag != WavIn->GetFormat()->wFormatTag)
		{
			std::wcerr << "Filter format not the same Input file format" << std::endl;
			throw(hr);
		}

		double fAttenuation = 0;
		double fElapsed = 0;
		apHiResElapsedTime t;
		hr = calculateOptimumAttenuation(fAttenuation, argv[2], nPartitions);
		fElapsed = t.msec();
		if (FAILED(hr))
		{
			std::wcerr << "Failed to calculate optimum attenuation" << std::endl;
			throw (hr);
		}
		std::wcerr << "Optimum attenuation: " << fAttenuation << " calculated in " << fElapsed << " milliseconds" << std::endl;
		fAttenuation = 0;
		std::wcerr << "Using attenuation of " << fAttenuation << std::endl;

		CWaveFileHandle WavOut;
		if( FAILED( hr = WavOut->Open( argv[4], WavIn->GetFormat(), WAVEFILE_WRITE ) ) )
		{
			std::wcerr << "Failed to open " << std::basic_string< _TCHAR >(argv[4], _tcslen(argv[4])) << " for writing" << std::endl;
			throw(hr);
		}

		const WORD SAMPLES = 1; // how many filter lengths to convolve at a time
		const DWORD cBufferLength = conv->FIR.nFilterLength * conv->FIR.wfexFilterFormat.Format.nBlockAlign * SAMPLES;  // bytes
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "cBufferLength=" << cBufferLength << std::endl ;
#endif
		std::vector<BYTE> pbInputSamples(cBufferLength);
		std::vector<BYTE> pbOutputSamples(cBufferLength);

		UINT dwSizeWrote = 0;
		DWORD dwSizeRead = 0;
		DWORD dwTotalSizeRead = 0;
		DWORD dwTotalSizeWritten = 0;
		const DWORD dwTotalSizeToRead = WavIn->GetSize(); // bytes
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "dwTotalSizeToRead=" << dwTotalSizeToRead << std::endl;
#endif

#if defined(DEBUG) | defined(_DEBUG)
		CWaveFileHandle CWaveFileTrace;
		if (FAILED(hr = CWaveFileTrace->Open(TEXT("c:\\temp\\CMDTrace.wav"), WavIn->GetFormat(), WAVEFILE_WRITE)))
		{
			std::wcerr << "Failed to open " << "c:\\temp\\CMDTrace.wav for writing" << std::endl;
			throw(hr);
		}
#endif

		t.reset(); // Start timing
		while (dwTotalSizeRead != dwTotalSizeToRead)
		{
			hr = WavIn->Read(&pbInputSamples[0], cBufferLength, &dwSizeRead);
			if (FAILED(hr))
			{
				std::wcerr << "Failed to read " << cBufferLength << "bytes." << std::endl;
				throw(std::length_error("Failed to read"));
			}
			dwTotalSizeRead += dwSizeRead;

			if (dwSizeRead % conv->FIR.wfexFilterFormat.Format.nBlockAlign != 0)
			{
				std::wcerr << "Failed to read a whole number of blocks. Read " << dwSizeRead << " bytes." << std::endl;
				throw(std::length_error("Failed to read a whole number of blocks"));
			}

			DWORD dwBlocksToProcess = dwSizeRead / conv->FIR.wfexFilterFormat.Format.nBlockAlign;

			// nPartitions == 0 => use overlap-save version
			DWORD dwBufferSizeGenerated = nPartitions == 0 ?
				conv->doConvolution(&pbInputSamples[0], &pbOutputSamples[0],
				/* dwBlocksToProcess */ dwBlocksToProcess,
				/* fAttenuation_db */ fAttenuation,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
				, CWaveFileTrace
#endif
				)
				:
			conv->doPartitionedConvolution(&pbInputSamples[0], &pbOutputSamples[0],
				/* dwBlocksToProcess */ dwBlocksToProcess,
				/* fAttenuation_db */ fAttenuation,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
				, CWaveFileTrace
#endif
				);

#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "dwBufferSizeGenerated=" << dwBufferSizeGenerated << std::endl;
#endif

			hr = WavOut->Write(dwBufferSizeGenerated, &pbOutputSamples[0], &dwSizeWrote);
			if (FAILED(hr))
			{
				std::wcerr << "Failed to write " << dwBufferSizeGenerated << "bytes." << std::endl;
				throw(std::length_error("Failed to write"));
			}

			if (dwSizeWrote != dwBufferSizeGenerated)
			{
				std::wcerr << "Failed to write " << dwBufferSizeGenerated << "bytes. Only wrote " 
					<< dwSizeWrote << std::endl;
				throw(std::length_error("Failed to write"));
			}
			dwTotalSizeWritten += dwSizeWrote;

		} 

		fElapsed = t.msec();

		std::wcerr << "Convolved and wrote " << dwTotalSizeWritten << " bytes to " << std::basic_string< _TCHAR >(argv[4], _tcslen(argv[4]))
			 << " in " << fElapsed << " milliseconds" << std::endl;

		WavIn->Close();
		WavOut->Close();
#if defined(DEBUG) | defined(_DEBUG)
		CWaveFileTrace->Close();
#endif

		return 0;

	}
	catch (...)
	{
		std::wcerr << "Failed" <<std::endl;
	}

	return hr;
}

