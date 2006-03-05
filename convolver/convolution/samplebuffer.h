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
#include <malloc.h>
//#include <windows.h>    // for ZeroMemory, CopyMemory, etc
#include <iterator>
#include <vector>
#include <algorithm>

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


// Use vectors of vecors/arrays, as we need aligned data at the base.
// Using projections onto a 1d array would not guarantee alignment other than of the first row

// A simple aligned array
template <class T>
class AlignedArray
{
public:
	typedef T					value_type;
	typedef std::size_t			size_type;

	// Constructors
	AlignedArray() :   first_(NULL), size_(0), bsize_(0)  {}

	explicit AlignedArray(const size_type& n) : first_(NULL), size_(0), bsize_(0)
	{
		if (n != 0) {
			first_ = static_cast<T*>(ALIGNED_MALLOC(n * sizeof(T)));
			if (first_ == NULL) {
				throw std::bad_alloc();
			}
			size_  = n;
			bsize_ = n * sizeof(T);
		}
		else {
			first_ = NULL;
			size_ = 0;
			bsize_ = 0;
		}   
	}

	// Destructor
	virtual ~AlignedArray()
	{   
		ALIGNED_FREE(first_);
		first_ = NULL;
		size_ = 0;
		bsize_ = 0;
	}

protected:

#if defined(__ICC) || defined(__INTEL_COMPILER)
	_declspec(align(16)) T* restrict first_;
#else
	T* restrict first_;
#endif
	size_type size_;        // length in elements
	size_type bsize_;       // length in bytes

private:

	// No copying
	AlignedArray(const AlignedArray<T>&);
	AlignedArray& operator=(const AlignedArray<T>&);
};

// comparisons
template<class T>
bool operator== (const AlignedArray<T>& x, const AlignedArray<T>& y);
template<class T>
bool operator< (const AlignedArray<T>& x, const AlignedArray<T>& y);
template<class T>
bool operator!= (const AlignedArray<T>& x, const AlignedArray<T>& y);
template<class T>
bool operator> (const AlignedArray<T>& x, const AlignedArray<T>& y);
template<class T>
bool operator<= (const AlignedArray<T>& x, const AlignedArray<T>& y);
template<class T>
bool operator>= (const AlignedArray<T>& x, const AlignedArray<T>& y);

// global swap()
template<class T>
inline void swap (AlignedArray<T>& x, AlignedArray<T>& y);

template <typename T> 
struct FastArray : public AlignedArray<T>
{

	// type definitions

	typedef typename AlignedArray<T>::value_type		value_type;

	typedef value_type*									pointer;
	typedef const value_type*							const_pointer;
	typedef value_type*									iterator;
	typedef const value_type*							const_iterator;
	typedef value_type&									reference;
	typedef const value_type&							const_reference;
	typedef typename AlignedArray<T>::size_type			size_type;
	typedef std::ptrdiff_t								difference_type;

	// iterator support
	iterator begin() { return first_; }
	const_iterator begin() const { return first_; }
	iterator end() { return first_ + size_; }
	const_iterator end() const { return first_ + size_; }

	// reverse iterator support
	typedef std::reverse_iterator<iterator> reverse_iterator;
	typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

	reverse_iterator rbegin() { return reverse_iterator(end()); }
	const_reverse_iterator rbegin() const
	{
		return const_reverse_iterator(end());
	}
	
	reverse_iterator rend() { return reverse_iterator(begin()); }
	const_reverse_iterator rend() const
	{
		return const_reverse_iterator(begin());
	}

	// Basic constructors
	FastArray() : AlignedArray<T>() {}
	explicit FastArray(const size_type n) : AlignedArray<T>(n)
	{ 
		// TODO: we probably don't need to do this
		std::uninitialized_fill_n(first_, size_, 0);
	}
	FastArray(const_reference x, const size_t n) : AlignedArray<T>(n)
	{ 
		std::uninitialized_fill_n(first_, size_, x);
	}
	FastArray(const_pointer p, const size_t n) : AlignedArray<T>(n)
	{
		std::uninitialized_copy(p, p + n, first_);
	}
	FastArray(const FastArray<T>& other) : AlignedArray<T>(other.size_)
	{
		std::uninitialized_copy(other.first_, other.first_ + other.size_,
			first_);
	}

	virtual ~FastArray() {};

	// Element access

	// operator[]
	reference restrict operator[](const size_type n)
	{
		assert(n >= 0 && n < size_);
		return *(first_ + n);
	}

