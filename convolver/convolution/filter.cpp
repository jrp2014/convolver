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

#include "convolution\ffthelp.h"
#include "convolution\filter.h"

Filter::Filter(TCHAR szFilterFileName[MAX_PATH], const WORD& nPartitions, const WORD& nFilterChannel, const DWORD& nSamplesPerSec,
			   const unsigned int& nPlanningRigour) : 
nPartitions (nPartitions),
nSamplesPerSec(nSamplesPerSec)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Filter::Filter " << nPartitions << " " << nSamplesPerSec << std::endl;);
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
	CWaveFileHandle pFilterWave(szFilterFileName, SFM_READ, &sf_FilterFormat, nSamplesPerSec); // Throws, if file invalid

	if(sf_FilterFormat.channels - 1 < nFilterChannel)
	{
		throw filterException("Filter channel number too big");
	}

	if(nSamplesPerSec != sf_FilterFormat.samplerate)
	{
		throw filterException("Filter does not have the specified sample rate");
	}
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

	if(wfexFilterFormat.Format.nChannels - 1 < nFilterChannel)
	{
		throw filterException("Filter channel number too big");
	}

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

	// Setup the filter
	// A partition will contain half real data, and half zero padding.  Taking the DFT will, of course, overwrite that padding
	nHalfPartitionLength = (nFilterLength + nPartitions - 1) / nPartitions;

	// Check that the filter is not too big ...
	if ( nHalfPartitionLength > HalfLargestDFTSize )
	{
		throw filterException("Filter too long to handle");
	}

	// .. or filter too small
	if ( nHalfPartitionLength == 0 )
	{
		filterException("Filter too short");
	}

	if ( nHalfPartitionLength == 1 )
		nHalfPartitionLength = 2; // Make sure that the minimum partition length is 4

	nPartitionLength = nHalfPartitionLength * 2;

	int nHalfPaddedPartitionLength = GetOptimalDFTSize(nHalfPartitionLength);
	int nPaddedPartitionLength = nHalfPaddedPartitionLength * 2;

#ifdef OOURA
	// Initialize the Oooura workspace;
	ip.resize(static_cast<int>(sqrt(static_cast<float>(nPaddedPartitionLength)) + 2));
	ip[0]=0; // signal the need to initialize
	w.resize(nHalfPaddedPartitionLength);	// w[0..nPaddedPartitionLength/2 - 1]
#endif

	// Initialise the Filter

	PlanningRigour pr;
#ifdef FFTW
	nFFTWPartitionLength = 2*(nPaddedPartitionLength/2+1);
#ifdef ARRAY
	coeffs = SampleBuffer(nPartitions, nFFTWPartitionLength);
#else
	coeffs = SampleBuffer(nPartitions, ChannelBuffer(nFFTWPartitionLength));
#endif
	plan =  fftwf_plan_dft_r2c_1d(nPaddedPartitionLength,
		coeffs.c_ptr(), reinterpret_cast<fftwf_complex*>(coeffs.c_ptr()),
		pr.Flag[nPlanningRigour]);
	reverse_plan =  fftwf_plan_dft_c2r_1d(nPaddedPartitionLength, 
		reinterpret_cast<fftwf_complex*>(coeffs.c_ptr()), coeffs.c_ptr(),
		pr.Flag[nPlanningRigour]);
#else
#ifdef ARRAY
	coeffs = SampleBuffer(nPartitions, nPaddedPartitionLength);
#else
	coeffs = SampleBuffer(nPartitions, ChannelBuffer(nPaddedPartitionLength));
#endif
#endif

