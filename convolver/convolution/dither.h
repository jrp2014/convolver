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
#pragma once

//1) Round-to-Zero-Truncation
//~~~~~~~~~~~~~~~~~~~~~~~~~~~
//Round samples down to zero.

//2) Rounding
//~~~~~~~~~~~
//Round samples to nearest integer.

//3) Additive Dithering using triangular distributed white noise
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//Add a on (-1,+1) triangular distributed white noise to the signal, then perform a rounding to the nearest integer. 
//A on (-1,+1) triangular distributed white noise can be calculated by adding two independent on (-0.5,+0.5) equi-distributed noises.

//4) Additive Dithering using triangular distributed triangular shaped noise
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//Add a on (-1,+1) triangular distributed triangular shaped noise to the signal, then perform a rounding to the nearest integer.
//A on (-1,+1) triangular distributed triangular shaped noise can be calculated by differentiating a on (0,+1) equi-distributed noise.

//5) Additive Dithering using normal distributed colored noise
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
//Add a colored noise to the signal, then perform a rounding to the nearest integer. 
//Colored noise can be calculated by filtering white noise, mostly using a recursive filter. 
//Normally it is not possible to dither with triangular distributed noise, the coloring transforms nearly every noise to a gaussian-like noise.

//6) Subtractive Dithering
//~~~~~~~~~~~~~~~~~~~~~~~~
//Add a on (-1,+1) triangular distributed white noise to the signal, then perform a rounding to the nearest integer. 
//A on (-1,+1) triangular distributed noise can be calculated by adding two independent on (-0.5,+0.5) equi-distributed noises.
//Noise must be calculated by a pseudo-random generator.
//On the D/A converter stage exactly the same pseudo-random generator generating exactly the same random number runs and subtracts
//the dither from the quantized signal.

//7) n-th order Noise Shaping
//~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
//Add a weighted sum of the last n quantization errors to the signal, then perform a rounding to the nearest integer.

//8) 1st order Noise Shaping + Additive Dithering using triangular distributed triangular shaped noise
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//Combines the principles of a 1st order Noise Shaping with the Additive Dithering using triangular distributed triangular shaped noise. 
//Dither is triangular distributed.

//9) n-th order Noise Shaping + Additive Dithering using colored noise
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//Combines the principles of a n-th order Noise Shaping with Additive Dithering using normal distributed colored noise. 
//Mostly the error weighting and the noise weighting used for dithering is the same. 
//Normally it is not possible to dither with triangular distributed noise, the coloring transforms nearly every noise to a gaussian-like noise.

//10) n-th order Noise Shaping + Subtractive Dithering
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~ 
//Combines the principles of a n-th order Noise Shaping with Subtractive Dithering using triangular distributed white noise.

//Typical Technical Data
//~~~~~~~~~~~~~~~~~~~~~~ 
//Relative to method 2) for 44.1 kHz in dB: 
//							1	2	3		4		5		6	7		8	9		10
//--------------------------------------------------------------------------------
//Audible Noise 44 kHz		+6	0	+4.8	+1		+0.5	0	-15		-5	-10		-15
//Technical Noise 44 kHz	+6	0	+4.8	+6		+12		0	+30		+6	+35		-15
//Audible Noise 96 kHz		+3	-3	+1.8	-2.8	-2.8	-3	-39		-8	-34		-39
//Technical Noise 96 kHz	+6	0	+4.8	+6		+6		0	+20		+6	+25		-36
//Distortions				+6	0	-		-		<<		-	-15		-	<<		- 
//Need playback modifications for optimum performance
//							yes yes -		:		no distortions 
//															<<	:		very few distortions 
//Notes
//~~~~~ 
//Most computer programs, but also a lot of produced CDs use method 1).
//For a 16 bit PCM system this is at most 15 bit precision for low level signals.


//Simple Quantization: 
//X = [x] 
//
//Simple Dithering: 
//X = [x + n] 
//
//Colored Dithering: 
//X = [x + (n,F)] 
//
//Noise Shaping: 
//X = [x + ((X-x),F)] 
//
//Dithering + Noise Shaping: 
//X = [x + ((X-x),F) + (n,F)] 

#include "convolution\config.h"
#include "convolution\holder.h"
#include <ks.h>
#include <ksmedia.h>
#include <mediaerr.h>

#include <boost\random.hpp>

