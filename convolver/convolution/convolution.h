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

// Pull in Common DX classes
#include "Common\dxstdafx.h"

#include <time.h>

#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugStream.h"
#endif

#include ".\samplebuffer.h"
#include "convolverWMP\waveformat.h"

// FFT routines
#include "fft\fftsg_h.h"

const DWORD MAX_FILTER_SIZE = 100000000; // Max impulse size.  1024 taps might be a better choice
const DWORD MAX_ATTENUATION = 1000; // dB

template <typename FFT_type>
class CConvolution
{
public:
	CConvolution(const DWORD nContainerSize,
		const DWORD nInputBufferIndex = 0);					// TODO: for use with partioned convolution
	virtual ~CConvolution(void);

	// TODO: make this thread safe.  As it stands, you can call doConvolutionArbitrary and doConvolutionConstrained and get into a mess, 
	// as they use the same m_InputBuffer, etc (eg, by trying to set attenuation while something is playing.

	// This version of the convolution routine works on input data that is not necessarily a whole number of filter lengths long
	// It means that the initial filter length of output is just silence, until we have picked up a filter length of input, which
	// can then be convolved.  It ought to be possible to arrange to feed the convolution routine to in buffers that are a
	// multiple of the filter length long, but the would appear to require more fiddling with IMediaObject::GetInputSizeInfo, or
	// allocating our own output buffers, etc.
	// Returns number of bytes processed (== number of output bytes, too)
	DWORD doConvolutionArbitrary(const BYTE* pbInputData, BYTE* pbOutputData,
		DWORD dwBlocksToProcess,
		const double fAttenuation_db,
		const double fWetMix,
		const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
		, CWaveFile* CWaveFileTrace
#endif	
		);

	// This version of the convolution routine works on input data that is a whole number of filter lengths long
	// It means that the output does not need to lag
	// Returns number of bytes processed  (== number of output bytes, too)
	DWORD doConvolutionConstrained(const BYTE* pbInputData, BYTE* pbOutputData,
		DWORD dwBlocksToProcess,
		const double fAttenuation_db,
		const double fWetMix,
		const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
		, CWaveFile* CWaveFileTrace
#endif	
		);

	const HRESULT LoadFilter(TCHAR szFilterFileName[MAX_PATH]);

	const CSampleBuffer<FFT_type>*	m_Filter;
	WAVEFORMATEXTENSIBLE					m_WfexFilterFormat;	// The format of the filter file
	// TODO: Need to keep the number of source channels, and either mix them, or use a sub-set, if the filter has more channels

protected:

	DWORD const m_nContainerSize;							// 8, 16, 24, or 32 bits (64 not implemented)

private:

	const CSampleBuffer<FFT_type>*	m_InputBuffer;
	const CSampleBuffer<FFT_type>*	m_OutputBuffer;
	const CSampleBuffer<FFT_type>*	m_InputBufferChannelCopy;	

	DWORD							m_nInputBufferIndex;	// placeholder

	// Complex array multiplication -- ordering specific to the Ooura routines. C = A * B
	void cmult(const FFT_type * A, const FFT_type * B, FFT_type * C, const int N);

protected:
	// Pure virtual functions that convert from a the sample type, to the FFT type
	virtual const FFT_type get_sample(const BYTE* container) const = 0;						// converts sample into a value of the right type
	virtual const DWORD normalize_sample(BYTE* dstContainer, double srcSample) const = 0;	// returns number of bytes processed
	virtual const FFT_type attenuated_sample(const double fAttenuation_db, const FFT_type sample) const = 0;
};

// Specializations with the appropriate functions for accessing the sample buffer
template <typename FFT_type>
class Cconvolution_ieeefloat : public CConvolution<FFT_type>
{
public:
	Cconvolution_ieeefloat() : CConvolution<FFT_type>(sizeof(float)){};

private:
	const FFT_type get_sample(const BYTE* container) const
	{
		//return *(FFT_type *)container;
		return *reinterpret_cast<const FFT_type *>(container);
	};	// TODO: find a cleaner way to do this

	const DWORD normalize_sample(BYTE* dstContainer, double srcSample) const 
	{ 
		*((float *) dstContainer) = (float) srcSample;
		//* reinterpret_cast<float*>(dstContainer) = static_cast<float>(srcSample); // TODO: find cleaner way to do this
		return sizeof(float);
	};

	const FFT_type attenuated_sample(const double fAttenuation_db, const FFT_type sample) const
	{
		return fAttenuation_db == 0 ? sample : 
		static_cast<FFT_type>(static_cast<double>(sample) * pow(static_cast<double>(10), static_cast<double>(fAttenuation_db / 20.0L)));
	}
};

// 8-bit sound is 0..255 with 128 == silence
template <typename FFT_type>
class Cconvolution_pcm8 : public CConvolution<FFT_type>
{
public:
	Cconvolution_pcm8() : CConvolution<FFT_type>(sizeof(BYTE)){};

private:

	const FFT_type get_sample(const BYTE* container) const
	{
		return static_cast<FFT_type>(*container - 128);
	};

	const DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
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

