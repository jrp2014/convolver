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

#include "convolution\filter.h"

Filter::Filter(TCHAR szFilterFileName[MAX_PATH], const int& nPartitions, const DWORD& nSampleRate) : 
nPartitions (nPartitions),
nSampleRate(nSampleRate)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Filter::Filter " << nPartitions << " " << nSampleRate << std::endl;);
#endif
#ifndef LIBSNDFILE
	HRESULT hr = S_OK;
#endif

	 // TODO:: unclear whether this has any effect
	SIMDFlushToZero();	// Set flush to zero processor mode for speed, with a loss of accuracy

	if (nPartitions == 0)
	{
		throw filterException("Number of partitions must be at least one");
	}

	// Load the wave file
#ifdef LIBSNDFILE
	::ZeroMemory(&sf_FilterFormat, sizeof(SF_INFO));
	CWaveFileHandle pFilterWave(szFilterFileName, SFM_READ, &sf_FilterFormat, nSampleRate); // Throws, if file invalid

	// Save number of channels and sample rate for easy access
	nChannels = sf_FilterFormat.channels;
	this->nSampleRate = sf_FilterFormat.samplerate;

	nFilterLength = sf_FilterFormat.frames;

#else
	CWaveFileHandle pFilterWave;
	hr = pFilterWave->Open( szFilterFileName, NULL, WAVEFILE_READ );
	if( FAILED(hr) )
	{
		throw filterException(hr);
	}

	// Save filter characteristic, for access by the properties page, etc
	::ZeroMemory(&wfexFilterFormat, sizeof(wfexFilterFormat));
	wfexFilterFormat.Format = *pFilterWave->GetFormat();

	// Save number of channels and sample rate for easy access
	nChannels = wfexFilterFormat.Format.nChannels;
	this->nSampleRate = wfexFilterFormat.Format.nSampleRate;

	WORD wValidBitsPerSample = wfexFilterFormat.Format.wBitsPerSample;
	WORD wFormatTag = wfexFilterFormat.Format.wFormatTag;

	// nBlockAlign should equal nChannels * wBitsPerSample / 8 (bits per byte) for 
	// WAVE_FORMAT_PCM, WAVE_FORMAT_IEEE_FLOAT and WAVE_FORMAT_EXTENSIBLE
	nFilterLength = pFilterWave->GetSize() / (wfexFilterFormat.Format.nChannels * wfexFilterFormat.Format.wBitsPerSample / 8);

	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		wfexFilterFormat = *(WAVEFORMATEXTENSIBLE*) pFilterWave->GetFormat();  // TODO: Check that this works
		// wValidBitsPerSample: usually equal to WAVEFORMATEX.wBitsPerSample, 
		// but can store 20 bits in a 32-bit container, for example
		wValidBitsPerSample = wfexFilterFormat.Samples.wValidBitsPerSample;

		if (wfexFilterFormat.SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			wFormatTag = WAVE_FORMAT_PCM;
			//// HACK: for Audition -- doesn't work
			//wfexFilterFormat.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
			//wFormatTag = WAVE_FORMAT_IEEE_FLOAT; // For Audition-generated files
		}
		else
		{
			if (wfexFilterFormat.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
			{
				wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			}
			else
			{
				throw filterException("Subformat not PCM or IEEE Float");
			}
		}
	}
#endif

	// Check that the filter is not too big ...
	if ( nFilterLength > MAX_FILTER_SIZE )
	{
		throw filterException("Filter too long to handle");
	}

	// Setup the filter
	// A partition will contain half real data, and half zero padding.  Taking the DFT will, of course, overwrite that padding
	nHalfPartitionLength = (nFilterLength + nPartitions - 1) / nPartitions;

	// .. or filter too small
	if ( nHalfPartitionLength == 0 )
	{
		filterException("Filter too short");
	}

	if ( nHalfPartitionLength == 1 )
		nHalfPartitionLength = 2; // Make sure that the minimum partition length is 4

	nPartitionLength = nHalfPartitionLength * 2;

#ifdef FFTW
	// Don't need to pad to power of 2 for FFTW
	int nPaddedPartitionLength = nPartitionLength;
#else
	int nPaddedPartitionLength = 4;	// Minimum length 4, to allow SIMD optimization
	// Now make sure that the partition length is a power of two long for the Ooura FFT package. (Will 0 pad if necessary.)
	while (nPaddedPartitionLength < nPartitionLength)
	{
		nPaddedPartitionLength *= 2;
	}
#endif

	DWORD nHalfPaddedPartitionLength = nPaddedPartitionLength / 2;

#ifdef OOURA
	// Initialize the Oooura workspace;
	ip.resize(static_cast<int>(sqrt(static_cast<float>(nPaddedPartitionLength)) + 2));
	ip[0]=0; // signal the need to initialize
	w.resize(nHalfPaddedPartitionLength);	// w[0..nPaddedPartitionLength/2 - 1]
