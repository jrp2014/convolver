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
#include <vector>
#include "convolution\samplebuffer.h"
#include "convolution\wavefile.h"
#include "convolution\waveformat.h"
#include "convolution\ffthelp.h"

class Filter
{
public:
	const DWORD	nPartitions;

	// Accessor functions

	DWORD nSamplesPerSec() const
	{
		return nSamplesPerSec_;
	}

	const SampleBuffer& coeffs() const
	{
		return coeffs_;
	}

#ifdef LIBSNDFILE
	const SF_INFO& sf_FilterFormat() const		// The format of the filter file
	{
		return sf_FilterFormat_;
	}
#else
	const WAVEFORMATEXTENSIBLE&	wfexFilterFormat() const		// The format of the filter file
	{
		return wfexFilterFormat_;
	}
#endif

	DWORD nPartitionLength() const		// in blocks (a block contains the samples for each channel)
	{
		assert(nPartitionLength_ == nHalfPartitionLength_ * 2);
		return nPartitionLength_;
	}

	DWORD nHalfPartitionLength() const	// in blocks
	{
		assert(nPartitionLength_ == nHalfPartitionLength_ * 2);
		return nHalfPartitionLength_;
	}

	DWORD nFilterLength() const
	{
		assert(nFilterLength_ == nPartitions * nPartitionLength());
		return nFilterLength_;
	}

#if defined(FFTW)
	DWORD nFFTWPartitionLength() const
	{
		assert(nFFTWPartitionLength_ == 2*(nPartitionLength()/2+1));
		return nFFTWPartitionLength_;
	}

	const fftwf_plan& plan() const
	{
		return plan_;
	}

	const fftwf_plan& reverse_plan() const
	{
		return reverse_plan_;
	}

#elif defined(OOURA)
	// Workspace for the non-simple Ooura routines.  
	const std::vector<DLReal>& w()						// w[0...n/2-1]   :cos/sin table
	{
		return w_;
	}
	const std::vector<int> ip()					// work area for bit reversal
	{
		return ip_;
	}
	// length of ip >= 2+sqrt(n)
	// strictly, length of ip >= 
	//    2+(1<<(int)(log(n+0.5)/log(2))/2).
#endif

	// Constructor
	Filter(const TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions, const DWORD nFilterChannel, const DWORD nSamplesPerSec,
			   const unsigned int nPlanningRigour);

	virtual ~Filter()
	{
#if defined(DEBUG) | defined(_DEBUG)
		DEBUGGING(3, cdebug << "Filter::~Filter " << std::endl;);
#endif
#ifdef FFTW
		fftwf_destroy_plan(plan_);
		fftwf_destroy_plan(reverse_plan_);
#endif
	}

private:

	DWORD					nSamplesPerSec_;		// 44100, 48000, etc
	SampleBuffer			coeffs_;
#ifdef LIBSNDFILE
	SF_INFO					sf_FilterFormat_;		// The format of the filter file
#else
	WAVEFORMATEXTENSIBLE	wfexFilterFormat_;		// The format of the filter file
#endif
	DWORD					nPartitionLength_;		// in blocks (a block contains the samples for each channel)
	DWORD					nHalfPartitionLength_;	// in blocks
	DWORD					nFilterLength_;			// nFilterLength = nPartitions * nPartitionLength
#if defined(FFTW)
	DWORD					nFFTWPartitionLength_;	// 2*(nPaddedPartitionLength/2+1);
	fftwf_plan				plan_;
	fftwf_plan				reverse_plan_;
#elif defined(OOURA)
	// Workspace for the non-simple Ooura routines.  
	std::vector<DLReal>		w_;						// w[0...n/2-1]   :cos/sin table
	std::vector<int>		ip_;					// work area for bit reversal
	// length of ip >= 2+sqrt(n)
	// strictly, length of ip >= 
	//    2+(1<<(int)(log(n+0.5)/log(2))/2).
#endif

	// Disable default copy construction and assignment, as it FFTW plans cannot be copied
	Filter();									// prevent construction
	Filter(const Filter&);						// prevent copying
	const Filter& operator =(const Filter&);	// prevent copying
};
