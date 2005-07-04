// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// m_Filter it with the input stream.
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

#include  "convolution.h"

// CConvolution Constructor
template <typename FFT_type>
CConvolution<FFT_type>::CConvolution(const DWORD nContainerSize, const CSampleBuffer<FFT_type>* Filter, const DWORD nInputBufferIndex = 0) :
m_nContainerSize(nContainerSize), 
m_nChannels(Filter->nChannels), 
m_n2xFilterLength(Filter->nSamples),	// 2 x Nh, the padded filter size
m_nFilterLength(Filter->nSamples / 2),	// Nh
m_nInputBufferIndex(nInputBufferIndex),
m_Filter(Filter)
{
	assert(m_n2xFilterLength % 2 == 0);

	m_InputBuffer = new CSampleBuffer<FFT_type>(m_nChannels, m_n2xFilterLength);
	m_OutputBuffer = new CSampleBuffer<FFT_type>(m_nChannels, m_n2xFilterLength);
	m_InputBufferChannelCopy = new CSampleBuffer<FFT_type>(1, m_n2xFilterLength);  // Need only one copy channel
};

// CConvolution Destructor
template <typename FFT_type>CConvolution<FFT_type>::~CConvolution(void)
{
	delete m_InputBufferChannelCopy;
	delete m_OutputBuffer;
	delete m_InputBuffer;
}


// This version of the convolution routine works on input data that is not necessarily a whole number of filter lengths long
// It means that the initial filter length of output is just silence, until we have picked up a filter length of input, which
// can then be convolved.  It ought to be possible to arrange to feed the convolution routine to in buffers that are a
// multiple of the filter length long, but the would appear to require more fiddling with IMediaObject::GetInputSizeInfo, or
// allocating our own output buffers, etc.
template <typename FFT_type>
DWORD
CConvolution<typename FFT_type>::doConvolutionWithLag(const BYTE* pbInputData, BYTE* pbOutputData,
													  DWORD dwBlocksToProcess,
													  const double fAttenuation_db,
													  const double fWetMix,
													  const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
													  , CWaveFile* CWaveFileTrace
#endif	
													  )  // Returns bytes processed
{
	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (WORD nChannel = 0; nChannel != m_nChannels; nChannel++)
		{
			// Get the input sample and convert it to a FFT_type (eg, float)
			m_InputBuffer->samples[nChannel][m_nInputBufferIndex] = attenuated_sample(fAttenuation_db, get_sample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += m_nContainerSize;
			cbInputBytesProcessed += m_nContainerSize;

			// Mix the processed signal with the dry signal (which lags by a filterlength)
			// This will be silence, until we have gathered a filter length a convolved it to produce some output. Hence with lag
			double outputSample = (m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength] * fDryMix ) + 
				(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] - m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength]) >
				0.001 + 0.01 * max(abs(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex]), abs(m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< m_nInputBufferIndex << ","
					<< nChannel << ", "
					<< m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength] << ","
					<< m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] << ", "
					<< (m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] - m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength]) << std::endl;
			}
#endif
#endif
			// Write to OutputData
#if defined(DEBUG) | defined(_DEBUG)
			DWORD containerSize = normalize_sample(pbOutputDataPointer, outputSample);
			assert(containerSize == m_nContainerSize);
			cbOutputBytesGenerated += containerSize; // input container size == output container size
#else
			cbOutputBytesGenerated += normalize_sample(pbOutputDataPointer, outputSample);
#endif
			pbOutputDataPointer += m_nContainerSize;

#if defined(DEBUG) | defined(_DEBUG)
			if (CWaveFileTrace)
			{
				// This does not quite work, because AllocateStreamingResources seems to be called after IEEE FFT_type playback, which rewrites the file
				// It is not clear why this happens, as it does not occur after PCM playback
				UINT nSizeWrote;
				HRESULT hr = S_OK;
				if (FAILED(hr = CWaveFileTrace->Write(sizeof(FFT_type), (BYTE *)&outputSample, &nSizeWrote)))
				{
					SAFE_DELETE( CWaveFileTrace );
					return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
				}
			}
