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

#ifdef FASTARRAY

//#ifdef FFTW
//#define ALIGNED_MALLOC(bytes) fftwf_malloc(bytes)
//#define ALIGNED_FREE(ptr) fftwf_free(ptr)
#if defined(__ICC) || defined(__INTEL_COMPILER)
#define ALIGNED_MALLOC(bytes) _mm_malloc(bytes, 16)
#define ALIGNED_FREE(ptr) _mm_free(ptr)
#else // Microsoft
#define ALIGNED_MALLOC(bytes) _aligned_malloc(bytes, 16)
#define ALIGNED_FREE(ptr) _aligned_free(ptr)
#endif


// T is normally float
template <typename T>
class fastarray
{
protected:
	class fastarray_impl		// Ensures cleanup after exception
	{
	public:
		explicit fastarray_impl(const int& size=0) :
		v_(static_cast<T*>(ALIGNED_MALLOC(size * sizeof(T)))),		// 16-byte aligned for SIMD
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
			ALIGNED_FREE(v_);
			//delete [] (v_);
		};

		void Swap(fastarray_impl& other)
		{
			std::swap(v_, other.v_);
			std::swap(size_, other.size_);
			std::swap(bsize_, other.bsize_);
		}

		T* v_;
		int size_;		// size in elements
		int bsize_;	// size in bytes

	private:
		// No copying allowed
		fastarray_impl(const fastarray_impl&);
		fastarray_impl(fastarray_impl&);                // discourage use of lvalue fastarray_impls
	};

	fastarray_impl impl_;	// private implementation

public:
	explicit fastarray(const int& size = 0) : impl_(size) {};

	explicit fastarray(const fastarray<T>& other) : impl_(other.impl_.size_)
	{
		assert(impl_.bsize_ == other.impl_.bsize_);
		::CopyMemory(impl_.v_, other.impl_.v_, other.impl_.bsize_); // memcpy => overlap not allowed
	}

	operator T* () const
	{
		return impl_.v_;
	}

	const T& operator[](int n) const
	{ 
		assert(n < impl_.size_ && n >= 0);
		return *(impl_.v_ + n);
	}

	T& operator[](int n)
	{ 
		assert(n < impl_.size_ && n >= 0);
		return *(impl_.v_+ n);
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
		//#pragma omp parallel for
#pragma ivdep
#pragma vector aligned
		for (int i = 0; i < impl_.size_; ++i)
			impl_.v_[i] = value;
		return *this;
	}

	fastarray<T>& operator=(const fastarray<T>& rhs)
	{
		if (this != &rhs)
		{
			if (impl_.bsize_ == rhs.impl_.bsize_)
			{
				::CopyMemory(impl_.v_, rhs.impl_.v_, impl_.bsize_); // memcpy => overlap not allowed
			}
			else
			{
				fastarray<T> temp(rhs);
				impl_.Swap(temp.impl_);
			}
		}
		return *this;
	}

	void shiftleft(const int n)
	{
		::MoveMemory(&impl_.v_[0], &impl_.v_[n], n * sizeof(T));
	}

	int size() const
	{
		return impl_.size_;
	}

	fastarray<T>& operator*= (const float& value) // // TODO: optimize this
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

private:
	fastarray(fastarray<T>&);                // discourage use of lvalue fastarrays
};

