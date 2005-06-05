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

// FFT routines
#include <fftsg_h.h>

/* Halfcomplex array convolution */
void hcconvolve(DLReal * A,const DLReal * B, DLReal * C,const int N)
{
	int r;
	int i;
	DLReal T1;
	DLReal T2;

	C[0] *= B[0];
	C[1] *= B[1];
	for (r = 2,i = 3;r < N;r += 2,i += 2)
	{
		T1 = A[r] * B[r];
		T2 = A[i] * B[i];
		C[i] = ((A[r] + A[i]) * (B[r] + B[i])) - (T1 + T2);
		C[r] = T1 - T2;
	} 
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::CConvolver
//
// Constructor
/////////////////////////////////////////////////////////////////////////////

CConvolver::CConvolver()
{
	m_fWetMix = 0.50;  // default to 50 percent wet
	m_fDryMix = 0.50;  // default to 50 percent dry
	m_szFilterFileName[0] = 0;
	::ZeroMemory(&m_WfexFilterFormat, sizeof(m_WfexFilterFormat));

	m_cbInputLength = 0;
	m_pbInputData = NULL;
	m_bValidTime = false;
	m_rtTimestamp = 0;
	m_bEnabled = TRUE;

	m_ppfloatFilter = NULL;
	m_cFilterLength = 0;
	m_nSampleBufferIndex = 0;

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
		// Read the wet mix value from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsWetmix, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a double.
			m_fWetMix = (double)dwValue / 100;
			// Calculate the dry mix value.
			m_fDryMix = 1.0 - m_fWetMix;
		}

		// Read the filter file name from the registry. 
		TCHAR szValue[MAX_PATH]	= TEXT("");
		ULONG ulMaxPath				= MAX_PATH;

		// Read the filter filename value.
		lResult = key.QueryStringValue(kszPrefsFilterFileName, szValue, &ulMaxPath );
		if (ERROR_SUCCESS == lResult)
		{
			_tcsncpy(m_szFilterFileName, szValue, ulMaxPath);
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

	// Test whether a buffer exists.
	// TODO: Do this for other Filter types too
	if (m_ppfloatFilter)
	{
		// A buffer already exists.
		// Delete the delay buffer.
		delete m_ppfloatFilter;
		m_ppfloatFilter = NULL;
	}

	CWaveFile* pFilterWave = new CWaveFile();

	// Load the wave file
	if( FAILED( hr = pFilterWave->Open( m_szFilterFileName, NULL, WAVEFILE_READ ) ) )
	{
		ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
		SAFE_DELETE(pFilterWave);
		return hr;
	}

	// Check that the filter is not too big
	DWORD dwTotalSize = pFilterWave->GetSize();
	if ( dwTotalSize > MAX_FILTER_SIZE )
	{
		ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
		pFilterWave->Close();
		delete pFilterWave;
		return E_OUTOFMEMORY;
	}

	m_WfexFilterFormat.cbSize = pFilterWave->GetFormat()->cbSize;
	m_WfexFilterFormat.nAvgBytesPerSec = pFilterWave->GetFormat()->nAvgBytesPerSec;
	m_WfexFilterFormat.nBlockAlign = pFilterWave->GetFormat()->nBlockAlign;
	m_WfexFilterFormat.nChannels = pFilterWave->GetFormat()->nChannels;
	m_WfexFilterFormat.nSamplesPerSec= pFilterWave->GetFormat()->nSamplesPerSec;
	m_WfexFilterFormat.wBitsPerSample = pFilterWave->GetFormat()->wBitsPerSample;
	m_WfexFilterFormat.wFormatTag= pFilterWave->GetFormat()->wFormatTag;

	int nChannels = m_WfexFilterFormat.nChannels;
	m_cFilterLength = dwTotalSize / m_WfexFilterFormat.nBlockAlign;  // Filter length in samples (Nh)
	if (m_cFilterLength > 1000000)
	{
		return E_OUTOFMEMORY; // filter too big
	}
	DWORD dwSizeToRead = m_WfexFilterFormat.wBitsPerSample / 8;  // in bytes
	DWORD dwSizeRead = 0;

	switch (m_WfexFilterFormat.wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		return E_NOTIMPL; // TODO: Unimplemented filter type
		//break;

	case WAVE_FORMAT_IEEE_FLOAT:
		switch (m_WfexFilterFormat.wBitsPerSample)
		{
		case 24:
			return E_NOTIMPL; // TODO: Unimplemented filter type
			//break; 
		case 32:
			{
				assert(dwSizeToRead == sizeof(float));

				// Filter length is 2 x Nh. Must be power of 2 for this FFT package.
				// Eg, if m_cFilterLength = Nh = 6, m_c2xPaddedFilterLength = 16 
				for(m_c2xPaddedFilterLength = 1; m_c2xPaddedFilterLength < 2 * m_cFilterLength; m_c2xPaddedFilterLength *= 2);


				// Initialise the Filter and the Sample buffer with which it will be multiplied
				m_ppfloatFilter = (float**) new float[nChannels];
				m_ppfloatSampleBuffer = (float**) new float[nChannels];
				m_pfloatSampleBufferChannelCopy = new float[m_c2xPaddedFilterLength]; 
				m_ppfloatOutputBuffer = (float**) new float[nChannels];
				for(int i = 0; i < nChannels; i++)
				{
					m_ppfloatFilter[i] = new float[m_c2xPaddedFilterLength];
					m_ppfloatSampleBuffer[i] = new float[m_c2xPaddedFilterLength];
					m_ppfloatOutputBuffer[i] = new float[m_c2xPaddedFilterLength];
				}

				for (int j=0; j < m_cFilterLength; j++)
					for (int i=0; i < nChannels; i++)
					{
						if (hr = FAILED(pFilterWave->Read((BYTE*)&m_ppfloatFilter[i][j], dwSizeToRead, &dwSizeRead)))
						{
							ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
							SAFE_DELETE(pFilterWave);
							return hr;
						}
						if (dwSizeRead != dwSizeToRead)
						{
							ZeroMemory( &m_WfexFilterFormat, sizeof(m_WfexFilterFormat) );
							pFilterWave->Close();
							delete pFilterWave;
							return E_FAIL;
						}
						m_ppfloatSampleBuffer[i][j] = 0.0;
						m_ppfloatOutputBuffer[i][j] = 0.0;
					} // for i

					for (int j=m_cFilterLength; j < m_c2xPaddedFilterLength; j++)
						for (int i=0; i < nChannels; i++)
						{
							m_ppfloatFilter[i][j] = 0.0;
							m_ppfloatSampleBuffer[i][j] = 0.0;
							m_ppfloatOutputBuffer[i][j] = 0.0;
						}

					// Now create a FFT of the filter
					for (int i=0; i < nChannels; i++)
					{
						rdft(m_c2xPaddedFilterLength, OouraRForward, m_ppfloatFilter[i]);		
					}

			}  // case
			break;
		default:
			return E_NOTIMPL; // Unsupported it sample size
		}
		break;

	default:
		return E_NOTIMPL;	// Filter file format is not supported
	}

	// Done with the filter file
	pFilterWave->Close(); 
	delete pFilterWave;

	//// Fill the buffer with values representing silence.
	//FillBufferWithSilence(pWave);

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
	if (m_ppfloatFilter)
	{
		delete m_ppfloatFilter;
		m_ppfloatFilter = NULL;
	}
	m_cFilterLength = 0;
	m_c2xPaddedFilterLength = 0;

	if (m_ppfloatOutputBuffer)
	{
		delete m_ppfloatOutputBuffer;
		m_ppfloatOutputBuffer = NULL;

	}

	if (m_ppfloatSampleBuffer)
	{
		delete m_ppfloatSampleBuffer;
		m_ppfloatSampleBuffer = NULL;
	}
	m_nSampleBufferIndex = 0;

	if (m_pfloatSampleBufferChannelCopy)
	{
		delete m_pfloatSampleBufferChannelCopy;
		m_pfloatSampleBufferChannelCopy = NULL;
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
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;

	// Don't know what to do if there is a mismatch in the number of channels
	if ( m_WfexFilterFormat.nChannels != pWave->nChannels)
	{
		return E_UNEXPECTED;
	}

	// Calculate the number of blocks to process.  A block contains the Samples for all channels
	DWORD dwBlocksToProcess = (*cbBytesProcessed / pWave->nBlockAlign);

	// Note: for 8 and 16-bit samples, we assume the container is the same size as
	// the samples. For > 16-bit samples, we need to use the WAVEFORMATEXTENSIBLE
	// structure to determine the valid bits per sample.

	switch (pWave->wBitsPerSample)
	{
	//case 8:
	//	{
	//		BYTE* pbEOFDelayBuffer = (m_pbDelayBuffer + m_cbDelayBuffer - sizeof(BYTE));

	//		// 8-bit sound is 0..255 with 128 == silence
	//		// Assume that channels are interleaved
	//		while (dwBlocksToProcess--)
	//		{
	//			for (int nc = 0; nc < pWave->nChannels; nc++)
	//			{
	//				// Get the input sample and normalize to -128 .. 127
	//				int inputSample = (*pbInputData++) - 128;

	//				// Get the delay sample and normalize to -128 .. 127
	//				//int delaySample = m_pbDelayPointer[0] - 128;

	//				//// Write the input sample to the delay buffer
	//				//m_pbDelayPointer[0] = inputSample;

	//				//// Increment the delay pointer
	//				//if (++m_pbDelayPointer > pbEOFDelayBuffer)
	//				//	m_pbDelayPointer = m_pbDelayBuffer;

	//				float convolution = 0;
	//				float const *p_m_ppfloatFilter;
	//				BYTE *p_m_pbDelayBuffer;

	//				// setup the filter (channel)
	//				p_m_ppfloatFilter = m_ppfloatFilter[nc];

	//				/* calculate the end part */
	//				p_m_pbDelayBuffer = m_pbDelayBuffer - m_iDelayOffset;
	//				*p_m_pbDelayBuffer = inputSample + 128; // Write the input sample to the delay buffer
	//				int end_ntaps = m_cFilterLength - m_iDelayOffset;
	//				for (int ii = 0; ii < end_ntaps; ii++)
	//					convolution += *p_m_ppfloatFilter++ * (*p_m_pbDelayBuffer++ - 128);

	//				/* calculate the beginning part */
	//				p_m_pbDelayBuffer = m_pbDelayBuffer;
	//				for (int ii = 0; ii < m_iDelayOffset; ii++)
	//					convolution += *p_m_ppfloatFilter++ * *p_m_pbDelayBuffer++;

	//				/* decrement the offset into the delay buffer, wrapping if below zero */
	//				if (--m_iDelayOffset < 0)
	//					m_iDelayOffset += m_cFilterLength;

	//				// Mix the delay with the dry signal.
	//				int outputSample = (int)((inputSample * m_fDryMix ) + (convolution * m_fWetMix));

	//				// Truncate if exceeded full scale.
	//				if (outputSample > 127)
	//					outputSample = 127;
	//				if (outputSample < -128)
	//					outputSample = -128;

	//				// Convert back to 0..255 and write to output buffer.
	//				*pbOutputData++ = (BYTE)(outputSample + 128);
	//			}
	//		}
	//		*cbBytesProcessed = dwBlocksToProcess * sizeof(BYTE) *  pWave->nChannels;             // return no. bytes processed
	//	}
	//	break;

	case 16:
		{
			// return no. bytes to be copied to output buffer
			*cbBytesProcessed = dwBlocksToProcess * sizeof(short) * pWave->nChannels;

			// Cross-check
			DWORD cbBytesActuallyProcessed = 0;

			// 16-bit sound is -32768..32767 with 0 == silence
			short   *pwInputData = (short *) pbInputData;
			short   *pwOutputData = (short *) pbOutputData;

			while (dwBlocksToProcess--)
			{
				for (int nc = 0; nc < pWave->nChannels; nc++)
				{
					// Get the input sample and convert it to a float
					float inputSample = *pwInputData++;

					m_ppfloatSampleBuffer[nc][m_nSampleBufferIndex] = inputSample; // Channels are interleaved

					// Mix the processed signal with the dry signal.
					// This does not actually work because of the convolution delay (ie, m_ppfloatSampleBuffer[][i] doesn't correspond to m_ppfloatOutputBuffer[][i])
					// So set the effect to 0 to get the proper convolved result
					int outputSample = (int)((m_ppfloatSampleBuffer[nc][m_nSampleBufferIndex + m_c2xPaddedFilterLength / 2] * m_fDryMix ) +
						(m_ppfloatOutputBuffer[nc][m_nSampleBufferIndex + m_c2xPaddedFilterLength / 2] * m_fWetMix)); // skip circular artefacts

					// Truncate if exceeded full scale.
					if (outputSample > 32767)
						outputSample = 32767;
					if (outputSample < -32768)
						outputSample = -32768;

					// Write to output buffer.
					*pwOutputData++ = outputSample;
					cbBytesActuallyProcessed += sizeof(short);
				}

				if (m_nSampleBufferIndex == m_c2xPaddedFilterLength / 2 - 1) //  m_c2xPaddedFilterLength Nh is a power of 2
				{	
					// Got a sample xi length Nh, so calculate m_ppfloatOutputBuffer
					for (int nc = 0; nc < pWave->nChannels; nc++)
					{
						// Copy the sample buffer, as the rdft routine overwrites it
						for (int j = 0; j < m_c2xPaddedFilterLength; j++)
							m_pfloatSampleBufferChannelCopy[j] = m_ppfloatSampleBuffer[nc][j];

						//  rdft(n, 1, a):
						//        n              :data length (int)
						//                        n >= 2, n = power of 2
						//        a[0...n-1]     :input data
						//                        output data
						//                                a[2*k] = R[k], 0<=k<n/2
						//                                a[2*k+1] = I[k], 0<k<n/2
						//                                a[1] = R[n/2]
						rdft(m_c2xPaddedFilterLength, OouraRForward, m_pfloatSampleBufferChannelCopy);  // get DFT of m_ppfloatSampleBuffer
						// Multiply point by point the complex Yi(m) =Xi(m)Hz(m). m_ppfloatFilter (Hz(m)) is already calculated (in AllocateStreamingResources)
						hcconvolve(m_pfloatSampleBufferChannelCopy, m_ppfloatFilter[nc], m_ppfloatOutputBuffer[nc], m_c2xPaddedFilterLength);
						//get back the yi
						rdft(m_c2xPaddedFilterLength, OouraRBackward, m_ppfloatOutputBuffer[nc]); // take the Inverse DFT
						// scale
						for (int j = m_c2xPaddedFilterLength / 2; j < m_c2xPaddedFilterLength; j++)  // No need to scale first half, as going to discard that
							m_ppfloatOutputBuffer[nc][j] *= 2.0 / (double) m_c2xPaddedFilterLength;

						// move overlap block x i to previous block x i-1
						for (int j = 0; j < m_c2xPaddedFilterLength / 2; j++)
							m_ppfloatSampleBuffer[nc][j + m_c2xPaddedFilterLength / 2] = m_ppfloatSampleBuffer[nc][j];
					}
					m_nSampleBufferIndex = 0;
				}
				else
					m_nSampleBufferIndex++;
			} // while

		assert (*cbBytesProcessed == cbBytesActuallyProcessed);
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
		(16 != pWave->wBitsPerSample) &&
		(24 != pWave->wBitsPerSample) &&
		(32 != pWave->wBitsPerSample) )
	{
		return DMO_E_TYPE_NOT_ACCEPTED;
	}

	// TODO: This does not work because m_fexFilterFormat is not set until AllocateStreaming resources is called
	//// make sure the wave format is compatible with the filter
	//if ((m_WfexFilterFormat.nChannels != pWave->nChannels) ||
	//	(m_WfexFilterFormat.nSamplesPerSec != pWave->nSamplesPerSec))
	//{
	//	return DMO_E_TYPE_NOT_ACCEPTED;
	//}

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