	const FFT_type attenuated_sample(const double fAttenuation_db, const FFT_type sample) const
	{
		return (fAttenuation_db == 0 ? sample  : 
			static_cast<FFT_type>(static_cast<double>(sample) * pow(static_cast<double>(10), static_cast<double>(fAttenuation_db / 20.0L)))) / 128.0;
	}
};

// 16-bit sound is -32768..32767 with 0 == silence
template <typename FFT_type>
class Cconvolution_pcm16 : public CConvolution<float>
{
public:
	Cconvolution_pcm16() : CConvolution<FFT_type>(sizeof(INT16)){};

private:

	const FFT_type get_sample(const BYTE* container) const 
	{ 
		return static_cast<FFT_type>(*reinterpret_cast<const INT16*>(container));
	}; 	// TODO: find a cleaner way to do this

	const DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
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
	};

	const FFT_type attenuated_sample(const double fAttenuation_db, const FFT_type sample) const
	{
		return (fAttenuation_db == 0 ? sample  : 
			static_cast<FFT_type>(static_cast<double>(sample) * pow(static_cast<double>(10), static_cast<double>(fAttenuation_db / 20.0L)))) / 32768.0;
	}
};

// 24-bit sound
template <typename FFT_type, int validBits>
class Cconvolution_pcm24 : public CConvolution<float>
{
public:
	Cconvolution_pcm24() : CConvolution<FFT_type>(3 /* Bytes */){};

private:

	const FFT_type get_sample(const BYTE* container) const
	{
		int i = (char) container[2];
		i = ( i << 8 ) | container[1];
		switch (validBits)
		{
		case 16:
			break;
		case 20:
			i = (i << 4) | (container[0] >> 4);
			break;
		case 24:
			i = (i << 8) | container[0];
			break;
		};
		return static_cast<FFT_type>(i);
	};

	const DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
	{   
		srcSample *= 8388608.0;
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

		return 3; // ie, 3 bytes generated
	};

	const FFT_type attenuated_sample(const double fAttenuation_db, const FFT_type sample) const
	{
		return (fAttenuation_db == 0 ? sample  : 
			static_cast<FFT_type>(static_cast<double>(sample) * pow(static_cast<double>(10), static_cast<double>(fAttenuation_db / 20.0L)))) / 8388608.0;
	}

};


// 32-bit sound
template <typename FFT_type, int validBits>
class Cconvolution_pcm32 : public CConvolution<float>
{
public:
	Cconvolution_pcm32() : CConvolution<FFT_type>(sizeof(INT32)){};

private:

	const FFT_type get_sample(const BYTE* container) const
	{
		INT32 i = *reinterpret_cast<const INT32*>(container);
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

	const DWORD normalize_sample(BYTE* dstContainer, double srcSample) const
	{   
		srcSample *= 2147483648.0;
		INT32 iClip = 0;
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
		*(INT32 *)dstContainer = (INT32) srcSample;
		//*reinterpret_cast<INT32*>(dstContainer) = static_cast<INT32>(srcSample);
		return sizeof(INT32);  // ie, 4 bytes generated
	};

	const FFT_type attenuated_sample(const double fAttenuation_db, const FFT_type sample) const
	{
		return (fAttenuation_db == 0 ? sample  : 
			static_cast<FFT_type>(static_cast<double>(sample) * pow(static_cast<double>(10), static_cast<double>(fAttenuation_db / 20.0L)))) / 2147483648.0;
	}
};


// Pick the correct version of the processing class
//
// TODO: build this into the Factory class.  (Probably more trouble than it is worth)
//
// Note that this allows multi-channel and high-resolution WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT.
// Strictly speaking these should be WAVE_FORMAT_EXTENSIBLE, if we want to avoid ambiguity.
// The code below assumes that for WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT, wBitsPerSample is the container size.
// The code below will not work with streams where wBitsPerSample is used to indicate the bits of actual valid data,
// while the container size is to be inferred from nBlockAlign and nChannels (eg, wBitsPerSample = 20, nBlockAlign = 8, nChannels = 2).
// See http://www.microsoft.com/whdc/device/audio/multichaud.mspx
template <typename FFT_type>
HRESULT SelectConvolution(const WAVEFORMATEX* pWave, CConvolution<FFT_type>*& convolution)
{
	HRESULT hr = S_OK;

	if (NULL == pWave)
	{
		return E_POINTER;
	}

	WORD wFormatTag = pWave->wFormatTag;
	WORD wValidBitsPerSample = pWave->wBitsPerSample;
	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		WAVEFORMATEXTENSIBLE* pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;
		wValidBitsPerSample = pWaveXT->Samples.wValidBitsPerSample;
		if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			wFormatTag = WAVE_FORMAT_PCM;
			// TODO: cross-check pWaveXT->Samples.wSamplesPerBlock
		}
		else
			if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
			{
				wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			}
			else
			{
				return E_INVALIDARG;
			}
	}

	delete convolution;

	switch (wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		switch (pWave->wBitsPerSample)
		{
			// Note: for 8 and 16-bit samples, we assume the sample is the same size as
			// the samples. For > 16-bit samples, we need to use the WAVEFORMATEXTENSIBLE
			// structure to determine the valid bits per sample. (See above)
		case 8:
			convolution = new Cconvolution_pcm8<FFT_type>();
			break;

		case 16:
			convolution = new Cconvolution_pcm16<FFT_type>();
			break;

		case 24:
			{
				switch (wValidBitsPerSample)
				{
				case 16:
					convolution = new Cconvolution_pcm24<FFT_type,16>();
					break;

				case 20:
					convolution = new Cconvolution_pcm24<FFT_type,20>();
					break;

				case 24:
					convolution = new Cconvolution_pcm24<FFT_type,24>();
					break;

				default:
					return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
				}
			}
			break;

		case 32:
			{
				switch (wValidBitsPerSample)
				{
				case 16:
					convolution = new Cconvolution_pcm32<FFT_type,16>();
					break;

				case 20:
					convolution = new Cconvolution_pcm32<FFT_type,20>();
					break;

				case 24:
					convolution = new Cconvolution_pcm32<FFT_type,24>();
					break;

				case 32:
					convolution = new Cconvolution_pcm32<FFT_type,32>();
					break;

				default:
					return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
				}
			}
			break;

		default:  // Unprocessable PCM
			return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
		}
		break;

	case WAVE_FORMAT_IEEE_FLOAT:
		switch (pWave->wBitsPerSample)
		{
		case 32:
			convolution = new Cconvolution_ieeefloat<FFT_type>();
			break;

		default:  // Unprocessable IEEE float
			return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
			//break;
		}
		break;

	default: // Not PCM or IEEE Float
		return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
	}

	return hr;
};