	const_reference operator[](const size_type n) const
	{
		assert(n >= 0 && n < size_);
		//return *(first_ + n);
		return first_[n];
	}

	// at() with range check
	reference restrict at(const size_type n)
	{
		rangecheck(i);
		//return *(first_ + n);
		return first_[n];
	}

	const_reference at(const size_type n) const
	{
		rangecheck(n);
		//return *(first_ + n);
		return first_[n];
	}

	// front() and back()
	reference front() { return *first_; }
	const_reference front() const { return *first_; }
	reference back() 
	{
		assert(!empty());
		return *(first_ + size_ - 1);
	}
	const_reference back() const
	{
		assert(!empty());
		return *(first_ + size_ - 1);
	}

	// size is constant
	size_type size() const { return size_; }
	size_type bsize() const { return bsize_; }
	bool empty() const { return size_ == 0; }
	size_type max_size() const { return size_; }
	size_type static_size() const { return size_; }

	// For use with C interface
	pointer restrict c_ptr() const
	{
		return empty() ? NULL : first_;
	}

	// swap (note: linear complexity in N, constant for given instantiation)
	//void swap (FastArray<T>& y) {
	//	std::swap_ranges(begin(),end(),y.begin());
	//}

	// Helper for =
	void swap(reference x)
	{
		std::swap(first_, x.first_);
		std::swap(size_, x.size_);
		std::swap(bsize_, x.bsize_);
	}

	// direct access to data (read-only)
	const_pointer data() const { return first_; }

	// use array as C array (direct read/write access to data)
	pointer data() { return first_; }

	// assign one value to all elements
	void assign (const T x)
	{
		std::fill_n(begin(), size(), x);
	}

	// Basic assignment.  Note that assertions are stricter than necessary, as unequally-size
	// arrays will be assigned correctly
	FastArray<T>& operator=(const FastArray<T>& other)
	{
#ifdef FFTW
		// Don't demand equality as arrays for FFTW are a bit longer to hold complex transforms
		assert(size() == other.size() || size() == 2*(other.size()/2+1));
#else
		assert(size_ == other.size_);
#endif
		//if (this != &other)
		//{
		//	FastArray<T> temp(other);
		//	swap(temp);
		//}
		if( this != &other)
			std::uninitialized_copy(other.first_, other.first_ + other.size_, first_);
		return *this;
	}

	// assignment with type conversion
	template <typename T2>
	FastArray<T>& operator= (const FastArray<T2>& rhs)
	{
#ifdef FFTW
		assert(size() == rhs.size() || size() == 2*(rhs.size()/2+1));
#else
		assert(size() == rhs.size());
#endif
		std::uninitialized_copy(rhs.begin(), rhs.end(), begin());
		return *this;
	}

	// Scalar assignment

	FastArray<T>& operator=(const T x)
	{
		std::uninitialized_fill_n(first_, size_, x);
//		if(x == 0)
//		{
//			::ZeroMemory(first_, bsize_);
//		}
//		else
//		{
//			const size_type ss = size_;						// Improve chances of vectorization
//#pragma loop count(65536)
//#pragma ivdep
//#pragma vector aligned
//			for (size_type i = 0; i < ss; ++i)
//				(*this)[i] = static_cast<T>(x);             // TODO: optimize int->float
//		}
		return *this;
	}

	// Scalar computed assignment.
	FastArray<T>& operator*= (const T x)
	{
		if(x != T(1))
		{
//			const size_type ss = size_;
//#pragma loop count(65536)
//#pragma ivdep
//#pragma vector aligned
//			for (size_type i = 0; i < ss; ++i)
//				(*this)[i] *= x;

			const pointer last = first_ + size_;
			for(pointer p = first_; p != last;)
			{
				*p++ *= x;
			}
		}
		return *this;
	}

	FastArray<T>& operator+= (const T x)
	{
		if(x != 0)
		{
//			const size_type ss = size_;
//#pragma loop count(65536)
//#pragma ivdep
//#pragma vector aligned
//			for (size_type i = 0; i < ss; ++i)
//				(*this)[i] += x;

			const pointer last = first_ + size_;
			for(pointer p = first_; p != last)
			{
				*p++ += x;
			}

		}
		return *this;
	}

