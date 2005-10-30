// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// Filter it with the input stream.
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

#include  "convolution\convolution.h"

template class CConvolution<float>;
template HRESULT SelectSampleConvertor<float>(WAVEFORMATEX* & pWave, Holder< Sample<float> >& sample_convertor);
template HRESULT calculateOptimumAttenuation<float>(float& fAttenuation, TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions);


// CConvolution Constructor
template <typename T>
CConvolution<T>::CConvolution(TCHAR szConfigFileName[MAX_PATH], const int& nPartitions):
Mixer(szConfigFileName, nPartitions),
#ifdef ARRAY
InputBuffer_(Mixer.nInputChannels, Mixer.nPartitionLength),
InputBufferAccumulator_(Mixer.nPartitionLength),
OutputBuffer_(Mixer.nPartitionLength),
OutputBufferAccumulator_(Mixer.nOutputChannels, Mixer.nPartitionLength),
#else
InputBuffer_(Mixer.nInputChannels, ChannelBuffer(Mixer.nPartitionLength)),
InputBufferAccumulator_(Mixer.nPartitionLength),
OutputBuffer_(Mixer.nPartitionLength), // NB. Actually, only need half partition length for DoPartitionedConvolution
OutputBufferAccumulator_(Mixer.nOutputChannels, ChannelBuffer(Mixer.nPartitionLength)),
#endif
#ifdef FFTW
#ifdef ARRAY
ComputationCircularBuffer_(nPartitions, Mixer.nPaths, Mixer.nFFTWPartitionLength),
FFTInputBufferAccumulator_(Mixer.nFFTWPartitionLength),
FFTOutputBuffer_(Mixer.nFFTWPartitionLength),
#else
ComputationCircularBuffer_(nPartitions, SampleBuffer(Mixer.nPaths, ChannelBuffer(Mixer.nFFTWPartitionLength))),
FFTInputBufferAccumulator_(ChannelBuffer(Mixer.nFFTWPartitionLength)),
FFTOutputBuffer_(ChannelBuffer(Mixer.nFFTWPartitionLength)),
#endif
#endif
nInputBufferIndex_(Mixer.nHalfPartitionLength),
nPartitionIndex_(0),
nPreviousPartitionIndex_(Mixer.nPartitions-1),
bStartWritingOutput_(false)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "CConvolution" << std::endl;)
#endif

		// This should not be necessary, as ChannelBuffer should be zero'd on construction.  But it is not for valarray
		Flush();
}

// Reset various buffers and pointers
template <typename T>
void CConvolution<T>::Flush()
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "CConvolution<T>::Flush" << std::endl;)
#endif
		for(int nChannel = 0; nChannel < Mixer.nInputChannels; ++nChannel)
		{
			InputBuffer_[nChannel] = 0;
		}

		for(int nPath=0; nPath < Mixer.nPaths; ++ nPath)
		{
			for (int nPartition = 0; nPartition < Mixer.nPartitions; ++nPartition)
				ComputationCircularBuffer_[nPartition][nPath] = 0;
		}

		nInputBufferIndex_ = Mixer.nHalfPartitionLength;// placeholder
		nPartitionIndex_ = 0;							// for partitioned convolution
		nPreviousPartitionIndex_ = Mixer.nPartitions-1;	// lags nPartitionIndex_ by 1
		bStartWritingOutput_ = false;					// don't start outputting until we have some convolved output
}


// This version of the convolution routine does partitioned convolution
template <typename T>
DWORD
CConvolution<T>::doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
										  const Holder< Sample<T> >& input_sample_convertor,	// The functionoid for converting between BYTE* and T
										  const Holder< Sample<T> >& output_sample_convertor,	// The functionoid for converting between T and BYTE*
										  DWORD dwBlocksToProcess,		// A block contains a sample for each channel
										  const T fAttenuation_db,
										  const T fWetMix,
										  const T fDryMix)  // Returns bytes processed
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "CConvolution<T>::doPartitionedConvolution" << std::endl;)
#endif
#ifndef FFTW
		// FFTW takes arbitrararily-sized args
		assert(isPowerOf2(Mixer.nPartitionLength));