// The following experiment needs a proper copy constuctor to work, as filter needs one
// It is not exception safe, as fastarray is, as it stands.
//// T is normally float
//template <typename T>
//class fastarray2 : protected fastarray<fastarray<T>*>
//{
//private:
//	T* v_;
//	int sizex_, sizey_;
//	bool allocated_here_;
//
//public:
//
//	explicit fastarray2(const int& sizex=0, const int& sizey=0, T* values=NULL) : 
//	fastarray<fastarray<T>*>(sizex), 
//		v_(values ? values : new T[sizex * sizey]), allocated_here_(values == NULL), 
//		sizex_(sizex), sizey_(sizey)
//
//	{
//		assert(sizex >= 0 &&  sizey >=0);
//
//		cdebug << ">fastarray2 " << sizex_ << " " << sizey_ << " " << allocated_here_ << std::endl;
//
//		T* ptr = v_;
//		for(int i = 0; i<sizex; ++i, ptr += sizey)
//		{
//			fastarray<fastarray<T>*>::operator[](i) = new fastarray<T>(sizey, ptr);
//		}
//
//		cdebug << "<fastarray2 " << sizex_ << " " << sizey_ << " " << allocated_here_ << std::endl;
//
//	}
//
//	virtual ~fastarray2()
//	{
//		cdebug << ">~fastarray2 " << sizex_ << " " << sizey_ << " " << allocated_here_ << std::endl;
//
//		for(int i = 0; i<sizex_; ++i)
//		{
//			delete fastarray<fastarray<T>*>::operator[](i);
//		}
//
//		if(allocated_here_)
//			delete[] v_;
//
//		cdebug << "<~fastarray2 " << sizex_ << " " << sizey_ << " " << allocated_here_ << std::endl;
//	}
//
//	const fastarray<T>& operator[](const int& n) const
//	{
//		assert (n >=0 && n <= sizex_);
//		return *(fastarray<fastarray<T>*>::operator[](n));
//	}
//
//	fastarray<T>& operator[](const int& n)
//	{
//		assert (n >=0 && n <= sizex_);
//		return *(fastarray<fastarray<T>*>::operator[](n));
//	}
//
//	int size() const
//	{
//		return sizex_;
//	}
//	private:
//		// No copying allowed
//		fastarray2(const fastarray2&);
//		void operator=(const fastarray2&);
//		fastarray2(fastarray2&);                // discourage use of lvalue fastarray3
//};
//
//// T is normally float
//template <typename T>
//class fastarray3 :protected fastarray<fastarray2<T>*>
//{
//private:
//	T* v_;
//	int sizex_, sizey_, sizez_;
//
//public:
//
//	explicit fastarray3(const int& sizex=0, const int& sizey=0, const int& sizez=0) : 
//	fastarray<fastarray2<T>*>(sizex), sizex_(sizex), sizey_(sizey), sizez_(sizez)
//	{
//		assert(sizex >= 0 &&  sizey >=0 && sizez >= 0);
//
//		cdebug << ">fastarray3 " << sizex_ << " " << sizey_ << " " << sizez_ << std::endl;
//
//		v_ = new T[sizex * sizey * sizez];
//
//		T* ptr = v_;
//		for(int i = 0; i<sizex; ++i, ptr += sizey * sizez)
//		{
//			fastarray<fastarray2<T>*>::operator[](i) = new fastarray2<T>(sizey, sizez, ptr);
//		}
//
//		cdebug << "<fastarray3 " << sizex_ << " " << sizey_ << " " << sizez_ << std::endl;
//	}
//
//	virtual ~fastarray3()
//	{
//
//		cdebug << ">~fastarray3 " << sizex_ << " " << sizey_ << " " << sizez_ << std::endl;
//
//		for(int i = 0; i<sizex_; ++i)
//		{
//			delete fastarray<fastarray2<T>*>::operator[](i);
//		}
//
//		delete[] v_;
//
//		cdebug << "<~fastarray3 " << sizex_ << " " << sizey_ << " " << sizez_ << std::endl;
//	}
//
//	const fastarray2<T>& operator[](const int& n) const
//	{
//		assert (n >=0 && n <= sizex_);
//		return *(fastarray<fastarray2<T>*>::operator[](n));
//	}
//
//	fastarray2<T>& operator[](const int& n)
//	{
//		assert (n >=0 && n <= sizex_);
//		return *(fastarray<fastarray2<T>*>::operator[](n));
//	}
//
//	int size() const
//	{
//		return sizex_;
//	}
//	private:
//		// No copying allowed
//		fastarray3(const fastarray3&);
//		void operator=(const fastarray3&);
//		fastarray3(fastarray3&);                // discourage use of lvalue fastarray3
//};



typedef	fastarray<float> ChannelBuffer;
#ifdef UNDEF
typedef	__declspec(align(16)) fastarray2<float> SampleBuffer;
typedef	__declspec(align(16)) fastarray3<float> PartitionedBuffer;
#else
typedef	std::vector< ChannelBuffer >  SampleBuffer;
typedef	std::vector< SampleBuffer >  PartitionedBuffer;
#endif


#else

// None of these are suitable
//#include "./Array.h"
//
//typedef Array::array1<float>::opt restrict ChannelBuffer;
//typedef Array::array2<float> restrict SampleBuffer;
//typedef Array::array3<float> restrict PartitionedBuffer;

#include "stlsoft_fixed_array.h"
typedef stlsoft::fixed_array_1d<float> restrict ChannelBuffer;
typedef stlsoft::fixed_array_2d<float> restrict SampleBuffer;
typedef stlsoft::fixed_array_3d<float> restrict PartitionedBuffer;

#endif

#if defined(DEBUG) | defined(_DEBUG)
void DumpChannelBuffer(const ChannelBuffer& buffer );
void DumpSampleBuffer(const SampleBuffer& buffer);
void DumpPartitionedBuffer(const PartitionedBuffer& buffer);
#endif
