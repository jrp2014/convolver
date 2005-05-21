/////////////////////////////////////////////////////////////////////////////
//
// convolver.cpp : Implementation of CConvolver
//
// Copyright (c) Microsoft Corporation. All rights reserved.
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
    m_dwDelayTime = 1000;   // Default to a delay time of 1000 ms.

	m_fWetMix = 0.50;  // default to 50 percent wet
	m_fDryMix = 0.50;  // default to 50 percent dry

    m_cbInputLength = 0;
    m_pbInputData = NULL;
    m_bValidTime = false;
    m_rtTimestamp = 0;
    m_bEnabled = TRUE;

	m_pbDelayBuffer = NULL;
	m_pbDelayPointer = NULL;
	m_cbDelayBuffer = 0;

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

	lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
	if (ERROR_SUCCESS == lResult)
	{
		// Read the delay time from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsDelayTime, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			m_dwDelayTime = dwValue;
		}

		// Read the wet mix value from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsWetmix, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a double.
			m_fWetMix = (double)dwValue / 100;
			// Calculate the dry mix value.
			m_fDryMix = 1.0 - m_fWetMix;
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
    FreeStreamingResources();  // In case client does not call this.        
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetStreamCount
//
// Implementation of IMediaObject::GetStreamCount
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetStreamCount( 
               DWORD *pcInputStreams,
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

    return S_OK;
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
    
STDMETHODIMP CConvolver::GetInputType ( 
               DWORD dwInputStreamIndex,
               DWORD dwTypeIndex,
               DMO_MEDIA_TYPE *pmt)
{
    HRESULT hr = S_OK;

    if ( 0 != dwInputStreamIndex )
    {
        return DMO_E_INVALIDSTREAMINDEX ;
    }

    // only support one preferred type
    if ( 0 != dwTypeIndex )
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
    else // otherwise use default for this plug-in
    {
        ::ZeroMemory( pmt, sizeof( DMO_MEDIA_TYPE ) );
        pmt->majortype = MEDIATYPE_Audio;
        pmt->subtype = MEDIASUBTYPE_PCM;
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetOutputType
//
// Implementation of IMediaObject::GetOutputType
/////////////////////////////////////////////////////////////////////////////
    
STDMETHODIMP CConvolver::GetOutputType( 
               DWORD dwOutputStreamIndex,
               DWORD dwTypeIndex,
               DMO_MEDIA_TYPE *pmt)
{
    HRESULT hr = S_OK;

    if ( 0 != dwOutputStreamIndex )
    {
        return DMO_E_INVALIDSTREAMINDEX;
    }

    // only support one preferred type
    if ( 0 != dwTypeIndex )
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
    else // other use default for this plug-in
    {
        ::ZeroMemory( pmt, sizeof( DMO_MEDIA_TYPE ) );
        pmt->majortype = MEDIATYPE_Audio;
        pmt->subtype = MEDIASUBTYPE_PCM;
    }

    return hr;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::SetInputType
//
// Implementation of IMediaObject::SetInputType
/////////////////////////////////////////////////////////////////////////////
    
STDMETHODIMP CConvolver::SetInputType( 
               DWORD dwInputStreamIndex,
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
    
STDMETHODIMP CConvolver::SetOutputType( 
               DWORD dwOutputStreamIndex,
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
    
STDMETHODIMP CConvolver::Discontinuity( 
               DWORD dwInputStreamIndex)
{
    return S_OK;
}


void CConvolver::FillBufferWithSilence(WAVEFORMATEX *pWfex)
{
	if (8 == pWfex->wBitsPerSample)
	{
		::FillMemory(m_pbDelayBuffer, m_cbDelayBuffer, 0x80);
	}
	else
		::ZeroMemory(m_pbDelayBuffer, m_cbDelayBuffer);
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::AllocateStreamingResources
//
// Implementation of IMediaObject::AllocateStreamingResources
/////////////////////////////////////////////////////////////////////////////
    
STDMETHODIMP CConvolver::AllocateStreamingResources ( void )
{
	// Allocate any buffers need to process the stream.

	// Get a pointer to the WAVEFORMATEX structure.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;
	if (NULL == pWave)
	{
		return E_FAIL;
	}

	// Get the size of the buffer required.
	m_cbDelayBuffer = (m_dwDelayTime * pWave->nSamplesPerSec * pWave->nBlockAlign) / 1000;

	// Test whether a buffer exists.
	if (m_pbDelayBuffer)
	{
		// A buffer already exists.
		// Delete the delay buffer.
		delete m_pbDelayBuffer;
		m_pbDelayBuffer = NULL;
	}

	// Allocate the buffer.
	m_pbDelayBuffer = new BYTE[m_cbDelayBuffer];

	if (!m_pbDelayBuffer)
		return E_OUTOFMEMORY;

	// Move the echo pointer to the head of the delay buffer.
	m_pbDelayPointer = m_pbDelayBuffer;


	// Fill the buffer with values representing silence.
	FillBufferWithSilence(pWave);

	return S_OK;
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

	// Test whether a buffer exists.
	if (m_pbDelayBuffer)
	{
		delete m_pbDelayBuffer;
		m_pbDelayBuffer = NULL;
		m_pbDelayPointer = NULL;
		m_cbDelayBuffer = 0;
	}

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::GetInputStatus
//
// Implementation of IMediaObject::GetInputStatus
/////////////////////////////////////////////////////////////////////////////
    
STDMETHODIMP CConvolver::GetInputStatus( 
           DWORD dwInputStreamIndex,
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
    
STDMETHODIMP CConvolver::ProcessInput( 
               DWORD dwInputStreamIndex,
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

STDMETHODIMP CConvolver::ProcessOutput( 
               DWORD dwFlags,
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
    // Only one property page is required for the plug-in.
    pPages->cElems = 1;
    pPages->pElems = (GUID *) (CoTaskMemAlloc(sizeof(GUID)));

    // Make sure memory is allocated for pPages->pElems
    if (NULL == pPages->pElems)
    {
        return E_OUTOFMEMORY;
    }

    // Return the property page's class ID
    *(pPages->pElems) = CLSID_ConvolverPropPage;

    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::get_delay
//
// Property get to retrieve the delay value via the public interface.
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::get_delay(DWORD *pVal)
{
    if ( NULL == pVal )
    {
        return E_POINTER;
    }

    *pVal = m_dwDelayTime;

    return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::put_delay
//
// Property put to store the delay value via the public interface.
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::put_delay(DWORD newVal)
{
	m_dwDelayTime = newVal;

	// Reallocate the delay buffer.
	AllocateStreamingResources();

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

/////////////////////////////////////////////////////////////////////////////
// CConvolver::DoProcessOutput
//
// Convert the input buffer to the output buffer
/////////////////////////////////////////////////////////////////////////////

HRESULT CConvolver::DoProcessOutput(
                            BYTE *pbOutputData,
                            const BYTE *pbInputData,
                            DWORD *cbBytesProcessed)
{
    // see if the plug-in has been disabled by the user
    if (!m_bEnabled)
    {
        // if so, just copy the data without changing it. You should
        // also do any neccesary format conversion here.
        memcpy(pbOutputData, pbInputData, *cbBytesProcessed);

        return S_OK;
    }

	// Get a pointer to the valid WAVEFORMATEX structure
	// for the current media type.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;

	// Calculate the number of samples to process.
	DWORD dwSamplesToProcess = (*cbBytesProcessed / pWave->nBlockAlign) * pWave->nChannels;

    // Note: for 8 and 16-bit samples, we assume the container is the same size as
    // the samples. For > 16-bit samples, we need to use the WAVEFORMATEXTENSIBLE
    // structure to determine the valid bits per sample.

    switch (pWave->wBitsPerSample)
    {
    case 8:
        {
            // return no. bytes actually copied to output buffer
            *cbBytesProcessed = dwSamplesToProcess * sizeof(BYTE);

			// Store the address of the end of the delay buffer.
			BYTE * pbEOFDelayBuffer = (m_pbDelayBuffer + m_cbDelayBuffer - sizeof(BYTE));

            // 8-bit sound is 0..255 with 128 == silence
            while (dwSamplesToProcess--)
            {
                // Get the input sample and normalize to -128 .. 127
                int i = (*pbInputData++) - 128;

				// Get the delay sample and normalize to -128 .. 127
				int delay = m_pbDelayPointer[0] - 128;

				// Write the input sample into the delay buffer.
				m_pbDelayPointer[0] = i + 128;

				// Increment the delay pointer.
				// If it has passed the end of the buffer,
				// then move it to the head of the buffer.
				if (++m_pbDelayPointer > pbEOFDelayBuffer)
					m_pbDelayPointer = m_pbDelayBuffer;

				// Mix the delay with the dry signal.
				i = (int)((i * m_fDryMix ) + (delay * m_fWetMix));

                //// Apply scale factor to sample
                //i = int( ((double) i) * m_dwDelayTime );
            
                // Truncate if exceeded full scale.
                if (i > 127)
                    i = 127;
                if (i < -128)
                    i = -128;

                // Convert back to 0..255 and write to output buffer.
                *pbOutputData++ = (BYTE)(i + 128);
            }
        }
        break;

    case 16:
        {
            // return no. bytes actually copied to output buffer
            *cbBytesProcessed = dwSamplesToProcess * sizeof(short);

            // 16-bit sound is -32768..32767 with 0 == silence
            short   *pwInputData = (short *) pbInputData;
            short   *pwOutputData = (short *) pbOutputData;
 
			// Store local pointers to the delay buffer.
			short    *pwDelayPointer = (short *)m_pbDelayPointer;
			short    *pwDelayBuffer = (short *) m_pbDelayBuffer;
			// Store the address of the last word of the delay buffer.
			short    *pwEOFDelayBuffer = (short *)(m_pbDelayBuffer + m_cbDelayBuffer - sizeof(short));

            while (dwSamplesToProcess--)
            {
                // Get the input sample
                int i = *pwInputData++;

				// Get the delay sample.
				int delay = *pwDelayPointer;

				// Write the input sample to the delay buffer.
				*pwDelayPointer = i;

				// Increment the local delay pointer.
				// If it is past the end of the buffer,
				// then move it to the head of the buffer.
				if (++pwDelayPointer > pwEOFDelayBuffer)
					pwDelayPointer = pwDelayBuffer;

				// Move the global delay pointer.
				m_pbDelayPointer = (BYTE *) pwDelayPointer;

				// Mix the delay with the dry signal.
				i = (int)((i * m_fDryMix ) + (delay * m_fWetMix)); 


                //// Apply scale factor to sample
                //i = int( ((double) i) * m_dwDelayTime );
            
                // Truncate if exceeded full scale.
                if (i > 32767)
                    i = 32767;
                if (i < -32768)
                    i = -32768;

                // Write to output buffer.
                *pwOutputData++ = i;
            }
        }
        break;

    //case 24:
    //    {
    //        // return no. bytes actually copied to output buffer
    //        *cbBytesProcessed = dwSamplesToProcess * 3;

    //        WAVEFORMATEXTENSIBLE *pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;

    //        while (dwSamplesToProcess--)
    //        {
    //            // Get the input sample
    //            int i = (char) pbInputData[2];
    //            i = (i << 8) | pbInputData[1];

    //            int iClip = 0;
    //            switch (pWaveXT->Samples.wValidBitsPerSample)
    //            {
    //            case 16:
    //                iClip = (1 << 15);
    //                break;
    //            case 20:
    //                i = (i << 4) | (pbInputData[0] >> 4);
    //                iClip = (1 << 19);
    //                break;
    //            case 24:
    //                i = (i << 8) | pbInputData[0];
    //                iClip = (1 << 23);
    //                break;
    //            }

    //            pbInputData += 3;

    //            // Apply scale factor to sample
    //            i = int( ((double) i) * m_dwDelayTime );
    //        
    //            // Truncate if exceeded full scale.
    //            if (i > (iClip - 1))
    //                i = iClip - 1;
    //            if (i < -iClip)
    //                i = -iClip;

    //            // Write to output buffer.
    //            *pbOutputData++ = i & 0xFF;
    //            *pbOutputData++ = (i >> 8) & 0xFF;
    //            *pbOutputData++ = (i >> 16) & 0xFF;
    //        }
    //    }
    //    break;

    //case 32:
    //    {
    //        // return no. bytes actually copied to output buffer
    //        *cbBytesProcessed = dwSamplesToProcess * sizeof(long);

    //        long   *plInputData = (long *) pbInputData;
    //        long   *plOutputData = (long *) pbOutputData;

    //        WAVEFORMATEXTENSIBLE *pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;

    //        while (dwSamplesToProcess--)
    //        {
    //            // Get the input sample
    //            int i = *plInputData++;

    //            int iClip = 0;
    //            switch (pWaveXT->Samples.wValidBitsPerSample)
    //            {
    //            case 16:
    //                i >>= 16;
    //                iClip = (1 << 15);
    //                break;
    //            case 20:
    //                i >>= 12;
    //                iClip = (1 << 19);
    //                break;
    //            case 24:
    //                i >>= 8;
    //                iClip = (1 << 23);
    //                break;
    //            case 32:
    //                iClip = (1 << 31);
    //                break;
    //            }

    //            // Apply scale factor to sample
    //            double f = ((double) i) * m_dwDelayTime;
    //        
    //            // Truncate if exceeded full scale.
    //            if (f > (iClip - 1))
    //                f = iClip - 1;
    //            if (f < -iClip)
    //                f = -iClip;

    //            // Write to output buffer.
    //            *plOutputData++ = (int) f;
    //        }
    //    }
    //    break;

    default:
        // return no. bytes actually copied to output buffer
        *cbBytesProcessed = 0;
        return E_FAIL;
        break;
    }

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
        (16 != pWave->wBitsPerSample) /* &&
        (24 != pWave->wBitsPerSample) &&
        (32 != pWave->wBitsPerSample) */)
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
        break;


	case WAVE_FORMAT_EXTENSIBLE:
		{
			// Sample size is greater than 16-bit or is multichannel.
			WAVEFORMATEXTENSIBLE *pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;

			if (KSDATAFORMAT_SUBTYPE_PCM != pWaveXT->SubFormat)
			{
				return DMO_E_TYPE_NOT_ACCEPTED;
			}
		}
		break;

    //case WAVE_FORMAT_EXTENSIBLE:
    //    {
    //        WAVEFORMATEXTENSIBLE *pWaveXT = (WAVEFORMATEXTENSIBLE *) pWave;

    //        // make sure the wave format extensible has the fields we require
    //        if ((KSDATAFORMAT_SUBTYPE_PCM != pWaveXT->SubFormat) ||
    //            (0 == pWaveXT->Samples.wSamplesPerBlock) ||
    //            (pWaveXT->Samples.wValidBitsPerSample > pWave->wBitsPerSample))
    //        {
    //            return DMO_E_TYPE_NOT_ACCEPTED;
    //        }

    //        // for 8 or 16-bit, the container and sample size must match
    //        if ((8  == pWave->wBitsPerSample) ||
    //            (16 == pWave->wBitsPerSample))
    //        {
    //            if (pWave->wBitsPerSample != pWaveXT->Samples.wValidBitsPerSample)
    //            {
    //                return DMO_E_TYPE_NOT_ACCEPTED;
    //            }
    //        }
    //        else 
    //        {
    //            // for any other container size, make sure the valid
    //            // bits per sample is a value we support
    //            if ((16 != pWaveXT->Samples.wValidBitsPerSample) &&
    //                (20 != pWaveXT->Samples.wValidBitsPerSample) &&
    //                (24 != pWaveXT->Samples.wValidBitsPerSample) &&
    //                (32 != pWaveXT->Samples.wValidBitsPerSample))
    //            {
    //                return DMO_E_TYPE_NOT_ACCEPTED;
    //            }
    //        }
    //    }
    //    break;

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
