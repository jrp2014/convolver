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

#include  "convolution.h"

// CConvolution Constructor
template <typename FFT_type>CConvolution<FFT_type>::CConvolution(const DWORD n2xSampleSize) : m_n2xSampleSize(n2xSampleSize), m_nSampleSize(n2xSampleSize/2), m_nInputBufferIndex(0)
{
	assert(m_n2xSampleSize % 2 == 0);
	m_SampleBufferChannelCopy = new CChannelBuffer<FFT_type>(m_n2xSampleSize);  // Need only one copy channel
};
// CConvolution Destructor
template <typename FFT_type>CConvolution<FFT_type>::~CConvolution(void)
{
	delete m_SampleBufferChannelCopy;
}

template <typename FFT_type>
DWORD
CConvolution<typename FFT_type>::doConvolution(const BYTE* pbInputData, BYTE* pbOutputData,
											   const unsigned short nChannels,
											   const CSampleBuffer<FFT_type>* filter,
											   CSampleBuffer<FFT_type>* inputBuffer,
											   CSampleBuffer<FFT_type>* outputBuffer,
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

	BYTE* pb_inputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pb_outputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (unsigned short nChannel = 0; nChannel != nChannels; nChannel++)
		{

			// Get the input sample and convert it to a FFT_type (eg, float)
			inputBuffer->channels[nChannel].samples[m_nInputBufferIndex] = attenuated_sample(fAttenuation_db, get_sample(pb_inputDataPointer)); // Channels are interleaved
			pb_inputDataPointer += m_nSampleSize;

			// Mix the processed signal with the dry signal.
			double outputSample = (inputBuffer->channels[nChannel].samples[m_nInputBufferIndex + m_nSampleSize] * fDryMix ) + 
				(outputBuffer->channels[nChannel].samples[m_nInputBufferIndex] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters
			if (abs(outputBuffer->channels[nChannel].samples[m_nInputBufferIndex] - 
				inputBuffer->channels[nChannel].samples[m_nInputBufferIndex + m_nSampleSize]) >
				abs(inputBuffer->channels[nChannel].samples[m_nInputBufferIndex + m_nSampleSize]) * 0.01)
			{
				OutputDebugString(TEXT("\n"));
				TCHAR sFormat[100];
				int k = swprintf(sFormat, TEXT("%i,"), dwBlocksToProcess);
				k += swprintf(sFormat + k, TEXT("%i, "), m_nInputBufferIndex);
				k += swprintf(sFormat + k, TEXT("%.4g, "), inputBuffer->channels[nChannel].samples[(m_nInputBufferIndex + m_nSampleSize) % m_n2xSampleSize]);
				k += swprintf(sFormat + k, TEXT("%.4g, "), outputBuffer->channels[nChannel].samples[m_nInputBufferIndex % m_n2xSampleSize]);
				k += swprintf(sFormat + k, TEXT("%.6g"), outputBuffer->channels[nChannel].samples[m_nInputBufferIndex] - 
					inputBuffer->channels[nChannel].samples[m_nInputBufferIndex + m_nSampleSize]);
				OutputDebugString(sFormat);
			}
#endif
#endif
			// Write to output buffer.
#if defined(DEBUG) | defined(_DEBUG)
			DWORD sample_size = normalize_sample(pb_outputDataPointer, outputSample);
			assert(sample_size == m_nSampleSize);
			cbBytesActuallyProcessed += m_nSampleSize;

#else
			cbBytesActuallyProcessed += normalize_sample(pb_outputDataPointer, outputSample);
#endif
			pb_outputDataPointer += m_nSampleSize;


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

		if (m_nInputBufferIndex == m_nSampleSize - 1) // Got a full sample
		{	
			// Got a sample xi length Nh, so calculate outputBuffer
			for (unsigned short nChannel = 0; nChannel != nChannels; nChannel++)
			{
				// Copy the sample buffer, as the rdft routine overwrites it
				// TODO: replace by a copy assignment operator in the inputBuffer class
				for (unsigned int j = 0; j != m_n2xSampleSize; j++)
					m_SampleBufferChannelCopy->samples[j] = inputBuffer->channels[nChannel].samples[j];
				//*m_SampleBufferChannelCopy = inputBuffer->channels[nChannel];

				//  rdft(n, 1, a):
				//        n              :data length (int)
				//                        n >= 2, n = power of 2
				//        a[0...n-1]     :input data
				//                        output data
				//                                a[2*k] = R[k], 0<=k<n/2
				//                                a[2*k+1] = I[k], 0<k<n/2
				//                                a[1] = R[n/2]

				// Calculate the Xi(m)
				rdft(m_n2xSampleSize, OouraRForward, m_SampleBufferChannelCopy->samples);  // get DFT of inputBuffer

				// Multiply point by point the complex Yi(m) = Xi(m)Hz(m).  filter (Hz(m)) is already calculated (in AllocateStreamingResources)
				cmult(m_SampleBufferChannelCopy->samples, filter->channels[nChannel].samples, outputBuffer->channels[nChannel].samples, m_n2xSampleSize);

				//get back the yi
				rdft(m_n2xSampleSize, OouraRBackward, outputBuffer->channels[nChannel].samples); // take the Inverse DFT

				// scale
				for (unsigned int j = 0; j != m_nSampleSize; j++)  // No need to scale second half of output buffer, as going to discard that
					outputBuffer->channels[nChannel].samples[j] *= static_cast<FFT_type>(2.0 / (double) m_n2xSampleSize);

				// move overlap block x i to previous block x i-1
				// TODO: To avoid this copy (ie, make inputBuffer circular), would need to have a filter that was aligned
				for (unsigned int j = 0; j < m_nSampleSize; j++)
					inputBuffer->channels[nChannel].samples[j + m_nSampleSize] = inputBuffer->channels[nChannel].samples[j]; // TODO: MemCpy
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