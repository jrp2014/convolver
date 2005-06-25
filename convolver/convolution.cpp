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
// Note that Filter.nSamples is the padded filter size
template <typename FFT_type>
CConvolution<FFT_type>::CConvolution(const DWORD nSampleSize, const CSampleBuffer<FFT_type>* Filter) :
m_nSampleSize(nSampleSize), 
m_nChannels(Filter->nChannels), 
m_n2xFilterLength(Filter->nSamples), 
m_nFilterLength(Filter->nSamples/2), 
m_nInputBufferIndex(0), 
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
	delete m_InputBuffer;
	delete m_OutputBuffer;
	delete m_InputBufferChannelCopy;
}

template <typename FFT_type>
DWORD
CConvolution<typename FFT_type>::doConvolution(const BYTE* pbInputData, BYTE* pbOutputData,
											   DWORD dwBlocksToProcess,
											   const double fAttenuation_db,
											   const double fWetMix,
											   const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
											   , CWaveFile* CWaveFileTrace
#endif	
											   )  // Returns bytes processed
{
	// Cross-check
	DWORD cbBytesActuallyProcessed = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (unsigned short nChannel = 0; nChannel != m_nChannels; nChannel++)
		{

			// Get the input sample and convert it to a FFT_type (eg, float)
			m_InputBuffer->samples[nChannel][m_nInputBufferIndex] = attenuated_sample(fAttenuation_db, get_sample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += m_nSampleSize;

			// Mix the processed signal with the dry signal (which lags by a filterlength)
			double outputSample = (m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength] * fDryMix ) + 
				(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters
			if (abs(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] - 
				m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength]) >
				abs(m_InputBuffer->samples[nChannel][m_nInputBufferIndex + m_nFilterLength]) * 0.01)
			{
				OutputDebugString(TEXT("\n"));
				TCHAR sFormat[100];
				int k = swprintf(sFormat, TEXT("%i,"), dwBlocksToProcess);
				k += swprintf(sFormat + k, TEXT("%i, "), m_nInputBufferIndex);
				k += swprintf(sFormat + k, TEXT("%.4g, "), m_InputBuffer->samples[nChannel][(m_nInputBufferIndex + m_nFilterLength) % m_n2xFilterLength]);
				k += swprintf(sFormat + k, TEXT("%.4g, "), m_OutputBuffer->samples[nChannel][m_nInputBufferIndex % m_n2xFilterLength]);
				k += swprintf(sFormat + k, TEXT("%.6g"), m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] - 
					m_InputBuffer->samples[m_nInputBufferIndex + m_nFilterLength]);
				OutputDebugString(sFormat);
			}
#endif
#endif
			// Write to output buffer.
			cbBytesActuallyProcessed += normalize_sample(pbOutputDataPointer, outputSample);
#if defined(DEBUG) | defined(_DEBUG)
			DWORD samplesize = normalize_sample(pbOutputDataPointer, outputSample);
			assert(samplesize == m_nSampleSize);
			pbOutputDataPointer += m_nSampleSize;
#else
			pbOutputDataPointer += normalize_sample(pbOutputDataPointer, outputSample);
#endif

#if defined(DEBUG) | defined(_DEBUG)
			// This does not quite work, because AllocateStreamingResources seems to be called (twice!) after IEEE FFT_type playback, which rewrites the file
			// It is not clear why this happens, as it does not occur after PCM playback
			UINT nSizeWrote;
			HRESULT hr = S_OK;
			if (FAILED(hr = CWaveFileTrace->Write(sizeof(FFT_type), (BYTE *)&outputSample, &nSizeWrote)))
			{
				SAFE_DELETE( CWaveFileTrace );
				return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
			}
#endif
		}

		if (m_nInputBufferIndex == m_nFilterLength - 1) // Got a full sample
		{	
			// Got a sample xi length Nh, so calculate m_OutputBuffer
			for (unsigned short nChannel = 0; nChannel != m_nChannels; nChannel++)
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
					m_OutputBuffer->samples[nChannel][nSample] *= static_cast<FFT_type>(2.0 / (double) m_n2xFilterLength);

				// move overlap block x i to previous block x i-1
				// TODO: To avoid this copy (ie, make m_InputBuffer circular), would need to have a m_Filter that was aligned
				for (DWORD nSample = 0; nSample != m_nFilterLength; nSample++)
					m_InputBuffer->samples[nChannel][nSample + m_nFilterLength] = m_InputBuffer->samples[nChannel][nSample]; // TODO: MemCpy
			}
			m_nInputBufferIndex = 0;
		}
		else
			m_nInputBufferIndex++;
	} // while

	return cbBytesActuallyProcessed;
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