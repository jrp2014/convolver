// ConvolverFilter: DirectShow filter that convolves an impulse respose
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

#include "convolution\config.h"

#include <windows.h>
#include <streams.h>
#include <initguid.h>
#if (1100 > _MSC_VER)
#include <olectlid.h>
#else
#include <olectl.h>
#endif
#include <string>
#include "iconvolverFilter.h"
#include "convolverFilterProp.h"
#include "convolverFilter.h"
#include "resource.h"
#include <assert.h>
#include <stdio.h>


#define TRANSFORM_NAME L"ConvolverFilter"

// Setup information
const REGPINTYPES sudPinTypes[] =
{
	{&MEDIATYPE_Audio, &MEDIASUBTYPE_IEEE_FLOAT},
	{&MEDIATYPE_Audio, &MEDIASUBTYPE_PCM},
	{&MEDIATYPE_Audio, &MEDIASUBTYPE_NULL}
};

const REGFILTERPINS sudpPins[] =
{
	{
		L"Input",             // Pins string name
			FALSE,                // Is it rendered
			FALSE,                // Is it an output
			FALSE,                // Are we allowed none
			FALSE,                // And allowed many
			&CLSID_NULL,          // Connects to filter
			NULL,                 // Connects to pin
			NUMELMS(sudPinTypes), // Number of types
			sudPinTypes          // Pin information
	},
	{
		L"Output",            // Pins string name
			FALSE,                // Is it rendered
			TRUE,                 // Is it an output
			FALSE,                // Are we allowed none
			FALSE,                // And allowed many
			&CLSID_NULL,          // Connects to filter
			NULL,                 // Connects to pin
			NUMELMS(sudPinTypes), // Number of types
			sudPinTypes          // Pin information
		}
};

const AMOVIESETUP_FILTER sudconvolverFilter =
{
	&CLSID_convolverFilter,	// Filter CLSID
	TRANSFORM_NAME,				// String name
	MERIT_DO_NOT_USE+1,			// Filter merit
	2,							// Number of pins
	sudpPins					// Pin information
};

// List of class IDs and creator functions for the class factory. This
// provides the link between the OLE entry point in the DLL and an object
// being created. The class factory will call the static CreateInstance

CFactoryTemplate g_Templates[] = {
	{ TRANSFORM_NAME
	, &CLSID_convolverFilter
	, CconvolverFilter::CreateInstance
	, NULL
	, &sudconvolverFilter }
	,
	{ TRANSFORM_NAME L" Properties"
	, &CLSID_convolverFilterPropertyPage
	, CconvolverFilterProperties::CreateInstance }
};
int g_cTemplates = sizeof(g_Templates) / sizeof(g_Templates[0]);


//
// DllEntryPoint
//
extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);

BOOL APIENTRY DllMain(HANDLE hModule, 
					  DWORD  dwReason, 
					  LPVOID lpReserved)
{
	return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}

//
// DllRegisterServer
//
// Handles sample registry and unregistry
//
STDAPI DllRegisterServer(void)
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::DllRegisterServer")));

	//return AMovieDllRegisterServer2(TRUE);
	HRESULT hr = AMovieDllRegisterServer2(TRUE);
	if (FAILED(hr))
	{
		return hr;
	}
	IFilterMapper2 *pFM2 = NULL;
	hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
		IID_IFilterMapper2, (void **)&pFM2);
	if (SUCCEEDED(hr))
	{
		// Declare filter information
		const REGFILTER2 rf2convolverFilterReg =
		{
			1,							// Version number
			MERIT_DO_NOT_USE+1,			// Filter merit
			2,							// Number of pins
			sudpPins					// Pin information
		};

		hr = pFM2->RegisterFilter(
			CLSID_convolverFilter,           // Filter CLSID. 
			TRANSFORM_NAME,                  // Filter name.
			NULL,                            // Device moniker. 
			&CLSID_LegacyAmFilterCategory,	 // DirectShow filter
			NULL,							 // Instance data.
			&rf2convolverFilterReg           // Filter information.
			);
	}
	if(pFM2 != NULL)
	{
		pFM2->Release();
	}
	return hr;
}

//
// DllUnregisterServer
//
STDAPI DllUnregisterServer()
{
	DbgLog((LOG_TRACE, 3, TEXT("CconvolverFilter::DllUnregisterServer")));

	//return AMovieDllRegisterServer2(FALSE);
	HRESULT hr = AMovieDllRegisterServer2(FALSE);
	if (FAILED(hr))
	{
		return hr;
	}
	IFilterMapper2 *pFM2 = NULL;
	hr = CoCreateInstance(CLSID_FilterMapper2, NULL, CLSCTX_INPROC_SERVER,
		IID_IFilterMapper2, (void **)&pFM2);
	if (SUCCEEDED(hr))
	{
		hr = pFM2->UnregisterFilter(&CLSID_LegacyAmFilterCategory, 
			TRANSFORM_NAME, CLSID_convolverFilter);
	}
	if(pFM2 != NULL)
	{
		pFM2->Release();
	}
	return hr;
}

