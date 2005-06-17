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
		const double WetMix,
		const double DryMix
#if defined(DEBUG) | defined(_DEBUG)
		, CWaveFile* CWaveFileTrace
#endif	
		);  // Returns bytes processed

private:
	CChannelBuffer<FFT_type>* m_SampleBufferChannelCopy;

	unsigned int m_n2xSampleSize;
	unsigned int m_nSampleBufferLength;
	unsigned int m_nSampleBufferIndex;

	// Complex array multiplication -- ordering specific to the Ooura routines
	void cmult(const FFT_type * A, const FFT_type * B, FFT_type * C,const int N);

protected:
	// Pure virtual functions that convert from a the container type, to the FFT type
	virtual FFT_type get_sample(const BYTE* Container) const = 0;						// converts Container into a value of the right type
	virtual BYTE normalize_sample(BYTE* dstContainer, double srcSample) const = 0;	// returns number of bytes processed
	virtual BYTE* next_container(BYTE* Container) const = 0;								// used to move the buffer pointer on
};



// Specializations with the appropriate functions for accessing the container buffer
template <typename FFT_type>
class Cconvolution_ieeefloat : public CConvolution<FFT_type>
{
public:
	Cconvolution_ieeefloat(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:
	FFT_type get_sample(const BYTE* Container) const
	{
		return *(FFT_type *)Container;
	};	// TODO: find a cleaner way to do this

	BYTE normalize_sample(BYTE* dstContainer, double srcSample) const 
	{ 
		float fsrcSample = static_cast<float>(srcSample);
		dstContainer = (BYTE*)(&fsrcSample);
		return sizeof(float);
	};

	BYTE* next_container(BYTE* Container) const 
	{ 
		return Container + sizeof(float);
	};
};

// 8-bit sound is 0..255 with 128 == silence
template <typename FFT_type>
class Cconvolution_pcm8 : public CConvolution<FFT_type>
{
public:
	Cconvolution_pcm8(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* Container) const
	{
		return static_cast<FFT_type>(*Container - 128);
	};

	BYTE normalize_sample(BYTE* dstContainer, double srcSample) const
	{   // Truncate if exceeded full scale.
		if (srcSample > 127)
			srcSample = 127;
		if (srcSample < -128)
			srcSample = -128;
		*dstContainer = static_cast<BYTE>(srcSample + 128);
		return 1; // ie, 1 byte
	};

	BYTE* next_container(BYTE* Container) const 
	{ 
		return Container + sizeof(BYTE);
	};
};

// 16-bit sound is -32768..32767 with 0 == silence
template <typename FFT_type>
class Cconvolution_pcm16 : public CConvolution<float>
{
public:
	Cconvolution_pcm16(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* Container) const 
	{ 
		return static_cast<FFT_type>(*(INT16*)Container);
	}; 	// TODO: find a cleaner way to do this

	BYTE normalize_sample(BYTE* dstContainer, double srcSample) const
	{   // Truncate if exceeded full scale.
		if (srcSample > 32767)
			srcSample = 32767;
		else
			if (srcSample < -32768)
				srcSample = -32768;
		*(INT16 *)dstContainer = static_cast<INT16>(srcSample);
		return sizeof(INT16);  // ie, 2 bytes consumed
	};

	BYTE* next_container(BYTE* Container) const
	{
		return Container + sizeof(INT16);
	};
};

// 24-bit sound
template <typename FFT_type, int validBits>
class Cconvolution_pcm24 : public CConvolution<float>
{
public:
	Cconvolution_pcm24(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* Container) const
	{
		int i = (char) Container[2];
		i = ( i << 8 ) | Container[1];
		switch (validBits)
		{
		case 16:
			break;
		case 20:
			i = (i << 4) | (Container[0] >> 4);
			break;
		case 24:
			i = (i << 8) | Container[0];
			break;
		};
		return static_cast<FFT_type>(*Container);
	}; 	// TODO: find a cleaner way to do this

	BYTE normalize_sample(BYTE* dstContainer, double srcSample) const
	{   
		int iClip = 0;
		switch (validBits)
		{
		case 16:
			iClip = (1 << 15);
			break;
		case 20:
			iClip = (1 << 19);
			break;
		case 24:
			iClip = (1 << 23);
			break;
		};

		// Truncate if exceeded full scale.
		int i = 0;
		if (srcSample > (iClip - 1))
			i = iClip - 1;
		else
			if (srcSample < -iClip)
				i = -iClip;
			else
				i = static_cast<int>(srcSample);

		dstContainer[0] = static_cast<BYTE>(i & 0xff);
		dstContainer[1] = static_cast<BYTE>((i >>  8) & 0xff);
		dstContainer[2] = static_cast<BYTE>((i >> 16) & 0xff);

		return 3; // ie, 3 bytes processed
	};

	BYTE* next_container(BYTE* Container) const 
	{
		return Container + 3; // 3 bytes = 24 bits
	}; 
};


// 32-bit sound
template <typename FFT_type, int validBits>
class Cconvolution_pcm32 : public CConvolution<float>
{
public:
	Cconvolution_pcm32(unsigned int n2xSampleSize) : CConvolution<FFT_type>(n2xSampleSize){};

private:

	FFT_type get_sample(const BYTE* Container) const
	{
		int i = *(INT32*)Container;
		switch (validBits)
		{
		case 16:
			i >>= 16;
			break;
		case 20:
			i >>= 12;
			break;
		case 24:
			i >>= 8;
			break;
		case 32:
			break;
		}
		return static_cast<FFT_type>(i);
	}; 

	BYTE normalize_sample(BYTE* dstContainer, double srcSample) const
	{   
		int iClip = 0;
		switch (validBits)
		{
		case 16:
			iClip = (1 << 15);
			break;
		case 20:
			iClip = (1 << 19);
			break;
		case 24:
			iClip = (1 << 23);
			break;
		case 32:
			iClip = (1 << 31);
			break;
		}

		// Truncate if exceeded full scale.
		if (srcSample > (iClip - 1))
			srcSample = static_cast<double>(iClip - 1);
		else
			if (srcSample < -iClip)
				srcSample = static_cast<double>(-iClip);

		*(INT32 *)dstContainer = static_cast<INT32>(srcSample);
		return 4;  // ie, 4 bytes processed
	};

	BYTE* next_container(BYTE* Container) const 
	{
		return Container + 4;  // 4 bytes = 32 bits
	};  
};