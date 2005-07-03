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
// CConvolverPropPage.h : Declaration of CConvolverPropPage
//
/////////////////////////////////////////////////////////////////////////////

#ifndef __CCONVOLVERPROPPAGE_H_
#define __CCONVOLVERPROPPAGE_H_

#include "resource.h"
#include "convolver.h"

// {C4315842-44E6-42F5-8E0F-FDB07AF48EAC}
DEFINE_GUID(CLSID_ConvolverPropPage, 0xc4315842, 0x44e6, 0x42f5, 0x8e, 0xf, 0xfd, 0xb0, 0x7a, 0xf4, 0x8e, 0xac);

// {A87DA8E0-F517-4a13-A184-A5E573CB2EB5}
DEFINE_GUID(CLSID_ConvolverPropPageDMO, 0xa87da8e0, 0xf517, 0x4a13, 0xa1, 0x84, 0xa5, 0xe5, 0x73, 0xcb, 0x2e, 0xb5);

// {767AB4A0-CF52-4cb2-8715-07035A44E2C4}
DEFINE_GUID(CLSID_ConvolverPropPageDS, 
0x767ab4a0, 0xcf52, 0x4cb2, 0x87, 0x15, 0x7, 0x3, 0x5a, 0x44, 0xe2, 0xc4);


/////////////////////////////////////////////////////////////////////////////
// CConvolverPropPage
class ATL_NO_VTABLE CConvolverPropPage :
	public CComObjectRootEx<CComMultiThreadModel>,
#ifdef DMO
	public CComCoClass<CConvolverPropPage, &CLSID_ConvolverPropPageDMO>,
#else
	public CComCoClass<CConvolverPropPage, &CLSID_ConvolverPropPage>,
#endif
	public IPropertyPageImpl<CConvolverPropPage>,
	public CDialogImpl<CConvolverPropPage>
{
public:
	        CConvolverPropPage(); 
	virtual ~CConvolverPropPage(); 
	

	enum {IDD = IDD_CONVOLVERPROPPAGE};
	

DECLARE_REGISTRY_RESOURCEID(IDR_CONVOLVERPROPPAGE)

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CConvolverPropPage) 
	COM_INTERFACE_ENTRY(IPropertyPage)
END_COM_MAP()

BEGIN_MSG_MAP(CConvolverPropPage)
	COMMAND_HANDLER(IDC_WETMIX, EN_CHANGE, OnEnChangeWetmix)
	COMMAND_HANDLER(IDC_GETFILTER, BN_CLICKED, OnBnClickedGetfilter)
	COMMAND_HANDLER(IDC_ATTENUATION, EN_CHANGE, OnEnChangeAttenuation)
	COMMAND_HANDLER(IDC_BUTTON_CALCULATEOPTIMUMATTENUATION, BN_CLICKED, OnBnClickedButtonCalculateoptimumattenuation)
	CHAIN_MSG_MAP(IPropertyPageImpl<CConvolverPropPage>)
	MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
END_MSG_MAP()

    STDMETHOD(SetObjects)(ULONG nObjects, IUnknown** ppUnk);
    STDMETHOD(Apply)(void);

	LRESULT (OnInitDialog)(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

	STDMETHOD(DisplayFilterFormat)(TCHAR* szFilterFileName);

private:
    CComPtr<IConvolver> m_spConvolver;  // pointer to plug-in interface

public:
	LRESULT OnEnChangeWetmix(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBnClickedGetfilter(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEnChangeAttenuation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBnClickedButtonCalculateoptimumattenuation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

#endif // __CCONVOLVERPROPPAGE_H_
