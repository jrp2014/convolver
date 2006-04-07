/* The following code class adapted from the book
* "C++ Templates - The Complete Guide"
* by David Vandevoorde and Nicolai M. Josuttis, Addison-Wesley, 2002
*
* (C) Copyright David Vandevoorde and Nicolai M. Josuttis 2002
*
* It is comparable to boost::scoped_ptr
* 
*/

#pragma once

/// This class provides an implementation of the mutex model based on the Win32 CRITICAL_SECTION
class Thread_Mutex
{
	// Construction
public:
	/// Creates an instance of the mutex
	Thread_Mutex()
	{
		::InitializeCriticalSection(&lock_);
	}

	/// Destroys an instance of the mutex
	~Thread_Mutex()
	{
		::DeleteCriticalSection(&lock_);
	}

	// Operations

	/// Acquires a lock on the mutex, pending the thread until the lock is aquired
	void lock()
	{
		::EnterCriticalSection(&lock_);
	}

	/// Releases an aquired lock on the mutex
	void unlock()
	{
		::LeaveCriticalSection(&lock_);
	}

	// Members
private:
	CRITICAL_SECTION    lock_;   // critical section

	// Not to be implemented
private:
	Thread_Mutex(const Thread_Mutex &rhs);
	Thread_Mutex &operator =(const Thread_Mutex &rhs);
};


class Null_Mutex
{
public:
	Null_Mutex()
	{}

	~Null_Mutex()
	{}

	void lock()
	{}

	void unlock()
	{}

private: // unimplemented
	Null_Mutex(Null_Mutex const &rhs);
	Null_Mutex &operator =(const Null_Mutex &rhs);
};


template <class Mutex>
class Mutex_Guard
{
public:
	// Store a pointer to the lock and acquire it
	Mutex_Guard(Mutex& lock) : lock_(lock)
	{
		lock_.lock();
	}

	~Mutex_Guard()
	{
		lock_.unlock();
	}

private:
	Mutex& lock_;		// reference to the lock we're managing

private: // unimplemented
	Mutex_Guard(Mutex_Guard const &rhs);
	Mutex_Guard &operator =(const Mutex_Guard &rhs);
};


template <typename T, class Mutex = Null_Mutex>
class Holder {
private:
	T* ptr_;    // refers to the object it holds (if any)
	bool owns_;

public:

	Mutex lock;

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

	T* const operator-> () const
	{ 
		assert(ptr_);
		return ptr_; 
	}

	// get referenced object (if any)
	T* const get_ptr() const
	{
		return ptr_;
	}

	const Holder& set_ptr(T* p)
	{
		if( ptr_ != p)
		{
			kill();
			owns_ = (p != NULL);
			ptr_ = p;
		}
		return *this;
	}

	const Holder& refer_ptr(T* p)
	{
		if( ptr_ != p)
		{
			kill();
			owns_ = false;
			ptr_ = p;
		}
		return *this;
	}

	const T* release_ptr()
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
	Holder& operator=(const Holder&);
};
