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

#include ".\filter.h"

Filter::Filter(TCHAR szFilterFileName[MAX_PATH], const DWORD nPartitions) : 
nPartitions (nPartitions)
{
#ifndef LIBSNDFILE
	HRESULT hr = S_OK;
#endif

	if (nPartitions == 0)
	{
		throw (E_INVALIDARG);
	}

	// Load the wave file
#ifdef LIBSNDFILE
	::ZeroMemory(&sf_FilterFormat, sizeof(SF_INFO));
	CWaveFileHandle pFilterWave(szFilterFileName, SFM_READ, &sf_FilterFormat); // Throws, if file invalid

	// Save number of channels for easy access
	nChannels = sf_FilterFormat.channels;

	nFilterLength = sf_FilterFormat.frames;

#else
	CWaveFileHandle pFilterWave;
	hr = pFilterWave->Open( szFilterFileName, NULL, WAVEFILE_READ );
	if( FAILED(hr) )
	{
		throw (hr);
	}

	// Save filter characteristic, for access by the properties page, etc
	::ZeroMemory(&wfexFilterFormat, sizeof(wfexFilterFormat));
	wfexFilterFormat.Format = *pFilterWave->GetFormat();

	// Save number of channels for easy access
	nChannels = wfexFilterFormat.Format.nChannels;

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
				throw(E_INVALIDARG);
			}
		}
	}
#endif

	// Check that the filter is not too big ...
	if ( nFilterLength > MAX_FILTER_SIZE )
	{
		throw(E_ABORT);
	}

	// Setup the filter
	// A partition will contain half real data, and half zero padding.  Taking the DFT will, of course, overwrite that padding

	nHalfPartitionLength = (nFilterLength + nPartitions - 1) / nPartitions;
	nPartitionLength = nHalfPartitionLength * 2;

	// .. or filter too small
	if ( nHalfPartitionLength == 0 )
	{
		throw(E_INVALIDARG);
	}

	// Now make sure that the partition length is a power of two long for the Ooura FFT package. (Will 0 pad if necessary.)
	int nPaddedPartitionLength = 4;	// Minimum length 4, to allow SIMD optimization
	while (nPaddedPartitionLength < nPartitionLength)
	{
		nPaddedPartitionLength *= 2;
	}
	DWORD nHalfPaddedPartitionLength = nPaddedPartitionLength / 2;

#ifndef OOURA_SIMPLE
	// Initialize the Oooura workspace;
	ip.reserve(static_cast<int>(sqrt(static_cast<float>(nPaddedPartitionLength)) + 2));
	ip[0]=0; // signal the need to initialize
	w.reserve(nHalfPaddedPartitionLength);	// w[0..nPaddedPartitionLength/2 - 1]
#endif

	// Initialise the Filter
	buffer = PartitionedBuffer(nPartitions, SampleBuffer(nChannels, ChannelBuffer(nPaddedPartitionLength)));

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
	DWORD nPartition = 0;
	DWORD nBlock = 0;					// LibSndFile refers to blocks as frames
	while (nBlock < nFilterLength)
	{
#ifdef LIBSNDFILE

		if (1 == pFilterWave.readf_float(&item[0], 1))		// 1 = 1 frame = nChannel items
		{
			// Got a frame / block ... the items / samples for each channel
			for (int nChannel=0; nChannel < nChannels; ++nChannel)
			{
				buffer[nPartition][nChannel][nBlock % nHalfPartitionLength] = item[nChannel];
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
			throw std::length_error("Failed to read a frame");
		};

#else
		for (int nChannel=0; nChannel < nChannels; ++nChannel)
		{
			hr = pFilterWave->Read(&bSample[0], dwSizeToRead, &dwSizeRead);
			if (FAILED(hr))
			{
				throw (E_ABORT);
			}

			if (dwSizeRead != dwSizeToRead) // file corrupt, non-existent, etc
			{
				throw (E_FAIL);
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
							throw(E_INVALIDARG);
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
							throw(E_INVALIDARG);
						}
						sample = static_cast<float>(i);
					}
					break;
				default:
					throw(E_NOTIMPL); // Unsupported it sample size
				}
				break;

			case WAVE_FORMAT_IEEE_FLOAT:
				switch (wfexFilterFormat.Format.wBitsPerSample)
				{
				case 24:
					throw(E_NOTIMPL);
				case 32:
					{
						assert(dwSizeToRead == sizeof(float));
						sample = *reinterpret_cast<float*>(&bSample[0]);
					}  // case
					break;
				default:
					throw(E_NOTIMPL); // Unsupported it sample size
				}
				break;

			default:
				throw(E_NOTIMPL);	// Filter file format is not supported
			}

			buffer[nPartition][nChannel][nBlock % nHalfPartitionLength] = sample;

