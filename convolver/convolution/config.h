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


#define STRSAFE_NO_DEPRECATE	1

// For STLport
#define _STLP_NO_ANACHRONISMS 1
#define _STLP_NEW_PLATFORM_SDK 1
//#define _STLP_USING_PLATFORM_SDK_COMPILER 1
#define _STLP_VERBOSE_AUTO_LINK 1
//#define _STLP_USE_BOOST_SUPPORT 1

#if defined(DEBUG) || defined(_DEBUG)

// For VC++ 2005
#define _HAS_ITERATOR_DEBUGGING 1
#define _SECURE_SCL 1

// For STLport std::
//#define _STLP_DEBUG 1
//#define   _STLP_DEBUG_LEVEL _STLP_STANDARD_DBG_LEVEL
//#define _STLP_DEBUG_UNINITIALIZED 1
//#define _STLP_DEBUG_ALLOC 1
#endif


#define _CRT_SECURE_NO_DEPRECATE	1
#define _SECURE_SCL_DEPRECATE		0

// CRT's memory leak detection
// use command line to define the following
//#define CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <stdlib.h>

#include <assert.h>
#include <wchar.h>
#include <math.h>
#include <windows.h>
#include <mmsystem.h>
#include <limits.h>      
#include <stdio.h>
#include <atlbase.h>
#include <atlcom.h>
#include "convolution\exception.h"

//#include <dshow.h>
////// Direct3D includes
////#include <d3d9.h>
////#include <d3dx9.h>
////#include <dxerr9.h>
////
//// DirectSound includes
#include <mmreg.h>
//#include <dsound.h>
//
//#include <ks.h>
//#include <ksmedia.h>

const DWORD MAXSTRING = 1024;	// length

// Debugging

#if defined(DEBUG) || defined(_DEBUG)

// For VC++ 2005
//#define _HAS_ITERATOR_DEBUGGING 1
#define _SECURE_SCL 1

// For STLport std::
//#define _STLP_DEBUG 1
//#define _STLP_DEBUG_UNINITIALIZED 1
//#define _STLP_DEBUG_ALLOC 1

#undef DIRAC_DELTA_FUNCTIONS

#include "debugging\debugging.h"
#include "debugging\debugStream.h"

#endif

#if !defined(__INTEL_COMPILER) || (defined(_MSC_VER) && _MSC_VER < 1400)
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

// Number of filter lengths used to calculate optimum attenuation
// The bigger the number the more accurate the calculation, but the
// longer it takes
const int NSAMPLES = 10;
