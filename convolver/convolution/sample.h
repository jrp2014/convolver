#pragma once

#include <ks.h>
#include <ksmedia.h>

template <typename T>
class Sample
{
public:
	virtual void GetSample(T& dstSample, BYTE* & srcContainer, const float& fAttenuationFactor, DWORD& nBytesProcessed) const = 0;	// converts sample into a T (eg, float), [-1..1]
	virtual void PutSample(BYTE* & dstContainer, T srcSample, DWORD& nBytesGenerated) const = 0;			// returns number of bytes processed
	virtual int nContainerSize() const = 0;

	virtual ~Sample(void) = 0;
};

template <typename T>
inline Sample<T>::~Sample(){}

// Specializations with the appropriate functions for accessing the sample buffer

// TODO: float with wValidBitsPerSample = 18; //Top 18 bits have data
template <typename T>
class Sample_ieeefloat : public Sample<T>
{
public:

	virtual void GetSample(T& dstSample, BYTE* & srcContainer, const float& fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		dstSample = *reinterpret_cast<float *>(srcContainer) * fAttenuationFactor;
		srcContainer += sizeof(float);
		nBytesProcessed += sizeof(float);
	}

	virtual void PutSample(BYTE* & dstContainer, T srcSample, DWORD& nBytesGenerated) const 
	{ 
		*((float *) dstContainer) = srcSample;
		//* reinterpret_cast<double*>(dstContainer) = static_cast<double>(srcSample); // TODO: find cleaner way to do this
		dstContainer += sizeof(float);
		nBytesGenerated += sizeof(float);
	}

	virtual int nContainerSize() const
	{
		return sizeof(float);
	}
};

template <typename T>
class Sample_ieeedouble : public Sample<T>
{
public:

	virtual void GetSample(T& dstSample, BYTE* & srcContainer, const float& fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		dstSample = *reinterpret_cast<double *>(srcContainer) * fAttenuationFactor;
		srcContainer += sizeof(double);
		nBytesProcessed += sizeof(double);
	}

	virtual void PutSample(BYTE* & dstContainer, T srcSample, DWORD& nBytesGenerated) const 
	{ 
		*((double *) dstContainer) = srcSample;
		//* reinterpret_cast<double*>(dstContainer) = static_cast<double>(srcSample); // TODO: find cleaner way to do this
		dstContainer += sizeof(double);
		nBytesGenerated += sizeof(double);
	}

	virtual int nContainerSize() const
	{
		return sizeof(double);
	}
};


// 8-bit sound is 0..255 with 128 == silence
template <typename T>
class Sample_pcm8 : public Sample<T>
{
public:
	virtual void GetSample(T& dstSample, BYTE* & srcContainer, const float& fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		dstSample = static_cast<T>((*srcContainer - static_cast<T>(128)) / static_cast<T>(128)) * fAttenuationFactor;  // Normalize to [-1..1]
		++srcContainer;
		++nBytesProcessed;
	}

	virtual void PutSample(BYTE* & dstContainer, T srcSample, DWORD& nBytesGenerated) const
	{   
		srcSample *= static_cast<T>(128);

		// Truncate if exceeded full scale.
		srcSample = (srcSample <= static_cast<T>(127)) ? ((srcSample >= static_cast<T>(-128)) ?  srcSample : -128) : 127;

		//if (srcSample > static_cast<T>(127))
		//{
		//	srcSample = static_cast<T>(127);
		//}
		//else
		//	if (srcSample < static_cast<T>(-128))
		//	{
		//		srcSample = static_cast<T>(-128);
		//	}

			*dstContainer = static_cast<BYTE>(srcSample + static_cast<T>(128));

			++dstContainer;
			++nBytesGenerated;
	}

	virtual int nContainerSize() const
	{
		return 1;
	}
};

// 16-bit sound is -32768..32767 with 0 == silence
template <typename T>
class Sample_pcm16 : public Sample<T>
{
public:
	virtual void  GetSample(T& dstSample, BYTE* & srcContainer, const float& fAttenuationFactor, DWORD& nBytesProcessed) const 
	{ 
		dstSample = static_cast<T>(*reinterpret_cast<const INT16*>(srcContainer) / static_cast<T>(32768)) * fAttenuationFactor; // TODO: find a cleaner way to do this
		srcContainer += 2;
		nBytesProcessed += 2;
	}

