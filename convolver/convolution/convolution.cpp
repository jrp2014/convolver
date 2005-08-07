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

#include  "convolution.h"
#include  "filter.h"

// CConvolution Constructor
CConvolution::CConvolution(TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions, const BYTE nContainerSize) :
bStartWritingOutput(false),
FIR(Filter(szFilterFileName, nPartitions)),
nContainerSize(nContainerSize), 
InputBuffer(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))),
OutputBuffer(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))), // NB. Actually, only need half partition length for DoParititionedConvolution
InputBufferCopy(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))),
ComputationCircularBuffer(PartitionedBuffer(nPartitions, SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength)))),
MultipliedFFTBuffer(SampleBuffer(FIR.nChannels, ChannelBuffer(FIR.nPartitionLength))),
nInputBufferIndex(0),
nCircularBufferIndex(0)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "CConvolution" << std::endl;)
#endif

		// This should not be necessary, as ChannelBuffer should be zero'd on construction.  But it is not for valarray
		Flush();
};

// CConvolution Destructor
CConvolution::~CConvolution(void)
{
}


// Reset various buffers and pointers
void CConvolution::Flush()
{
		for(int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
		{
			InputBuffer[nChannel] = 0;
			OutputBuffer[nChannel] = 0;
			MultipliedFFTBuffer[nChannel] = 0;
			for (int nPartition = 0; nPartition < FIR.nPartitions; ++nPartition)
				ComputationCircularBuffer[nPartition][nChannel] = 0;
		}

	nInputBufferIndex = 0;			// placeholder
	nCircularBufferIndex = 0;		// for partitioned convolution
	bStartWritingOutput = false;	// don't start outputting until we have some convolved output
}


// This version of the convolution routine does partitioned convolution
DWORD
CConvolution::doPartitionedConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
									   DWORD dwBlocksToProcess, // A block contains a sample for each channel
									   const double fAttenuation_db,
									   const double fWetMix,
									   const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
									   , CWaveFileHandle & CWaveFileTrace
#endif	
									   )  // Returns bytes processed
{
	assert(isPowerOf2(FIR.nPartitionLength));

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
		{
			// Mix the processed signal with the dry signal
			double outputSample = (InputBuffer[nChannel][nInputBufferIndex] * fDryMix ) + 
				(OutputBuffer[nChannel][nInputBufferIndex] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(OutputBuffer[nChannel][nInputBufferIndex] - InputBuffer[nChannel][nInputBufferIndex]) >
				0.001 + 0.01 * (abs(OutputBuffer[nChannel][nInputBufferIndex]) + abs(InputBuffer[nChannel][nInputBufferIndex])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< nInputBufferIndex << ","
					<< nCircularBufferIndex << ", "
					<< nChannel << ", "
					<< InputBuffer[nChannel][nInputBufferIndex] << ","
					<< OutputBuffer[nChannel][nInputBufferIndex] << ", "
					<< (OutputBuffer[nChannel][nInputBufferIndex] - InputBuffer[nChannel][nInputBufferIndex]) << std::endl;
			}
#endif
#endif

			// Get the input sample and convert it to a FFT_type (eg, float)
			InputBuffer[nChannel][nInputBufferIndex] = AttenuatedSample(fAttenuation_db, GetSample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += nContainerSize;
			cbInputBytesProcessed += nContainerSize;

			// Write to OutputData
			if(bStartWritingOutput)
			{
#if defined(DEBUG) | defined(_DEBUG)
				DWORD containerSize = NormalizeSample(pbOutputDataPointer, outputSample);
				assert(containerSize == nContainerSize);
				cbOutputBytesGenerated += containerSize; // input container size == output container size
#else
				cbOutputBytesGenerated += NormalizeSample(pbOutputDataPointer, outputSample);
#endif

#if defined(DEBUG) | defined(_DEBUG)
				{
					// This does not quite work, because AllocateStreamingResources seems to be called after IEEE FFT_type playback, which rewrites the file
					// It is not clear why this happens, as it does not occur after PCM playback
					UINT nSizeWrote;
					HRESULT hr = CWaveFileTrace->Write(nContainerSize, pbOutputDataPointer, &nSizeWrote);
					if (FAILED(hr) || nSizeWrote != nContainerSize)
					{
						return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
					}
				}
#endif
				pbOutputDataPointer += nContainerSize;
			}
		} // nChannel

		// Got a block
		if (nInputBufferIndex == FIR.nHalfPartitionLength - 1) // Got half a partition-length's worth
		{	

			//cdebug << "InputBuffer: " << nInputBufferIndex; DumpSampleBuffer(InputBuffer); cdebug << std::endl;

			for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
			{
				// Copy the sample buffer partition as the rdft routine overwrites it
				//for (DWORD nSample=0; nSample != FIR.nPartitionLength; ++nSample)
				//{
				//	InputBufferChannelCopy[0][nSample] = InputBuffer[nChannel][nSample];
				//}
				//::CopyMemory(&InputBufferChannelCopy[0], &InputBuffer[nChannel][0], FIR.nPartitionLength * sizeof(float));
				InputBufferCopy[nChannel] = InputBuffer[nChannel];

				// Calculate the Xi(m)
				//  rdft(n, 1, a):
				//        n              :data length (int)
				//                        n >= 2, n = power of 2
				//        a[0...n-1]     :input data
				//                        output data
				//                                a[2*k] = R[k], 0<=k<n/2
				//                                a[2*k+1] = I[k], 0<k<n/2
				//                                a[1] = R[n/2]

				rdft(FIR.nPartitionLength, OouraRForward, &InputBufferCopy[nChannel][0]);  // get DFT of InputBuffer

				for (DWORD nPartitionIndex = 0; nPartitionIndex != FIR.nPartitions; ++nPartitionIndex)
				{
					cmult(InputBufferCopy[nChannel], FIR.buffer[nPartitionIndex][nChannel],
						MultipliedFFTBuffer[nChannel], FIR.nPartitionLength);	

					//for (DWORD nSample = 0; nSample != FIR.nPartitionLength; ++nSample)
					//{
					//	ComputationCircularBuffer[nCircularBufferIndex][nChannel][nSample] += MultipliedFFTChannelBuffer[nSample];
					//}
					ComputationCircularBuffer[nCircularBufferIndex][nChannel] += MultipliedFFTBuffer[nChannel];

					nCircularBufferIndex++;
					nCircularBufferIndex %= FIR.nPartitions;	// circular
				}

				//get back the yi: take the Inverse DFT
				rdft(FIR.nPartitionLength, OouraRBackward, &ComputationCircularBuffer[nCircularBufferIndex][nChannel][0]);
				// Not necessary to scale here, as did so when reading filter

				// TODO:: use memcpy
				//for (DWORD nSample = 0; nSample != FIR.nHalfPartitionLength; ++nSample)
				//{
				//	// Take the first half of the ComputationCircularBuffer as the output
				//	OutputBuffer[nChannel][nSample] = ComputationCircularBuffer[nCircularBufferIndex][nChannel][nSample];

				//	// Save the previous half partition; the first half will be read in over the next cycle
				//	InputBuffer[nChannel][nSample + FIR.nHalfPartitionLength] = InputBuffer[nChannel][nSample];
				//}
				::CopyMemory(&OutputBuffer[nChannel][0], &ComputationCircularBuffer[nCircularBufferIndex][nChannel][0],
					FIR.nHalfPartitionLength * sizeof(float));
				::CopyMemory(&InputBuffer[nChannel][FIR.nHalfPartitionLength], &InputBuffer[nChannel][0],
					FIR.nHalfPartitionLength * sizeof(float));

				// Zero the circular buffer for the next cycle
				//for (DWORD nSample = 0; nSample != FIR.nPartitionLength; ++nSample)
				//{
				//	ComputationCircularBuffer[nCircularBufferIndex][nChannel][nSample] = 0;
				//}
				//::ZeroMemory(&ComputationCircularBuffer[nCircularBufferIndex][nChannel][0], FIR.nPartitionLength * sizeof(float));
				ComputationCircularBuffer[nCircularBufferIndex][nChannel] = 0;
			} // nChannel


			//cdebug << "OutputBuffer: " ; DumpSampleBuffer(OutputBuffer); cdebug << std::endl;


			// Move the circular buffer on for the next cycle
			nCircularBufferIndex++;
			nCircularBufferIndex %= FIR.nPartitions;	// circular

			nInputBufferIndex = 0;		// start to refill the InputBuffer
			bStartWritingOutput = true;
		}
		else // keep filling InputBuffer
			nInputBufferIndex++;
	} // while

//#if defined(DEBUG) | defined(_DEBUG)
//	cdebug
//		<< "nInputBufferIndex " << nInputBufferIndex
//		<< ", bytes processed: " << cbInputBytesProcessed << " bytes generated: " << cbOutputBytesGenerated << std::endl;
//#endif

	return cbOutputBytesGenerated;
};


// This version of the convolution routine is just plain overlap-save.
DWORD
CConvolution::doConvolution(const BYTE pbInputData[], BYTE pbOutputData[],
							DWORD dwBlocksToProcess, // A block contains a sample for each channel
							const double fAttenuation_db,
							const double fWetMix,
							const double fDryMix
#if defined(DEBUG) | defined(_DEBUG)
							, CWaveFileHandle & CWaveFileTrace
#endif	
							)  // Returns bytes processed
{
	// This convolution algorithm assumes that the filter is stored in the first, and only, partition
	if(FIR.nPartitions != 1)
	{
		return 0;
	}

	assert(isPowerOf2(FIR.nPartitionLength));

	DWORD cbInputBytesProcessed = 0;
	DWORD cbOutputBytesGenerated = 0;

	BYTE* pbInputDataPointer = const_cast<BYTE*>(pbInputData);
	BYTE* pbOutputDataPointer = pbOutputData;

	while (dwBlocksToProcess--)
	{
		for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
		{
			// Mix the processed signal with the dry signal
			// This will be silence, until we have gathered a filter length a convolved it to produce some output. Hence with lag
			double outputSample = (InputBuffer[nChannel][nInputBufferIndex] * fDryMix ) + 
				(OutputBuffer[nChannel][nInputBufferIndex] * fWetMix);

#if defined(DEBUG) | defined(_DEBUG)
#if defined(DIRAC_DELTA_FUNCTIONS)
			// Only for testing with perfect Dirac Delta filters, where the output is supposed to be the same as the input
			// TODO: not a good test, if one of the values is 0
			if (abs(OutputBuffer[nChannel][nInputBufferIndex] - InputBuffer[nChannel][nInputBufferIndex]) >
				0.001 + 0.01 * (abs(OutputBuffer[nChannel][nInputBufferIndex]) + abs(InputBuffer[nChannel][nInputBufferIndex])))
			{
				cdebug 
					<< dwBlocksToProcess << ","
					<< nInputBufferIndex << ","
					<< nCircularBufferIndex << ", "
					<< nChannel << ", "
					<< InputBuffer[nChannel][nInputBufferIndex] << ","
					<< OutputBuffer[nChannel][nInputBufferIndex] << ", "
					<< (OutputBuffer[nChannel][nInputBufferIndex] - InputBuffer[nChannel][nInputBufferIndex]) << std::endl;
			}
#endif
#endif

			// Get the input sample and convert it to a FFT_type (eg, float)
			InputBuffer[nChannel][nInputBufferIndex] = AttenuatedSample(fAttenuation_db, GetSample(pbInputDataPointer)); // Channels are interleaved
			pbInputDataPointer += nContainerSize;
			cbInputBytesProcessed += nContainerSize;

			// Write to OutputData
			if(bStartWritingOutput)
			{
#if defined(DEBUG) | defined(_DEBUG)
				DWORD containerSize = NormalizeSample(pbOutputDataPointer, outputSample);
				assert(containerSize == nContainerSize);
				cbOutputBytesGenerated += containerSize; // input container size == output container size
#else
				cbOutputBytesGenerated += NormalizeSample(pbOutputDataPointer, outputSample);
#endif

#if defined(DEBUG) | defined(_DEBUG)
				{
					// This does not quite work, because AllocateStreamingResources seems to be called after IEEE FFT_type playback, which rewrites the file
					// It is not clear why this happens, as it does not occur after PCM playback
					UINT nSizeWrote;
					HRESULT hr = CWaveFileTrace->Write(nContainerSize, pbOutputDataPointer, &nSizeWrote);
					if (FAILED(hr) || nSizeWrote != nContainerSize)
					{
						return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to trace file"), hr);
					}
				}
#endif
				pbOutputDataPointer += nContainerSize;
			}
		}

		// Got a block
		if (nInputBufferIndex == FIR.nHalfPartitionLength - 1) // Got half a partition-length's worth
		{	
			for (int nChannel = 0; nChannel < FIR.nChannels; ++nChannel)
			{
				// Copy the sample buffer partition as the rdft routine overwrites it
				//for (DWORD nSample=0; nSample != FIR.nPartitionLength; ++nSample)
				//{
				//	InputBufferChannelCopy[0][nSample] = InputBuffer[nChannel][nSample];
				//}
				//::CopyMemory(&InputBufferChannelCopy[0], &InputBuffer[nChannel][0], FIR.nPartitionLength * sizeof(float));
				InputBufferCopy[nChannel] = InputBuffer[nChannel];

				// Calculate the Xi(m)
				//  rdft(n, 1, a):
				//        n              :data length (int)
				//                        n >= 2, n = power of 2
				//        a[0...n-1]     :input data
				//                        output data
				//                                a[2*k] = R[k], 0<=k<n/2
				//                                a[2*k+1] = I[k], 0<k<n/2
				//                                a[1] = R[n/2]

				rdft(FIR.nPartitionLength, OouraRForward, &InputBufferCopy[nChannel][0]);  // get DFT of InputBuffer

				cmult(InputBufferCopy[nChannel], FIR.buffer[0][nChannel],					// use the first partition only
					OutputBuffer[nChannel], FIR.nPartitionLength);	

				//get back the yi: take the Inverse DFT
				rdft(FIR.nPartitionLength, OouraRBackward, &OutputBuffer[nChannel][0]);
				// Not necessary to scale here, as did so when reading filter

				//for (DWORD nSample = 0; nSample != FIR.nHalfPartitionLength; ++nSample)
				//{
				//	// Save the previous half partition; the first half will be read in over the next cycle
				//	InputBuffer[nChannel][nSample + FIR.nHalfPartitionLength] = InputBuffer[nChannel][nSample];
				//}
				::CopyMemory(&InputBuffer[nChannel][FIR.nHalfPartitionLength], &InputBuffer[nChannel][0],
					FIR.nHalfPartitionLength * sizeof(float));
			}
			nInputBufferIndex = 0;
			bStartWritingOutput = true;
		}
		else // keep filling InputBuffer
			nInputBufferIndex++;
	} // while

//#if defined(DEBUG) | defined(_DEBUG)
//	cdebug
//		<< "nInputBufferIndex " << nInputBufferIndex
//		<< ", bytes processed: " << cbInputBytesProcessed << " bytes generated: " << cbOutputBytesGenerated << std::endl;
//#endif

	return cbOutputBytesGenerated;
};

/* Complex multiplication */
inline void CConvolution::cmult(const std::valarray<float>& A, const std::valarray<float>& B, std::valarray<float>& C, const DWORD N)
{
	DWORD R;
	DWORD I;
	float T1;
	float T2;

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

	// assert(N % 4 == 0);
	// __assume(N % 4 == 0);
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
HRESULT SelectConvolution(WAVEFORMATEX* & pWave, CConvolution* & convolution, TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions)
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

	//	delete convolution;

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
				convolution = new Cconvolution_pcm8(szFilterFileName, nPartitions);
				break;

			case 16:
				convolution = new Cconvolution_pcm16(szFilterFileName, nPartitions);
				break;

			case 24:
				{
					switch (wValidBitsPerSample)
					{
					case 16:
						convolution = new Cconvolution_pcm24<16>(szFilterFileName, nPartitions);
						break;

					case 20:
						convolution = new Cconvolution_pcm24<20>(szFilterFileName, nPartitions);
						break;

					case 24:
						convolution = new Cconvolution_pcm24<24>(szFilterFileName, nPartitions);
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
						convolution = new Cconvolution_pcm32<16>(szFilterFileName, nPartitions);
						break;

					case 20:
						convolution = new Cconvolution_pcm32<20>(szFilterFileName, nPartitions);
						break;

					case 24:
						convolution = new Cconvolution_pcm32<24>(szFilterFileName, nPartitions);
						break;

					case 32:
						convolution = new Cconvolution_pcm32<32>(szFilterFileName, nPartitions);
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
				convolution = new Cconvolution_ieeefloat(szFilterFileName, nPartitions);
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
};

// Convolve the filter with white noise to get the maximum output, from which we calculate the maximum attenuation
HRESULT calculateOptimumAttenuation(double& fAttenuation, TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions)
{
	HRESULT hr = S_OK;

	try 
	{
		Cconvolution_ieeefloat c(szFilterFileName, nPartitions == 0 ? 1 : nPartitions); // 0 used to signal use of overlap-save

		const WORD nSamples = 5; // length of the test sample, in filter lengths
		const DWORD nBlocks = nSamples * c.FIR.nFilterLength;
		const DWORD nBufferLength = nBlocks * c.FIR.nChannels;	// Channels are interleaved

#if defined(DEBUG) | defined(_DEBUG)
		CWaveFileHandle CWaveFileTrace;
		if (FAILED(hr = CWaveFileTrace->Open(TEXT("c:\\temp\\AttenuationTrace.wav"), &c.FIR.wfexFilterFormat.Format, WAVEFILE_WRITE)))
		{
			return DXTRACE_ERR_MSGBOX(TEXT("Failed to open trace file for writing"), hr);
		}
#endif

		std::vector<float>InputSamples(nBufferLength);
		std::vector<float>OutputSamples(nBufferLength);

		// Seed the random-number generator with current time so that
		// the numbers will be different every time we run.
		srand( (unsigned)time( NULL ) );

		bool again = TRUE;
		float maxSample = 0;

		do
		{
			for(int i = 0; i < nBufferLength; i++)
			{
				InputSamples[i] = static_cast<float>((2.0 * rand() - RAND_MAX) / RAND_MAX); // -1..1
				OutputSamples[i] = 0;  // silence
			}

			// nPartitions == 0 => use overlap-save version
			DWORD nBytesGenerated = nPartitions == 0 ?
				c.doConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
				/* dwBlocksToProcess */ nBlocks,
				/* fAttenuation_db */ 0,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
				, CWaveFileTrace
#endif
				)
				: c.doPartitionedConvolution(reinterpret_cast<BYTE*>(&InputSamples[0]), reinterpret_cast<BYTE*>(&OutputSamples[0]),
				/* dwBlocksToProcess */ nBlocks,
				/* fAttenuation_db */ 0,
				/* fWetMix,*/ 1.0L,
				/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
				, CWaveFileTrace
#endif
				);
			// The output will be missing the last half filter length, the first time around
			assert (nBytesGenerated % sizeof(float) == 0);
			assert (nBytesGenerated <= nBufferLength * sizeof(float));

			// Scan the output buffer for larger output samples
			again = FALSE;
			for(int i = 0; i  < nBytesGenerated / sizeof(float); ++i)
			{
				if (abs(OutputSamples[i]) > maxSample)
				{
					maxSample = abs(OutputSamples[i]);
					again = TRUE; // Keep convolving until find no larger output samples
				}
			}
		} while (again);

		// maxSample 0..1
		// 10 ^ (fAttenuation_db / 20) = 1
		// Limit fAttenuation to +/-MAX_ATTENUATION dB
		fAttenuation = maxSample > 1e-8 ? 20.0f * log(1.0f / maxSample) : 0;

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
		return hr;
	}
	catch (...)
	{
		return E_OUTOFMEMORY;
	}
}

