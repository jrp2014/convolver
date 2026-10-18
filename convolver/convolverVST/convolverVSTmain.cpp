// Convolver: VST plug-in that convolves an impulse respose
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
/////////////////////////////////////////////////////////////////////////////
#include "convolvervst.h"


//-------------------------------------------------------------------------------------------------------
AudioEffect* createEffectInstance (audioMasterCallback audioMaster)
{


#if defined(DEBUG) | defined(_DEBUG)
	//const int	mode =   (1 * _CRTDBG_MODE_DEBUG) | (0 * _CRTDBG_MODE_WNDW);
	//::_CrtSetReportMode (_CRT_WARN, mode);
	//::_CrtSetReportMode (_CRT_ERROR, mode);
	//::_CrtSetReportMode (_CRT_ASSERT, mode);
	//const int	old_flags = ::_CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);
	//::_CrtSetDbgFlag (  old_flags
	//	| (1 * _CRTDBG_LEAK_CHECK_DF)
	//	| (1 * _CRTDBG_CHECK_ALWAYS_DF)
	//	| (1 * _CRTDBG_ALLOC_MEM_DF));

	// 3 = function call trace
	apDebug::gOnly().set(4);

	debugstream.sink (apDebugSinkConsole::sOnly);
#endif

	return new ConvolverVST (audioMaster);
}