template <typename T>
class Dither
{
public:
	// Generate the dither
	virtual T dither() = 0;

	virtual ~Dither(void) = 0;
};

template <typename T>
inline Dither<T>::~Dither(){}

template <typename T>
class NoDither : public Dither<T>
{
public:

	NoDither() {}

	// Generate the dither
	T dither()
	{
		return 0;
	}

	~NoDither(void) {};
};

//  

template <typename T, unsigned short validBits, 
typename distribution_type = boost::uniform_real<T>, 
typename base_generator_type = boost::random::lagged_fibonacci_01<T, 48, 607, 273> >
class SimpleDither : public Dither<T>
{
public:

	SimpleDither() : gen_dither_(generator_, distribution_type(-Qover2, Qover2))
	{};

	// Dithering may vary or be optimizable by the number of channels or sample rate
	SimpleDither(WORD nChannels) : gen_dither_(generator_, distribution_type(-Qover2, Qover2))
	{};

	// Generate the dither
	virtual T dither()
	{
		return 0;
	}

	virtual ~SimpleDither(void) {};

protected:
	static const T Q;				// Least significant bit
	static const T Qover2;

	typedef boost::variate_generator<base_generator_type&, distribution_type> gen_type;
	gen_type gen_dither_;
	base_generator_type generator_;
};

template <typename T, unsigned short validBits,
typename distribution_type, typename base_generator_type >
const T  SimpleDither<T, validBits, distribution_type, base_generator_type>::Q = 2.0 / ((1 << validBits) - 1);

template <typename T, unsigned short validBits,
typename distribution_type, typename base_generator_type >
const T  SimpleDither<T, validBits, distribution_type, base_generator_type>::Qover2 = 1.0 / ((1 << validBits) - 1);

template <typename T, unsigned short validBits> 
class RectangularDither : public SimpleDither<T, validBits>
{
public:
	RectangularDither() : SimpleDither<T, validBits>()
	{};

	// Generate the dither
	T dither()
	{
		return gen_dither_();
	}
};


template <typename T, unsigned short validBits>
class TriangularDither : public SimpleDither<T, validBits>
{
public:
	TriangularDither(WORD nChannel) : SimpleDither<T, validBits>(nChannel)
	{};

	// Generate the dither
	T dither()
	{
		return gen_dither_() + gen_dither_();		// TODO:: Optimize this
	}
};

// TODO:: turn into a factory class
template <typename T>
class Ditherer
{
public:

	/// These ditherers are currently available:
	enum DitherType { None = 0, Triangular= 1,  Rectangular = 2 };

	static const unsigned int nDitherers = 3;
	static const unsigned int nStrLen = 15;

	TCHAR Description[nDitherers][nStrLen];

	HRESULT SelectDither(DitherType dt, const WAVEFORMATEX* pWave);

	/// Default constructor
	Ditherer() : ditherer_(NULL)
	{
		_tcsncpy(Description[None], TEXT("None"), nStrLen);
		_tcsncpy(Description[Triangular], TEXT("Triangular"), nStrLen);
		_tcsncpy(Description[Rectangular], TEXT("Rectangular"), nStrLen);
	}

	Dither<T>* ditherer() const
	{
		return ditherer_.get_ptr();
	}

	unsigned int Lookup(const TCHAR* r) const
	{
		for(unsigned int i=0; i<nDitherers; ++i)
		{
			if(_tcsncicmp(r, Description[i], nStrLen) == 0)
				return i;
		}
		throw std::range_error("Internal Error: Invalid dither");
	}
private:
	Holder< Dither<T> > ditherer_;
};

