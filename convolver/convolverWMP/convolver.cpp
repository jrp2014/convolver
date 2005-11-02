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
// convolver.cpp : Implementation of CConvolver
//
/////////////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "convolver.h"
#include "ConvolverPropPage.h"

#include <mediaerr.h>   // DirectX SDK media errors
#include <dmort.h>      // DirectX SDK DMO runtime support
#include <uuids.h>      // DirectX SDK media types and subtyes
#include <ks.h>         // required for WAVEFORMATEXTENSIBLE
#include <ksmedia.h>

/////////////////////////////////////////////////////////////////////////////
// CConvolver::CConvolver
//
// Constructor
/////////////////////////////////////////////////////////////////////////////

CConvolver::CConvolver() :
m_fWetMix(1.0),  // default to 100 percent wet
m_fDryMix(0.0),  // default to 0 percent dry
m_fAttenuation_db(0.0), // default attenuation
m_cbInputLength(0),
m_pbInputData (NULL),
m_bValidTime(false),
m_rtTimestamp(0),
m_bEnabled(TRUE),
m_nPartitions(0), // default, just overlap-save
m_Convolution(NULL),
m_InputSampleConvertor(NULL),
m_OutputSampleConvertor(NULL)
{
	m_szFilterFileName[0] = 0;

	::ZeroMemory(&m_mtInput, sizeof(m_mtInput));
	::ZeroMemory(&m_mtOutput, sizeof(m_mtOutput));
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::~CConvolver
//
// Destructor
/////////////////////////////////////////////////////////////////////////////

CConvolver::~CConvolver()
{
	::MoFreeMediaType(&m_mtInput);
	::MoFreeMediaType(&m_mtOutput);
	m_Convolution = NULL;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::FinalConstruct
//
// Called when an plug-in is first loaded. Use this function to do one-time
// intializations that could fail instead of doing this in the constructor,
// which cannot return an error.
/////////////////////////////////////////////////////////////////////////////

HRESULT CConvolver::FinalConstruct()
{
	CRegKey key;
	LONG    lResult;
	DWORD   dwValue;

#if defined(DEBUG) | defined(_DEBUG)

	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF | _CRTDBG_CHECK_ALWAYS_DF);
	debugstream.sink (apDebugSinkWindows::sOnly);
	apDebugSinkWindows::sOnly.showHeader (true);

	// 3 = function call trace
	apDebug::gOnly().set(3);

	DEBUGGING(3, cdebug << "FinalConstruct" << std::endl;);
#endif

	lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
	if (ERROR_SUCCESS == lResult)
	{
		// Read the wet mix value from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsWetmix, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a float.
			m_fWetMix = static_cast<float>(dwValue) / 100.0f;
			// Calculate the dry mix value.
			m_fDryMix = 1.0f - m_fWetMix;
		}

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

		try // creating m_Convolution might throw
		{
			m_Convolution = new CConvolution<float>(m_szFilterFileName,  m_nPartitions == 0 ? 1 : m_nPartitions); // 0 partitions = overlap-save
		}
		catch (...) 
		{
			return S_FALSE;  // Don't return a real error as it will prevent the property page from showing
		}
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver:::FinalRelease
//
// Called when an plug-in is unloaded. Use this function to free any
// resources allocated.
/////////////////////////////////////////////////////////////////////////////

void CConvolver::FinalRelease()
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "FinalRelease" << std::endl;);
#endif

	FreeStreamingResources();  // In case client does not call this.        
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetStreamCount
//
// Implementation of IMediaObject::GetStreamCount
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetStreamCount(DWORD *pcInputStreams,
										DWORD *pcOutputStreams)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetStreamCount" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( NULL == pcInputStreams )
	{
		return E_POINTER;
	}

	if ( NULL == pcOutputStreams )
	{
		return E_POINTER;
	}

	// The plug-in uses one stream in each direction.
	*pcInputStreams = 1;
	*pcOutputStreams = 1;

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetInputStreamInfo
//
// Implementation of IMediaObject::GetInputStreamInfo
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetInputStreamInfo( 
	DWORD dwInputStreamIndex,
	DWORD *pdwFlags)
{

#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetInputStreamInfo" << std::endl;);
#endif

	if ( NULL == pdwFlags )
	{
		return E_POINTER;
	}

	// The stream index must be zero.
	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	*pdwFlags =  DMO_OUTPUT_STREAMF_WHOLE_SAMPLES | DMO_OUTPUT_STREAMF_FIXED_SAMPLE_SIZE;

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetOutputStreamInfo
//
// Implementation of IMediaObject::GetOutputStreamInfo
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetOutputStreamInfo( 
	DWORD dwOutputStreamIndex,
	DWORD *pdwFlags)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetOutputStreamInfo" << std::endl;);
#endif

	if ( NULL == pdwFlags )
	{
		return E_POINTER;
	}

	// The stream index must be zero.
	if ( 0 != dwOutputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	*pdwFlags = DMO_OUTPUT_STREAMF_WHOLE_SAMPLES | DMO_OUTPUT_STREAMF_FIXED_SAMPLE_SIZE;

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetInputType
//
// Implementation of IMediaObject::GetInputType
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetInputType (DWORD dwInputStreamIndex,
									   DWORD dwTypeIndex,
									   DMO_MEDIA_TYPE *pmt)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetInputType" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX ;
	}

	// only support two preferred types
	if ( 1 > dwTypeIndex )
	{
		return DMO_E_NO_MORE_ITEMS;
	}

	if ( NULL == pmt )
	{
		return E_POINTER;

	}

	// if output type has been defined, use that as input type
	if (GUID_NULL != m_mtOutput.majortype)
	{
		hr = ::MoCopyMediaType( pmt, &m_mtOutput );
	}
	else
	{
		::ZeroMemory( pmt, sizeof( DMO_MEDIA_TYPE ) );
		switch (dwTypeIndex)
		{
		case 0:
			{
				pmt->majortype = MEDIATYPE_Audio;
				pmt->subtype = MEDIASUBTYPE_IEEE_FLOAT;
			}
			break;

		case 1:
			{
				pmt->majortype = MEDIATYPE_Audio;
				pmt->subtype = MEDIASUBTYPE_PCM;
			}
			break;

		default:
			hr = E_INVALIDARG;
		}
	}

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetOutputType
//
// Implementation of IMediaObject::GetOutputType
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetOutputType(DWORD dwOutputStreamIndex,
									   DWORD dwTypeIndex,
									   DMO_MEDIA_TYPE *pmt)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetOutputType" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwOutputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	// only support one preferred type
	if (1 < dwTypeIndex )
	{
		return DMO_E_NO_MORE_ITEMS;

	}

	if ( NULL == pmt )
	{
		return E_POINTER;

	}

	// if input type has been defined, use that as output type
	if (GUID_NULL != m_mtInput.majortype)
	{
		hr = ::MoCopyMediaType( pmt, &m_mtInput );
	}
	else
	{
		::ZeroMemory( pmt, sizeof( DMO_MEDIA_TYPE ) );
		switch (dwTypeIndex)
		{
		case 0:
			{
				pmt->majortype = MEDIATYPE_Audio;
				pmt->subtype = MEDIASUBTYPE_IEEE_FLOAT;
			}
			break;

		case 1:
			{
				pmt->majortype = MEDIATYPE_Audio;
				pmt->subtype = MEDIASUBTYPE_PCM;
			}
			break;

		default:
			hr = E_INVALIDARG;
		}
	}

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::SetInputType
//
// Implementation of IMediaObject::SetInputType
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::SetInputType(DWORD dwInputStreamIndex,
									  const DMO_MEDIA_TYPE *pmt,
									  DWORD dwFlags)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "SetInputType" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if ( DMO_SET_TYPEF_CLEAR & dwFlags ) 
	{
		::MoFreeMediaType(&m_mtInput);
		::ZeroMemory(&m_mtInput, sizeof(m_mtInput));

		return S_OK;
	}

	if ( NULL == pmt )
	{
		return E_POINTER;
	}

	// validate that the input media type matches our requirements and
	// and matches our output type (if set)
	hr = ValidateMediaType(pmt, &m_mtOutput);

	if (FAILED(hr))
	{
		if( DMO_SET_TYPEF_TEST_ONLY & dwFlags )
		{
			return S_FALSE;
		}
	}

	if ( 0 == dwFlags )
	{
		// free existing media type
		::MoFreeMediaType(&m_mtInput);
		::ZeroMemory(&m_mtInput, sizeof(m_mtInput));

		// copy new media type
		hr = ::MoCopyMediaType( &m_mtInput, pmt );
		if (FAILED(hr))
		{
			return hr;
		}
	}

	// TODO: Can't signal failure to get a convolution filter in FinalizeConstruction without
	// breaking access to the properties page
	if(m_Convolution.get_ptr() == NULL)
	{
		return E_POINTER;
	}
	else
	{
		WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) pmt->pbFormat;
		if (NULL == pWave)
		{
			return E_POINTER;
		}

		// Check that the filter has the right number of channels and the same sample rate as our filter
		if ((pWave->nChannels != m_Convolution->Mixer.nInputChannels) ||
#ifdef LIBSNDFILE

			(pWave->nSamplesPerSec != m_Convolution->Mixer.Paths[0].filter.sf_FilterFormat.samplerate))
#else
			(pWave->nSamplesPerSec != m_Convolution->Mixer.Paths[0].wfexFilterFormat.Format.nSamplesPerSec))
#endif
			return DMO_SET_TYPEF_TEST_ONLY & dwFlags ? S_FALSE : DMO_E_TYPE_NOT_ACCEPTED;

		hr = SelectSampleConvertor(pWave, m_InputSampleConvertor);
		if (FAILED(hr))
		{
			return hr;
		}
		else
		{
			return S_OK;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::SetOutputType
//
// Implementation of IMediaObject::SetOutputType
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::SetOutputType(DWORD dwOutputStreamIndex,
									   const DMO_MEDIA_TYPE *pmt,
									   DWORD dwFlags)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "SetOutputType" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwOutputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if( DMO_SET_TYPEF_CLEAR & dwFlags )
	{
		::MoFreeMediaType( &m_mtOutput );
		::ZeroMemory(&m_mtOutput, sizeof(m_mtOutput));

		return S_OK;
	}

	if ( NULL == pmt )
	{
		return E_POINTER;
	}

	// validate that the output media type matches our requirements and
	// and matches our input type (if set)
	hr = ValidateMediaType(pmt, &m_mtInput);

	if (FAILED(hr))
	{
		if( DMO_SET_TYPEF_TEST_ONLY & dwFlags )
		{
			return S_FALSE;
		}
	}

	if ( 0 == dwFlags )
	{
		// free existing media type
		::MoFreeMediaType(&m_mtOutput);
		::ZeroMemory(&m_mtOutput, sizeof(m_mtOutput));

		// copy new media type
		hr = ::MoCopyMediaType( &m_mtOutput, pmt );
		if (FAILED(hr))
		{
			return hr;
		}
	}

	// TODO: Can't signal failure to get a convolution filter in FinalizeConstruction without
	// breaking access to the properties page
	if(m_Convolution.get_ptr() == NULL)
	{
		return E_POINTER;
	}
	else
	{
		WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) pmt->pbFormat;
		if (NULL == pWave)
		{
			return E_POINTER;
		}

		// Check that the filter has the right number of channels and the same sample rate as our filter
		if ((pWave->nChannels != m_Convolution->Mixer.nOutputChannels) ||
#ifdef LIBSNDFILE
			(pWave->nSamplesPerSec != m_Convolution->Mixer.Paths[0].filter.sf_FilterFormat.samplerate))
#else
			(pWave->nSamplesPerSec != m_Convolution->Mixer.Paths[0].wfexFilterFormat.Format.nSamplesPerSec))

#endif
			return DMO_SET_TYPEF_TEST_ONLY & dwFlags ? S_FALSE : DMO_E_TYPE_NOT_ACCEPTED;

		hr = SelectSampleConvertor(pWave, m_OutputSampleConvertor);
		if (FAILED(hr))
		{
			return hr;
		}
		else
		{
			return S_OK;
		}
	}
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetInputCurrentType
//
// Implementation of IMediaObject::GetInputCurrentType
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetInputCurrentType( 
	DWORD dwInputStreamIndex,
	DMO_MEDIA_TYPE *pmt)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetInputCurrentType" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if ( NULL == pmt )
	{
		return E_POINTER;
	}

	if (GUID_NULL == m_mtInput.majortype)
	{
		return DMO_E_TYPE_NOT_SET;
	}

	hr = ::MoCopyMediaType( pmt, &m_mtInput );

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetOutputCurrentType
//
// Implementation of IMediaObject::GetOutputCurrentType
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetOutputCurrentType( 
	DWORD dwOutputStreamIndex,
	DMO_MEDIA_TYPE *pmt)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetOutputCurrentType" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwOutputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if ( NULL == pmt )
	{
		return E_POINTER;
	}

	if (GUID_NULL == m_mtOutput.majortype)
	{
		return DMO_E_TYPE_NOT_SET;
	}

	hr = ::MoCopyMediaType( pmt, &m_mtOutput );

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetInputSizeInfo
//
// Implementation of IMediaObject::GetInputSizeInfo
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetInputSizeInfo( 
	DWORD dwInputStreamIndex,
	DWORD *pcbSize,
	DWORD *pcbMaxLookahead,
	DWORD *pcbAlignment)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetInputSizeInfo" << std::endl;);
