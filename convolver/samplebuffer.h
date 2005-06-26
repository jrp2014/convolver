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
//


//TODO: use vectors

//#include <vector>
//
// for ZeroMemory, etc
#include <Windows.h>

template <typename T> // T will normally be float, or perhaps double, depending on the DFT routines used
class CSampleBuffer
{
public:

	WORD nChannels;				// number of channels
	DWORD nSamples;				// length of each channel buffer

	T** samples;				// 2d nChannels x nSamples array holding samples
								// TODO: tried this with a vector<T*>, but cpu usage high (in debug)

	CSampleBuffer(const WORD Channels, const DWORD Samples);
	~CSampleBuffer(void);

	CSampleBuffer(const CSampleBuffer& sb);				// copy constructor
	CSampleBuffer& operator= (const CSampleBuffer& sb);	// copy assignment

};

