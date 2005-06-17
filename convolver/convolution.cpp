#include  "convolution.h"

// CConvolution Constructor
template <typename FFT_type>CConvolution<FFT_type>::CConvolution(unsigned int n2xContainerSize) : m_n2xContainerSize(n2xContainerSize)
{
	m_ContainerBufferChannelCopy = new CChannelBuffer<FFT_type>(n2xContainerSize);  // Need only one copy channel
	m_nContainerSize = n2xContainerSize / 2; // TODO: Check that it really is divisible by 2	
	m_nContainerBufferIndex = 0;
};
// CConvolution Destructor
template <typename FFT_type>CConvolution<FFT_type>::~CConvolution(void)
{
	delete m_ContainerBufferChannelCopy;
}




template <typename FFT_type>
DWORD
CConvolution<typename FFT_type>::doConvolution(const BYTE* pbInputData, BYTE* pbOutputData,
																	 const unsigned int nChannels,
																	 const CContainerBuffer<FFT_type>* Filter,
																	 CContainerBuffer<FFT_type>* ContainerBuffer,
																	 CContainerBuffer<FFT_type>* OutputBuffer,
																	 DWORD dwBlocksToProcess,
																	 const FFT_type WetMix,
																	 const FFT_type DryMix
#if defined(DEBUG) | defined(_DEBUG)
																	 , CWaveFile* CWaveFileTrace
#endif	
																	 )  // Returns bytes processed
{
	// Cross-check
	DWORD cbBytesActuallyProcessed = 0;

//	SAMPLE_type   *InputData = (SAMPLE_type *) pbInputData;
//	SAMPLE_type   *OutputData = (SAMPLE_type *) pbOutputData;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (unsigned int nChannel = 0; nChannel != nChannels; nChannel++)
		{

			// Get the input container and convert it to a FFT_type (eg, float)
//			FFT_type inputContainer = container_normalize(*InputData++);

			ContainerBuffer->channels[nChannel].containers[m_nContainerBufferIndex] = get_container(pbInputDataPointer); // Channels are interleaved
			pbInputDataPointer = next_container(pbInputDataPointer);

			// Mix the processed signal with the dry signal.
			FFT_type outputContainer = (ContainerBuffer->channels[nChannel].containers[m_nContainerBufferIndex + m_nContainerSize] * DryMix ) + 
				(OutputBuffer->channels[nChannel].containers[m_nContainerBufferIndex] * WetMix);

#if defined(DEBUG) | defined(_DEBUG)
			// Only for testing with perfect Dirac Delta filters
			if (abs(OutputBuffer->channels[nChannel].containers[m_nContainerBufferIndex] - 
				ContainerBuffer->channels[nChannel].containers[m_nContainerBufferIndex + m_nContainerSize]) > 0.00001)
			{
				OutputDebugString(TEXT("\n"));
				TCHAR sFormat[100];
				int k = swprintf(sFormat, TEXT("%i,"), dwBlocksToProcess);
				k += swprintf(sFormat + k, TEXT("%i, "), m_nContainerBufferIndex);
				k += swprintf(sFormat + k, TEXT("%.4g, "), ContainerBuffer->channels[nChannel].containers[(m_nContainerBufferIndex + m_nContainerSize) % m_n2xContainerSize]);
				k += swprintf(sFormat + k, TEXT("%.4g"), OutputBuffer->channels[nChannel].containers[m_nContainerBufferIndex % m_n2xContainerSize]);
				OutputDebugString(sFormat);
			}
#endif
			// Write to output buffer.
			//*OutputData++ = normalize_container(outputContainer);
			cbBytesActuallyProcessed += normalize_container(pbOutputDataPointer, outputContainer);
			pbOutputDataPointer = next_container(pbOutputDataPointer);

#if defined(DEBUG) | defined(_DEBUG)
			// This does not quite work, because AllocateStreamingResources seems to be called (twice!) after IEEE FFT_type playback, which rewrites the file
			// It is not clear why this happens, as it does not occur after PCM playback
			UINT nSizeWrote;
			HRESULT hr = S_OK;
			if (FAILED(hr = CWaveFileTrace->Write(sizeof(FFT_type), (BYTE *)&outputContainer, &nSizeWrote)))
			{
				SAFE_DELETE( CWaveFileTrace );
				return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
			}
#endif
		}

		if (m_nContainerBufferIndex == m_nContainerSize - 1) // Got a full container
		{	
			// Got a container xi length Nh, so calculate OutputBuffer
			for (unsigned int nChannel = 0; nChannel != nChannels; nChannel++)
			{
				// Copy the container buffer, as the rdft routine overwrites it
				// TODO: replace by a copy assignment operator in the ContainerBuffer class
				for (unsigned int j = 0; j != m_n2xContainerSize; j++)
					m_ContainerBufferChannelCopy->containers[j] = ContainerBuffer->channels[nChannel].containers[j];
				//*m_ContainerBufferChannelCopy = ContainerBuffer->channels[nChannel];

				//  rdft(n, 1, a):
				//        n              :data length (int)
				//                        n >= 2, n = power of 2
				//        a[0...n-1]     :input data
				//                        output data
				//                                a[2*k] = R[k], 0<=k<n/2
				//                                a[2*k+1] = I[k], 0<k<n/2
				//                                a[1] = R[n/2]

				rdft(m_n2xContainerSize, OouraRForward, m_ContainerBufferChannelCopy->containers);  // get DFT of ContainerBuffer

				// Multiply point by point the complex Yi(m) = Xi(m)Hz(m).  m_ppFFT_typeFilter (Hz(m)) is already calculated (in AllocateStreamingResources)
				cmult(m_ContainerBufferChannelCopy->containers, Filter->channels[nChannel].containers, OutputBuffer->channels[nChannel].containers, m_n2xContainerSize);

				//get back the yi
				rdft(m_n2xContainerSize, OouraRBackward, OutputBuffer->channels[nChannel].containers); // take the Inverse DFT

				// scale
				for (unsigned int j = 0; j != m_nContainerSize; j++)  // No need to scale second half of output buffer, as going to discard that
					OutputBuffer->channels[nChannel].containers[j] *= static_cast<FFT_type>(2.0 / (double) m_n2xContainerSize);

				// move overlap block x i to previous block x i-1
				for (unsigned int j = 0; j < m_nContainerSize; j++)
					ContainerBuffer->channels[nChannel].containers[j + m_nContainerSize] = ContainerBuffer->channels[nChannel].containers[j];
			}
			m_nContainerBufferIndex = 0;
		}
		else
			m_nContainerBufferIndex++;
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