/////////////////////////////////////////////////////////////////////////////
//
// CConvolverPropPage.h : Declaration of CConvolverPropPage
//
// Copyright (c) Microsoft Corporation. All rights reserved.
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
	CHAIN_MSG_MAP(IPropertyPageImpl<CConvolverPropPage>)
	COMMAND_HANDLER(IDC_DELAYTIME, EN_CHANGE, OnChangeDelay)
	MESSAGE_HANDLER(WM_INITDIALOG, OnInitDialog)
END_MSG_MAP()

    STDMETHOD(SetObjects)(ULONG nObjects, IUnknown** ppUnk);
    STDMETHOD(Apply)(void);

	LRESULT (OnChangeDelay)(WORD wNotifyCode, WORD wID, HWND hWndCtl, BOOL& bHandled);
	LRESULT (OnInitDialog)(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

private:
    CComPtr<IConvolver> m_spConvolver;  // pointer to plug-in interface
public:
	LRESULT OnEnChangeWetmix(WORD /*wNotifyCode*/, WORD /*wID*/, HWND /*hWndCtl*/, BOOL& /*bHandled*/);
};

#endif // __CCONVOLVERPROPPAGE_H_