	virtual void PutSample(BYTE* & dstContainer, T srcSample, DWORD& nBytesGenerated) const
	{   
		srcSample *= static_cast<T>(32768);

		// Truncate if exceeded full scale.
		srcSample = (srcSample <= static_cast<T>(32767)) ? ((srcSample >= static_cast<T>(-32768)) ?  srcSample : -32768) : 32767;

		//if (srcSample > static_cast<T>(32767))
		//{
		//	srcSample = static_cast<T>(32767);
		//}
		//else
		//	if (srcSample < static_cast<T>(-32768))
		//	{
		//		srcSample =static_cast<T>(-32768);
		//	}
			*(INT16 *)dstContainer = static_cast<INT16>(srcSample);
			//*reinterpret_cast<INT16*>(dstContainer) = static_cast<INT16>(srcSample);

			dstContainer += 2;
			nBytesGenerated += 2;
	}

	virtual int nContainerSize() const
	{
		return 2;
	}
};

// 24-bit sound
template <typename T, int validBits>
class Sample_pcm24 : public Sample<T>
{
public:
	virtual void  GetSample(T& dstSample, BYTE* & srcContainer, const float& fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		int i = srcContainer[2];
		i = ( i << 8 ) | srcContainer[1];
		switch (validBits)
		{
		case 16:
			dstSample =  fAttenuationFactor * i / static_cast<T>(32768); // 2^15
		case 20:
			i = (i << 4) | (srcContainer[0] >> 4);
			dstSample =  fAttenuationFactor * i / static_cast<T>(524288); // 2^19
		case 24:
			i = (i << 8) | srcContainer[0];
			dstSample =  fAttenuationFactor * i / static_cast<T>(8388608); // 2^23
		default:
			assert(false);
			dstSample =  fAttenuationFactor * i / static_cast<T>(8388608); // 2^23;
		};

		srcContainer += 3;
		nBytesProcessed += 3;
	}

	virtual void PutSample(BYTE* & dstContainer, T srcSample, DWORD& nBytesGenerated) const
	{   
		int iClip = 0;
		switch (validBits)
		{
		case 16:
			srcSample *= static_cast<T>(32768);
			iClip = (1 << 15);
			break;
		case 20:
			srcSample *= static_cast<T>(524288);
			iClip = (1 << 19);
			break;
		case 24:
			srcSample *= static_cast<T>(8388608);
			iClip = (1 << 23);
			break;
		default:
			assert(false);
			srcSample *= static_cast<T>(8388608);
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
				i = lrintf(srcSample);	// cast to int

		dstContainer[0] = static_cast<BYTE>(i & 0xff);
		dstContainer[1] = static_cast<BYTE>((i >>  8) & 0xff);
		dstContainer[2] = static_cast<BYTE>((i >> 16) & 0xff);

		dstContainer += 3;
		nBytesGenerated += 3;
	}

	virtual int nContainerSize() const
	{
		return 3;
	}
};

// 32-bit sound
template <typename T, int validBits>
class Sample_pcm32 : public Sample<T>
{
public:
	virtual void GetSample(T& dstSample, BYTE* & srcContainer, const float& fAttenuationFactor, DWORD& nBytesProcessed) const
	{
		INT32 i = *reinterpret_cast<const INT32*>(srcContainer);
		switch (validBits)
		{
		case 16:
			i >>= 16;
			dstSample = fAttenuationFactor * i / static_cast<T>(32768); // 2^15;
		case 20:
			i >>= 12;
			dstSample = fAttenuationFactor * i / static_cast<T>(524288); // 2^19;
		case 24:
			i >>= 8;
			dstSample = fAttenuationFactor * i / static_cast<T>(8388608); // 2^23;
		case 32:
			dstSample = fAttenuationFactor * i / static_cast<T>(2147483648); // 2^31
		default:
			assert(false);
			dstSample = fAttenuationFactor * i / static_cast<T>(2147483648); // 2^31
		}

		srcContainer += 4;
		nBytesProcessed += 4;
	}

	virtual void PutSample(BYTE* & dstContainer, T srcSample, DWORD& nBytesGenerated) const
	{   
		INT32 iClip = 0;
		switch (validBits)
		{
		case 16:
			iClip = (1 << 15);
			srcSample *= static_cast<T>(32768); // 2^15;
			break;
		case 20:
			iClip = (1 << 19);
			srcSample *= static_cast<T>(524288); // 2^19;
			break;
		case 24:
			iClip = (1 << 23);
			srcSample *= static_cast<T>(8388608); // 2^23;
			break;
		case 32:
			iClip = (1 << 31);
			srcSample *=  static_cast<T>(2147483648);  // 2^31
			break;
		default:
			assert(false);
			iClip = (1 << 31);
			srcSample *=  static_cast<T>(2147483648);  // 2^31
		}

		// Truncate if exceeded full scale.
		if (srcSample > (iClip - 1))
		{
			srcSample = static_cast<T>(iClip - 1);
		}
		else
		{
			if (srcSample < -iClip)
			{
				srcSample = static_cast<T>(-iClip);
			}
		}
		*(INT32 *)dstContainer = static_cast<INT32>(srcSample);
		//*reinterpret_cast<INT32*>(dstContainer) = static_cast<INT32>(srcSample);

		dstContainer += 4;
		nBytesGenerated += 4;
	}