#endif

	int cbInputBytesProcessed = 0;
	int cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	const float fAttenuationFactor = powf(10, fAttenuation_db / 20.0f);

	while (dwBlocksToProcess--)	// Frames to process
	{
		// Channels are stored sequentially in a frame (ie, they are interleaved on the channel)


		// Get the next frame from pbInputBuffer_ to InputBuffer_, copying the previous frame from OutputBuffer_ to pbOutputData
		// Output lags input by half a partition length

		if(bStartWritingOutput_)
		{
#pragma loop count(6)
#pragma ivdep
			for (int nChannel = 0; nChannel<Mixer.nOutputChannels; ++nChannel)
			{
				// Mix the processed signal with the dry signal
				// TODO: remove support for wet/drymix
				output_sample_convertor->PutSample(pbOutputDataPointer,
					InputBuffer_[nChannel][nInputBufferIndex_] * fDryMix + OutputBufferAccumulator_[nChannel][nInputBufferIndex_] * fWetMix,
					cbOutputBytesGenerated);
			}
		}

#pragma loop count(6)
#pragma ivdep
		for (int nChannel = 0; nChannel<Mixer.nInputChannels; ++nChannel)
		{
			// Get the next input sample and convert it to a float, and scale it
			input_sample_convertor->GetSample(InputBuffer_[nChannel][nInputBufferIndex_],
				pbInputDataPointer, fAttenuationFactor, cbInputBytesProcessed);
		} // nChannel

		// Got a frame
		if (nInputBufferIndex_ == Mixer.nPartitionLength - 1) // Got half a partition-length's worth of frames
		{	
			// Apply the filter paths

#pragma loop count(6)
#pragma ivdep
			for(int nChannel=0; nChannel<Mixer.nOutputChannels; ++nChannel)
			{
				OutputBufferAccumulator_[nChannel] = 0;
			}

#pragma loop count(6)
			for (int nPath = 0; nPath < Mixer.nPaths; ++nPath)
			{

				// Zero the partition from circular coeffs that we have just used, for the next cycle
				ComputationCircularBuffer_[nPreviousPartitionIndex_][nPath] = 0;

				// Mix the input samples for this filter path
				mix_input(Mixer.Paths[nPath]);
				//				InputBufferAccumulator_ = 0;
				//#pragma loop count(6)
				//				for(int nChannel = 0; nChannel < Mixer.Paths[nPath].inChannel.size(); ++nChannel)
				//				{
				//#pragma loop count(65536)
				//					for(int i=0; i<Mixer.nPartitionLength; ++i)
				//					{
				//						InputBufferAccumulator_[i] += InputBuffer_[Mixer.Paths[nPath].inChannel[nChannel].nChannel][i] *
				//							Mixer.Paths[nPath].inChannel[nChannel].fScale;
				//					}
				//				}

				// get DFT of InputBufferAccumulator_
#ifdef FFTW
				fftwf_execute_dft_r2c(Mixer.Paths[0].filter.plan, &InputBufferAccumulator_[0],
					reinterpret_cast<fftwf_complex*>(&FFTInputBufferAccumulator_[0]));
#elif defined(SIMPLE_OOURA)
				rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0]);
#elif defined(OOURA)
				// TODO: rationalize the ip, w references
				rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0], 
					&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif

#pragma loop count(4)
				for (int nPartitionIndex = 0; nPartitionIndex < Mixer.nPartitions; ++nPartitionIndex)
				{
					// Complex vector multiplication of InputBufferAccumulator_ and Mixer.Paths[nPath].filter,
					// added to ComputationCircularBuffer_
#ifdef FFTW
					complex_mul_add(reinterpret_cast<fftwf_complex*>(&FFTInputBufferAccumulator_[0]),
						reinterpret_cast<fftwf_complex*>(&Mixer.Paths[nPath].filter.fft_coeffs[nPartitionIndex][0][0]),
						reinterpret_cast<fftwf_complex*>(&ComputationCircularBuffer_[nPartitionIndex_][nPath][0]),
						Mixer.nPartitionLength);
#elif defined(__ICC) || defined(__INTEL_COMPILER)
					// Vectorizable
					cmuladd(&InputBufferAccumulator_[0], &Mixer.Paths[nPath].filter.coeffs[nPartitionIndex][0][0],
						&ComputationCircularBuffer_[nPartitionIndex_][nPath][0], Mixer.nPartitionLength);
#else
					// Non-vectorizable
					cmultadd(InputBufferAccumulator_, Mixer.Paths[nPath].filter.coeffs[nPartitionIndex][0], //TODO: 0=assume only one channel for filter
						ComputationCircularBuffer_[nPartitionIndex_][nPath], Mixer.nPartitionLength);
#endif
					nPartitionIndex_ = (nPartitionIndex_ + 1) % Mixer.nPartitions;	// circular
				} // nPartitionIndex

				//get back the yi: take the Inverse DFT. Not necessary to scale here, as did so when reading filter