//
// Constructor
//
CconvolverFilter::CconvolverFilter(TCHAR *tszName, LPUNKNOWN punk, HRESULT *phr) :
CTransformFilter(tszName, punk, CLSID_convolverFilter),
m_fAttenuation_db(0),
m_nPartitions(0),
m_nPlanningRigour(PlanningRigour::Measure),
m_nDither(Ditherer<float>::None),
m_nNoiseShaping(NoiseShaper<float>::None),
m_InputSampleConvertor(NULL),
m_OutputSampleConvertor(NULL)
{
#if defined(DEBUG) | defined(_DEBUG)
	DbgSetModuleLevel(LOG_ERROR|LOG_LOCKING|LOG_MEMORY|LOG_TIMING|LOG_TRACE, 2);

	// 3 = function call trace
	apDebug::gOnly().set(3);

	DEBUGGING(3, cdebug << "CconvolverFilter" << std::endl;);

	const int	mode =   (1 * _CRTDBG_MODE_DEBUG) | (1 * _CRTDBG_MODE_WNDW);
	::_CrtSetReportMode (_CRT_WARN, mode);
	::_CrtSetReportMode (_CRT_ERROR, mode);
	::_CrtSetReportMode (_CRT_ASSERT, mode);

	const int	old_flags = ::_CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);
	::_CrtSetDbgFlag (  old_flags
		| (1 * _CRTDBG_LEAK_CHECK_DF)
		| (1 * _CRTDBG_CHECK_ALWAYS_DF)
		| (1 * _CRTDBG_ALLOC_MEM_DF));
	//::_CrtSetBreakAlloc (-1);	// Specify here a memory bloc number

	debugstream.sink (apDebugSinkWindows::sOnly);
	apDebugSinkWindows::sOnly.showHeader (true);
#endif

	::ZeroMemory(&m_WaveInXT, sizeof(m_WaveInXT));
	::ZeroMemory(&m_WaveOutXT, sizeof(m_WaveOutXT));

	m_szFilterFileName[0] = 0;

	CRegKey key;
	DWORD   dwValue = 0;

	LONG lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
	if (ERROR_SUCCESS == lResult)
	{
		// Read the attenuation value from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsAttenuation, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a float.
			m_fAttenuation_db = decode_Attenuationdb(dwValue);
		}

		// Read the filter file name from the registry. 
		TCHAR szValue[MAX_PATH]	= TEXT("");
		ULONG ulMaxPath			= MAX_PATH;

		// Read the filter filename value.
		lResult = key.QueryStringValue(kszPrefsFilterFileName, szValue, &ulMaxPath );

		if (ERROR_SUCCESS == lResult)
		{
			_tcsncpy(m_szFilterFileName, szValue, ulMaxPath);
		}

		// Read the number of partitions that the convolution routine should use
		lResult = key.QueryDWORDValue(kszPrefsPartitions, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a WORD.
			m_nPartitions = dwValue;
		}

		// Read the planning rigour that the convolution routine should use
		lResult = key.QueryDWORDValue(kszPrefsPlanningRigour, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a unsigned int.
			m_nPlanningRigour = static_cast<PlanningRigour::Degree>(dwValue);
		}

		// Read the dither that the convolution routine should use
		lResult = key.QueryDWORDValue(kszPrefsDither, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			m_nDither = Ditherer<BaseT>::DitherType(dwValue);
		}

		
		// Read the noise shaping that the convolution routine should use
		lResult = key.QueryDWORDValue(kszPrefsNoiseShaping, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			m_nNoiseShaping = NoiseShaper<BaseT>::NoiseShapingType(dwValue);
		}

		try // creating m_ConvolutionList might throw
		{
			m_ConvolutionList.set_ptr(new ConvolutionList<BaseT>(m_szFilterFileName,
				m_nPartitions == 0 ? 1 : m_nPartitions,	m_nPlanningRigour)); // 0 partitions = overlap-save
		}
		catch (...) 
		{
			m_ConvolutionList.set_ptr(NULL);
		}
	} 
}

//
// ~CconvolverFilter
//
//CconvolverFilter::~CconvolverFilter() 
//{
//}

//
// CreateInstance
//
// Provide the way for COM to create a convolverFilter object
//
CUnknown *CconvolverFilter::CreateInstance(LPUNKNOWN punk, HRESULT *phr)
{
	DbgLog((LOG_TRACE, 3, TEXT("CconvolverFilter::CreateInstance")));

	CconvolverFilter *pNewObject = new CconvolverFilter(NAME("ConvolverFilter"), punk, phr);
	if (pNewObject == NULL) {
		*phr = E_OUTOFMEMORY;
	}
	return pNewObject;
}

//
// NonDelegatingQueryInterface
//
// Reveals IconvolverFilter and ISpecifyPropertyPages
//
STDMETHODIMP CconvolverFilter::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
	DbgLog((LOG_TRACE, 3, TEXT("CconvolverFilter::NonDelegatingQueryInterface")));

	CheckPointer(ppv,E_POINTER);

	if (riid == IID_IconvolverFilter) {
		return GetInterface((IconvolverFilter *) this, ppv);
	} else if (riid == IID_ISpecifyPropertyPages) {
		return GetInterface((ISpecifyPropertyPages *) this, ppv);
	} else {
		return CTransformFilter::NonDelegatingQueryInterface(riid, ppv);
	}
}

