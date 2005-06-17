#pragma once

// Pull in Common DX classes
#include "Common\dxstdafx.h"

#include ".\containerBuffer.h"

// FFT routines
#include <fftsg_h.h>

template <typename FFT_type>
class CConvolution
{
public:
	CConvolution(unsigned int n2xContainers);
	~CConvolution(void);

	DWORD doConvolution(const BYTE* pbInputData, BYTE* pbOutputData,
		const unsigned int nChannels,
		const CContainerBuffer<FFT_type>* Filter,
		CContainerBuffer<FFT_type>* ContainerBuffer,
		CContainerBuffer<FFT_type>* OutputBuffer,
		DWORD dwBlocksToProcess,
		const FFT_type WetMix,
		const FFT_type DryMix
#if defined(DEBUG) | defined(_DEBUG)
		, CWaveFile* CWaveFileTrace
#endif	
		);  // Returns bytes processed

private:
	CChannelBuffer<FFT_type>* m_ContainerBufferChannelCopy;

	unsigned int m_n2xContainerSize;
	unsigned int m_nContainerSize;
	unsigned int m_nContainerBufferIndex;

	// Complex array multiplication -- ordering specific to the Ooura routines
	void cmult(const FFT_type * A, const FFT_type * B, FFT_type * C,const int N);

protected:
	// Pure virtual functions that convert from a the container type, to the FFT type
	virtual FFT_type get_container(const BYTE* Container) const = 0;						// converts Container into a value of the right type
	virtual BYTE normalize_container(BYTE* dstContainer, FFT_type srcContainer) const = 0;	// returns number of bytes processed
	virtual BYTE* next_container(BYTE* Container) const = 0;								// used to move the buffer pointer on
};



// Specializations with the appropriate functions for accessing the container buffer
template <typename FFT_type>
class Cconvolution_ieeefloat : public CConvolution<FFT_type>
{
public:
	Cconvolution_ieeefloat(unsigned int n2xContainerSize) : CConvolution<FFT_type>(n2xContainerSize){};

private:
	FFT_type get_container(const BYTE* Container) const  { return *(FFT_type *)Container; };	// TODO: find a cleaner way to do this
	BYTE normalize_container(BYTE* dstContainer, FFT_type srcContainer) const  { dstContainer = (BYTE*)(&srcContainer); return sizeof(float); };
	BYTE* next_container(BYTE* Container) const { return Container + sizeof(float); };
};

// 8-bit sound is 0..255 with 128 == silence
template <typename FFT_type>
class Cconvolution_pcm8 : public CConvolution<FFT_type>
{
public:
	Cconvolution_pcm8(unsigned int n2xContainerSize) : CConvolution<FFT_type>(n2xContainerSize){};

private:
	
	FFT_type get_container(const BYTE* Container) const   { return static_cast<FFT_type>(*Container - 128); };
	BYTE normalize_container(BYTE* dstContainer, FFT_type srcContainer) const
	{   // Truncate if exceeded full scale.
		if (srcContainer > 127)
			srcContainer = 127;
		if (srcContainer < -128)
			srcContainer = -128;
		*dstContainer = srcContainer + 128;
		return 1; // ie, 1 byte
	};
	BYTE* next_container(BYTE* Container) const { return Container + sizeof(BYTE); };
};

// 16-bit sound is -32768..32767 with 0 == silence
template <typename FFT_type>
class Cconvolution_pcm16 : public CConvolution<float>
{
public:
	Cconvolution_pcm16(unsigned int n2xContainerSize) : CConvolution<FFT_type>(n2xContainerSize){};

private:

	FFT_type get_container(const BYTE* Container) const  { return *(INT16*)Container; }; 	// TODO: find a cleaner way to do this
	BYTE normalize_container(BYTE* dstContainer, FFT_type srcContainer) const
	{   // Truncate if exceeded full scale.
		if (srcContainer > 32767)
			srcContainer = 32767;
		if (srcContainer < -32768)
			srcContainer = -32768;
		*(INT16 *)dstContainer = srcContainer;
		return sizeof(INT16);  // ie, 2 bytes consumed
	};
	BYTE* next_container(BYTE* Container) const {return Container + sizeof(INT16); };
};