#ifdef FFTW
				fftwf_execute_dft_c2r(Mixer.Paths[0].filter.reverse_plan,
					reinterpret_cast<fftwf_complex*>(&ComputationCircularBuffer_[nPartitionIndex_][nPath][0]),
					&OutputBuffer_[0]);
#elif defined(SIMPLE_OOURA)
				rdft(Mixer.nPartitionLength, OouraRBackward, &ComputationCircularBuffer_[nPartitionIndex_][nPath][0]);
#elif defined(OOURA)
				rdft(Mixer.nPartitionLength, OouraRBackward, &ComputationCircularBuffer_[nPartitionIndex_][nPath][0],
					&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif

				// Mix the outputs
				mix_output(Mixer.Paths[nPath], OutputBufferAccumulator_, 
#ifdef FFTW
					OutputBuffer_,
#else
					ComputationCircularBuffer_[nPartitionIndex_][nPath],
#endif
					Mixer.nHalfPartitionLength, Mixer.nPartitionLength);

				//#pragma loop count(6)
				//				for(int nChannel=0; nChannel<Mixer.Paths[nPath].outChannel.size(); ++nChannel)
				//				{
				//#pragma loop count(65536)
				//					for(int i=0; i < Mixer.nHalfPartitionLength; ++i)
				//					{
				//						OutputBufferAccumulator_[Mixer.Paths[nPath].outChannel[nChannel].nChannel][i] += Mixer.Paths[nPath].outChannel[nChannel].fScale *
				//#ifdef FFTW
				//							OutputBuffer_[i];
				//#else
				//							ComputationCircularBuffer_[nPartitionIndex_][nPath][i] ;
				//#endif
				//					} // i
				//				} // nChannel
			} // nPath

			// Save the previous half partition; the first half will be read in over the next cycle
			// TODO: figure out how to use the circular input buffer method used in doConvolution:
			// it does not work straightforwardly, as the ComputationCircularBuffer can't accumulate sums
			// containing both Next+Prev and Prev+Next input sample blocks
#pragma loop count(6)
#pragma ivdep
			for(int nChannel = 0; nChannel < Mixer.nInputChannels; ++nChannel)
			{
				InputBuffer_[nChannel].shiftleft(Mixer.nHalfPartitionLength);
			}
			nInputBufferIndex_ = Mixer.nHalfPartitionLength;

			// Save the partition to be used for output
			nPreviousPartitionIndex_ = nPartitionIndex_;
			nPartitionIndex_ = (nPartitionIndex_ + 1) % Mixer.nPartitions;

			bStartWritingOutput_ = true;
		}
		else
			++ nInputBufferIndex_ ;		// get next frame

	} // while

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "cbInputBytesProcessed: " << cbInputBytesProcessed  << ", cbOutputBytesGenerated: " << cbOutputBytesGenerated << std::endl;
#endif

	return cbOutputBytesGenerated;
}


// This version of the convolution routine is just plain overlap-save.
template <typename T>
DWORD
CConvolution<T>::doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
							   const Holder< Sample<T> >& input_sample_convertor,	// The functionoid for converting between BYTE* and T
							   const Holder< Sample<T> >& output_sample_convertor,	// The functionoid for converting between T and BYTE*
							   DWORD dwBlocksToProcess,		// A block contains a sample for each channel
							   const T fAttenuation_db,
							   const T fWetMix,
							   const T fDryMix)			// Returns bytes processed
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "CConvolution<T>::doConvolution" << std::endl;)
#endif

		// This convolution algorithm assumes that the filter is stored in the first, and only, partition
		if(Mixer.nPartitions != 1)
		{
			return 0;
		}
#ifndef FFTW
		// FFTW does not need powers of 2
		assert(isPowerOf2(Mixer.nPartitionLength));
