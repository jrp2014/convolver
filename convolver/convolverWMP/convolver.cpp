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
m_fAttenuation_db(0), // default attenuation
m_cbInputLength(0),
m_pbInputData (NULL),
m_bValidTime(false),
m_rtTimestamp(0),
m_bEnabled(TRUE),
m_nPartitions(0), // default, just overlap-save
m_nPlanningRigour(0),
m_ConvolutionList(NULL),
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
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "~CConvolver" << std::endl;);
#endif

	::MoFreeMediaType(&m_mtInput);
	::MoFreeMediaType(&m_mtOutput);

	// Probably otiose
	m_ConvolutionList.set_ptr(NULL);

#if defined(DEBUG) | defined(_DEBUG)
	const int	mode =   (1 * _CRTDBG_MODE_DEBUG) | (0 * _CRTDBG_MODE_WNDW);
	::_CrtSetReportMode (_CRT_WARN, mode);
	::_CrtSetReportMode (_CRT_ERROR, mode);
	::_CrtSetReportMode (_CRT_ASSERT, mode);

	::_CrtDumpMemoryLeaks();
#endif
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
	// 3 = function call trace
	apDebug::gOnly().set(3);

	DEBUGGING(3, cdebug << "FinalConstruct" << std::endl;);

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

	lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
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

		// Read the number of partitions that the convolution routine should use
		lResult = key.QueryDWORDValue(kszPrefsPlanningRigour, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a WORD.
			m_nPlanningRigour = dwValue;
		}

		try // creating m_ConvolutionList might throw
		{
			m_ConvolutionList.set_ptr(new ConvolutionList<float>(m_szFilterFileName,  m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour)); // 0 partitions = overlap-save

		}
		catch (...) 
		{
			//throw(std::runtime_error("Failed to initialize Convolver with current config file"));
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

	*pdwFlags =  DMO_INPUT_STREAMF_WHOLE_SAMPLES | DMO_INPUT_STREAMF_FIXED_SAMPLE_SIZE;

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
// GetMediaType
//
// Helper for GetInputType and GetOutputType
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetMediaType (DWORD dwStreamIndex,
					  DWORD dwTypeIndex,
					  DMO_MEDIA_TYPE *pmt,
					  WORD nChannels,
					  DWORD dwChannelMask)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetMediaType " << dwTypeIndex << " " << nChannels << " " << dwChannelMask << std::endl;);
#endif

	HRESULT hr = S_OK;

	if ( 0 != dwStreamIndex )
	{
		return DMO_E_INVALIDSTREAMINDEX;
	}

	if (NULL == m_ConvolutionList.get_ptr())
	{
		return DMO_E_NO_MORE_ITEMS;
	}

	// Number of Media types = number of available formats * number of different ChannelPaths
	if ( m_FormatSpecs.size * m_ConvolutionList->nConvolutionList() <= dwTypeIndex )
	{
		return DMO_E_NO_MORE_ITEMS;
	}

	// Decompose the dwTypeIndex into dwPathIndex and dwFormatSpecIndex
	DWORD dwPathIndex = dwTypeIndex %  m_ConvolutionList->nConvolutionList(); 
	DWORD dwFormatSpecIndex = dwTypeIndex % m_FormatSpecs.size;
#if defined(DEBUG) | defined(_DEBUG)
	cdebug << "dwTypeIndex:" << dwTypeIndex << " dwPathIndex:" << dwPathIndex << " dwFormatSpecIndex:" << dwFormatSpecIndex << std::endl;
#endif

	if ( NULL == pmt ) 
	{
		hr = ::MoCreateMediaType(&pmt,
			m_FormatSpecs[dwFormatSpecIndex].wFormatTag == WAVE_FORMAT_EXTENSIBLE ? sizeof(WAVEFORMATEXTENSIBLE) : sizeof(WAVEFORMATEX));
		if(FAILED(hr))
		{
			return hr;
		}
	}
	else
	{
		hr = ::MoFreeMediaType(pmt);
		if(FAILED(hr))
		{
			return hr;
		}

		hr = ::MoInitMediaType(pmt, 
			m_FormatSpecs[dwFormatSpecIndex].wFormatTag == WAVE_FORMAT_EXTENSIBLE ? sizeof(WAVEFORMATEXTENSIBLE) : sizeof(WAVEFORMATEX));
		if(FAILED(hr))
		{
			return hr;
		}
	}

	pmt->majortype = MEDIATYPE_Audio;
	pmt->subtype = m_FormatSpecs[dwFormatSpecIndex].SubType;
	pmt->bFixedSizeSamples = true;
	pmt->bTemporalCompression = false;
	pmt->formattype = FORMAT_WaveFormatEx;
	assert( m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample % 8 == 0);
	pmt->lSampleSize = nChannels * m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample / 8; // Block align

	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) pmt->pbFormat;
	if(NULL == pWave)
	{
		return E_OUTOFMEMORY;
	}

	pWave->cbSize = 0; // Size, in bytes, of extra format information appended to the end of the WAVEFORMATEX structure
	pWave->wFormatTag = m_FormatSpecs[dwFormatSpecIndex].wFormatTag;
	pWave->nChannels = nChannels;
	pWave->nSamplesPerSec = m_ConvolutionList->Conv(dwPathIndex).Mixer.nSamplesPerSec();
	pWave->wBitsPerSample = m_FormatSpecs[dwFormatSpecIndex].wBitsPerSample;
	pWave->nBlockAlign = pmt->lSampleSize;
	pWave->nAvgBytesPerSec = pWave->nSamplesPerSec * pWave->nBlockAlign;

	if(pWave->wFormatTag == WAVE_FORMAT_EXTENSIBLE)
	{
		pWave->cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX); // Should be 22

		WAVEFORMATEXTENSIBLE *pWaveXT = ( WAVEFORMATEXTENSIBLE * ) pmt->pbFormat;
		if(NULL == pWaveXT)	// Probably redundant
		{
			return E_OUTOFMEMORY;
		}
		pWaveXT->Samples.wValidBitsPerSample = m_FormatSpecs[dwFormatSpecIndex].wValidBitsPerSample;
		pWaveXT->dwChannelMask = dwChannelMask;
		pWaveXT->SubFormat = m_FormatSpecs[dwFormatSpecIndex].SubType;
	}

	return hr;
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
	DEBUGGING(3, cdebug << "GetInputType " << dwTypeIndex << std::endl;);