//
// Transform
//
// Transforms the input and saves results in the the output
//
HRESULT CconvolverFilter::Transform(IMediaSample *pIn, IMediaSample *pOut)
{
	DbgLog((LOG_TRACE, 3, TEXT("CconvolverFilter::Transform")));

	CheckPointer(pIn, E_POINTER);
	CheckPointer(pOut, E_POINTER);

	HRESULT hr = S_OK;


	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
	if(m_ConvolutionList.get_ptr() == NULL || !m_ConvolutionList->ConvolutionSelected())
	{
		NoValidFilterSelected();
		return E_ABORT;
	}

	// Look for format changes from downstream.
	CMediaType *pMediaType = NULL;
	if( S_OK == pOut->GetMediaType((AM_MEDIA_TYPE**)&pMediaType) && pMediaType)
	{
#if defined(DEBUG) | defined(_DEBUG)
		cdebug << "Format change" << std::endl;
#endif

        hr = m_pOutput->CheckMediaType(pMediaType);
        if(FAILED(hr))
        {
            DeleteMediaType(pMediaType);
            return E_FAIL;
        }

		// Notify our own output pin of the new type
		hr = m_pOutput->SetMediaType(pMediaType);
		DeleteMediaType(pMediaType);
		if(FAILED(hr))
		{
			return hr;
		}
	}

	// input
	CheckPointer(m_pInput, E_UNEXPECTED);
	const AM_MEDIA_TYPE* pTypeIn = &m_pInput->CurrentMediaType();
	BYTE *pbSrc = NULL;
	hr = pIn->GetPointer((BYTE **)&pbSrc); // retrieve a read/write pointer to the media sample's buffer.
	if(FAILED(hr))
	{
		return hr;
	}
	CheckPointer(pbSrc, E_UNEXPECTED);

	// output
	CheckPointer(m_pOutput, E_UNEXPECTED);
	AM_MEDIA_TYPE *pTypeOut = &m_pOutput->CurrentMediaType();
	hr = pOut->SetMediaType(pTypeOut);
	BYTE *pbDst = NULL;
	hr = pOut->GetPointer((BYTE **)&pbDst); // retrieve a read/write pointer to the media sample's buffer.
	if(FAILED(hr))
	{
		return hr;
	}
	CheckPointer(pbDst, E_UNEXPECTED);

	// Get sizes
	const DWORD cbOutputMaxLength = pOut->GetSize();
	const DWORD cbBytesToProcess = pIn->GetActualDataLength();

	DWORD cbBytesThatWouldBeGenerated = m_ConvolutionList->SelectedConvolution().cbOutputBuffer(cbBytesToProcess,
		m_InputSampleConvertor.get_ptr(), m_OutputSampleConvertor.get_ptr());

	// This should never happen if DecideBufferSize did its job
	if (cbBytesThatWouldBeGenerated > cbOutputMaxLength)
	{
		return VFW_E_BUFFER_OVERFLOW;
	}

	// Calculate the number of blocks to process.  A block contains the Samples for all channels
	assert(cbBytesToProcess % m_WaveInXT.Format.nBlockAlign == 0);
	DWORD dwBlocksToProcess = (cbBytesToProcess / m_WaveInXT.Format.nBlockAlign);

	if(pIn->IsDiscontinuity() == S_OK)
	{
		m_ConvolutionList->SelectedConvolution().Flush();
	}

	DWORD cbBytesGenerated = m_nPartitions == 0 ?
		m_ConvolutionList->SelectedConvolution().doConvolution(InterleavedIO<BaseT>(pbSrc, pbDst,
		m_InputSampleConvertor.get_ptr(), m_OutputSampleConvertor.get_ptr()),
		dwBlocksToProcess,
		m_fAttenuation_db)
		: 	m_ConvolutionList->SelectedConvolution().doPartitionedConvolution(InterleavedIO<BaseT>(pbSrc, pbDst,
		m_InputSampleConvertor.get_ptr(), m_OutputSampleConvertor.get_ptr()),
		dwBlocksToProcess,
		m_fAttenuation_db);

	assert(cbBytesThatWouldBeGenerated == cbBytesGenerated);

	assert(pOut->GetSize() >= cbBytesGenerated);
	// Set the size of the valid data in the output buffer.
	hr = pOut->SetActualDataLength(cbBytesGenerated);
	if (FAILED(hr))
	{
		return hr;
	}

	assert(!pTypeIn->bTemporalCompression);

	if(cbBytesGenerated > 0)
	{
		return S_OK;
	}
	else // no bytes generated yet
	{
		return S_FALSE; // Don't deliver
	}
}

//
// CheckInputType
//
// Check the input type is OK - return an error otherwise
//
HRESULT CconvolverFilter::CheckInputType(const CMediaType *mtIn)
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::CheckInputType")));

	CheckPointer(mtIn, E_POINTER);
#ifdef DEBUG
	DisplayType(TEXT("mtIn:"), (AM_MEDIA_TYPE*)(mtIn));
#endif
	assert(mtIn->IsValid());

	if(((mtIn->majortype != MEDIATYPE_Audio) && (mtIn->majortype != MEDIATYPE_NULL)) ||
		((mtIn->subtype != MEDIASUBTYPE_PCM) && (mtIn->subtype != MEDIASUBTYPE_IEEE_FLOAT) && (mtIn->subtype != MEDIASUBTYPE_NULL)) ||
		((mtIn->formattype != FORMAT_WaveFormatEx) && (mtIn->formattype != GUID_NULL)))
	{
		return VFW_E_INVALIDMEDIATYPE;
	}

	if(mtIn->formattype == FORMAT_WaveFormatEx)
	{
		if(mtIn->pbFormat == NULL)
		{
			return VFW_E_TYPE_NOT_ACCEPTED;
		}

		assert(mtIn->cbFormat >= sizeof(WAVEFORMATEX));
		assert(mtIn->cbFormat <= sizeof(WAVEFORMATEXTENSIBLE));

		if(m_ConvolutionList.get_ptr() == NULL)
		{
			HRESULT hr = ShowFilterPropertyPage();
			if(FAILED(hr))
				return hr;
		}

		const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
		if(m_ConvolutionList.get_ptr() != NULL)
		{
			// Does not check compatibility with output type -- CheckTransform does that
			if(FAILED(m_FormatSpecs.CheckSampleFormat(reinterpret_cast<WAVEFORMATEX*>(mtIn->pbFormat))))
			{
				return VFW_E_TYPE_NOT_ACCEPTED;
			}
		}
		else
		{
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	}
	// else check the precise format in CheckTransform, if it is not specified yet

	return S_OK;
}

