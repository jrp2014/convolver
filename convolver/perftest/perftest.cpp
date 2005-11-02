// perftest.cpp	: Defines the entry	point for the console application.
//

#include "stdafx.h"
#include "convolution\config.h"

// TODO: fix
#define NCHANNELS 8

#ifdef LIBSNDFILE
#include "libsndfile\sndfile.h"
#else
#include "Common\dxstdafx.h"
#endif
#if defined(DEBUG) || defined(_DEBUG)
#include "debugging\debugging.h"
#endif
#include "debugging\fasttiming.h"
#include "convolution\wavefile.h"
#include "convolution\convolution.h"
#include <iostream>
#include <sstream>


int	_tmain(int argc, _TCHAR* argv[])
{

#if	defined(DEBUG) | defined(_DEBUG)
	debugstream.sink (apDebugSinkConsole::sOnly);
#endif
	HRESULT	hr = S_OK;
	Holder< CConvolution<float> > conv;
	double fElapsedLoad	= 0;
	double fTotalElapsedLoad = 0;
	double fElapsedCalc = 0;
	double fTotalElapsedCalc = 0;
	apHiResElapsedTime t;

	//CWaveFileHandle	FilterWav;

	if (argc !=	4)
	{
		std::wcerr << "Usage: perftest MaxnPartitions nIterations config.txt" << std::endl;
		return 1;
	}

	try
	{
		std::wistringstream	szPartitions(argv[1]);
		int max_nPartitions;
		szPartitions >>	max_nPartitions;

		if (max_nPartitions	< 0 || max_nPartitions > 1000)
		{
			std::cerr << "max_nPartitions (" << max_nPartitions << ") must be between 0 and 1000" << std::endl;
			throw(std::length_error("invalid max_nPartitions"));
		}

		std::wistringstream	szIterations(argv[2]);
		int nIterations;
		szIterations >>	nIterations;
		if (nIterations	< 1 || nIterations > 1000)
		{
			std::wcerr << "nIterations (" << nIterations << ") must be between 1 and 1000" << std::endl;
			throw(std::length_error("invalid nIterations"));
		}

//#ifdef LIBSNDFILE
//		SF_INFO sfinfo;
//		ZeroMemory(&sfinfo, sizeof(SF_INFO));
//		CWaveFileHandle FilterWav(argv[3], SFM_READ, &sfinfo);
//		std::cerr << waveFormatDescription(sfinfo, 	"Filter file format: ") << std::endl;
//#else
//		std::basic_string< _TCHAR >FilterFileName(argv[3],	_tcslen(argv[3]));
//		if(	FAILED(	hr = FilterWav->Open( argv[3], NULL, WAVEFILE_READ ) ) )
//		{
//			std::wcerr << "Failed to open "	<< FilterFileName << " for reading"	<< std::endl;
//			throw(hr);
//		}
//
//		WAVEFORMATEX *pWave	= FilterWav->GetFormat();
//		std::cerr << waveFormatDescription(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pWave), 
//			FilterWav->GetSize() / FilterWav->GetFormat()->nBlockAlign,	"Filter file format: ") << std::endl;
//#endif

		float fAttenuation	= 0;

#ifdef LIBSNDFILE
		std::cout << std::endl << "Partitions\tRate\tSecCalc\tSecLoad\tAttenuation\tFilter Length\tPartition Length\tIteration" << std::endl;
#else
		std::cout << std::endl << "Partitions\tSecCalc\tSecLoad\tAttenuation\tFilter Length\tPartition Length\tIteration" << std::endl;
#endif

		for (int nPartitions = 0; nPartitions <= max_nPartitions; ++nPartitions)
		{
			for (int nIteration = 1; nIteration<=nIterations; ++nIteration)
			{

				t.reset();
				conv = new CConvolution<float>(argv[3], nPartitions == 0 ? 1 : nPartitions); // Used to calculate nPartitionLength
				fElapsedLoad = t.sec();
				fTotalElapsedLoad += fElapsedLoad;
				
				t.reset();
				hr = calculateOptimumAttenuation(fAttenuation, argv[3],	nPartitions);
				fElapsedCalc = t.sec();
				fTotalElapsedCalc += fElapsedCalc;
				
				if (FAILED(hr))
				{
					std::wcerr << "Failed to calculate optimum attenuation (" << std::hex << hr	<< std::dec << ")" << std::endl;
					throw (hr);
				}
				std::cout  << std::setprecision(3) << nPartitions << "\t"
#ifdef LIBSNDFILE
					<< (static_cast<float>(conv->Mixer.Paths[0].filter.sf_FilterFormat.frames * NSAMPLES) / fElapsedCalc) /
						static_cast<float>(conv->Mixer.Paths[0].filter.sf_FilterFormat.samplerate) << "\t"
#endif
					<< fElapsedCalc << "\t" << fElapsedLoad << "\t" 
					<< fAttenuation << "\t" << conv->Mixer.nFilterLength << "\t" 
					<< conv->Mixer.nPartitionLength << "\t" << nIteration  << std::endl;
			}
		}

		std::cout << std::endl << "Total load time: " << fTotalElapsedLoad  << "s" << std::endl;
		std::cout << "Total execution time: " << fTotalElapsedCalc << "s" << std::endl;

	}

	catch (HRESULT& hr) // from CConvolution
	{
		std::wcerr << "Failed to calculate optimum attenuation (" << std::hex << hr	<< std::dec << ")" << std::endl;
		return hr;
	}
	catch(const std::exception& error)
	{
		std::wcerr << "Standard exception: " << error.what() << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const char* what)
	{
		std::wcerr << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const TCHAR* what)
	{
		std::wcerr << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (...)
	{
		std::wcerr << "Failed." <<std::endl;
	}

#if defined(DEBUG) | defined(_DEBUG)
	_CrtDumpMemoryLeaks();
#endif

	return hr;

}