	virtual int nContainerSize() const
	{
		return 4;
	}
};

// TODO: this leads to memory leaks as 
template <typename T>
class CFormatSpecs
{
private:
	static const struct FormatSpec
	{
		GUID					SubType;
		WORD					wFormatTag;
		WORD					wBitsPerSample; // 
		WORD					wValidBitsPerSample;
		Sample<T>*				sample_convertor;
	} FormatSpecs_[];
public:

	static const int size = 15;

	CFormatSpecs() {}

	virtual ~CFormatSpecs() {}

	const FormatSpec& operator[](int i) const
	{
		assert(i >= 0 && i < size);
		return FormatSpecs_[i];
	}

	HRESULT SelectSampleConvertor(const WAVEFORMATEX* & pWave, Sample<T>* & sample_convertor)
	{
		if(pWave == NULL)
		{
			return E_POINTER;
		}

		// Formats that support more than two channels or sample sizes of more than 16 bits can be described
		// in a WAVEFORMATEXTENSIBLE structure, which includes the WAVEFORMAT structure.
		if (pWave->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		{
			for(int i = 0; i < size; ++i)
			{

				WAVEFORMATEXTENSIBLE* pWaveXT = (WAVEFORMATEXTENSIBLE*) pWave;
				if(	pWaveXT->Format.wFormatTag == FormatSpecs_[i].wFormatTag &&
					pWaveXT->Format.wBitsPerSample == FormatSpecs_[i].wBitsPerSample &&
					pWaveXT->Samples.wValidBitsPerSample == FormatSpecs_[i].wValidBitsPerSample)
				{
					sample_convertor = FormatSpecs_[i].sample_convertor;
					return S_OK;
				}
			}
		}
		else // WAVE_FORMAT_PCM (8, or 16-bit) or WAVE_FORMAT_IEEE_FLOAT (32-bit)
		{
			for(int i = 0; i < size; ++i)
			{
				if(	pWave->wFormatTag == FormatSpecs_[i].wFormatTag &&
					pWave->wBitsPerSample == FormatSpecs_[i].wBitsPerSample &&
					pWave->wBitsPerSample == FormatSpecs_[i].wValidBitsPerSample)
				{
					sample_convertor = FormatSpecs_[i].sample_convertor;
					return S_OK;
				}
			}
		}

		sample_convertor = NULL;
		return E_FAIL;
	}

private:
	// No copying
	CFormatSpecs& operator= (const CFormatSpecs&);
	CFormatSpecs(const CFormatSpecs&);
};

template <typename T>
const typename CFormatSpecs<T>::FormatSpec CFormatSpecs<T>::FormatSpecs_[CFormatSpecs<T>::size] =
{
	{KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_EXTENSIBLE, 32, 32, new Sample_ieeefloat<T>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 32, 32, new Sample_pcm32<T,32>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 24, 24, new Sample_pcm24<T,24>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 16, 16, new Sample_pcm16<T>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE,  8,  8, new Sample_pcm8<T>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 32, 24, new Sample_pcm32<T,24>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 32, 20, new Sample_pcm32<T,20>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 32, 16, new Sample_pcm32<T,16>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 24, 20, new Sample_pcm24<T,20>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_EXTENSIBLE, 24, 16, new Sample_pcm24<T,16>()},
	{KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_EXTENSIBLE, 64, 64, new Sample_ieeedouble<T>()},
	{KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_IEEE_FLOAT, 32, 32, new Sample_ieeefloat<T>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_PCM,		  16, 16, new Sample_pcm16<T>()},
	{KSDATAFORMAT_SUBTYPE_PCM,		  WAVE_FORMAT_PCM,		   8,  8, new Sample_pcm8<T>()},
	{KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, WAVE_FORMAT_IEEE_FLOAT, 64, 64, new Sample_ieeedouble<T>()}
};

//template class CFormatSpecs<float>;
