
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

#include "waveformat.h"
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

const std::string channelDescription(const WAVEFORMATEX* w)
{
	if (w->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		DWORD dwChannelMask = ((WAVEFORMATEXTENSIBLE*) w)->dwChannelMask;

		if (dwChannelMask & KSAUDIO_SPEAKER_7POINT1)
			return "7.1";
		if (dwChannelMask & KSAUDIO_SPEAKER_5POINT1)
			return "5.1";
		if (dwChannelMask & KSAUDIO_SPEAKER_SURROUND)
			return "Surround";
		if (dwChannelMask & KSAUDIO_SPEAKER_QUAD)
			return "Quad";
		if (dwChannelMask & KSAUDIO_SPEAKER_STEREO)
			return "Stereo";
		if (dwChannelMask & KSAUDIO_SPEAKER_SUPER_WOOFER)
			return "Sub";
		if (dwChannelMask & KSAUDIO_SPEAKER_MONO)
			return "Mono";
		if (dwChannelMask & KSAUDIO_SPEAKER_DIRECTOUT)
			return "Direct";

		std::ostringstream s;

		s << w->nChannels << "-channel( ";
		if( dwChannelMask & SPEAKER_FRONT_LEFT)
			s << "Front Left,";
		if( dwChannelMask & SPEAKER_FRONT_RIGHT)
			s <<  "Front Right,";
		if( dwChannelMask & SPEAKER_FRONT_CENTER)
			s <<  "Front Centre,";
		if( dwChannelMask & SPEAKER_LOW_FREQUENCY)
			s <<  "Low Frequency,";
		if( dwChannelMask & SPEAKER_BACK_LEFT)
			s <<  "Back Left,";
		if( dwChannelMask & SPEAKER_BACK_RIGHT)
			s <<  "Back Right,";
		if( dwChannelMask & SPEAKER_FRONT_LEFT_OF_CENTER)
			s <<  "Front Left of Centre,";
		if( dwChannelMask & SPEAKER_FRONT_RIGHT_OF_CENTER)
			s <<  "Front Right of Centre,";
		if( dwChannelMask & SPEAKER_BACK_CENTER)
			s <<  "Back Centre,";
		if( dwChannelMask & SPEAKER_SIDE_LEFT)
			s <<  "Side Left,";
		if( dwChannelMask & SPEAKER_SIDE_RIGHT)
			s <<  "Side Right,";
		if( dwChannelMask & SPEAKER_TOP_CENTER)
			s <<  "Top Centre,";
		if( dwChannelMask & SPEAKER_TOP_FRONT_LEFT)
			s <<  "Top Front Left,";
		if( dwChannelMask & SPEAKER_TOP_FRONT_CENTER)
			s <<  "Top Front Centre,";
		if( dwChannelMask & SPEAKER_TOP_FRONT_RIGHT)
			s <<  "Top Front Right,";
		if( dwChannelMask & SPEAKER_TOP_BACK_LEFT)
			s <<  "Top Back Left,";
		if( dwChannelMask & SPEAKER_TOP_BACK_CENTER)
			s <<  "Top Back Centre,";
		if( dwChannelMask & SPEAKER_TOP_BACK_RIGHT)
			s <<  "Top Back Right,";

		long pos = s.tellp();
		s.seekp(pos - 1); // remove trailing ,
		s << " )";
		return s.str();
	}
	else
	{
		std::ostringstream s;
		s << w->nChannels << "-channel";
		return s.str();
	}
}


const std::string formatDescription(const WAVEFORMATEX* w)
{
	switch (w->wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		return "PCM";

	case WAVE_FORMAT_IEEE_FLOAT:
		return "IEEE Float";

	case WAVE_FORMAT_EXTENSIBLE:
		{
			GUID SubFormat = ((WAVEFORMATEXTENSIBLE*) w)->SubFormat;
			if (SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
				return "PCM";
			else
				if (SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
					return "IEEE Float";
				else
					return "Unsupported extensible format";
		}

	default:
		{
			std::ostringstream s;
			s << "Unsupported format: " << w->wFormatTag;
			return s.str(); 
		}
	}
}

const WORD BitsPerSample(const WAVEFORMATEX* w)
{
	if (w->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
		return ((WAVEFORMATEXTENSIBLE*) w)->Samples.wValidBitsPerSample;
	else // Assume PCM or IEEE Float
		return w->wBitsPerSample;
}


std::string waveFormatDescription(const WAVEFORMATEX* w, const DWORD samples, const char* prefix)
{
	std::ostringstream s;
	s << prefix << BitsPerSample(w) << "-bit " << w->nSamplesPerSec/1000 << "kHz " << channelDescription(w) << " " << formatDescription(w) << ", " << samples << " samples";
	return s.str();
}