//
// Checktransform
//
// Check a transform can be done between these formats
//
HRESULT CconvolverFilter::CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut)
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::CheckTransform")));

	// Health check.
	CheckPointer(mtIn, E_POINTER);
	CheckPointer(mtOut, E_POINTER);
#ifdef DEBUG
	DisplayType(TEXT("mtIn:"), (AM_MEDIA_TYPE*)(mtIn));
	DisplayType(TEXT("mtOut:"), (AM_MEDIA_TYPE*)(mtOut));
#endif

	if(((mtIn->majortype != MEDIATYPE_Audio) && (mtIn->majortype != MEDIATYPE_NULL)) ||
		((mtIn->subtype != MEDIASUBTYPE_PCM) && (mtIn->subtype != MEDIASUBTYPE_IEEE_FLOAT) &&
		(mtIn->subtype != MEDIASUBTYPE_NULL)) ||
		((mtIn->formattype != FORMAT_WaveFormatEx) && (mtIn->formattype != GUID_NULL)))
	{
		return VFW_E_INVALIDMEDIATYPE;
	}

	if(((mtOut->majortype != MEDIATYPE_Audio) && (mtOut->majortype != MEDIATYPE_NULL)) ||
		((mtOut->subtype != MEDIASUBTYPE_PCM) && (mtOut->subtype != MEDIASUBTYPE_IEEE_FLOAT) && 
		(mtOut->subtype != MEDIASUBTYPE_NULL)) ||
		((mtOut->formattype != FORMAT_WaveFormatEx) && (mtOut->formattype != GUID_NULL)))
	{
		return VFW_E_INVALIDMEDIATYPE;
	}

	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
	if(m_ConvolutionList.get_ptr() == NULL)
	{
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	const WAVEFORMATEX *pWaveIn = NULL;
	const WAVEFORMATEX *pWaveOut = NULL;

	if(mtIn->formattype == FORMAT_WaveFormatEx)
	{
		pWaveIn = reinterpret_cast<WAVEFORMATEX*>(mtIn->pbFormat);
		if(FAILED(m_FormatSpecs.CheckSampleFormat(pWaveIn)))
		{
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	}

	if(mtOut->formattype == FORMAT_WaveFormatEx)
	{
		pWaveOut = reinterpret_cast<WAVEFORMATEX*>(mtOut->pbFormat);
		if(FAILED(m_FormatSpecs.CheckSampleFormat(pWaveOut)))
		{
			return VFW_E_TYPE_NOT_ACCEPTED;
		}
	}
	
	if(FAILED(m_ConvolutionList->CheckConvolutionList(pWaveIn, pWaveOut)))
	{
		return VFW_E_TYPE_NOT_ACCEPTED;
	}

	return S_OK;
}

//
// DecideBufferSize
//
// Tell the output pin's allocator what size buffers we
// require. Can only do this when the input is connected
//
HRESULT CconvolverFilter::DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProp)
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::DecideBufferSize")));

	// Is the input pin connected
	CheckPointer(m_pInput, E_UNEXPECTED);
	if (m_pInput->IsConnected() == FALSE)
	{
		return E_UNEXPECTED;
	}

	CheckPointer(pAlloc, E_POINTER);
	CheckPointer(pProp, E_POINTER);

	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
	if(m_ConvolutionList.get_ptr() == NULL)
	{
		return E_ABORT;
	}

	// Find the properties of the upstream allocator
	ALLOCATOR_PROPERTIES InputProp;
	IMemAllocator *pAllocInput = NULL;
	HRESULT hr = m_pInput->GetAllocator(&pAllocInput);
	if(FAILED(hr))
	{
		return hr;
	}
	// Now get the input properties
	hr = pAllocInput->GetProperties(&InputProp);
	pAllocInput->Release();
	if(FAILED(hr))
	{
		return hr;
	}

	// Back to setting the output properties

	// TODO: Assume no prefix
	assert(pProp->cbPrefix==0);

	if(0 == pProp->cbAlign)
	{
		pProp->cbAlign = 16;	// Align to 16, for speed (perhaps 1 would be more widely compatible)
	}
	if(0 == pProp->cBuffers)	// Just use the same number of buffers as upstream
	{
		pProp->cBuffers = InputProp.cBuffers == 0 ? 3 : InputProp.cBuffers;
	}

	if(!m_ConvolutionList->ConvolutionSelected())
	{
		return E_ABORT;
	}

	// Now calculate the output buffer size, which is a function of the input buffer size
	const unsigned int cbOutputBuffer = m_ConvolutionList->SelectedConvolution().cbOutputBuffer(InputProp.cbBuffer,
		m_InputSampleConvertor.get_ptr(), m_OutputSampleConvertor.get_ptr());
	if (cbOutputBuffer > pProp->cbBuffer)
	{
		pProp->cbBuffer = cbOutputBuffer;
	}
	// else accept the proposed buffer size (although the surplus will be unused)

	ASSERT(pProp->cbBuffer);

	// Ask the allocator to reserve us some sample memory, NOTE the function
	// can succeed (that is return NOERROR) but still not have allocated the
	// memory that we requested, so we must check we got whatever we wanted

	ALLOCATOR_PROPERTIES Actual;
	hr = pAlloc->SetProperties(pProp,&Actual);
	if (FAILED(hr)) {
		return hr;
	}

	ASSERT(Actual.cBuffers > 0);

	if (pProp->cBuffers > Actual.cBuffers ||
		pProp->cbBuffer > Actual.cbBuffer) {
			return E_FAIL;
	}
	return S_OK;
}

