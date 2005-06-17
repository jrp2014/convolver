#pragma once

//TODO: use vectors

//#include <vector>
//
// for ZeroMemory, etc
#include <Windows.h>

template <typename T> // Real or double for handing over to DFT routines
class CChannelBuffer
{
public:
	unsigned int nContainers;
	T* containers;											// A container contains a sample

	CChannelBuffer(const unsigned int s);
	virtual ~CChannelBuffer(void);

	// TODO: Test these properly
	CChannelBuffer(const CChannelBuffer& cb);				// copy constructor
	CChannelBuffer& operator=(const CChannelBuffer &cb);	// copy assignment

	T* containerArray() {return containers;}

protected:
	void copy(const CChannelBuffer &other);
};


template <typename T>
class CContainerBuffer
{
public:
	unsigned int nChannels;
	//vector< CChannelBuffer<T> > channels;
	CChannelBuffer<T>* channels;

	CContainerBuffer(const unsigned int c, const unsigned int s );
	~CContainerBuffer(void);
};