#endif
		assert(Mixer.nPartitionLength == Mixer.nFilterLength);

		int cbInputBytesProcessed = 0;
		int cbOutputBytesGenerated = 0;

		BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
		BYTE* pbOutputDataPointer = pbOutputData;

		const float fAttenuationFactor = powf(10, fAttenuation_db / 20.0f);

		while (dwBlocksToProcess--)
		{
			// Channels are stored sequentially in a frame (ie, they are interleaved on the channel)

			// Get the next frame from pbInputBuffer_ to InputBuffer_, copying the previous frame from OutputBuffer_ to pbOutputData

			if(bStartWritingOutput_)
			{
#pragma loop count(6)
#pragma ivdep
				for (int nChannel = 0; nChannel<Mixer.nOutputChannels; ++nChannel)
				{
					// Mix the processed signal with the dry signal
					// TODO: remove support for wet/drymix
					output_sample_convertor->PutSample(pbOutputDataPointer,
						InputBuffer_[nChannel][nInputBufferIndex_] * fDryMix + OutputBufferAccumulator_[nChannel][nInputBufferIndex_] * fWetMix,
						cbOutputBytesGenerated);
				}
			}

#pragma loop count(6)
#pragma ivdep
			for (int nChannel = 0; nChannel<Mixer.nInputChannels; ++nChannel)
			{
				// Get the next input sample and convert it to a float, and scale it
				input_sample_convertor->GetSample(InputBuffer_[nChannel][nInputBufferIndex_],
					pbInputDataPointer, fAttenuationFactor, cbInputBytesProcessed);
			} // nChannel

			// Got a frame
			if (nInputBufferIndex_ == Mixer.nPartitionLength - 1) // Got half a partition-length's worth of frames
			{
				// Apply the filters

				// Initialize the OutputBufferAccumulator_
#pragma loop count(6)
#pragma ivdep
				for(int nChannel = 0; nChannel < Mixer.nOutputChannels; ++nChannel)
				{
					OutputBufferAccumulator_[nChannel] = 0;
				}

#pragma loop count(6)
				for (int nPath = 0; nPath < Mixer.nPaths; ++nPath)
				{
					// Mix the input samples for this filter path into InputBufferAccumulator_
					mix_input(Mixer.Paths[nPath]);
					//				InputBufferAccumulator_ = 0;
					//				for(int nChannel = 0; nChannel < Mixer.Paths[nPath].inChannel.size(); ++nChannel)
					//				{
					//#pragma loop count(65536)
					//					for(int i=0; i<Mixer.nPartitionLength; ++i)
					//					{
					//						InputBufferAccumulator_[i] += InputBuffer_[Mixer.Paths[nPath].inChannel[nChannel].nChannel][i] *
					//							Mixer.Paths[nPath].inChannel[nChannel].fScale;
					//					}
					//				}

					// get DFT of InputBufferAccumulator_
#ifdef FFTW
					fftwf_execute_dft_r2c(Mixer.Paths[nPath].filter.plan, &InputBufferAccumulator_[0],
						reinterpret_cast<fftwf_complex*>(&FFTInputBufferAccumulator_[0]));
#elif defined(SIMPLE_OOURA)
					rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0]);
#elif defined(OOURA)
					// TODO: rationalize the ip, w references
					rdft(Mixer.nPartitionLength, OouraRForward, &InputBufferAccumulator_[0], 
						&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif

#ifdef FFTW
					complex_mul(reinterpret_cast<fftwf_complex*>(&FFTInputBufferAccumulator_[0]),
						reinterpret_cast<fftwf_complex*>(&Mixer.Paths[nPath].filter.fft_coeffs[0][0][0]),
						reinterpret_cast<fftwf_complex*>(&FFTOutputBuffer_[0]),
						Mixer.nPartitionLength);
#elif defined(__ICC) || defined(__INTEL_COMPILER)
					// vectorized
					cmul(&InputBufferAccumulator_[0], 
						&Mixer.Paths[nPath].filter.coeffs[0][0][0],					// use the first partition and channel only
						&FFTOutputBuffer_[0], Mixer.nPartitionLength);	
#else
					// Non-vectorizable
					cmult(InputBufferAccumulator_,
						Mixer.Paths[nPath].filter.coeffs[0][0],					// use the first partition and channel only
						OutputBuffer_, Mixer.nPartitionLength);	
#endif
					//get back the yi: take the Inverse DFT. Not necessary to scale here, as did so when reading filter
#ifdef FFTW
					fftwf_execute_dft_c2r(Mixer.Paths[nPath].filter.reverse_plan,
						reinterpret_cast<fftwf_complex*>(&FFTOutputBuffer_[0]),
						&OutputBuffer_[0]);
#elif defined(SIMPLE_OOURA)
					rdft(Mixer.nPartitionLength, OouraRBackward, &OutputBuffer_[0]);
#elif defined(OOURA)
					rdft(Mixer.nPartitionLength, OouraRBackward, &OutputBuffer_[0],
						&Mixer.Paths[0].filter.ip[0], &Mixer.Paths[0].filter.w[0]);
#else
#error "No FFT package defined"
#endif

#if defined(DEBUG) || defined(_DEBUG)
#ifdef FFTW
					DEBUGGING(4, 
						verify_convolution(InputBufferAccumulator_,
						Mixer.Paths[nPath].filter.coeffs[0][0], OutputBuffer_,
						Mixer.nHalfPartitionLength, Mixer.nPartitionLength););
#endif
#endif
					// Mix the outputs (only use the last half, as the rest is junk)
					mix_output(Mixer.Paths[nPath], OutputBufferAccumulator_, OutputBuffer_,
						Mixer.nHalfPartitionLength, Mixer.nPartitionLength);

					//#pragma loop count(6)
					//				for(int nChannel=0; nChannel<Mixer.Paths[nPath].outChannel.size(); ++nChannel)
					//				{
					//#pragma loop count(65536)
					//					for(int i=0; i<Mixer.nHalfPartitionLength; ++i)
					//					{
					//						OutputBufferAccumulator_[Mixer.Paths[nPath].outChannel[nChannel].nChannel][i] += 
					//							OutputBuffer_[i] * Mixer.Paths[nPath].outChannel[nChannel].fScale;
					//					}
					//				}
				} // nPath

				// Save the previous half partition; the first half will be read in over the next cycle
				// TODO: avoid this copy by rotating the filter when it is read in?
#pragma loop count(6)
#pragma ivdep
				for(int nChannel = 0; nChannel < Mixer.nInputChannels; ++nChannel)
				{
					InputBuffer_[nChannel].shiftleft(Mixer.nHalfPartitionLength);
				}
				nInputBufferIndex_ = Mixer.nHalfPartitionLength;

				bStartWritingOutput_ = true;
			}
			else
				++nInputBufferIndex_;

		} // while

