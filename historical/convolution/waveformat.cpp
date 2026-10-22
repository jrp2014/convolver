// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005,2006  jrp
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
#include <ks.h>
#include <ksmedia.h>

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

#ifdef LIBSNDFILE

const std::basic_string<TCHAR> channelDescription(const SF_INFO& sfinfo)
{
	switch(sfinfo.channels)
	{
	case 1:
		return TEXT("Mono");
	case 2:
		return TEXT("Stereo");
	default:
		{
			std::basic_ostringstream<TCHAR> s;
			s << sfinfo.channels << TEXT("-channel");
			return s.str();
		}
	}
}

const std::basic_string<TCHAR> formatDescription(const SF_INFO& sfinfo)
{
	SF_FORMAT_INFO	format_info;
	format_info.format = sfinfo.format;
	sf_command (NULL, SFC_GET_FORMAT_INFO, &format_info, sizeof (format_info)) ;
	//printf ("%08x  %s %s\n", format_info.format, format_info.name, format_info.extension) ;
	std::basic_ostringstream<TCHAR> s;
	s <<  format_info.name 
#if defined(DEBUG) | defined(_DEBUG)
	<< TEXT(" [") << format_info.extension << TEXT("]")
#endif
	;
	return s.str();
}

const std::basic_string<TCHAR> waveFormatDescription(const SF_INFO& sfinfo, const TCHAR* prefix)
{
	std::basic_ostringstream<TCHAR> s;
	s << prefix << channelDescription(sfinfo) << TEXT(" ") 
		<< formatDescription(sfinfo) << TEXT(" ")
		<< sfinfo.samplerate/1000.0f << TEXT("kHz ")
#if defined(DEBUG) | defined(_DEBUG)
		<< sfinfo.sections << TEXT(" sections ")
#endif
		<< sfinfo.frames << TEXT(" frames");
	return s.str();
}

#endif

const std::basic_string<TCHAR> channelDescription(const WAVEFORMATEXTENSIBLE* w)
{
	return channelDescription(w->Format.wFormatTag, w->dwChannelMask,  w->Format.nChannels);
}

