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
//
/////////////////////////////////////////////////////////////////////////////

#include "convolution\lrint.h"


// From the Intel Software Optimization Cookbook

void SIMDFlushToZero(void)
{
	DWORD SIMDCtrl;
	_asm
	{
		STMXCSR SIMDCtrl
			mov eax, SIMDCtrl
			// flush-to-zero = bit 15
			// mask underflow = bit 11
			// denormals = 0 are bit 6
			or eax, 08840h
			mov SIMDCtrl, eax
			LDMXCSR SIMDCtrl
	}
}


long int
lrint (double flt)
{
	int intgr;
	_asm
	{
		fld flt
			fistp intgr
	} ;

	return intgr;
}

long int
lrintf (float flt)
{
	int intgr ;
	_asm
	{
		fld flt
		fistp intgr
	} ;

	return intgr;
}

// From Fast Rounding of Floating Point Numbers in C/C++ on Wintel Platform
// Laurent de Soras
// 2004.07.04
// web: http://ldesoras.free.fr

// IntType may be Int16, 32, or 64; FloatType may be float or double
// Uses current rounding mode, assumed to be round to nearest
template <typename IntType, typename FloatType>
inline IntType conv_float_to_int (FloatType x)
{
	IntType a;
	__asm
	{
		fld x
		fistp a
	}
	return (a);
}

inline int conv_float_to_int_mem (double x)
{
	const int p = 52;
	const double c_p1 = static_cast <double> (1L << (p / 2));
	const double c_p2 = static_cast <double> (1L << (p - p / 2));
	const double c_mul = c_p1 * c_p2;
	assert (x > -0.5 * c_mul - 1);
	assert (x < 0.5 * c_mul);
	assert (x > static_cast <double> (INT_MIN) - 1.0);
	assert (x < static_cast <double> (INT_MAX) + 1.0);
	const double cs = 1.5 * c_mul;
	x += cs;
	const int a = *(reinterpret_cast <const int *> (&x));
	assert (a * x >= 0);
	return (a);
}

int round_int (double x)
{
	assert (x > static_cast <double> (INT_MIN / 2) - 1.0);
	assert (x < static_cast <double> (INT_MAX / 2) + 1.0);
	const float round_to_nearest = 0.5f;
	int i;
	__asm
	{
		fld x
			fadd st, st (0)
			fadd round_to_nearest
			fistp i
			sar i, 1
	}
	return (i);
}

// IntType may be Int16, 32, or 64; FloatType may be float or double
// Uses current rounding mode, assumed to be round to nearest
template <typename IntType, typename FloatType>
IntType floor_int (FloatType x)
{
	assert (x > static_cast <FloatType> (INT_MIN / 2) - 1.0);
	assert (x < static_cast <FloatType> (INT_MAX / 2) + 1.0);
	static const FloatType round_towards_m_i = FloatType(-0.5);
	IntType i;
	__asm
	{
		fld x
			fadd st, st (0)
			fadd round_towards_m_i
			fistp i
			sar i, 1
	}
	return (i);
}

int ceil_int (double x)
{
	assert (x > static_cast <double> (INT_MIN / 2) - 1.0);
	assert (x < static_cast <double> (INT_MAX / 2) + 1.0);
	const float round_towards_p_i = -0.5f;
	int i;
	__asm
	{
		fld x
			fadd st, st (0)
			fsubr round_towards_p_i
			fistp i
			sar i, 1
	}
	return (-i);
}


////Truncate mode is the default rounding mode in C. This consists in suppressing the decimals,
////giving a towards minus infinity mode for positive numbers, and towards plus infinity mode for
////negative numbers.
//int truncate_int (double x)
//{
//	assert (x > static_cast <double> (INT_MIN / 2) – 1.0);
//	assert (x < static_cast <double> (INT_MAX / 2) + 1.0);
//	const float round_towards_p_i = -0.5f;
//	int i;
//	__asm
//	{
//		fld x
//		fadd st, st (0)
//		fabs st (0)
//		fadd round_towards_m_i
//		fistp i
//		sar i, 1
//	}
//	if (x < 0)
//	{
//		i = -i;
//	}
//	return (i);
//}

//From Denormal numbers in floating point signal processing applications
//Laurent de Soras
//2005.04.19
//web: http://ldesoras.free.fr

void test_and_kill_denormal (float &val)
{ // Requires 32-bit int
	const int x = *reinterpret_cast <const INT32 *> (&val);
	const int abs_mantissa = x & 0x007FFFFF;
	const int biased_exponent = x & 0x7F800000;
	if (biased_exponent == 0 && abs_mantissa != 0)
	{
		val = 0;
	}
}

void kill_denormal_by_quantization (float &val)
{
	static const float anti_denormal = 1e-18f;
	val += anti_denormal;
	val -= anti_denormal;
}

bool is_denormalized ()
{
	short int status;
	__asm
	{
		fstsw word ptr [status] ; Retrieve FPU status word
			fclex ; Clear FPU exceptions (denormal flag)
	}
	return ((status & 0x0002) != 0);
}

unsigned int rand_state = 1; // Needs 32-bit int
void add_white_noise (float &val)
{ 
	rand_state = rand_state * 1234567UL + 890123UL;
	int mantissa = rand_state & 0x807F0000; // Keep only most significant bits (or rand_state & 0x007F0000 for +ve nos only)
	int flt_rnd = mantissa | 0x1E000000; // Set exponent
	val += *reinterpret_cast <const float *> (&flt_rnd);
}

void add_dc (float &val)
{
	static const float anti_denormal = 1e-20f;
	val += anti_denormal;
}