#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "cbInputBytesProcessed: " << cbInputBytesProcessed  << ", cbOutputBytesGenerated: " << cbOutputBytesGenerated << std::endl;
#endif

		return cbOutputBytesGenerated;
}


// TODO: optimize as the previous half-partition is in the accumulator
template <typename T>
void inline CConvolution<T>::mix_input(const ChannelPaths::ChannelPath& thisPath)
{
	InputBufferAccumulator_ = 0;
	const int nChannels = thisPath.inChannel.size();
#pragma loop count(6)
#pragma ivdep
	for(int nChannel = 0; nChannel < nChannels; ++nChannel)
	{
		const float fScale = thisPath.inChannel[nChannel].fScale;
		const float thisChannel = thisPath.inChannel[nChannel].nChannel;
		const ChannelBuffer& InputSamples = InputBuffer_[thisChannel];
#pragma loop count(65536)
#pragma ivdep
		// Need to accumulate the whole partition, even though the input buffer contains the previous half-partition
		// because FFTW destroys its inputs.
		for(int i=0; i<Mixer.nPartitionLength; ++i)
		{
			InputBufferAccumulator_[i] += fScale * InputSamples[i];
		}
	}
}

template <typename T>
void inline CConvolution<T>::mix_output(const ChannelPaths::ChannelPath& thisPath, SampleBuffer& Accumulator, const ChannelBuffer& Output,
										const int 	from, const int  to)
{
	const int nChannels = thisPath.outChannel.size();
#pragma loop count(6)
#pragma ivdep
	for(int nChannel=0; nChannel<nChannels; ++nChannel)
	{
		const float fScale = thisPath.outChannel[nChannel].fScale;
		const float thisChannel = thisPath.outChannel[nChannel].nChannel;
#pragma loop count(65536)
#pragma ivdep
		for(int i= from; i < to; ++i)
		{
			Accumulator[thisChannel][i] += fScale * Output[i];
		} // i
	} // nChannel
}

#ifdef FFTW
template <typename T>
void inline CConvolution<T>::complex_mul(fftwf_complex* restrict in1, fftwf_complex* restrict in2, fftwf_complex* restrict result, unsigned int count)
{

	//__declspec(align( 16 )) float T1;
	//__declspec(align( 16 )) float T2;
#pragma ivdep
	for (int index = 0; index < count/2+1; ++index) {

		result[index][0] = in1[index][0] * in2[index][0] - in1[index][1] * in2[index][1];
		result[index][1] = in1[index][0] * in2[index][1] + in1[index][1] * in2[index][0];

		//T1 = in1[index][0] * in2[index][0];
		//T2 = in1[index][1] * in2[index][1];
		//result[index][0] = T1 - T2;
		//result[index][1] = ((in1[index][0] + in1[index][1]) * (in2[index][0] + in2[index][1])) - (T1 + T2);

	}
}

template <typename T>
void inline CConvolution<T>::complex_mul_add(fftwf_complex* restrict in1, fftwf_complex* restrict in2, fftwf_complex* restrict result, unsigned int count)
{

	//__declspec(align( 16 )) float T1;
	//__declspec(align( 16 )) float T2;
#pragma ivdep
	for (int index = 0; index < count/2+1; ++index) {

		result[index][0] += in1[index][0] * in2[index][0] - in1[index][1] * in2[index][1];
		result[index][1] += in1[index][0] * in2[index][1] + in1[index][1] * in2[index][0];

		//T1 = in1[index][0] * in2[index][0];
		//T2 = in1[index][1] * in2[index][1];
		//result[index][0] = T1 - T2;
		//result[index][1] = ((in1[index][0] + in1[index][1]) * (in2[index][0] + in2[index][1])) - (T1 + T2);

	}
}

