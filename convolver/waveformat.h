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

#include <mmreg.h>

class WaveFormat
{
public:
	const WORD  wFormatTag; 
    const WORD  nChannels;
	const WORD  wBitsPerSample;
	const WORD  wValidBitsPerSample;

	~WaveFormat(void);
	WaveFormat(WAVEFORMATEX* pWave): 
		wFormatTag(pWave->wFormatTag),
		nChannels(pWave->nChannels),
		wBitsPerSample(wBitsPerSample),
		wValidBitsPerSample(((WAVEFORMATEXTENSIBLE *)pWave)->Samples.wValidBitsPerSample)
	{	};


	// invalid for compressible audio types
	// See http://msdn.microsoft.com/library/default.asp?url=/library/en-us/multimed/htm/_win32_waveformatextensible_str.asp
//	bool WaveFormat::operator<(WaveFormat& wf1, WaveFormat& wf2);
};

	//bool WaveFormat::operator<(WaveFormat& wf1, WaveFormat& wf2)
	//{
	//	return (wf1.wFormatTag == wf1.wFormatTag && 
	//		wf1.nChannels < wf1.nChannels &&
	//		wf1.wValidBitsPerSample < wf1.wBitsPerSample);
	//};

	typedef WaveFormat WaveFormat_t;