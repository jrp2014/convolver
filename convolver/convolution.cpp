#include  "convolution.h"

// CConvolution Constructor
template <typename FFT_type>CConvolution<FFT_type>::CConvolution(unsigned int n2xContainerSize) : m_n2xSampleSize(n2xContainerSize)
{
	m_SampleBufferChannelCopy = new CChannelBuffer<FFT_type>(n2xContainerSize);  // Need only one copy channel
	m_nSampleBufferLength = n2xContainerSize / 2; // TODO: Check that it really is divisible by 2	
	m_nSampleBufferIndex = 0;
};
// CConvolution Destructor
template <typename FFT_type>CConvolution<FFT_type>::~CConvolution(void)
{
	delete m_SampleBufferChannelCopy;
}




template <typename FFT_type>
DWORD
CConvolution<typename FFT_type>::doConvolution(const BYTE* pbInputData, BYTE* pbOutputData,
																	 const unsigned int nChannels,
																	 const CContainerBuffer<FFT_type>* Filter,
																	 CContainerBuffer<FFT_type>* SampleBuffer,
																	 CContainerBuffer<FFT_type>* OutputBuffer,
																	 DWORD dwBlocksToProcess,
																	 const double WetMix,
																	 const double DryMix
#if defined(DEBUG) | defined(_DEBUG)
																	 , CWaveFile* CWaveFileTrace
#endif	
																	 )  // Returns bytes processed
{
	// Cross-check
	DWORD cbBytesActuallyProcessed = 0;

//	SAMPLE_type   *InputData = (SAMPLE_type *) pbInputData;
//	SAMPLE_type   *OutputData = (SAMPLE_type *) pbOutputData;

	BYTE* pbSampleBufferPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (unsigned int nChannel = 0; nChannel != nChannels; nChannel++)
		{

			// Get the input sample and convert it to a FFT_type (eg, float)
			SampleBuffer->channels[nChannel].containers[m_nSampleBufferIndex] = get_sample(pbSampleBufferPointer); // Channels are interleaved
			pbSampleBufferPointer = next_container(pbSampleBufferPointer);

			// Mix the processed signal with the dry signal.
			double outputSample = (SampleBuffer->channels[nChannel].containers[m_nSampleBufferIndex + m_nSampleBufferLength] * DryMix ) + 
				(OutputBuffer->channels[nChannel].containers[m_nSampleBufferIndex] * WetMix);

#if defined(DEBUG) | defined(_DEBUG)
			// Only for testing with perfect Dirac Delta filters
			if (abs(OutputBuffer->channels[nChannel].containers[m_nSampleBufferIndex] - 
				SampleBuffer->channels[nChannel].containers[m_nSampleBufferIndex + m_nSampleBufferLength]) > 0.00001)
			{
				OutputDebugString(TEXT("\n"));
				TCHAR sFormat[100];
				int k = swprintf(sFormat, TEXT("%i,"), dwBlocksToProcess);
				k += swprintf(sFormat + k, TEXT("%i, "), m_nSampleBufferIndex);
				k += swprintf(sFormat + k, TEXT("%.4g, "), SampleBuffer->channels[nChannel].containers[(m_nSampleBufferIndex + m_nSampleBufferLength) % m_n2xSampleSize]);
				k += swprintf(sFormat + k, TEXT("%.4g"), OutputBuffer->channels[nChannel].containers[m_nSampleBufferIndex % m_n2xSampleSize]);
				OutputDebugString(sFormat);
			}
#endif
			// Write to output buffer.
			cbBytesActuallyProcessed += normalize_sample(pbOutputDataPointer, outputSample);
			pbOutputDataPointer = next_container(pbOutputDataPointer);

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

		if (m_nSampleBufferIndex == m_nSampleBufferLength - 1) // Got a full container
		{	
			// Got a container xi length Nh, so calculate OutputBuffer
			for (unsigned int nChannel = 0; nChannel != nChannels; nChannel++)
			{
				// Copy the container buffer, as the rdft routine overwrites it
				// TODO: replace by a copy assignment operator in the SampleBuffer class
				for (unsigned int j = 0; j != m_n2xSampleSize; j++)
					m_SampleBufferChannelCopy->containers[j] = SampleBuffer->channels[nChannel].containers[j];
				//*m_SampleBufferChannelCopy = SampleBuffer->channels[nChannel];

				//  rdft(n, 1, a):
				//        n              :data length (int)
				//                        n >= 2, n = power of 2
				//        a[0...n-1]     :input data
				//                        output data
				//                                a[2*k] = R[k], 0<=k<n/2
				//                                a[2*k+1] = I[k], 0<k<n/2
				//                                a[1] = R[n/2]

				rdft(m_n2xSampleSize, OouraRForward, m_SampleBufferChannelCopy->containers);  // get DFT of SampleBuffer

				// Multiply point by point the complex Yi(m) = Xi(m)Hz(m).  m_ppFFT_typeFilter (Hz(m)) is already calculated (in AllocateStreamingResources)
				cmult(m_SampleBufferChannelCopy->containers, Filter->channels[nChannel].containers, OutputBuffer->channels[nChannel].containers, m_n2xSampleSize);

				//get back the yi
				rdft(m_n2xSampleSize, OouraRBackward, OutputBuffer->channels[nChannel].containers); // take the Inverse DFT

				// scale
				for (unsigned int j = 0; j != m_nSampleBufferLength; j++)  // No need to scale second half of output buffer, as going to discard that
					OutputBuffer->channels[nChannel].containers[j] *= static_cast<FFT_type>(2.0 / (double) m_n2xSampleSize);

				// move overlap block x i to previous block x i-1
				for (unsigned int j = 0; j < m_nSampleBufferLength; j++)
					SampleBuffer->channels[nChannel].containers[j + m_nSampleBufferLength] = SampleBuffer->channels[nChannel].containers[j];
			}
			m_nSampleBufferIndex = 0;
		}
		else
			m_nSampleBufferIndex++;
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