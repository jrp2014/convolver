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

ChannelPaths::ChannelPaths(const TCHAR szChannelPathsFileName[MAX_PATH], const WORD& nPartitions, const unsigned int& nPlanningRigour) :
nInputChannels_(0),
nOutputChannels_(0),
nSamplesPerSec_(0),
dwChannelMask_(0),
nPaths_(0),
nPartitions(nPartitions),
nPartitionLength_(0),
nHalfPartitionLength_(0),
nFilterLength_(0),
#ifdef FFTW
nFFTWPartitionLength_(2),
#endif
config_(szChannelPathsFileName)
{
	//USES_CONVERSION;

#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ChannelPaths::ChannelPaths " << CT2A(szChannelPathsFileName) << " " << nPartitions << " " << std::endl;);
#endif

	if(0 == *szChannelPathsFileName)
	{
		throw channelPathsException("Filter path filename is null", szChannelPathsFileName);
	}

	try
	{
#ifdef LIBSNDFILE
		SF_INFO sf_info; ::ZeroMemory(&sf_info, sizeof(sf_info));
		CWaveFileHandle wav(szChannelPathsFileName, SFM_READ, &sf_info, 44100);

		nInputChannels_ = sf_info.channels;
		nOutputChannels_ = sf_info.channels;
		nSamplesPerSec_ = sf_info.samplerate;
#else
		WAVEFORMATEX wfex; ::ZeroMemory(&wfex, sizeof(WAVEFORMATEX));
		CWaveFileHandle wav;

		if( FAILED(pFilterWave->Open( szChannelPathsFileName, NULL, WAVEFILE_READ )) )
		{
			throw wavfileException("Failed to open WAV file", szFilterFileName, "test open failed");
		}

		nInputChannels_ = wfex.nChannels;
		nOutputChannels_ = wfex.nChannels;
		nSamplesPerSec_ = wfex.nSamplesPerSec_;
#endif

		for(WORD nChannel=0; nChannel < nInputChannels_; ++nChannel)
		{
			std::vector<ChannelPath::ScaledChannel> inChannel(1, ChannelPath::ScaledChannel(nChannel, 1.0f));
			std::vector<ChannelPath::ScaledChannel> outChannel(1, ChannelPath::ScaledChannel(nChannel, 1.0f));
			Paths_.push_back(new ChannelPath(szChannelPathsFileName, nPartitions, inChannel, outChannel, nChannel, 
				nSamplesPerSec_, nPlanningRigour));
			++nPaths_;
		}
	}
	catch(const wavfileException&)
	{
		// Failed to open as a wav file, so assume that we have a text config_ file

		bool got_path_spec = false;

		try
		{
			config_() >> nSamplesPerSec_;
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << nSamplesPerSec_ << " sample rate" << std::endl;
#endif

			config_() >> nInputChannels_;
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << nInputChannels_ << " input channels" << std::endl;
#endif

			config_() >> nOutputChannels_;
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << nOutputChannels_ << " output channels" << std::endl;
#endif
			config_().unsetf(std::ios_base::dec);
			config_().setf(std::ios_base::hex);
			config_() >> dwChannelMask_;		// Read in hex
			config_().unsetf(std::ios_base::hex);
			config_().setf(std::ios_base::dec);
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << std::hex << dwChannelMask_ << std::dec << " channel mask" << std::endl;
#endif

			config_().get();  // consume newline

			TCHAR szFilterFilename[MAX_PATH];
			while(!config_().eof())
			{
				config_().getline(szFilterFilename, MAX_PATH);
#if defined(DEBUG) | defined(_DEBUG)
				cdebug << "Reading specification for " << szFilterFilename << std::endl;
#endif
				got_path_spec = false;

				DWORD nFilterChannel=0;			// The channel to select from the filter file.
				config_() >> nFilterChannel;

				std::basic_ifstream<TCHAR>::int_type nextchar = config_().peek();
				if (nextchar != '\n')
				{
					throw channelPathsException("Missing filter channel specification?", szChannelPathsFileName);
				}

				std::vector<ChannelPath::ScaledChannel> inChannel;
				while(true) // Require at least one input channel
				{
					float scale=0; // The scaling factor to be applied
					config_() >> scale;

					float fchannel=0;
					// The modff function breaks down the floating-point value scale into fractional and integer parts,
					// each of which has the same sign as scale.
					// The signed fractional portion of scale is returned
					// The integer portion is stored as a floating-point value at fchannel
					scale = modff(scale, &fchannel);
					const WORD channel = static_cast<WORD>(abs((fchannel)));

					if(channel > nInputChannels_ - 1)
						throw channelPathsException("Input channel number greater than the specified number of input channels",
						szChannelPathsFileName);

					// 0 implies no scaling to be applied (ie, scale = 1)
					if(scale == 0)
						scale = fchannel < 0 ? -1.0f : 1.0f;

					inChannel.push_back(ChannelPath::ScaledChannel(channel, scale));

					nextchar = config_().get();
					if (nextchar == '\n')
						break;
					else
					{
						if (nextchar == std::char_traits<TCHAR>::eof())
							throw channelPathsException("Missing output channels specification", szChannelPathsFileName);
					}
				}

				// Get the output channels for this filter
				std::vector<ChannelPath::ScaledChannel> outChannel;
				while(true)
				{
					float scale=0; // The scaling factor to be applied
					config_() >> scale;

					float fchannel=0;
					scale = modff(scale, &fchannel);
					const WORD channel = static_cast<WORD>(abs(fchannel));

					if(channel > nOutputChannels_ - 1)
						throw channelPathsException("Output channel number greater than the specified number of output channels",
						szChannelPathsFileName);

					// 0 implies no scaling to be applied (ie, scale = 1)
					if(scale == 0)
						scale = fchannel < 0 ? -1.0f : 1.0f;

					outChannel.push_back(ChannelPath::ScaledChannel(channel, scale));

					nextchar = config_().get();
					if (nextchar == '\n' || nextchar == std::char_traits<TCHAR>::eof())
						break;
				}

				Paths_.push_back(new ChannelPath(szFilterFilename, nPartitions, inChannel, outChannel, nFilterChannel, 
					nSamplesPerSec_, nPlanningRigour));
				++nPaths_;

				got_path_spec = true;
			}
		}
		catch(const std::ios_base::failure& error)
		{
			if(config_().eof())
			{
				if(!got_path_spec)
				{
					throw channelPathsException("Premature end of filter path configuration file.  Missing final blank line?",
						szChannelPathsFileName);
				}
			}
			else if (config_().fail())
			{
				throw channelPathsException("Filter path file syntax is incorrect", szChannelPathsFileName);
			}
			else if (config_().bad())
			{
				throw channelPathsException("Fatal error opening/reading Filter path file", szChannelPathsFileName);
			}
			else
			{
#if defined(DEBUG) | defined(_DEBUG)
				cdebug << "I/O exception: " << error.what() << std::endl;
#endif
				throw channelPathsException(error.what(), szChannelPathsFileName);
			}
			// else fall through
		}
		catch(const convolutionException& ex)	// self-generated exception
		{
			throw convolutionException(ex.what());
		}
		catch(const std::exception& error)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Standard exception: " << error.what() << std::endl;
#endif
			throw channelPathsException(error.what(), szChannelPathsFileName);
		}
		catch (...)
		{
			throw channelPathsException("Unexpected exception", szChannelPathsFileName);
		}
	}
	catch(const std::exception& error)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Standard exception: " << error.what() << std::endl;