	template <typename T2>
		FastArray<T>& operator+= (const FastArray<T2>& rhs)
	{
#ifdef FFTW
		assert(size() == rhs.size() || size() == 2*(rhs.size()/2+1));
#else
		assert(size() == rhs.size());
#endif
		// Optimization
//		const size_type rhs_size = rhs.size();
//#pragma loop count(65536)
//#pragma ivdep
//#pragma vector aligned
//		for (size_type i = 0; i < rhs_size; ++i)
//			(*this)[i] += rhs[i];
		const pointer last = first_ + size_;
		for(pointer p = first_, q = rhs.first_; p != last;)
		{
			*p++ += *q++;
		}

		return *this;
	}

	// Member functions

	void ShiftLeft(const size_type dst, const size_type src, const size_type len)
	{
		assert(src > 0);
		assert(src + len <= size());
		// void CopyMemory(PVOID Destination, const VOID* Source, SIZE_T Length);
		if(len > 0)
		{
			assert(dst + len < src + 1);  // No overlapping
			::CopyMemory(first_ + dst, first_ + src, len * sizeof(T));
		}
	}

	void Zero(const size_type start, const size_type len)
	{
		assert(start + len <= size());
		::ZeroMemory(first_ + start, len * sizeof(T));
	}

private:
	// check range (may be private because it is static)
	static void rangecheck (size_type n)
	{
		if (n < 0 || n >= size())
		{ 
			throw std::out_of_range("FastArray<>: index out of range");
		}
	}
};

// The following is unused, but could provide a basis for using std::vector instead of FastArray as a basis

//COTD Entry by James Johnson [kmeson@telocity.com]
///////////////////////////////////////////////////////////////////////////////
// Class:           aligned_BlockAlloc
// Base Classes:    std::allocator<T>
// Type:            template class
// Inheritance:     public
// Desc:            implements a block aligned allocator. Allocation occurs on
//                  T_ALIGNMENT boundary. This boundary must be a power of 2.
//                  The minimum block size allocated is T_BLOCKSIZE and must be
//                  non-ZERO. Allocations smaller than	T_BLOCKSIZE are rounded
//                  up to the nears multiple of T_BLOCKSIZE.
//
//                  Default alignment is 16 bytes
//                  Default block size is 8 bytes
//
// Usage:
//
//    std::vector<float,aligned_BlockAlloc<float> >	vecFloatAlign16by8;
//    vecFloatAlign16by8.resize(1024,1.0f)
//
///////////////////////////////////////////////////////////////////////////////
template <typename T,
           unsigned long T_ALIGNMENT=16,
           unsigned long T_BLOCKSIZE=8>
class aligned_BlockAlloc : public std::allocator<T>
{
private:
     typedef std::allocator<T>  BASE_TYPE;

public:
     aligned_BlockAlloc() {}
     aligned_BlockAlloc& operator=(const aligned_BlockAlloc& r)
	 {
         BASE_TYPE::operator=(r);
         return *this;
     }

     pointer allocate(size_type n, const void *hint){
         pointer p = NULL;
         unsigned long byteCount = sizeof(T) * n;
         unsigned long byteCountLeft = byteCount % T_BLOCKSIZE;
         if(byteCountLeft)
		 {
             byteCount += T_BLOCKSIZE -  byteCountLeft;
         }
         if(!hint
			 ){
             p = reinterpret_cast<pointer>(_aligned_malloc(byteCount,T_ALIGNMENT));
         }
		 else
		 {
             p = reinterpret_cast<pointer>(_aligned_realloc((void*)hint,byteCount,T_ALIGNMENT));
         }
         return p;
     }

     void deallocate(pointer p, size_type n)
	 {
         _aligned_free(p);
     }

     void construct(pointer p, const T &val)
	 {
         new(p) T(val);
     }

     void destroy(pointer p)
	 {
         p->~T();
     }
};



typedef FastArray<float> ChannelBuffer;
typedef std::vector<ChannelBuffer> SampleBuffer;
typedef std::vector<SampleBuffer> PartitionedBuffer;

// float must be the same as the FastArray base type
extern float* restrict c_ptr(const ChannelBuffer& x);
extern float* restrict c_ptr(const SampleBuffer& x, const ChannelBuffer::size_type row=0);
extern float* restrict c_ptr(const PartitionedBuffer& x, const SampleBuffer::size_type row=0, const ChannelBuffer::size_type column=0);

// zero all the elements
extern void Zero (ChannelBuffer& x);
extern void Zero (SampleBuffer& x);
extern void Zero (PartitionedBuffer& x);

#endif


#if defined(DEBUG) | defined(_DEBUG)
void DumpSampleBuffer(const SampleBuffer& buffer);
void DumpChannelBuffer(const ChannelBuffer& buffer);
void DumpPartitionedBuffer(const PartitionedBuffer& buffer);
#endif