#endif

	if(m_ConvolutionList.get_ptr() == NULL)
	{
		return DMO_E_NO_MORE_ITEMS;
	}
	else
	{
		return GetMediaType(dwInputStreamIndex, dwTypeIndex, pmt, 
			m_ConvolutionList->Conv(dwTypeIndex %  m_ConvolutionList->nConvolutionList()).Mixer.nInputChannels(), 0);
	}
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
	DEBUGGING(3, cdebug << "GetOutputType " << dwTypeIndex << std::endl;);
#endif

	if(m_ConvolutionList.get_ptr() == NULL)
	{
		return DMO_E_NO_MORE_ITEMS;
	}
	else
	{
		return GetMediaType(dwOutputStreamIndex, dwTypeIndex, pmt,
			m_ConvolutionList->Conv(dwTypeIndex %  m_ConvolutionList->nConvolutionList()).Mixer.nOutputChannels(),
			m_ConvolutionList->Conv(dwTypeIndex %  m_ConvolutionList->nConvolutionList()).Mixer.dwChannelMask());
	}
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
	DEBUGGING(3, cdebug << "SetInputType " << dwFlags << std::endl;);
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

	// validate that the input media type is acceptable, and compatible with the output type, if set
	// compatibility with the convolution filter is checked below
	hr = ValidateMediaType(pmt, &m_mtOutput);
	if (FAILED(hr))
	{
		return DMO_SET_TYPEF_TEST_ONLY & dwFlags ? S_FALSE : hr;
	}
	else 
	{
		if(DMO_SET_TYPEF_TEST_ONLY & dwFlags) // Gone far enough
		{
			return S_OK;
		}

		// Set the input type

		// TODO: Can't signal failure to get a convolution filter in FinalizeConstruction without
		// breaking access to the properties page, so m_ConvolutionList might be null
		if(m_ConvolutionList.get_ptr() == NULL)
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}
		else
		{
			const WAVEFORMATEX *pWaveIn = ( WAVEFORMATEX * ) pmt->pbFormat;
			if (NULL == pWaveIn)
			{
				return E_POINTER;
			}

			const WAVEFORMATEX *pWaveOut = ( WAVEFORMATEX * ) m_mtOutput.pbFormat;

			if(FAILED(m_ConvolutionList->SelectConvolution(pWaveIn, pWaveOut)))
			{
				return DMO_E_TYPE_NOT_ACCEPTED;
			}
			else // Acceptable type
			{
				// set the type

				// free any existing media type
				::MoFreeMediaType(&m_mtInput);
				::ZeroMemory(&m_mtInput, sizeof(m_mtInput));

				// copy new media type
				hr = ::MoCopyMediaType( &m_mtInput, pmt );
				if (FAILED(hr))
				{
					return hr;
				}

				return m_FormatSpecs.SelectSampleConvertor(pWaveIn, m_InputSampleConvertor);
			}
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
	DEBUGGING(3, cdebug << "SetOutputType " << dwFlags << std::endl;);
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

	// validate that the output media type is acceptable, and sample rate is compatible with the input type
	// and matches our input type (if set)
	hr = ValidateMediaType(&m_mtInput, pmt);
	if (FAILED(hr))
	{
		return DMO_SET_TYPEF_TEST_ONLY & dwFlags ? S_FALSE : DMO_E_TYPE_NOT_ACCEPTED;
	}
	else
	{
		if(DMO_SET_TYPEF_TEST_ONLY & dwFlags) // Gone far enough
		{
			return S_OK;
		}

		// Set the input type
		// TODO: Can't signal failure to get a convolution filter in FinalizeConstruction without
		// breaking access to the properties page, so m_ConvolutionList might be null
		if(m_ConvolutionList.get_ptr() == NULL)
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}
		else
		{
			const WAVEFORMATEX *pWaveIn = ( WAVEFORMATEX * )  m_mtInput.pbFormat;

			const WAVEFORMATEX *pWaveOut = ( WAVEFORMATEX * ) pmt->pbFormat;
			if (NULL == pWaveOut)
			{
				return E_POINTER;
			}

			if(FAILED(m_ConvolutionList->SelectConvolution(pWaveIn, pWaveOut)))
			{
				return DMO_E_TYPE_NOT_ACCEPTED;
			}
			else // Acceptable type
			{
				// set the type

				// free any existing media type
				::MoFreeMediaType(&m_mtOutput);
				::ZeroMemory(&m_mtOutput, sizeof(m_mtOutput));

				// copy new media type
				hr = ::MoCopyMediaType( &m_mtOutput, pmt );
				if (FAILED(hr))
				{
					return hr;
				}

				return m_FormatSpecs.SelectSampleConvertor(pWaveOut, m_OutputSampleConvertor);
			}
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

	// Get the pointer to the input format structure.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;

	// Return the input buffer alignment, in bytes.
	*pcbAlignment = pWave->nBlockAlign;

	// Default to the input sample size, in bytes.
	*pcbSize = m_mtInput.lSampleSize;

	// TODO: There may be no SelectedConvolution
	//if(m_ConvolutionList.get_ptr() != NULL && m_InputSampleConvertor != NULL)
	//{
	//	// For optimization, require data to be presented in half-partition lengths
	//	// TODO: This seems to have no effect.  May need to allocate a new IMediaBuffer, copy
	//	// the bits there, and return that buffer in ProcessOutput.
	//	*pcbSize = m_ConvolutionList->SelectedConvolution().cbLookAhead(m_InputSampleConvertor);

	//	// This plug-in lags its output by half a partition length
	//	*pcbMaxLookahead = m_ConvolutionList->SelectedConvolution().cbLookAhead(m_InputSampleConvertor);
	//}
	//else
	//{
		*pcbMaxLookahead = 0;
	//	return E_FAIL; // May be too strict
	//}

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

	if(m_ConvolutionList.get_ptr() != NULL)
	{
		// Not clear when this routine is called.
		WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat;
		if ( NULL == pWave)
		{
			return E_FAIL;
		}

		//// Latency is half a partition length
		*prtMaxLatency = ::MulDiv( m_ConvolutionList->SelectedConvolution().cbLookAhead(m_InputSampleConvertor), UNITS, pWave->nAvgBytesPerSec);

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

	if (m_ConvolutionList.get_ptr() != NULL)
	{


		// Flush may be called after a new filter is selected via properties, but without
		// calling SetInputType / SetOutputType
		if(!m_ConvolutionList->ConvolutionSelected())
		{
			HRESULT hr = m_ConvolutionList->SelectConvolution(( WAVEFORMATEX * ) m_mtOutput.pbFormat, ( WAVEFORMATEX * ) m_mtOutput.pbFormat);
			if(FAILED(hr))
			{
				return hr;
			}
			if(!m_ConvolutionList->ConvolutionSelected())
				return DMO_E_TYPE_NOT_ACCEPTED;
		}
		m_ConvolutionList->SelectedConvolution().Flush();
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

	// TODO:: Check that this is enough. Should be, since
	// we should always force the client to process all output before the
	// plug-in will accept more input. 
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
	//m_ConvolutionList = NULL;

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

	if(cbInputLength == 0)
	{
		return S_FALSE; // No output to process.
	}
	else
	{
		return S_OK;
	}
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

	// Get the pointer to the input and output format structures.
	const WAVEFORMATEX *pWaveIn = ( WAVEFORMATEX * ) m_mtInput.pbFormat;
	const WAVEFORMATEX *pWaveOut = ( WAVEFORMATEX * ) m_mtOutput.pbFormat;

	// Calculate how many bytes we can process. Tricky, because the number of input and output channels,
	// and the input and output block sizes may not be the equal
	bool bComplete = false; // The entire buffer is not yet processed.

	if (0 != pWaveIn->nBlockAlign && 0 != pWaveOut->nBlockAlign)
	{
		assert(m_cbInputLength % pWaveIn->nBlockAlign == 0); // Not strictly needed

		const DWORD cBlocksToProcess = m_cbInputLength / pWaveIn->nBlockAlign;				// Number of input blocks
		const DWORD cbBytesThatWouldBeGenerated = cBlocksToProcess * pWaveOut->nBlockAlign;

		if (cbBytesThatWouldBeGenerated > cbOutputMaxLength)
		{
			const DWORD cOutputMaxBlocks = cbOutputMaxLength / pWaveOut->nBlockAlign;
			cbBytesToProcess = cOutputMaxBlocks * pWaveIn->nBlockAlign; // only process as much of the input as can fit in the output
		}
		else
		{
			cbBytesToProcess = m_cbInputLength; // process entire input buffer
			bComplete = true;                   // the entire input buffer has been processed. 
		}
	}
	else
	{
		return E_ABORT;
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

		// estimate time length of output buffer
		pOutputBuffers[0].dwStatus |= DMO_OUTPUT_DATA_BUFFERF_TIMELENGTH;
		pOutputBuffers[0].rtTimelength = ::MulDiv(cbBytesGenerated, UNITS, pWaveOut->nAvgBytesPerSec);

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
// CConvolver::GetCaps
//
// Implementation of IWMPPlugin::GetCaps
/////////////////////////////////////////////////////////////////////////////

STDMETHODIMP CConvolver::GetCaps( DWORD* pdwFlags )
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "GetCaps" << std::endl;);
#endif

	if ( NULL == pdwFlags )
	{
		return E_POINTER;
	}

	*pdwFlags = 0; // zero to indicate that the plug-in can convert formats
	// *pdwFlags = WMPPlugin_Caps_CannotConvertFormats; // forces Windows Media Player to handle any necessary format conversion

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

	if( pVal == NULL)
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

	if (_tcscmp(newVal, m_szFilterFileName) != 0) // if new filename set
	{
		_tcsncpy(m_szFilterFileName, newVal, MAX_PATH);

		// May throw
		m_ConvolutionList.set_ptr(new ConvolutionList<float>(m_szFilterFileName,  
			m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour)); // 0 partitions = overlap-save
	}

	return S_OK;
}

// Property get to retrieve the number of partitions by using the public interface.
STDMETHODIMP CConvolver::get_partitions(WORD *pVal)
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
STDMETHODIMP CConvolver::put_partitions(WORD newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_partitions" << std::endl;);
#endif

	if(m_nPartitions != newVal)
	{
		m_nPartitions = newVal;

		// May throw
		m_ConvolutionList.set_ptr(new ConvolutionList<float>(m_szFilterFileName,
			m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour)); // 0 partitions = overlap-save
	}

	return S_OK;
}


// Property get to retrieve the planning rigour by using the public interface.
STDMETHODIMP CConvolver::get_planning_rigour(unsigned int *pVal)
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
STDMETHODIMP CConvolver::put_planning_rigour(unsigned int newVal)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "put_planning_rigour" << std::endl;);
#endif

	if(m_nPlanningRigour != newVal)
	{
		m_nPlanningRigour = newVal;

		// May throw
		m_ConvolutionList.set_ptr(new ConvolutionList<float>(m_szFilterFileName,
			m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour)); // 0 partitions = overlap-save
	}

	return S_OK;
}

// Property to get a filter description 
STDMETHODIMP CConvolver::get_filter_description(std::string* description)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "get_filter_description" << std::endl;);
#endif

	if(m_ConvolutionList.get_ptr() != NULL)
	{
		*description = m_ConvolutionList->DisplayConvolutionList();
		return S_OK;
	}
	else
	{
		*description = "No filter loaded";
		return E_FAIL;
	}
}

