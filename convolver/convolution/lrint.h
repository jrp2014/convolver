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

inline int conv_float_to_int (float x);
inline int conv_float_to_int_mem (double x);
int round_int (double x);
int floor_int (double x);
int ceil_int (double x);
//int truncate_int (double x);

void test_and_kill_denormal (float &val);
void kill_denormal_by_quantization (float &val);
bool is_denormalized ();
void add_white_noise (float &val);
void add_dc (float &val);
