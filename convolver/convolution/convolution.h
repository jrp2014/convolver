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
#include "convolution\holder.h"
#include "convolution\sample.h"
#include "convolution\samplebuffer.h"
#include "convolution\channelpaths.h"
#include "convolution\waveformat.h"
#include "convolution\lrint.h"
#include "convolution\ffthelp.h"

// For random number seed
#include <time.h>

// The following routines are external to pick up multiplication routines that have been vectorized using Intel C99
#ifndef FFTW
#if (defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))
// Intel compiler, for vectorization
extern "C" void cmul(float *, float *, float *, int);
extern "C" void cmuladd(float *, float *, float *, int);
#endif
#endif

// Convolution does the work

template <typename T>
class Convolution
{
public:
	Convolution(const TCHAR szConfigFileName[MAX_PATH], const DWORD& nPartitions, const unsigned int& nPlanningRigour);
	//	virtual ~Convolution(void) {};

	// This version of the convolution routine does partitioned convolution
	// Returns number of bytes processed  (== number of output bytes, too)
	DWORD doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		const ConvertSample<T>* input_sample_convertor,		// The functionoid for converting between BYTE* and T
		const ConvertSample<T>* output_sample_convertor,		// The functionoid for converting between T and BYTE*
		DWORD dwBlocksToProcess,						// A block contains a sample for each channel
		const T fAttenuation_db);						// Returns bytes generated

	// This version does straight overlap-save convolution
	// Returns number of bytes processed  (== number of output bytes, too)
	DWORD doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
		const ConvertSample<T>* input_sample_convertor,		// The functionoid for converting between BYTE* and T
		const ConvertSample<T>* output_sample_convertor,		// The functionoid for converting between T and BYTE*
		DWORD dwBlocksToProcess,
		const T fAttenuation_db);						// Returns bytes generated

	void Flush();								// zero buffers, reset pointers

	const ChannelPaths		Mixer;				// Order dependent

	HRESULT calculateOptimumAttenuation(T& fAttenuation, const bool overlapsave = false);

	int cbLookAhead(const ConvertSample<T>* sample_convertor) const
	{
		if(sample_convertor != NULL)
		{
			return Mixer.nPartitionLength() * sample_convertor->nContainerSize(); // The lag
		}
		else
		{
			throw convolutionException("Internal Error: LookAhead incalculable");
		}
	}

	// Calculate the size in bytes of the output buffer corresponding to a given input buffer size
	unsigned int cbOutputBuffer(const int cbInputBuffer, 
		const ConvertSample<T>* InputSampleConvertor, const ConvertSample<T>* OutputSampleConvertor) const
	{
		// Size of each input frame / block
		const unsigned int cbInputFrame = InputSampleConvertor->nContainerSize() * Mixer.nInputChannels();
		// Check that the input buffer size is a whole number of blocks / frames
		assert(cbInputBuffer % cbInputFrame == 0);

		const unsigned int nInputFrames = cbInputBuffer / cbInputFrame; // No of frames/blocks in each input buffer

		// Size of each output frame
		const unsigned int cbOutputFrame = OutputSampleConvertor->nContainerSize() * Mixer.nOutputChannels();

		return cbOutputFrame * nInputFrames;
	}

private:
	SampleBuffer		InputBuffer_;				// Circular buffer holding the current and previous half partition's
													// worth of samples
	ChannelBuffer		InputBufferAccumulator_;
	ChannelBuffer		OutputBuffer_;				// The output for a particular path, before mixing
	SampleBuffer		OutputBufferAccumulator_;	// For collecting path outputs
	PartitionedBuffer	ComputationCircularBuffer_;	// Used as the output buffer for partitioned convolution

	const DWORD			nPartitions_;
	DWORD				nInputBufferIndex_;			// placeholder
	DWORD				nPartitionIndex_;			// for partitioned convolution
	DWORD				nPreviousPartitionIndex_;	// lags nPartitionIndex_ by 1
	bool				bStartWriting_;

	//void mix_input(const ChannelPaths::ChannelPath& restrict thisPath);
	void mix_input(const ChannelPaths::ChannelPath& restrict thisPath, 
							   const SampleBuffer& restrict InputBuffer,
							   ChannelBuffer& restrict InputBufferAccumulator);
	void mix_output(const ChannelPaths::ChannelPath& restrict thisPath, SampleBuffer& restrict Accumulator, 
		const ChannelBuffer& restrict Output, const DWORD to);

	// The following need to be distinguished because different FFT routines use different orderings