// Property to get a filter description 
STDMETHODIMP CConvolver::calculateOptimumAttenuation(float & fAttenuation)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "calculateOptimumAttenuation" << std::endl;);
#endif

	if(m_ConvolutionList.get_ptr() != NULL)
	{
		float min_fAttenuation = MAX_ATTENUATION;
		for(unsigned int i=0; i<m_ConvolutionList->nConvolutionList(); ++i)
		{
			HRESULT hr = m_ConvolutionList->Conv(i).calculateOptimumAttenuation(fAttenuation);
			if(FAILED(hr))
			{
				return hr;
			}
			if(fAttenuation < min_fAttenuation)
			{
				min_fAttenuation = fAttenuation;
			}
		}
		fAttenuation = min_fAttenuation;
		return S_OK;
	}
	else
	{
		fAttenuation = 0;
		return E_FAIL;
	}
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

	if(m_ConvolutionList.get_ptr() == NULL)
	{
		return E_FAIL;
	}

	// Get a pointer to the valid WAVEFORMATEX structure
	// for the current media type.
	WAVEFORMATEX *pWave = ( WAVEFORMATEX * ) m_mtInput.pbFormat; // TODO: reinterpret_cast?

	// Calculate the number of blocks to process.  A block contains the Samples for all channels
	DWORD dwBlocksToProcess = (*cbInputBytesToProcess / pWave->nBlockAlign);

	// Convolve the pbInputData to produce pbOutputData
	*cbOutputBytesGenerated = m_nPartitions == 0 ?
		m_ConvolutionList->SelectedConvolution().doConvolution(pbInputData, pbOutputData,
		m_InputSampleConvertor, m_OutputSampleConvertor,
		dwBlocksToProcess,
		m_fAttenuation_db)
		: 	m_ConvolutionList->SelectedConvolution().doPartitionedConvolution(pbInputData, pbOutputData,
		m_InputSampleConvertor, m_OutputSampleConvertor,
		dwBlocksToProcess,
		m_fAttenuation_db);

	return S_OK;
}

