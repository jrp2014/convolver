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
//
// WAV format helper routines

#include ".\waveformat.h"
//
//class WaveFormat
//{
//	WaveFormat();
//	~WaveFormat();
//
//	const std::string channelDescription(const WAVEFORMATEX* w);
//
//};

// TODO: put the strings in this file into the resource file for internationalisation

//#ifdef LIBSNDFILE

const std::string channelDescription(const SF_INFO& sfinfo)
{
	switch(sfinfo.channels)
	{
	case 1:
		return "Mono";
	case 2:
		return "Stereo";
	default:
		{
			std::ostringstream s;
			s << sfinfo.channels << "-channel";
			return s.str();
		}
	}
}

const std::string formatDescription(const SF_INFO& sfinfo)
{
	SF_FORMAT_INFO	format_info;
	format_info.format = sfinfo.format;
	sf_command (NULL, SFC_GET_FORMAT_INFO, &format_info, sizeof (format_info)) ;
	//printf ("%08x  %s %s\n", format_info.format, format_info.name, format_info.extension) ;
	std::ostringstream s;
	s <<  format_info.name 
#if defined(DEBUG) | defined(_DEBUG)
	<< " [" << format_info.extension << "]"
#endif
	;
	return s.str();
}

std::string waveFormatDescription(const SF_INFO& sfinfo, const char* prefix)
{
	std::ostringstream s;
	s << prefix << channelDescription(sfinfo) << " " 
		<< formatDescription(sfinfo) << " "
		<< sfinfo.samplerate/1000.0f << "kHz "
#if defined(DEBUG) | defined(_DEBUG)
		<< sfinfo.sections << " sections "
#endif
		<< sfinfo.frames << " taps";
	return s.str();
}

//#else

const std::string channelDescription(const WAVEFORMATEXTENSIBLE* w)
{
	if (w->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		// Need to check number of channels as the likes of Audition sets the channel mask to 7.1 for 5.1 wav files, eg
		if (w->dwChannelMask & KSAUDIO_SPEAKER_7POINT1 && w->Format.nChannels == 8)
			return "7.1";
		if (w->dwChannelMask & KSAUDIO_SPEAKER_5POINT1 && w->Format.nChannels == 6)
			return "5.1";
		if (w->dwChannelMask & KSAUDIO_SPEAKER_SURROUND && w->Format.nChannels == 4)
			return "Surround";
		if (w->dwChannelMask & KSAUDIO_SPEAKER_QUAD && w->Format.nChannels == 4)
			return "Quad";
		if (w->dwChannelMask & KSAUDIO_SPEAKER_STEREO && w->Format.nChannels == 2)
			return "Stereo";
		if (w->dwChannelMask & KSAUDIO_SPEAKER_SUPER_WOOFER && w->Format.nChannels == 1)
			return "Sub";
		if (w->dwChannelMask & KSAUDIO_SPEAKER_MONO && w->Format.nChannels == 1)
			return "Mono";
		if (w->dwChannelMask & KSAUDIO_SPEAKER_DIRECTOUT)
			return "Direct";

		std::ostringstream s;

		s << w->Format.nChannels << "-channel( ";
		if( w->dwChannelMask & SPEAKER_FRONT_LEFT)
			s << "Front Left,";
		if( w->dwChannelMask & SPEAKER_FRONT_RIGHT)
			s <<  "Front Right,";
		if( w->dwChannelMask & SPEAKER_FRONT_CENTER)
			s <<  "Front Centre,";
		if( w->dwChannelMask & SPEAKER_LOW_FREQUENCY)
			s <<  "Low Frequency,";
		if( w->dwChannelMask & SPEAKER_BACK_LEFT)
			s <<  "Back Left,";
		if( w->dwChannelMask & SPEAKER_BACK_RIGHT)
			s <<  "Back Right,";
		if( w->dwChannelMask & SPEAKER_FRONT_LEFT_OF_CENTER)
			s <<  "Front Left of Centre,";
		if( w->dwChannelMask & SPEAKER_FRONT_RIGHT_OF_CENTER)
			s <<  "Front Right of Centre,";
		if( w->dwChannelMask & SPEAKER_BACK_CENTER)
			s <<  "Back Centre,";
		if( w->dwChannelMask & SPEAKER_SIDE_LEFT)
			s <<  "Side Left,";
		if( w->dwChannelMask & SPEAKER_SIDE_RIGHT)
			s <<  "Side Right,";
		if( w->dwChannelMask & SPEAKER_TOP_CENTER)
			s <<  "Top Centre,";
		if( w->dwChannelMask & SPEAKER_TOP_FRONT_LEFT)
			s <<  "Top Front Left,";
		if( w->dwChannelMask & SPEAKER_TOP_FRONT_CENTER)
			s <<  "Top Front Centre,";
		if( w->dwChannelMask & SPEAKER_TOP_FRONT_RIGHT)
			s <<  "Top Front Right,";
		if( w->dwChannelMask & SPEAKER_TOP_BACK_LEFT)
			s <<  "Top Back Left,";
		if( w->dwChannelMask & SPEAKER_TOP_BACK_CENTER)
			s <<  "Top Back Centre,";
		if( w->dwChannelMask & SPEAKER_TOP_BACK_RIGHT)
			s <<  "Top Back Right,";

		long pos = s.tellp();
		s.seekp(pos - 1); // remove trailing ,
		s << " )";
		return s.str();
	}
	else
	{
		std::ostringstream s;
		s << w->Format.nChannels << "-channel";
		return s.str();
	}
}

const std::string formatDescription(const WAVEFORMATEXTENSIBLE* w)
{
	switch (w->Format.wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		return "PCM";

	case WAVE_FORMAT_IEEE_FLOAT:
		return "IEEE Float";

	case WAVE_FORMAT_EXTENSIBLE:
		{
			if (w->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
				return "PCM";
			else
				if (w->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
					return "IEEE Float";
				else
					return "Unsupported extensible format";
		}

	default:
		{
			std::ostringstream s;
			s << "Unsupported format: " << w->Format.wFormatTag;
			return s.str(); 
		}
	}
}

WORD BitsPerSample(const WAVEFORMATEXTENSIBLE* w)
{
	if (w->Format.wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		return w->Samples.wValidBitsPerSample;
	else // Assume PCM or IEEE Float
		return w->Format.wBitsPerSample;
}


std::string waveFormatDescription(const WAVEFORMATEXTENSIBLE* w, const DWORD samples, const char* prefix)
{
	std::ostringstream s;
	s << prefix << BitsPerSample(w) << "-bit " << w->Format.nSamplesPerSec/1000.0f << "kHz " << channelDescription(w) << " " << formatDescription(w) << ", " << samples << " samples";
	return s.str();
}

//#endif