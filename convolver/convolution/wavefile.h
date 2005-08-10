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

#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugStream.h"
#endif

// Pull in Common DX classes
#include "Common\dxstdafx.h"

class CWaveFileHandle
{
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

