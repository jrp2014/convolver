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
#include <ks.h>
#include <ksmedia.h>
#include <mediaerr.h>
#include "convolution\factory.h"
#include "convolution\dither.h"

//Floating-point samples use the range -1.0...1.0, inclusive.
//Integer formats use the full signed range of their data type,
//for example 16-bit samples use the range -32768...32767.
//This presents a problem, because either the conversion to
//or from a float would have to be asymmetric, or zero would
//have to be mapped to something other than zero.  (A third
//option is to use a symmetric, zero-preserving mapping that
//might clip, and check for clipping...but this option is
//both uncompelling and slow.)
//
//Convolver chooses to use a symmetric mapping that doesn't
//preserve 0:
//
//16-bit    float        16-bit
//-32768 -> -1.000000 -> -32768
///
//     0 ->  0.000015 ->      0
//           0.000000 ->      0
//
// 32767 ->  1.000000 ->  32767

// Note that 0.0 (float) still maps to 0 (int), so it is nearly
// an ideal mapping.  An analogous mapping is used between
// 24-bit ints and floats.

template <typename T>
struct __declspec(novtable) __single_inheritance ConvertSample
{
	ConvertSample() {}

	ConvertSample(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{}

	virtual void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const = 0;	// converts sample into a T (eg, float), [-1..1]

	virtual void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const = 0;

	virtual WORD nContainerSize() const = 0;

	virtual ~ConvertSample(void) = 0;
};

template <typename T>
inline ConvertSample<T>::~ConvertSample(void){}

// Specializations with the appropriate functions for accessing the sample buffer

// TODO: float with wValidBitsPerSample = 18; //Top 18 bits have data
template <typename T>
struct ConvertSample_ieeefloat : public ConvertSample<T>
{

	ConvertSample_ieeefloat()
	{}

	ConvertSample_ieeefloat(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{}

	void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		dstSample = *reinterpret_cast<float *>(srcContainer) * fAttenuationFactor;
		srcContainer += sizeof(float);
		nBytesProcessed += sizeof(float);
	}

	void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const 
	{ 
		*((float *) dstContainer) = srcSample;
		//* reinterpret_cast<double*>(dstContainer) = static_cast<double>(srcSample); // TODO: find cleaner way to do this
		dstContainer += sizeof(float);
		nBytesGenerated += sizeof(float);
	}

	WORD nContainerSize() const
	{
		return sizeof(float);
	}
};

template <typename T>
struct ConvertSample_ieeedouble : public ConvertSample<T>
{
	ConvertSample_ieeedouble()
	{}

	ConvertSample_ieeedouble(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{}

	void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		dstSample = *reinterpret_cast<double *>(srcContainer) * fAttenuationFactor;
		srcContainer += sizeof(double);
		nBytesProcessed += sizeof(double);
	}

	void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const 
	{ 
		*((double *) dstContainer) = srcSample;
		//* reinterpret_cast<double*>(dstContainer) = static_cast<double>(srcSample); // TODO: find cleaner way to do this
		dstContainer += sizeof(double);
		nBytesGenerated += sizeof(double);
	}

	WORD nContainerSize() const
	{
		return sizeof(double);
	}
};

// 8-bit sound is 0..255 with 128 == silence
template <typename T>
struct ConvertSample_pcm8 : public ConvertSample<T>
{

	ConvertSample_pcm8() : dither_(new NoDither<T>()), noiseshape_(new NoNoiseShape<T,INT16,8>(1))
	{}