const std::basic_string<TCHAR> channelDescription(const WORD wFormatTag, const DWORD& dwChannelMask, const WORD& nChannels)
{
	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		// Need to check number of channels as the likes of Audition sets the channel mask to 7.1 for 5.1 wav files, eg
		// Also, order dependent
		if ((dwChannelMask & KSAUDIO_SPEAKER_7POINT1_SURROUND) && nChannels == 8)
			return TEXT("7.1 surround");
		if ((dwChannelMask & KSAUDIO_SPEAKER_7POINT1) && nChannels == 8)
			return TEXT("7.1");
		if ((dwChannelMask & KSAUDIO_SPEAKER_5POINT1) && nChannels == 6)
			return TEXT("5.1");
		if ((dwChannelMask & KSAUDIO_SPEAKER_QUAD) && nChannels == 4)
			return TEXT("Quad");
		if ((dwChannelMask & KSAUDIO_SPEAKER_SURROUND) && nChannels == 4)
			return TEXT("Surround");
		if ((dwChannelMask & KSAUDIO_SPEAKER_STEREO) && nChannels == 2)
			return TEXT("Stereo");
		if ((dwChannelMask & KSAUDIO_SPEAKER_SUPER_WOOFER) && nChannels == 1)
			return TEXT("Sub");
		if ((dwChannelMask & KSAUDIO_SPEAKER_MONO) && nChannels == 1)
			return TEXT("Mono");

		std::basic_ostringstream<TCHAR> s;

		if (nChannels == 1)
		{
			s <<TEXT("Mono");
		}
		else if (nChannels == 2)
		{
			s <<TEXT("Stereo");
		}
		else
			s << nChannels <<TEXT("-channel");

		if(dwChannelMask == 0)
		{
			s <<TEXT(" direct");
		}
		else
		{
			s <<TEXT(" (");
			if (dwChannelMask & SPEAKER_ALL)
				s <<TEXT("any speakers");

			if( dwChannelMask & SPEAKER_FRONT_LEFT)
				s <<TEXT("Front Left,");
			if( dwChannelMask & SPEAKER_FRONT_RIGHT)
				s << TEXT("Front Right,");
			if( dwChannelMask & SPEAKER_FRONT_CENTER)
				s << TEXT("Front Centre,");
			if( dwChannelMask & SPEAKER_LOW_FREQUENCY)
				s << TEXT("Low Frequency,");
			if( dwChannelMask & SPEAKER_BACK_LEFT)
				s << TEXT("Back Left,");
			if( dwChannelMask & SPEAKER_BACK_RIGHT)
				s << TEXT("Back Right,");
			if( dwChannelMask & SPEAKER_FRONT_LEFT_OF_CENTER)
				s << TEXT("Front Left of Centre,");
			if( dwChannelMask & SPEAKER_FRONT_RIGHT_OF_CENTER)
				s << TEXT("Front Right of Centre,");
			if( dwChannelMask & SPEAKER_BACK_CENTER)
				s << TEXT("Back Centre,");
			if( dwChannelMask & SPEAKER_SIDE_LEFT)
				s << TEXT("Side Left,");
			if( dwChannelMask & SPEAKER_SIDE_RIGHT)
				s << TEXT("Side Right,");
			if( dwChannelMask & SPEAKER_TOP_CENTER)
				s << TEXT("Top Centre,");
			if( dwChannelMask & SPEAKER_TOP_FRONT_LEFT)
				s << TEXT("Top Front Left,");
			if( dwChannelMask & SPEAKER_TOP_FRONT_CENTER)
				s << TEXT("Top Front Centre,");
			if( dwChannelMask & SPEAKER_TOP_FRONT_RIGHT)
				s << TEXT("Top Front Right,");
			if( dwChannelMask & SPEAKER_TOP_BACK_LEFT)
				s << TEXT("Top Back Left,");
			if( dwChannelMask & SPEAKER_TOP_BACK_CENTER)
				s << TEXT("Top Back Centre,");
			if( dwChannelMask & SPEAKER_TOP_BACK_RIGHT)
				s << TEXT("Top Back Right,");

			long pos = s.tellp();
			s.seekp(pos - 1); // remove trailing ,
			s <<TEXT(" )");
		}

		return s.str();
	}
	else
	{
		std::basic_ostringstream<TCHAR> s;
		s << nChannels << TEXT("-channel");
		return s.str();
	}
}

const std::basic_string<TCHAR> formatDescription(const WAVEFORMATEXTENSIBLE* w)
{
	switch (w->Format.wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		return TEXT("PCM");

	case WAVE_FORMAT_IEEE_FLOAT:
		return TEXT("IEEE Float");

	case WAVE_FORMAT_EXTENSIBLE:
		{
			if (w->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
				return TEXT("PCM");
			else
				if (w->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
					return TEXT("IEEE Float");
				else
					return TEXT("Unsupported extensible format");
		}

	default:
		{
			std::basic_ostringstream<TCHAR> s;
			s << TEXT("Unsupported format: ") << w->Format.wFormatTag;
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


const std::basic_string<TCHAR> waveFormatDescription(const WAVEFORMATEXTENSIBLE* w, const DWORD samples, const TCHAR* prefix)
{
	std::basic_ostringstream<TCHAR> s;
	if(w == NULL)
	{
		s << prefix << TEXT("Not set");
	}
	else
	{
		s << prefix << BitsPerSample(w) << TEXT("-bit ") <<
			w->Format.nSamplesPerSec/1000.0f << TEXT("kHz ") << 
			channelDescription(w) << TEXT(" ") << 
			formatDescription(w);
		if(samples > 0)
			s << TEXT(" ") << samples << TEXT(" samples");
	}
	return s.str();
}

//#endif
