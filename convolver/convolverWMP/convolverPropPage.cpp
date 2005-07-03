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
// CConvolverPropPage.cpp : Implementation of the property page for CConvolver
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <stdio.h>
#include "convolverWMP\convolver.h"
#include "convolverWMP\convolverPropPage.h"

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::CConvolverProp
// Constructor

CConvolverPropPage::CConvolverPropPage()
{
	m_dwTitleID = IDS_TITLEPROPPAGE;
	m_dwHelpFileID = IDS_HELPFILEPROPPAGE;
	m_dwDocStringID = IDS_DOCSTRINGPROPPAGE;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::~CConvolverProp
// Destructor

CConvolverPropPage::~CConvolverPropPage()
{
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::SetObjects
//
STDMETHODIMP CConvolverPropPage::SetObjects(ULONG nObjects, IUnknown** ppUnk)
{
	// find our plug-in object, if it was passed in
	for (DWORD i = 0; i < nObjects; i++)
	{
		CComPtr<IConvolver> pPlugin;

		IUnknown    *pUnknown = ppUnk[i];
		if (pUnknown)
		{
			HRESULT hr = pUnknown->QueryInterface(__uuidof(IConvolver), (void**)&pPlugin); // Get a pointer to the plug-in.
			if ((SUCCEEDED(hr)) && (pPlugin))
			{
				// save plug-in interface
				m_spConvolver = pPlugin;
				break;
			}
		}
	}

	return S_OK;
}

STDMETHODIMP CConvolverPropPage::DisplayFilterFormat(TCHAR* szFilterFileName)
{
	HRESULT hr = ERROR_SUCCESS;

	SetDlgItemText( IDC_FILTERFILELABEL, szFilterFileName );

	// Load the wave file
	CWaveFile* pFilterWave = new CWaveFile();
	if( FAILED(hr = pFilterWave->Open( szFilterFileName, NULL, WAVEFILE_READ ) ) )
	{
		SetDlgItemText( IDC_STATUS, TEXT("Failed to open Filter file") );
	}
	else
	{
		// Put up a description of the filter format. CA2CT converts from a const char* to an LPCTSTR
		// TODO: internationalisation of Filter:
		std::string description = waveFormatDescription(pFilterWave->GetFormat(), pFilterWave->GetSize() / pFilterWave->GetFormat()->nBlockAlign , "Filter: ");
		SetDlgItemText( IDC_STATUS, CA2CT(description.c_str()) );
	}
	SAFE_DELETE(pFilterWave);

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::Apply
//

STDMETHODIMP CConvolverPropPage::Apply(void)
{
	TCHAR   szStr[MAXSTRING] = { 0 };
	double	fAttenuation = 0.0;	// Initialize a double for the attenuation
	DWORD   dwAttenuation = m_spConvolver->encode_Attenuationdb(fAttenuation);  // Encoding necessary as DWORD is an unsigned long
	DWORD   dwWetmix = 50;       // Initialize a DWORD for effect level.
	double  fWetmix = 0.50;      // Initialize a double for effect level.
	TCHAR szFilterFileName[MAX_PATH]	= { 0 };

	// Get the attenuation value from the dialog box.
	GetDlgItemText(IDC_ATTENUATION, szStr, sizeof(szStr) / sizeof(szStr[0]));
	// atof does not work with Unicode
#ifdef UNICODE 
	CHAR strTmp[2*(sizeof(szStr)+1)]; // SIZE equals (2*(sizeof(tstr)+1)). This ensures enough
	// room for the multibyte characters if they are two
	// bytes long and a terminating null character.
	wcstombs(strTmp, (const wchar_t *) szStr, sizeof(strTmp)); 
	fAttenuation = atof(strTmp); 
#else 
	fAttenuation = atof(szStr);
#endif 

	// Make sure attenuation value is valid
	if ((-fAttenuation > MAX_ATTENUATION) || (fAttenuation > MAX_ATTENUATION))
	{
		if (::LoadString(_Module.GetResourceInstance(), IDS_ATTENUATIONRANGEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
		{
			MessageBox(szStr);
		}
		return E_FAIL;
	}
	else
		dwAttenuation = m_spConvolver->encode_Attenuationdb(fAttenuation); // to ensure that it is unsigned

	// Get the effects level value from the dialog box.
	GetDlgItemText(IDC_WETMIX, szStr, sizeof(szStr) / sizeof(szStr[0]));
#ifdef UNICODE 
	wcstombs(strTmp, (const wchar_t *) szStr, sizeof(strTmp)); 
	dwWetmix = static_cast<DWORD>(atof(strTmp)); 
#else 
	dwWetmix = static_cast<DWORD>(atof(szStr));
#endif 

	// Make sure wet mix value is valid.
	if (dwWetmix > 100)
	{
		if (::LoadString(_Module.GetResourceInstance(), IDS_MIXRANGEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
		{
			MessageBox(szStr);
		}
		return E_FAIL;
	}
	else
		fWetmix = static_cast<double>(dwWetmix) / 100.0L;

	// update the registry
	CRegKey key;
	LONG    lResult;

	// Write the wet mix value to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		lResult = key.SetDWORDValue( kszPrefsWetmix, dwWetmix );
	}

	// Write the attenuation value to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		lResult = key.SetDWORDValue( kszPrefsAttenuation, dwAttenuation );
	}

	// Write the filter filename to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		// Get the filter file name from the dialog box.
		GetDlgItemText(IDC_FILTERFILELABEL, szFilterFileName, sizeof(szFilterFileName) / sizeof(szFilterFileName[0]));

		lResult = key.SetStringValue( kszPrefsFilterFileName, szFilterFileName );
	}

	// update the plug-in
	if (m_spConvolver)
	{
		m_spConvolver->put_wetmix(fWetmix);
		m_spConvolver->put_attenuation(fAttenuation);
		m_spConvolver->put_filterfilename(szFilterFileName);
	}

	m_bDirty = FALSE; // Tell the property page to disable Apply.

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::OnInitDialog
//

LRESULT CConvolverPropPage::OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	DWORD  dwWetmix				= 50;						// Default wet mix DWORD.
	double fWetmix				=  0.50;					// Default wet mix double.
	DWORD  dwAttenuation		= MAX_ATTENUATION * 100;	// Default attenuation DWORD (offset, as DWORD unsigned)
	double fAttenuation			= 0.0;						// Default attenuation double
	TCHAR*  szFilterFileName	= TEXT("");
	CRegKey key;
	LONG    lResult = 0;
	DWORD   dwValue = 0;

	// read from plug-in if it is available
	if (m_spConvolver)
	{
		m_spConvolver->get_wetmix(&fWetmix);
		// Convert wet mix from double to DWORD.
		dwWetmix = static_cast<DWORD>(fWetmix * 100);

		m_spConvolver->get_attenuation(&fAttenuation);
		dwAttenuation = m_spConvolver->encode_Attenuationdb(fAttenuation);

		m_spConvolver->get_filterfilename(&szFilterFileName);
	}	
	else // otherwise read from registry
	{
		lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
		if (ERROR_SUCCESS == lResult)
		{
			// Read the wet mix value.
			lResult = key.QueryDWORDValue(kszPrefsWetmix, dwValue );
			if (ERROR_SUCCESS == lResult)
			{
				dwWetmix = dwValue;
			}

			// Read the attenuation value.
			lResult = key.QueryDWORDValue(kszPrefsAttenuation, dwValue );
			if (ERROR_SUCCESS == lResult)
			{
				dwAttenuation = dwValue;
				fAttenuation = m_spConvolver->decode_Attenuationdb(dwAttenuation);
			}

			TCHAR szValue[MAX_PATH]	= TEXT("");
			ULONG ulMaxPath			= MAX_PATH;

			// Read the filter filename value.
			lResult = key.QueryStringValue(kszPrefsFilterFileName, szValue, &ulMaxPath );
			if (ERROR_SUCCESS == lResult)
			{
				_tcsncpy(szFilterFileName, szValue, ulMaxPath);
			}
		}
	}

	DisplayFilterFormat(szFilterFileName);

	TCHAR   szStr[MAXSTRING];

	// Display the effect level.
	_stprintf(szStr, _T("%u"), dwWetmix);
	SetDlgItemText(IDC_WETMIX, szStr);

	// Display the attenuation.
	_stprintf(szStr, _T("%.1f"), fAttenuation);
	SetDlgItemText(IDC_ATTENUATION, szStr);

	return 0;
}

LRESULT CConvolverPropPage::OnEnChangeWetmix(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{

	SetDirty(TRUE); // Enable Apply.

	return 0;
}

// This only sets the file name

LRESULT CConvolverPropPage::OnBnClickedGetfilter(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	TCHAR szFilterFileName[MAX_PATH]	= TEXT("");
	TCHAR szFilterPath[MAX_PATH]		= TEXT("");
	HRESULT hr = ERROR_SUCCESS;

	// Setup the OPENFILENAME structure
	OPENFILENAME ofn = { sizeof(OPENFILENAME), hWndCtl, NULL,
		TEXT("Wave Files\0*.wav\0PCM Files\0*.pcm\0All Files\0*.*\0\0"), NULL,
		0, 1, szFilterFileName, MAX_PATH, NULL, 0, szFilterPath,
		TEXT("Get filter file"),
		OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_READONLY, 
		0, 0, TEXT(".wav"), 0, NULL, NULL };

	SetDlgItemText( IDC_STATUS, TEXT("Filter filename...") );

	// Display the SaveFileName dialog. Then, try to load the specified file
	if( TRUE != GetSaveFileName( &ofn ) )
	{
		SetDlgItemText( IDC_STATUS, TEXT("Get filter aborted.") );
		return CommDlgExtendedError();
	}

	SetDlgItemText( IDC_FILTERFILELABEL, szFilterFileName );

	hr = DisplayFilterFormat(szFilterFileName);

	SetDirty(TRUE);

	return ERROR_SUCCESS;
}

LRESULT CConvolverPropPage::OnEnChangeAttenuation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	SetDirty(True);

	return 0;
}

LRESULT CConvolverPropPage::OnBnClickedButtonCalculateoptimumattenuation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	double	fAttenuation = 0;
	HRESULT hr = m_spConvolver->calculateOptimumAttenuation(fAttenuation);
	if (FAILED(hr))
		return hr;

	// Display the attenuation.
	TCHAR   szStr[MAXSTRING] = { 0 };
	_stprintf(szStr, _T("%.1f"), fAttenuation);
	SetDlgItemText(IDC_ATTENUATION, szStr);

	SetDirty(TRUE);

	return hr;
}