#elif !(defined(__ICC) || defined(__INTEL_COMPILER))
// Non-vectorizable versions

/* Complex multiplication */
template <typename T>
void inline CConvolution<T>::cmult(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const int N)
{
	//__declspec(align( 16 )) float T1;
	//__declspec(align( 16 )) float T2;

	// a[2*k] = R[k], 0<=k<n/2
	// a[2*k+1] = I[k], 0<k<n/2
	// a[1] = R[n/2]

	// (R[R[a] + I[a]) * (R[b]+I[b]] = R[a]R[b] - I[a]I[b]
	// (I[R[a] + I[a]) * (R[b]+I[b]] = R[a]I[b] + I[a]R[b]

	// 0<k<n/2
	//								  A[R]*B[R]      - A[I]*B[I]
	// R[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k]   - a[2*k+1]b[2*k+1]
	//								  A[R]*B[I]      + A{I]*B[R]
	// I[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k+1] + a[2*k+1]b[2*k]

	// tempt the optimizer
	assert(N % 4 == 0);

	C[0] = A[0] * B[0];
	C[1] = A[1] * B[1];
	for(int R = 2, I = 3; R < N ;R += 2, I += 2)
	{
		C[R] = A[R]*B[R] - A[I]*B[I];
		C[I] = A[R]*B[I] + A[I]*B[R];
		//T1 = A[R] * B[R];
		//T2 = A[I] * B[I];
		//C[I] = ((A[R] + A[I]) * (B[R] + B[I])) - (T1 + T2);
		//C[R] = T1 - T2;
	}
}


/* Complex multiplication with addition */
template <typename T>
void inline CConvolution<T>::cmultadd(const ChannelBuffer& restrict A, const ChannelBuffer& restrict B, ChannelBuffer& restrict C, const int N)
{
	//__declspec(align( 16 )) float T1;
	//__declspec(align( 16 )) float T2;

	// a[2*k] = R[k], 0<=k<n/2
	// a[2*k+1] = I[k], 0<k<n/2
	// a[1] = R[n/2]

	// R[R[a] + I[a]) * (R[b]+I[b]] = R[a]R[b] - I[a]I[b]
	// I[R[a] + I[a]) * (R[b]+I[b]] = R[a]I[b] + I[a]R[b]

	// 0<k<n/2
	//								  A[R]*B[R]      - A[I]*B[I]
	// R[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k]   - a[2*k+1]b[2*k+1]
	//								  A[R]*B[I]      + A{I]*B[R]
	// I[R[a] + I[a]) * (R[b]+I[b]] = a[2*k]b[2*k+1] + a[2*k+1]b[2*k]

	// tempt the optimizer
	assert(N % 4 == 0);

	C[0] += A[0] * B[0];
	C[1] += A[1] * B[1];

	for (int R = 2, I = 3; R < N ;R += 2, I += 2)
	{
		C[R] += A[R]*B[R] - A[I]*B[I];
		C[I] += A[R]*B[I] + A[I]*B[R];
		//T1 = A[R] * B[R];
		//T2 = A[I] * B[I];
		//C[I] += ((A[R] + A[I]) * (B[R] + B[I])) - (T1 + T2);
		//C[R] += T1 - T2;
	}
}

#endif

#if defined(DEBUG) | defined(_DEBUG)
template <typename T>
T CConvolution<T>::verify_convolution(const ChannelBuffer& X, const ChannelBuffer& H, const ChannelBuffer& Y, const int from, const int to) const
{
	ChannelBuffer y(2 * Mixer.nPartitionLength);
	y = 0;
	// Direct calculation, usint input side algorithm
	for (int n = 0; n < Mixer.nPartitionLength; ++n)
	{
		for (int k = 0; k < Mixer.nPartitionLength; ++k)
			y[n+k] += H[k] * X[n];
	}

	// Only compare the valid part of the output buffer
	float diff = 0, diff2 = 0;
	for(int n = from; n < to; ++n)
	{
		diff = abs(Y[n] - y[n]);
		diff2 += diff * diff;
	}

	//if (diff > 0.001 + 0.01 * (abs(Y[n]) + abs(yn)))
	//	cdebug << n << ": " << Y[n] << ", " << yn << std::endl;

	if(diff2 > 1e-7)
	{
		cdebug << "Bad convolution: ";
		cdebug << "nInputBufferIndex_:" << nInputBufferIndex_ << " nPartitionIndex_:" << nPartitionIndex_ << 
			" PartitionLength: " << Mixer.nPartitionLength << " from " << from << " to " << to << std:: endl;

		cdebug << "X "; DumpChannelBuffer(X);  cdebug << std::endl;
		cdebug << "H "; DumpChannelBuffer(H);  cdebug << std::endl;
		cdebug << "Y "; DumpChannelBuffer(Y);  cdebug << std::endl;
		cdebug << "y "; DumpChannelBuffer(y);  cdebug << std::endl << std::endl;
	}
	return sqrt(diff);
}
#endif