template <typename T>
HRESULT Ditherer<T>::SelectDither(DitherType dt, const WAVEFORMATEX* pWave)
{
	if(pWave == NULL)
	{
		ditherer_.set_ptr(new NoDither<T>()); // Equivalent to no dither
		return S_OK;
	}

	if ((pWave->nBlockAlign != pWave->nChannels * pWave->wBitsPerSample / 8) ||
		(pWave->nAvgBytesPerSec != pWave->nBlockAlign * pWave->nSamplesPerSec))
	{
		return DMO_E_INVALIDTYPE;
	}

	WORD validBits = 0;
	WORD nChannels = 0;

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

		validBits = pWaveXT->Samples.wValidBitsPerSample;
		nChannels = pWaveXT->Format.nChannels;
	}
	else // WAVE_FORMAT_PCM (8, 16 or 32-bit) or WAVE_FORMAT_IEEE_FLOAT (32-bit)
	{
		// TODO:: reject WAVE_FORMAT_IEEE_FLOAT ??

		if(pWave->cbSize != 0)
		{
			return DMO_E_INVALIDTYPE;
		}

		validBits = pWave->wBitsPerSample;
		nChannels = pWave->nChannels;
	}

	switch (dt)
	{
	case None:
		ditherer_.set_ptr(new NoDither<T>());
		break;
	case Triangular:
		switch (validBits)
		{
		case 16:
			ditherer_.set_ptr(new TriangularDither<T, 16>(nChannels));
			break;
		case 20:
			ditherer_.set_ptr(new TriangularDither<T, 20>(nChannels));
			break;
		case 24:
			ditherer_.set_ptr(new TriangularDither<T, 24>(nChannels)); 
			break;
		default:
			ditherer_.set_ptr(new NoDither<T>());
		}
		break;
	case Rectangular:
		switch (validBits)
		{
		case 16:
			ditherer_.set_ptr(new RectangularDither<T, 16>());
			break;
		case 20:
			ditherer_.set_ptr(new RectangularDither<T, 20>());
			break;
		case 24:
			ditherer_.set_ptr(new RectangularDither<T, 24>()); 
			break;
		default:
			ditherer_.set_ptr(new NoDither<T>());
		}
		break;
	default:
		throw std::range_error("Internal error: Invalid SelectDitherer type");
	}

	return S_OK;
}

template <typename T>
class NoiseShape
{
public:
	// Generate the dither
	virtual INT32 shapenoise(T sample, Dither<T>* dither) = 0;

	virtual ~NoiseShape(void) = 0;
};
template <typename T>
inline NoiseShape<T>::~NoiseShape(){}

template <typename T, int validBits>
class NoNoiseShape : public NoiseShape<T>
{
public:
	NoNoiseShape() {}

	// Generate the dither
	virtual INT32 shapenoise(T sample, Dither<T>* dither)
	{
		return floor_int<INT32,T>((dither->dither() + sample) * q_);	// just scale (eg from float to INT32)
	}

	virtual ~NoNoiseShape(void) {}

protected:
	static const T Q_;				// Least significant bit
	static const T Qover2_;
	static const T q_;
};

template <typename T, int validBits>
const T  NoNoiseShape<T, validBits>::Q_ = 1.0 / ((1 << (validBits-1)) - 0.5);

template <typename T, int validBits>
const T  NoNoiseShape<T, validBits>::Qover2_ = 0.5 / ((1 << (validBits-1)) - 0.5);

template <typename T, int validBits>
const T  NoNoiseShape<T, validBits>::q_ = ((1 << (validBits-1)) - 0.5);


// SimpleShaping 
template <typename T, int validBits>
class SimpleNoiseShape : public NoNoiseShape<T, validBits>
{
public:
	SimpleNoiseShape(WORD nChannels) : NoNoiseShape<T, validBits>(), e_(nChannels, 0), nChannels_(nChannels), nChannel_(0)
	{}

	// Generate shaped sample
	virtual INT32 shapenoise(T sample, Dither<T>* dither)
	{
		const T d = dither->dither();
		const T y = sample + d - e_[nChannel_];
		const INT32 yint = floor_int<INT32,T>(y * q_);			// just scale (eg from float to INT32)
		e_[nChannel_] = Q_ * (yint + T(0.5)) - (y - d);			// generate the feedback for this channel
		if(nChannel_++ == nChannels_)
			nChannel_ = 0;	
		return yint;	
	}

	virtual ~SimpleNoiseShape(void) {}

private:
	std::vector<T> e_;				// error feedback
	const WORD nChannels_;
	WORD nChannel_;
};


// TODO:: turn into a factory class
template <typename T>
class NoiseShaper
{
public:

	/// These ditherers are currently available:
	enum NoiseShapingType { None = 0, Simple= 1 };

	static const unsigned int nNoiseShapers = 2;
	static const unsigned int nStrLen = 21;

	TCHAR Description[nNoiseShapers][nStrLen];

	HRESULT SelectNoiseShaper(NoiseShapingType nt, const WAVEFORMATEX* pWave);

