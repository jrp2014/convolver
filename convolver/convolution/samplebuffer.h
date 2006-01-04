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
	reference restrict operator[](size_type n)
	{
		assert(n >= 0 && n < size_);
		return *(first_ + n);
	}

	const_reference operator[](size_type n) const
	{
		assert(n >= 0 && n < size_);
		return *(first_ + n);
	}

	// at() with range check
	reference restrict at(size_type n)
	{
		rangecheck(i);
		return *(first_ + n);
	}

	const_reference at(size_type i) const
	{
		rangecheck(i);
		return *(first_ + n);
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
	void swap(FastArray& x)
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
	void assign (const T& x)
	{
		std::fill_n(begin(), size(), x);
	}

	// Basic assignment.  Note that assertions are stricter than necessary, as unequally-size
	// arrays will be assigned correctly
	FastArray<T>& operator=(const FastArray<T>& other)
	{
		assert(size_ == 0 || size_ == other.size_);
		if (this != &other)
		{
			FastArray<T> temp(other);
			swap(temp);
		}
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
		assert(size() == rhs->size()));
		std::copy(rhs.begin(),rhs.end(), begin());
		return *this;
	}

	// Scalar assignment

	FastArray<T>& operator=(const_reference x)
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
	FastArray<T>& operator*= (const_reference x)
	{
		if(x != static_cast<T>(1))
		{
			const size_type ss = size_;
#pragma loop count(65536)
#pragma ivdep
#pragma vector aligned
			for (size_type i = 0; i < ss; ++i)
				(*this)[i] *= x;
		}
		return *this;
	}

	FastArray<T>& operator+= (const_reference x)
	{
		if(x != 0)
		{
			const size_type ss = size_;
#pragma loop count(65536)
#pragma ivdep
#pragma vector aligned
			for (size_type i = 0; i < ss; ++i)
				(*this)[i] += x;
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
		const size_type rhs_size = rhs.size();
#pragma loop count(65536)
#pragma ivdep
#pragma vector aligned
		for (size_type i = 0; i < rhs_size; ++i)
			(*this)[i] += rhs[i];

		return *this;
	}

	// Member functions

	void shiftleft(const size_type n)
	{
		//std::copy(begin() + n, begin() + 2 * n - 1, begin());
		::MoveMemory(first_, first_ + n, n * sizeof(T));
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

//template <typename T> 
//struct FastArray2D : public std::vector< FastArray<T> >
//{ 
//	// Constructors
//	FastArray2D() : std::vector< FastArray<T> >() {}
//	explicit FastArray2D(const size_type n) : std::vector< FastArray<T> >(n) {}
//	FastArray2D(const FastArray2D& other) : std::vector< FastArray<T> >(other) {}
//	FastArray2D(const size_type n, const FastArray<T>& x) : std::vector< FastArray<T> >(n, x) {}
//
//	// Assignment
//	FastArray2D<T>& operator=(const T x)
//	{
//		for(size_type i=0; i < this->size(); ++i)
//		{
//			(*this)[i] = x;
//		}
//		return *this;
//	}
//
//	FastArray2D<T>& operator+=(const T x)
//	{
//		for(size_type i=0; i < this->size(); ++i)
//		{
//			(*this)[i] += x;
//		}
//		return *this;
//	}
//
//	template <typename T2>
//	FastArray2D<T>& operator+= (const FastArray2D<T2>& rhs)
//	{
//		assert(this->size() == rhs->size()));
//		for(size_type i=0; i < this->size(); ++i)
//		{
//			(*this)[i] += rhs[i];
//		}
//		return *this;
//	}
//
//
//	T* restrict c_ptr(const size_type& row=0) const
//	{
//		assert (row < this->size());
//		return (*this)[row].c_ptr();
//	}
//}; 
//
//template <typename T> 
//struct FastArray3D : public std::vector< FastArray2D<T> >
//{ 
//	// Constructors
//	FastArray3D() : std::vector< FastArray2D<T> >() {}
//	explicit FastArray3D(const size_type n) : std::vector< FastArray2D<T> >(n) {}
//	FastArray3D(const FastArray3D& other) : std::vector< FastArray2D<T> >(other) {}
//	FastArray3D(const size_type n, const FastArray2D<T>& x) : std::vector< FastArray2D<T> >(n, x) {}
//
//	T* restrict c_ptr(const size_type row=0, const size_type column=0) const
//	{
//		assert (row < this->size() && (column < (*this)[row].size()));
//		//return &(*this)[row][column][plane];
//		return (*this)[row][column].c_ptr();
//	}
//
//	FastArray3D<T>& operator=(const T x)
//	{
//		for(size_type i=0; i < this->size(); ++i)
//		{
//			(*this)[i] = x;
//		}
//		return *this;
//	}
//
//	//FastArray3D<T>& operator=(const int& x)
//	//{
//	//	*this = static_cast<T>(x);
//	//	return *this;
//	//}
//
//}; 



typedef FastArray<float> ChannelBuffer;
typedef std::vector<ChannelBuffer> SampleBuffer;
typedef std::vector<SampleBuffer> PartitionedBuffer;

// float must be the same as the FastArray base type
extern float* restrict c_ptr(const ChannelBuffer& x);
extern float* restrict c_ptr(const SampleBuffer& x, const ChannelBuffer::size_type row=0);
extern float* restrict c_ptr(const PartitionedBuffer& x, const SampleBuffer::size_type row=0, const ChannelBuffer::size_type column=0);

// zero all the elements
extern void Zero (SampleBuffer& x);
extern void Zero (PartitionedBuffer& x);

//#ifdef UNDEF
//// T is normally float
//template <typename T>
//class fastarray
//{
//protected:
//  class fastarray_impl        // Ensures cleanup after exception
//  {
//  public:
//      explicit fastarray_impl(const int& size=0) :
//      v_(static_cast<T*>(ALIGNED_MALLOC(size * sizeof(T)))),      // 16-byte aligned for SIMD
//          //v_(new T[size]), // operator new ensures zero initialization
//          size_(size), bsize_(size * sizeof(T))
//      {
//          if(!v_)
//          {
//              throw(std::bad_alloc());
//          }
//      };
//
//      ~fastarray_impl()
//      {
//          ALIGNED_FREE(v_);
//          //delete [] (v_);
//      };
//
//      void Swap(fastarray_impl& other)
//      {
//          std::swap(v_, other.v_);
//          std::swap(size_, other.size_);
//          std::swap(bsize_, other.bsize_);
//      }
//
//      T* v_;
//      int size_;      // size in elements
//      int bsize_; // size in bytes
//
//  private:
//      // No copying allowed
//      fastarray_impl(const fastarray_impl&);
//      fastarray_impl(fastarray_impl&);                // discourage use of lvalue fastarray_impls
//  };
//
//  fastarray_impl impl_;   // private implementation
//
//public:
//  explicit fastarray(const int& size = 0) : impl_(size) {};
//
//  explicit fastarray(const fastarray<T>& other) : impl_(other.impl_.size_)
//  {
//      assert(impl_.bsize_ == other.impl_.bsize_);
//      ::CopyMemory(impl_.v_, other.impl_.v_, other.impl_.bsize_); // memcpy => overlap not allowed
//  }
//
//  operator T* () const
//  {
//      return impl_.v_;
//  }
//
//  const T& operator[](int n) const
//  { 
//      assert(n < impl_.size_ && n >= 0);
//      return *(impl_.v_ + n);
//  }
//
//  T& operator[](int n)
//  { 
//      assert(n < impl_.size_ && n >= 0);
//      return *(impl_.v_+ n);
//  }
//
//  fastarray<T>& operator=(const int& value)
//  {
//      if( value == 0)
//      {
//          ::ZeroMemory(impl_.v_, impl_.bsize_);
//      }
//      else
//      {
//          //#pragma omp parallel for
//#pragma ivdep
//#pragma vector aligned
//          for (int i = 0; i < impl_.size_; ++i)
//              impl_.v_[i] = static_cast<T>(value);                // TODO: optimize int->float
//      }
//      return *this;
//  }
//
//  fastarray<T>& operator=(const T& value)
//  {
//      //#pragma omp parallel for
//#pragma ivdep
//#pragma vector aligned
//      for (int i = 0; i < impl_.size_; ++i)
//          impl_.v_[i] = value;
//      return *this;
//  }
//
//  fastarray<T>& operator=(const fastarray<T>& rhs)
//  {
//      if (this != &rhs)
//      {
//          if (impl_.bsize_ == rhs.impl_.bsize_)
//          {
//              ::CopyMemory(impl_.v_, rhs.impl_.v_, impl_.bsize_); // memcpy => overlap not allowed
//          }
//          else
//          {
//              fastarray<T> temp(rhs);
//              impl_.Swap(temp.impl_);
//          }
//      }
//      return *this;
//  }
//
//  void shiftleft(const int n)
//  {
//      ::MoveMemory(&impl_.v_[0], &impl_.v_[n], n * sizeof(T));
//  }
//
//  int size() const
//  {
//      return impl_.size_;
//  }
//
//  fastarray<T>& operator*= (const float& value) // // TODO: optimize this
//  {
//      if (value != static_cast<T>(1.0))
//      {
//          //#pragma omp parallel for
//#pragma ivdep
//#pragma vector aligned
//          for (int i = 0; i < impl_.size_; ++i)
//              impl_.v_[i] *= value;
//      }
//      return *this;
//  }
//
//  fastarray<T>& operator+= (const fastarray<T>& other)    // TODO: optimize this
//  {
//      assert(impl_.size_ == other.impl_.size_);
//#pragma ivdep
//#pragma vector aligned
//      for (int i = 0; i < this->size(); ++i)
//          impl_.v_[i] += other.impl_.v_[i];
//      return *this;
//  }
//
//private:
//  fastarray(fastarray<T>&);                // discourage use of lvalue fastarrays
//};
//
//typedef   fastarray<float> ChannelBuffer;
//#ifdef UNDEF
//typedef   __declspec(align(16)) fastarray2<float> SampleBuffer;
//typedef   __declspec(align(16)) fastarray3<float> PartitionedBuffer;
//#else
//typedef   std::vector< ChannelBuffer >  SampleBuffer;
//typedef   std::vector< SampleBuffer >  PartitionedBuffer;
//#endif
//
//
//#else
//
////#include <iterator>
////
////template <class T>
////class stride_iter
////{
////public:
////    typedef typename std::iterator_traits<T>::value_type value_type;
////    typedef typename std::iterator_traits<T>::reference reference;
////    typedef typename std::iterator_traits<T>::difference_type difference_type;
////    typedef typename std::iterator_traits<T>::pointer pointer;
////    typedef std::random_access_iterator_tag iterator_category;
////    typedef stride_iter self;
////
////    // constructors
////    stride_iter(): m(NULL), step(0) {};
////    stride_iter(const self& x) : m(x.m), step(x.step);
////    stride_iter(T x, difference_type n) : m(x), step(n) {}
////
////    // operators
////    self& operator++() {m += step; return *this;}
////    self operator++(int) {self tmp = *this; m += step; return tmp;}
////    self& operator+=(difference_type x) {m += x * step; return *this;}
////    self& operator--() {m -= step; return *this;}
////    self& operator--(int) {self tmp = *this; m -= step; return tmp;}
////    self& operator-=(difference_type x) {m -= x * step; return *this;}
////    reference operator[](difference_type n) {return [n * step];}
////    reference operator*() {return *m;}
////
////    // friend operators
////    friend bool operator==(const self& x, const self& y)
////    {
////        assert(x.step == y.step);
////        return x.m == y.m;
////    }
////    friend bool operator!=(const self& x, const self& y)
////    {
////        assert(x.step == y.step);
////        return x.m != y.m;
////    }
////    friend bool operator<(const self& x, const self& y)
////    {
////        assert(x.step == y.step);
////        return x.m < y.m;
////    }
////    friend difference_type operator-(const self& x, const self& y)
////    {
////        assert(x.step == y.step);
////        return (x.m - y.m) / x.step;
////    }
////    friend difference_type operator+(const self& x, difference_type y)
////    {
////        assert(x.step == y.step);
////        return x += y * x.step;
////    }
////    friend difference_type operator+(difference_type x, const self& y)
////    {
////        assert(x.step == y.step);
////        return y += x * x.step;
////    }
////private:
////    T m;
////    difference_type step;
////};
//
////#include <iterator> 
////
////template<class Iter_T> 
////class stride_iter 
////{ 
////public: 
////    // public typedefs 
////    typedef typename std::iterator_traits<Iter_T>::value_type value_type; 
////    typedef typename std::iterator_traits<Iter_T>::reference reference; 
////    typedef typename std::iterator_traits<Iter_T>::difference_type difference_type; 
////    typedef typename std::iterator_traits<Iter_T>::pointer pointer; 
////    typedef std::random_access_iterator_tag iterator_category; 
////    typedef stride_iter self; 
////
////
////    // constructors 
////    stride_iter() : m(null), step(0) { }; 
////    explicit stride_iter(Iter_T x, difference_type n) : m(x), step(n) { } 
////
////
////    // operators 
////    self& operator++() { m += step; return *this; } 
////    self operator++(int) { self tmp = *this; m += step; return tmp; } 
////    self& operator+=(difference_type x) { m += x * step; return *this; } 
////    self& operator--() { m -= step; return *this; } 
////    self operator--(int) { self tmp = *this; m -= step; return tmp; } 
////    self& operator-=(difference_type x) { m -= x * step; return *this; } 
////    reference operator[](difference_type n) { return m[n * step]; } 
////    reference operator*() { return *m; } 
////
////
////    // friend operators 
////    friend bool operator==(const self& x, const self& y)
////    { 
////        assert(x.step == y.step);
////        return x.m == y.m;
////    } 
////    friend bool operator!=(const self& x, const self& y)
////    { 
////        assert(x.step == y.step);
////        return x.m != y.m;
////    } 
////    friend bool operator<(const self& x, const self& y) 
////    { 
////        assert(x.step == y.step);
////        return x.m < y.m;
////    } 
////    friend difference_type operator-(const self&x, const self& y)
////    {
////        assert(x.step == y.step);
////        return (x.m - y.m) / x.step;
////    } 
////    friend self operator+(const self&x, difference_type y)
////    { 
////        assert(x.step == y.step);
////        return x += y * x.step;
////    } 
////    friend self operator+(difference_type x, const self& y)
////    { 
////        assert(x.step == y.step);
////        return y += x * x.step;
////    } 
////private: 
////    Iter_T m; 
////    difference_type step; 
////}; 
////
////template<class Iter_T, int Step_N> 
////class kstride_iter 
////{ 
////public: 
////    // public typedefs 
////    typedef typename std::iterator_traits<Iter_T>::value_type value_type; 
////    typedef typename std::iterator_traits<Iter_T>::reference reference; 
////    typedef typename std::iterator_traits<Iter_T>::difference_type difference_type; 
////    typedef typename std::iterator_traits<Iter_T>::pointer pointer; 
////    typedef std::random_access_iterator_tag iterator_category; 
////    typedef kstride_iter self; 
////
////    // constructors 
////    kstride_iter() : m(NULL) { } 
////    kstride_iter(const self& x) : m(x.m) { }
////    explicit kstride_iter(Iter_T x) : m(x) { } 
////
////    // operators 
////    self& operator++() { m += Step_N; return *this; } 
////    self operator++(int) { self tmp = *this; m += Step_N; return tmp; } 
////    self& operator+=(difference_type x) { m += x * Step_N; return *this; } 
////    self& operator--() { m -= Step_N; return *this; } 
////    self operator--(int) { self tmp = *this; m -= Step_N; return tmp; } 
////    self& operator-=(difference_type x) { m -= x * Step_N; return *this; } 
////    reference operator[](difference_type n) { return m[n * Step_N]; } 
////    reference operator*() { return *m; } 
////
////    // friend operators 
////    friend bool operator==(self& x, self& y)
////    { return x.m == y.m; } 
////    friend bool operator!=(self& x, self& y)
////    { return x.m != y.m; } 
////    friend bool operator<(self& x, self& y)
////    { return x.m < y.m; } 
////    friend difference_type operator-(self&x, self& y)
////    { return (x.m - y.m) / Step_N; } 
////    friend self operator+(self& x, difference_type y)
////    { return x += y * Step_N; } 
////    friend self operator+(difference_type x, self& y)
////    { return y += x * Step_N; } 
////private: 
////    Iter_T m; 
////}; 
////
////#include <valarray>
////#include <numeric>
////#include <algorithm>
////
////template <class Value_T>
////class matrix
////{
////public:
////    // public typedefs
////    typedef Value_T value_type;
////    typedef matrix self;
////    typedef value_type* iterator;
////    typedef const value_type* const_iterator;
////    typedef Value_T* row_type;
////    typedef stride_iter<value_type*> col_type;
////    typedef const value_type* const_row_type;
////    typedef stride_iter<const value_type*> const_col_type;
////
////    // constructors
////    matrix() : nrows(0), ncols(0), m(0) {}
////    matrix( int r, int c ) : nrows(r), ncols(c), m(r*c) {}
////    matrix(const self& x) : m(x.m), nrows(x.nrows), ncols(x.ncols) {}
////
////    template<typename T>
////    explicit matrix(const std::valarray<T>& x) : m(x.size() + 1), nrows(x.size()), ncols(1)
////    {
////        for(int i=0; i < nrows; ++i)
////            m[i] = x[i];
////    }
////
////    // allow construction from matrixes of other types
////    template<typename T>
////        explicit matrix(const matrix<T>& x) : m(x.size() + 1), nrows(x.nrows), ncols(x.ncols)
////    {
////        copy(x.begin(), x.end(), m.begin());
////    }
////
////    //public functions
////    int rows() const { return nrows; }
////    int cols() const { return ncols; }
////    int size() const { return nrows * ncols; }
////
////    // element access
////    row_type row_begin(int n) { return &m[n * ncols]; }
////    row_type row_end(int n) { return row_begin() + ncols; }
////    col_type col_begin(int n) { return col_type(&m[n], ncols); }
////    col_type col_end(int n) { return col_begin(n) + ncols; } // TODO:: + nrows?
////    const_row_type row_begin(int n) const { return &m[n * ncols]; }
////    const_row_type row_end(int n) const { return row_begin() + ncols; }
////    const_col_type col_begin(int n) const { return col_type(&m[n], ncols); }
////    const_col_type col_end(int n) const { return col_begin(n) + ncols; } // TODO:: + nrows?
////    iterator begin() { return &m[0]; }
////    iterator end() { return &m[0] + nrows * ncols; }
////    const_iterator begin() const { return &m[0]; }
////    const_iterator end() const { return &m[0] + nrows * ncols; }
////
////    // operators
////    self& operator=(const self& x)
////    {
////        m = x.m;
////        nrows = x.nrows;
////        ncols = x.ncols;
////        return * this;
////    }
////    self& operator=(value_type x)
////    {
////        m = x;
////        return *this;
////    }
////    
////    //row_type operator[](int n) { return row_begin(n); }
////    //const_row_type operator[](const int n) const { return row_begin(n); }
////    std::valarray<Value_T> operator[](int n)
////    {
////        assert(n >= 0 && n < nrows);
////        return std::valarray<Value_T>(&m[n * ncols], nrows);
////    }
////    const std::valarray<Value_T> operator[](int n) const
////    {
////        assert(n >= 0 && n < nrows);
////        return std::valarray<Value_T>(&m[n * ncols], nrows);
////    }
////    self& operator+=(const self& x) { m+=x.m; return *this }
////    self& operator-=(const self& x) { m-=x.m; return *this }
////    self& operator+=(value_type x) { m+=x.m; return *this }
////    self& operator-=(value_type x) { m-=x.m; return *this }
////    self& operator*=(value_type x) { m*=x.m; return *this }
////    self& operator/=(value_type x) { m/=x.m; return *this }
////    self& operator%=(value_type x) { m%=x.m; return *this }
////    self& operator-() { return -m; }
////    self& operator+() { return +m; }
////    self& operator!() { return !m; }
////    self& operator~() { return ~m; }
////
////    // friend operators
////    friend self operator+(const self& x, const self& y) { return self(x) += y; }
////    friend self operator-(const self& x, const self& y) { return self(x) -= y; }
////    friend self operator+(const self& x, value_type y) { return self(x) += y; }
////    friend self operator-(const self& x, value_type y) { return self(x) -= y; }
////    friend self operator*(const self& x, value_type y) { return self(x) *= y; }
////    friend self operator/(const self& x, value_type y) { return self(x) /= y; }
////    friend self operator%(const self& x, value_type y) { return self(x) %= y; }
////
////private:
////    mutable std::valarray<Value_T> m;
////    int nrows;
////    int ncols;
////};
//
//
//// None of these are suitable
////#include "./Array.h"
////
////typedef Array::array1<float>::opt restrict ChannelBuffer;
////typedef Array::array2<float> restrict SampleBuffer;
////typedef Array::array3<float> restrict PartitionedBuffer;
//
////typedef std::valarray<float> ChannelBuffer;
////typedef matrix<float> SampleBuffer;
////typedef std::vector<SampleBuffer> PartitionedBuffer;
////
////#include <xdebug>
////#include <blitz/array.h>
////typedef blitz::Array<float,1> ChannelBuffer;
////typedef blitz::Array<float,2> SampleBuffer;
////typedef blitz::Array<float,3> PartitionedBuffer;
////#include <boost\multi_array.hpp>
////using boost;
////typedef boost::multi_array<float,1> ChannelBuffer;
////typedef boost::multi_array<float,2> SampleBuffer;
////typedef boost::multi_array<float,3> PartitionedBuffer;
//
////#include <APTL\Data\Array.h>
////typedef APTL::Array<float> ChannelBuffer;
////typedef APTL::Array2D<float> SampleBuffer;
////typedef APTL::Array3D<float> PartitionedBuffer;
//
//#endif
//
#endif


#if defined(DEBUG) | defined(_DEBUG)
void DumpSampleBuffer(const SampleBuffer& buffer);
void DumpChannelBuffer(const ChannelBuffer& buffer);
void DumpPartitionedBuffer(const PartitionedBuffer& buffer);
#endif
