/////////////////////////////////////////////////////////////////////////////
//
// CConvolverPropPage.cpp : Implementation of the property page for CConvolver
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include <stdio.h>
#include "convolver.h"
#include "ConvolverPropPage.h"
#include ".\convolverproppage.h"

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

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::Apply
//

STDMETHODIMP CConvolverPropPage::Apply(void)
{
	TCHAR   szStr[MAXSTRING] = { 0 };
	DWORD   dwDelayTime = 1000;  // Initialize the delay time.
	DWORD   dwWetmix = 50;       // Initialize a DWORD for effect level.
	double  fWetmix = 0.50;      // Initialize a double for effect level.
	TCHAR szFilterFileName[MAX_PATH]	= TEXT("");
                  
 // atio does not work with Unicode
#ifdef UNICODE 
	CHAR strTmp[2*(sizeof(szStr)+1)]; // SIZE equals (2*(sizeof(tstr)+1)). This ensures enough
                       // room for the multibyte characters if they are two
                       // bytes long and a terminating null character.
    wcstombs(strTmp, (const wchar_t *) szStr, sizeof(strTmp)); 
    dwDelayTime = atoi(strTmp); 
#else 
	dwDelayTime = atoi(szStr);
#endif 

	// Get the effects level value from the dialog box.
	GetDlgItemText(IDC_WETMIX, szStr, sizeof(szStr) / sizeof(szStr[0]));
#ifdef UNICODE 
	wcstombs(strTmp, (const wchar_t *) szStr, sizeof(strTmp)); 
    dwWetmix = atoi(strTmp); 
#else 
	dwWetmix = atoi(szStr);
#endif 

	// Make sure wet mix value is valid.
	if ((dwWetmix < 0) || (dwWetmix > 100))
	{
		if (::LoadString(_Module.GetResourceInstance(), IDS_MIXRANGEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
		{
			MessageBox(szStr);
		}

		return E_FAIL;
	}

	// Get the filter file name from the dialog box.
	GetDlgItemText(IDC_FILTERFILELABEL, szFilterFileName, sizeof(szFilterFileName) / sizeof(szFilterFileName[0]));

	// update the registry
	CRegKey key;
	LONG    lResult;

	// Write the delay time value to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		lResult = key.SetDWORDValue( kszPrefsDelayTime, dwDelayTime );
	}

	// Write the wet mix value to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		lResult = key.SetDWORDValue( kszPrefsWetmix, dwWetmix );
	}

	// Write the filter filename to the registry.
	lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
	if (ERROR_SUCCESS == lResult)
	{
		lResult = key.SetStringValue( kszPrefsFilterFileName, szFilterFileName );
	}

	// update the plug-in
	if (m_spConvolver)
	{
		// Convert the wet mix value from DWORD to double.
		fWetmix = (double)dwWetmix / 100;
		m_spConvolver->put_wetmix(fWetmix);

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
	DWORD  dwWetmix = 50;         // Default wet mix DWORD.
	double fWetmix =  0.50;       // Default wet mix double.
	TCHAR*  szFilterFileName	= TEXT("");


	// read from plug-in if it is available
	if (m_spConvolver)
	{
		m_spConvolver->get_wetmix(&fWetmix);
		// Convert wet mix from double to DWORD.
		dwWetmix = (DWORD)(fWetmix * 100);
		
		m_spConvolver->get_filterfilename(&szFilterFileName);
		SetDlgItemText( IDC_FILTERFILELABEL, szFilterFileName );
	}	
	else // otherwise read from registry
	{
		CRegKey key;
		LONG    lResult;

		lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
		if (ERROR_SUCCESS == lResult)
		{
			DWORD   dwValue = 0;

			// Read the wet mix value.
			lResult = key.QueryDWORDValue(kszPrefsWetmix, dwValue );
			if (ERROR_SUCCESS == lResult)
			{
				dwWetmix = dwValue;
			}

			TCHAR szValue[MAX_PATH]	= TEXT("");
			ULONG ulMaxPath				= MAX_PATH;

			// Read the filter filename value.
			lResult = key.QueryStringValue(kszPrefsFilterFileName, szValue, &ulMaxPath );
			if (ERROR_SUCCESS == lResult)
			{
				_tcsncpy(szFilterFileName, szValue, ulMaxPath);
			}
		}
	}


	TCHAR   szStr[MAXSTRING];

	// Display the effect level.
	_stprintf(szStr, _T("%u"), dwWetmix);
	SetDlgItemText(IDC_WETMIX, szStr);


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

	SetDirty(TRUE);

	return ERROR_SUCCESS;
}