	/// Default constructor
	NoiseShaper() : noiseshape_(NULL)
	{
		_tcsncpy(Description[None], TEXT("No noise shaping"), nStrLen);
		_tcsncpy(Description[Simple], TEXT("Simple noise shaping"), nStrLen);
	}

	NoiseShape<T>* noiseshaper() const
	{
		return noiseshape_.get_ptr();
	}

	unsigned int Lookup(const TCHAR* r) const
	{
		for(unsigned int i=0; i<nNoiseShapers; ++i)
		{
			if(_tcsncicmp(r, Description[i], nStrLen) == 0)
				return i;
		}
		throw std::range_error("Invalid Noise Shaper");
	}
private:
	Holder< NoiseShape<T> > noiseshape_;
};


template <typename T>
HRESULT NoiseShaper<T>::SelectNoiseShaper(NoiseShapingType nt, const WAVEFORMATEX* pWave)
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

	// TODO:: will need to use Sample Rate to select shapers
	WORD wBitsPerSample = 0;
	WORD validBits = 0;
	WORD nChannels = 0;

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

		validBits = pWaveXT->Samples.wValidBitsPerSample;
		nChannels = pWaveXT->Format.nChannels;
		wBitsPerSample = pWaveXT->Format.wBitsPerSample;
	}
	else // WAVE_FORMAT_PCM (8, 16 or 32-bit) or WAVE_FORMAT_IEEE_FLOAT (32-bit)
	{
		// TODO:: reject WAVE_FORMAT_IEEE_FLOAT ??
		if(pWave->cbSize != 0)
		{
			return DMO_E_INVALIDTYPE;
		}

		validBits = pWave->wBitsPerSample;
		nChannels = pWave->nChannels;
		wBitsPerSample = pWave->wBitsPerSample;
	}

	switch (nt)
	{
	case None:
		switch (wBitsPerSample)
		{
		case 8:		// Apparently shaping 8-bit channels is not a good idea
			noiseshape_.set_ptr(new NoNoiseShape<T,8>());
			break;
		case 16:
			{
				assert(validBits == 16);
				noiseshape_.set_ptr(new NoNoiseShape<T,16>());
				break;
			}
			break;
		case 24:
		case 32:
			switch (validBits)
			{
			case 8:
				std::range_error("Internal error: NoiseShaper: NoNoiseShape: 8 is not an acceptable number of bits for 24 or 32-bit containers");
				break;
			case 16:
				noiseshape_.set_ptr(new NoNoiseShape<T,16>());
				break;
			case 20:
				noiseshape_.set_ptr(new NoNoiseShape<T,20>());
				break;
			case 24:
				noiseshape_.set_ptr(new NoNoiseShape<T,24>());
				break;
			case 32:
				{
					assert(validBits == 32);
					noiseshape_.set_ptr(new NoNoiseShape<T,32>());
				}
				break;
			default:
				throw std::range_error("Internal error: NoiseShaper: NoNoiseShaping: Invalid valid bits for 24 or 32-bit containers");
			}
		}
		break;
	case Simple:
		switch (wBitsPerSample)
		{
		case 8:		// Apparently shaping 8-bit channels is not a good idea
			noiseshape_.set_ptr(new NoNoiseShape<T,8>());
			break;
		case 16:
			{
				assert(validBits == 16);
				noiseshape_.set_ptr(new SimpleNoiseShape<T,16>(nChannels));
				break;
			}
			break;
		case 24:
		case 32:
			switch (validBits)
			{
			case 8:
				std::range_error("Internal error: NoiseShaper: SimpleNoiseShape:  8 is not an acceptable number of bits for 24 or 32-bit containers");
				break;
			case 16:
				noiseshape_.set_ptr(new SimpleNoiseShape<T,16>(nChannels));
				break;
			case 20:
				noiseshape_.set_ptr(new SimpleNoiseShape<T,20>(nChannels));
				break;
			case 24:
				noiseshape_.set_ptr(new SimpleNoiseShape<T,24>(nChannels));
				break;
			case 32:
				{
					assert(validBits == 32);
					noiseshape_.set_ptr(new SimpleNoiseShape<T,32>(nChannels));
				}
				break;
			default:
				throw std::range_error("Internal error: NoiseShaper: SimpleNoiseShape: Invalid valid bits for 24 or 32-bit containers");
			}
		}
		break;
	default:
		throw std::range_error("Internal error: NoiseShaper: Invalid bits per sample");
	}

	return S_OK;
}
