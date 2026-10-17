#pragma once
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
// wavefile.h : Wrapper around the Common CWaveFile example class
//
/////////////////////////////////////////////////////////////////////////////

#include "convolution\config.h"

#ifdef LIBSNDFILE

#include <atlbase.h>
#include <libsndfile\sndfile.h>
#include <locale>
#include <algorithm>
#include <stdexcept>

class CWaveFileHandle
{
private:

	SNDFILE *sndfile_;

public:

	CWaveFileHandle() : sndfile_(NULL)	{};

	~CWaveFileHandle()
	{
		close();
	};

	// Explicit closure lets stream clients report errors while the destructor
	// remains non-throwing. The caller retains ownership of an opened descriptor.
	int close()
	{
		if(sndfile_ == NULL)
			return 0;

		sf_write_sync(sndfile_);
		// sf_close releases the error state, so preserve errors raised by sync.
		const int streamError = sf_error(sndfile_);
		const int closeResult = sf_close(sndfile_);
		sndfile_ = NULL;
		const int result = closeResult ? closeResult : streamError;
#if defined(DEBUG) | defined(_DEBUG)
		if(result)
		{
			cdebug << "Failed to close cleanly: (" << result << ") " << sf_error_number(result) << std::endl;
		}
		else
		{
			cdebug << "closed SNDFILE" << std::endl;
		}
#endif
		return result;
	}

	// Query after a short read or write, before closing the handle.
	int error() const
	{
		return sf_error(sndfile_);
	}

	CWaveFileHandle (const TCHAR path[MAX_PATH], int mode, SF_INFO *sfinfo, const DWORD& nSampleRate)
	{

		if (mode == SFM_WRITE && sf_format_check (sfinfo) == false)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Invalid output file format specified." << std::endl;
#endif
			throw wavfileException("Invalid output file format specified", path, "");
		}
		else if(mode == SFM_READ)
		{
			sfinfo->format = 0;

			const std::basic_string<TCHAR> spath = path;
			std::wstring::size_type idx = spath.rfind('.');
			if(idx != std::wstring::npos)
			{
				if(spath.substr(idx+1) == TEXT("pcm") || spath.substr(idx+1) == TEXT("PCM") || spath.substr(idx+1) == TEXT("Pcm") ||
					spath.substr(idx+1) == TEXT("raw") || spath.substr(idx+1) == TEXT("RAW") || spath.substr(idx+1) == TEXT("Raw"))
				{
					sfinfo->format = SF_FORMAT_RAW | SF_FORMAT_FLOAT;	// RAW PCM data (32 bit IEEE floating point)
					sfinfo->channels = 1;								// Single channel
					sfinfo->samplerate = nSampleRate;
				}
				if(spath.substr(idx+1) == TEXT("dbl") || spath.substr(idx+1) == TEXT("DBL") || spath.substr(idx+1) == TEXT("Dbl"))
				{
					sfinfo->format = SF_FORMAT_RAW | SF_FORMAT_DOUBLE;	// RAW PCM data (64 bit IEEE floating point)
					sfinfo->channels = 1;								// Single channel
					sfinfo->samplerate = nSampleRate;
				}

			}
		}

		sndfile_ = sf_open(CT2CA(path), mode, sfinfo);

#if defined(DEBUG) | defined(_DEBUG)
		char  buffer [2048];
		sf_command (sndfile_, SFC_GET_LOG_INFO, buffer, sizeof (buffer));

		cdebug << "Opening " << CT2CA(path) << ": " << std::endl << buffer;
#endif
		if(sndfile_ == NULL)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Failed to open " << CT2CA(path) << std::endl;
#endif
			throw wavfileException("Failed to open filter sound file:", path, "");
		}
	};

	CWaveFileHandle (int fd, int mode, SF_INFO *sfinfo) : sndfile_(NULL)
	{

		if (mode == SFM_WRITE && sf_format_check (sfinfo) == false)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Invalid output file format specified." << std::endl;
#endif
			throw wavfileException("Invalid output format specified", fd, "");
		}
		else if(mode == SFM_READ)
		{
			if((sfinfo->format & SF_FORMAT_TYPEMASK) != SF_FORMAT_RAW)
				sfinfo->format = 0;
		}

		sndfile_ = sf_open_fd (fd, mode, sfinfo, SF_FALSE /* Don't close file descriptor on sf_close */);

#if defined(DEBUG) | defined(_DEBUG)
		char  buffer [2048];
		sf_command (sndfile_, SFC_GET_LOG_INFO, buffer, sizeof (buffer));

		cdebug << "Opening file descriptor " << fd << ": " << std::endl << buffer;
#endif
		if(sndfile_ == NULL)
		{
#if defined(DEBUG) | defined(_DEBUG)
			cdebug << "Failed to open file descriptor " << fd << std::endl;
#endif
			throw wavfileException("Failed to open file descriptor", fd, sf_strerror(NULL));
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

	void  sf_rewind()
	{
		if(sf_seek(sndfile_, 0, SEEK_SET) != 0)
		{
			throw wavfileException("Failed to rewind filter sound file", TEXT(""), "");
		}
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


struct SndFileFormat
{
	typedef unsigned int SndFileFormatType;

	static const unsigned int nMajor = 21;
	static const unsigned int nMinor = 22;

	static const unsigned int nStrLen = sizeof(TEXT("VOX_ADPCM")) / sizeof(TCHAR) + 1;

	struct SndFileMap
	{
		TCHAR id[nStrLen];
		SndFileFormatType mask;
	};

	static const SndFileMap Major[nMajor];
	static const SndFileMap Minor[nMinor];

	static SndFileFormatType Lookup(const TCHAR* major, const TCHAR* minor)
	{
		SndFileFormatType result = 0;

		for(SndFileFormatType i=0; i<nMajor; ++i)
		{
			if(_tcsncicmp(major, Major[i].id, nStrLen) == 0)
			{
				result = Major[i].mask;

				for(SndFileFormatType j=0; j<nMinor; ++j)
				{
					if(_tcsncicmp(minor, Minor[j].id, nStrLen) == 0)
					{
						return result | Minor[j].mask;
					}
				}
			}
		}

		throw std::range_error("Invalid sound format specification");
	}
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
