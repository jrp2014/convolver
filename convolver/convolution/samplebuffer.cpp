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

#include "samplebuffer.h"

#if defined(DEBUG) | defined(_DEBUG)
void DumpChannelBuffer(const ChannelBuffer& buffer )
{
	const size_t LIMIT = 16; // Don't print too much
	size_t limit = buffer.size() > LIMIT ? LIMIT : buffer.size();

	for (int nSample = 0; nSample < limit; ++nSample)
	{
		cdebug << buffer[nSample] << " ";
	}

	if (buffer.size() > LIMIT)
	{
		cdebug << "... " << std::endl << "Size: " << buffer.size() << ", Max: " << buffer.max() << ", Min: " << buffer.min();
	}
};

void DumpSampleBuffer(const SampleBuffer& buffer)
{
	for (int nChannel = 0; nChannel < buffer.size(); ++nChannel)
	{
		 cdebug << std::endl << "[" << nChannel << ": "; DumpChannelBuffer(buffer[nChannel]); cdebug << "]";
	}
};

void DumpPartitionedBuffer(const PartitionedBuffer& buffer)
{
	
	for (int nPartition = 0; nPartition < buffer.size(); ++nPartition)
	{
		cdebug << "{" << nPartition << ": " ; DumpSampleBuffer(buffer[nPartition]); cdebug << "}" << std::endl; 

	}
};
#endif


//template <typename T>
//CSampleBuffer<T>::CSampleBuffer(const WORD Channels, const DWORD Samples) : nChannels(Channels), nSamples(Samples)
//{
//	samples = new T*[nChannels];
//	for (WORD nChannel = 0; nChannel != nChannels; nChannel++)
//	{
//		samples[nChannel] = new T[nSamples];
//		ZeroMemory(&samples[nChannel], sizeof(T) * nSamples);
//	}
//};
//
//template <typename T>
//CSampleBuffer<T>::~CSampleBuffer(void)
//{
//
//	for (WORD nChannel = 0; nChannel != nChannels; nChannel++)
//	{
//		delete [] samples[nChannel];
//	}
//
//	delete [] samples;
//};
//
//template <typename T>
//CSampleBuffer<T>::CSampleBuffer(const CSampleBuffer& sb) : nChannels(sb.nChannels), nSamples(sb.nSamples) // copy constructor
//{
//	samples = new T*[nChannels];
//	for (WORD nChannel = 0; nChannel != nChannels; nChannel++)
//	{
//		samples[nChannel] = new T[nSamples];
//		for (DWORD nSample = 0; nSample != nSamples; nSample++)
//			samples[nChannel][nSample] = sb.samples[nChannel][nSample];
//	}
//};
//

//