// Convolve the filter with white noise to get the maximum output, from which we calculate the maximum attenuation
template<typename FFT_type>
HRESULT calculateOptimumAttenuation(double& fAttenuation, TCHAR szFilterFileName[MAX_PATH])
{
	HRESULT hr = S_OK;

	CConvolution<float>* c = new Cconvolution_ieeefloat<FFT_type>(); 

	if ( NULL == c )
	{
		return E_OUTOFMEMORY;
	}

	hr = c->LoadFilter(szFilterFileName);
	if ( FAILED(hr) )
	{
		delete c;
		return hr;
	}

	const WORD SAMPLES = 5; // length of the test sample, in filter lengths
	const DWORD nBufferLength =c->m_Filter->nChannels * c->m_Filter->nSamples * SAMPLES;
	const DWORD cBufferLength = nBufferLength * sizeof(FFT_type);


	BYTE* pbInputSamples = new BYTE[cBufferLength];
	if (NULL == pbInputSamples)
	{
		delete c;
		return E_OUTOFMEMORY;
	}
	BYTE* pbOutputSamples = new BYTE[cBufferLength];
	if (NULL == pbOutputSamples)
	{
		delete c;
		delete pbInputSamples;
		return E_OUTOFMEMORY;
	}
	float* pfInputSample = NULL;
	float* pfOutputSample = NULL;

	// Seed the random-number generator with current time so that
	// the numbers will be different every time we run.
	srand( (unsigned)time( NULL ) );

	bool again = TRUE;
	float maxSample = 0;

	do
	{
		pfInputSample = reinterpret_cast<FFT_type *>(pbInputSamples);
		pfOutputSample = reinterpret_cast<FFT_type *>(pbOutputSamples);

		for(DWORD i = 0; i!= nBufferLength; i++)
		{
			*pfInputSample = (2.0f * rand() - RAND_MAX) / RAND_MAX; // -1..1
			*pfOutputSample = 0;  // silence
			pfInputSample++;
			pfOutputSample++;
		}

		// Can use the constrained version of the convolution routine as have a buffer that is a multiple of the filter size in length to process
		c->doConvolutionConstrained(pbInputSamples, pbOutputSamples,
			/* dwBlocksToProcess */ c->m_Filter->nSamples * SAMPLES,
			/* fAttenuation_db */ 0,
			/* fWetMix,*/ 1.0L,
			/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
			, NULL
#endif
			);

		// Scan the output buffer for larger output samples
		again = FALSE;
		pfOutputSample = reinterpret_cast<FFT_type *>(pbOutputSamples);
		for(DWORD i = 0; i!= nBufferLength; i++)
		{
			if (abs(*pfOutputSample) > abs(maxSample))
			{
				maxSample = *pfOutputSample;
				again = TRUE; // Keep convolving until find no larger output samples
			}
			pfOutputSample++;
		}

	} while (again);

	// maxSample 0..1
	// 10 ^ (fAttenuation_db / 20) = 1
	// Limit fAttenuation to +/-MAX_ATTENUATION dB
	fAttenuation = abs(maxSample) > 1e-8 ? 20.0f * log(1.0f / abs(maxSample)) : 0;

	if (fAttenuation > MAX_ATTENUATION)
	{
		fAttenuation = MAX_ATTENUATION;
	}
	else if (-fAttenuation > MAX_ATTENUATION)
	{
		fAttenuation = -1.0L * MAX_ATTENUATION;
	}

	delete c;
	delete pbOutputSamples;
	delete pbInputSamples;

	return hr;
}