//
// GetMediaType
//
// Returns the supported media types in order of preferred  types (starting with iPosition=0)
//
HRESULT CconvolverFilter::GetMediaType(int iPosition, CMediaType *pMediaType)
{

	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::GetMediaType %d"), iPosition));

	CheckPointer(m_pInput, E_UNEXPECTED);
	if(!m_pInput->IsConnected())
		return E_UNEXPECTED;

	CheckPointer(pMediaType, E_POINTER);

	if (iPosition < 0)
	{
		return E_INVALIDARG;
	}

	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
	if (NULL == m_ConvolutionList.get_ptr())
	{
		//// use the input format
		//HRESULT hr = m_pInput->ConnectionMediaType(pMediaType);
		//if (FAILED(hr))
		//{
		//	return hr;
		//}
		return VFW_S_NO_MORE_ITEMS;
	}

	// Number of Media types = number of available formats * number of different Convolutions available
	if ( m_FormatSpecs.size() * m_ConvolutionList->nConvolutionList() <= iPosition )
	{
		return VFW_S_NO_MORE_ITEMS;
	}

	// Decompose the dwTypeIndex into dwPathIndex and dwFormatSpecIndex
	DWORD dwPathIndex = iPosition %  m_ConvolutionList->nConvolutionList(); 
	DWORD dwFormatSpecIndex = iPosition % m_FormatSpecs.size();
#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "iPosition:" << iPosition << " dwPathIndex:" << dwPathIndex << " dwFormatSpecIndex:" << dwFormatSpecIndex << std::endl;
#endif

	pMediaType->InitMediaType();
	pMediaType->SetType(&MEDIATYPE_Audio);
	pMediaType->SetSubtype(&m_FormatSpecs[dwFormatSpecIndex].SubType);

	pMediaType->bFixedSizeSamples = true; // Actually already done in InitMediaType
	pMediaType->SetTemporalCompression(false);
	pMediaType->SetFormatType(&FORMAT_WaveFormatEx);
	pMediaType->pUnk = NULL;
	assert(m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample % 8 == 0);

	if(WAVE_FORMAT_EXTENSIBLE == m_FormatSpecs[dwFormatSpecIndex].wFormatTag)
	{
		WAVEFORMATEXTENSIBLE *pWaveXT = ( WAVEFORMATEXTENSIBLE * ) pMediaType->AllocFormatBuffer(sizeof(WAVEFORMATEXTENSIBLE));
		if(NULL == pWaveXT)
		{
			return E_OUTOFMEMORY;
		}
		pWaveXT->Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX); // Should be 22
		pWaveXT->Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; // m_FormatSpecs[dwFormatSpecIndex].wFormatTag;
		pWaveXT->Format.nChannels = m_ConvolutionList->Conv(dwPathIndex).Mixer.nOutputChannels();
		pWaveXT->Format.nSamplesPerSec = m_ConvolutionList->Conv(dwPathIndex).Mixer.nSamplesPerSec();
		pWaveXT->Format.wBitsPerSample = m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample;
		pWaveXT->Format.nBlockAlign = pWaveXT->Format.nChannels * m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample / 8;
		pMediaType->SetSampleSize(pWaveXT->Format.nBlockAlign);
		pWaveXT->Format.nAvgBytesPerSec = pWaveXT->Format.nSamplesPerSec * pWaveXT->Format.nBlockAlign;

		pWaveXT->Samples.wValidBitsPerSample = m_FormatSpecs[dwFormatSpecIndex].wValidBitsPerSample;
		pWaveXT->dwChannelMask = m_ConvolutionList->Conv(dwPathIndex).Mixer.dwChannelMask();
		pWaveXT->SubFormat = m_FormatSpecs[dwFormatSpecIndex].SubType;
	}
	else
	{
		WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) pMediaType->AllocFormatBuffer(sizeof(WAVEFORMATEX));
		if(NULL == pWave)
		{
			return E_OUTOFMEMORY;
		}

		pWave->cbSize = 0; // Size, in bytes, of extra format information appended to the end of the WAVEFORMATEX structure
		pWave->wFormatTag = m_FormatSpecs[dwFormatSpecIndex].wFormatTag;
		pWave->nChannels = m_ConvolutionList->Conv(dwPathIndex).Mixer.nOutputChannels();
		pWave->nSamplesPerSec = m_ConvolutionList->Conv(dwPathIndex).Mixer.nSamplesPerSec();
		pWave->wBitsPerSample = m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample;
		pWave->nBlockAlign = pWave->nChannels *  m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample / 8;
		pMediaType->SetSampleSize(pWave->nBlockAlign);
		pWave->nAvgBytesPerSec = pWave->nSamplesPerSec * pWave->nBlockAlign;
	}

	return S_OK;
}


