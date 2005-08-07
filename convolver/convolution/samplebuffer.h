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

#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugStream.h"
#endif

// Vector is slow when debugging
// The following undefs are needed to avoid valarray conflicts
#undef min
#undef max
#include <valarray>

#include <vector>

typedef	std::valarray<float> ChannelBuffer;
typedef	std::vector< ChannelBuffer > SampleBuffer;
typedef	std::vector< SampleBuffer > PartitionedBuffer;

void DumpChannelBuffer(const ChannelBuffer& buffer );
void DumpSampleBuffer(const SampleBuffer& buffer);
void DumpPartitionedBuffer(const PartitionedBuffer& buffer);

//template <typename FFT_type> // FFT_type will normally be float, or perhaps double, depending on the DFT routines used
//class ChannelBuffer
//{
//public:
//	DWORD nSamples;				// length of each channel buffer
//
//	std::vector<FFT_type> samples;
//
//	explicit ChannelBuffer(const DWORD nSamples) : nSamples(nSamples), samples(std::vector<FFT_type>(nSamples))
//	{
//#if defined(DEBUG) | defined(_DEBUG)
//		cdebug << "new ChannelBuffer " << nSamples << std::endl;
//#endif
//	};
//
////	~ChannelBuffer()
////	{
////#if defined(DEBUG) | defined(_DEBUG)
////		cdebug << "deleted ChannelBuffer " << std::endl;
////#endif
////	}
//
//#if defined(DEBUG) | defined(_DEBUG)
//	void DumpBuffer() const;
//#endif
//
//private:
//	//ChannelBuffer(const ChannelBuffer&);
//	//ChannelBuffer& operator=(const ChannelBuffer&);
//};
//
//template <typename FFT_type> // FFT_type will normally be float, or perhaps double, depending on the DFT routines used
//class SampleBuffer
//{
//public:
//	WORD nChannels;				// number of channels
//
//	std::vector< ChannelBuffer<FFT_type> > channels;
//
//	explicit SampleBuffer(const WORD nChannels, const DWORD nSamples) : nChannels(nChannels), 
//		channels(std::vector< ChannelBuffer<FFT_type> >(nChannels, ChannelBuffer<FFT_type>(nSamples)))
//	{
//#if defined(DEBUG) | defined(_DEBUG)
//		cdebug << "new SampleBuffer " << nChannels << " " << nSamples << std::endl;
//#endif
//		//channels.reserve(nChannels);
//		//for(WORD nChannel = 0; nChannel != nChannels; ++nChannel)
//		//{
//		//	channels.push_back(ChannelBuffer<FFT_type>(nSamples));
//		//}
//	};
//
////	~SampleBuffer()
////	{
////#if defined(DEBUG) | defined(_DEBUG)
////		cdebug << "deleted SampleBuffer" << std::endl;
////#endif
////	}
//
//#if defined(DEBUG) | defined(_DEBUG)
//	void DumpBuffer() const;
//#endif
//
//private:
//	//SampleBuffer(const SampleBuffer&);
//	//SampleBuffer& operator=(const SampleBuffer&);
//};
//
//template <typename FFT_type> // FFT_type will normally be float, or perhaps double, depending on the DFT routines used
//class PartitionedBuffer
//{
//public:
//	DWORD nPartitions;				// number of partitions
//
//	std::vector< SampleBuffer<FFT_type> > partitions;
//
//	explicit PartitionedBuffer(DWORD nPartitions) : nPartitions(nPartitions)
//	{
//#if defined(DEBUG) | defined(_DEBUG)
//		cdebug << "new empty Parititioned Buffer" << std::endl;
//#endif
//		partitions.reserve(nPartitions);
//	}
//
//	explicit PartitionedBuffer(DWORD nPartitions, WORD nChannels, DWORD nSamples) : nPartitions(nPartitions),
//		partitions(std::vector< SampleBuffer<FFT_type> >(nPartitions, SampleBuffer<FFT_type>(nChannels, nSamples)))
//	{
//#if defined(DEBUG) | defined(_DEBUG)
//		cdebug << "new Parititioned Buffer " << nPartitions << " " << nChannels << " " << nSamples << std::endl;
//#endif
//		//partitions.reserve(nPartitions);
//		//for (DWORD nPartition = 0; nPartition != nPartitions; ++nPartition)
//		//{
//		//	partitions.push_back(SampleBuffer<FFT_type>(nChannels, nSamples));
//		//}
//	};
//
////	~PartitionedBuffer()
////	{
////#if defined(DEBUG) | defined(_DEBUG)
////		cdebug << "deleted ParititionedBuffer " << std::endl;
////#endif
////	}
//
//#if defined(DEBUG) | defined(_DEBUG)
//	void DumpBuffer() const;
//#endif
//
//private:
//	//PartitionedBuffer(const PartitionedBuffer&);
//	//PartitionedBuffer& operator=(const PartitionedBuffer&);
//};
