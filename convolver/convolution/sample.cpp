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

#include "convolution\sample.h"

// Ordering is by preference
bool operator <(const SampleFormatId& x, const SampleFormatId& y)
{
	if (x.wValidBitsPerSample != y.wValidBitsPerSample)
		return x.wValidBitsPerSample > y.wValidBitsPerSample;	// prefer more bits

	if (x.wBitsPerSample != y.wBitsPerSample)
		return x.wBitsPerSample > y.wBitsPerSample;				// prefer more bits

	assert(x.SubType == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT || x.SubType == KSDATAFORMAT_SUBTYPE_PCM);
	assert(y.SubType == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT || y.SubType == KSDATAFORMAT_SUBTYPE_PCM);
	if(x.SubType != y.SubType)
	{
		return x.SubType == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;	// prefer float
	}

	if (x.wFormatTag != y.wFormatTag)
		return x.wFormatTag > y.wFormatTag;						// extensible (0xFFFE) preferred to float (3) or pcm (1)

	return false;												// (x == y)
}