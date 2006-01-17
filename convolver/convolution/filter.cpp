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

Filter::Filter(const TCHAR szFilterFileName[MAX_PATH], const WORD& nPartitions, const WORD& nFilterChannel, const DWORD& nSamplesPerSec,
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
		throw filterException("Number of partitions must be at least one", szFilterFileName);
	}

	// Load the wave file
#ifdef LIBSNDFILE
	::ZeroMemory(&sf_FilterFormat_, sizeof(SF_INFO));
	CWaveFileHandle pFilterWave(szFilterFileName, SFM_READ, &sf_FilterFormat_, nSamplesPerSec); // Throws, if file invalid

	if(sf_FilterFormat_.channels - 1 < nFilterChannel)
	{
		throw filterException("Filter channel number too big", szFilterFileName);
	}

	if(nSamplesPerSec != sf_FilterFormat_.samplerate)
	{
		throw filterException("Filter does not have the specified sample rate", szFilterFileName);
	}
	nFilterLength_ = sf_FilterFormat_.frames;

#else
	CWaveFileHandle pFilterWave;
	hr = pFilterWave->Open( szFilterFileName, NULL, WAVEFILE_READ );
	if( FAILED(hr) )
	{
		throw filterException(hr);
	}

	// Save filter characteristic, for access by the properties page, etc
	::ZeroMemory(&wfexFilterFormat_, sizeof(wfexFilterFormat_));
	wfexFilterFormat_.Format = *pFilterWave->GetFormat();

	if(wfexFilterFormat_.Format.nChannels - 1 < nFilterChannel)
	{
		throw filterException("Filter channel number too big");
	}

	this->nSampleRate = wfexFilterFormat_.Format.nSampleRate;

	WORD wValidBitsPerSample = wfexFilterFormat_.Format.wBitsPerSample;
	WORD wFormatTag = wfexFilterFormat_.Format.wFormatTag;

	// nBlockAlign should equal nChannels * wBitsPerSample / 8 (bits per byte) for 
	// WAVE_FORMAT_PCM, WAVE_FORMAT_IEEE_FLOAT and WAVE_FORMAT_EXTENSIBLE
	nFilterLength_ = pFilterWave->GetSize() / (wfexFilterFormat_.Format.nChannels * wfexFilterFormat_.Format.wBitsPerSample / 8);

	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		wfexFilterFormat_ = *(WAVEFORMATEXTENSIBLE*) pFilterWave->GetFormat();  // TODO: Check that this works
		// wValidBitsPerSample: usually equal to WAVEFORMATEX.wBitsPerSample, 
		// but can store 20 bits in a 32-bit container, for example
		wValidBitsPerSample = wfexFilterFormat_.Samples.wValidBitsPerSample;

		if (wfexFilterFormat_.SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
		{
			wFormatTag = WAVE_FORMAT_PCM;
			//// HACK: for Audition -- doesn't work
			//wfexFilterFormat_.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
			//wFormatTag = WAVE_FORMAT_IEEE_FLOAT; // For Audition-generated files
		}
		else
		{
			if (wfexFilterFormat_.SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
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
	nHalfPartitionLength_ = (nFilterLength_ + nPartitions - 1) / nPartitions;

	OptimalDFT oDFT;	// helper
	// Check that the filter is not too big ...
	if ( nHalfPartitionLength_ > oDFT.HalfLargestDFTSize )
	{
		throw filterException("Filter too long to handle", szFilterFileName);
	}

	// .. or filter too small
	if ( nHalfPartitionLength_ == 0 )
	{
		filterException("Filter too short", szFilterFileName);
	}

	if ( nHalfPartitionLength_ == 1 )
		nHalfPartitionLength_ = 2; // Make sure that the minimum partition length is 4

	nPartitionLength_ = nHalfPartitionLength_ * 2;

	int nHalfPaddedPartitionLength = oDFT.GetOptimalDFTSize(nHalfPartitionLength_);
	int nPaddedPartitionLength = nHalfPaddedPartitionLength * 2;

#ifdef OOURA
	// Initialize the Oooura workspace;
	ip_.resize(static_cast<int>(sqrt(static_cast<float>(nPaddedPartitionLength)) + 2));
	ip_[0]=0; // signal the need to initialize
	w_.resize(nHalfPaddedPartitionLength);	// w_[0..nPaddedPartitionLength/2 - 1]
#endif

	// Initialise the Filter

	PlanningRigour pr;
#ifdef FFTW
	nFFTWPartitionLength_ = 2*(nPaddedPartitionLength/2+1);
#ifdef ARRAY
	coeffs_ = SampleBuffer(nPartitions, nFFTWPartitionLength_);
#else
	coeffs_ = SampleBuffer(nPartitions, ChannelBuffer(nFFTWPartitionLength_));
#endif
	// PATIENT will disable multithreading, if it's not faster
	if(nPlanningRigour > 1)
		fftwf_plan_with_nthreads(2);

	plan_ =  fftwf_plan_dft_r2c_1d(nPaddedPartitionLength,
		c_ptr(coeffs_), reinterpret_cast<fftwf_complex*>(c_ptr(coeffs_)),
		pr.Flag[nPlanningRigour]);
	reverse_plan_ =  fftwf_plan_dft_c2r_1d(nPaddedPartitionLength, 
		reinterpret_cast<fftwf_complex*>(c_ptr(coeffs_)), c_ptr(coeffs_),
		pr.Flag[nPlanningRigour]);
#else
#ifdef ARRAY
	coeffs_ = SampleBuffer(nPartitions, nPaddedPartitionLength);
#else
	coeffs_ = SampleBuffer(nPartitions, ChannelBuffer(nPaddedPartitionLength));
#endif
#endif

#ifdef LIBSNDFILE
	std::vector<float> item(sf_FilterFormat_.channels);
#else
	assert(wfexFilterFormat_.Format.wBitsPerSample >= wValidBitsPerSample);
	const DWORD dwSizeToRead = wfexFilterFormat_.Format.wBitsPerSample / 8;  // container size, in bytes

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
	while (nFrame < nFilterLength_)
	{
#ifdef LIBSNDFILE
		if (1 == pFilterWave.readf_float(&item[0], 1))		// 1 = 1 frame = nChannel items
		{
			// Got a frame / block (ie, the items / samples for each channel)
			// Pick the sample corresponding to the selected channel
			coeffs_[nPartition][nFrame % nHalfPartitionLength_] = item[nFilterChannel];
#if defined(DEBUG) | defined(_DEBUG)
			if (item[nFilterChannel] > maxSample)
				maxSample = item[nFilterChannel];
			if (item[nFilterChannel] < minSample)
				minSample = item[nFilterChannel];
#endif
		}
		else
		{
			throw filterException("Failed to read a frame", szFilterFileName);
		};
#else
		for(WORD nChannel = 0; nChannel <= nFilterChannel; ++nChannel)
		{
			hr = pFilterWave->Read(&bSample[nChannel], dwSizeToRead, &dwSizeRead);

			if (FAILED(hr))
			{
				throw filterException("Failed to read a block", szFilterFileName);
			}

			if (dwSizeRead != dwSizeToRead) // file corrupt, non-existent, etc
			{
				throw filterException("Failed to read a complete block", szFilterFileName);
			}
		}
		// Now convert bSample to the float sample
		switch (wFormatTag)
		{
		case WAVE_FORMAT_PCM:
			switch (wfexFilterFormat_.Format.wBitsPerSample)	// container size
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
						throw filterException("Bit depth for 24-bit container must be 16, 20 or 24", szFilterFileName);
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
						throw filterException("Bit depth for 32-bit container must be 16, 20, 24 or 32", szFilterFileName);
					}
					sample = static_cast<float>(i);
				}
				break;
			default:
				throw filterException("Unsupported PCM sample size", szFilterFileName); // Unsupported it sample size
			}
			break;

		case WAVE_FORMAT_IEEE_FLOAT:
			switch (wfexFilterFormat_.Format.wBitsPerSample)
			{
			case 16:
				throw filterException("16-bit IEEE float sample size not implemented", szFilterFileName);
			case 24:
				throw filterException("24-bit IEEE float sample size not implemented", szFilterFileName);
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
				throw filterException("Invalid IEEE float sample size", szFilterFileName); // Unsupported it sample size
			}
			break;

		default:
			throw filterException("Only PCM and IEEE Float file formats supported", szFilterFileName););	// Filter file format is not supported
		}

		coeffs_[nPartition][nFrame % nHalfPartitionLength_] = sample;

