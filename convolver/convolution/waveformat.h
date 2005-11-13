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

//#include "convolution\config.h"


#ifndef LIBSNDFILE
#include "Common\dxstdafx.h"
#else
#include "libsndfile\sndfile.h"
#endif
#include "convolution\wavefile.h"

#include <string>
#include <sstream>
#include <iomanip>

//#ifdef LIBSNDFILE
std::string waveFormatDescription(const SF_INFO& sf_info, const char* prefix);
//#else
std::string waveFormatDescription(const WAVEFORMATEXTENSIBLE* w, const DWORD samples, const char* prefix);
const std::string channelDescription(const WORD wFormatTag, const DWORD& dwChannelMask, const WORD& nChannels);
//#endif

//// Used to distinguish the different encoding and decoding routines
//class WaveFormatSignature
//{
//public:
//	WORD wFormatTag;
//	WORD wBitsPerSample;
//	WORD wValidBitsPerSample;
//
//	WaveFormatSignature(WORD wFormatTag, WORD wBitsPerSample, WORD wValidBitsPerSample) :
//	wFormatTag(wFormatTag), wBitsPerSample(wBitsPerSample), wValidBitsPerSample(wValidBitsPerSample) {};
//
//};