#endif

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if ( NULL == pcbSize )
	{
		return E_POINTER;
	}

	if ( NULL == pcbMaxLookahead )
	{
		return E_POINTER;
	}

	if ( NULL == pcbAlignment )
	{
		return E_POINTER;
	}

	if (GUID_NULL == m_mtInput.majortype)
	{
		return DMO_E_TYPE_NOT_SET;
	}

	// Return the input sample size, in bytes.
	*pcbSize = m_mtInput.lSampleSize;

	// Get the pointer to the input format structure.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;

	// Return the input buffer alignment, in bytes.
	*pcbAlignment = pWave->nBlockAlign;

	if(m_Convolution.get_ptr() != NULL)
	{
		// This plug-in lags its output by half a partition length
		*pcbMaxLookahead = m_Convolution->cbLookAhead(m_InputSampleConvertor);
	}
	else
	{
		*pcbMaxLookahead = 0;
		return E_FAIL;
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetOutputSizeInfo
//
// Implementation of IMediaObject::GetOutputSizeInfo
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetOutputSizeInfo( 
	DWORD dwOutputStreamIndex,
	DWORD *pcbSize,
	DWORD *pcbAlignment)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetOutputSizeInfo" << std::endl;);
#endif

	if ( 0 != dwOutputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if ( NULL == pcbSize )
	{
		return E_POINTER;
	}

	if ( NULL == pcbAlignment )
	{
		return E_POINTER;
	}

	if (GUID_NULL == m_mtOutput.majortype)
	{
		return DMO_E_TYPE_NOT_SET;
	}

	// Return the output sample size, in bytes.
	*pcbSize = m_mtOutput.lSampleSize;

	// Get the pointer to the output format structure.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtOutput.pbFormat;

	// Return the output buffer aligment, in bytes.
	*pcbAlignment = pWave->nBlockAlign;

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetInputMaxLatency
//
// Implementation of IMediaObject::GetInputMaxLatency
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetInputMaxLatency( 
	DWORD dwInputStreamIndex,
	REFERENCE_TIME *prtMaxLatency)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetInputMaxLatency" << std::endl;);
#endif

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if(m_Convolution.get_ptr() != NULL)
	{
		// Not clear when this routine is called.
		WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;
		if ( NULL == pWave)
		{
			return E_FAIL;
		}

		//// Latency is half a partition length
		*prtMaxLatency = ::MulDiv( m_Convolution->cbLookAhead(m_InputSampleConvertor), UNITS, pWave->nAvgBytesPerSec);

		return S_OK;
	}
	else
		return E_FAIL;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::SetInputMaxLatency
//
// Implementation of IMediaObject::SetInputMaxLatency
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::SetInputMaxLatency( 
	DWORD dwInputStreamIndex,
	REFERENCE_TIME rtMaxLatency)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "SetInputMaxLatency" << std::endl;);
#endif

	// TODO: work out how to use this, when partitioned convoltion implemented
	return E_NOTIMPL; // Not dealing with latency in this plug-in.
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::Flush
//
// Implementation of IMediaObject::Flush
//
// The DMO performs the following actions when this method is called: 
//
// Releases any IMediaBuffer references it holds. 
// Discards any values that specify the time stamp or sample length for a media buffer. 
// Reinitializes any internal states that depend on the contents of a media sample. 
// Media types, maximum latency, and locked state do not change.
//
// When the method returns, every input stream accepts data.
// Output streams cannot produce any data until the application calls the 
// IMediaObject::ProcessInput method on at least one input stream.
//
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::Flush( void )
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Flush" << std::endl;);
#endif

	m_spInputBuffer = NULL;  // release smart pointer
	m_cbInputLength = 0;
	m_pbInputData = NULL;
	m_bValidTime = false;
	m_rtTimestamp = 0;

	if (m_Convolution.get_ptr())
	{
		m_Convolution->Flush();
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::Discontinuity
//
// Implementation of IMediaObject::Discontinuity
//
// A discontinuity represents a break in the input. 
// A discontinuity might occur because no more data is expected, the format 
// is changing, or there is a gap in the data. After a discontinuity, the DMO
// does not accept further input on that stream until all pending data has 
// been processed. The application should call the ProcessOutput method until 
// none of the streams returns the DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE flag.
//
// This method might fail if it is called before the client sets the input 
// and output types on the DMO. 
//
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::Discontinuity(DWORD dwInputStreamIndex)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Discontinuity" << std::endl;);
#endif

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	// TODO:: Check that this is enough
	return S_OK;
}

//
//void CConvolver::FillBufferWithSilence(WAVEFORMATEX *pWfex)
//{
//	
//	if (8 == pWfex->wBitsPerSample)
//	{
//		::FillMemory(m_pbFilter, m_cbFilter, 0x80);
//	}
//	else
//		::ZeroMemory(m_pbFilter, m_cbFilter);
//}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::AllocateStreamingResources
//
// Implementation of IMediaObject::AllocateStreamingResources
/////////////////////////////////////////////////////////////////////////////
STDMETHODIMP CConvolver::AllocateStreamingResources ( void )
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "AllocateStreamingResources" << std::endl;);
#endif

	HRESULT hr = S_OK;

	// Allocate any buffers need to process the stream.

	//// Get a pointer to the WAVEFORMATEX structure.
	//WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;
	//if (NULL == pWave)
	//{
	//	return E_FAIL;
	//}

	//FillBufferWithSilence(pWave);

	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::FreeStreamingResources
