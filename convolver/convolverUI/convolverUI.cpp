/////////////////////////////////////////////////////////////////////////////
//
// convolverUI.cpp : Implementation of CConvolverUI
// Copyright (c) Microsoft Corporation. All rights reserved.
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "convolverUI.h"
#include "CPluginWindow.h"
#include "CPropertyDialog.h"
#include "convolverWMP\convolver.h"

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::CConvolverUI
// Constructor

CConvolverUI::CConvolverUI()
{
    m_pPluginWindow = NULL;
    lstrcpyn(m_szPluginText, _T("convolverUI Plugin"), sizeof(m_szPluginText) / sizeof(m_szPluginText[0]));
    m_dwAdviseCookie = 0;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::~CConvolverUI
// Destructor

CConvolverUI::~CConvolverUI()
{
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI:::FinalConstruct
// Called when an plugin is first loaded. Use this function to do one-time
// intializations that could fail instead of doing this in the constructor,
// which cannot return an error.

HRESULT CConvolverUI::FinalConstruct()
{
    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI:::FinalRelease
// Called when a plugin is unloaded. Use this function to free any
// resources allocated in FinalConstruct.

void CConvolverUI::FinalRelease()
{
    ReleaseCore();
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::SetCore
// Set WMP core interface

HRESULT CConvolverUI::SetCore(IWMPCore *pCore)
{
    HRESULT hr = S_OK;

    // release any existing WMP core interfaces
    ReleaseCore();

    // If we get passed a NULL core, this  means
    // that the plugin is being shutdown.

    if (pCore == NULL)
    {
        return S_OK;
    }

    m_spCore = pCore;

    // connect up the event interface
    CComPtr<IConnectionPointContainer>  spConnectionContainer;

    hr = m_spCore->QueryInterface( &spConnectionContainer );

    if (SUCCEEDED(hr))
    {
        hr = spConnectionContainer->FindConnectionPoint( __uuidof(IWMPEvents), &m_spConnectionPoint );
    }

    if (SUCCEEDED(hr))
    {
        hr = m_spConnectionPoint->Advise( GetUnknown(), &m_dwAdviseCookie );

        if ((FAILED(hr)) || (0 == m_dwAdviseCookie))
        {
            m_spConnectionPoint = NULL;
        }
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::ReleaseCore
// Release WMP core interfaces

void CConvolverUI::ReleaseCore()
{
    if (m_spConnectionPoint)
    {
        if (0 != m_dwAdviseCookie)
        {
            m_spConnectionPoint->Unadvise(m_dwAdviseCookie);
            m_dwAdviseCookie = 0;
        }
        m_spConnectionPoint = NULL;
    }

    if (m_spCore)
    {
        m_spCore = NULL;
    }
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::Create
// Create window for plugin

HRESULT CConvolverUI::Create(HWND hwndParent, HWND *phwndWindow)
{
    if (NULL == phwndWindow)
    {
        return E_UNEXPECTED;
    }

    m_pPluginWindow = new CPluginWindow(this);
    if (NULL == m_pPluginWindow)
    {
        return E_OUTOFMEMORY;
    }

    // create the plugin window
    RECT rcPos = { 0, 0, 100, 100};
    HWND hWnd = m_pPluginWindow->Create(hwndParent, rcPos, _T("convolverUI Plugin"));
    if (NULL == hWnd)
    {
        delete m_pPluginWindow;
        m_pPluginWindow = NULL;
        return E_FAIL;
    }

    *phwndWindow = hWnd;

    return S_OK;
}
/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::Destroy
// Destroy window for plugin

HRESULT CConvolverUI::Destroy()
{
    if (NULL == m_pPluginWindow)
    {
        return E_UNEXPECTED;
    }

    m_pPluginWindow->DestroyWindow();
    delete m_pPluginWindow;
    m_pPluginWindow = NULL;

    return S_OK;
}
/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::TranslateAccelerator
// Handle TranslateAccelerator for plugin

HRESULT CConvolverUI::TranslateAccelerator(LPMSG lpmsg)
{
    if (NULL == m_pPluginWindow)
    {
        return E_UNEXPECTED;
    }

    if (NULL == lpmsg)
    {
        return E_POINTER;
    }

    return S_OK;
}
/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::DisplayPropertyPage
// Display property page for plugin

HRESULT CConvolverUI::DisplayPropertyPage(HWND hwndParent)
{
    CPropertyDialog dialog(this);

    dialog.DoModal(hwndParent);

    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::GetProperty
// Get plugin property

HRESULT CConvolverUI::GetProperty(const WCHAR *pwszName, VARIANT *pvarProperty)
{
    if (NULL == pvarProperty)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;

    if (0 == lstrcmpiW(pwszName, PLUGIN_SEPARATEWINDOW_DEFAULTWIDTH ))
    {
        pvarProperty->vt = VT_I4;
        pvarProperty->lVal = 100;
    }
    else if (0 == lstrcmpiW(pwszName, PLUGIN_SEPARATEWINDOW_DEFAULTHEIGHT))
    {
        pvarProperty->vt = VT_I4;
        pvarProperty->lVal = 100;
    }
    else
    {
        hr = E_NOTIMPL;
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolverUI::SetProperty
// Set plugin property

HRESULT CConvolverUI::SetProperty(const WCHAR *pwszName, const VARIANT *pvarProperty)
{
    return E_NOTIMPL;
}