#ifdef FFTW
	void inline complex_mul(const fftwf_complex* restrict in1, const fftwf_complex* restrict in2,
		fftwf_complex* restrict result, const ChannelBuffer::size_type count);
	void inline complex_mul_add(const fftwf_complex* restrict in1, const fftwf_complex* restrict in2,
		fftwf_complex* restrict result, const ChannelBuffer::size_type count);
#if defined(DEBUG) | defined(_DEBUG)
	T verify_convolution(const ChannelBuffer& X, const ChannelBuffer& H, const ChannelBuffer& Y, 
		const ChannelBuffer::size_type from, const ChannelBuffer::size_type to) const;
#endif
#elif !(defined(__ICC) || defined(__INTEL_COMPILER))
	// non-vectorized complex array multiplication -- ordering specific to the Ooura routines. C = A * B
	void inline cmult(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C,
		const ChannelBuffer::size_type N);
	void inline cmultadd(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C,
		const ChannelBuffer::size_type N);
#endif

	// Used to check filter / partion lengths
	bool isPowerOf2(int i) const
	{
		return i > 0 && (i & (i - 1)) == 0;
	};

	Convolution(); // no implementation
	Convolution(const Convolution& other); // no impl.
	const Convolution& operator=(const Convolution& other); // no impl.
};

template <typename T>
class ConvolutionList
{
public:
	ConvolutionList(const TCHAR szConfigFileName[MAX_PATH], const DWORD& nPartitions,
		const unsigned int& nPlanningRigour);

	virtual ~ConvolutionList() 
	{
#if defined(DEBUG) | defined(_DEBUG)
		DEBUGGING(3, cdebug << "ConvolutionList::~ConvolutionList " << std::endl;);
#endif
		// TODO: check that this is enough (ptr_vector should do the work)
	}

	typedef typename boost::ptr_vector< Convolution<T> >::size_type size_type;
	enum SelectedState {Unselected, InputSelected, OutputSelected, Selected};

	// Accessor functions

	size_type nConvolutionList() const
	{
		assert(nConvolutionList_ == ConvolutionList_.size());
		return nConvolutionList_;
	}

	void selectConvolutionIndex(const size_type& pathsIndex)
	{
		assert(pathsIndex < nConvolutionList_);
		state_ = Selected;
		selectedConvolutionIndex_ = pathsIndex;
	}

	bool ConvolutionSelected() const
	{
		return state_ == Selected;
	}

	Convolution<T>& SelectedConvolution()
	{
		if(state_ != Selected)
			throw convolutionListException("Internal error: no filter paths selected", TEXT(""));
		return ConvolutionList_[selectedConvolutionIndex_];
	}

	const Convolution<T>& operator[](size_type n) const
	{
		assert(n >= 0 && n < nConvolutionList());
		return ConvolutionList_[n];
	}

	Convolution<T>& Conv(size_type n)
	{
		assert(n >= 0 && n < nConvolutionList());
		return ConvolutionList_[n];
	}

	HRESULT CheckConvolutionList(const WAVEFORMATEX* pWaveIn, const WAVEFORMATEX* pWaveOut, 
		bool select=false);

	HRESULT SelectConvolution(const WAVEFORMATEX* pWaveIn, const WAVEFORMATEX* pWaveOut);

	void ClearInputSelection()
	{
		if(state_ == InputSelected)
		{
			state_ = Unselected;
		}
		else if (state_ == Selected)
		{
			state_ = OutputSelected;
		}
	}

	void ClearOutputSelection()
	{
		if(state_ == OutputSelected)
		{
			state_ = Unselected;
		}
		else if (state_ == Selected)
		{
			state_ = InputSelected;
		}
	}

	void Flush()
	{
		if(ConvolutionSelected())
		{
			SelectedConvolution().Flush();
		}
	}

	const std::string DisplayConvolutionList() const;


	bool	bNeedsUpdating;				// Whether the current convolutionlist is valid


#if defined(DEBUG) | defined(_DEBUG)
	void Dump() const;
#endif

private:
	configFile	config_;


	SelectedState state_;
	size_type selectedConvolutionIndex_;
	boost::ptr_vector< Convolution<T> > ConvolutionList_;
	size_type	nConvolutionList_;
	DWORD	nPartitions_;

	ConvolutionList();											// No default ctor
	ConvolutionList(const ConvolutionList&);					// No copy ctor
	const ConvolutionList &operator =(const ConvolutionList&);	// No copy assignment
};
