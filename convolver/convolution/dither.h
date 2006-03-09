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
#include <time.h>
#include <boost\random.hpp>
#include <limits>

//// IntType may be Int16, 32, or 64; T may be float or double
//// Uses current rounding mode, assumed to be round to nearest
//template <typename IntType, typename T>
//inline IntType floor_int (const T x)
//{
//#undef min
//#undef max
//	assert (x > static_cast <T> (std::numeric_limits<IntType>::min()/2 - 1.0));
//	assert (x < static_cast <T> (std::numeric_limits<IntType>::max()/2 + 1.0));
//	static const T round_towards_m_i = T(-0.5);
//	IntType i;
//	__asm
//	{
//		fld x
//			fadd st, st (0)
//			fadd round_towards_m_i
//			fistp i
//			sar i, 1
//	}
//	return (i);
//}

// floor_int does not work for |x| > maxint/2
// So use the following, which assumes round to nearest rounding mode
template <typename IntType, typename T>
inline IntType conv_float_to_int (T x)
{
	IntType a;
	static const T round_towards_m_i = T(-0.5);
	__asm
	{
		fld x
			fadd round_towards_m_i
			fistp a
	}
	return a;
}

template <typename T>
class Dither
{
public:
	// Generate the dither
	virtual T dither(const WORD nChannel) = 0;

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
	T dither(const WORD nChannel)
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

	// Dithering may vary or be optimizable by the number of channels or sample rate
	SimpleDither(const WORD nChannels) : nChannels_(nChannels),
	  generator_(unsigned int(std::time(NULL))), gen_dither_(generator_, distribution_type(-Qover2, Qover2))
	{}

	// Generate the dither
	virtual T dither(const WORD nChannel) = 0;

	virtual ~SimpleDither(void) {};

protected:
	static const T Q;				// Least significant bit
	static const T Qover2;

	typedef boost::variate_generator<base_generator_type&, distribution_type> gen_type;
	gen_type gen_dither_;
	base_generator_type generator_;

	const WORD nChannels_;
};

template <typename T, unsigned short validBits,
typename distribution_type, typename base_generator_type >
const T  SimpleDither<T, validBits, distribution_type, base_generator_type>::Q = 2.0 / ((1 << (validBits-2)) - 1 + (1 << (validBits-2)));

template <typename T, unsigned short validBits,
typename distribution_type, typename base_generator_type >
const T  SimpleDither<T, validBits, distribution_type, base_generator_type>::Qover2 = 1.0 / ((1 << (validBits-2)) - 1 + (1 << (validBits-2)));

template <typename T, unsigned short validBits> 
class RectangularDither : public SimpleDither<T, validBits>
{
public:
	RectangularDither(WORD nChannels) : SimpleDither<T, validBits>(nChannels)
	{};

	// Generate the dither
	T dither(const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		return gen_dither_();
	}
};


template <typename T, unsigned short validBits>
class TriangularDither : public SimpleDither<T, validBits>
{
public:
	TriangularDither(WORD nChannels) : SimpleDither<T, validBits>(nChannels), prev_dither_(nChannels, gen_dither_())
	{};

	// Generate the triangular dither by differencing successive rectangular dither values
	T dither(const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		const T dither = gen_dither_();
		const T result = dither - prev_dither_[nChannel];
		prev_dither_[nChannel] = dither;
		return result;
	}

private:
	std::vector<T> prev_dither_;
};

// Optimization for 2 channels; requires only 1 extra random number per channel cycle
template <typename T, unsigned short validBits>
class StereoTriangularDither : public SimpleDither<T, validBits>
{
public:
	StereoTriangularDither() : SimpleDither<T, validBits>(2), prev_dither_(gen_dither_()), right_dither_(gen_dither_())
	{};

