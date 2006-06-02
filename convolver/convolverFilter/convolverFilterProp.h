// ConvolverFilter: DirectShow filter that convolves an impulse respose
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
#pragma once

class CconvolverFilterProperties : public CBasePropertyPage
{

public:

	static CUnknown * WINAPI CreateInstance(LPUNKNOWN lpunk, HRESULT *phr);

private:

	BOOL OnReceiveMessage(HWND hwnd,UINT uMsg,WPARAM wParam,LPARAM lParam);
	HRESULT OnConnect(IUnknown *pUnknown);
	HRESULT OnDisconnect();
	HRESULT OnActivate();
	HRESULT OnDeactivate();
	HRESULT OnApplyChanges();

	HRESULT DisplayFilterFormat(HWND hwnd, TCHAR* szFilterFileName);

	void	GetControlValues(float& fAttenuation, 
		TCHAR szFilterFileName[MAX_PATH],
		DWORD& nPartitions,
		unsigned int& nPlanningRigour,
		unsigned int& nDither, 
		unsigned int& nNoiseShaping
		);

	void	SetDirty();

	CconvolverFilterProperties(LPUNKNOWN lpunk, HRESULT *phr);

	BOOL m_bIsInitialized;							// Used to ignore startup messages
	IconvolverFilter *m_pIconvolverFilter;      // The custom interface on the filter
};

