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
#include <assert.h>

// Vector is slow when debugging
#include <vector>

// A range of different buffer implementations ...

// std::valarray
#undef VALARRAY
#define FASTARRAY 1

#ifdef	VALARRAY
// The following undefs are needed to avoid valarray conflicts
#undef min
#undef max
#include <valarray>
typedef	std::valarray<float> ChannelBuffer;
#endif

#ifdef FASTARRAY
#include <malloc.h>
#include <windows.h>	// for ZeroMemory, CopyMemory, etc

// The following undefs are needed to avoid conflicts
#undef min
#undef max

template <typename T>
class fastarray
{
	
private:
	class fastarray_impl		// Ensures cleanup after exception
	{
	public:
		explicit fastarray_impl(int size=0) :
		  v_(static_cast<T*>(_aligned_malloc(size * sizeof(T), 16))),	// 16-byte aligned for SIMD
			  //v_(new T[size]), // operator new ensures zero initialization
			  size_(size), bsize_(size * sizeof(T))
		  {
			  if(!v_)
			  {
				  throw(std::bad_alloc());
			  }
		  };

		  ~fastarray_impl()
		  {
			  _aligned_free(v_);
			  //delete [] (v_);
		  };

		  T* v_;
		  int size_;		// size in elements
		  int bsize_;	// size in bytes

	private:
		// No copying allowed
		fastarray_impl(const fastarray_impl&);
		fastarray_impl& operator=(const fastarray_impl&);
		fastarray_impl(fastarray_impl&);                // discourage use of lvalue fastarray_impls
	};

	fastarray_impl impl_;	// private implementation

public:
	fastarray(int size = 0) : impl_(size) {};

	explicit fastarray(const fastarray<T>& other) : impl_(other.impl_.size_)
	{
		::CopyMemory(impl_.v_, other.impl_.v_, other.impl_.bsize_); // memcpy => overlap not allowed
	}

	fastarray<T>& operator=(const fastarray<T>& rhs)
	{
		//assert(impl_.bsize_ == rhs.impl_.bsize_);  
		//if (this != &rhs) // 
		::CopyMemory(impl_.v_, rhs.impl_.v_, impl_.bsize_); // memcpy => overlap not allowed
		return *this;
	}

	void shiftright(const int n)
	{
		::MoveMemory(&impl_.v_[n], impl_.v_, n * sizeof(T));
	}

	fastarray<T>& operator=(const int& value)
	{
		if( value == 0)
		{
			::ZeroMemory(impl_.v_, impl_.bsize_);
		}
		else
		{
			for (int i = 0; i < impl_.size_; ++i)
				impl_.v_[i] = static_cast<T>(value);				// TODO: optimize int->float
		}
		return *this;
	}

	fastarray<T>& operator=(const T& value)
	{
		{
			for (int i = 0; i < impl_.size_; ++i)
				impl_.v_[i] = value;
		}
		return *this;
	}

	T  operator[](int n) const
	{ 
		//assert(n < impl_.size_ && n >= 0);;
		return this->impl_.v_[n];
	}

	T& operator[](int n)
	{ 
		//assert(n < impl_.size_ && n >= 0);
		return this->impl_.v_[n];
	}

	int size() const
	{
		return impl_.size_;
	}

	fastarray<T>& operator*= (const T& value) // // TODO: optimize this
	{
		for (int i = 0; i < impl_.size_; ++i)
			impl_.v_[i] *= value;
		return *this;
	}

	// No longer needed
	fastarray<T>& operator+= (const fastarray<T>& other)	// TODO: optimize this
	{
		for (int i = 0; i < this->size(); ++i)
			impl_.v_[i] += other.impl_.v_[i];
		return *this;
	}

	T min () const
	{
		T result = 0;
		for (int i = 0; i < impl_.size_; ++i)
			if (result > impl_.v_[i])
				result = impl_.v_[i];
		return result;
	}

	T max () const
	{
		T result = 0;
		for (int i = 0; i < impl_.size_; ++i)
			if (result < impl_.v_[i])
				result = impl_.v_[i];
		return result;
	}

private:
	fastarray(fastarray<T>&);                // discourage use of lvalue fastarrays
};

typedef	fastarray<float> ChannelBuffer;

#endif

// Don't use fastarray, as the fastarray assumes a fundamental type: it doesn't use contructors
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