	ConvertSample_pcm8(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{
		switch(nDither)
		{
		case Ditherer<T>::None:
			{
				dither_.set_ptr(new NoDither<T>());
			}
			break;
		case Ditherer<T>::Triangular:
			{
				if(nChannels == 2)
				{
					dither_.set_ptr(new StereoTriangularDither<T, 8>());
				}
				else
				{
					dither_.set_ptr(new TriangularDither<T, 8>(nChannels));
				}
			}
			break;
		case Ditherer<T>::Rectangular:
			{
				dither_.set_ptr(new RectangularDither<T, 8>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid Dither for pcm8");
		}

		switch(nNoiseShaper)
		{
		case NoiseShaper<T>::None:
			{
				noiseshape_.set_ptr(new NoNoiseShape<T,INT16,8>(nChannels));
			}
			break;
		case NoiseShaper<T>::Simple:
			{
				noiseshape_.set_ptr(new SimpleNoiseShape<T,INT16,8>(nChannels));
			}
			break;
		case NoiseShaper<T>::SecondOrder:
			{
				noiseshape_.set_ptr(new SecondOrderNoiseShape<T,INT16,8>(nChannels));
			}
			break;
		case NoiseShaper<T>::ThirdOrder:
			{
				noiseshape_.set_ptr(new ThirdOrderNoiseShape<T,INT16,8>(nChannels));
			}
			break;
		case NoiseShaper<T>::NinthOrder:
			{
				noiseshape_.set_ptr(new NinthOrderNoiseShape<T,INT16,8>(nChannels));
			}
			break;
		case NoiseShaper<T>::SonySBM:
			{
				noiseshape_.set_ptr(new SonySBM<T,INT16,8>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid NoiseShaper for pcm8");
		}
	}

	void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		const T Q = 1.0 / ((1 << (8-1)) - 0.5); // 1/127.5
		dstSample = static_cast<T>((*srcContainer - T(127.5)) * Q) * fAttenuationFactor;  // Normalize [0..255] -> to [-1..1)
		++srcContainer;
		++nBytesProcessed;
	}

	void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const 
	{   
		//*dstContainer = static_cast<BYTE>(srcSample < T(-1.0) ? 0 : (srcSample > T(1.0) ? 255 : srcSample * T(127.5) + T(128.0))); 

		*dstContainer = static_cast<BYTE>(noiseshape_->shapenoise(srcSample < T(-1.0) ? 
			T(-1.0) : (srcSample > T(1.0) ? T(1.0) : srcSample), dither_.get_ptr(), nChannel) + 128);

		++dstContainer;
		++nBytesGenerated;
	}

	WORD nContainerSize() const
	{
		return 1;
	}

private:
	Holder<Dither<T> > dither_;
	Holder<NoiseShape<T, INT16> > noiseshape_;
};

// 16-bit sound is -32768..32767 with 0 == silence
template <typename T>
struct ConvertSample_pcm16 : public ConvertSample<T>
{
	ConvertSample_pcm16() : dither_(new NoDither<T>()), noiseshape_(new NoNoiseShape<T,INT16,16>(1))
	{}

	ConvertSample_pcm16(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{
		switch(nDither)
		{
		case Ditherer<T>::None:
			{
				dither_.set_ptr(new NoDither<T>());
			}
			break;
		case Ditherer<T>::Triangular:
			{
				if(nChannels == 2)
				{
					dither_.set_ptr(new StereoTriangularDither<T, 16>());
				}
				else
				{
					dither_.set_ptr(new TriangularDither<T, 16>(nChannels));
				}
			}
			break;
		case Ditherer<T>::Rectangular:
			{
				dither_.set_ptr(new RectangularDither<T, 16>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid Dither for pcm16");
		}

		switch(nNoiseShaper)
		{
		case NoiseShaper<T>::None:
			{
				noiseshape_.set_ptr(new NoNoiseShape<T,INT16,16>(nChannels));
			}
			break;
		case NoiseShaper<T>::Simple:
			{
				noiseshape_.set_ptr(new SimpleNoiseShape<T,INT16,16>(nChannels));
			}
			break;
		case NoiseShaper<T>::SecondOrder:
			{
				noiseshape_.set_ptr(new SecondOrderNoiseShape<T,INT16,16>(nChannels));
			}
			break;
		case NoiseShaper<T>::ThirdOrder:
			{
				noiseshape_.set_ptr(new ThirdOrderNoiseShape<T,INT16,16>(nChannels));
			}
			break;
		case NoiseShaper<T>::NinthOrder:
			{
				noiseshape_.set_ptr(new NinthOrderNoiseShape<T,INT16,16>(nChannels));
			}
			break;
		case NoiseShaper<T>::SonySBM:
			{
				noiseshape_.set_ptr(new SonySBM<T,INT16,16>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid NoiseShaper for pcm16");
		}
	}

	void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const 
	{ 
		const T Q = 2.0 / ((1 << 16) - 1.0); // 2/65535 

		dstSample = (static_cast<T>(*reinterpret_cast<INT16*>(srcContainer)) + T(0.5)) * Q * fAttenuationFactor;
		srcContainer += 2;
		nBytesProcessed += 2;
	}

	void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const 
	{   
		*(INT16 *)dstContainer = noiseshape_->shapenoise(srcSample < T(-1.0) ? 
			T(-1.0) : (srcSample > T(1.0) ? T(1.0) : srcSample), dither_.get_ptr(), nChannel);

		dstContainer += 2;
		nBytesGenerated += 2;
	}

	WORD nContainerSize() const
	{
		return 2;
	}

private:
	Holder<Dither<T> > dither_;
	Holder<NoiseShape<T, INT16> > noiseshape_;
};

// 24-bit sound
template <typename T, int validBits>
struct ConvertSample_pcm24 : public ConvertSample<T>
{
	ConvertSample_pcm24() : dither_(new NoDither<T>()), noiseshape_(new NoNoiseShape<T,INT32,validBits>(1))
	{}

	ConvertSample_pcm24(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{
		switch(nDither)
		{
		case Ditherer<T>::None:
			{
				dither_.set_ptr(new NoDither<T>());
			}
			break;
		case Ditherer<T>::Triangular:
			{
				if(nChannels == 2)
				{
					dither_.set_ptr(new StereoTriangularDither<T, validBits>());
				}
				else
				{
					dither_.set_ptr(new TriangularDither<T, validBits>(nChannels));
				}
			}
			break;
		case Ditherer<T>::Rectangular:
			{
				dither_.set_ptr(new RectangularDither<T, validBits>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid Dither for pcm24");
		}

		switch(nNoiseShaper)
		{
		case NoiseShaper<T>::None:
			{
				noiseshape_.set_ptr(new NoNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::Simple:
			{
				noiseshape_.set_ptr(new SimpleNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::SecondOrder:
			{
				noiseshape_.set_ptr(new SecondOrderNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::ThirdOrder:
			{
				noiseshape_.set_ptr(new ThirdOrderNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::NinthOrder:
			{
				noiseshape_.set_ptr(new NinthOrderNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::SonySBM:
			{
				noiseshape_.set_ptr(new SonySBM<T,INT32,validBits>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid NoiseShaper for pcm24");
		}
	}

	void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const
	{

		INT32 i = srcContainer[2];
		i = ( i << 8 ) | srcContainer[1];

		switch (validBits)
		{
		case 16:
			break;
		case 20:
			i = (i << 4) | (srcContainer[0] >> 4);
		case 24:
			i = (i << 8) | srcContainer[0];
		default:
			assert(false);
		};

		const T Q = 2.0 / ((1 << validBits) - 1);

		dstSample =  (i + T(0.5)) * Q * fAttenuationFactor;

		srcContainer += 3;
		nBytesProcessed += 3;
	}

	void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const 
	{   
		// Clip if exceeded full scale.
		const INT32 i = noiseshape_->shapenoise(srcSample < T(-1.0) ?
			T(-1.0) : (srcSample > T(1.0) ?	T(1.0) : srcSample), dither_.get_ptr(), nChannel);

		dstContainer[0] = static_cast<BYTE>(i & 0xff);
		dstContainer[1] = static_cast<BYTE>((i >>  8) & 0xff);
		dstContainer[2] = static_cast<BYTE>((i >> 16) & 0xff);

		dstContainer += 3;
		nBytesGenerated += 3;
	}

	WORD nContainerSize() const
	{
		return 3;
	}

private:
	Holder<Dither<T> > dither_;
	Holder<NoiseShape<T, INT32> > noiseshape_;
};

// 32-bit sound
template <typename T, int validBits = 32>
struct ConvertSample_pcm32 : public ConvertSample<T>
{
	ConvertSample_pcm32() : dither_(new NoDither<T>()), noiseshape_(new NoNoiseShape<T,INT32,validBits>(1))
	{}

	ConvertSample_pcm32(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{
		switch(nDither)
		{
		case Ditherer<T>::None:
			{
				dither_.set_ptr(new NoDither<T>());
			}
			break;
		case Ditherer<T>::Triangular:
			{
				if(nChannels == 2)
				{
					dither_.set_ptr(new StereoTriangularDither<T, validBits>());
				}
				else
				{
					dither_.set_ptr(new TriangularDither<T, validBits>(nChannels));
				}
			}
			break;
		case Ditherer<T>::Rectangular:
			{
				dither_.set_ptr(new RectangularDither<T, validBits>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid Dither for pcm32");
		}

		switch(nNoiseShaper)
		{
		case NoiseShaper<T>::None:
			{
				noiseshape_.set_ptr(new NoNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::Simple:
			{
				noiseshape_.set_ptr(new SimpleNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::SecondOrder:
			{
				noiseshape_.set_ptr(new SecondOrderNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::ThirdOrder:
			{
				noiseshape_.set_ptr(new ThirdOrderNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::NinthOrder:
			{
				noiseshape_.set_ptr(new NinthOrderNoiseShape<T,INT32,validBits>(nChannels));
			}
			break;
		case NoiseShaper<T>::SonySBM:
			{
				noiseshape_.set_ptr(new SonySBM<T,INT32,validBits>(nChannels));
			}
			break;
		default:
			throw std::runtime_error("Invalid NoiseShaper for pcm32");
		}
	}

	void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		const T Q = 1.0 / ( (1 << (validBits-2)) - 0.5 + (1 << (validBits-2)) );

		dstSample = (*reinterpret_cast<INT32*>(srcContainer) + T(0.5)) * Q * fAttenuationFactor;

		srcContainer += 4;
		nBytesProcessed += 4;
	}

	void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const
	{   
		// Clip if exceeded full scale.
		*(INT32 *)dstContainer = noiseshape_->shapenoise(srcSample < T(-1.0) ?
			T(-1.0) : (srcSample > T(1.0) ? T(1.0) : srcSample), dither_.get_ptr(), nChannel);

		dstContainer += 4;
		nBytesGenerated += 4;
	}

	WORD nContainerSize() const
	{
		return 4;
	}

private:
	Holder<Dither<T> > dither_;
	Holder<NoiseShape<T, INT32> > noiseshape_;
};

// 32-bit sound. No dithering or shaping
template <typename T>
struct ConvertSample_pcm32<T,32> : public ConvertSample<T>
{
	ConvertSample_pcm32()
	{}

	ConvertSample_pcm32(const typename NoiseShaper<T>::NoiseShapingType nNoiseShaper, const typename Ditherer<T>::DitherType nDither,
		const WORD nChannels, const DWORD nSamplesPerSec)
	{}

	void GetSample(T& dstSample, BYTE*& srcContainer, const float fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		const T Q = 1.0 / ( (1 << (32-2)) - 0.5 + (1 << (32-2)) );

		dstSample = (*reinterpret_cast<INT32*>(srcContainer) + T(0.5)) * Q * fAttenuationFactor;

		srcContainer += 4;
		nBytesProcessed += 4;
	}

	void PutSample(BYTE*& dstContainer, T srcSample, const WORD nChannel, DWORD& nBytesGenerated) const
	{   
		const T q = (1 << 30) - 0.5 + (1 << 30); // (2^31 - 0.5)

		// Clip if exceeded full scale.
		*(INT32 *)dstContainer = 
			conv_float_to_int<INT32,T>((srcSample < T(-1.0) ? T(-1.0) : (srcSample > T(1.0) ? T(1.0) : srcSample)) * q);

		dstContainer += 4;
		nBytesGenerated += 4;
	}

	WORD nContainerSize() const
	{
		return 4;
	}
};


// Used to distinguish between sample formats
struct SampleFormatId
{
	SampleFormatId(const GUID SubType,
		const WORD wFormatTag,
		const WORD wBitsPerSample,
		const WORD wValidBitsPerSample) : 
	SubType(SubType), wFormatTag(wFormatTag), wBitsPerSample(wBitsPerSample), wValidBitsPerSample(wValidBitsPerSample)
	{}

	const GUID SubType;
	const WORD wFormatTag;
	const WORD wBitsPerSample; 
	const WORD wValidBitsPerSample;
};

// Ordering is by preference
bool operator <(const SampleFormatId& x, const SampleFormatId& y);


template <typename T>
class ConvertSampleMaker
{
private:
	typedef typename ObjectFactory<ConvertSample<T> *(typename NoiseShaper<T>::NoiseShapingType, typename Ditherer<T>::DitherType,
		WORD, DWORD), SampleFormatId> FactoryType;

public:
	ConvertSampleMaker()
	{
		sample_factory_.Register<ConvertSample_ieeefloat<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_IEEE_FLOAT,	32,32));
		sample_factory_.Register<ConvertSample_ieeefloat<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_EXTENSIBLE,	32,32));
		sample_factory_.Register<ConvertSample_pcm32<T,32> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,			32,32));
		sample_factory_.Register<ConvertSample_pcm32<T,24> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,			32,24));
		sample_factory_.Register<ConvertSample_pcm32<T,20> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,			32,20));
		sample_factory_.Register<ConvertSample_pcm32<T,16> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,			32,16));
		sample_factory_.Register<ConvertSample_pcm24<T,24> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,			24,24));
		sample_factory_.Register<ConvertSample_pcm24<T,20> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,			24,20));
		sample_factory_.Register<ConvertSample_pcm24<T,16> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,			24,16));
		sample_factory_.Register<ConvertSample_pcm16<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_PCM,					16,16));
		sample_factory_.Register<ConvertSample_pcm16<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,				16,16));
		sample_factory_.Register<ConvertSample_pcm8<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_PCM,						 8, 8));
		sample_factory_.Register<ConvertSample_pcm8<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_PCM, WAVE_FORMAT_EXTENSIBLE,				 8, 8));
		sample_factory_.Register<ConvertSample_ieeedouble<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_IEEE_FLOAT,	64,64)); // May not exist
		sample_factory_.Register<ConvertSample_ieeedouble<T> >(SampleFormatId(KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_EXTENSIBLE,	64,64)); // May not exist
	}

	typename FactoryType::size_type size() const
	{
		return sample_factory_.size();
	}

	const SampleFormatId& operator[](const typename FactoryType::size_type i) const
	{
		assert(i < size());
		typename FactoryType::const_iterator pos = sample_factory_.begin();
		for(int j= 0; j < i; ++j)
			++pos;
		return pos->first;
	}

	// Is the pWave sample format supported?
	HRESULT CheckSampleFormat(const WAVEFORMATEX* pWave) const
	{
		if(pWave == NULL)
		{
			return E_POINTER;
		}

		if ((pWave->nBlockAlign != pWave->nChannels * pWave->wBitsPerSample / 8) ||
			(pWave->nAvgBytesPerSec != pWave->nBlockAlign * pWave->nSamplesPerSec))
		{
			return DMO_E_INVALIDTYPE;
		}

		// Formats that support more than two channels or sample sizes of more than 16 bits can be described
		// in a WAVEFORMATEXTENSIBLE structure, which includes the WAVEFORMAT structure.
		if (pWave->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			const WAVEFORMATEXTENSIBLE* pWaveXT = (WAVEFORMATEXTENSIBLE*) pWave;

			if(pWaveXT->Format.cbSize < 22) // required (see http://www.microsoft.com/whdc/device/audio/multichaud.mspx)
			{
				return DMO_E_INVALIDTYPE;
			}
			assert(pWaveXT->Format.cbSize == 22); // for the types that we currently support

			return sample_factory_.Registered(SampleFormatId(pWaveXT->SubFormat, WAVE_FORMAT_EXTENSIBLE,
				pWaveXT->Format.wBitsPerSample, pWaveXT->Samples.wValidBitsPerSample)) ? S_OK : DMO_E_TYPE_NOT_ACCEPTED;
		}
		else // WAVE_FORMAT_PCM (8, or 16-bit) or WAVE_FORMAT_IEEE_FLOAT (32-bit)
		{
			assert(pWave->wFormatTag == WAVE_FORMAT_PCM || pWave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);

			if(pWave->cbSize != 0)
			{
				return DMO_E_INVALIDTYPE;
			}

			return sample_factory_.Registered(SampleFormatId(pWave->wFormatTag == WAVE_FORMAT_PCM
				? KSDATAFORMAT_SUBTYPE_PCM : KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,
				pWave->wFormatTag, pWave->wBitsPerSample, pWave->wBitsPerSample)) ? S_OK : DMO_E_TYPE_NOT_ACCEPTED;
		}
	}

	// Same as CheckSampleFormat, but sets sample_convertor
	HRESULT SelectSampleConvertor(const WAVEFORMATEX* pWave, Holder<ConvertSample<T> > & sample_convertor, 
		typename NoiseShaper<T>::NoiseShapingType nNoiseShaper = NoiseShaper<T>::None,
		typename Ditherer<T>::DitherType nDither = Ditherer<T>::None) const
	{
		if(pWave == NULL)
		{
			return E_POINTER;
		}

		if ((pWave->nBlockAlign != pWave->nChannels * pWave->wBitsPerSample / 8) ||
			(pWave->nAvgBytesPerSec != pWave->nBlockAlign * pWave->nSamplesPerSec))
		{
			return DMO_E_INVALIDTYPE;
		}

		// Formats that support more than two channels or sample sizes of more than 16 bits can be described
		// in a WAVEFORMATEXTENSIBLE structure, which includes the WAVEFORMAT structure.
		if (pWave->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			const WAVEFORMATEXTENSIBLE* pWaveXT = (WAVEFORMATEXTENSIBLE*) pWave;

			if(pWaveXT->Format.cbSize < 22) // required (see http://www.microsoft.com/whdc/device/audio/multichaud.mspx)
			{
				return DMO_E_INVALIDTYPE;
			}
			assert(pWaveXT->Format.cbSize == 22); // for the types that we currently support

			sample_convertor.set_ptr(sample_factory_.Create(SampleFormatId(pWaveXT->SubFormat, WAVE_FORMAT_EXTENSIBLE,
				pWaveXT->Format.wBitsPerSample, pWaveXT->Samples.wValidBitsPerSample), nNoiseShaper, nDither,
				pWaveXT->Format.nChannels, pWaveXT->Format.nSamplesPerSec));

			return sample_convertor.get_ptr() == NULL ? DMO_E_TYPE_NOT_ACCEPTED : S_OK;

		}
		else // WAVE_FORMAT_PCM (8, or 16-bit) or WAVE_FORMAT_IEEE_FLOAT (32-bit)
		{
			assert(pWave->wFormatTag == WAVE_FORMAT_PCM || pWave->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);

			if(pWave->cbSize != 0)
			{
				return DMO_E_INVALIDTYPE;
			}

			sample_convertor.set_ptr(sample_factory_.Create(SampleFormatId(pWave->wFormatTag == WAVE_FORMAT_PCM 
				? KSDATAFORMAT_SUBTYPE_PCM : KSDATAFORMAT_SUBTYPE_IEEE_FLOAT,
				pWave->wFormatTag, pWave->wBitsPerSample, pWave->wBitsPerSample), nNoiseShaper, nDither,
				pWave->nChannels, pWave->nSamplesPerSec));

			return sample_convertor.get_ptr() == NULL ? DMO_E_TYPE_NOT_ACCEPTED : S_OK;
		}
	}

	virtual ~ConvertSampleMaker() {}

private:

	FactoryType sample_factory_;

	// No copying
	ConvertSampleMaker& operator= (const ConvertSampleMaker&);
	ConvertSampleMaker(const ConvertSampleMaker&);
};