//
// Implementation of IMediaObject::FreeStreamingResources
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::FreeStreamingResources( void )
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "FreeStreamingResources" << std::endl;);
#endif

	m_spInputBuffer = NULL; // release smart pointer
	m_cbInputLength = 0;
	m_pbInputData = NULL;
	m_bValidTime = false;
	m_rtTimestamp = 0;

	// Don't delete this here; only when filter name or partition number changes
	//m_Convolution = NULL;

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetInputStatus
//
// Implementation of IMediaObject::GetInputStatus
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetInputStatus(DWORD dwInputStreamIndex,
										DWORD *pdwFlags)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetInputStatus" << std::endl;);
#endif

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if ( NULL == pdwFlags )
	{
		return E_POINTER;
	}

	if ( m_spInputBuffer )
	{
		*pdwFlags = 0; //The buffer still contains data; return zero.
	}
	else
	{
		*pdwFlags = DMO_INPUT_STATUSF_ACCEPT_DATA; // OK to call ProcessInput.
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::ProcessInput
//
// Implementation of IMediaObject::ProcessInput
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::ProcessInput(DWORD dwInputStreamIndex,
									  IMediaBuffer *pBuffer,
									  DWORD dwFlags,
									  REFERENCE_TIME rtTimestamp,
									  REFERENCE_TIME rtTimelength)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ProcessInput" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if ( NULL == pBuffer )
	{
		return E_POINTER;
	}

	if (GUID_NULL == m_mtInput.majortype)
	{
		return DMO_E_TYPE_NOT_SET;
	}

	// Get a pointer to the actual data and length information.
	BYTE    *pbInputData = NULL;
	DWORD   cbInputLength = 0;
	hr = pBuffer->GetBufferAndLength(&pbInputData, &cbInputLength);
	if (FAILED(hr))
	{
		return hr;
	}

	// Hold on to the buffer using a smart pointer.
	m_spInputBuffer = pBuffer;
	m_pbInputData = pbInputData;
	m_cbInputLength = cbInputLength;

	//Verify that buffer's time stamp is valid.
	if (dwFlags & DMO_INPUT_DATA_BUFFERF_TIME)
	{
		m_bValidTime = true;
		m_rtTimestamp = rtTimestamp;
	}
	else
	{
		m_bValidTime = false;
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::ProcessOutput
//
// Implementation of IMediaObject::ProcessOutput
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::ProcessOutput(DWORD dwFlags,
									   DWORD cOutputBufferCount,
									   DMO_OUTPUT_DATA_BUFFER *pOutputBuffers,
									   DWORD *pdwStatus)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ProcessOutput" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( NULL == pOutputBuffers )
	{
		return E_POINTER;
	}

	// this plug-in only supports one output buffer
	if (1 != cOutputBufferCount)
	{
		return E_INVALIDARG;
	}

	if (GUID_NULL == m_mtOutput.majortype)
	{
		return DMO_E_TYPE_NOT_SET;
	}

	if (pdwStatus)
	{
		*pdwStatus = 0;
	}

	// make sure input and output buffer exist
	IMediaBuffer *pOutputBuffer = pOutputBuffers[0].pBuffer;

	if ((!m_spInputBuffer) || (!pOutputBuffer))
	{
		if (pOutputBuffer)
		{
			pOutputBuffer->SetLength(0);
		}

		pOutputBuffers[0].dwStatus = 0;

		return S_FALSE;
	}

	BYTE         *pbOutputData = NULL;
	DWORD        cbOutputMaxLength = 0;
	DWORD        cbBytesToProcess = 0;
	DWORD		 cbBytesGenerated = 0;

	// Get current length of output buffer
	hr = pOutputBuffer->GetBufferAndLength(&pbOutputData, &cbOutputMaxLength);
	if (FAILED(hr))
	{
		return hr;
	}

	// Get max length of output buffer
	hr = pOutputBuffer->GetMaxLength(&cbOutputMaxLength);
	if (FAILED(hr))
	{
		return hr;
	}

	// Calculate how many bytes we can process
	bool bComplete = false; // The entire buffer is not yet processed.

	if (m_cbInputLength > cbOutputMaxLength)
	{
		cbBytesToProcess = cbOutputMaxLength; // only process as much of the input as can fit in the output
	}
	else
	{
		cbBytesToProcess = m_cbInputLength; // process entire input buffer
		bComplete = true;                   // the entire input buffer has been processed. 
	}

	// Call the internal processing method, which returns the no. bytes processed
	hr = DoProcessOutput(pbOutputData, m_pbInputData, &cbBytesToProcess, &cbBytesGenerated);
	if (FAILED(hr))
	{
		return hr;
	}

	// Set the size of the valid data in the output buffer.
	hr = pOutputBuffer->SetLength(cbBytesGenerated);
	if (FAILED(hr))
	{
		return hr;
	}

	// Update the DMO_OUTPUT_DATA_BUFFER information for the output buffer.
	pOutputBuffers[0].dwStatus = 0;

	if (m_bValidTime)
	{
		// store start time of output buffer
		pOutputBuffers[0].dwStatus |= DMO_OUTPUT_DATA_BUFFERF_TIME;
		pOutputBuffers[0].rtTimestamp = m_rtTimestamp;

		// Get the pointer to the output format structure.
		WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtOutput.pbFormat;

		// estimate time length of output buffer
		pOutputBuffers[0].dwStatus |= DMO_OUTPUT_DATA_BUFFERF_TIMELENGTH;
		pOutputBuffers[0].rtTimelength = ::MulDiv(cbBytesGenerated, UNITS, pWave->nAvgBytesPerSec);

		// this much time has been consumed, so move the time stamp accordingly
		m_rtTimestamp += pOutputBuffers[0].rtTimelength;
	}

	if (bComplete) 
	{
		m_spInputBuffer = NULL;   // Release smart pointer
		m_cbInputLength = 0;
		m_pbInputData = NULL;
		m_bValidTime = false;
		m_rtTimestamp = 0;
	}
	else 
	{
		// Let the client know there is still data that needs processing 
		// in the input buffer.
		pOutputBuffers[0].dwStatus |= DMO_OUTPUT_DATA_BUFFERF_INCOMPLETE;
		m_pbInputData += cbBytesToProcess;
		m_cbInputLength -= cbBytesToProcess;
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::Lock
//
// Implementation of IMediaObject::Lock
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::Lock( LONG bLock )
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Lock" << std::endl;);
#endif

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::SetEnable
//
// Implementation of IWMPPluginEnable::SetEnable
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::SetEnable( BOOL fEnable )
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "SetEnable" << std::endl;);
#endif

	// This function is called when the plug-in is being enabled or disabled,
	// typically by user action. Once a plug-in is disabled, it will still be
	// loaded into the graph but ProcessInput and ProcessOutput will not be called.

	// This function allows any state or UI associated with the plug-in to reflect the
	// enabled/disable state of the plug-in

	m_bEnabled = fEnable;

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetEnable
//
// Implementation of IWMPPluginEnable::GetEnable
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetEnable( BOOL *pfEnable )
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetEnable" << std::endl;);
#endif

	if ( NULL == pfEnable )
	{
		return E_POINTER;
	}

	*pfEnable = m_bEnabled;

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetPages
//
// Implementation of ISpecifyPropertyPages::GetPages
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetPages(CAUUID *pPages)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetPages" << std::endl;);
#endif

	const unsigned CPROPPAGES = 1;
	GUID *pGUID = (GUID*) CoTaskMemAlloc( CPROPPAGES * sizeof(GUID) );

	pPages->cElems = 0;
	pPages->pElems = NULL;

	if( NULL == pGUID )
	{
		return E_OUTOFMEMORY;
	}

	// Fill the array of property pages now
	pGUID[0] = CLSID_ConvolverPropPage;

	//Fill the structure and return
	pPages->cElems = CPROPPAGES;
	pPages->pElems = pGUID;
	return S_OK;
}

// Property get to retrieve the wet mix value by using the public interface.
STDMETHODIMP CConvolver::get_wetmix(float *pVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_wetmix" << std::endl;);
#endif

	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_fWetMix;

	return S_OK;
}

// Property put to store the wet mix value by using the public interface.
STDMETHODIMP CConvolver::put_wetmix(float newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_wetmix" << std::endl;);
#endif

	m_fWetMix = newVal;

	// Calculate m_fDryMix
	m_fDryMix = 1.0f - m_fWetMix;

	return S_OK;
}

// Property get to retrieve the wet mix value by using the public interface.
STDMETHODIMP CConvolver::get_attenuation(float *pVal)
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
STDMETHODIMP CConvolver::put_attenuation(float newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_attenuation" << std::endl;);
#endif

	m_fAttenuation_db = newVal;

	return S_OK;
}

// Property get to retrieve the filter filename by using the public interface.
STDMETHODIMP CConvolver::get_filterfilename(TCHAR *pVal[])
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
STDMETHODIMP CConvolver::put_filterfilename(TCHAR newVal[])
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_filterfilename" << std::endl;);
#endif

	_tcsncpy(m_szFilterFileName, newVal, MAX_PATH);

	try // creating m_Convolution might throw
	{
		m_Convolution = new CConvolution<float>(m_szFilterFileName,  m_nPartitions == 0 ? 1 : m_nPartitions); // 0 partitions = overlap-save
	}
	catch (HRESULT& hr)
	{
		return hr;
	}
	catch (...) 
	{
		return E_FAIL;
	}

	return S_OK;
}