//
// SetMediaType
//
// Overrides base class implementation so that we know when the media type is really set
// Copies the contents of pMediaType->pbFormat
// This information will be used by the Transform method
//
HRESULT CconvolverFilter::SetMediaType(PIN_DIRECTION direction, const CMediaType *pMediaType)
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::SetMediaType %d"), direction));

	CheckPointer(pMediaType, E_POINTER);
	CheckPointer(pMediaType->pbFormat, E_POINTER);
	if((pMediaType->formattype != FORMAT_WaveFormatEx) ||
		(pMediaType->FormatLength() < sizeof(WAVEFORMATEX)) ||
		(pMediaType->FormatLength() > sizeof(WAVEFORMATEXTENSIBLE)))
	{
		return VFW_E_INVALIDMEDIATYPE;
	};

	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
	if(m_ConvolutionList.get_ptr() == NULL)
	{
		return E_ABORT;
	}

	CheckPointer(pMediaType->pbFormat, E_POINTER);
	WAVEFORMATEX *pWave = (WAVEFORMATEX*)pMediaType->pbFormat;
	HRESULT hr = S_OK;

	if (direction == PINDIR_INPUT)
	{

		// check the transform can be done (should always succeed)
		assert(SUCCEEDED(CheckInputType(pMediaType)));

		hr = m_ConvolutionList->SelectConvolution(pWave, NULL);
		if(FAILED(hr))
		{
			return hr;
		}
		hr = m_FormatSpecs.SelectSampleConvertor(pWave, m_InputSampleConvertor);
		if(FAILED(hr))
		{
			return hr;
		}

		::ZeroMemory(&m_WaveInXT, sizeof(m_WaveInXT));
		if(WAVE_FORMAT_EXTENSIBLE == pWave->wFormatTag)
		{
			WAVEFORMATEXTENSIBLE *pWaveXT = (WAVEFORMATEXTENSIBLE*)pMediaType->pbFormat;
			::CopyMemory(&m_WaveInXT, pWaveXT, sizeof(WAVEFORMATEXTENSIBLE));
		}
		else
		{
			::CopyMemory(&m_WaveInXT, pWave, sizeof(WAVEFORMATEX));
		}
	}
	else // Output pin
	{
		assert(direction == PINDIR_OUTPUT);

		hr = m_ConvolutionList->SelectConvolution(NULL, pWave);
		if(FAILED(hr))
		{
			return hr;
		}
		hr = m_FormatSpecs.SelectSampleConvertor(pWave, m_OutputSampleConvertor, m_nNoiseShaping, m_nDither);
		if(FAILED(hr))
		{
			return hr;
		}

		::ZeroMemory(&m_WaveOutXT, sizeof(WAVEFORMATEXTENSIBLE));
		if(WAVE_FORMAT_EXTENSIBLE == pWave->wFormatTag)
		{
			WAVEFORMATEXTENSIBLE *pWaveXT = (WAVEFORMATEXTENSIBLE*)pMediaType->pbFormat;
			::CopyMemory(&m_WaveOutXT, pWaveXT, sizeof(WAVEFORMATEXTENSIBLE));
		}
		else
		{
			::CopyMemory(&m_WaveOutXT, pWave, sizeof(WAVEFORMATEX));
		}
	}

	return S_OK;
}

// Override base implementation
HRESULT CconvolverFilter::BeginFlush(void)
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::BeginFlush")));

	HRESULT hr = NOERROR;
	if (m_pOutput != NULL) {
		// block receives -- done by caller (CBaseInputPin::BeginFlush)

		const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
		if (m_ConvolutionList.get_ptr() != NULL)
		{
			m_ConvolutionList->Flush();
		}

		// call downstream
		hr = m_pOutput->DeliverBeginFlush();
	}
	return hr;
}

//
// GetPages
//
// Returns the clsid's of the property pages we support
//
STDMETHODIMP CconvolverFilter::GetPages(CAUUID *pPages)
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::GetPages")));

	pPages->cElems = 1;
	pPages->pElems = (GUID *) CoTaskMemAlloc(sizeof(GUID));
	if (pPages->pElems == NULL) {
		return E_OUTOFMEMORY;
	}
	*(pPages->pElems) = CLSID_convolverFilterPropertyPage;
	return NOERROR;
}



HRESULT CconvolverFilter::ShowFilterPropertyPage()
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::ShowFilterPropertyPage")));

	CAUUID caGUID;
	HRESULT hr = GetPages(&caGUID);
	if (FAILED(hr))
		return hr;

	IUnknown * pFilterUnk;
	hr = QueryInterface(IID_IUnknown, (void **)&pFilterUnk);
	if(FAILED(hr))
		return hr;

	// Display the filter's property page
	hr = OleCreatePropertyFrame(
		NULL,					// Parent window
		0,                      // x (Reserved)
		0,                      // y (Reserved)
		OLESTR("ConvolverFilter"),// Caption for the dialog box
		1,                      // Number of filters
		(IUnknown **)&pFilterUnk,// Pointer to the filter 
		caGUID.cElems,          // Number of property pages
		caGUID.pElems,          // Pointer to property page CLSIDs
		0,                   // Locale identifier
		0,                      // Reserved
		NULL                    // Reserved
		);

	CoTaskMemFree(caGUID.pElems);

	return hr;
}

