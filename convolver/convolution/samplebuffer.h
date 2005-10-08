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

#include "convolution\config.h"

// Vector is slow when debugging
#include <vector>
#include <malloc.h>
#include <windows.h>	// for ZeroMemory, CopyMemory, etc

// The following undefs are needed to avoid conflicts
#undef min
#undef max

// T is normally float
template <typename T>
class fastarray
{

private:
	class fastarray_impl		// Ensures cleanup after exception
	{
	public:
		explicit fastarray_impl(int size=0) :
		v_(size ? static_cast<T*>(_aligned_malloc(size * sizeof(T), 16)) : NULL),	// 16-byte aligned for SIMD
			//v_(new T[size]), // operator new ensures zero initialization
			size_(size), bsize_(size * sizeof(T))
		{
			if(size && !v_)
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
		assert(impl_.bsize_ == rhs.impl_.bsize_);  
		assert (this != &rhs);
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
//#pragma omp parallel for
#pragma ivdep
#pragma vector aligned
			for (int i = 0; i < impl_.size_; ++i)
				impl_.v_[i] = static_cast<T>(value);				// TODO: optimize int->float
		}
		return *this;
	}

	fastarray<T>& operator=(const T& value)
	{
		{
//#pragma omp parallel for
#pragma ivdep
#pragma vector aligned
			for (int i = 0; i < impl_.size_; ++i)
				impl_.v_[i] = value;
		}
		return *this;
	}

	T  operator[](int n) const
	{ 
		assert(n < impl_.size_ && n >= 0);
		return this->impl_.v_[n];
	}

	T& operator[](int n)
	{ 
		assert(n < impl_.size_ && n >= 0);
		return this->impl_.v_[n];
	}

	int size() const
	{
		return impl_.size_;
	}

	fastarray<T>& operator*= (const T& value) // // TODO: optimize this
	{
		if (value != static_cast<T>(1.0))
		{
//#pragma omp parallel for
#pragma ivdep
#pragma vector aligned
			for (int i = 0; i < impl_.size_; ++i)
				impl_.v_[i] *= value;
		}
		return *this;
	}

	fastarray<T>& operator+= (const fastarray<T>& other)	// TODO: optimize this
	{
		assert(impl_.size_ == other.impl_.size_);
#pragma ivdep
#pragma vector aligned
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
typedef	std::vector< ChannelBuffer > SampleBuffer;
typedef	std::vector< SampleBuffer > PartitionedBuffer;

#if defined(DEBUG) | defined(_DEBUG)
void DumpChannelBuffer(const ChannelBuffer& buffer );
void DumpSampleBuffer(const SampleBuffer& buffer);
void DumpPartitionedBuffer(const PartitionedBuffer& buffer);
#endif
