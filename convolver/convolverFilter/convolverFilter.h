#pragma once

#include "convolution\config.h"
#include "convolution\convolution.h"

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\DirectShow Filter");
const TCHAR kszPrefsAttenuation[] = _T("Attenuation");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");
const TCHAR kszPrefsPartitions[] = _T("Partitions");

class CconvolverFilter : public CTransformFilter,
		 public IconvolverFilter,
		 public ISpecifyPropertyPages
{

public:

    DECLARE_IUNKNOWN
    static CUnknown * WINAPI CreateInstance(LPUNKNOWN punk, HRESULT *phr);

    // Reveals IconvolverFilter and ISpecifyPropertyPages
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void ** ppv);

    // Overrriden from CTransformFilter base class
    HRESULT Transform(IMediaSample *pIn, IMediaSample *pOut);
    HRESULT CheckInputType(const CMediaType *mtIn);
    HRESULT CheckTransform(const CMediaType *mtIn, const CMediaType *mtOut);
    HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *pProperties);
    HRESULT GetMediaType(int iPosition, CMediaType *pMediaType);
	HRESULT CconvolverFilter::SetMediaType(PIN_DIRECTION direction, const CMediaType *pMediaType);

    // These implement the custom IconvolverFilter interface
	STDMETHODIMP get_filterfilename(TCHAR *pVal[]);
	STDMETHODIMP put_filterfilename(TCHAR newVal[]);

	STDMETHODIMP get_partitions(DWORD *pVal);
	STDMETHODIMP put_partitions(DWORD newVal);

	STDMETHODIMP get_filter_description(std::string* description);

	STDMETHODIMP get_attenuation(float *pVal);
	STDMETHODIMP put_attenuation(float newVal);

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

    // ISpecifyPropertyPages interface
    STDMETHODIMP GetPages(CAUUID *pPages);

private:

    // Constructor
    CconvolverFilter(TCHAR *tszName, LPUNKNOWN punk, HRESULT *phr);
	//~CconvolverFilter();

    BOOL CanPerformTransform(const CMediaType *pMediaType) const;

    CCritSec m_convolverFilterLock;         // Private play critical section

	HRESULT Copy(IMediaSample *pSource, IMediaSample *pDest) const;


	float					m_fAttenuation_db;	// attenuation (up to +/-20dB).  What is displayed.
	DWORD					m_nPartitions;		// Number of partitions to be used in convolution algorithm
	TCHAR							m_szFilterFileName[MAX_PATH * 2];

	Holder< CConvolution<float> >	m_Convolution;				// Processing class.  Handle manages resources
	Sample<float>*					m_InputSampleConvertor;		// functionoid conversion between BYTE and 
	Sample<float>*					m_OutputSampleConvertor;	// float

	CFormatSpecs<float>				m_FormatSpecs;				// Supported formats

	WAVEFORMATEXTENSIBLE			m_pWaveInXT;				// The current formats;
	WAVEFORMATEXTENSIBLE			m_pWaveOutXT;
};

