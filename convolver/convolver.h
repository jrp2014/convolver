/////////////////////////////////////////////////////////////////////////////
//
// convolver.h : Declaration of CConvolver
//
// Note: Requires DirectX 8 SDK or later.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
/////////////////////////////////////////////////////////////////////////////
  
#ifndef __CCONVOLVER_H_
#define __CCONVOLVER_H_

// Pull in Common DX classes
#include "Common\dxstdafx.h"

#include "resource.h"
#include <mediaobj.h>       // The IMediaObject header from the DirectX SDK.
#include "wmpservices.h"    // The header containing the WMP interface definitions.

const DWORD UNITS = 10000000;  // 1 sec = 1 * UNITS
const DWORD MAXSTRING = 1024;

const MAX_FILTER_SIZE = 100000000; // Max impulse size.  1024 might be a better choice

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\DSP Plugin");
const TCHAR kszPrefsDelayTime[] = _T("DelayTime");
const TCHAR kszPrefsWetmix[] = _T("Wetmix");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");


/////////////////////////////////////////////////////////////////////////////
// IConvolver
/////////////////////////////////////////////////////////////////////////////

// {E68732FA-624D-40B4-917C-DE6B8876AC5B}
DEFINE_GUID(CLSID_Convolver, 0xe68732fa, 0x624d, 0x40b4, 0x91, 0x7c, 0xde, 0x6b, 0x88, 0x76, 0xac, 0x5b);

interface __declspec(uuid("{9B102F5D-8E2C-41F2-9256-2D3CA76FBE35}")) IConvolver : IUnknown
{
public:

