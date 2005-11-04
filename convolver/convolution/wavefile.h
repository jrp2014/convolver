#pragma once
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
/////////////////////////////////////////////////////////////////////////////
//
// wavefile.h : Wrapper around the Common CWaveFile example class
//
/////////////////////////////////////////////////////////////////////////////

#include "convolution\config.h"

#ifdef LIBSNDFILE

#include <atlbase.h>
#include <libsndfile\sndfile.h>

class CWaveFileHandle
{
private:

	SNDFILE *sndfile_;

public:

	CWaveFileHandle() : sndfile_(NULL)	{};

	~CWaveFileHandle()
	{

		 sf_write_sync(sndfile_);

		int result = sf_close (sndfile_);
#if defined(DEBUG) | defined(_DEBUG)
		if (result)
		{
			cdebug << "Failed to close cleanly: (" << result << ") " << sf_error_number(result) << std::endl;
		}
		else
		{
			cdebug << "closed SNDFILE" << std::endl;
		}
#endif
	};

	CWaveFileHandle (TCHAR* path, int mode, SF_INFO *sfinfo)
	{

		if (mode == SFM_WRITE && sf_format_check (sfinfo) == false)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Invalid output file format." << std::endl;
#endif
			throw wavfileException("Invalid output file format", path, "");
		}
		else if(mode == SFM_READ)
		{
			sfinfo->format = 0;

//			// Look for .pcm
//			if (path == NULL)
//			{
//				throw wavfileException("Null filename invalid");
//			}
//
//			TCHAR* cptr = _tcsrchr (path,
//#ifdef _UNICODE
//				L'.'
//#else
//				'.'
//#endif
//				);
//
//			if (cptr != NULL)
//			{
//				TCHAR buffer[16];
//				++cptr;	// points to the extension
//				if (_tcslen (cptr) > sizeof (buffer) - 1)
//					throw wavfileException("Filename extension too long");
//
//				_tcsncpy (buffer, cptr, sizeof (buffer));
//				buffer [sizeof (buffer) - 1] = 0 ;
//
//				/* Convert everything in the buffer to lower case. */
//				cptr = buffer ;
//				while (*cptr)
//				{	
//					*cptr = 
//#ifdef _UNICODE
//						towlower (*cptr);
//#else
//						tolower (*cptr);
//#endif
//					++cptr;
//				}
//
//				cptr = buffer;
//
//				if (_tcsncmp (cptr, "pcm") == 0)
//				{
//					sfinfo->format = SF_FORMAT_RAW | SF_FORMAT_FLOAT;	// RAW PCM data (32 bit IEEE floating point)
//					sfinfo->channels = 1;								// Single channel
//					sfinfo->samplerate = ??;
//
//				}
//			}
		}

	sndfile_ = sf_open(CT2CA(path), mode, sfinfo);

#if defined(DEBUG) | defined(_DEBUG)
		char  buffer [2048];
		sf_command (sndfile_, SFC_GET_LOG_INFO, buffer, sizeof (buffer)) ;

		cdebug << "Opening " << CT2CA(path) << ": " << std::endl << buffer;
#endif
		if(sndfile_ == NULL)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Failed to open " << CT2CA(path) << std::endl;
#endif
			throw wavfileException("Failed to open", path, sf_strerror (NULL));
		}
	};

	sf_count_t readf_float(float *ptr, sf_count_t frames) const
	{
		return sf_readf_float(sndfile_, ptr, frames);
	};

	sf_count_t  write_float(float *ptr, sf_count_t items) const
	{
		return sf_write_float(sndfile_, ptr, items);
	};


	sf_count_t  writef_float  (float *ptr, sf_count_t frames) const
	{
		return sf_writef_float  (sndfile_, ptr, frames);
	}


	sf_count_t read_int(int *ptr, sf_count_t items) const
	{
		return sf_read_int(sndfile_, ptr, items);
	};


	sf_count_t write_int(int *ptr, sf_count_t items) const
	{
		return sf_write_int(sndfile_, ptr, items);
	};


	sf_count_t read_raw(void *ptr, sf_count_t bytes)
	{
		return sf_read_raw(sndfile_, ptr, bytes);
	};

	sf_count_t  write_raw(void *ptr, sf_count_t bytes)
	{
		return sf_write_raw(sndfile_, ptr, bytes);
	};

	int format_check (const SF_INFO *info)
	{
		const int result = sf_format_check (info);

#if defined(DEBUG) | defined(_DEBUG)
		char  buffer [2048];
		sf_command (sndfile_, SFC_GET_LOG_INFO, buffer, sizeof (buffer)) ;

		cdebug << "Invalid format: " << buffer << std::endl;
#endif
		return result;

	}

	//WAVEFORMATEXTENSIBLE sfinfo_to_wfex(const SF_INFO& sf_info) const
	//{
	//	WAVEFORMATEXTENSIBLE wfex;
	//	::ZeroMemory(&wfex, sizeof(wfex);

	//	wfex.dwChannelMask=0;
	//	wfex.Format.cbSize=0;
	//	wfex.Format.nAvgBytesPerSec=0;
	//	wfex.Format.nBlockAlign=0;
	//	wfex.Format.nChannels = sf_info.channels;
	//	wfex.Format.nSamplesPerSec=sf_info.samplerate;
	//	wfex.Format.wBitsPerSample=0;
	//	wfex.Format.wFormatTag=sf_info.format;
	//	wfex.Samples.wValidBitsPerSample / samplesperblock;
	//	wfex.SubFormat.Data1=0;
	//	wfex.SubFormat.Data2=0;
	//	wfex.SubFormat.Data3=0;
	//	wfex.SubFormat.Data4=0;
	//	return wfex;
	//}


private:
	CWaveFileHandle(const CWaveFileHandle& other);
	CWaveFileHandle& operator=(const CWaveFileHandle& other);
};


#else
// Pull in Common DX classes
#include "Common\dxstdafx.h"

class CWaveFileHandle
{
private:
	CWaveFile* m_CWaveFile;
public:
	explicit CWaveFileHandle() : m_CWaveFile(new CWaveFile) {}

	~CWaveFileHandle()
	{
		SAFE_DELETE(m_CWaveFile);
	}

	CWaveFile* operator -> () const
	{
		assert(m_CWaveFile);
		return m_CWaveFile;
	}

	CWaveFile& operator*() const
	{
		assert(m_CWaveFile);
		return *m_CWaveFile;
	}

private:
	CWaveFileHandle(const CWaveFileHandle& other);
	CWaveFileHandle& operator=(const CWaveFileHandle& other);
};

#endif
