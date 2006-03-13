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