	// Generate the triangular dither by summing and differencing successive rectangular dither values
	T dither(const WORD nChannel)
	{
		assert(nChannels_ == 2);
		assert(nChannel < nChannels_);
		if(nChannel == 0)
		{
			const T dither = gen_dither_();
			const T left_dither = dither - prev_dither_;
			right_dither_ = dither + prev_dither_;
			prev_dither_ = dither;
			return left_dither;
		}
		else
		{
			return right_dither_;
		}
	}

private:
	T prev_dither_;
	T right_dither_;
};


template <typename T>
class Ditherer
{
public:

	/// These ditherers are currently available:
	enum DitherType { None = 0, Triangular= 1,  Rectangular = 2 };

	static const unsigned int nDitherers = 3;
	static const unsigned int nStrLen = sizeof(TEXT("Rectangular")) / sizeof(TCHAR) + 1;

	static const TCHAR Description[nDitherers][nStrLen];

	HRESULT SelectDither(DitherType dt, const WAVEFORMATEX* pWave);

	unsigned int Lookup(const TCHAR* r) const
	{
		for(unsigned int i=0; i<nDitherers; ++i)
		{
			if(_tcsncicmp(r, Description[i], nStrLen) == 0)
				return i;
		}
		throw std::range_error("Internal Error: Invalid dither");
	}
};
template <typename T>
const TCHAR Ditherer<T>::Description[Ditherer<T>::nDitherers][Ditherer<T>::nStrLen] =
{
	TEXT("None"),
	TEXT("Triangular"),
	TEXT("Rectangular")
};


template <typename T, typename IntType>
struct NoiseShape
{
	// Generate the dither
	virtual IntType shapenoise(const T sample, Dither<T> * dither, const WORD nChannel) = 0;

	virtual ~NoiseShape(void) = 0;
};
template <typename T, typename IntType>
inline NoiseShape<T,IntType>::~NoiseShape(void){}

template <typename T, typename IntType, int validBits>
class NoNoiseShape : public NoiseShape<T,IntType>
{
public:
	NoNoiseShape(const WORD nChannels) : nChannels_(nChannels)
	{}

	// Generate the dither
	virtual IntType shapenoise(const T sample, Dither<T> * dither, const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		return conv_float_to_int<IntType,T>((dither->dither(nChannel) + sample) * q_);	// just scale (eg from float to IntType)
	}

	virtual ~NoNoiseShape(void) {}

protected:
	static const T Q_;				// Least significant bit
	static const T Qover2_;
	static const T q_;
	const WORD nChannels_;
};

template <typename T, typename IntType, int validBits>
const T  NoNoiseShape<T, IntType, validBits>::Q_ = 1.0 / ((1 << (validBits-2)) - 0.5 + (1 << (validBits-2)));

template <typename T, typename IntType, int validBits>
const T  NoNoiseShape<T, IntType, validBits>::Qover2_ = 0.5 / ((1 << (validBits-2)) - 0.5 + (1 << (validBits-2)));

template <typename T, typename IntType, int validBits>
const T  NoNoiseShape<T, IntType, validBits>::q_ = ((1 << (validBits-2)) - 0.5 + (1 << (validBits-2)));


// SimpleShaping 
template <typename T, typename IntType, int validBits>
class SimpleNoiseShape : public NoNoiseShape<T, IntType, validBits>
{
public:
	SimpleNoiseShape(const WORD nChannels) : NoNoiseShape<T, IntType, validBits>(nChannels), e_(nChannels, 0)
	{}

	// Generate shaped sample
	virtual IntType shapenoise(const T sample,  Dither<T> * dither, const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		const T d = dither->dither(nChannel);
		const T H = e_[nChannel];
		const T y = sample + d - H;
		const IntType yint = conv_float_to_int<IntType,T>(y * q_);		// just scale (eg from float to IntType)
		e_[nChannel] = Q_ * (yint + T(0.5)) - (sample - H);				// generate the feedback for this channel
		return yint;	
	}

	virtual ~SimpleNoiseShape(void) {}

private:
	std::vector<T> e_;				// error feedback
};