#endif
		}

		// Got a block

		if (m_nInputBufferIndex == m_nFilterLength - 1) // Got a filter-length's worth of blocks
		{	
			// Got a sample xi length Nh, so calculate m_OutputBuffer
			for (WORD nChannel = 0; nChannel != m_nChannels; nChannel++)
			{
				// Copy the sample buffer, as the rdft routine overwrites it
				// TODO: replace by a copy assignment operator in the SampleBuffer class
				for (DWORD nSample = 0; nSample != m_n2xFilterLength; nSample++)
					m_InputBufferChannelCopy->samples[0][nSample] = m_InputBuffer->samples[nChannel][nSample];

				//  rdft(n, 1, a):
				//        n              :data length (int)
				//                        n >= 2, n = power of 2
				//        a[0...n-1]     :input data
				//                        output data
				//                                a[2*k] = R[k], 0<=k<n/2
				//                                a[2*k+1] = I[k], 0<k<n/2
				//                                a[1] = R[n/2]

				// Calculate the Xi(m)
				rdft(m_n2xFilterLength, OouraRForward, m_InputBufferChannelCopy->samples[0]);  // get DFT of m_InputBuffer

				// Multiply point by point the complex Yi(m) = Xi(m)Hz(m).  m_Filter (Hz(m)) is already calculated (in AllocateStreamingResources)
				cmult(m_InputBufferChannelCopy->samples[0], m_Filter->samples[nChannel], m_OutputBuffer->samples[nChannel], m_n2xFilterLength);

				//get back the yi
				rdft(m_n2xFilterLength, OouraRBackward, m_OutputBuffer->samples[nChannel]); // take the Inverse DFT

				// scale
				for (DWORD nSample = 0; nSample != m_nFilterLength; nSample++)  // No need to scale second half of output buffer, as going to discard that
					m_OutputBuffer->samples[nChannel][nSample] *= static_cast<FFT_type>(2.0L / (double) m_n2xFilterLength);

				// move overlap block x i to previous block x i-1, hence the overlap-save method
				// TODO: To avoid this copy (ie, make m_InputBuffer circular), would need to have a m_Filter that was aligned
				for (DWORD nSample = 0; nSample != m_nFilterLength; nSample++)
					m_InputBuffer->samples[nChannel][nSample + m_nFilterLength] = m_InputBuffer->samples[nChannel][nSample]; // TODO: MemCpy
			}

			m_nInputBufferIndex = 0;
		}
		else
			m_nInputBufferIndex++;
	} // while

	assert (cbOutputBytesGenerated == cbInputBytesProcessed);
#if defined(DEBUG) | defined(_DEBUG)
	cdebug
		<< "m_nInputBufferIndex " << m_nInputBufferIndex
		<< ", bytes processed: " << cbOutputBytesGenerated << std::endl;
#endif

	return cbOutputBytesGenerated;
};