#endif

	// Initialise the Filter

#ifdef FFTW
	nFFTWPartitionLength = 2*(nPaddedPartitionLength/2+1);
#ifdef ARRAY
	coeffs = PartitionedBuffer(nPartitions, nChannels, nFFTWPartitionLength);
#else
	coeffs = PartitionedBuffer(nPartitions, SampleBuffer(nChannels, ChannelBuffer(nFFTWPartitionLength)));
#endif
	plan =  fftwf_plan_dft_r2c_1d(nPaddedPartitionLength,
		&coeffs[0][0][0], reinterpret_cast<fftwf_complex*>(&coeffs[0][0][0]),
		FFTW_MEASURE);
	reverse_plan =  fftwf_plan_dft_c2r_1d(nPaddedPartitionLength, 
		reinterpret_cast<fftwf_complex*>(&coeffs[0][0][0]), &coeffs[0][0][0],
		FFTW_MEASURE);
#else
#ifdef ARRAY
	coeffs = PartitionedBuffer(nPartitions, nChannels, nPaddedPartitionLength);
#else
	coeffs = PartitionedBuffer(nPartitions, SampleBuffer(nChannels, ChannelBuffer(nPaddedPartitionLength)));
#endif
#endif

#ifdef LIBSNDFILE
	std::vector<float> item(nChannels);
#else
	assert(wfexFilterFormat.Format.wBitsPerSample >= wValidBitsPerSample);
	const DWORD dwSizeToRead = wfexFilterFormat.Format.wBitsPerSample / 8;  // container size, in bytes

	assert (dwSizeToRead <= 8);
	std::vector<BYTE> bSample(8,0); // 8 is the biggest sample size (64-bit)

	DWORD dwSizeRead = 0;

	float sample = 0;
#endif

#if defined(DEBUG) | defined(_DEBUG)
	float minSample = 0;
	float maxSample = 0;
#endif

	// Read the filter file
	int nPartition = 0;
	int nFrame = 0;					// LibSndFile refers to blocks as frames
	while (nFrame < nFilterLength)
	{
#ifdef LIBSNDFILE
		if (1 == pFilterWave.readf_float(&item[0], 1))		// 1 = 1 frame = nChannel items
		{
			// Got a frame / block ... the items / samples for each channel
			for (int nChannel=0; nChannel < nChannels; ++nChannel)
			{
				coeffs[nPartition][nChannel][nFrame % nHalfPartitionLength] = item[nChannel];
#if defined(DEBUG) | defined(_DEBUG)
				if (item[nChannel] > maxSample)
					maxSample = item[nChannel];
				if (item[nChannel] < minSample)
					minSample = item[nChannel];
#endif
			}
		}
		else
		{
			throw filterException("Failed to read a frame");
		};

#else
		for (int nChannel=0; nChannel < nChannels; ++nChannel)
		{
			hr = pFilterWave->Read(&bSample[0], dwSizeToRead, &dwSizeRead);
			if (FAILED(hr))
			{
				throw filterException("Failed to read a block");
			}

			if (dwSizeRead != dwSizeToRead) // file corrupt, non-existent, etc
			{
				throw filterException("Failed to read a complete block");
			}

			// Now convert bSample to the float sample
			switch (wFormatTag)
			{
			case WAVE_FORMAT_PCM:
				switch (wfexFilterFormat.Format.wBitsPerSample)	// container size
				{
				case 8:
					{
						assert(dwSizeToRead == sizeof(BYTE));
						sample = static_cast<float>(bSample[0] - 128);
					}
					break;
				case 16:
					{
						assert(dwSizeToRead == sizeof(INT16));
						sample = *reinterpret_cast<INT16*>(&bSample[0]);
					}
					break;
				case 24:
					{
						assert(dwSizeToRead == 3);  // 3 bytes
						// Get the input sample
						int i = bSample[2];
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
							throw filterException("Bit depth for 24-bit container must be 16, 20 or 24");
						}
						sample = static_cast<float>(i);
					}
					break;
				case 32:
					{
						assert(dwSizeToRead == sizeof(INT32));

						//// Get the input sample
						INT32 i = *reinterpret_cast<INT32 *>(&bSample[0]);

						////sample = (bSample[3] << 24) + (bSample[2] << 16) + (bSample[1] << 8) + bSample[0]; // little-endian. (RIFF)
						////sample = (bSample[0] << 24) + (bSample[1] << 16) + (bSample[2] << 8) + bSample[3]; // big-endian. (RIFX)

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
							throw filterException("Bit depth for 32-bit container must be 16, 20, 24 or 32");
						}
						sample = static_cast<float>(i);
					}
					break;
				default:
					throw filterException("Unsupported PCM sample size"); // Unsupported it sample size
				}
				break;

			case WAVE_FORMAT_IEEE_FLOAT:
				switch (wfexFilterFormat.Format.wBitsPerSample)
				{
				case 16:
					throw filterException("16-bit IEEE float sample size not implemented");
				case 24:
					throw filterException("24-bit IEEE float sample size not implemented");
				case 32:
					{
						assert(dwSizeToRead == sizeof(float));
						sample = *reinterpret_cast<float*>(&bSample[0]);
					}  // case
					break;
				case 64:
					{
						assert(dwSizeToRead == sizeof(double));
						sample = *reinterpret_cast<double*>(&bSample[0]);
					}  // case
					break;
				default:
					throw filterException("Invalid IEEE float sample size"); // Unsupported it sample size
				}
				break;

			default:
				throw filterException("Only PCM and IEEE Float file formats supported"););	// Filter file format is not supported
			}

			coeffs[nPartition][nChannel][nFrame % nHalfPartitionLength] = sample;

