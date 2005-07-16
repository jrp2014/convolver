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
CConvolution<FFT_type>::CConvolution(const DWORD nContainerSize, const DWORD nInputBufferIndex = 0) :
m_nContainerSize(nContainerSize), 
m_nInputBufferIndex(nInputBufferIndex),
m_Filter(NULL),
m_InputBuffer(NULL),
m_OutputBuffer(NULL),
m_InputBufferChannelCopy(NULL)
{
	::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));
};

// CConvolution Destructor
template <typename FFT_type>CConvolution<FFT_type>::~CConvolution(void)
{
	delete m_Filter;
	::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));
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
CConvolution<typename FFT_type>::doConvolutionArbitrary(const BYTE* pbInputData, BYTE* pbOutputData,
														DWORD dwBlocksToProcess,
														const double fAttenuation_db,
														const double fWetMix,
														const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
														, CWaveFile* CWaveFileTrace
#endif	
														)  // Returns bytes processed
{
	if (NULL == m_Filter)
		return 0;

	if (m_Filter->nSamples == 0)
		return 0;

	assert(m_Filter->nSamples % 2 == 0);
	const DWORD cnFilterLength = m_Filter->nSamples / 2;  // Nh (the filter is padded to 2 * Nh, and a power of two long, for Ooura routines
	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (WORD nChannel = 0; nChannel != m_Filter->nChannels; nChannel++)
		{
			// Get the input sample and convert it to a FFT_type (eg, float)
			m_InputBuffer->samples[nChannel][m_nInputBufferIndex] = attenuated_sample(fAttenuation_db, get_sample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += m_nContainerSize;
			cbInputBytesProcessed += m_nContainerSize;

			// Mix the processed signal with the dry signal (which lags by a filterlength)
			// This will be silence, until we have gathered a filter length a convolved it to produce some output. Hence with lag
			double outputSample = (m_InputBuffer->samples[nChannel][m_nInputBufferIndex + cnFilterLength] * fDryMix ) + 
				(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] - m_InputBuffer->samples[nChannel][m_nInputBufferIndex + cnFilterLength]) >
				0.001 + 0.01 * max(abs(m_OutputBuffer->samples[nChannel][m_nInputBufferIndex]), abs(m_InputBuffer->samples[nChannel][m_nInputBufferIndex + cnFilterLength])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< m_nInputBufferIndex << ","
					<< nChannel << ", "
					<< m_InputBuffer->samples[nChannel][m_nInputBufferIndex + cnFilterLength] << ","
					<< m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] << ", "
					<< (m_OutputBuffer->samples[nChannel][m_nInputBufferIndex] - m_InputBuffer->samples[nChannel][m_nInputBufferIndex + cnFilterLength]) << std::endl;
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

		if (m_nInputBufferIndex == cnFilterLength - 1) // Got a filter-length's worth of blocks
		{	
			// Got a sample xi length Nh, so calculate m_OutputBuffer
			for (WORD nChannel = 0; nChannel !=  m_Filter->nChannels; nChannel++)
			{
				// Copy the sample buffer, as the rdft routine overwrites it
				// TODO: replace by a copy assignment operator in the SampleBuffer class
				for (DWORD nSample = 0; nSample != m_Filter->nSamples; nSample++)
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
				rdft(m_Filter->nSamples, OouraRForward, m_InputBufferChannelCopy->samples[0]);  // get DFT of m_InputBuffer

				// Multiply point by point the complex Yi(m) = Xi(m)Hz(m).  m_Filter (Hz(m)) is already calculated (in LoadFilter)
				cmult(m_InputBufferChannelCopy->samples[0], m_Filter->samples[nChannel], m_OutputBuffer->samples[nChannel], m_Filter->nSamples);

				//get back the yi
				rdft(m_Filter->nSamples, OouraRBackward, m_OutputBuffer->samples[nChannel]); // take the Inverse DFT

				// scale
				for (DWORD nSample = 0; nSample != cnFilterLength; nSample++)  // No need to scale second half of output buffer, as going to discard that
					m_OutputBuffer->samples[nChannel][nSample] *= static_cast<FFT_type>(2.0L / (double) m_Filter->nSamples);

				// move overlap block x i to previous block x i-1, hence the overlap-save method
				// TODO: To avoid this copy (ie, make m_InputBuffer circular), but would need to have a m_Filter that was aligned or
				// use two InputBuffers, one lagging the other by cnFilterLength
				memcpy(&(m_InputBuffer->samples[nChannel][cnFilterLength]), &(m_InputBuffer->samples[nChannel][0]), cnFilterLength * m_nContainerSize);
				//for (DWORD nSample = 0; nSample != cnFilterLength; nSample++)
				//	m_InputBuffer->samples[nChannel][nSample + cnFilterLength] = m_InputBuffer->samples[nChannel][nSample]; // TODO: MemCpy
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
CConvolution<typename FFT_type>::doConvolutionConstrained(const BYTE* pbInputData, BYTE* pbOutputData,
														  DWORD dwBlocksToProcess,
														  const double fAttenuation_db,
														  const double fWetMix,
														  const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
														  , CWaveFile* CWaveFileTrace
#endif	
														  )  // Returns bytes processed
{
	if (NULL == m_Filter)
		return 0;

	if (m_Filter->nSamples == 0)
		return 0;

	assert(m_Filter->nSamples % 2 == 0);
	const DWORD cnFilterLength = m_Filter->nSamples / 2;  // Nh (the filter is padded to 2 * Nh, and a power of two long, for Ooura routines

	// An alternative semantic would be to remove the following test and leave some input samples unprocessed
	assert(m_nInputBufferIndex == 0); // Ie, that the last input buffer was completely processed
	assert(dwBlocksToProcess % cnFilterLength == 0);  // Only support a whole number of filter lengths
	if (dwBlocksToProcess % cnFilterLength != 0)
		return 0; // ie, no bytes processed.

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (WORD nChannel = 0; nChannel !=  m_Filter->nChannels; nChannel++)
		{
			// Get the input sample and convert it to a FFT_type (eg, float)
			m_InputBuffer->samples[nChannel][m_nInputBufferIndex] = attenuated_sample(fAttenuation_db, get_sample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += m_nContainerSize;
			cbInputBytesProcessed += m_nContainerSize;
		}
		// Got a block

		if (m_nInputBufferIndex == cnFilterLength - 1) // Got a filter-length's worth
		{	
			// Got a sample xi length Nh, so calculate m_OutputBuffer
			for (WORD nChannel = 0; nChannel !=  m_Filter->nChannels; nChannel++)
			{
				// Copy the sample buffer, as the rdft routine overwrites it
				// TODO: replace by a copy assignment operator in the SampleBuffer class
				for (DWORD nSample = 0; nSample != m_Filter->nSamples; nSample++)
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
				rdft(m_Filter->nSamples, OouraRForward, m_InputBufferChannelCopy->samples[0]);  // get DFT of m_InputBuffer

				// Multiply point by point the complex Yi(m) = Xi(m)Hz(m).  m_Filter (Hz(m)) is already calculated (in LoadFilter)
				cmult(m_InputBufferChannelCopy->samples[0], m_Filter->samples[nChannel], m_OutputBuffer->samples[nChannel], m_Filter->nSamples);

				//get back the yi
				rdft(m_Filter->nSamples, OouraRBackward, m_OutputBuffer->samples[nChannel]); // take the Inverse DFT

				// scale
				for (DWORD nSample = 0; nSample != cnFilterLength; nSample++)  // No need to scale second half of output buffer, as going to discard that
					m_OutputBuffer->samples[nChannel][nSample] *= static_cast<FFT_type>(2.0L / (double) m_Filter->nSamples);

				// move overlap block x i to previous block x i-1, hence the overlap-save method
				// TODO: To avoid this copy (ie, make m_InputBuffer circular), would need to have a m_Filter that was aligned or
				// use two InputBuffers, one lagging the other by cnFilterLength
				memcpy(&(m_InputBuffer->samples[nChannel][cnFilterLength]), &(m_InputBuffer->samples[nChannel][0]), cnFilterLength * m_nContainerSize);
				//for (DWORD nSample = 0; nSample != cnFilterLength; nSample++)
				//	m_InputBuffer->samples[nChannel][nSample + cnFilterLength] = m_InputBuffer->samples[nChannel][nSample]; // TODO: MemCpy
			}

			// Now copy the convolved values (in FFT_type format) from OutputBuffer to OutputData (in byte stream PCM, IEEE, etc, format)

			for (DWORD nSample = 0; nSample != cnFilterLength; nSample++)
			{
				for (WORD nChannel = 0; nChannel !=  m_Filter->nChannels; nChannel++) // Channels are interleaved
				{
					// Mix the processed signal with the dry signal (which lags by a filterlength)
					double outputSample = (m_InputBuffer->samples[nChannel][nSample + cnFilterLength] * fDryMix ) + 
						(m_OutputBuffer->samples[nChannel][nSample] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
					// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
					// TODO: not a good test, if one of the values is 0
					if (abs(m_OutputBuffer->samples[nChannel][nSample] - m_InputBuffer->samples[nChannel][nSample + cnFilterLength]) >
						0.001 + 0.01 * max(abs(m_OutputBuffer->samples[nChannel][nSample]), abs(m_InputBuffer->samples[nChannel][nSample + cnFilterLength])))
					{
						cdebug 
							<< dwBlocksToProcess << ","
							<< nSample << ","
							<< nChannel << ", "
							<< m_InputBuffer->samples[nChannel][nSample + cnFilterLength] << ","
							<< m_OutputBuffer->samples[nChannel][nSample] << ", "
							<< (m_OutputBuffer->samples[nChannel][nSample] - m_InputBuffer->samples[nChannel][nSample + cnFilterLength]) << std::endl;
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
};

template<typename FFT_type>
const HRESULT CConvolution<FFT_type>::LoadFilter(TCHAR szFilterFileName[MAX_PATH])
{
	HRESULT hr = S_OK;

	// Load the wave file
	CWaveFile* pFilterWave = new CWaveFile();
	hr = pFilterWave->Open( szFilterFileName, NULL, WAVEFILE_READ );
	if( FAILED(hr) )
	{
		SAFE_DELETE(pFilterWave);
		return hr;
	}

	// Save filter characteristic, for access by the properties page, etc
	::ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat));
	m_WfexFilterFormat = * reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pFilterWave->GetFormat());

	WORD wValidBitsPerSample = pFilterWave->GetFormat()->wBitsPerSample;
	WORD wFormatTag = pFilterWave->GetFormat()->wFormatTag;
	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		wValidBitsPerSample = m_WfexFilterFormat.Samples.wValidBitsPerSample;
		if (m_WfexFilterFormat.SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			wFormatTag = WAVE_FORMAT_PCM;
			// TODO: cross-check m_WfexFilterFormat->Samples.wSamplesPerBlock
		}
		else
			if (m_WfexFilterFormat.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
			{
				wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			}
			else
			{
				return E_INVALIDARG;
			}
	}

	DWORD cFilterLength = pFilterWave->GetSize() / m_WfexFilterFormat.Format.nBlockAlign;  // Filter length in samples (Nh)

	// Check that the filter is not too big ...
	if ( cFilterLength > MAX_FILTER_SIZE )
	{
		::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));
		pFilterWave->Close();
		SAFE_DELETE(pFilterWave);
		return E_OUTOFMEMORY;
	}
	// .. or too small
	if ( cFilterLength < 2 )
	{
		::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));
		pFilterWave->Close();
		SAFE_DELETE(pFilterWave);
		return E_FAIL;
	}

	// Setup the filter

	// Filter length is 2 x Nh. Must be power of 2 for the Ooura FFT package.
	// Eg, if m_cFilterLength = Nh = 6, c2xPaddedFilterLength = 16
	DWORD c2xPaddedFilterLength = 1; // The length of the filter
	for(c2xPaddedFilterLength = 1; c2xPaddedFilterLength < 2 * cFilterLength; c2xPaddedFilterLength *= 2);

	// Initialise the Filter and the various sample buffers that will be used during processing
	delete m_Filter;
	m_Filter = new CSampleBuffer<float>(m_WfexFilterFormat.Format.nChannels, c2xPaddedFilterLength);
	if (m_Filter == NULL)
		return E_OUTOFMEMORY;

	// Also initialize the class buffers
	delete m_InputBuffer;
	m_InputBuffer = new CSampleBuffer<FFT_type>(m_WfexFilterFormat.Format.nChannels, c2xPaddedFilterLength);
	if (m_InputBuffer == NULL)
		return E_OUTOFMEMORY;

	delete m_OutputBuffer;
	m_OutputBuffer = new CSampleBuffer<FFT_type>(m_WfexFilterFormat.Format.nChannels, c2xPaddedFilterLength);
	if (m_OutputBuffer == NULL)
		return E_OUTOFMEMORY;

	delete m_InputBufferChannelCopy;
	m_InputBufferChannelCopy = new CSampleBuffer<FFT_type>(1, c2xPaddedFilterLength);  // Need only one copy channel
	if (m_InputBufferChannelCopy == NULL)
		return E_OUTOFMEMORY;

	const DWORD dwSizeToRead = m_WfexFilterFormat.Format.wBitsPerSample / 8;  // in bytes
	DWORD dwSizeRead = 0;

	FFT_type sample = 0;
#if defined(DEBUG) | defined(_DEBUG)
	FFT_type minSample = 0;
	FFT_type maxSample = 0;
#endif
	BYTE bSample[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // 8 is the biggest sample size (64-bit)
	assert (dwSizeToRead <= 8);

	// Read the filter file
	for (DWORD nSample=0; nSample != cFilterLength; nSample++)
	{
		for (WORD nChannel=0; nChannel !=  m_Filter->nChannels; nChannel++)
		{
			hr = pFilterWave->Read(bSample, dwSizeToRead, &dwSizeRead);
			if (FAILED(hr))
			{
				::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));
				SAFE_DELETE(pFilterWave);
				return hr;
			}

			if (dwSizeRead != dwSizeToRead) // file corrupt, non-existent, etc
			{
				::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));
				pFilterWave->Close();
				delete pFilterWave;
				return E_FAIL;
			}

			// Now convert bSample to the float sample
			switch (wFormatTag)
			{
			case WAVE_FORMAT_PCM:
				switch (m_WfexFilterFormat.Format.wBitsPerSample)	// container size
				{
				case 8:
					{
						assert(dwSizeToRead == sizeof(BYTE));
						sample = static_cast<FFT_type>(bSample[0] - 128);
					}
				case 16:
					{
						assert(dwSizeToRead == sizeof(INT16));
						sample = *reinterpret_cast<INT16*>(bSample);
					}
					break;
				case 24:
					{
						assert(dwSizeToRead == 3);  // 3 bytes
						// Get the input sample
						int i = (char) bSample[2];
						i = (i << 8) | bSample[1];

						switch (wValidBitsPerSample)
						{
						case 16:
							break;
						case 20:
							i = (i << 4) | (bSample[0] >> 4);
							break;
						case 24:
							i = (i << 8) | bSample[0];
							break;
						default:
							return E_INVALIDARG;
						}
						sample = static_cast<FFT_type>(i);
					}
					break;
				case 32:
					{
						assert(dwSizeToRead == sizeof(INT32));
						long  *plSample = (long *) bSample;

						// Get the input sample
						int i = *plSample;

						switch (wValidBitsPerSample)
						{
						case 16:
							i >>= 16;
							break;
						case 20:
							i >>= 12;
							break;
						case 24:
							i >>= 8;
							break;
						case 32:
							break;
						default:
							return E_INVALIDARG;
						}
						sample = static_cast<FFT_type>(i);
					}
					break;
				default:
					return E_NOTIMPL; // Unsupported it sample size
				}
				break;

			case WAVE_FORMAT_IEEE_FLOAT:
				switch (m_WfexFilterFormat.Format.wBitsPerSample)
				{
				case 24:
					return E_NOTIMPL;
				case 32:
					{
						assert(dwSizeToRead == sizeof(float));
						sample = *reinterpret_cast<FFT_type*>(bSample);
					}  // case
					break;
				default:
					return E_NOTIMPL; // Unsupported it sample size
				}
				break;

			default:
				return E_NOTIMPL;	// Filter file format is not supported
			}

			m_Filter->samples[nChannel][nSample] = sample;
#if defined(DEBUG) | defined(_DEBUG)
			if (sample > maxSample)
				maxSample = sample;
			if (sample < minSample)
				minSample = sample;
#endif

		} // for nChannel
	} // for nSample

	// Done with the filter file
	pFilterWave->Close(); 
	delete pFilterWave;

	// Now create a FFT of the filter
	for (unsigned short nChannel=0; nChannel != m_WfexFilterFormat.Format.nChannels; nChannel++)
	{
		rdft(c2xPaddedFilterLength, OouraRForward, m_Filter->samples[nChannel]);		
	}

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << waveFormatDescription(&m_WfexFilterFormat, cFilterLength, "FFT Filter:") << std::endl;
	cdebug << "minSample " << minSample << ", maxSample " << maxSample << std::endl;
#endif

	return hr;
}