// 2nd order Shaping 
template <typename T, typename IntType, int validBits>
class SecondOrderNoiseShape : public NoNoiseShape<T, IntType, validBits>
{
public:
	SecondOrderNoiseShape(const WORD nChannels) : NoNoiseShape<T, IntType, validBits>(nChannels), 
		pos_(nChannels, 0), e_(nChannels,std::vector<T>(4, 0))
	{}

	// Generate shaped sample
	virtual IntType shapenoise(const T sample,  Dither<T> * dither, const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		const T d = dither->dither(nChannel);

		unsigned int pos = pos_[nChannel];

		const T H = e_[nChannel][pos] * T(1.287) + e_[nChannel][pos+1]*T(-0.651);
		const T y = sample + d - H;

		const IntType yint = conv_float_to_int<IntType,T>(y * q_);		// just scale (eg from float to IntType)

		pos = 1 - pos;
		e_[nChannel][pos+2] = e_[nChannel][pos] = Q_ * (yint + T(0.5)) - (sample - H);
		pos_[nChannel] = pos;

		return yint;	
	}

	virtual ~SecondOrderNoiseShape(void) {}

private:
	std::vector<unsigned int> pos_;
	std::vector<std::vector<T> > e_;				// error feedback
};


// 3rd order Shaping 
template <typename T, typename IntType, int validBits>
class ThirdOrderNoiseShape : public NoNoiseShape<T, IntType, validBits>
{
public:
	ThirdOrderNoiseShape(const WORD nChannels) : NoNoiseShape<T, IntType, validBits>(nChannels), 
		pos_(nChannels, 2), e_(nChannels,std::vector<T>(6, 0))
	{}

	// Generate shaped sample
	virtual IntType shapenoise(const T sample,  Dither<T> * dither, const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		const T d = dither->dither(nChannel);

		unsigned int pos = pos_[nChannel];

		const T H = e_[nChannel][pos] * T(1.329) + e_[nChannel][pos+1]*T(-0.735) + e_[nChannel][pos+2]*T(0.0646);
		const T y = sample + d - H;
	
		const IntType yint = conv_float_to_int<IntType,T>(y * q_);		// just scale (eg from float to INT32)


		//cdebug << "sample " << sample << " y " << y << " yint " << yint <<std::endl;
		//cdebug << "H " << H << " e_[" << nChannel << "][" << pos << "]=" << e_[nChannel][pos] << 
		//	" e_[" << nChannel << "][" << pos+1 << "]=" << e_[nChannel][pos+1] << 
		//	" e_[" << nChannel << "][" << pos+2 << "]=" << e_[nChannel][pos+2] << std::endl;

		if(pos == 0)
		{
			pos = 2; 
		}
		else
		{
			--pos;
		}
		e_[nChannel][pos+3] = e_[nChannel][pos] = Q_ * (yint + T(0.5)) - (sample - H);
		pos_[nChannel] = pos;

		return yint;	
	}

	virtual ~ThirdOrderNoiseShape(void) {}

private:
	std::vector<unsigned int> pos_;
	std::vector<std::vector<T> > e_;				// error feedback
};

// 9th order Shaping 
template <typename T, typename IntType, int validBits>
class NinthOrderNoiseShape : public NoNoiseShape<T, IntType, validBits>
{
public:
	NinthOrderNoiseShape(const WORD nChannels) : NoNoiseShape<T, IntType, validBits>(nChannels), 
		pos_(nChannels, 8), e_(nChannels,std::vector<T>(18, 0))
	{}

