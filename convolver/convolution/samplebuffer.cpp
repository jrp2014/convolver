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

#include "convolution\samplebuffer.h"

// comparisons
template<class T>
bool operator== (const AlignedArray<T>& x, const AlignedArray<T>& y)
{
    return std::equal(x.begin(), x.end(), y.begin());
}
template<class T>
bool operator< (const AlignedArray<T>& x, const AlignedArray<T>& y)
{
    return std::lexicographical_compare(x.begin(),x.end(),y.begin(),y.end());
}
template<class T>
bool operator!= (const AlignedArray<T>& x, const AlignedArray<T>& y) 
{
    return !(x==y);
}
template<class T>
bool operator> (const AlignedArray<T>& x, const AlignedArray<T>& y)
{
    return y<x;
}
template<class T>
bool operator<= (const AlignedArray<T>& x, const AlignedArray<T>& y) 
{
    return !(y<x);
}
template<class T>
bool operator>= (const AlignedArray<T>& x, const AlignedArray<T>& y)
{
    return !(x<y);
}

// global swap()
template<class T>
inline void swap (AlignedArray<T>& x, AlignedArray<T>& y)
{
	x.swap(y);
}

inline float* restrict c_ptr(const ChannelBuffer& x)
{
	return x.c_ptr();
}

inline float* restrict c_ptr(const SampleBuffer& x, const ChannelBuffer::size_type row)
{
	assert (row < x.size());
	return x[row].c_ptr();
}

inline float* restrict c_ptr(const PartitionedBuffer& x, const SampleBuffer::size_type row, const ChannelBuffer::size_type column)
{
	assert (row < x.size() && (column < x[row].size()));
	//return &x[row][column][0];
	return x[row][column].c_ptr();
}

inline void Zero (ChannelBuffer& x)
{
	x.Zero(0, x.size()); 
}

inline void Zero (SampleBuffer& x)
{
	for(SampleBuffer::size_type i=0; i < x.size(); ++i)
	{
		//x[i] = 0;
		//x[i].Zero(0, x.size());
		Zero(x[i]);
	}
}

inline void Zero (PartitionedBuffer& x)
{
	for(SampleBuffer::size_type i=0; i < x.size(); ++i)
	{
		Zero(x[i]);
	}
}


#if defined(DEBUG) | defined(_DEBUG)
void DumpChannelBuffer(const ChannelBuffer &buffer)
{
	const ChannelBuffer::size_type LIMIT = 16; // Don't print too much
	ChannelBuffer::size_type limit = buffer.size() > LIMIT ? LIMIT : buffer.size();

	for (ChannelBuffer::size_type nSample = 0; nSample < limit; ++nSample)
	{
		cdebug << buffer[nSample] << " ";
	}
}

void DumpSampleBuffer(const SampleBuffer& buffer)
{
	cdebug << "SampleBuffer: " ;

	for (SampleBuffer::size_type nChannel = 0; nChannel < buffer.size(); ++nChannel)
	{
		 cdebug << std::endl << "[Channel " << nChannel << ": "; DumpChannelBuffer(buffer[nChannel]); cdebug << "]";
	}
}

void DumpPartitionedBuffer(const PartitionedBuffer& buffer)
{
	cdebug << "PartitionedBuffer: "  ;
	for (PartitionedBuffer::size_type nPartition = 0; nPartition < buffer.size(); ++nPartition)
	{
		cdebug << "{Partition " << nPartition << ": " ; DumpSampleBuffer(buffer[nPartition]); cdebug << "}" << std::endl; 

	}
}
#endif

