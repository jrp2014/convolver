/* The following code class adapted from the book
* "C++ Templates - The Complete Guide"
* by David Vandevoorde and Nicolai M. Josuttis, Addison-Wesley, 2002
*
* (C) Copyright David Vandevoorde and Nicolai M. Josuttis 2002
* 
*/

#pragma once

template <typename T>
class Holder {
private:
	T* ptr_;    // refers to the object it holds (if any)

public:
	// default constructor: let the holder refer to nothing, and
	// constructor for a pointer: let the holder refer to where the pointer refers
	explicit Holder (T* p = 0) : ptr_(p) { }

	// destructor: releases the object to which it refers (if any)
	~Holder()
	{ 
		delete ptr_;
	}

	// assignment of new pointer
	Holder<T>& operator= (T* p)
	{
		delete ptr_;
		ptr_ = p;
		return *this;
	}

	// pointer operators
	T& operator* () const
	{ 
		return *ptr_;
	}

	T* operator-> () const
	{ 
		return ptr_; 
	}

    // get referenced object (if any)
    T* get_ptr() const
	{
        return ptr_;
    }

private:
	// no copying and copy assignment allowed
	Holder (Holder<T> const&);
	Holder<T>& operator= (Holder<T> const&);
};
