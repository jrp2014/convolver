// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005,2006  John Pavel
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
//#include "Common\dxstdafx.h"

#if defined(DEBUG) | defined(_DEBUG)
#include "debugging\debugging.h"
#include "debugging\debugStream.h"
#endif

#include "convolution\waveformat.h"
#include "convolution\convolution.h"

const DWORD UNITS = 10000000;	// 1 sec = 1 * UNITS

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\DSP Plugin");
const TCHAR kszPrefsAttenuation[] = _T("Attenuation");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");
const TCHAR kszPrefsPartitions[] = _T("Partitions");
const TCHAR kszPrefsPlanningRigour[] = _T("PlanningRigour");
const TCHAR kszPrefsDither[] = _T("Dither");
const TCHAR kszPrefsNoiseShaping[] = _T("NoiseShaping");

/////////////////////////////////////////////////////////////////////////////
// IConvolver
/////////////////////////////////////////////////////////////////////////////

// {47427372-7AED-4e37-ABEB-7BD64C4184BF}
DEFINE_GUID(CLSID_Convolver, 
			0x47427372, 0x7aed, 0x4e37, 0xab, 0xeb, 0x7b, 0xd6, 0x4c, 0x41, 0x84, 0xbf);

interface __declspec(uuid("{9B102F5D-8E2C-41F2-9256-2D3CA76FBE35}")) IConvolver : IUnknown
{
public:
	STDMETHOD(get_filterfilename)(TCHAR* *pVal) PURE;
	STDMETHOD(put_filterfilename)(TCHAR* newVal) PURE;

	STDMETHOD(get_attenuation)(float *pVal) PURE;
	STDMETHOD(put_attenuation)(float newVal) PURE;

	STDMETHOD(get_partitions)(DWORD *pVal) PURE;
	STDMETHOD(put_partitions)(DWORD newVal) PURE;

	STDMETHOD(get_planning_rigour)(PlanningRigour::PlanningRigourType *pVal) PURE;
	STDMETHOD(put_planning_rigour)(PlanningRigour::PlanningRigourType newVal) PURE;

	STDMETHOD(get_dither)(unsigned int *pVal) PURE;
	STDMETHOD(put_dither)(unsigned int newVal) PURE;

	STDMETHOD(get_noiseshaping)(unsigned int *pVal) PURE;
	STDMETHOD(put_noiseshaping)(unsigned int newVal) PURE;

	STDMETHOD(get_filter_description)(std::basic_string<TCHAR>* description) PURE;
	STDMETHOD(calculateOptimumAttenuation)(float & fAttenuation) PURE;

	STDMETHOD_(float,decode_Attenuationdb)(const DWORD dwValue) PURE;
	STDMETHOD_(DWORD,encode_Attenuationdb)(const float fValue) PURE;
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
	STDMETHODIMP get_filterfilename(TCHAR *pVal[]);
	STDMETHODIMP put_filterfilename(TCHAR newVal[]);

	STDMETHODIMP get_attenuation(float *pVal);
	STDMETHODIMP put_attenuation(float newVal);

	STDMETHODIMP get_partitions(DWORD *pVal);
	STDMETHODIMP put_partitions(DWORD newVal);

	STDMETHODIMP get_planning_rigour(unsigned int *pVal);
	STDMETHODIMP put_planning_rigour(unsigned int newVal);

	STDMETHODIMP get_dither(unsigned int *pVal);
	STDMETHODIMP put_dither(unsigned int newVal);

	STDMETHODIMP get_noiseshaping(unsigned int *pVal);
	STDMETHODIMP put_noiseshaping(unsigned int newVal);

	STDMETHODIMP get_filter_description(std::basic_string<TCHAR>* description);
	STDMETHODIMP calculateOptimumAttenuation(float & fAttenuation);

	// IMediaObject methods
	STDMETHODIMP GetStreamCount ( 
		DWORD *pcInputStreams,
		DWORD *pcOutputStreams
		);

	STDMETHODIMP GetInputStreamInfo ( 
		DWORD dwInputStreamIndex,
		DWORD *pdwFlags
		);

	STDMETHODIMP GetOutputStreamInfo ( 
		DWORD dwOutputStreamIndex,
		DWORD *pdwFlags
		);

	STDMETHODIMP GetInputType ( 
		DWORD dwInputStreamIndex,
		DWORD dwTypeIndex,
		DMO_MEDIA_TYPE *pmt
		);

	STDMETHODIMP GetOutputType ( 
		DWORD dwOutputStreamIndex,
		DWORD dwTypeIndex,
		DMO_MEDIA_TYPE *pmt
		);

	STDMETHODIMP GetMediaType ( // Helper
		DWORD dwInputStreamIndex,
		DWORD dwTypeIndex,
		DMO_MEDIA_TYPE *pmt,
		WORD nChannels,
		DWORD dwChannelMask
		);

	STDMETHODIMP SetInputType ( 
		DWORD dwInputStreamIndex,
		const DMO_MEDIA_TYPE *pmt,
		DWORD dwFlags
		);

	STDMETHODIMP SetOutputType ( 
		DWORD dwOutputStreamIndex,
		const DMO_MEDIA_TYPE *pmt,
		DWORD dwFlags
		);

	STDMETHODIMP GetInputCurrentType ( 
		DWORD dwInputStreamIndex,
		DMO_MEDIA_TYPE *pmt
		);

	STDMETHODIMP GetOutputCurrentType ( 
		DWORD dwOutputStreamIndex,
		DMO_MEDIA_TYPE *pmt
		);