#if defined(DEBUG) | defined(_DEBUG)
			if (sample > maxSample)
				maxSample = sample;
			if (sample < minSample)
				minSample = sample;
#endif

		} // for nChannel
#endif

		++nFrame;
		if (nFrame % nHalfPartitionLength == 0)
		{
			// Pad partition
			for (int nChannel=0; nChannel < nChannels; ++nChannel)
			{
				for (int nPadding = nHalfPartitionLength; nPadding < nPaddedPartitionLength; ++nPadding)
				{
					coeffs[nPartition][nChannel][nPadding] = 0;
				}

				// Take the DFT
#ifdef FFTW
				fftwf_execute_dft_r2c(plan, 
					&coeffs[nPartition][nChannel][0], 
					reinterpret_cast<fftwf_complex*>(&coeffs[nPartition][nChannel][0]));
#elif defined(OOURA_SIMPLE)
				rdft(nPaddedPartitionLength, OouraRForward, &coeffs[nPartition][nChannel][0]);
#elif defined(OOURA)
				rdft(nPaddedPartitionLength, OouraRForward, &coeffs[nPartition][nChannel][0], &ip[0], &w[0]);
#else
#error "No FFT package defined"
#endif

				// Scale here, so that we don't need to do so when convolving
#ifdef FFTW
				coeffs[nPartition][nChannel] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#elif defined(OOURA) || defined(SIMPLE_OOURA)
				coeffs[nPartition][nChannel] *= static_cast<float>(2.0L / nPaddedPartitionLength);
#else
#error "No FFT package defined"
#endif
			}
			++nPartition;
		}
	} // while

	// Pad the current partition (if necessary)
	if (nFrame % nHalfPartitionLength != 0)
	{
		for (int nChannel=0; nChannel < nChannels; ++nChannel)
		{
			for (int nSample = nFrame % nHalfPartitionLength; nSample < nPaddedPartitionLength; ++nSample)
			{
				coeffs[nPartition][nChannel][nSample] = 0;
			}

			// Take the DFT
#ifdef FFTW
			fftwf_execute_dft_r2c(plan, &coeffs[nPartition][nChannel][0], reinterpret_cast<fftwf_complex*>(&coeffs[nPartition][nChannel][0]));
#elif defined(OOURA_SIMPLE)
			rdft(nPaddedPartitionLength, OouraRForward, &coeffs[nPartition][nChannel][0]);
#elif defined(OOURA)
			rdft(nPaddedPartitionLength, OouraRForward, &coeffs[nPartition][nChannel][0], &ip[0], &w[0]);
#else
#error "No FFT package defined"
#endif

			// Scale here, so that we don't need to do so when convolving
#ifdef FFTW
			coeffs[nPartition][nChannel] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#elif defined(OOURA) || defined(SIMPLE_OOURA)
			coeffs[nPartition][nChannel] *= static_cast<float>(2.0L / nPaddedPartitionLength);
#else
#error "No FFT package defined"
#endif
		}
		++nPartition;
	}

	// Zero any further partitions (valarray should do this, but doesn't)
	for (;nPartition < nPartitions; ++nPartition)
	{
		for (int nChannel=0; nChannel < nChannels; ++nChannel)
		{
			coeffs[nPartition][nChannel] = 0;
		}
	}

	// The padded lengths become the actual lengths that we are going to work with
	nPartitionLength = nPaddedPartitionLength;
	nHalfPartitionLength = nHalfPaddedPartitionLength;
	nFilterLength = nPartitions * nPartitionLength;


#if defined(DEBUG) | defined(_DEBUG)

	cdebug << "FFT Filter: ";
	DumpPartitionedBuffer(coeffs);


#ifdef LIBSNDFILE
	cdebug << waveFormatDescription(sf_FilterFormat, "FFT Filter: ") << std::endl;
#else
	cdebug << waveFormatDescription(&wfexFilterFormat, nFilterLength, "FFT Filter:") << std::endl;
#endif

	cdebug << "minSample " << minSample << ", maxSample " << maxSample << std::endl;
#endif

}
