#pragma once
// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005,2006  John Pavel
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

#include "convolution\config.h"


struct OptimalDFT
{
	static const DWORD HalfLargestDFTSize = 1000000; // allow FFTs up to 2x this size

	static const DWORD OptimalDFTSize[];

	static DWORD GetOptimalDFTSize( DWORD size0 );
};

struct PlanningRigour
{
#ifdef FFTW
	enum Degree {Estimate=0, Measure=1, Patient=2, Exhaustive=3, TimeLimited=4};
	static const unsigned int nDegrees = 5;
	static const unsigned int nStrLen = sizeof(TEXT("Take 1 minute")) / sizeof(TCHAR) + 1;
	static const int nTimeLimit = 30; // 30s -> 1min, for user, as a forward and reverse plan are optimized
#else
	enum Degree {Default=0};
	static const unsigned int nDegrees = 1;
	static const unsigned int nStrLen = 8;
#endif
	static const TCHAR Rigour[nDegrees][nStrLen];
	static const unsigned int Flag[nDegrees];

	static unsigned int Lookup(const TCHAR* r)
	{
		for(unsigned int i=0; i<nDegrees; ++i)
		{
			if(_tcsncicmp(r, Rigour[i], nStrLen) == 0)
				return i;
		}
		throw std::range_error("Invalid rigour");
	}
};

#ifdef FFTW

// The following is a workaround with FFTW is a DLL
static void my_fftwf_write_char(char c, void *f) { fputc(c, (FILE *) f); }
#define fftwf_export_wisdom_to_file(f) fftwf_export_wisdom(my_fftwf_write_char, (void*) (f))

static int my_fftwf_read_char(void *f) { return fgetc((FILE *) f); } 
#define fftwf_import_wisdom_from_file(f) fftwf_import_wisdom(my_fftwf_read_char, (void*) (f))

struct Wisdom
{
	static TCHAR szCurrentDir[MAX_PATH];
	static char szWisdomFileName[MAX_PATH];

	Wisdom()
	{

		if(GetCurrentDirectory(MAX_PATH, szCurrentDir) == 0)
		{
//#if defined(DEBUG) | defined(_DEBUG)
//			cdebug << "Failed to get current Directory" << std::endl;
//#endif
		}

		if (fftwf_init_threads() == 0)
		{
//#if defined(DEBUG) | defined(_DEBUG)
//			cdebug << "Failed to fftwf_init_threads" << std::endl;
//#endif
		}

		FILE* wisdom = fopen (szWisdomFileName,"r");
		if (wisdom!=NULL)
		{
			if(fftwf_import_wisdom_from_file(wisdom) != 1)
			{
// cdebug may not yet be initialized
//#if defined(DEBUG) | defined(_DEBUG)
//			cdebug << "Failed to fftwf_import_wisdom_from_file" << std::endl;
//#endif
			}
			fclose (wisdom);
		}
	}


	~Wisdom()
	{
			SetCurrentDirectory(szCurrentDir);

			FILE* wisdom = fopen (szWisdomFileName,"w");
			if (wisdom!=NULL)
			{
				fftwf_export_wisdom_to_file(wisdom);
				fflush(wisdom);		// needed as WIN32 fclose does not do it as it should
				fclose (wisdom);
			}
			else
			{
#if defined(DEBUG) | defined(_DEBUG)
				cdebug << "Failed to fftwf_export_wisdom_from_file" << std::endl;
#endif
			}
			fftwf_cleanup_threads();
	}
};

#endif