// Property get to retrieve the wet mix value by using the public interface.
STDMETHODIMP CConvolver::get_partitions(DWORD *pVal)
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

// Property put to store the wet mix value by using the public interface.
STDMETHODIMP CConvolver::put_partitions(DWORD newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_partitions" << std::endl;);
#endif

	m_nPartitions = newVal;

	try // creating m_Convolution might throw
	{
		m_Convolution = new CConvolution<float>(m_szFilterFileName,  m_nPartitions == 0 ? 1 : m_nPartitions); // 0 partitions = overlap-save
	}
	catch (HRESULT& hr)
	{
		return hr;
	}
	catch (...) 
	{
		return E_FAIL;
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::DoProcessOutput
//
// Convert the input buffer to the output buffer
/////////////////////////////////////////////////////////////////////////////

HRESULT CConvolver::DoProcessOutput(BYTE *pbOutputData,
									const BYTE *pbInputData,
									DWORD *cbInputBytesToProcess,
									DWORD *cbOutputBytesGenerated)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "DoProcessOutput" << std::endl;);
#endif

	// see if the plug-in has been disabled by the user
	if (!m_bEnabled)
	{
		// if so, just copy the data without changing it. You should
		// also do any necessary format conversion here.
		memcpy(pbOutputData, pbInputData, *cbInputBytesToProcess);
		*cbOutputBytesGenerated = *cbInputBytesToProcess;

		return S_OK;
	}

	if(m_Convolution.get_ptr() == NULL)
	{
		return E_FAIL;
	}

	// Get a pointer to the valid WAVEFORMATEX structure
	// for the current media type.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat; // TODO: reinterpret_cast?

	// Calculate the number of blocks to process.  A block contains the Samples for all channels
	DWORD dwBlocksToProcess = (*cbInputBytesToProcess / pWave->nBlockAlign);

	// Convolve the pbInputData to produce pbOutputData (of the same length)
	*cbOutputBytesGenerated = m_nPartitions == 0 ?
		m_Convolution->doConvolution(pbInputData, pbOutputData,
		m_InputSampleConvertor, m_OutputSampleConvertor,
		dwBlocksToProcess,
		m_fAttenuation_db,
		m_fWetMix, m_fDryMix)
		: 	m_Convolution->doPartitionedConvolution(pbInputData, pbOutputData,
		m_InputSampleConvertor, m_OutputSampleConvertor,
		dwBlocksToProcess,
		m_fAttenuation_db,
		m_fWetMix, m_fDryMix);

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::ValidateMediaType
//
// Validate that the media type is acceptable
/////////////////////////////////////////////////////////////////////////////

HRESULT CConvolver::ValidateMediaType(const DMO_MEDIA_TYPE *pmtTarget, const DMO_MEDIA_TYPE *pmtPartner)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ValidateMediaType" << std::endl;);
#endif

	if (pmtPartner == NULL)
	{
		return E_POINTER;
	}

	// make sure the target media type has the fields we require
	if( ( MEDIATYPE_Audio != pmtTarget->majortype ) ||
		( FORMAT_WaveFormatEx != pmtTarget->formattype ) ||
		( pmtTarget->cbFormat < sizeof( WAVEFORMATEX )) ||
		( NULL == pmtTarget->pbFormat) )
	{
		return DMO_E_TYPE_NOT_ACCEPTED;
	}


	if (pmtTarget == NULL)
	{
		return E_POINTER;
	}

	// make sure the wave header has the fields we require
	WAVEFORMATEX *pWave = (WAVEFORMATEX *) pmtTarget->pbFormat;

	if ((0 == pWave->nChannels) ||
		(0 == pWave->nSamplesPerSec) ||
		(0 == pWave->nAvgBytesPerSec) ||
		(0 == pWave->nBlockAlign) ||
		(0 == pWave->wBitsPerSample))
	{
		return DMO_E_TYPE_NOT_ACCEPTED;
	}

	// make sure this is a supported container size
	if ((8  != pWave->wBitsPerSample) &&
		(16 != pWave->wBitsPerSample) &&
		(24 != pWave->wBitsPerSample) &&
		(32 != pWave->wBitsPerSample))
	{
		return DMO_E_TYPE_NOT_ACCEPTED;
	}

	// make sure the wave format is acceptable
	switch (pWave->wFormatTag)
	{
	case WAVE_FORMAT_PCM:

		// make sure sample size is 8 or 16-bit
		if ((8  != pWave->wBitsPerSample) &&
			(16 != pWave->wBitsPerSample))
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}

		// channel-to-speaker specification for WAVE_FORMAT_PCM is undefined for the case of nChannels > 2
		// But let it through
		// See http://www.microsoft.com/whdc/device/audio/multichaud.mspx
		if (pWave->nChannels > 2)
		{
			//return DMO_E_TYPE_NOT_ACCEPTED;
		}
		break;

	case WAVE_FORMAT_IEEE_FLOAT:
		// make sure sample size is 32-bit
		if ((32 != pWave->wBitsPerSample) && (64 != pWave->wBitsPerSample))
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}

		// channel-to-speaker specification for WAVE_FORMAT_IEEE_FLOAT is undefined for the case of nChannels > 2
		// But let it through
		// See http://www.microsoft.com/whdc/device/audio/multichaud.mspx
		if (pWave->nChannels > 2)
		{
			//return DMO_E_TYPE_NOT_ACCEPTED;
		}
		break;

	case WAVE_FORMAT_EXTENSIBLE:
		{
			WAVEFORMATEXTENSIBLE *pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;

			if(pWaveXT->Format.cbSize >= 22) // required (see http://www.microsoft.com/whdc/device/audio/multichaud.mspx)
			{
				return DMO_E_TYPE_NOT_ACCEPTED;
			}

			if(pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
			{
				// make sure the wave format extensible has the fields we require
				if ((0 == pWaveXT->Samples.wSamplesPerBlock) ||
					(pWaveXT->Samples.wValidBitsPerSample > pWave->wBitsPerSample) ||
					(pWaveXT->Format.cbSize != 22))
				{
					return DMO_E_TYPE_NOT_ACCEPTED;
				}

				// for 8 or 16-bit, the container and sample size must match
				if ((8  == pWave->wBitsPerSample) ||
					(16 == pWave->wBitsPerSample))
				{
					if (pWave->wBitsPerSample != pWaveXT->Samples.wValidBitsPerSample)
					{
						return DMO_E_TYPE_NOT_ACCEPTED;
					}
				}
				else 
				{
					// for any other container size, make sure the valid
					// bits per sample is a value we support
					if ((16 != pWaveXT->Samples.wValidBitsPerSample) &&
						(20 != pWaveXT->Samples.wValidBitsPerSample) &&
						(24 != pWaveXT->Samples.wValidBitsPerSample) &&
						(32 != pWaveXT->Samples.wValidBitsPerSample))
					{
						return DMO_E_TYPE_NOT_ACCEPTED;
					}
				}
			}
			else
				if(pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
				{
					{
						if ((32 != pWave->wBitsPerSample) ||
							(32 != pWaveXT->Samples.wValidBitsPerSample)  ||
							(64 != pWave->wBitsPerSample) ||
							(64 != pWaveXT->Samples.wValidBitsPerSample)  ||
							(pWaveXT->Format.cbSize != 22))
						{
							return DMO_E_TYPE_NOT_ACCEPTED;
						}
					}
				}
				else // Not a recognised SubFormat
					return DMO_E_TYPE_NOT_ACCEPTED;
		}
		break;

	default:
		return DMO_E_TYPE_NOT_ACCEPTED;
	}

	// if the partner media type is configured, make sure it matches the target.
	// this is done because this plug-in requires the same input and output types
	if (GUID_NULL != pmtPartner->majortype)
	{
		if ((pmtTarget->majortype != pmtPartner->majortype) ||
			(pmtTarget->subtype   != pmtPartner->subtype))
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}

		// make sure the wave headers for the target and the partner match
		WAVEFORMATEX *pPartnerWave = (WAVEFORMATEX *) pmtPartner->pbFormat;

		if ((pWave->nChannels != pPartnerWave->nChannels) ||
			(pWave->nSamplesPerSec != pPartnerWave->nSamplesPerSec) ||
			(pWave->nAvgBytesPerSec != pPartnerWave->nAvgBytesPerSec) ||
			(pWave->nBlockAlign != pPartnerWave->nBlockAlign) ||
			(pWave->wBitsPerSample != pPartnerWave->wBitsPerSample) ||
			(pWave->wFormatTag != pPartnerWave->wFormatTag))
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}
	}

	// media type is valid
	return S_OK;
}