	// Generate shaped sample
	virtual IntType shapenoise(const T sample,  Dither<T> * dither, const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		const T d = dither->dither(nChannel);

		unsigned int pos = pos_[nChannel];

		const T H = e_[nChannel][pos]*T(2.203) + e_[nChannel][pos+1]*T(-3.210) + e_[nChannel][pos+2]*T(3.970) +
			e_[nChannel][pos+3]*T(-4.404) + e_[nChannel][pos+4]*T(3.668) + e_[nChannel][pos+5]*T(-2.656) +
			e_[nChannel][pos+6]*T(1.644) + e_[nChannel][pos+7]*T(-0.767) + e_[nChannel][pos+8]*T(0.118);

		const T y = sample + d - H;
	
		const IntType yint = conv_float_to_int<IntType,T>(y * q_);		// just scale (eg from float to INT32)

		if(pos == 0)
		{
			pos = 8; 
		}
		else
		{
			--pos;
		}
		e_[nChannel][pos+9] = e_[nChannel][pos] = Q_ * (yint + T(0.5)) - (sample - H);
		pos_[nChannel] = pos;

		return yint;	
	}

	virtual ~NinthOrderNoiseShape(void) {}

private:
	std::vector<unsigned int> pos_;
	std::vector<std::vector<T> > e_;				// error feedback
};

// Pseudo Sony SBM(TM) 
template <typename T, typename IntType, int validBits>
class SonySBM : public NoNoiseShape<T, IntType, validBits>
{
public:
	SonySBM(const WORD nChannels) : NoNoiseShape<T, IntType, validBits>(nChannels), 
		pos_(nChannels, 11), e_(nChannels,std::vector<T>(24, 0))
	{}

	// Generate shaped sample
	virtual IntType shapenoise(const T sample,  Dither<T> * dither, const WORD nChannel)
	{
		assert(nChannel < nChannels_);
		const T d = dither->dither(nChannel);

		unsigned int pos = pos_[nChannel];

		const T H = e_[nChannel][pos]*T(1.47933) + e_[nChannel][pos+1]*T(-1.59032) + e_[nChannel][pos+2]*T(1.64436) +
			e_[nChannel][pos+3]*T(-1.36613) + e_[nChannel][pos+4]*T(0.926704) + e_[nChannel][pos+5]*T(-0.557931) +
			e_[nChannel][pos+6]*T(-0.267859) + e_[nChannel][pos+7]*T(-0.106726) + e_[nChannel][pos+8]*T(-0.0285161) +
			e_[nChannel][pos+9]*T(0.00123066) + e_[nChannel][pos+10]*T(-0.00616555) + e_[nChannel][pos+11]*T(0.003067);

		const T y = sample + d - H;
	
		const IntType yint = conv_float_to_int<IntType,T>(y * q_);		// just scale (eg from float to INT32)

		if(pos == 0)
		{
			pos = 11; 
		}
		else
		{
			--pos;
		}
		e_[nChannel][pos+12] = e_[nChannel][pos] = Q_ * (yint + T(0.5)) - (sample - H);
		pos_[nChannel] = pos;

		return yint;	
	}

	virtual ~SonySBM(void) {}

private:
	std::vector<unsigned int> pos_;
	std::vector<std::vector<T> > e_;				// error feedback
};

template <typename T>
struct NoiseShaper
{
	/// These noise shapers are currently available:
	enum NoiseShapingType { None=0, Simple=1, SecondOrder=2, ThirdOrder=3, NinthOrder=4, SonySBM=5};

	static const unsigned int nNoiseShapers = 6;
	static const unsigned int nStrLen = sizeof(TEXT("Simple noise shaping")) / sizeof(TCHAR) + 1;

	static const TCHAR Description[nNoiseShapers][nStrLen];

	unsigned int Lookup(const TCHAR* r) const
	{
		for(unsigned int i=0; i<nNoiseShapers; ++i)
		{
			if(_tcsncicmp(r, Description[i], nStrLen) == 0)
				return i;
		}
		throw std::range_error("Invalid Noise Shaper");
	}
};

template <typename T>
const TCHAR NoiseShaper<T>::Description[NoiseShaper<T>::nNoiseShapers][NoiseShaper<T>::nStrLen] =
{
	TEXT("No noise shaping"),
		TEXT("Simple noise shaping"),
		TEXT("2nd order shaping"),
		TEXT("3rd order shaping"),
		TEXT("9th order shaping"),
		TEXT("Pseudo Sony SBM")
};