#endif
		throw channelPathsException(error.what(), szChannelPathsFileName);
	}
	catch (...)
	{
		throw channelPathsException("Unexpected exception", szChannelPathsFileName);
	}

	// Verify
	if (nPaths_ > 0)
	{
		nPartitionLength_ = Paths_[0].filter.nPartitionLength();			// in frames (a frame contains the interleaved samples for each channel)
		nHalfPartitionLength_ = Paths_[0].filter.nHalfPartitionLength();	// in frames
		nFilterLength_ = Paths_[0].filter.nFilterLength();				// nFilterLength_ = nPartitions_ * nPartitionLength_
		nSamplesPerSec_ = Paths_[0].filter.nSamplesPerSec;
#ifdef FFTW
		nFFTWPartitionLength_ = Paths_[0].filter.nFFTWPartitionLength();	// Needs an extra element
#endif
	}
	else
	{
		throw channelPathsException("Must specify at least one filter", szChannelPathsFileName);
	}

	for(WORD i = 0; i < nPaths_; ++i)
	{
		if(Paths_[i].filter.nFilterLength() != nFilterLength_)
		{
			throw channelPathsException("Filters must all be of the same length", szChannelPathsFileName);
		}

		if(Paths_[i].filter.nSamplesPerSec != nSamplesPerSec_)
		{
			throw channelPathsException("Filters must all have the same sample rate", szChannelPathsFileName);
		}
	}
#if defined(DEBUG) | defined(_DEBUG)
	Dump();
#endif
}

const std::string ChannelPaths::DisplayChannelPaths() const
{
	std::ostringstream result;

	if (nPaths() == 0)
	{
		result << "No paths defined";
	}
	else
	{
		if (nPaths() == 1)
		{
			result << "1 Path (";
		}
		else
		{
			result << nPaths() << " Paths (";
		}

		if (nInputChannels() == 1)
		{
			result << "Mono to ";
		}
		else if (nInputChannels() == 2)
		{
			result << "Stereo to ";
		}
		else
			result << nInputChannels() << " channels to ";

		result << channelDescription(WAVE_FORMAT_EXTENSIBLE, dwChannelMask(), nOutputChannels()) << ") ";

		result
			<< nSamplesPerSec()/1000.0f << "kHz, " 
			<< nFilterLength() << " taps, " 
			<< std::setprecision(2) << (static_cast<float>(nPartitionLength()) / static_cast<float>(nSamplesPerSec())) << "s lag";
	}
	return result.str();
}

#if defined(DEBUG) | defined(_DEBUG)
void ChannelPaths::Dump() const
{
	cdebug << "ChannelPaths: " << " nInputChannels_:" << nInputChannels_ << " nOutputChannels_:" << nOutputChannels_ <<
		" nPartitions:" << nPartitions << " nPartitionLength:" << nPartitionLength_ <<
		" nHalfPartitionLength:" << nHalfPartitionLength_ << " nFilterLength:" << nFilterLength_ << std::endl;
	for(int i = 0; i < nPaths_; ++i) 
		Paths_[i].Dump();
}

void ChannelPaths::ChannelPath::Dump() const
{
	cdebug << "inChannel:";
	for(ChannelPaths::size_type i=0; i<inChannel.size(); ++i)
	{
		cdebug << " "; inChannel[i].Dump();
	}

	cdebug << std::endl << "outChannel:";
	for(ChannelPaths::size_type i=0; i<outChannel.size(); ++i)
	{
		cdebug << " "; outChannel[i].Dump();
	}
	cdebug << std::endl;
}

void ChannelPaths::ChannelPath::ScaledChannel::Dump() const
{
	cdebug << nChannel << "/" << fScale;
}

#endif