HRESULT CconvolverFilter::NoValidFilterSelected()
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::NoValidFilterSelected")));

	std::basic_ostringstream<TCHAR> diagnostic;

	diagnostic << waveFormatDescription(&m_WaveInXT, 0, TEXT("Input: ")) << std::endl;
	diagnostic << waveFormatDescription(&m_WaveOutXT, 0, TEXT("Output: ")) << std::endl << std::endl;

	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);

	if( m_ConvolutionList.get_ptr() == NULL)
	{
		diagnostic << "No valid filter loaded.";
	}
	else
	{
		diagnostic << TEXT("Loaded filter: ") << std::endl <<  m_ConvolutionList->DisplayConvolutionList();
	}

	MessageBox(NULL, diagnostic.str().c_str(), TEXT("Filter invalid for current input/output"), 0);

	return S_OK;
}

//
// get_convolverFilter
//
// Copies the transform parameters to the given destination.
//
//STDMETHODIMP CconvolverFilter::get_convolverFilter(convolverFilterParameters *irp)
//{
//	CAutoLock cAutolock(&m_convolverFilterLock);
//	CheckPointer(irp,E_POINTER);
//
//	*irp = m_convolverFilterParameters;
//
//	return NOERROR;
//}
//
////
//// put_convolverFilter
////
//// Copies the transform parameters from the given source.
////
//STDMETHODIMP CconvolverFilter::put_convolverFilter(convolverFilterParameters irp)
//{
//	CAutoLock cAutolock(&m_convolverFilterLock);
//
//	m_convolverFilterParameters = irp;
//	SetDirty();
//
//	// reconnect
//	CMediaType &mt = m_pOutput->CurrentMediaType();
//	WAVEFORMATEX *pWaveOut = (WAVEFORMATEX *)mt.pbFormat;
//	if (!pWaveOut)
//		return S_OK;
//	// TODO: modify pWaveOut if output resolution or type has changed
//	HRESULT hr = ReconnectPin(m_pOutput, &mt);
//
//	return NOERROR;
//} 

//
// Copy
//
// Make destination an identical copy of source
// Not suitable for this filter, as it can change formats
//
HRESULT CconvolverFilter::Copy(IMediaSample *pSource, IMediaSample *pDest) const
{
	DbgLog((LOG_TRACE, 2, TEXT("CconvolverFilter::Copy")));

	// Copy the sample data

	BYTE *pSourceBuffer, *pDestBuffer;
	long lSourceSize = pSource->GetActualDataLength();
	long lDestSize	= pDest->GetSize();

	ASSERT(lDestSize >= lSourceSize);

	pSource->GetPointer(&pSourceBuffer);
	pDest->GetPointer(&pDestBuffer);

	CopyMemory( (PVOID) pDestBuffer,(PVOID) pSourceBuffer, lSourceSize);

	// Copy the sample times

	REFERENCE_TIME TimeStart, TimeEnd;
	if (S_OK == pSource->GetTime(&TimeStart, &TimeEnd)) {
		pDest->SetTime(&TimeStart, &TimeEnd);
	}

	LONGLONG MediaStart, MediaEnd;
	if (pSource->GetMediaTime(&MediaStart,&MediaEnd) == NOERROR) {
		pDest->SetMediaTime(&MediaStart,&MediaEnd);
	}

	// Copy the Sync point property

	HRESULT hr = pSource->IsSyncPoint();
	if (hr == S_OK) {
		pDest->SetSyncPoint(TRUE);
	}
	else if (hr == S_FALSE) {
		pDest->SetSyncPoint(FALSE);
	}
	else {  // an unexpected error has occured...
		return E_UNEXPECTED;
	}

	// Copy the media type

	AM_MEDIA_TYPE *pMediaType;
	pSource->GetMediaType(&pMediaType);
	pDest->SetMediaType(pMediaType);
	DeleteMediaType(pMediaType);

	// Copy the preroll property

	hr = pSource->IsPreroll();
	if (hr == S_OK) {
		pDest->SetPreroll(TRUE);
	}
	else if (hr == S_FALSE) {
		pDest->SetPreroll(FALSE);
	}
	else {  // an unexpected error has occured...
		return E_UNEXPECTED;
	}

	// Copy the discontinuity property

	hr = pSource->IsDiscontinuity();
	if (hr == S_OK) {
		pDest->SetDiscontinuity(TRUE);
	}
	else if (hr == S_FALSE) {
		pDest->SetDiscontinuity(FALSE);
	}
	else {  // an unexpected error has occured...
		return E_UNEXPECTED;
	}

	// Copy the actual data length

	long lDataLength = pSource->GetActualDataLength();
	pDest->SetActualDataLength(lDataLength);

	return NOERROR;
}



// Property get to retrieve the filter filename by using the public interface.
STDMETHODIMP CconvolverFilter::get_filterfilename(TCHAR *pVal[])
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_filterfilename" << std::endl;);
#endif

	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_szFilterFileName;

	return S_OK;
}

// Property put to store the filter filename by using the public interface.
STDMETHODIMP CconvolverFilter::put_filterfilename(TCHAR newVal[])
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_filterfilename" << std::endl;);
#endif

	if (_tcscmp(newVal, m_szFilterFileName) != 0) // if new filename set
	{
		HRESULT hr = updateConvolutionList<BaseT>(m_ConvolutionList, &m_WaveInXT.Format, &m_WaveOutXT.Format,
			newVal,	m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour);// May throw

		if(FAILED(hr))
			return hr;

		_tcsncpy(m_szFilterFileName, newVal, MAX_PATH);
	}

	return S_OK;
}