// This version of the convolution routine works on input data that is a whole number of filter lengths long
// It means that the output does not need to lag
template <typename FFT_type>
DWORD
CConvolution<typename FFT_type>::doConvolutionInPlace(const BYTE* pbInputData, BYTE* pbOutputData,
													  DWORD dwBlocksToProcess,
													  const double fAttenuation_db,
													  const double fWetMix,
													  const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
													  , CWaveFile* CWaveFileTrace
#endif	
													  )  // Returns bytes processed
{
	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	assert(dwBlocksToProcess % m_nFilterLength == 0);  // Only support a whole number of filter lengths

	if (dwBlocksToProcess % m_nFilterLength != 0)
		return 0; // ie, no bytes processed.

	assert(m_nInputBufferIndex == 0); // Ie, that the last input buffer was completely processed

	while (dwBlocksToProcess--)
	{
		for (WORD nChannel = 0; nChannel != m_nChannels; nChannel++)
		{
			// Get the input sample and convert it to a FFT_type (eg, float)
			m_InputBuffer->samples[nChannel][m_nInputBufferIndex] = attenuated_sample(fAttenuation_db, get_sample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += m_nContainerSize;
			cbInputBytesProcessed += m_nContainerSize;
		}
		// Got a block

		if (m_nInputBufferIndex == m_nFilterLength - 1) // Got a filter-length's worth
		{	
			// Got a sample xi length Nh, so calculate m_OutputBuffer
			for (WORD nChannel = 0; nChannel != m_nChannels; nChannel++)
			{
				// Copy the sample buffer, as the rdft routine overwrites it
				// TODO: replace by a copy assignment operator in the SampleBuffer class
				for (DWORD nSample = 0; nSample != m_n2xFilterLength; nSample++)
					m_InputBufferChannelCopy->samples[0][nSample] = m_InputBuffer->samples[nChannel][nSample];

				//  rdft(n, 1, a):
				//        n              :data length (int)
				//                        n >= 2, n = power of 2
				//        a[0...n-1]     :input data
				//                        output data
				//                                a[2*k] = R[k], 0<=k<n/2
				//                                a[2*k+1] = I[k], 0<k<n/2
				//                                a[1] = R[n/2]

				// Calculate the Xi(m)
				rdft(m_n2xFilterLength, OouraRForward, m_InputBufferChannelCopy->samples[0]);  // get DFT of m_InputBuffer

				// Multiply point by point the complex Yi(m) = Xi(m)Hz(m).  m_Filter (Hz(m)) is already calculated (in AllocateStreamingResources)
				cmult(m_InputBufferChannelCopy->samples[0], m_Filter->samples[nChannel], m_OutputBuffer->samples[nChannel], m_n2xFilterLength);

				//get back the yi
				rdft(m_n2xFilterLength, OouraRBackward, m_OutputBuffer->samples[nChannel]); // take the Inverse DFT

				// scale
				for (DWORD nSample = 0; nSample != m_nFilterLength; nSample++)  // No need to scale second half of output buffer, as going to discard that
					m_OutputBuffer->samples[nChannel][nSample] *= static_cast<FFT_type>(2.0L / (double) m_n2xFilterLength);

				// move overlap block x i to previous block x i-1, hence the overlap-save method
				// TODO: To avoid this copy (ie, make m_InputBuffer circular), would need to have a m_Filter that was aligned
				for (DWORD nSample = 0; nSample != m_nFilterLength; nSample++)
					m_InputBuffer->samples[nChannel][nSample + m_nFilterLength] = m_InputBuffer->samples[nChannel][nSample]; // TODO: MemCpy
			}

			// Now copy the convolved values (in FFT_type format) from OutputBuffer to OutputData (in byte stream PCM, IEEE, etc, format)

			for (DWORD nSample = 0; nSample != m_nFilterLength; nSample++)
			{
				for (WORD nChannel = 0; nChannel != m_nChannels; nChannel++) // Channels are interleaved
				{
					// Mix the processed signal with the dry signal (which lags by a filterlength)
					double outputSample = (m_InputBuffer->samples[nChannel][nSample + m_nFilterLength] * fDryMix ) + 
						(m_OutputBuffer->samples[nChannel][nSample] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
					// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
					// TODO: not a good test, if one of the values is 0
					if (abs(m_OutputBuffer->samples[nChannel][nSample] - m_InputBuffer->samples[nChannel][nSample + m_nFilterLength]) >
						0.001 + 0.01 * max(abs(m_OutputBuffer->samples[nChannel][nSample]), abs(m_InputBuffer->samples[nChannel][nSample + m_nFilterLength])))
					{
						cdebug 
							<< dwBlocksToProcess << ","
							<< nSample << ","
							<< nChannel << ", "
							<< m_InputBuffer->samples[nChannel][nSample + m_nFilterLength] << ","
							<< m_OutputBuffer->samples[nChannel][nSample] << ", "
							<< (m_OutputBuffer->samples[nChannel][nSample] - m_InputBuffer->samples[nChannel][nSample + m_nFilterLength]) << std::endl;
					}
#endif
#endif
					// Write to OutputData
#if defined(DEBUG) | defined(_DEBUG)
					DWORD containerSize = normalize_sample(pbOutputDataPointer, outputSample);
					assert(containerSize == m_nContainerSize);
					cbOutputBytesGenerated += containerSize; // input container size == output container size
#else
					cbOutputBytesGenerated += normalize_sample(pbOutputDataPointer, outputSample);
#endif
					pbOutputDataPointer += m_nContainerSize;

#if defined(DEBUG) | defined(_DEBUG)
					if (CWaveFileTrace)
					{
						// This does not quite work, because AllocateStreamingResources seems to be called after IEEE FFT_type playback, which rewrites the file
						// It is not clear why this happens, as it does not occur after PCM playback
						UINT nSizeWrote;
						HRESULT hr = S_OK;
						if (FAILED(hr = CWaveFileTrace->Write(sizeof(FFT_type), (BYTE *)&outputSample, &nSizeWrote)))
						{
							SAFE_DELETE( CWaveFileTrace );
							return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
						}
					}
#endif
				} // for nChannel
			} // for nSample

			m_nInputBufferIndex = 0;
		}
		else
			m_nInputBufferIndex++;
	} // while

	assert (cbOutputBytesGenerated == cbInputBytesProcessed);
#if defined(DEBUG) | defined(_DEBUG)
	cdebug
		<< "m_nInputBufferIndex " << m_nInputBufferIndex
		<< ", bytes processed: " << cbOutputBytesGenerated << std::endl;
#endif


	return cbOutputBytesGenerated;
};

/* Complex multiplication */
template <typename FFT_type>
void CConvolution<FFT_type>::cmult(const FFT_type * A,const FFT_type * B, FFT_type * C,const int N)
{
	int R;
	int I;
	FFT_type T1;
	FFT_type T2;

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

	C[0] = A[0] * B[0];
	C[1] = A[1] * B[1];
	for (R = 2,I = 3;R < N;R += 2,I += 2)
	{
		//C[R] = A[R]*B[R] - A[I]*B[I];
		//C[I] = A[R]*B[I] + A[I]*B[R];
		T1 = A[R] * B[R];
		T2 = A[I] * B[I];
		C[I] = ((A[R] + A[I]) * (B[R] + B[I])) - (T1 + T2);
		C[R] = T1 - T2;
	} 
}