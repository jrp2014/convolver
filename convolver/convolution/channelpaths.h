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
#include <mediaerr.h>

class configFile
{
public:
	configFile(const TCHAR szConfigFileName[MAX_PATH]) : opened(false)
	{
		config.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit);
		//USES_CONVERSION;

		try
		{
			config.open(CT2A(szConfigFileName));
		}
		catch (...)
		{
			throw convolutionException("Failed to open file:" + std::string(CT2CA(szConfigFileName)));
		}

		if (config == NULL) // Should never happen, as the exceptions are set
		{
			throw convolutionException("Unexpectedly failed to open file:" + std::string(CT2CA(szConfigFileName)));
		}

		opened=true;
	}

	std::basic_ifstream<TCHAR>& operator()()
	{
		return config;
	}

	void close()
	{
		if(opened)
		{
			try
			{
				config.close();
				opened = false;
			}
			catch (...)
			{
				throw convolutionException("Failed to close config file");
			}
		}
	}

	virtual ~configFile()
	{
		try
		{
			if(opened)
				config.close();
		}
		catch (...)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Unexpected exception closing config file" << std::endl;
#endif
		}
	}

private:
	std::basic_ifstream<TCHAR> config;
	bool opened;

	configFile();										// No default ctor
	configFile(const configFile&);						// No copy ctor
	const configFile &operator =(const configFile&);	// No copy assignment
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

			ScaledChannel(const WORD nChannel, const float fScale) : nChannel(nChannel), fScale(fScale)
			{
#if defined(DEBUG) | defined(_DEBUG)
				DEBUGGING(3, cdebug << "ChannelPath::ChannelPath::ScaledChannel" << std::endl;);
#endif
			};

#if defined(DEBUG) | defined(_DEBUG)
			void Dump() const;
#endif
		private:
			ScaledChannel();								// Prevent default construction
			// Default copying OK
		};

		typedef std::vector<ScaledChannel>::size_type size_type;

		// A ChannelPath associates a filter with a set of scaled input and output channels
		const std::vector<ScaledChannel> inChannel;
		const Filter filter;
		const std::vector<ScaledChannel> outChannel;

		ChannelPath(const TCHAR szChannelPathsFileName[MAX_PATH], const WORD& nPartitions,
			const std::vector<ScaledChannel>& inChannel, const std::vector<ScaledChannel>& outChannel,
			const WORD& nFilterChannel, const DWORD& nSampleRate, const unsigned int& nPlanningRigour) :
		filter(szChannelPathsFileName, nPartitions, nFilterChannel, nSampleRate, nPlanningRigour),
			inChannel(inChannel), outChannel(outChannel)
		{
#if defined(DEBUG) | defined(_DEBUG)
			DEBUGGING(3, cdebug << "ChannelPath::ChannelPath" << std::endl;);
#endif
		};

#if defined(DEBUG) | defined(_DEBUG)
	void Dump() const;
#endif
	};

	typedef boost::ptr_vector<ChannelPath>::size_type size_type;

	const boost::ptr_vector<ChannelPath>& Paths() const
	{
		assert(Paths_.size() > 0);
		assert(nPaths_ == Paths_.size());
		return Paths_;
	}

	const WORD nPartitions;

	DWORD nInputChannels() const	// number of input channels; must be a DWORD, otherwise not read correctly by >> from TCHAR config file
	{
		return nInputChannels_;
	}

	DWORD nOutputChannels()	const	// number of output channels
	{
		return nOutputChannels_;
	}

	DWORD nSamplesPerSec() const	// 44100, 48000, etc
	{
		return nSamplesPerSec_;
	}

	DWORD	dwChannelMask()	const		// http://www.microsoft.com/whdc/device/audio/multichaud.mspx
	{
		return dwChannelMask_;
	}

	unsigned int nPaths() const				// number of Paths
	{
		assert(nPaths_ == Paths_.size());
		return nPaths_;
	}

	DWORD nPartitionLength() const		// in frames (a frame/block contains the samples for each channel)
	{
		assert(nPartitionLength_ == 2 * nHalfPartitionLength_);
		return nPartitionLength_;
	}

	DWORD nHalfPartitionLength() const	// in frames
	{
		assert(nPartitionLength_ == 2 * nHalfPartitionLength_);
		return nHalfPartitionLength_;
	}

	DWORD nFilterLength() const			// nFilterLength = nPartitions * nPartitionLength
	{
		assert(nFilterLength_ == nPartitions * nPartitionLength());
		return nFilterLength_;
	}

#ifdef FFTW
	DWORD nFFTWPartitionLength() const
	{
		assert(nFFTWPartitionLength_ == 2*(nPartitionLength() / 2 + 1));
		return nFFTWPartitionLength_;
	}
#endif

	ChannelPaths(const TCHAR szChannelPathsFileName[MAX_PATH], const WORD& nPartitions, const unsigned int& nPlanningRigour);

	const std::string DisplayChannelPaths() const;

#if defined(DEBUG) | defined(_DEBUG)
	void Dump() const;
#endif

	virtual ~ChannelPaths() 
	{
#if defined(DEBUG) | defined(_DEBUG)
		DEBUGGING(3, cdebug << "ChannelPaths::~ChannelPaths " << std::endl;);
#endif
		// TODO: check that this is enough (ptr_vector should do the work)
	}

private:
	configFile	config_;

	// FFTW plans cannot just be copied without leading to memory leaks or
	// multiple deletions, so use pointers
	boost::ptr_vector<ChannelPath> Paths_;

	DWORD	nInputChannels_;		// number of input channels; must be a DWORD, otherwise not read correctly by >> from TCHAR config file
	DWORD	nOutputChannels_;		// number of output channels
	DWORD	nSamplesPerSec_;		// 44100, 48000, etc
	DWORD	dwChannelMask_;			// http://www.microsoft.com/whdc/device/audio/multichaud.mspx
	unsigned int	nPaths_;				// number of Paths
	DWORD	nPartitionLength_;		// in frames (a frame/block contains the samples for each channel)
	DWORD	nHalfPartitionLength_;	// in frames
	DWORD	nFilterLength_;			// nFilterLength = nPartitions * nPartitionLength
#ifdef FFTW
	DWORD		nFFTWPartitionLength_;	// 2*(nPartitionLength / 2 + 1)
#endif

	ChannelPaths();											// No construction
	ChannelPaths(const ChannelPaths&);						// No copy ctor
	const ChannelPaths &operator =(const ChannelPaths&);	// No copy assignment
};