	STDMETHODIMP GetInputSizeInfo ( 
		DWORD dwInputStreamIndex,
		DWORD *pcbSize,
		DWORD *pcbMaxLookahead,
		DWORD *pcbAlignment
		);

	STDMETHODIMP GetOutputSizeInfo ( 
		DWORD dwOutputStreamIndex,
		DWORD *pcbSize,
		DWORD *pcbAlignment
		);

	STDMETHODIMP GetInputMaxLatency ( 
		DWORD dwInputStreamIndex,
		REFERENCE_TIME *prtMaxLatency
		);

	STDMETHODIMP SetInputMaxLatency ( 
		DWORD dwInputStreamIndex,
		REFERENCE_TIME rtMaxLatency
		);

	STDMETHODIMP Flush ( void );

	STDMETHODIMP Discontinuity ( 
		DWORD dwInputStreamIndex
		);
	STDMETHODIMP AllocateStreamingResources ( void );

	STDMETHODIMP FreeStreamingResources ( void );

	STDMETHODIMP GetInputStatus ( 
		DWORD dwInputStreamIndex,
		DWORD *pdwFlags
		);

	STDMETHODIMP ProcessInput ( 
		DWORD dwInputStreamIndex,
		IMediaBuffer *pBuffer,
		DWORD dwFlags,
		REFERENCE_TIME rtTimestamp,
		REFERENCE_TIME rtTimelength
		);

	STDMETHODIMP ProcessOutput ( 
		DWORD dwFlags,
		DWORD cOutputBufferCount,
		DMO_OUTPUT_DATA_BUFFER *pOutputBuffers,
		DWORD *pdwStatus
		);

	STDMETHODIMP Lock ( LONG bLock );

	// Note: need to override CComObjectRootEx::Lock to avoid
	// ambiguity with IMediaObject::Lock. The override just
	// calls through to the base class implementation.

	// CComObjectRootEx overrides
	void Lock()
	{
		CComObjectRootEx<CComMultiThreadModel>::Lock();
	}

	// IWMPPluginEnable methods
	STDMETHODIMP SetEnable ( BOOL fEnable );
	STDMETHODIMP GetEnable ( BOOL *pfEnable );
	STDMETHODIMP GetCaps ( DWORD* pdwFlags );

	// ISpecifyPropertyPages methods
	STDMETHODIMP GetPages (CAUUID *pPages);

#ifdef IPROPERTYBAG
	// IPropertyBag methods
	STDMETHODIMP Read(LPCOLESTR pszPropName,VARIANT *pVar, IErrorLog *pErrorLog);

	STDMETHODIMP Write(LPCOLESTR pszPropName, VARIANT *pVar) 
	{return E_NOTIMPL;}
#endif

	// The following pair is needed because DWORD is unsigned
	STDMETHODIMP_(float) decode_Attenuationdb(const DWORD dwValue)
	{
		assert(dwValue <= (MAX_ATTENUATION_DB + MAX_ATTENUATION_DB) * 100);
		return static_cast<BaseT>(dwValue) / 100.0 - MAX_ATTENUATION_DB;
	}
	STDMETHODIMP_(DWORD) encode_Attenuationdb(const float fValue)
	{	assert (fabs(fValue) <= MAX_ATTENUATION_DB);
		return static_cast<DWORD>((fValue + MAX_ATTENUATION_DB) * 100.0);
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

	HRESULT ShowFilterPropertyPage();
	HRESULT NoValidFilterSelected();

	//void FillBufferWithSilence(WAVEFORMATEX *pWfex); // TODO: Remove or make this do something useful

	DMO_MEDIA_TYPE          m_mtInput;          // Stores the input format structure
	DMO_MEDIA_TYPE          m_mtOutput;         // Stores the output format structure

	CComPtr<IMediaBuffer>   m_spInputBuffer;    // Smart pointer to the input buffer object
	BYTE*                   m_pbInputData;      // Pointer to the data in the input buffer
	DWORD                   m_cbInputLength;    // Length of the data in the input buffer

	bool                    m_bValidTime;       // Is timestamp valid?
	REFERENCE_TIME          m_rtTimestamp;      // Stores the input buffer timestamp

	TCHAR					m_szFilterFileName[MAX_PATH];
	float					m_fAttenuation_db;	// attenuation (up to +/-20dB).  What is displayed.
	DWORD					m_nPartitions;		// Number of partitions to be used in convolution algorithm
	PlanningRigour::PlanningRigourType	m_nPlanningRigour;

	typedef NoiseShaper<BaseT>::NoiseShapingType NoiseShapingType;
	typedef Ditherer<BaseT>::DitherType DitherType;

	DitherType				m_nDither;			// The dither type index
	NoiseShapingType		m_nNoiseShaping;	// The noise shaping type index

	Holder< ConvolutionList<BaseT>, Thread_Mutex >	m_ConvolutionList;			// Processing class.
	Holder<ConvertSample<BaseT> >		m_InputSampleConvertor;		// functionoid conversion between BYTE and 
	Holder<ConvertSample<BaseT> >		m_OutputSampleConvertor;	// BaseT

	ConvertSampleMaker<BaseT>			m_FormatSpecs;

	BOOL					m_bEnabled;         // TRUE if enabled

//#ifdef DMO_NOATL
//    CRITICAL_SECTION		m_cs;
//#else
//    CComAutoCriticalSection m_cs;
//#endif

	Thread_Mutex			m_cs;				// for locking the whole DMO

};

#endif //__CCONVOLVER_H_
