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

#include "convolution\config.h"
#include "stdafx.h"
#include <stdio.h>
#include "convolverWMP\convolver.h"
#include "convolverWMP\convolverPropPage.h"
#include "debugging\fastTiming.h"
#include "convolution\waveformat.h"
#include "convolverWMP\version.h"
#include <exception>
#include "convolution\dither.h"



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
	m_spConvolver = NULL;

	if(NULL == ppUnk)
		return E_POINTER;

	for (DWORD i = 0; i < nObjects; ++i)
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

	if (m_spConvolver)
	{
		try
		{
			std::string description = "";
			hr = m_spConvolver->get_filter_description(&description);
			if (FAILED(hr))
			{
				SetDlgItemText( IDC_STATUS, TEXT("Get valid config file or filter sound file."));
				return hr;
			}
			SetDlgItemText( IDC_STATUS, CA2CT(description.c_str()));
		}
		catch (std::exception& error)
		{
			SetDlgItemText( IDC_STATUS, CA2CT(error.what()));
			hr = E_FAIL;
		}
		catch (...) // creating m_Convolution might throw
		{

			SetDlgItemText( IDC_STATUS, TEXT("Failed to load config or filter sound file.") );
			hr = E_FAIL;
		}
	}
	else
	{
		SetDlgItemText( IDC_STATUS, TEXT("Convolution plug-in initialization failed.") );
		return E_FAIL;
	}

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::Apply
//

