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