	virtual HRESULT STDMETHODCALLTYPE get_wetmix(double *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_wetmix(double newVal) = 0;

	virtual HRESULT STDMETHODCALLTYPE get_filterfilename(TCHAR* *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_filterfilename(TCHAR* newVal) = 0;

	virtual HRESULT STDMETHODCALLTYPE get_filterformat(WAVEFORMATEX *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_filterformat(WAVEFORMATEX newVal) = 0;

};


/////////////////////////////////////////////////////////////////////////////
// CConvolver
/////////////////////////////////////////////////////////////////////////////

class ATL_NO_VTABLE CConvolver : 
    public CComObjectRootEx<CComMultiThreadModel>,
    public CComCoClass<CConvolver, &CLSID_Convolver>,
    public IConvolver,
    public IMediaObject,
    public IWMPPluginEnable,
    public ISpecifyPropertyPages
{
public:
    CConvolver();
    virtual ~CConvolver();

DECLARE_REGISTRY_RESOURCEID(IDR_CONVOLVER)

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CConvolver)
    COM_INTERFACE_ENTRY(IConvolver)
    COM_INTERFACE_ENTRY(IMediaObject)
    COM_INTERFACE_ENTRY(IWMPPluginEnable)
    COM_INTERFACE_ENTRY(ISpecifyPropertyPages)
END_COM_MAP()

    // CComCoClass Overrides
    HRESULT FinalConstruct();
    void    FinalRelease();

    // IConvolver methods
	STDMETHOD(get_wetmix)(double *pVal);
	STDMETHOD(put_wetmix)(double newVal);

	STDMETHOD(get_filterfilename)(TCHAR *pVal[]);
	STDMETHOD(put_filterfilename)(TCHAR newVal[]);

	STDMETHOD(get_filterformat)(WAVEFORMATEX *pVal);
	STDMETHOD(put_filterformat)(WAVEFORMATEX newVal);


    // IMediaObject methods
    STDMETHOD( GetStreamCount )( 
                   DWORD *pcInputStreams,
                   DWORD *pcOutputStreams
                   );
    
    STDMETHOD( GetInputStreamInfo )( 
                   DWORD dwInputStreamIndex,
                   DWORD *pdwFlags
                   );
    
    STDMETHOD( GetOutputStreamInfo )( 
                   DWORD dwOutputStreamIndex,
                   DWORD *pdwFlags
                   );
    
    STDMETHOD( GetInputType )( 
                   DWORD dwInputStreamIndex,
                   DWORD dwTypeIndex,
                   DMO_MEDIA_TYPE *pmt
                   );
    
    STDMETHOD( GetOutputType )( 
                   DWORD dwOutputStreamIndex,
                   DWORD dwTypeIndex,
                   DMO_MEDIA_TYPE *pmt
                   );
    
    STDMETHOD( SetInputType )( 
                   DWORD dwInputStreamIndex,
                   const DMO_MEDIA_TYPE *pmt,
                   DWORD dwFlags
                   );
    
    STDMETHOD( SetOutputType )( 
                   DWORD dwOutputStreamIndex,
                   const DMO_MEDIA_TYPE *pmt,
                   DWORD dwFlags
                   );
    
    STDMETHOD( GetInputCurrentType )( 
                   DWORD dwInputStreamIndex,
                   DMO_MEDIA_TYPE *pmt
                   );
    
    STDMETHOD( GetOutputCurrentType )( 
                   DWORD dwOutputStreamIndex,
                   DMO_MEDIA_TYPE *pmt
                   );
    
    STDMETHOD( GetInputSizeInfo )( 
                   DWORD dwInputStreamIndex,
                   DWORD *pcbSize,
                   DWORD *pcbMaxLookahead,
                   DWORD *pcbAlignment
                   );
    
    STDMETHOD( GetOutputSizeInfo )( 
                   DWORD dwOutputStreamIndex,
                   DWORD *pcbSize,
                   DWORD *pcbAlignment
                   );
    
    STDMETHOD( GetInputMaxLatency )( 
                   DWORD dwInputStreamIndex,
                   REFERENCE_TIME *prtMaxLatency
                   );
    
    STDMETHOD( SetInputMaxLatency )( 
                   DWORD dwInputStreamIndex,
                   REFERENCE_TIME rtMaxLatency
                   );
    
    STDMETHOD( Flush )( void );
    
    STDMETHOD( Discontinuity )( 
                   DWORD dwInputStreamIndex
                   );
    
    STDMETHOD( AllocateStreamingResources )( void );
    
    STDMETHOD( FreeStreamingResources )( void );
    
    STDMETHOD( GetInputStatus )( 
                   DWORD dwInputStreamIndex,
                   DWORD *pdwFlags
                   );
    
    STDMETHOD( ProcessInput )( 
                   DWORD dwInputStreamIndex,
                   IMediaBuffer *pBuffer,
                   DWORD dwFlags,
                   REFERENCE_TIME rtTimestamp,
                   REFERENCE_TIME rtTimelength
                   );
    
    STDMETHOD( ProcessOutput )( 
                   DWORD dwFlags,
                   DWORD cOutputBufferCount,
                   DMO_OUTPUT_DATA_BUFFER *pOutputBuffers,
                   DWORD *pdwStatus
                   );

    STDMETHOD( Lock )( LONG bLock );

    // Note: need to override CComObjectRootEx::Lock to avoid
    // ambiguity with IMediaObject::Lock. The override just
    // calls through to the base class implementation.

    // CComObjectRootEx overrides
    void Lock()
    {
        CComObjectRootEx<CComMultiThreadModel>::Lock();
    }

    // IWMPPluginEnable methods
    STDMETHOD( SetEnable )( BOOL fEnable );
    STDMETHOD( GetEnable )( BOOL *pfEnable );

    // ISpecifyPropertyPages methods
    STDMETHOD( GetPages )(CAUUID *pPages);
    
private:
    HRESULT DoProcessOutput(
                BYTE *pbOutputData,             // Pointer to the output buffer
                const BYTE *pbInputData,        // Pointer to the input buffer
                DWORD *cbBytesProcessed);       // Number of bytes processed
    HRESULT ValidateMediaType(
                const DMO_MEDIA_TYPE *pmtTarget,    // target media type to verify
                const DMO_MEDIA_TYPE *pmtPartner);  // partner media type to verify

	void FillBufferWithSilence(WAVEFORMATEX *pWfex);

    DMO_MEDIA_TYPE          m_mtInput;          // Stores the input format structure
    DMO_MEDIA_TYPE          m_mtOutput;         // Stores the output format structure

    CComPtr<IMediaBuffer>   m_spInputBuffer;    // Smart pointer to the input buffer object
    BYTE*                   m_pbInputData;      // Pointer to the data in the input buffer
    DWORD                   m_cbInputLength;    // Length of the data in the input buffer
   
    bool                    m_bValidTime;       // Is timestamp valid?
    REFERENCE_TIME          m_rtTimestamp;      // Stores the input buffer timestamp

	double					m_fWetMix;			// percentage of effect
	double					m_fDryMix;			// percentage of dry signal

	TCHAR					m_szFilterFileName[MAX_PATH];
	WAVEFORMATEX			m_WfexFilterFormat;	// The format of the filter file
	float  **m_ppfloatFilter;					// h(channel, n)
	float  **m_ppfloatSampleBuffer;				// xi(channel, n)
	float  *m_pfloatSampleBufferChannelCopy;
	float  **m_ppfloatOutputBuffer;				// y(channel, n)

	long  m_cFilterLength;						// Filter size in samples
	long  m_c2xPaddedFilterLength;				// 2^n, padded with zeros for radix 2 FFT
	long  m_nSampleBufferIndex;					// placeholder

//    DWORD					m_dwDelayTime;		// Delay time
    BOOL                    m_bEnabled;         // TRUE if enabled

//	long  m_cbDelayBuffer;						// Count of bytes in delay buffer
//	BYTE*  m_pbDelayPointer;					// Movable pointer to delay buffer
//	BYTE*  m_pbDelayBuffer;						// Movable pointer to the head of the delay buffer
//	int	   m_iDelayOffset;						// Pointer into the delay buffer

};

#endif //__CCONVOLVER_H_