// Pick the correct version of the processing class
//
// TODO: build this into the Factory class.  (Probably more trouble than it is worth)
//
// Note that this allows multi-channel and high-resolution WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT.
// Strictly speaking these should be WAVE_FORMAT_EXTENSIBLE, if we want to avoid ambiguity.
// The code below assumes that for WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT, wBitsPerSample is the container size.
// The code below will not work with streams where wBitsPerSample is used to indicate the bits of actual valid data,
// while the container size is to be inferred from nBlockAlign and nChannels (eg, wBitsPerSample = 20, nBlockAlign = 8, nChannels = 2).
// See http://www.microsoft.com/whdc/device/audio/multichaud.mspx
template <typename T>
HRESULT SelectSampleConvertor(WAVEFORMATEX* & pWave, Holder< Sample<T> >& sample_convertor)
{
	HRESULT hr = S_OK;

	if (NULL == pWave)
	{
		return E_POINTER;
	}

	WORD wFormatTag = pWave->wFormatTag;
	WORD wValidBitsPerSample = pWave->wBitsPerSample;
	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		WAVEFORMATEXTENSIBLE* pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;
		wValidBitsPerSample = pWaveXT->Samples.wValidBitsPerSample;
		if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			wFormatTag = WAVE_FORMAT_PCM;
			// TODO: cross-check pWaveXT->Samples.wSamplesPerBlock
		}
		else
			if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
			{
				wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			}
			else
			{
				return E_INVALIDARG;
			}
	}

	sample_convertor = NULL;

	try
	{
		switch (wFormatTag)
		{
		case WAVE_FORMAT_PCM:
			switch (pWave->wBitsPerSample)
			{
				// Note: for 8 and 16-bit samples, we assume the sample is the same size as
				// the samples. For > 16-bit samples, we need to use the WAVEFORMATEXTENSIBLE
				// structure to determine the valid bits per sample. (See above)
			case 8:
				sample_convertor = new Sample_pcm8<T>();
				break;

			case 16:
				sample_convertor = new Sample_pcm16<T>();
				break;

			case 24:
				{
					switch (wValidBitsPerSample)
					{
					case 16:
						sample_convertor = new Sample_pcm24<T, 16>();
						break;

					case 20:
						sample_convertor = new Sample_pcm24<T, 20>();
						break;

					case 24:
						sample_convertor = new Sample_pcm24<T, 24>();
						break;

					default:
						return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
					}
				}
				break;

			case 32:
				{
					switch (wValidBitsPerSample)
					{
					case 16:
						sample_convertor = new Sample_pcm32<T, 16>();
						break;

					case 20:
						sample_convertor = new Sample_pcm32<T, 20>();
						break;

					case 24:
						sample_convertor = new Sample_pcm32<T, 24>();
						break;

					case 32:
						sample_convertor = new Sample_pcm32<T, 32>();
						break;

					default:
						return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
					}
				}
				break;

			default:  // Unprocessable PCM
				return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
			}
			break;

		case WAVE_FORMAT_IEEE_FLOAT:
			switch (pWave->wBitsPerSample)
			{
			case 32:
				sample_convertor = new Sample_ieeefloat<T>();
				break;

			case 64:
				sample_convertor = new Sample_ieeedouble<T>();
				break;

			default:  // Unprocessable IEEE float
				return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
				//break;
			}
			break;

		default: // Not PCM or IEEE Float
			return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
		}

		return hr;
	}
	catch(HRESULT hr)
	{
		return hr;
	}
}