#ifdef IPROPERTYBAG
// Helper

IStream* StreamFromResource(int id)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "StreamFromResource" << std::endl;);
#endif

	HMODULE hModule;
	HRSRC hrsrc = NULL;
	int len = 0;
	HGLOBAL hres = NULL;
	LPVOID pres = NULL;
	IStream* pstm = NULL;

	hModule = GetModuleHandle(TEXT("convolverWMP.dll"));			
	hrsrc = FindResource(hModule, MAKEINTRESOURCE(id), TEXT("STREAM")); // TODO: does not work
	len = SizeofResource(hModule, hrsrc);
	hres = LoadResource(hModule, hrsrc);
	pres = LockResource(hres);
	if (pres)
	{
		HGLOBAL hmem = GlobalAlloc(GMEM_FIXED, len);
		BYTE* pmem = (BYTE*)GlobalLock(hmem);

		if (pmem)
		{
			memcpy(pmem, pres, len);
			CreateStreamOnHGlobal(hmem, FALSE, &pstm);
		}

		DeleteObject(hres);
		DeleteObject(hrsrc);
	}

	if (hres) DeleteObject(hres);
	if (hrsrc) DeleteObject(hrsrc);

	return pstm;
}


// IPropertyBag method
STDMETHODIMP CConvolver::Read(LPCOLESTR pszPropName,VARIANT *pVar, IErrorLog *pErrorLog)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "Read" << std::endl;);
#endif

	if (lstrcmpW(pszPropName, L"IconStreams") == 0)
	{
		//HMODULE hModule = GetModuleHandle(TEXT("convolverWMP.dll"));			
#undef ANIMATE
#ifdef ANIMATE
		SAFEARRAYBOUND sab;
		sab.cElements = 4;
		sab.lLbound = 0;
		SAFEARRAY *psa = SafeArrayCreate(VT_UNKNOWN, 1, &sab);

		if (psa)
		{
			long rgIndices;
			for (rgIndices = 0; rgIndices < 4; rgIndices++)
				SafeArrayPutElement(psa, &rgIndices, StreamFromResource(IDB_ICONSTREAM0 + rgIndices));
		}

		pVar->punkVal = (IUnknown *)psa;
		pVar->vt = VT_ARRAY;

#else
		pVar->punkVal = StreamFromResource(IDI_ICONCONVOLVER);
		pVar->vt = VT_UNKNOWN;
#endif
		if (pVar->punkVal != NULL)
			return S_OK;
		else
			return E_FAIL;
	}
	return E_NOTIMPL;
}
#endif