#if defined(DEBUG) | defined(_DEBUG)
			if (sample > maxSample)
				maxSample = sample;
			if (sample < minSample)
				minSample = sample;
#endif

		} // for nChannel
#endif

		++nBlock;
		if (nBlock % nHalfPartitionLength == 0)
		{
			// Pad partition
			for (int nChannel=0; nChannel < nChannels; ++nChannel)
			{
				for (int nPadding = nHalfPartitionLength; nPadding < nPaddedPartitionLength; ++nPadding)
				{
					buffer[nPartition][nChannel][nPadding] = 0;
				}

				// Take the DFT
#ifdef OOURA_SIMPLE
				rdft(nPaddedPartitionLength, OouraRForward, &buffer[nPartition][nChannel][0]);
#else
				rdft(nPaddedPartitionLength, OouraRForward, &buffer[nPartition][nChannel][0], &ip[0], &w[0]);
#endif

				// Scale here, so it is not necessary to do it when taking the inverse convolution
				//for (int nSample = 0; nSample < nPaddedPartitionLength; ++nSample)
				//{
				//	buffer[nPartition][nChannel][nSample] *= static_cast<float>(2.0L / nPaddedPartitionLength);
				//}
				buffer[nPartition][nChannel] *= static_cast<float>(2.0L / nPaddedPartitionLength);
			}
			++nPartition;
		}
	} // while

	// Pad the current partition (if necessary)
	if (nBlock % nHalfPartitionLength != 0)
	{
		for (int nChannel=0; nChannel < nChannels; ++nChannel)
		{
			for (int nSample = nBlock % nHalfPartitionLength; nSample < nPaddedPartitionLength; ++nSample)
			{
				buffer[nPartition][nChannel][nSample] = 0;
			}

			// Take the DFT
#ifdef OOURA_SIMPLE
			rdft(nPaddedPartitionLength, OouraRForward, &buffer[nPartition][nChannel][0]);
#else
			rdft(nPaddedPartitionLength, OouraRForward, &buffer[nPartition][nChannel][0], &ip[0], &w[0]);
#endif

			// Scale here, so it is not necessary to do it during convolution
			//for (int nSample = 0; nSample < nPaddedPartitionLength; ++nSample)
			//{
			//	buffer[nPartition][nChannel][nSample] *= static_cast<float>(2.0L / nPaddedPartitionLength);
			//}
			buffer[nPartition][nChannel] *= static_cast<float>(2.0L / nPaddedPartitionLength);
		}
		++nPartition;
	}

	// Zero any further partitions (valarray should do this, but doesn't)
	for (;nPartition < nPartitions; ++nPartition)
	{
		for (int nChannel=0; nChannel < nChannels; ++nChannel)
		{
			buffer[nPartition][nChannel] = 0;
		}
	}

	// The padded lengths become the actual lengths that we are going to work with
	nPartitionLength = nPaddedPartitionLength;
	nHalfPartitionLength = nHalfPaddedPartitionLength;
	nFilterLength = nPartitions * nPartitionLength;

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "FFT Filter: "; DumpPartitionedBuffer(buffer);

#ifdef LIBSNDFILE
	cdebug << waveFormatDescription(sf_FilterFormat, "FFT Filter: ") << std::endl;
#else
	cdebug << waveFormatDescription(&wfexFilterFormat, nFilterLength, "FFT Filter:") << std::endl;
#endif

	cdebug << "minSample " << minSample << ", maxSample " << maxSample << std::endl;
#endif
}