// Convolve the filter with white noise to get the maximum output, from which we calculate the maximum attenuation
template <typename T>
HRESULT calculateOptimumAttenuation(T& fAttenuation, TCHAR szConfigFileName[MAX_PATH], const int& nChannels, const int& nPartitions)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "calculateOptimumAttenuation" << std::endl;)
#endif
		HRESULT hr = S_OK;

	try 
	{
		CConvolution<T> c(szConfigFileName, nPartitions == 0 ? 1 : nPartitions); // 0 used to signal use of overlap-save

		const int nSamples = NSAMPLES; // length of the test sample, in filter lengths
		// So if you are using a 44.1kHz sample rate and it takes 1s to calculate optimum attenuation,
		// that is equivalent to 0.1s to process a filter
		// which means that the filter must be less than 44100 / .1 samples in length to keep up
		const int nBlocks = nSamples * c.Mixer.nFilterLength;
		const int nBufferLength = nBlocks * c.Mixer.nInputChannels;	// Channels are interleaved

		std::vector<T>InputSamples(nBufferLength);
		std::vector<T>OutputSamples(nBufferLength);

		Holder< Sample<T> > convertor(new Sample_ieeefloat<T>());

		srand( (unsigned)time( NULL ) );

		bool again = TRUE;
		float maxSample = 0;

		// TODO: don't keep repeating, as this messes up the rate calculation which does not know how many iterations
		// have been used
		//do
		//{
		for(int i = 0; i < nBufferLength; ++i)
		{
			InputSamples[i] = (2.0f * static_cast<T>(rand()) - static_cast<T>(RAND_MAX)) / static_cast<T>(RAND_MAX); // -1..1
			//InputSamples[i] = 1.0 / (i / 8 + 1.0);  // For testing algorithm
			OutputSamples[i] = 0;  // silence
		}

#if defined(DEBUG) | defined(_DEBUG)
		DEBUGGING(4, 
			cdebug << "InputSamples: ";
		copy(InputSamples.begin(), InputSamples.end(), std::ostream_iterator<float>(cdebug, ", "));
		cdebug << std::endl;);
#endif
		// nPartitions == 0 => use overlap-save version
		DWORD nBytesGenerated = nPartitions == 0 ?
			c.doConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
			convertor, convertor,
			/* dwBlocksToProcess */ nBlocks,
			/* fAttenuation_db */ 0,
			/* fWetMix,*/ 1.0L,
			/* fDryMix */ 0.0L)
			: c.doPartitionedConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
			convertor, convertor,
			/* dwBlocksToProcess */ nBlocks,
			/* fAttenuation_db */ 0,
			/* fWetMix,*/ 1.0L,
			/* fDryMix */ 0.0L);

			// The output will be missing the last half filter length, the first time around
			assert (nBytesGenerated % sizeof(T) == 0);
		assert (nBytesGenerated <= nBufferLength * sizeof(T));

#if defined(DEBUG) | defined(_DEBUG)
		DEBUGGING(4,
			cdebug << "OutputSamples(" << nBytesGenerated/sizeof(T) << ") " ;
		copy(OutputSamples.begin(), OutputSamples.end(), std::ostream_iterator<T>(cdebug, ", "));
		cdebug << std::endl << std::endl;);
#endif

		// Scan the output coeffs for larger output samples
		again = FALSE;
		for(int i = 0; i  < nBytesGenerated / sizeof(T); ++i)
		{
			if (abs(OutputSamples[i]) > maxSample)
			{
				maxSample = abs(OutputSamples[i]);
				again = TRUE; // Keep convolving until find no larger output samples
			}
		}
		//} while (again);

		// maxSample * 10 ^ (fAttenuation_db / 20) = 1
		// Limit fAttenuation to +/-MAX_ATTENUATION dB
		fAttenuation = -20.0f * log10(maxSample);

		if (fAttenuation > MAX_ATTENUATION)
		{
			fAttenuation = MAX_ATTENUATION;
		}
		else if (-fAttenuation > MAX_ATTENUATION)
		{
			fAttenuation = -1.0L * MAX_ATTENUATION;
		}

		return hr;
	}
	catch (HRESULT& hr) // from CConvolution
	{
#if defined(DEBUG) | defined(_DEBUG)	
		cdebug << "Failed to calculate optimum attenuation (" << std::hex << hr	<< std::dec << ")" << std::endl;
#endif
		return hr;
	}
#if defined(DEBUG) | defined(_DEBUG)
	catch(const std::exception& error)
	{
		cdebug << "Standard exception: " << error.what() << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const char* what)
	{
		cdebug << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
	catch (const TCHAR* what)
	{
		cdebug << "Failed: " << what << std::endl;
		return E_OUTOFMEMORY;
	}
#endif
	catch (...)
	{
#if defined(DEBUG) | defined(_DEBUG)	
		cdebug << "Failed to calculate optimum attenuation.  Unknown exception." << std::endl;
#endif
		return E_OUTOFMEMORY;
	}
}
