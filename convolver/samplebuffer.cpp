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

#include ".\SampleBuffer.h"


template <typename T>
CSampleBuffer<T>::CSampleBuffer(const unsigned short Channels, const DWORD Samples) : nChannels(Channels), nSamples(Samples)
{
	samples = new T*[nChannels];
	for (unsigned short i = 0; i != nChannels; i++)
	{
		samples[i] = new T[nSamples];
		ZeroMemory(samples[i], sizeof(T) * nSamples);
	}
};

template <typename T>
CSampleBuffer<T>::~CSampleBuffer(void)
{

	for (unsigned int i = 0; i != nChannels; i++)
	{
		delete [] samples[i];
	}

	delete [] samples;
};

template <typename T>
CSampleBuffer<T>::CSampleBuffer(const CSampleBuffer& sb) : nChannels(sb.nChannels), nSamples(sb.nSamples) // copy constructor
{
	samples = new T*[nChannels];
	for (unsigned short i = 0; i != nChannels; i++)
	{
		samples[i] = new T[nSamples];
		for (DWORD j = 0; j != nSamples; j++)
			samples[i][j] = sb.samples[i][j];
	}
};

template <typename T>
CSampleBuffer<T>& CSampleBuffer<T>::operator= (const CSampleBuffer& sb) // copy assignment
{
	if (this != &sb)
	{
		this->~CSampleBuffer();

		nChannels = sb.nChannels;
		nSamples = sb.nSamples;

		samples = new T*[nChannels];
		for (unsigned short i = 0; i != nChannels; i++)
		{
			samples[i] = new T[nSamples];
			for (DWORD j = 0; j != nSamples; j++)
				samples[i][j] = sb.samples[i][j];
		}
	}
	return *this;
};
