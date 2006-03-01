#pragma once
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

#include "convolution\config.h"


struct OptimalDFT
{

	static const DWORD HalfLargestDFTSize = 1000000; // allow FFTs up to 2x this size

	static const DWORD OptimalDFTSize[];

	DWORD GetOptimalDFTSize( DWORD size0 );

};

struct PlanningRigour
{
#ifdef FFTW
	enum Degree {Estimate=0, Measure=1, Patient=2, Exhaustive=3, TimeLimited=4};
	static const unsigned int nDegrees = 5;
	static const unsigned int nStrLen = 15;
	static const int nTimeLimit = 30; // 30s -> 1min, for user, as a forward and reverse plan are optimized
#else
	enum Degree {Default=0};
	static const unsigned int nDegrees = 1;
	static const unsigned int nStrLen = 8;
#endif
	TCHAR Rigour[nDegrees][nStrLen];
	unsigned int Flag[nDegrees];

	PlanningRigour()
	{
#ifdef FFTW
		_tcsncpy(Rigour[Estimate], TEXT("Estimate"), nStrLen);		Flag[Estimate] = FFTW_ESTIMATE|FFTW_DESTROY_INPUT;
		_tcsncpy(Rigour[Measure], TEXT("Measure"), nStrLen);		Flag[Measure] = FFTW_MEASURE|FFTW_DESTROY_INPUT;
		_tcsncpy(Rigour[Patient], TEXT("Patient"), nStrLen);		Flag[Patient] = FFTW_PATIENT|FFTW_DESTROY_INPUT;
		_tcsncpy(Rigour[Exhaustive], TEXT("Exhaustive"), nStrLen);	Flag[Exhaustive] = FFTW_EXHAUSTIVE|FFTW_DESTROY_INPUT;
		_tcsncpy(Rigour[TimeLimited], TEXT("Take 1 minute"), nStrLen);	Flag[TimeLimited] = FFTW_PATIENT|FFTW_DESTROY_INPUT;
#else
		_tcsncpy(Rigour[Default], TEXT("Default"), nStrLen);	Flag[0] = 0;
#endif
	}

	unsigned int Lookup(const TCHAR* r) const
	{
		for(unsigned int i=0; i<nDegrees; ++i)
		{
			if(_tcsncicmp(r, Rigour[i], nStrLen) == 0)
				return i;
		}
		throw std::range_error("Invalid rigour");
	}
};
