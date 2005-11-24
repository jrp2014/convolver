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


#include "convolution\channelpaths.h"

ChannelPaths::ChannelPaths(TCHAR szConfigFileName[MAX_PATH], const int& nPartitions) :
nInputChannels(0),
nOutputChannels(0),
nSamplesPerSec(0),
dwChannelMask(0),
nPaths(0),
nPartitions(nPartitions),
nPartitionLength(0),
nHalfPartitionLength(0),
nFilterLength(0)
#ifdef FFTW
,nFFTWPartitionLength(2)
#endif
{
	USES_CONVERSION;
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ChannelPaths::ChannelPaths " << T2A(szConfigFileName) << " " << nPartitions << " " << std::endl;);
#endif

	if(0 == *szConfigFileName)
	{
		throw configException("No config file specified");
	}

	std::ifstream config;
	bool got_path_spec = false;

	try
	{
		config.open(T2A(szConfigFileName));
		if (config == NULL)
		{
			throw configException("Failed to open config file");
		}

		config.exceptions(std::ios::badbit | std::ios::failbit | std::ios::eofbit | std::ios::badbit);

		config >> nSamplesPerSec;
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << nSamplesPerSec << " sample rate" << std::endl;
#endif

		config >> nInputChannels;
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << nInputChannels << " input channels" << std::endl;
#endif

		config >> nOutputChannels;
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << nOutputChannels << " output channels" << std::endl;
#endif
		config.unsetf(std::ios_base::dec);
		config.setf(std::ios_base::hex);
		config >> dwChannelMask;		// Read in hex
		config.unsetf(std::ios_base::hex);
		config.setf(std::ios_base::dec);
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << std::hex << dwChannelMask << std::dec << " channel mask" << std::endl;
#endif

		config.get();  // consume newline

		char szFilterFilename[MAX_PATH];
		while(!config.eof())
		{
			config.getline(szFilterFilename, MAX_PATH);
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Reading specification for " << szFilterFilename << std::endl;
#endif

			got_path_spec = false;

			std::vector<ChannelPath::ScaledChannel> inChannel;
			while(true)
			{
				// Require at least one input channel
				float scale;
				config >> scale;

				if(scale < 0)
					throw configException("Negative input channel number");

				// The integer part designates the input channel
				int channel = floor(scale);   

				if(channel > nInputChannels - 1)
					throw configException("Input channel number greater than the specified number of input channels");

				// The fractional part is the scaling factor to be applied
				scale -= channel;

				// 0 implies no scaling to be applied (ie, scale = 1)
				if(scale == 0)
					scale = 1.0f;

				inChannel.push_back(ChannelPath::ScaledChannel(channel, scale));

				int nextchar = config.get();
				if (nextchar == '\n' || nextchar == EOF)
					break;
			}

			// Get the output channels for this filter
			std::vector<ChannelPath::ScaledChannel> outChannel;
			while(true)
			{
				float scale;
				config >> scale;

				if(scale < 0)
					throw configException("Negative output channel number");

				// The integer part designates the output channel
				int channel = floor(scale);

				if(channel > nOutputChannels - 1)
					throw configException("Output channel number greater than the specified number of output channels");

				// The fractional part is the scaling factor to be applied
				scale -= channel;
				// 0 implies no scaling to be applied (ie, scale = 1)
				if(scale == 0)
					scale = 1.0f;

				outChannel.push_back(ChannelPath::ScaledChannel(channel, scale));

				int nextchar = config.get();
				if (nextchar == '\n' || nextchar == EOF)
					break;
			}

			Paths.push_back(ChannelPath(A2T(szFilterFilename), nPartitions, inChannel, outChannel, nSamplesPerSec));
			++nPaths;

			got_path_spec = true;
		}
	}
	catch(const std::ios_base::failure& error)
	{
		if(config.eof())
		{

			if(!got_path_spec)
			{
				throw configException("Premature end of configuration file.  Missing final blank line?");
			}

			// Verify
			if (nPaths > 0)
			{
				nPartitionLength = Paths[0].filter.nPartitionLength;			// in frames (a frame contains the interleaved samples for each channel)
				nHalfPartitionLength = Paths[0].filter.nHalfPartitionLength;	// in frames
				nFilterLength = Paths[0].filter.nFilterLength;					// nFilterLength = nPartitions * nPartitionLength
				nSamplesPerSec = Paths[0].filter.nSamplesPerSec;
#ifdef FFTW
				nFFTWPartitionLength = 2 * (nPartitionLength / 2 + 1);			// Needs an extra element
#endif
			}
			else
			{
				throw configException("Must specify at least one filter");
			}

			for(int i = 0; i < nPaths; ++i)
			{
				if(Paths[i].filter.nChannels != 1)
				{
					throw configException("Filters must have exactly one channel");
				}

				if(Paths[i].filter.nFilterLength != nFilterLength)
				{
					throw configException("Filters must all be of the same length");
				}

				if(Paths[i].filter.nSamplesPerSec != nSamplesPerSec)
				{	// TODO: This is not necessary, as it is caught in CWaveFile
					throw configException("Filters must all have the same sample rate");
				}
			}
#if defined(DEBUG) | defined(_DEBUG)
			Dump();
#endif
		}
		else if (config.fail())
		{
			throw configException("Config file syntax is incorrect");
		}
		else if (config.bad())
		{
			throw configException("Fatal error reading config file");
		}
		else
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "I/O exception: " << error.what() << std::endl;
#endif
			throw configException(error.what());
		}
		// else fall through
	}
	catch(const convolutionException& ex)	// self-generated exception
	{
		throw configException(ex.what());
	}
	catch(const std::exception& error)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Standard exception: " << error.what() << std::endl;
