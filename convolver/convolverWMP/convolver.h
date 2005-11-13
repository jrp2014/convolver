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

#define IPROPERTYBAG

#include "resource.h"
#include <mediaobj.h>       // The IMediaObject header from the DirectX SDK.

#ifndef DMO
#include "wmpservices.h"    // The header containing the WMP interface definitions.
#endif

// Pull in Common DX classes
#include "Common\dxstdafx.h"

#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#include "debugging\debugStream.h"
#endif

#include "convolution\waveformat.h"
#include "convolution\convolution.h"

// FFT routines
#include "fft\fftsg_h.h"

const DWORD UNITS = 10000000;	// 1 sec = 1 * UNITS
const DWORD MAXSTRING = 1024;	// length

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\DSP Plugin");
const TCHAR kszPrefsAttenuation[] = _T("Attenuation");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");
const TCHAR kszPrefsPartitions[] = _T("Partitions");

/////////////////////////////////////////////////////////////////////////////
// IConvolver
/////////////////////////////////////////////////////////////////////////////

// {47427372-7AED-4e37-ABEB-7BD64C4184BF}
DEFINE_GUID(CLSID_Convolver, 
			0x47427372, 0x7aed, 0x4e37, 0xab, 0xeb, 0x7b, 0xd6, 0x4c, 0x41, 0x84, 0xbf);

interface __declspec(uuid("{9B102F5D-8E2C-41F2-9256-2D3CA76FBE35}")) IConvolver : IUnknown
{
public:
	virtual HRESULT STDMETHODCALLTYPE get_filterfilename(TCHAR* *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_filterfilename(TCHAR* newVal) = 0;

	virtual HRESULT STDMETHODCALLTYPE get_attenuation(float *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_attenuation(float newVal) = 0;

	virtual float	decode_Attenuationdb(const DWORD dwValue) = 0;
	virtual DWORD	encode_Attenuationdb(const float fValue) = 0;

	virtual HRESULT STDMETHODCALLTYPE get_partitions(DWORD *pVal) = 0;
	virtual HRESULT STDMETHODCALLTYPE put_partitions(DWORD newVal) = 0;
};


/////////////////////////////////////////////////////////////////////////////
// CConvolver
/////////////////////////////////////////////////////////////////////////////

// TODO: Could also implement the IMediaObjectInPlace interface, as an optimization for cases where the input and output
// streams have the same format
// See: http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directshow/htm/imediaobjectinplaceinterface.asp

class ATL_NO_VTABLE CConvolver : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CConvolver, &CLSID_Convolver>,
	public IConvolver,
	public IMediaObject,
	public IWMPPluginEnable,
#ifdef IPROPERTYBAG
	public IPropertyBag,
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
		COM_INTERFACE_ENTRY(IWMPPluginEnable)
#ifdef IPROPERTYBAG
		COM_INTERFACE_ENTRY(IPropertyBag)
#endif
		COM_INTERFACE_ENTRY(ISpecifyPropertyPages)
	END_COM_MAP()

	// CComCoClass Overrides
	HRESULT FinalConstruct();
	void    FinalRelease();

	// IConvolver methods
	STDMETHOD(get_filterfilename)(TCHAR *pVal[]);
	STDMETHOD(put_filterfilename)(TCHAR newVal[]);

	STDMETHOD(get_attenuation)(float *pVal);
	STDMETHOD(put_attenuation)(float newVal);

	STDMETHOD(get_partitions)(DWORD *pVal);
	STDMETHOD(put_partitions)(DWORD newVal);

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

	STDMETHOD( GetMediaType )( // Helper
		DWORD dwInputStreamIndex,
		DWORD dwTypeIndex,
		DMO_MEDIA_TYPE *pmt,
		WORD nChannels,
		DWORD dwChannelMask
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
	STDMETHOD( GetCaps )( DWORD* pdwFlags );

	// ISpecifyPropertyPages methods
	STDMETHOD( GetPages )(CAUUID *pPages);

#ifdef IPROPERTYBAG
	// IPropertyBag methods
	STDMETHODIMP Read(LPCOLESTR pszPropName,VARIANT *pVar, IErrorLog *pErrorLog);

	STDMETHODIMP Write(LPCOLESTR pszPropName, VARIANT *pVar) 
	{return E_NOTIMPL;}
#endif

	// The following pair is needed because DWORD is unsigned
	float decode_Attenuationdb(const DWORD dwValue)
	{
		assert(dwValue <= (MAX_ATTENUATION + MAX_ATTENUATION) * 100);
		return static_cast<float>(dwValue) / 100.0f - MAX_ATTENUATION;
	}
	DWORD encode_Attenuationdb(const float fValue)
	{	assert (abs(fValue) <= MAX_ATTENUATION);
		return static_cast<DWORD>((fValue + MAX_ATTENUATION) * 100.0f);
	}

private:
	HRESULT DoProcessOutput(
		BYTE *pbOutputData,						// Pointer to the output buffer
		const BYTE *pbInputData,				// Pointer to the input buffer
		DWORD *cbInputBytesProcessed,			// Number of input bytes
		DWORD *cbOutputBytesGenerated);			// Number of output bytes
	HRESULT ValidateMediaType(
		const DMO_MEDIA_TYPE *pmtTarget,		// target media type to verify
		const DMO_MEDIA_TYPE *pmtPartner);		// partner media type to verify

	//void FillBufferWithSilence(WAVEFORMATEX *pWfex); // TODO: Remove or make this do something useful

	DMO_MEDIA_TYPE          m_mtInput;          // Stores the input format structure
	DMO_MEDIA_TYPE          m_mtOutput;         // Stores the output format structure

	CComPtr<IMediaBuffer>   m_spInputBuffer;    // Smart pointer to the input buffer object
	BYTE*                   m_pbInputData;      // Pointer to the data in the input buffer
	DWORD                   m_cbInputLength;    // Length of the data in the input buffer

	bool                    m_bValidTime;       // Is timestamp valid?
	REFERENCE_TIME          m_rtTimestamp;      // Stores the input buffer timestamp

	float					m_fAttenuation_db;	// attenuation (up to +/-20dB).  What is displayed.
	DWORD					m_nPartitions;		// Number of partitions to be used in convolution algorithm

	Holder< CConvolution<float> >	m_Convolution;			// Processing class.  Handle manages resources
	Sample<float>*					m_InputSampleConvertor;		// functionoid conversion between BYTE and 
	Sample<float>*					m_OutputSampleConvertor;	// float
	TCHAR							m_szFilterFileName[MAX_PATH];

	CFormatSpecs<float>				m_FormatSpecs;

	BOOL                    m_bEnabled;         // TRUE if enabled
};

#endif //__CCONVOLVER_H_
