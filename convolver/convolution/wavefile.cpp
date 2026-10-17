// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005,2006  John Pavel
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
/////////////////////////////////////////////////////////////////////////////
//
// wavefile.cpp : Wrapper around the Common CWaveFile example class
//
/////////////////////////////////////////////////////////////////////////////

#include "convolution\wavefile.h"

#ifdef LIBSNDFILE

const SndFileFormat::SndFileMap SndFileFormat::Major[SndFileFormat::nMajor] =
{
	{TEXT("WAV")	, 0x010000},	/*	Microsoft WAV format (little endian). */
	{TEXT("AIFF")	, 0x020000},	/*	Apple/SGI AIFF format (big endian). */
	{TEXT("AU")		, 0x030000},	/*	Sun/NeXT AU format (big endian). */
	{TEXT("RAW")	, 0x040000},	/*	RAW PCM data. */
	{TEXT("PAF")	, 0x050000},	/*	Ensoniq PARIS file format. */
	{TEXT("SVX")	, 0x060000},	/*	Amiga IFF / SVX8 / SV16 format. */
	{TEXT("NIST")	, 0x070000},	/*	Sphere NIST format. */
	{TEXT("VOC")	, 0x080000},	/*	VOC files. */
	{TEXT("IRCAM")	, 0x0A0000},	/*	Berkeley/IRCAM/CARL */
	{TEXT("W64")	, 0x0B0000},	/*	Sonic Foundry's 64 bit RIFF/WAV */
	{TEXT("MAT4")	, 0x0C0000},	/*	Matlab (tm) V4.2 / GNU Octave 2.0 */
	{TEXT("MAT5")	, 0x0D0000},	/*	Matlab (tm) V5.0 / GNU Octave 2.1 */
	{TEXT("PVF")	, 0x0E0000},	/*	Portable Voice Format */
	{TEXT("XI")		, 0x0F0000},	/*	Fasttracker 2 Extended Instrument */
	{TEXT("HTK")	, 0x100000},	/*	HMM Tool Kit format */
	{TEXT("SDS")	, 0x110000},	/*	Midi Sample Dump Standard */
	{TEXT("AVR")	, 0x120000},	/*	Audio Visual Research */
	{TEXT("WAVEX")	, 0x130000},	/*	MS WAVE with WAVEFORMATEX */
	{TEXT("SD2")	, 0x160000},	/*	Sound Designer 2 */
	{TEXT("FLAC")	, 0x170000},	/*	FLAC lossless file format */
	{TEXT("CAF")	, 0x180000},	/*	Core Audio File format */

};

const SndFileFormat::SndFileMap SndFileFormat::Minor[SndFileFormat::nMinor] =
{
	{TEXT("PCM_S8"),	0x0001},	/*	Signed 8 bit data */
	{TEXT("PCM_16"),	0x0002},	/*	Signed 16 bit data */
	{TEXT("PCM_24"),	0x0003},	/*	Signed 24 bit data */
	{TEXT("PCM_32"),	0x0004},	/*	Signed 32 bit data */

	{TEXT("PCM_U8"),	0x0005},	/*	Unsigned 8 bit data (WAV and RAW	only) */

	{TEXT("FLOAT"),		0x0006},	/*	32 bit float data */
	{TEXT("DOUBLE"),	0x0007},	/*	64 bit float data */

	{TEXT("ULAW"),		0x0010},	/*	U-Law encoded. */
	{TEXT("ALAW"),		0x0011},	/*	A-Law encoded. */
	{TEXT("IMA_ADPCM"),	0x0012},	/*	IMA ADPCM. */
	{TEXT("MS_ADPCM"),	0x0013},	/*	Microsoft ADPCM. */

	{TEXT("GSM610"),	0x0020},	/*	GSM 6.10 encoding. */
	{TEXT("VOX_ADPCM"),	0x0021},	/*	Oki Dialogic ADPCM encoding. */

	{TEXT("G721_32"),	0x0030},	/*	32kbs G721 ADPCM encoding. */
	{TEXT("G723_24"),	0x0031},	/*	24kbs G723 ADPCM encoding. */
	{TEXT("G723_40"),	0x0032},	/*	40kbs G723 ADPCM encoding. */

	{TEXT("DWVW_12"),	0x0040},	/*	12 bit Delta Width Variable Word encoding. */
	{TEXT("DWVW_16"),	0x0041},	/*	16 bit Delta Width Variable Word encoding. */
	{TEXT("DWVW_24"),	0x0042},	/*	24 bit Delta Width Variable Word encoding. */
	{TEXT("DWVW_N"),	0x0043},	/*	N bit Delta Width Variable Word	encoding. */

	{TEXT("DPCM_8"),	0x0050},	/*	8 bit differential PCM (XI only) */
	{TEXT("DPCM_16"),	0x0051}		/*	16 bit differential PCM (XI only) */
};

#endif // LIBSNDFILE