#endif
		throw configException(error.what());
	}
	catch (...)
	{
		throw configException("Unexpected exception");
	}
}

#if defined(DEBUG) | defined(_DEBUG)
void ChannelPaths::Dump()
{
	cdebug << "ChannelPaths: " << " nInputChannels:" << nInputChannels << " nOutputChannels:" << nOutputChannels <<
		" nPartitions:" << nPartitions << " nPartitionLength:" << nPartitionLength <<
		" nHalfPartitionLength:" << nHalfPartitionLength << " nFilterLength:" << nFilterLength << std::endl;
	for(int i = 0; i < nPaths; ++i) 
		Paths[i].Dump();
}

void ChannelPaths::ChannelPath::Dump()
{
	cdebug << "inChannel:";
	for(int i=0; i<inChannel.size(); ++i)
	{
		cdebug << " "; inChannel[i].Dump();
	}

	cdebug << std::endl << "outChannel:";
	for(int i=0; i<outChannel.size(); ++i)
	{
		cdebug << " "; outChannel[i].Dump();
	}
	cdebug << std::endl;
}

void ChannelPaths::ChannelPath::ScaledChannel::Dump()
{
	cdebug << nChannel << "/" << fScale;
}

#endif

const std::string ChannelPaths::DisplayChannelPaths()
{
	std::ostringstream result;

	if (nPaths == 0)
	{
		result << "No paths defined";
	}
	else
	{
		if (nPaths == 1)
		{
			result << "1 Path (";
		}
		else
		{
			result << nPaths << " Paths (";
		}

		if (nInputChannels == 1)
		{
			result << "Mono to ";
		}
		else if (nInputChannels == 2)
		{
			result << "Stereo to ";
		}
		else
			result << nInputChannels << " channels to ";

		result << channelDescription(WAVE_FORMAT_EXTENSIBLE, dwChannelMask, nOutputChannels) << ") ";

		result
#ifdef LIBSNDFILE
			<< Paths[0].filter.sf_FilterFormat.samplerate/1000.0f << "kHz " 
#else
			<< Paths[0].filter.wfexFilterFormat.Format.nSamplesPerSec/1000.0f << "kHz " 
#endif
			<< Paths[0].filter.nFilterLength << " taps";
	}
	return result.str();
}
