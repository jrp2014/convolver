// convolverCMD.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "dxstdafx.h"

int _tmain(int argc, _TCHAR* argv[])
{

	HRESULT hr = S_OK;

#if defined(DEBUG) | defined(_DEBUG)
	debugstream.sink (apDebugSinkConsole::sOnly);
#endif

	if (argc != 4)
	{
		std::cerr <<  "Usage: convolverCMD filter.wav inputwavfile1.wav outputwavfile.wav" << std::endl;
		return 1;
	}

	CWaveFile* WavIn = new CWaveFile;
	if( FAILED( hr = WavIn->Open( argv[2], NULL, WAVEFILE_READ ) ) )
	{
		std::cerr << "Failed to open " << argv[2] << " for reading" << std::endl;
		SAFE_DELETE(WavIn);
		return hr;
	}
	std::cerr << waveFormatDescription(WavIn->GetFormat(), WavIn->GetSize() / WavIn->GetFormat()->nBlockAlign, "Input file format: ") << std::endl;

	CConvolution<float>* conv = NULL; 
	hr = SelectConvolution(WavIn->GetFormat(), conv); // Sets conv 
	if (FAILED(hr))
	{
		SAFE_DELETE(WavIn);
		return hr;
	}

	if ( NULL == conv )
	{
		SAFE_DELETE(WavIn);
		return E_OUTOFMEMORY;
	}

	hr = conv->LoadFilter(argv[1]);
	if (FAILED(hr))
	{
		std::cerr << "Failed to read Filter " << argv[1] << std::endl;
		delete conv;
		SAFE_DELETE(WavIn);
		return hr;
	}

	std::cerr << waveFormatDescription(&(conv->m_WfexFilterFormat), conv->m_Filter->nSamples, "Filter format: ") << std::endl;

	if (conv->m_WfexFilterFormat.nAvgBytesPerSec != WavIn->GetFormat()->nAvgBytesPerSec ||
		conv->m_WfexFilterFormat.nBlockAlign != WavIn->GetFormat()->nBlockAlign ||
		conv->m_WfexFilterFormat.nChannels != WavIn->GetFormat()->nChannels ||
		conv->m_WfexFilterFormat.nSamplesPerSec!= WavIn->GetFormat()->nSamplesPerSec ||
		conv->m_WfexFilterFormat.wBitsPerSample != WavIn->GetFormat()->wBitsPerSample ||
		conv->m_WfexFilterFormat.wFormatTag != WavIn->GetFormat()->wFormatTag)
	{
		std::cerr << "Filter format not the same Input file format" << std::endl;
		delete conv;
		SAFE_DELETE(WavIn);
		return hr;
	}

	double fAttenuation = 0;
	hr = calculateOptimumAttenuation<float>(fAttenuation, argv[1]);
	if (FAILED(hr))
	{
		std::cerr << "Failed to calculate optimum attenuation" << std::endl;
		delete conv;
		SAFE_DELETE(WavIn);
		return hr;
	}
	std::cerr << "Optimum attenuation: " << fAttenuation << std::endl;

	CWaveFile* WavOut = new CWaveFile;
	if( FAILED( hr = WavOut->Open( argv[3], WavIn->GetFormat(), WAVEFILE_WRITE ) ) )
	{
		std::cerr << "Failed to open " << argv[3] << " for writing" << std::endl;
		delete conv;
		SAFE_DELETE(WavIn);
		SAFE_DELETE(WavOut);
		return hr;
	}

	const DWORD dwSizeToRead = WavIn->GetFormat()->wBitsPerSample / 8;  // in bytes
#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "dwSizeToRead=" << dwSizeToRead << std::endl;
#endif
	DWORD dwSizeRead = 0;
	UINT dwSizeWrote = 0;
	DWORD dwBufferSizeRead = 0;
	DWORD dwTotalSizeRead = 0;
	DWORD dwTotalSizeToRead = WavIn->GetSize(); // bytes
#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "dwTotalSizeToRead=" << dwTotalSizeToRead << std::endl;
#endif

	const WORD SAMPLES = 1; // how many filter lengths to convolve at a time
	const DWORD nBufferLength = conv->m_Filter->nChannels * conv->m_Filter->nSamples * SAMPLES;
	const DWORD cBufferLength = nBufferLength * dwSizeToRead;  // bytes

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "nBufferLength=" << nBufferLength << ", cBufferLength=" << cBufferLength << std::endl ;
#endif

	BYTE* pbInputSamples = new BYTE[cBufferLength];
	if (NULL == pbInputSamples)
	{
		std::cerr << "Failed to allocate bInputSample" << std::endl;
		delete conv;
		SAFE_DELETE(WavIn);
		SAFE_DELETE(WavOut);
		return E_OUTOFMEMORY;
	}
	BYTE* pbInputSample = NULL;

	BYTE* pbOutputSamples = new BYTE[cBufferLength];
	if (NULL == pbOutputSamples)
	{
		std::cerr << "Failed to allocate bOutputSample" << std::endl;
		delete conv;
		delete pbInputSamples;
		SAFE_DELETE(WavIn);
		SAFE_DELETE(WavOut);
		return E_OUTOFMEMORY;
	}

	do
	{
		::ZeroMemory(pbInputSamples, cBufferLength);

		dwBufferSizeRead = 0;
		pbInputSample = pbInputSamples;

		// Samples are interleaved
		for (DWORD nSample = 0; nSample != conv->m_Filter->nSamples; nSample++)
		{
			for(WORD nChannel = 0; nChannel != conv->m_Filter->nChannels; nChannel++)
			{
				hr = WavIn->Read(pbInputSample, dwSizeToRead, &dwSizeRead);
				if (FAILED(hr))
				{
					std::cerr << "Failed to read " << dwSizeToRead << "bytes.  nChannel=" << nChannel << ", "
						<< "nSample=" << nSample << "." << std::endl;
					goto cleanup;
				}

				if (dwSizeToRead != dwSizeRead)
				{
					std::cerr << "Only read " << dwSizeToRead << "bytes.  nChannel=" << nChannel << ", "
						<< "nSample=" << nSample << "." << std::endl;
					goto cleanup;
				}
				pbInputSample += dwSizeRead;
				dwTotalSizeRead += dwSizeRead;
				dwBufferSizeRead += dwSizeRead;
			}
			if (dwTotalSizeToRead == dwTotalSizeRead)
				break;
		}
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "dwBufferSizeRead=" << dwBufferSizeRead << ", dwTotalSizeRead=" << dwTotalSizeRead << std::endl;
#endif

		DWORD dwBlocksToProcess = conv->m_Filter->nSamples;
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "dwBlocksToProcess=" << dwBlocksToProcess << std::endl;
#endif

		conv->doConvolutionConstrained(pbInputSamples, pbOutputSamples,
			/* dwBlocksToProcess */ dwBlocksToProcess,
			/* fAttenuation_db */ fAttenuation,
			/* fWetMix,*/ 1.0L,
			/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
			, NULL
#endif
			);

		hr = WavOut->Write(dwBufferSizeRead, pbOutputSamples, &dwSizeWrote);
		if (FAILED(hr))
		{
			std::cerr << "Failed to write " << dwBufferSizeRead << "bytes." << std::endl;
			goto cleanup;
		}

		if (dwSizeWrote != dwBufferSizeRead)
		{
			std::cerr << "Failed to write " << dwBufferSizeRead << "bytes. Only wrote " 
				<< dwSizeWrote << std::endl;
			goto cleanup;
		}

	} while (dwTotalSizeRead != dwTotalSizeRead);

	WavIn->Close();
	WavOut->Close();

cleanup:
	delete conv;
	delete pbInputSamples;
	delete pbOutputSamples;
	SAFE_DELETE(WavIn);
	SAFE_DELETE(WavOut);

	return hr;
}