/////////////////////////////////////////////////////////////////////////////
// CConvolver::ValidateMediaType
//
// Validate that the media type is acceptable
/////////////////////////////////////////////////////////////////////////////

HRESULT CConvolver::ValidateMediaType(const DMO_MEDIA_TYPE *pmtInput, const DMO_MEDIA_TYPE *pmtOutput)
{
#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ValidateMediaType" << std::endl;);
#endif

	HRESULT hr = S_OK;

	if (pmtOutput == NULL || pmtInput == NULL)
	{
		return E_POINTER;
	}

#if defined(DEBUG) | defined(_DEBUG)
	if (GUID_NULL != pmtInput->majortype && GUID_NULL != pmtInput->subtype)
	{
		cdebug << waveFormatDescription((WAVEFORMATEXTENSIBLE *)pmtInput->pbFormat, 0, "Input:") << std::endl;
	}
	else
	{
		cdebug << "Null Input" << std::endl;
	}
	if (GUID_NULL != pmtOutput->majortype && GUID_NULL != pmtOutput->subtype)
	{
		cdebug << waveFormatDescription((WAVEFORMATEXTENSIBLE *)pmtOutput->pbFormat, 0, "Output:") << std::endl;
	}
	else
	{
		cdebug << "Null Output" << std::endl;
	}
#endif

	WAVEFORMATEX *pInputWave = NULL;

	if (GUID_NULL != pmtInput->majortype || GUID_NULL != pmtInput->subtype)
	{
		if( ( MEDIATYPE_Audio != pmtInput->majortype ) ||
			( KSDATAFORMAT_SUBTYPE_PCM != pmtInput->subtype && KSDATAFORMAT_SUBTYPE_IEEE_FLOAT != pmtInput->subtype ) ||
			( FORMAT_WaveFormatEx != pmtInput->formattype ) )
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}
		else
		{
			pInputWave = (WAVEFORMATEX *) pmtInput->pbFormat;
			hr = m_FormatSpecs.CheckSampleFormat(pInputWave);
			if(FAILED(hr))
			{
				return hr;
			}
		}
	}

	WAVEFORMATEX *pOutputWave = NULL;

	if (GUID_NULL != pmtOutput->majortype || GUID_NULL != pmtOutput->subtype)
	{
		if( ( MEDIATYPE_Audio != pmtOutput->majortype ) ||
			( KSDATAFORMAT_SUBTYPE_PCM != pmtOutput->subtype && KSDATAFORMAT_SUBTYPE_IEEE_FLOAT != pmtOutput->subtype ) ||
			( FORMAT_WaveFormatEx != pmtOutput->formattype ) )
		{
			return DMO_E_TYPE_NOT_ACCEPTED;
		}
		else
		{
			pOutputWave = (WAVEFORMATEX *) pmtOutput->pbFormat;
			hr = m_FormatSpecs.CheckSampleFormat(pOutputWave);
			if(FAILED(hr))
			{
				return hr;
			}
		}
	}

	// Check that there is a compatible filter path
	if(m_ConvolutionList.get_ptr() != NULL)
	{
		hr = m_ConvolutionList->CheckConvolutionList(pInputWave, pOutputWave);
		if(FAILED(hr))
		{
			return hr;
		}
	}
	else
	{
		return DMO_E_TYPE_NOT_ACCEPTED;
	}

#if defined(DEBUG) | defined(_DEBUG)
	DEBUGGING(3, cdebug << "ValidateMediaType OK" << std::endl;);
#endif

	// media type is valid
	return hr;
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
	hrsrc = FindResource(hModule, MAKEINTRESOURCE(id), TEXT("BITMAPWITHHEADER")); // TODO: does not work
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
		// NB The maximum dimensions for DSP plug-in graphics are 38 pixels wide and 14 pixels high.
		pVar->punkVal = StreamFromResource(IDR_BITMAPWITHHEADER);
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
