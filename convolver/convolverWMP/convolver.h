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
// convolver.h : Declaration of CConvolver
//
// Note: Requires DirectX 8 SDK or later.
//
/////////////////////////////////////////////////////////////////////////////
  
#ifndef __CCONVOLVER_H_
#define __CCONVOLVER_H_

#include "resource.h"
#include <mediaobj.h>       // The IMediaObject header from the DirectX SDK.

#ifndef DMO
#include "wmpservices.h"    // The header containing the WMP interface definitions.
#endif

// Pull in Common DX classes
#include "Common\dxstdafx.h"

#include <time.h>

#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugStream.h"
#endif

#include "waveformat.h"
#include "convolution\convolution.h"

// FFT routines
#include "fft\fftsg_h.h"

const DWORD UNITS = 10000000;	// 1 sec = 1 * UNITS
const DWORD MAXSTRING = 1024;	// length

const DWORD MAX_FILTER_SIZE = 100000000; // Max impulse size.  1024 taps might be a better choice
const DWORD MAX_ATTENUATION = 1000; // dB

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\DSP Plugin");
const TCHAR kszPrefsAttenuation[] = _T("Attenuation");
const TCHAR kszPrefsWetmix[] = _T("Wetmix");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");

/////////////////////////////////////////////////////////////////////////////
// IConvolver
/////////////////////////////////////////////////////////////////////////////

#ifdef DMO
// {35708C4E-9CD1-4231-9B60-1D8BCDDCD391}
DEFINE_GUID(CLSID_ConvolverDMO,
			0x35708c4e, 0x9cd1, 0x4231, 0x9b, 0x60, 0x1d, 0x8b, 0xcd, 0xdc, 0xd3, 0x91);
#else
// {E68732FA-624D-40B4-917C-DE6B8876AC5B}
DEFINE_GUID(CLSID_Convolver,
			0xe68732fa, 0x624d, 0x40b4, 0x91, 0x7c, 0xde, 0x6b, 0x88, 0x76, 0xac, 0x5b);
#endif

interface __declspec(uuid("{9B102F5D-8E2C-41F2-9256-2D3CA76FBE35}")) IConvolver : IUnknown
{
public:

	virtual HRESULT STDMETHODCALLTYPE get_wetmix(double *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_wetmix(double newVal) = 0;

	virtual HRESULT STDMETHODCALLTYPE get_filterfilename(TCHAR* *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_filterfilename(TCHAR* newVal) = 0;

	virtual HRESULT STDMETHODCALLTYPE get_filterformat(WAVEFORMATEX *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_filterformat(WAVEFORMATEX newVal) = 0;

	virtual HRESULT STDMETHODCALLTYPE get_attenuation(double *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_attenuation(double newVal) = 0;

	virtual double	decode_Attenuationdb(const DWORD dwValue) = 0;
	virtual DWORD	encode_Attenuationdb(const double fValue) = 0;

	virtual HRESULT STDMETHODCALLTYPE calculateOptimumAttenuation(double& fAttenuation) = 0;

};


/////////////////////////////////////////////////////////////////////////////
// CConvolver
/////////////////////////////////////////////////////////////////////////////

// TODO: Could also implement the IMediaObjectInPlace interface, as an optimization
// See: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directshow/htm/imediaobjectinplaceinterface.asp

class ATL_NO_VTABLE CConvolver : 
    public CComObjectRootEx<CComMultiThreadModel>,
#ifdef DMO
	public CComCoClass<CConvolver, &CLSID_ConvolverDMO>,
#else
    public CComCoClass<CConvolver, &CLSID_Convolver>,
#endif
    public IConvolver,
    public IMediaObject,
#ifndef DMO
    public IWMPPluginEnable,
#endif
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
#ifndef DMO
    COM_INTERFACE_ENTRY(IWMPPluginEnable)
#endif
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

	STDMETHOD(get_attenuation)(double *pVal);
	STDMETHOD(put_attenuation)(double newVal);

	STDMETHOD (calculateOptimumAttenuation)(double& fAttenuation);

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


	// The following pair is needed because DWORD is unsigned
	double decode_Attenuationdb(const DWORD dwValue)
		{
			assert(dwValue <= (MAX_ATTENUATION + MAX_ATTENUATION) * 100);
			return static_cast<double>(dwValue) / 100.0L - MAX_ATTENUATION;
		}
	DWORD encode_Attenuationdb(const double fValue)
		{	assert (abs(fValue) <= MAX_ATTENUATION);
			return static_cast<DWORD>((fValue + MAX_ATTENUATION) * 100.0);
		}
    
private:
    HRESULT DoProcessOutput(
                BYTE *pbOutputData,             // Pointer to the output buffer
                const BYTE *pbInputData,        // Pointer to the input buffer
                DWORD *cbBytesProcessed);       // Number of bytes processed
    HRESULT ValidateMediaType(
                const DMO_MEDIA_TYPE *pmtTarget,    // target media type to verify
                const DMO_MEDIA_TYPE *pmtPartner);  // partner media type to verify

	//void FillBufferWithSilence(WAVEFORMATEX *pWfex); // TODO: Remove or make this do something useful

    DMO_MEDIA_TYPE          m_mtInput;          // Stores the input format structure
    DMO_MEDIA_TYPE          m_mtOutput;         // Stores the output format structure

    CComPtr<IMediaBuffer>   m_spInputBuffer;    // Smart pointer to the input buffer object
    BYTE*                   m_pbInputData;      // Pointer to the data in the input buffer
    DWORD                   m_cbInputLength;    // Length of the data in the input buffer
   
    bool                    m_bValidTime;       // Is timestamp valid?
    REFERENCE_TIME          m_rtTimestamp;      // Stores the input buffer timestamp

	double					m_fWetMix;			// percentage of effect
	double					m_fDryMix;			// percentage of dry signal


	double					m_fAttenuation_db;	// attenuation (up to +/-20dB).  What is displayed.

	CConvolution<float>*	m_Convolution;			// Polymorphic processing class
	HRESULT SelectConvolution(const WAVEFORMATEX *pWave);

	CSampleBuffer<float>*	m_Filter;				// The filter to be applied
	TCHAR					m_szFilterFileName[MAX_PATH];
	WAVEFORMATEX			m_WfexFilterFormat;	// The format of the filter file
	template<typename FFT_type>
							HRESULT LoadFilter();

#if defined(DEBUG) | defined(_DEBUG)
	CWaveFile*				m_CWaveFileTrace;	// To keep a record of the processed output
#endif

    BOOL                    m_bEnabled;         // TRUE if enabled
};

#endif //__CCONVOLVER_H_