#ifdef LIBSNDFILE
	std::vector<float> item(sf_FilterFormat.channels);
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
	WORD nPartition = 0;
	DWORD nFrame = 0;					// LibSndFile refers to blocks as frames
	while (nFrame < nFilterLength)
	{
#ifdef LIBSNDFILE
		if (1 == pFilterWave.readf_float(&item[0], 1))		// 1 = 1 frame = nChannel items
		{
			// Got a frame / block (ie, the items / samples for each channel)
			// Pick the sample corresponding to the selected channel
			coeffs[nPartition][nFrame % nHalfPartitionLength] = item[nFilterChannel];
#if defined(DEBUG) | defined(_DEBUG)
			if (item[nFilterChannel] > maxSample)
				maxSample = item[nFilterChannel];
			if (item[nFilterChannel] < minSample)
				minSample = item[nFilterChannel];
#endif
		}
		else
		{
			throw filterException("Failed to read a frame");
		};

#else
		for(WORD nChannel = 0; nChannel <= nFilterChannel; ++nChannel)
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

		coeffs[nPartition][nFrame % nHalfPartitionLength] = sample;

#if defined(DEBUG) | defined(_DEBUG)
		if (sample > maxSample)
			maxSample = sample;
		if (sample < minSample)
			minSample = sample;
#endif

		// skip the rest of the block
		for(;nChannel < wfexFilterFormat.Format.nChannels ; ++ nChannel)
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
		}

#endif

		++nFrame;
		if (nFrame % nHalfPartitionLength == 0)
		{
			// Pad partition
			for (int nPadding = nHalfPartitionLength; nPadding < nPaddedPartitionLength; ++nPadding)
			{
				coeffs[nPartition][nPadding] = 0;
			}

			// Take the DFT
#ifdef FFTW
			fftwf_execute_dft_r2c(plan, 
				coeffs.c_ptr(nPartition), 
				reinterpret_cast<fftwf_complex*>(coeffs.c_ptr(nPartition)));
#elif defined(OOURA_SIMPLE)
			rdft(nPaddedPartitionLength, OouraRForward, coeffs.c_ptr(nPartition));
#elif defined(OOURA)
			rdft(nPaddedPartitionLength, OouraRForward, coeffs.c_ptr(nPartition), &ip[0], &w[0]);
#else
#error "No FFT package defined"
#endif

			// Scale here, so that we don't need to do so when convolving
#ifdef FFTW
			coeffs[nPartition] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#elif defined(OOURA) || defined(SIMPLE_OOURA)
			coeffs[nPartition] *= static_cast<float>(2.0L / nPaddedPartitionLength);
#else
#error "No FFT package defined"
#endif
			++nPartition;
		}
	} // while

	// Pad the current partition (if necessary)
	if (nFrame % nHalfPartitionLength != 0)
	{
		for (int nSample = nFrame % nHalfPartitionLength; nSample < nPaddedPartitionLength; ++nSample)
		{
			coeffs[nPartition][nSample] = 0;
		}

		// Take the DFT
#ifdef FFTW
		fftwf_execute_dft_r2c(plan, coeffs.c_ptr(nPartition), reinterpret_cast<fftwf_complex*>(coeffs.c_ptr(nPartition)));
#elif defined(OOURA_SIMPLE)
		rdft(nPaddedPartitionLength, OouraRForward, coeffs.c_ptr(nPartition));
#elif defined(OOURA)
		rdft(nPaddedPartitionLength, OouraRForward, coeffs.c_ptr(nPartition), &ip[0], &w[0]);
#else
#error "No FFT package defined"
#endif

		// Scale here, so that we don't need to do so when convolving
#ifdef FFTW
		coeffs[nPartition] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#elif defined(OOURA) || defined(SIMPLE_OOURA)
		coeffs[nPartition] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#else
#error "No FFT package defined"
#endif
		++nPartition;
	}

	// Zero any further partitions
	for (;nPartition < nPartitions; ++nPartition)
	{
		coeffs[nPartition] = 0;
	}

	// The padded lengths become the actual lengths that we are going to work with
	nPartitionLength = nPaddedPartitionLength;
	nHalfPartitionLength = nHalfPaddedPartitionLength;
	nFilterLength = nPartitions * nPartitionLength;


#if defined(DEBUG) | defined(_DEBUG)

	cdebug << "FFT Filter: ";
	DumpSampleBuffer(coeffs);


#ifdef LIBSNDFILE
	cdebug << waveFormatDescription(sf_FilterFormat, "FFT Filter: ") << std::endl;
#else
	cdebug << waveFormatDescription(&wfexFilterFormat, nFilterLength, "FFT Filter:") << std::endl;
#endif

	cdebug << "minSample " << minSample << ", maxSample " << maxSample << std::endl;
#endif

}