STDMETHODIMP CConvolverPropPage::Apply(void)
{
	TCHAR   szStr[MAXSTRING] = { 0 };											// Temporary string
#ifdef UNICODE
	CHAR strTmp[2*(sizeof(szStr)+1)];	// SIZE equals (2*(sizeof(tstr)+1)). This ensures enough
	// room for the multibyte characters if they are two
	// bytes long and a terminating null character.
#endif
	float	fAttenuation = 0.0;													// Initialize a float for the attenuation
	DWORD   dwAttenuation = m_spConvolver->encode_Attenuationdb(fAttenuation);  // Encoding necessary as DWORD is an unsigned long
	TCHAR	szFilterFileName[MAX_PATH]	= { 0 };
	DWORD	nPartitions = 0;													// Initialize a DWORD for the number of partitions
																				// to be used in the convolution algorithm
																				// Get the attenuation value from the dialog box.
	unsigned int nPlanningRigour =
#ifdef FFTW
		1;		// Set "Measure" as the default
#else
		0;
#endif

	unsigned int nDither = 0;
	unsigned int nNoiseShaping = 0;

	try
	{
		GetDlgItemText(IDC_ATTENUATION, szStr, sizeof(szStr) / sizeof(szStr[0]));
		// atof does not work with Unicode
		// TODO:: redo this in C++ style, with stringstream
#ifdef UNICODE
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

		// Get the number of partitions value from the dialogue box.
		nPartitions = GetDlgItemInt(IDC_PARTITIONS, NULL, FALSE); 

		// Get the planning rigour from the dialogue box
		//nPlanningRigour = GetDlgItemInt(IDC_COMBOPLANNINGRIGOUR, NULL, FALSE); // Doesn't work
		TCHAR* szMeasure = new TCHAR[PlanningRigour::nStrLen];
		GetDlgItemText(IDC_COMBOPLANNINGRIGOUR, szMeasure, PlanningRigour::nStrLen);
		nPlanningRigour = PlanningRigour::Lookup(szMeasure);
		delete [] szMeasure;

		// Get the dither from the dialogue box
		// nDither = GetDlgItemInt(IDC_COMBODITHER, NULL, FALSE); // Doesn't work
		TCHAR* szDither = new TCHAR[Ditherer<BaseT>::nStrLen];
		GetDlgItemText(IDC_COMBODITHER, szDither, Ditherer<BaseT>::nStrLen);
		nDither = Ditherer<BaseT>::Lookup(szDither);
		delete [] szDither;

		// Get the shaping from the dialogue box
		// nDither = GetDlgItemInt(IDC_COMBODITHER, NULL, FALSE); // Doesn't work
		TCHAR* szShaping = new TCHAR[NoiseShaper<BaseT>::nStrLen];
		GetDlgItemText(IDC_COMBONOISESHAPING, szShaping, NoiseShaper<BaseT>::nStrLen);
		nNoiseShaping = NoiseShaper<BaseT>::Lookup(szShaping);
		delete [] szShaping;


		// update the registry
		CRegKey key;
		LONG    lResult;

		// Write the attenuation value to the registry.
		lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
		if (ERROR_SUCCESS == lResult)
		{
			lResult = key.SetDWORDValue( kszPrefsAttenuation, dwAttenuation );
			if (lResult != ERROR_SUCCESS)
			{
				SetDlgItemText( IDC_STATUS, TEXT("Failed to save attenuation to registry.") );
				return lResult;
			}
		}

		// Write the filter filename to the registry.
		lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
		if (ERROR_SUCCESS == lResult)
		{
			// Get the filter file name from the dialog box.
			GetDlgItemText(IDC_FILTERFILELABEL, szFilterFileName, sizeof(szFilterFileName) / sizeof(szFilterFileName[0]));

			lResult = key.SetStringValue( kszPrefsFilterFileName, szFilterFileName );
			if (lResult != ERROR_SUCCESS)
			{
				SetDlgItemText( IDC_STATUS, TEXT("Failed to save filename to registry.") );
				return lResult;
			}
		}

		// Write the number of partitions to the registry.
		lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
		if (ERROR_SUCCESS == lResult)
		{
			lResult = key.SetDWORDValue( kszPrefsPartitions, nPartitions );
			if (lResult != ERROR_SUCCESS)
			{
				SetDlgItemText( IDC_STATUS, TEXT("Failed to save number of partitions to registry") );
				return lResult;
			}
		}

		// Write the planning rigour to the registry.
		lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
		if (ERROR_SUCCESS == lResult)
		{
			lResult = key.SetDWORDValue( kszPrefsPlanningRigour, nPlanningRigour );
			if (lResult != ERROR_SUCCESS)
			{
				SetDlgItemText( IDC_STATUS, TEXT("Failed to save tuning rigour to registry") );
				return lResult;
			}
		}

		// Write the dither value to the registry.
		lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
		if (ERROR_SUCCESS == lResult)
		{
			lResult = key.SetDWORDValue( kszPrefsDither, nDither );
			if (lResult != ERROR_SUCCESS)
			{
				SetDlgItemText( IDC_STATUS, TEXT("Failed to save dither to registry") );
				return lResult;
			}
		}

		// Write the noise shaping value to the registry.
		lResult = key.Create(HKEY_CURRENT_USER, kszPrefsRegKey);
		if (ERROR_SUCCESS == lResult)
		{
			lResult = key.SetDWORDValue( kszPrefsNoiseShaping, nNoiseShaping );
			if (lResult != ERROR_SUCCESS)
			{
				SetDlgItemText( IDC_STATUS, TEXT("Failed to save noise shaping to registry") );
				return lResult;
			}
		}


		// update the plug-in
		if (m_spConvolver)
		{
			HRESULT hr;

			hr = m_spConvolver->put_attenuation(fAttenuation);
			if (FAILED(hr))
			{
				if (::LoadString(_Module.GetResourceInstance(), IDS_ATTENUATIONSAVEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
				{
					MessageBox(szStr);
				}
				return hr;
			}

			hr = m_spConvolver->put_filterfilename(szFilterFileName);
			if (FAILED(hr))
			{
				if (::LoadString(_Module.GetResourceInstance(), IDS_FILTERSAVEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
				{
					MessageBox(szStr);
				}
				return hr;
			}

			hr = m_spConvolver->put_partitions(nPartitions);
			if (FAILED(hr))
			{
				if (::LoadString(_Module.GetResourceInstance(), IDS_PARTITIONSSAVEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
				{
					MessageBox(szStr);
				}
				return hr;
			}

			hr = m_spConvolver->put_planning_rigour(nPlanningRigour);
			if (FAILED(hr))
			{
				if (::LoadString(_Module.GetResourceInstance(), IDS_PLANNINGRIGOURSAVEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
				{
					MessageBox(szStr);
				}
				return hr;
			}

			hr = m_spConvolver->put_dither(nDither);
			if (FAILED(hr))
			{
				if (::LoadString(_Module.GetResourceInstance(), IDS_DITHERSAVEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
				{
					MessageBox(szStr);
				}
				return hr;
			}

			hr = m_spConvolver->put_noiseshaping(nNoiseShaping);
			if (FAILED(hr))
			{
				if (::LoadString(_Module.GetResourceInstance(), IDS_NOISESHAPINGSAVEERROR, szStr, sizeof(szStr) / sizeof(szStr[0])))
				{
					MessageBox(szStr);
				}
				return hr;
			}

		}
	}
	catch (std::exception& error)
	{
		SetDlgItemText( IDC_STATUS, CA2CT(error.what()));
		return E_FAIL;
	}
	catch (...) 
	{

		SetDlgItemText( IDC_STATUS, TEXT("Failed to apply settings.") );
		return E_FAIL;
	}

	SetDirty(FALSE); // Tell the property page to disable Apply.

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverProp::OnInitDialog
//

LRESULT CConvolverPropPage::OnInitDialog(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled)
{
	float fAttenuation		 = 0.0;									// Default attenuation float
	DWORD  dwAttenuation	 =										// Default attenuation DWORD (offset, as DWORD unsigned)
		m_spConvolver->encode_Attenuationdb(fAttenuation);
	TCHAR*  szFilterFileName = TEXT("");
	DWORD   nPartitions		 = 0;									// Default number of partitions
	CRegKey key;
	DWORD   dwValue			 = 0;
	unsigned int nPlanningRigour = 
#ifdef FFTW
		1;		// Set "Measure" as the default
#else
		0;
#endif
	unsigned int nDither = 0;
	unsigned int nNoiseShaping = 0;

	try
	{
		// May throw
		version v(TEXT("convolverWMP.dll"));
		SetDlgItemText(IDC_STATIC_VERSION,  (TEXT("Version ") + v.get_product_version()).c_str());

		for(unsigned int i=0; i<PlanningRigour::nDegrees; ++i)
		{
			SendDlgItemMessage(IDC_COMBOPLANNINGRIGOUR, CB_ADDSTRING, 0, (LPARAM)PlanningRigour::Rigour[i]);
		}
		SendDlgItemMessage(IDC_COMBOPLANNINGRIGOUR, CB_SETCURSEL, nPlanningRigour, 0);

		for(unsigned int i=0; i<Ditherer<BaseT>::nDitherers; ++i)
		{
			SendDlgItemMessage(IDC_COMBODITHER, CB_ADDSTRING, 0, (LPARAM)Ditherer<BaseT>::Description[i]);
		}
		SendDlgItemMessage(IDC_COMBODITHER, CB_SETCURSEL, nDither, 0);

		for(unsigned int i=0; i<NoiseShaper<BaseT>::nNoiseShapers; ++i)
		{
			SendDlgItemMessage(IDC_COMBONOISESHAPING, CB_ADDSTRING, 0, (LPARAM)NoiseShaper<BaseT>::Description[i]);
		}
		SendDlgItemMessage(IDC_COMBONOISESHAPING, CB_SETCURSEL, nNoiseShaping, 0);


		// read from plug-in if it is available
		if (m_spConvolver)
		{
			HRESULT hr = S_OK;
			hr = m_spConvolver->get_attenuation(&fAttenuation);
			if(FAILED(hr)) return hr;
			dwAttenuation = m_spConvolver->encode_Attenuationdb(fAttenuation);

			hr = m_spConvolver->get_filterfilename(&szFilterFileName);
			if(FAILED(hr)) return hr;

			hr = m_spConvolver->get_partitions(&nPartitions);
			if(FAILED(hr)) return hr;
			hr = m_spConvolver->get_planning_rigour(&nPlanningRigour);
			if(FAILED(hr)) return hr;
			hr = m_spConvolver->get_dither(&nDither);
			if(FAILED(hr)) return hr;
			hr = m_spConvolver->get_noiseshaping(&nNoiseShaping);
			if(FAILED(hr)) return hr;
		}	
		else // otherwise read from registry
		{
			LONG lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
			if (ERROR_SUCCESS == lResult)
			{
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

				// Read the number of partitions
				lResult = key.QueryDWORDValue(kszPrefsPartitions, dwValue );
				if (ERROR_SUCCESS == lResult)
				{
					nPartitions = dwValue;
				}
				// Read the planning rigour
				lResult = key.QueryDWORDValue(kszPrefsPlanningRigour, dwValue );
				if (ERROR_SUCCESS == lResult)
				{
					nPlanningRigour = dwValue;
				}
				// Read the dither
				lResult = key.QueryDWORDValue(kszPrefsDither, dwValue );
				if (ERROR_SUCCESS == lResult)
				{
					nDither = dwValue;
				}
				// Read the noise shaping value
				lResult = key.QueryDWORDValue(kszPrefsNoiseShaping, dwValue );
				if (ERROR_SUCCESS == lResult)
				{
					nNoiseShaping = dwValue;
				}
			}
		}

		DisplayFilterFormat(szFilterFileName);

		TCHAR szStr[MAXSTRING];

		// Display the attenuation.
		_stprintf(szStr, _T("%.1f"), fAttenuation);
		SetDlgItemText(IDC_ATTENUATION, szStr);

		// Display the number of partitions
		_stprintf(szStr, _T("%u"), nPartitions);
		SetDlgItemText(IDC_PARTITIONS, szStr);

		// Display the planning rigour
		SendDlgItemMessage(IDC_COMBOPLANNINGRIGOUR, CB_SETCURSEL, nPlanningRigour, 0);

		// Display the dither
		SendDlgItemMessage(IDC_COMBODITHER, CB_SETCURSEL, nDither, 0);

		// Display the noise shaping
		SendDlgItemMessage(IDC_COMBONOISESHAPING, CB_SETCURSEL, nNoiseShaping, 0);
	}
	catch (std::exception& error)
	{
		SetDlgItemText( IDC_STATUS, CA2CT(error.what()));
		return TRUE;    // let the system set the focus
	}
	catch (...) 
	{

		SetDlgItemText( IDC_STATUS, TEXT("Failed to initialize dialogue.") );
		return TRUE;    // let the system set the focus
	}

	return TRUE;    // let the system set the focus
}

// This only sets the file name

LRESULT CConvolverPropPage::OnBnClickedGetfilter(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled)
{
	TCHAR szFilterFileName[MAX_PATH]	= TEXT("");
	TCHAR szFilterPath[MAX_PATH]		= TEXT("");
	HRESULT hr = S_OK;

	try
	{
		// Setup the OPENFILENAME structure
		OPENFILENAME ofn = { sizeof(OPENFILENAME), hWndCtl, NULL,
			TEXT("Filter sound/Config text files\0*.txt;*.cfg;*.WAV;*.PCM;*.DBL\0All Files\0*.*\0\0"), NULL,
			0, 1, szFilterFileName, MAX_PATH, NULL, 0, szFilterPath,
			TEXT("Get filter file"),
			OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_READONLY | OFN_HIDEREADONLY | OFN_EXPLORER, 
			0, 0, TEXT(".txt"), 0, NULL, NULL };

		SetDlgItemText( IDC_STATUS, TEXT("Configuration filename...") );

		// Display the SaveFileName dialog. Then, try to load the specified file
		if( TRUE != GetOpenFileName( &ofn ) )
		{
			//SetDlgItemText( IDC_STATUS, TEXT("Get filter aborted.") );
			//return CommDlgExtendedError();
			return 0;
		}

		SetDlgItemText( IDC_FILTERFILELABEL, szFilterFileName );

		hr = m_spConvolver->put_filterfilename(szFilterFileName);
		if(FAILED(hr))
		{
			SetDlgItemText( IDC_STATUS, TEXT("Failed to save filter filename.") );
			return 0;
		}

		hr = DisplayFilterFormat(szFilterFileName);
	}
	catch (std::exception& error)
	{
		SetDlgItemText( IDC_STATUS, CA2CT(error.what()));
		return 0;
	}
	catch (...) 
	{
		SetDlgItemText( IDC_STATUS, TEXT("Failed to load filter config file.") );
		return 0;
	}

	hr = Apply(); // apply the filter immediately; don't wait for apply button to be set, as we have 
	if(FAILED(hr))
	{
		return 0;
	}

	return 0;
}

LRESULT CConvolverPropPage::OnEnChangeAttenuation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	SetDirty(True);

	return 0;
}

LRESULT CConvolverPropPage::OnBnClickedButtonCalculateoptimumattenuation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	float	fAttenuation = 0;
	HRESULT hr S_OK;

	try
	{
		if(IsPageDirty() == S_OK) // Make sure that the currently-visible settings are used
		{
			hr = Apply();
			if (FAILED(hr))
			{
				//SetDlgItemText( IDC_STATUS, TEXT("Failed to apply settings.") ); // Apply supplies its own diagnostics
				return 0;
			}
		}

		double fElapsed = 0;
		apHiResElapsedTime t;
		hr = m_spConvolver->calculateOptimumAttenuation(fAttenuation);
		fElapsed = t.sec();
		if (FAILED(hr))
		{
			SetDlgItemText( IDC_STATUS, TEXT("Failed to calculate optimum attenuation. Check filter filename.") );  // TODO: internationalize
			return 0;
		}

		// Display the attenuation.
		TCHAR   szStr[MAXSTRING] = { 0 };
		_stprintf(szStr, _T("%.1f"), fAttenuation);
		SetDlgItemText(IDC_ATTENUATION, szStr);

		SetDirty(TRUE);

		// Display calculation time
		_stprintf(szStr, _T("Elapsed calculation time: %.2f seconds"), fElapsed);
		SetDlgItemText(IDC_STATUS, szStr);
	}
	catch (std::exception& error)
	{
		SetDlgItemText( IDC_STATUS, CA2CT(error.what()));
		return 0;
	}
	catch (...) 
	{

		SetDlgItemText( IDC_STATUS, TEXT("Failed to calculate optimum attenuation.") );
		return 0;
	}

	return 0;
}

LRESULT CConvolverPropPage::OnEnChangePartitions(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	SetDirty(TRUE);

	return 0;
}

LRESULT CConvolverPropPage::OnCbnSelendokCombofftwmeasure(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	SetDirty(TRUE);

	return 0;
}


LRESULT CConvolverPropPage::OnCbnSelendokCombodither(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{

	SetDirty(TRUE);

	return 0;
}


LRESULT CConvolverPropPage::OnCbnSelendokCombonoiseshaping(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	SetDirty(TRUE);

	return 0;
}

LRESULT CConvolverPropPage::OnCbnSelchangeCombodither(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/)
{
	// TODO: Add your control notification handler code here

	return 0;
}
