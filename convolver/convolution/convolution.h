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

#include "convolution\config.h"
#include "convolution\samplebuffer.h"
#include "convolution\channelpaths.h"
#include "convolution\waveformat.h"
#include "convolution\holder.h"

// For random number seed
#include <time.h>

#if (defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))
// Intel compiler, for vectorization
extern "C" void cmul(float *, float *, float *, int);
extern "C" void cmuladd(float *, float *, float *, int);
#endif

// CConvolution does the work
// The data path is:
// pbInputData (raw bytes) ->  AttenuatedSample(const float fAttenuation_db, GetSample(BYTE* & container)) ->
// m_InputBuffer (FFT_type) -> [convolve] ->
// m_OutputBuffer (FFT_type) -> NormalizeSample(BYTE* dstContainer, float& srcSample) ->
// pbOuputData (raw bytes)


class CConvolution
{
public:
	CConvolution(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions, const int nContainerSize);
	virtual ~CConvolution(void);

	// This version of the convolution routine does partitioned convolution
	// Returns number of bytes processed  (== number of output bytes, too)

	// This version of the convolution routine does partitioned convolution
	DWORD doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		DWORD dwBlocksToProcess, // A block contains a sample for each channel
		const float fAttenuation_db,
		const float fWetMix,
		const float fDryMix);  // Returns bytes generated

	// This version does straight overlap-save convolution
	DWORD doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		DWORD dwBlocksToProcess,
		const float fAttenuation_db,
		const float fWetMix,
		const float fDryMix); // Returns bytes generated

	void Flush();								// zero buffers, reset pointers

	ChannelPaths			Mixer;				// Order dependent

protected:

	const int			nContainerSize_;			// 8, 16, 24, or 32 bits (64 not implemented)

private:

	SampleBuffer		InputBuffer_;
	ChannelBuffer		InputBufferAccumulator_;	// Only need to accumulate one channel at a time
	ChannelBuffer		OutputBuffer_;				// Used by overlap-save
	SampleBuffer		OutputBufferAccumulator_;	// As FFT routines destroy their input
	PartitionedBuffer	ComputationCircularBuffer_;	// Used as the output buffer for partitioned convolution

	int					nInputBufferIndex_;		// placeholder
	int					nPartitionIndex_;		// for partitioned convolution
	int					nOutputPartitionIndex_;	// lags nPartitionIndex_ by 1
	bool				bStartWritingOutput_;	// don't start outputting until we have some convolved output

#if !(defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))
	// non-vectorized complex array multiplication -- ordering specific to the Ooura routines. C = A * B
	void cmult(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const int N);
	void cmultadd(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const int N);
#endif

	// Used to check filter / partion lengths
	bool isPowerOf2(int i) const
	{
		return i > 0 && (i & (i - 1)) == 0;
	};

	CConvolution(const CConvolution& other); // no impl.
	void operator=(const CConvolution& other); // no impl.
	CConvolution(CConvolution& other);                // discourage use of lvalue Filters

protected:
	// Pure virtual functions that convert from a the sample type, to float
	virtual float GetSample(BYTE* & container) const = 0;						// converts sample into a float, [-1..1]
	virtual int NormalizeSample(BYTE* dstContainer, float& srcSample) const = 0;	// returns number of bytes processed
};



// Specializations with the appropriate functions for accessing the sample buffer
class Cconvolution_ieeefloat : public CConvolution
{
public:
	Cconvolution_ieeefloat(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions) :
	  CConvolution(szConfigFileName, nChannels, nPartitions, sizeof(float)){};

private:
	float GetSample(BYTE* & container) const
	{
		return *reinterpret_cast<const float *>(container);
	};

	int NormalizeSample(BYTE* dstContainer, float& srcSample) const 
	{ 
		*((float *) dstContainer) = srcSample;
		//* reinterpret_cast<float*>(dstContainer) = static_cast<float>(srcSample); // TODO: find cleaner way to do this
		return sizeof(float);
	};
};

// 8-bit sound is 0..255 with 128 == silence
class Cconvolution_pcm8 : public CConvolution
{
public:
	Cconvolution_pcm8(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions) :
	  CConvolution(szConfigFileName, nChannels, nPartitions, sizeof(BYTE)){};

private:

	float GetSample(BYTE* & container) const
	{
		return static_cast<float>((*container - 128.0) / 128.0);  // Normalize to [-1..1]
	};

	int NormalizeSample(BYTE* dstContainer, float& srcSample) const
	{   
		srcSample *= 128.0;

		// Truncate if exceeded full scale.
		if (srcSample > 127)
			srcSample = 127;
		if (srcSample < -128)
			srcSample = -128;
		*dstContainer = static_cast<BYTE>(srcSample + 128);
		return sizeof(BYTE); // ie, 1 byte
	};
};

