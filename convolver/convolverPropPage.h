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

/////////////////////////////////////////////////////////////////////////////
// CConvolverPropPage
class ATL_NO_VTABLE CConvolverPropPage :
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CConvolverPropPage, &CLSID_ConvolverPropPage>,
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
	CHAIN_MSG_MAP(IPropertyPageImpl<CConvolverPropPage>)
	MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
END_MSG_MAP()

    STDMETHOD(SetObjects)(ULONG nObjects, IUnknown** ppUnk);
    STDMETHOD(Apply)(void);

	LRESULT (OnInitDialog)(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    CComPtr<IConvolver> m_spConvolver;  // pointer to plug-in interface

public:
	LRESULT OnEnChangeWetmix(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnBnClickedGetfilter(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
	LRESULT OnEnChangeAttenuation(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

#endif // __CCONVOLVERPROPPAGE_H_
