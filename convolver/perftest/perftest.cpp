// perftest.cpp	: Defines the entry	point for the console application.
//

#include "stdafx.h"

#include "Common\dxstdafx.h"
#include <sstream>
#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#endif
#include "debugging\fasttiming.h"
#include "convolution\wavefile.h"
#include "convolution\convolution.h"

int	_tmain(int argc, _TCHAR* argv[])
{

	HRESULT	hr = S_OK;
	Holder<CConvolution> conv;
	double fElapsedLoad	= 0;
	double fTotalElapsedLoad = 0;
	double fElapsedCalc = 0;
	double fTotalElapsedCalc = 0;
	apHiResElapsedTime t;

	CWaveFileHandle	FilterWav;

#if	defined(DEBUG) | defined(_DEBUG)
	debugstream.sink (apDebugSinkConsole::sOnly);
#endif

	if (argc !=	4)
	{
		std::wcerr << "Usage: perftest MaxnPartitions nIterations filter.wav" << std::endl;
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

#ifdef LIBSNDFILE
		SF_INFO sfinfo;
		ZeroMemory(&sfinfo, sizeof(SF_INFO));
		CWaveFileHandle FilterWav(argv[3], SFM_READ, &sfinfo);
		std::cerr << waveFormatDescription(sfinfo, 	"Filter file format: ") << std::endl;
#else
		std::basic_string< _TCHAR >FilterFileName(argv[3],	_tcslen(argv[3]));
		if(	FAILED(	hr = FilterWav->Open( argv[3], NULL, WAVEFILE_READ ) ) )
		{
			std::wcerr << "Failed to open "	<< FilterFileName << " for reading"	<< std::endl;
			throw(hr);
		}

		WAVEFORMATEX *pWave	= FilterWav->GetFormat();
		std::cerr << waveFormatDescription(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pWave), 
			FilterWav->GetSize() / FilterWav->GetFormat()->nBlockAlign,	"Filter file format: ") << std::endl;
#endif

		float fAttenuation	= 0;

		std::cout << std::endl << "Partitions\tSecCalc\tSecLoad\tAttenuation\tFilter Length\tPartition Length\tIteration" << std::endl;

		for (int nPartitions = 0; nPartitions <= max_nPartitions; ++nPartitions)
		{
			for (int nIteration = 1; nIteration<=nIterations; ++nIteration)
			{

				t.reset();
#ifdef LIBSNDFILE
				conv = new Cconvolution_ieeefloat(argv[3], nPartitions == 0 ? 1 : nPartitions); // Sets conv.	nPartitions==0 => use overlap-save
#else
				hr = SelectConvolution(pWave, conv,	argv[3], nPartitions ==	0 ?	1 :	nPartitions); // Sets conv.	nPartitions==0 => use overlap-save
				if (FAILED(hr))
				{
					std::wcerr << "Failed to select filter " << FilterFileName << " (" << std::hex << hr	<< std::dec << ")" << std::endl;
					throw(hr);
				}
#endif
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
				std::cout  << nPartitions << "\t"  << fElapsedCalc << "\t" << fElapsedLoad << "\t" 
					<< fAttenuation << "\t" << conv->FIR.nFilterLength << "\t" 
					<< conv->FIR.nPartitionLength << "\t" << nIteration  << std::endl;
			}
		}

		std::cout << std::endl << "Total load time: " << fTotalElapsedLoad  << "s" << std::endl;
		std::cout << "Total execution time: " << fTotalElapsedCalc << "s" << std::endl;

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
