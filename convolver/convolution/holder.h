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
	bool owns_;

public:
	// default constructor: let the holder refer to nothing, and
	// constructor for a pointer: let the holder refer to where the pointer refers
	Holder() : ptr_(NULL), owns_(true) {}

	explicit Holder (T* p) : ptr_(p), owns_(p != NULL) {}

	// destructor: releases the object to which it refers (if any)
	~Holder()
	{ 
		kill();
	}

	// pointer operators
	T& operator* () const
	{ 
		assert(ptr_);
		return *ptr_;
	}

	T* operator-> () const
	{ 
		assert(ptr_);
		return ptr_; 
	}

	// get referenced object (if any)
	T* get_ptr() const
	{
		return ptr_;
	}

	Holder& set_ptr(T* p)
	{
		if( ptr_ != p)
		{
			kill();
			owns_ = (p != NULL);
			ptr_ = p;
		}
		return *this;
	}

	Holder& refer_ptr(T* p)
	{
		if( ptr_ != p)
		{
			kill();
			owns_ = false;
			ptr_ = p;
		}
		return *this;
	}

	T* release_ptr()
	{
		owns_=false;
		return ptr_;
	}

private:

	void kill()
	{
		if (owns_)
		{
			delete ptr_;
		}
		ptr_=NULL;
		owns_=false;
	}

	// No copying
	Holder(const Holder&);
	const Holder& operator=(const Holder&);
};
