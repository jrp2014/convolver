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

#pragma once


#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#include "debugging\debugStream.h"
#endif

// Pull in Common DX classes
#include "Common\dxstdafx.h"

#include "convolution\samplebuffer.h"
#include "convolution\waveformat.h"
#include "convolution\wavefile.h"
#include "fft\fftsg_h.h"

const DWORD MAX_FILTER_SIZE = 100000000; // Max impulse size.

class Filter
{
public:

	int						nChannels;				// number of channels
	PartitionedBuffer		buffer;
	WAVEFORMATEXTENSIBLE	wfexFilterFormat;		// The format of the filter file
	const int				nPartitions;
	int						nPartitionLength;		// in blocks (a block contains the samples for each channel)
	int						nHalfPartitionLength;	// in blocks
	int						nFilterLength;			// nFilterLength = nPartitions * nPartitionLength

	Filter(TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions);

private:
	Filter(const Filter& other); // no impl.
	void operator=(const Filter& other); // no impl.
	Filter(Filter& other);                // discourage use of lvalue Filters
};
