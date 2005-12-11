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
#include <boost\ptr_container\ptr_vector.hpp>

class configFile
{
public:
	configFile(TCHAR szConfigFileName[MAX_PATH]) : opened(false)
	{
		config.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit | std::ios::badbit);
		USES_CONVERSION;
		config.open(T2A(szConfigFileName));

		if (config == NULL)
		{
			throw configException("Failed to open config file");
		}

		opened=true;
	}

	std::ifstream& operator()()
	{
		return config;
	}

	~configFile()
	{
		if(opened)
			config.close();
	}

private:
	std::ifstream config;
	bool opened;

	configFile();									// No construction
	configFile(const configFile&);				// No copy ctor
	const configFile &operator =(const configFile&); // No copy assignment
};

class ChannelPaths
{
public:

	class ChannelPath
	{
	public:
		class ScaledChannel
		{
		public:
			WORD nChannel;
			float fScale;

			ScaledChannel(const WORD& nChannel, const float& fScale) : nChannel(nChannel), fScale(fScale)
			{
#if defined(DEBUG) | defined(_DEBUG)
				DEBUGGING(3, cdebug << "ChannelPath::ChannelPath::ScaledChannel" << std::endl;);
#endif
			};

#if defined(DEBUG) | defined(_DEBUG)
			const void Dump() const;
#endif
		private:
			ScaledChannel();								// Prevent default construction
			// Default copying OK
		};

		const std::vector<ScaledChannel> inChannel;
		const Filter filter;
		const std::vector<ScaledChannel> outChannel;

		ChannelPath(TCHAR szConfigFileName[MAX_PATH], const WORD& nPartitions,
			const std::vector<ScaledChannel>& inChannel, const std::vector<ScaledChannel>& outChannel,
			const WORD& nFilterChannel, const DWORD& nSampleRate, const unsigned int& nPlanningRigour) :
		filter(szConfigFileName, nPartitions, nFilterChannel, nSampleRate, nPlanningRigour),
			inChannel(inChannel), outChannel(outChannel)
		{
#if defined(DEBUG) | defined(_DEBUG)
			DEBUGGING(3, cdebug << "ChannelPath::ChannelPath" << std::endl;);
#endif
		};

#if defined(DEBUG) | defined(_DEBUG)
		const void Dump() const;
#endif
	};

	configFile	config;
	WORD		nInputChannels;		// number of input channels
	WORD		nOutputChannels;	// number of output channels
	DWORD		nSamplesPerSec;		// 44100, 48000, etc
	DWORD		dwChannelMask;		// http://www.microsoft.com/whdc/device/audio/multichaud.mspx
	WORD		nPaths;				// number of Paths
	WORD		nPartitions;
	DWORD		nPartitionLength;		// in blocks (a block contains the samples for each channel)
	DWORD		nHalfPartitionLength;	// in blocks
	DWORD		nFilterLength;			// nFilterLength = nPartitions * nPartitionLength
#ifdef FFTW
	DWORD		nFFTWPartitionLength;	// 2*(nPartitionLength / 2 + 1)
#endif

	// TODO: this is not right as copying of filter class is problematic as
	// FFTW plans cannot just be copied without leading to memory leaks or
	// multiple deletions
	boost::ptr_vector<ChannelPath> Paths;

	ChannelPaths(TCHAR szConfigFileName[MAX_PATH], const WORD& nPartitions, const unsigned int& nPlanningRigour);

	const std::string DisplayChannelPaths();

#if defined(DEBUG) | defined(_DEBUG)
	const void Dump() const;
#endif

	virtual ~ChannelPaths() 
	{
#if defined(DEBUG) | defined(_DEBUG)
		DEBUGGING(3, cdebug << "ChannelPaths::~ChannelPaths " << std::endl;);
#endif
// TODO: check that this is enough (ptr_vector should do the work)
	}

private:
	ChannelPaths();									// No construction
	ChannelPaths(const ChannelPaths&);				// No copy ctor
	const ChannelPaths &operator =(const ChannelPaths&); // No copy assignment
};


