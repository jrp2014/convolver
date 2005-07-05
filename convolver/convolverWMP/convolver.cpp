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

CConvolver::CConvolver()
{
	m_fWetMix = 0.50;  // default to 50 percent wet
	m_fDryMix = 0.50;  // default to 50 percent dry

	m_fAttenuation_db = 0; // default attenuation

	m_szFilterFileName[0] = 0;
	::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));

	m_cbInputLength = 0;
	m_pbInputData = NULL;
	m_bValidTime = false;
	m_rtTimestamp = 0;
	m_bEnabled = TRUE;

	m_Convolution=NULL;
	m_Filter = NULL;

	::ZeroMemory(&m_mtInput, sizeof(m_mtInput));
	::ZeroMemory(&m_mtOutput, sizeof(m_mtOutput));

#if defined(DEBUG) | defined(_DEBUG)
	m_CWaveFileTrace = NULL;
#endif
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
	debugstream.sink (apDebugSinkWindows::sOnly);
	apDebugSinkWindows::sOnly.showHeader (true);
#endif

	lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
	if (ERROR_SUCCESS == lResult)
	{
		// Read the wet mix value from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsWetmix, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a double.
			m_fWetMix = static_cast<double>(dwValue) / 100.0;
			// Calculate the dry mix value.
			m_fDryMix = 1.0 - m_fWetMix;
		}

		// Read the attenuation value from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsAttenuation, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a double.
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
		else
			m_szFilterFileName[0] = 0;

		//// TODO: Could try to load the filter at the outset, but
		//// -- What happens if it is null, as it will be before we have chosen a filter?
		//// -- What happens if we change formats in mid-flight?
		//// so do it in AllocateStreamingResources, for now
		//if (m_szFilterFileName == TEXT(""))
		//{
		//	HRESULT hr = LoadFilter();
		//	if ( FAILED(hr))
		//		return hr;
		//}
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
	if ( NULL == pdwFlags )
	{
		return E_POINTER;
	}

	// The stream index must be zero.
	if ( 0 != dwInputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	// Use the default input stream configuration (a single stream). 
	*pdwFlags = 0;

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
	if ( NULL == pdwFlags )
	{
		return E_POINTER;
	}

	// The stream index must be zero.
	if ( 0 != dwOutputStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	// Use the default output stream configuration (a single stream).
	*pdwFlags = 0;

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
			hr = S_FALSE;
		}
	}
	else if ( 0 == dwFlags )
	{
		// free existing media type
		::MoFreeMediaType(&m_mtInput);
		::ZeroMemory(&m_mtInput, sizeof(m_mtInput));

		// copy new media type
		hr = ::MoCopyMediaType( &m_mtInput, pmt );
	}

	return hr;
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
			hr = S_FALSE;
		}
	}
	else if ( 0 == dwFlags )
	{
		// free existing media type
		::MoFreeMediaType(&m_mtOutput);
		::ZeroMemory(&m_mtOutput, sizeof(m_mtOutput));

		// copy new media type
		hr = ::MoCopyMediaType( &m_mtOutput, pmt );
	}

	return hr;
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

	// This plug-in doesn't perform lookahead. Return zero.
	*pcbMaxLookahead = 0;

	// Get the pointer to the input format structure.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;

	// Return the input buffer alignment, in bytes.
	*pcbAlignment = pWave->nBlockAlign;

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
	// TODO: work out how to use this, when partitioned convoltion implemented
	return E_NOTIMPL; // Not dealing with latency in this plug-in.
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
	return E_NOTIMPL; // Not dealing with latency in this plug-in.
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::Flush
//
// Implementation of IMediaObject::Flush
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::Flush( void )
{
	m_spInputBuffer = NULL;  // release smart pointer
	m_cbInputLength = 0;
	m_pbInputData = NULL;
	m_bValidTime = false;
	m_rtTimestamp = 0;

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::Discontinuity
//
// Implementation of IMediaObject::Discontinuity
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::Discontinuity(DWORD dwInputStreamIndex)
{
	// TODO:: Check that this is enough
	return S_OK;
}


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
	// Allocate any buffers need to process the stream.

	HRESULT hr = S_OK;


	// Get a pointer to the WAVEFORMATEX structure.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;
	if (NULL == pWave)
	{
		return E_FAIL;
	}

	// Set m_Filter and m_WfexFilterFormat
	hr = LoadFilter<float>();
	if (FAILED(hr))
		return hr;

	// Check that the filter has the same number of channels and sample rate as the input
	if ((pWave->nChannels != m_WfexFilterFormat.nChannels) ||
		(pWave->nSamplesPerSec != m_WfexFilterFormat.nSamplesPerSec))
		goto cleanup;

	hr = SelectConvolution(pWave); // Sets m_Convolution 
	if (FAILED(hr))
		goto cleanup;

#if defined(DEBUG) | defined(_DEBUG)
	m_CWaveFileTrace = new CWaveFile;
	if (FAILED(hr = m_CWaveFileTrace->Open(TEXT("c:\\temp\\Trace.wav"), pWave, WAVEFILE_WRITE)))
	{
		SAFE_DELETE( m_CWaveFileTrace );
		ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
		delete m_Filter;
		m_Filter = NULL;
		return DXTRACE_ERR_MSGBOX(TEXT("Failed to open trace file for writing"), hr);
	}
#endif

	// TODO: Fill the buffer with values representing silence.
	// FillBufferWithSilence(pWave);

	return hr;

cleanup:
	ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
	delete m_Filter;
	m_Filter = NULL;
	return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::FreeStreamingResources
//
// Implementation of IMediaObject::FreeStreamingResources
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::FreeStreamingResources( void )
{
	m_spInputBuffer = NULL; // release smart pointer
	m_cbInputLength = 0;
	m_pbInputData = NULL;
	m_bValidTime = false;
	m_rtTimestamp = 0;


	delete m_Filter;
	m_Filter = NULL;

#if defined(DEBUG) | defined(_DEBUG)
	if (m_CWaveFileTrace)
		SAFE_DELETE(m_CWaveFileTrace);

	_CrtMemDumpAllObjectsSince( NULL );

	_CrtDumpMemoryLeaks();
#endif

	delete m_Convolution;
	m_Convolution = NULL;

	delete m_Filter;
	m_Filter = NULL;

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
	DWORD        cbBytesProcessed = 0;

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
		cbBytesProcessed = cbOutputMaxLength; // only process as much of the input as can fit in the output
	}
	else
	{
		cbBytesProcessed = m_cbInputLength; // process entire input buffer
		bComplete = true;                   // the entire input buffer has been processed. 
	}

	// Call the internal processing method, which returns the no. bytes processed
	hr = DoProcessOutput(pbOutputData, m_pbInputData, &cbBytesProcessed);
	if (FAILED(hr))
	{
		return hr;
	}

	// Set the size of the valid data in the output buffer.
	hr = pOutputBuffer->SetLength(cbBytesProcessed);
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
		pOutputBuffers[0].rtTimelength = ::MulDiv(cbBytesProcessed, UNITS, pWave->nAvgBytesPerSec);

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
		m_pbInputData += cbBytesProcessed;
		m_cbInputLength -= cbBytesProcessed;
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
	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::SetEnable
//
// Implementation of IWMPPluginEnable::SetEnable
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::SetEnable( BOOL fEnable )
{
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
	const unsigned CPROPPAGES = 2;
	GUID *pGUID = (GUID*) CoTaskMemAlloc( CPROPPAGES * sizeof(GUID) );

	pPages->cElems = 0;
	pPages->pElems = NULL;

	if( NULL == pGUID )
	{
		return E_OUTOFMEMORY;
	}

	// Fill the array of property pages now
#ifdef DMO
	pGUID[0] = CLSID_ConvolverPropPageDMO;
	pGUID[1] = CLSID_ConvolverPropPageDMO;
#else
	pGUID[0] = CLSID_ConvolverPropPage;
	pGUID[1] = CLSID_ConvolverPropPage;
#endif

	//Fill the structure and return
	pPages->cElems = CPROPPAGES;
	pPages->pElems = pGUID;
	return S_OK;
}

// Property get to retrieve the wet mix value by using the public interface.
STDMETHODIMP CConvolver::get_wetmix(double *pVal)
{
	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_fWetMix;

	return S_OK;
}

// Property put to store the wet mix value by using the public interface.
STDMETHODIMP CConvolver::put_wetmix(double newVal)
{
	m_fWetMix = newVal;

	// Calculate m_fDryMix
	m_fDryMix = 1.0 - m_fWetMix;

	return S_OK;
}

// Property get to retrieve the wet mix value by using the public interface.
STDMETHODIMP CConvolver::get_attenuation(double *pVal)
{
	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_fAttenuation_db;

	return S_OK;
}

// Property put to store the wet mix value by using the public interface.
STDMETHODIMP CConvolver::put_attenuation(double newVal)
{
	m_fAttenuation_db = newVal;

	return S_OK;
}

// Property get to retrieve the filter filename by using the public interface.
STDMETHODIMP CConvolver::get_filterfilename(TCHAR *pVal[])
{
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
	_tcsncpy(m_szFilterFileName, newVal, MAX_PATH);

	return S_OK;
}

// Property get to retrieve the filter format by using the public interface.
STDMETHODIMP CConvolver::get_filterformat(WAVEFORMATEX *pVal)
{
	if ( NULL == pVal )
	{
		return E_POINTER;
	}

	*pVal = m_WfexFilterFormat;

	return S_OK;
}

// Property put to store the filter format by using the public interface.
STDMETHODIMP CConvolver::put_filterformat(WAVEFORMATEX newVal)
{
	m_WfexFilterFormat = newVal;

	return S_OK;
}

// Convolve the filter with white noise to get the maximum output, from which we calculate the maximum attenuation
STDMETHODIMP CConvolver::calculateOptimumAttenuation(double& fAttenuation)
{
	HRESULT hr = S_OK;

	if ( NULL == m_Filter )
	{
		hr = LoadFilter<float>();
		if ( FAILED(hr) )
			return hr;
	}

	const WORD SAMPLES = 10; // length of the test sample, in filter lengths
	const DWORD nBufferLength = m_Filter->nChannels * m_Filter->nSamples * SAMPLES;
	const DWORD cBufferLength = nBufferLength * sizeof(float);


	BYTE* inputSamples = new BYTE[cBufferLength];
	BYTE* outputSamples = new BYTE[cBufferLength];
	float* pfInputSample = NULL;
	float* pfOutputSample = NULL;

	// Seed the random-number generator with current time so that
	// the numbers will be different every time we run.
	srand( (unsigned)time( NULL ) );

	CConvolution<float>* c = new Cconvolution_ieeefloat<float>(m_Filter);

	bool again = TRUE;
	float maxSample = 0;

	do
	{
		pfInputSample = reinterpret_cast<float *>(inputSamples);
		pfOutputSample = reinterpret_cast<float *>(outputSamples);

		for(DWORD i = 0; i!= nBufferLength; i++)
		{
			*pfInputSample = (2.0f * rand() - RAND_MAX) / RAND_MAX; // -1..1
			*pfOutputSample = 0;  // silence
			pfInputSample++;
			pfOutputSample++;
		}

		// Can use the constrained version of the convolution routine as have a buffer that is a multiple of the filter size in length to process
		c->doConvolutionConstrained(inputSamples, outputSamples,
			/* dwBlocksToProcess */ m_Filter->nSamples * SAMPLES,
			/* fAttenuation_db */ 0,
			/* fWetMix,*/ 1.0L,
			/* fDryMix */ 0.0L
#if defined(DEBUG) | defined(_DEBUG)
			, m_CWaveFileTrace
#endif
			);

		// Scan the output buffer for larger output samples
		again = FALSE;
		pfOutputSample = reinterpret_cast<float*>(outputSamples);
		for(DWORD i = 0; i!= nBufferLength; i++)
		{
			if (abs(*pfOutputSample) > abs(maxSample))
			{
				maxSample = *pfOutputSample;
				again = TRUE; // Keep convolving until find no larger output samples
			}
			pfOutputSample++;
		}

	} while (again);

	// maxSample 0..1
	// 10 ^ (fAttenuation_db / 20) = 1
	// Limit fAttenuation to +/-MAX_ATTENUATION dB
	fAttenuation = abs(maxSample) > 1e-8 ? 20.0f * log(1.0f / abs(maxSample)) : 0;

	if (fAttenuation > MAX_ATTENUATION)
	{
		fAttenuation = MAX_ATTENUATION;
	}
	else if (-fAttenuation > MAX_ATTENUATION)
	{
		fAttenuation = -1.0L * MAX_ATTENUATION;
	}

	delete c;
	delete outputSamples;
	delete inputSamples;

	return hr;
}


/////////////////////////////////////////////////////////////////////////////
// CConvolver::DoProcessOutput
//
// Convert the input buffer to the output buffer
/////////////////////////////////////////////////////////////////////////////

HRESULT CConvolver::DoProcessOutput(BYTE *pbOutputData,
									const BYTE *pbInputData,
									DWORD *cbBytesProcessed)
{
	// see if the plug-in has been disabled by the user
	if (!m_bEnabled)
	{
		// if so, just copy the data without changing it. You should
		// also do any necessary format conversion here.
		memcpy(pbOutputData, pbInputData, *cbBytesProcessed);

		return S_OK;
	}

	// Get a pointer to the valid WAVEFORMATEX structure
	// for the current media type.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat; // TODO: reinterpret_cast?

	// Don't know what to do if there is a mismatch in the number of channels
	if ( m_Filter->nChannels != pWave->nChannels)
	{
		return E_UNEXPECTED;
	}

	// Calculate the number of blocks to process.  A block contains the Samples for all channels
	DWORD dwBlocksToProcess = (*cbBytesProcessed / pWave->nBlockAlign);

	// Cannot use the InPlace version, as have not yet fixed the various IMedia routines to ensure 
	// that the input data in a multiple of the filter length in length. So will get an initial filter
	// length's worth of silence.
	*cbBytesProcessed = m_Convolution->doConvolutionArbitrary(pbInputData, pbOutputData,
		dwBlocksToProcess,
		m_fAttenuation_db,
		m_fWetMix,
		m_fDryMix
#if defined(DEBUG) | defined(_DEBUG)
		, m_CWaveFileTrace
#endif	
		);
	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::ValidateMediaType
//
// Validate that the media type is acceptable
/////////////////////////////////////////////////////////////////////////////

HRESULT CConvolver::ValidateMediaType(const DMO_MEDIA_TYPE *pmtTarget, const DMO_MEDIA_TYPE *pmtPartner)
{
	// make sure the target media type has the fields we require
	if( ( MEDIATYPE_Audio != pmtTarget->majortype ) ||
		( FORMAT_WaveFormatEx != pmtTarget->formattype ) ||
		( pmtTarget->cbFormat < sizeof( WAVEFORMATEX )) ||
		( NULL == pmtTarget->pbFormat) )
	{
		return DMO_E_TYPE_NOT_ACCEPTED;
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
		// TODO: 24-bit?
		if ((32 != pWave->wBitsPerSample))
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
						// TODO: What about 24-bit samples?  Do they exist?
						if ((32 != pWave->wBitsPerSample) ||
							(32 != pWaveXT->Samples.wValidBitsPerSample)  ||
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
		break;
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


template<typename FFT_type>
HRESULT CConvolver::LoadFilter()
{
	HRESULT hr = S_OK;

	// Test whether a buffer exists.
	if (m_Filter)
	{
		// A buffer already exists.
		// Delete the filter.
		// TODO: this is not thread safe (and so causes problems attempting to change filter during playback)
		delete m_Filter;
		m_Filter = NULL;
	}

	// Load the wave file
	CWaveFile* pFilterWave = new CWaveFile();
	hr = pFilterWave->Open( m_szFilterFileName, NULL, WAVEFILE_READ );
	if( FAILED(hr) )
	{
		ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
		SAFE_DELETE(pFilterWave);
		return hr;
	}

	// Save filter characteristic, for access by the properties page, etc
	m_WfexFilterFormat.cbSize = pFilterWave->GetFormat()->cbSize;
	m_WfexFilterFormat.nAvgBytesPerSec = pFilterWave->GetFormat()->nAvgBytesPerSec;
	m_WfexFilterFormat.nBlockAlign = pFilterWave->GetFormat()->nBlockAlign;
	m_WfexFilterFormat.nChannels = pFilterWave->GetFormat()->nChannels;
	m_WfexFilterFormat.nSamplesPerSec= pFilterWave->GetFormat()->nSamplesPerSec;
	m_WfexFilterFormat.wBitsPerSample = pFilterWave->GetFormat()->wBitsPerSample;
	m_WfexFilterFormat.wFormatTag= pFilterWave->GetFormat()->wFormatTag;

	DWORD cFilterLength = pFilterWave->GetSize() / m_WfexFilterFormat.nBlockAlign;  // Filter length in samples (Nh)

	// Check that the filter is not too big
	if ( cFilterLength > MAX_FILTER_SIZE )
	{
		ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
		pFilterWave->Close();
		SAFE_DELETE(pFilterWave);
		return E_OUTOFMEMORY;
	}
	// .. or too small
	if ( cFilterLength < 2 )
	{
		ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
		pFilterWave->Close();
		SAFE_DELETE(pFilterWave);
		return E_FAIL;
	}

	DWORD dwSizeToRead = m_WfexFilterFormat.wBitsPerSample / 8;  // in bytes
	DWORD dwSizeRead = 0;

	// Setup the filter

	// Filter length is 2 x Nh. Must be power of 2 for the Ooura FFT package.
	// Eg, if m_cFilterLength = Nh = 6, c2xPaddedFilterLength = 16
	DWORD c2xPaddedFilterLength = 1; // The length of the filter
	for(c2xPaddedFilterLength = 1; c2xPaddedFilterLength < 2 * cFilterLength; c2xPaddedFilterLength *= 2);

	// Initialise the Filter and the various sample buffers that will be used during processing
	m_Filter = new CSampleBuffer<float>(m_WfexFilterFormat.nChannels, c2xPaddedFilterLength);

	FFT_type sample = 0;
#if defined(DEBUG) | defined(_DEBUG)
	FFT_type minSample = 0;
	FFT_type maxSample = 0;
#endif
	BYTE bSample[8] = {0, 0, 0, 0, 0, 0, 0, 0}; // 8 is the biggest sample size (64-bit)
	assert (dwSizeToRead <= 8);

	// Read the filter file
	for (DWORD nSample=0; nSample != cFilterLength; nSample++)
	{
		for (WORD nChannel=0; nChannel !=  m_Filter->nChannels; nChannel++)
		{
			hr = pFilterWave->Read(bSample, dwSizeToRead, &dwSizeRead);
			if (FAILED(hr))
			{
				ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
				SAFE_DELETE(pFilterWave);
				return hr;
			}

			if (dwSizeRead != dwSizeToRead) // file corrupt, non-existent, etc
			{
				ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
				pFilterWave->Close();
				delete pFilterWave;
				return E_FAIL;
			}

			// Now convert bSample to the float sample
			switch (m_WfexFilterFormat.wFormatTag)
			{
			case WAVE_FORMAT_PCM: // the sample is normalized to +/-1
				switch (m_WfexFilterFormat.wBitsPerSample)	// container size
				{
				case 8:
					{
						assert(dwSizeToRead == sizeof(BYTE));
						sample = static_cast<FFT_type>((bSample[0] - 128) / 128.0L);
					}
				case 16:
					{
						assert(dwSizeToRead == sizeof(INT16));
						sample = static_cast<FFT_type>(*reinterpret_cast<INT16*>(bSample) / 32768.0L);
					}
					break;
				case 24:
					{
						assert(dwSizeToRead == 3);  // 3 bytes
						// Get the input sample
						int i = (char) bSample[2];
						i = (i << 8) | bSample[1];
						sample = static_cast<FFT_type>(((i << 8) | bSample[0]) / 8388608.0);
					}
					break;
				case 32:
					{
						assert(dwSizeToRead == sizeof(INT32));
						long  *plSample = (long *) bSample;

						// Get the input sample
						INT32 i = *plSample;
						sample = static_cast<FFT_type>(i / 2147483648.0);
					}
					break;
				case 64:
					{
						assert(dwSizeToRead == sizeof(INT64));
						long long  *pllSample = (long long *) bSample;

						// Get the input sample
						long long i = *pllSample;
						sample = static_cast<FFT_type>(i / 9223372036854775808.0);
					}
					break;
				default:
					return E_NOTIMPL; // Unsupported it sample size
				}
				break;

			case WAVE_FORMAT_IEEE_FLOAT:
				switch (m_WfexFilterFormat.wBitsPerSample)
				{
				case 24:
					return DMO_E_TYPE_NOT_ACCEPTED;
				case 32:
					{
						assert(dwSizeToRead == sizeof(float));
						sample = static_cast<FFT_type>(*reinterpret_cast<float*>(bSample));
					}
					break;
				case 64:
					{
						assert(dwSizeToRead == sizeof(double));
						sample = static_cast<FFT_type>(*reinterpret_cast<double*>(bSample));
					}
					break;
				default:
					return DMO_E_TYPE_NOT_ACCEPTED; // Unsupported it sample size
				}
				break;

			case WAVE_FORMAT_EXTENSIBLE:
				{
					//// Multi-channel filter, or filter with container size > sample size.

					const WAVEFORMATEXTENSIBLE* const wfexFilterFormat = (WAVEFORMATEXTENSIBLE *) &m_WfexFilterFormat;

					if(wfexFilterFormat->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
					{
						switch(wfexFilterFormat->Format.wBitsPerSample)
						{
						case 8:
							{
								assert(dwSizeToRead == sizeof(BYTE));
								sample = static_cast<FFT_type>((bSample[0] - 128) / 128.0L);
							}
						case 16:
							{
								assert(dwSizeToRead == sizeof(INT16));
								sample = static_cast<FFT_type>(*reinterpret_cast<INT16*>(bSample) / 32768.0L);
							}
							break;
						case 24:
							{
								assert(dwSizeToRead == 3);  // 3 bytes
								// Get the input sample
								int i = (char) bSample[2];
								i = (i << 8) | bSample[1];
								switch (wfexFilterFormat->Samples.wValidBitsPerSample)
								{
								case 16:
									sample = static_cast<FFT_type>(i / 32768.0);
									break;
								case 20:
									sample = static_cast<FFT_type>(((i << 4) | (bSample[0] >> 4)) / 524288.0);
									break;
								case 24:
									sample = static_cast<FFT_type>(((i << 8) | bSample[0]) / 8388608.0);
									break;
								default:
									return DMO_E_TYPE_NOT_ACCEPTED;
								}
							}
							break;
						case 32:
							{
								assert(dwSizeToRead == sizeof(INT32));
								long  *plSample = (long *) bSample;

								// Get the input sample
								INT32 i = *plSample;

								switch (wfexFilterFormat->Samples.wValidBitsPerSample)
								{
								case 16:
									i >>= 16;
									sample = static_cast<FFT_type>(i / 32768.0);
									break;
								case 20:
									i >>= 12;
									sample = static_cast<FFT_type>(i / 524288.0);
									break;
								case 24:
									i >>= 8;
									sample = static_cast<FFT_type>(i / 8388608.0);
									break;
								case 32:
									sample = static_cast<FFT_type>(i / 2147483648.0);
									break;
								default:
									return DMO_E_TYPE_NOT_ACCEPTED;
								}
							}
							break;
						case 64:
							{
								assert(dwSizeToRead == sizeof(INT64));
								long long  *pllSample = (long long *) bSample;

								// Get the input sample
								long long i = *pllSample;

								switch (wfexFilterFormat->Samples.wValidBitsPerSample)
								{
								case 16:
									i >>= 48;
									sample = static_cast<FFT_type>(i / 32768.0);
									break;
								case 20:
									i >>= 44;
									sample = static_cast<FFT_type>(i / 524288.0);
									break;
								case 24:
									i >>= 40;
									sample = static_cast<FFT_type>(i / 8388608.0);
									break;
								case 32:
									i >>= 32;
									sample = static_cast<FFT_type>(i / 2147483648.0);
									break;
								case 64:
									sample = static_cast<FFT_type>(i / 9223372036854775808.0);
									break;
								default:
									return DMO_E_TYPE_NOT_ACCEPTED;
								}
							}
							break;
						default:
							return DMO_E_TYPE_NOT_ACCEPTED; // Unsupported it sample size
						}
					}
					else if(wfexFilterFormat->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
					{
						switch (wfexFilterFormat->Format.wBitsPerSample)
						{
						case 24:
							return DMO_E_TYPE_NOT_ACCEPTED;
						case 32:
							{
								assert(dwSizeToRead == sizeof(float));
								sample = static_cast<FFT_type>(*reinterpret_cast<float*>(bSample));
							}
							break;
						case 64:
							{
								assert(dwSizeToRead == sizeof(double));
								sample = static_cast<FFT_type>(*reinterpret_cast<double*>(bSample));
							}
							break;
						default:
							return DMO_E_TYPE_NOT_ACCEPTED; // Unsupported it sample size
						}
						break;
					}
					else
						return DMO_E_TYPE_NOT_ACCEPTED;
				}
				break;

			default:
				return DMO_E_TYPE_NOT_ACCEPTED;	// Filter file format is not supported
			}

			m_Filter->samples[nChannel][nSample] = sample;
#if defined(DEBUG) | defined(_DEBUG)
			if (sample > maxSample)
				maxSample = sample;
			if (sample < minSample)
				minSample = sample;
#endif

		} // for nChannel
	} // for nSample

	// Now create a FFT of the filter
	for (unsigned short nChannel=0; nChannel != m_WfexFilterFormat.nChannels; nChannel++)
	{
		rdft(c2xPaddedFilterLength, OouraRForward, m_Filter->samples[nChannel]);		
	}

	// Done with the filter file
	pFilterWave->Close(); 
	delete pFilterWave;

#if defined(DEBUG) | defined(_DEBUG)
	cdebug << waveFormatDescription(&m_WfexFilterFormat, cFilterLength, "FFT Filter:") << std::endl;
	cdebug << "minSample " << minSample << ", maxSample " << maxSample << std::endl;
#endif

	return hr;
}



// Pick the correct version of the processing class
//
// TODO: build this into the Factory class.  (Probably more trouble than it is worth)
//
// Note that this allows multi-channel and high-resolution WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT.
// Strictly speaking these should be WAVE_FORMAT_EXTENSIBLE, if we want to avoid ambiguity.
// The code below assumes that for WAVE_FORMAT_PCM and WAVE_FORMAT_IEEE_FLOAT, wBitsPerSample is the container size.
// The code below will not work with streams where wBitsPerSample is used to indicate the bits of actual valid data,
// while the container size is to be inferred from nBlockAlign and nChannels (eg, wBitsPerSample = 20, nBlockAlign = 8, nChannels = 2).
// See http://www.microsoft.com/whdc/device/audio/multichaud.mspx
HRESULT CConvolver::SelectConvolution(const WAVEFORMATEX *pWave)
{
	HRESULT hr = S_OK;

	WORD wFormatTag = pWave->wFormatTag;
	WAVEFORMATEXTENSIBLE* pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;
	if (wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)
			wFormatTag = WAVE_FORMAT_PCM;
		else
			if (pWaveXT->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)
				wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
			else
				return E_INVALIDARG;
	}

	switch (wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		switch (pWave->wBitsPerSample)
		{
			// Note: for 8 and 16-bit samples, we assume the sample is the same size as
			// the samples. For > 16-bit samples, we need to use the WAVEFORMATEXTENSIBLE
			// structure to determine the valid bits per sample. (See above)
		case 8:
			m_Convolution = new Cconvolution_pcm8<float>(m_Filter);
			break;

		case 16:
			m_Convolution = new Cconvolution_pcm16<float>(m_Filter);
			break;

		case 24:
			{
				switch (pWaveXT->Samples.wValidBitsPerSample)
				{
				case 16:
					m_Convolution = new Cconvolution_pcm24<float,16>(m_Filter);
					break;

				case 20:
					m_Convolution = new Cconvolution_pcm24<float,20>(m_Filter);
					break;

				case 24:
					m_Convolution = new Cconvolution_pcm24<float,24>(m_Filter);
					break;

				default:
					return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
				}
			}
			break;

		case 32:
			{
				switch (pWaveXT->Samples.wValidBitsPerSample)
				{
				case 16:
					m_Convolution = new Cconvolution_pcm32<float,16>(m_Filter);
					break;

				case 20:
					m_Convolution = new Cconvolution_pcm32<float,20>(m_Filter);
					break;

				case 24:
					m_Convolution = new Cconvolution_pcm32<float,24>(m_Filter);
					break;

				case 32:
					m_Convolution = new Cconvolution_pcm32<float,32>(m_Filter);
					break;

				default:
					return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
				}
			}
			break;

		default:  // Unprocessable PCM
			return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
		}
		break;

	case WAVE_FORMAT_IEEE_FLOAT:
		switch (pWave->wBitsPerSample)
		{
		case 32:
			m_Convolution = new Cconvolution_ieeefloat<float>(m_Filter);
			break;

		default:  // Unprocessable IEEE float
			return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
			//break;
		}
		break;

	default: // Not PCM or IEEE Float
		return E_INVALIDARG; // Should have been filtered out by ValidateMediaType
	}

	return hr;
}


//
//// IPersistPropertyBag methods 
//// WARNING !!! 
//// WARNING !!! Not exposed in NonDelegatingQueryInterface
//// WARNING !!! 
//STDMETHODIMP InitNew() 
//{
//	return S_OK;
//}
//
//STDMETHODIMP Load(IPropertyBag *pPropBag, IErrorLog *pErrorLog)
//{
//	return E_NOTIMPL;
//}
//
//STDMETHODIMP Save(IPropertyBag *pPropBag, BOOL fClearDirty, BOOL fSaveAllProperties)
//{
//	if (m_MediaPropertyList.GetCount() == 0)
//		return E_NOTIMPL;
//
//	POSITION pos = m_MediaPropertyList.GetHeadPosition();
//	while(pos)
//	{
//		CMediaProperty *pInfo = m_MediaPropertyList.GetNext(pos);
//
//		pPropBag->Write(pInfo->tag, &pInfo->var);
//	}
//	return S_OK;
//}
//
//// IPropertyBag methods
//STDMETHODIMP Read(LPCOLESTR pszPropName,VARIANT *pVar, IErrorLog *pErrorLog)
//{
//	if (lstrcmpW(pszPropName, L"IconStreams") == 0)
//	{
//		HMODULE hModule = GetModuleHandle("MMSwitch.ax");			
//		//#define ANIMATE
//#ifdef ANIMATE
//		SAFEARRAYBOUND sab;
//		sab.cElements = 4;
//		sab.lLbound = 0;
//		SAFEARRAY *psa = SafeArrayCreate(VT_UNKNOWN, 1, &sab);
//
//		if (psa)
//		{
//			long rgIndices;
//			for (rgIndices = 0; rgIndices < 4; rgIndices++)
//				SafeArrayPutElement(psa, &rgIndices, StreamFromResource(IDB_ICONSTREAM0 + rgIndices));
//		}
//
//		pVar->punkVal = (IUnknown *)psa;
//		pVar->vt = VT_ARRAY;
//
//#else
//		pVar->punkVal = StreamFromResource(IDI_ICONCONVOLVER);
//		pVar->vt = VT_UNKNOWN;
//#endif
//		if (pVar->punkVal != NULL)
//			return S_OK;
//		else
//			return E_FAIL;
//	}
//	return E_NOTIMPL;
//}
//
//STDMETHODIMP Write(LPCOLESTR pszPropName, VARIANT *pVar) 
//{return E_NOTIMPL;}