// Property get to retrieve the number of partitions by using the public interface.
STDMETHODIMP CconvolverFilter::get_partitions(DWORD *pVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_partitions" << std::endl;);
#endif

	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_nPartitions;

	return S_OK;
}

// Property put to store the number of partitions by using the public interface.
STDMETHODIMP CconvolverFilter::put_partitions(DWORD newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_partitions" << std::endl;);
#endif

	if(m_nPartitions != newVal)
	{
		HRESULT hr = updateConvolutionList<BaseT>(m_ConvolutionList, &m_WaveInXT.Format, &m_WaveOutXT.Format,
			m_szFilterFileName,	newVal == 0 ? 1 : newVal, m_nPlanningRigour);// May throw

		if(FAILED(hr))
			return hr;

		m_nPartitions = newVal;
	}

	return S_OK;
}


// Property get to retrieve the planning rigour by using the public interface.
STDMETHODIMP CconvolverFilter::get_planning_rigour(PlanningRigour::PlanningRigourType *pVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_planning_rigour" << std::endl;);
#endif

	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_nPlanningRigour;

	return S_OK;
}

// Property put to store the number of planning rigour by using the public interface.
STDMETHODIMP CconvolverFilter::put_planning_rigour(PlanningRigour::PlanningRigourType newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_planning_rigour" << std::endl;);
#endif

	if(m_nPlanningRigour != newVal)
	{
		HRESULT hr = updateConvolutionList<BaseT>(m_ConvolutionList, &m_WaveInXT.Format, &m_WaveOutXT.Format,
			m_szFilterFileName,	m_nPartitions == 0 ? 1 : m_nPartitions, newVal);// May throw

		if(FAILED(hr))
			return hr;

		m_nPlanningRigour = newVal;
	}

	return S_OK;
}


// Property get to retrieve the dither value by using the public interface.
STDMETHODIMP CconvolverFilter::get_dither(unsigned int *pVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_dither" << std::endl;);
#endif

	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_nDither;

	return S_OK;
}


// Property put to store the dither value by using the public interface.
STDMETHODIMP CconvolverFilter::put_dither(unsigned int newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_dither" << std::endl;);
#endif

	m_nDither = static_cast<Ditherer<float>::DitherType>(newVal);

	if(m_WaveOutXT.Format.wFormatTag == 0)
	{
		return S_OK;
	}
	else
	{
		return m_FormatSpecs.SelectSampleConvertor((WAVEFORMATEX*)(&m_WaveOutXT), m_OutputSampleConvertor, m_nNoiseShaping, m_nDither);
	}
}


// Property get to retrieve the noise shaper value by using the public interface.
STDMETHODIMP CconvolverFilter::get_noiseshaping(unsigned int *pVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_noiseshaping" << std::endl;);
#endif

	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_nNoiseShaping;

	return S_OK;
}

// Property put to store the noise shaper value by using the public interface.
STDMETHODIMP CconvolverFilter::put_noiseshaping(unsigned int newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_noiseshaping" << std::endl;);
#endif

	m_nNoiseShaping = static_cast<NoiseShaper<float>::NoiseShapingType>(newVal);

	if(m_WaveOutXT.Format.wFormatTag == 0)
	{
		return S_OK;
	}
	else
	{
		return m_FormatSpecs.SelectSampleConvertor((WAVEFORMATEX*)(&m_WaveOutXT), m_OutputSampleConvertor, m_nNoiseShaping, m_nDither);
	}
}

// Property to get a filter description 
STDMETHODIMP CconvolverFilter::get_filter_description(std::basic_string<TCHAR>* description)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_filter_description" << std::endl;);
#endif

	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
	*description = m_ConvolutionList.get_ptr() == NULL ? TEXT("No valid filter loaded") : m_ConvolutionList->DisplayConvolutionList();
	return S_OK;
}

// Property get to retrieve the wet mix value by using the public interface.
STDMETHODIMP CconvolverFilter::get_attenuation(float *pVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_attenuation" << std::endl;);
#endif

	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_fAttenuation_db;

	return S_OK;
}

// Property put to store the wet mix value by using the public interface.
STDMETHODIMP CconvolverFilter::put_attenuation(float newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_attenuation" << std::endl;);
#endif

	m_fAttenuation_db = newVal;

	return S_OK;
}

// Property to get a filter description 
STDMETHODIMP CconvolverFilter::calculateOptimumAttenuation(float & fAttenuation_db)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "calculateOptimumAttenuation" << std::endl;);
#endif

	// Need to call up a new Convolver as the current one may be playing.
	Holder< ConvolutionList<BaseT> > ConvolutionListOpt(new ConvolutionList<BaseT>(m_szFilterFileName,
		m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour)); // 0 partitions = overlap-save

	float min_fAttenuation_db = MAX_ATTENUATION_DB;
	for(unsigned int i=0; i<ConvolutionListOpt->nConvolutionList(); ++i)
	{
		HRESULT hr = ConvolutionListOpt->Conv(i).calculateOptimumAttenuation(fAttenuation_db);
		if(FAILED(hr))
		{
			return hr;
		}
		if(fAttenuation_db < min_fAttenuation_db)
		{
			min_fAttenuation_db = fAttenuation_db;
		}
	}
	fAttenuation_db = min_fAttenuation_db;
	return S_OK;
}
