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

#pragma once
#include "convolution\config.h"

void SIMDFlushToZero(void);
long int lrint (double flt);
long int lrintf (float flt);

template <class IntType, class FloatType>
IntType conv_float_to_int (FloatType x);

extern inline int conv_float_to_int (float x);
extern inline int conv_float_to_int_mem (double x);
extern int round_int (double x);

//template <typename IntType, typename FloatType>
//extern inline IntType floor_int (const FloatType x);
//
//extern BYTE floor_int (const float x);
//extern BYTE floor_int (const double x);
//
//template
//extern INT16 floor_int<INT16,float>(float);
//
//template
//extern INT32 floor_int<INT32,float>(float);

int ceil_int (double x);
//int truncate_int (double x);

extern void test_and_kill_denormal (float &val);
extern void kill_denormal_by_quantization (float &val);
extern bool is_denormalized ();
extern void add_white_noise (float &val);
extern void add_dc (float &val);
