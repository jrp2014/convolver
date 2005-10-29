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


#ifndef STRICT
#define STRICT
#endif

// If app hasn't choosen, set to work with Windows 98, Windows Me, Windows 2000, Windows XP and beyond
#ifndef WINVER
#define WINVER         0x0410
#endif
#ifndef _WIN32_WINDOWS
#define _WIN32_WINDOWS 0x0410 
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT   0x0500 
#endif

#include <assert.h>
#include <wchar.h>
#include <math.h>
#include <windows.h>
#include <mmsystem.h>
#include <limits.h>      
#include <stdio.h>

// Debugging

#if defined(DEBUG) | defined(_DEBUG)

// CRT's memory leak detection
#define CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include "debugging\debugging.h"
#include "debugging\debugStream.h"

#define DIRAC_DELTA_FUNCTIONS

#endif

#ifndef __INTEL_COMPILER
#define restrict
#endif



// FFT routines
//#define OOURA	1
//#define SIMPLE_OOURA	1
#define FFTW	1

#if defined(OOURA) || defined(SIMPLE_OOURA)
#include "fft\fftsg_h.h"

#elif defined(FFTW)
#include "fftw\fftw3.h"

#else
#error "No FFT package defined"
#endif


// using home grown array
#define FASTARRAY
// User array initialization, rather than vector initialization
// Unimplemented
#undef ARRAY

// Use LibSndFile, rather than the DirectX samples
#define LIBSNDFILE 1


const DWORD MAX_ATTENUATION = 1000; // dB
const int MAX_CHANNEL = 32; // Maximum number of channels (used just for sanity checking)

// Number of filter lengths used to calculate optimum attenuation
const int NSAMPLES = 10;