// 16-bit sound is -32768..32767 with 0 == silence
class Cconvolution_pcm16 : public CConvolution
{
public:
	Cconvolution_pcm16(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions) :
	  CConvolution(szConfigFileName, nChannels, nPartitions, sizeof(INT16)){};

private:

	float GetSample(BYTE* & container) const 
	{ 
		return static_cast<float>(*reinterpret_cast<INT16*>(container) / 32768.0);
	}; 	// TODO: find a cleaner way to do this

	int NormalizeSample(BYTE* dstContainer, float& srcSample) const
	{   
		srcSample *= 32768.0;

		// Truncate if exceeded full scale.
		if (srcSample > 32767)
			srcSample = 32767;
		else
			if (srcSample < -32768)
				srcSample = -32768;
		*(INT16 *)dstContainer = (INT16)(srcSample);
		//*reinterpret_cast<INT16*>(dstContainer) = static_cast<INT16>(srcSample);
		return sizeof(INT16);  // ie, 2 bytes produced
	}
};

// 24-bit sound
template <int validBits>
class Cconvolution_pcm24 : public CConvolution
{
public:
	Cconvolution_pcm24(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions) :
	  CConvolution(szConfigFileName, nChannels, nPartitions, 3 /* Bytes */){};

private:

	float GetSample(BYTE* & container) const
	{
		int i = container[2];
		i = ( i << 8 ) | container[1];
		switch (validBits)
		{
		case 16:
			return i / 32768.0f; // 2^15
		case 20:
			i = (i << 4) | (container[0] >> 4);
			return i / 524288.0f; // 2^19
		case 24:
			i = (i << 8) | container[0];
			return i / 8388608.0f; // 2^23
		default:
			assert(false);
			return i / 8388608.0f; // 2^23;
		};

	};

	int NormalizeSample(BYTE* dstContainer, float& srcSample) const
	{   
		int iClip = 0;
		switch (validBits)
		{
		case 16:
			srcSample *= 32768.0f;
			iClip = (1 << 15);
			break;
		case 20:
			srcSample *= 524288.0f;
			iClip = (1 << 19);
			break;
		case 24:
			srcSample *= 8388608.0f;
			iClip = (1 << 23);
			break;
		default:
			assert(false);
			srcSample *= 8388608.0f;
			iClip = (1 << 23);
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

		return 3; // ie, 3 bytes generated
	};
};

// 32-bit sound
template <int validBits>
class Cconvolution_pcm32 : public CConvolution
{
public:
	Cconvolution_pcm32(TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions) :
	  CConvolution(szConfigFileName, nChannels, nPartitions, sizeof(INT32)){};

private:

	float GetSample(BYTE* & container) const
	{
		INT32 i = *reinterpret_cast<INT32*>(container);
		switch (validBits)
		{
		case 16:
			i >>= 16;
			return i / 32768.0f; // 2^15;
		case 20:
			i >>= 12;
			return i / 524288.0f; // 2^19;
		case 24:
			i >>= 8;
			return i / 8388608.0f; // 2^23;
		case 32:
			return i / 2147483648.0f; // 2^31
		default:
			assert(false);
			return i / 2147483648.0f; // 2^31
		}
	}; 

	int NormalizeSample(BYTE* dstContainer, float& srcSample) const
	{   
		INT32 iClip = 0;
		switch (validBits)
		{
		case 16:
			iClip = (1 << 15);
			srcSample *= 32768.0f; // 2^15;
			break;
		case 20:
			iClip = (1 << 19);
			srcSample *= 524288.0f; // 2^19;
			break;
		case 24:
			srcSample *= 8388608.0f; // 2^23;
			iClip = (1 << 23);
			break;
		case 32:
			srcSample *=  2147483648.0f;  // 2^31
			iClip = (1 << 31);
			break;
		default:
			assert(false);
		}

		// Truncate if exceeded full scale.
		if (srcSample > (iClip - 1))
		{
			srcSample = static_cast<float>(iClip - 1);
		}
		else
		{
			if (srcSample < -iClip)
			{
				srcSample = static_cast<float>(-iClip);
			}
		}
		*(INT32 *)dstContainer = static_cast<INT32>(srcSample);
		//*reinterpret_cast<INT32*>(dstContainer) = static_cast<INT32>(srcSample);
		return sizeof(INT32);  // ie, 4 bytes generated
	};
};

HRESULT SelectConvolution(WAVEFORMATEX* & pWave, Holder<CConvolution>& convolution, TCHAR szConfigFileName[MAX_PATH], const int nPartitions);
HRESULT calculateOptimumAttenuation(float& fAttenuation, TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions);
