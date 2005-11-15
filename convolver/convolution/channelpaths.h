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
#pragma once

#include "convolution\config.h"
#include "convolution\filter.h"
#include <string>
#include <fstream>
#include <vector>


class ChannelPaths
{
public:

	class ChannelPath
	{
	public:
		class ScaledChannel
		{
		public:
			int nChannel;
			float fScale;

			ScaledChannel(int& nChannel, float& fScale) : nChannel(nChannel), fScale(fScale)
			{
#if defined(DEBUG) | defined(_DEBUG)
				DEBUGGING(3, cdebug << "ChannelPath::ChannelPath::ScaledChannel" << std::endl;);
#endif
			};

#if defined(DEBUG) | defined(_DEBUG)
			void Dump();
#endif
		};

		std::vector<ScaledChannel> inChannel;
		Filter filter;
		std::vector<ScaledChannel> outChannel;

		ChannelPath(TCHAR szConfigFileName[MAX_PATH], const int& nPartitions,
			std::vector<ScaledChannel>& inChannel, std::vector<ScaledChannel>& outChannel, const DWORD& nSampleRate) :
		filter(szConfigFileName, nPartitions, nSampleRate), inChannel(inChannel), outChannel(outChannel)
		{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ChannelPath::ChannelPath" << std::endl;);
#endif
		};

#if defined(DEBUG) | defined(_DEBUG)
		void Dump();
#endif
	};

	int	nInputChannels;			// number of input channels
	int nOutputChannels;		// number of output channels
	DWORD nSampleRate;			// 44100, 48000, etc
	DWORD dwChannelMask;		// http://www.microsoft.com/whdc/device/audio/multichaud.mspx
	int nPaths;					// number of Paths
	int	nPartitions;
	int	nPartitionLength;		// in blocks (a block contains the samples for each channel)
	int	nHalfPartitionLength;	// in blocks
	int	nFilterLength;			// nFilterLength = nPartitions * nPartitionLength
#ifdef FFTW
	int	nFFTWPartitionLength;	// 2*(nPartitionLength / 2 + 1)
#endif

	std::vector<ChannelPath> Paths;

	ChannelPaths(TCHAR szConfigFileName[MAX_PATH], const int& nPartitions);

	~ChannelPaths()
	{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ChannelPaths::~ChannelPaths " << std::endl;);
#endif
		// Delete the plans here to avoid problems with vector and copy constructors
		for(int i=0; i < Paths.size(); ++i)
		{
			fftwf_destroy_plan(Paths[i].filter.plan);
			Paths[i].filter.plan = NULL;
			fftwf_destroy_plan(Paths[i].filter.reverse_plan);
			Paths[i].filter.reverse_plan = NULL;
		}
	}

	const std::string DisplayChannelPaths();

#if defined(DEBUG) | defined(_DEBUG)
	void Dump();
#endif

private:
	ChannelPaths(const ChannelPaths&); // No copy ctor
	ChannelPaths &operator =(const ChannelPaths&); // No copy assignment
};