#if defined(DEBUG) | defined(_DEBUG)
		if (sample > maxSample)
			maxSample = sample;
		if (sample < minSample)
			minSample = sample;
#endif

		// skip the rest of the block
		for(;nChannel < wfexFilterFormat_.Format.nChannels ; ++ nChannel)
		{
			hr = pFilterWave->Read(&bSample[0], dwSizeToRead, &dwSizeRead);

			if (FAILED(hr))
			{
				throw filterException("Failed to read a block", szFilterFileName);
			}

			if (dwSizeRead != dwSizeToRead) // file corrupt, non-existent, etc
			{
				throw filterException("Failed to read a complete block", szFilterFileName);
			}
		}

#endif

		++nFrame;
		if (nFrame % nHalfPartitionLength_ == 0)
		{
			// Pad partition
			for (ChannelBuffer::size_type nPadding = nHalfPartitionLength_; nPadding < nPaddedPartitionLength; ++nPadding)
			{
				coeffs_[nPartition][nPadding] = 0;
			}

			// Take the DFT
#ifdef FFTW
			fftwf_execute_dft_r2c(plan_, 
				c_ptr(coeffs_, nPartition), 
				reinterpret_cast<fftwf_complex*>(c_ptr(coeffs_, nPartition)));
#elif defined(OOURA_SIMPLE)
			rdft(nPaddedPartitionLength, OouraRForward, c_ptr(coeffs_, nPartition));
#elif defined(OOURA)
			rdft(nPaddedPartitionLength, OouraRForward, c_ptr(coeffs_, nPartition), &ip_[0], &w_[0]);
#else
#error "No FFT package defined"
#endif

			// Scale here, so that we don't need to do so when convolving
#ifdef FFTW
			coeffs_[nPartition] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#elif defined(OOURA) || defined(SIMPLE_OOURA)
			coeffs_[nPartition] *= static_cast<float>(2.0L / nPaddedPartitionLength);
#else
#error "No FFT package defined"
#endif
			++nPartition;
		}
	} // while

	// Pad the current partition (if necessary)
	if (nFrame % nHalfPartitionLength_ != 0)
	{
		for (ChannelBuffer::size_type nSample = nFrame % nHalfPartitionLength_; nSample < nPaddedPartitionLength; ++nSample)
		{
			coeffs_[nPartition][nSample] = 0;
		}

		// Take the DFT
#ifdef FFTW
		fftwf_execute_dft_r2c(plan_, c_ptr(coeffs_, nPartition), reinterpret_cast<fftwf_complex*>(c_ptr(coeffs_, nPartition)));
#elif defined(OOURA_SIMPLE)
		rdft(nPaddedPartitionLength, OouraRForward, c_ptr(coeffs_, nPartition));
#elif defined(OOURA)
		rdft(nPaddedPartitionLength, OouraRForward, c_ptr(coeffs_, nPartition), &ip_[0], &w_[0]);
#else
#error "No FFT package defined"
#endif

		// Scale here, so that we don't need to do so when convolving
#ifdef FFTW
		coeffs_[nPartition] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#elif defined(OOURA) || defined(SIMPLE_OOURA)
		coeffs_[nPartition] *= static_cast<float>(1.0L / nPaddedPartitionLength);
#else
#error "No FFT package defined"
#endif
		++nPartition;
	}

	// Zero any further partitions
	for (;nPartition < nPartitions; ++nPartition)
	{
		coeffs_[nPartition] = 0;
	}

	// The padded lengths become the actual lengths that we are going to work with
	nPartitionLength_ = nPaddedPartitionLength;
	nHalfPartitionLength_ = nHalfPaddedPartitionLength;
	nFilterLength_ = nPartitions * nPartitionLength_;


#if defined(DEBUG) | defined(_DEBUG)

	cdebug << "FFT Filter: ";
	DumpSampleBuffer(coeffs_);
	cdebug << std::endl;


#ifdef LIBSNDFILE
	cdebug << waveFormatDescription(sf_FilterFormat(), "FFT Filter: ") << std::endl;
#else
	cdebug << waveFormatDescription(&wfexFilterFormat_, nFilterLength_, "FFT Filter:") << std::endl;
#endif

	cdebug << "minSample " << minSample << ", maxSample " << maxSample << std::endl;
#endif

}
