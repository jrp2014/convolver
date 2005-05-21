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


	// Get the delay time value from the dialog box.
	GetDlgItemText(IDC_DELAYTIME, szStr, sizeof(szStr) / sizeof(szStr[0]));

	dwDelayTime = atoi(szStr);

	// Make sure delay time is valid.
	if ((dwDelayTime < 10) || (dwDelayTime > 2000))
	{
		if (::LoadString(_Module.GetResourceInstance(), IDS_DELAYRANGEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
		{
			MessageBox(szStr);
		}

		return E_FAIL;
	}

	// Get the effects level value from the dialog box.
	GetDlgItemText(IDC_WETMIX, szStr, sizeof(szStr) / sizeof(szStr[0]));

	dwWetmix = atoi(szStr);

	// Make sure wet mix value is valid.
	if ((dwWetmix < 0) || (dwWetmix > 100))
	{
		if (::LoadString(_Module.GetResourceInstance(), IDS_MIXRANGEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
		{
			MessageBox(szStr);
		}

		return E_FAIL;
	}

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

	// update the plug-in
	if (m_spConvolver)
	{
		m_spConvolver->put_delay(dwDelayTime);

		// Convert the wet mix value from DWORD to double.
		fWetmix = (double)dwWetmix / 100;
		m_spConvolver->put_wetmix(fWetmix);
	}


	m_bDirty = FALSE; // Tell the property page to disable Apply.

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::OnChangeScale
//

LRESULT CConvolverPropPage::OnChangeDelay(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled){

	SetDirty(TRUE); // Enable Apply.
	
	return 0;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::OnInitDialog
//

LRESULT CConvolverPropPage::OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	DWORD  dwDelayTime = 1000;    // Default delay time.
	DWORD  dwWetmix = 50;         // Default wet mix DWORD.
	double fWetmix =  0.50;       // Default wet mix double.


	// read scale factor from plug-in if it is available
	if (m_spConvolver)
	{
		m_spConvolver->get_delay(&dwDelayTime);
		m_spConvolver->get_wetmix(&fWetmix);
		// Convert wet mix from double to DWORD.
		dwWetmix = (DWORD)(fWetmix * 100);
	}	
	else // otherwise read scale factor from registry
	{
		CRegKey key;
		LONG    lResult;

		lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
		if (ERROR_SUCCESS == lResult)
		{
			DWORD   dwValue = 0;

			// Read the delay time.
			lResult = key.QueryDWORDValue(kszPrefsDelayTime, dwValue );
			if (ERROR_SUCCESS == lResult)
			{
				dwDelayTime = dwValue;
			}

			// Read the wet mix value.
			lResult = key.QueryDWORDValue(kszPrefsWetmix, dwValue );
			if (ERROR_SUCCESS == lResult)
			{
				dwWetmix = dwValue;
			}
		}
	}


	TCHAR   szStr[MAXSTRING];

	// Display the delay time.
	_stprintf(szStr, _T("%u"), dwDelayTime);
	SetDlgItemText(IDC_DELAYTIME, szStr);

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
