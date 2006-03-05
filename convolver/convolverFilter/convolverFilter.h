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
#pragma once

#include "convolution\config.h"
#include "convolution\convolution.h"

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\DirectShow Filter");
const TCHAR kszPrefsAttenuation[] = _T("Attenuation");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");
const TCHAR kszPrefsPartitions[] = _T("Partitions");
const TCHAR kszPrefsPlanningRigour[] = _T("PlanningRigour");
const TCHAR kszPrefsDither[] = _T("Dither");
const TCHAR kszPrefsNoiseShaping[] = _T("NoiseShaping");

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
	HRESULT SetMediaType(PIN_DIRECTION direction, const CMediaType *pMediaType);
	HRESULT CconvolverFilter::BeginFlush(void);

    // These implement the custom IconvolverFilter interface
	STDMETHOD(get_filterfilename)(TCHAR *pVal[]);
	STDMETHOD(put_filterfilename)(TCHAR newVal[]);

	STDMETHOD(get_attenuation)(float *pVal);
	STDMETHOD(put_attenuation)(float newVal);

	STDMETHOD(get_partitions)(DWORD *pVal);
	STDMETHOD(put_partitions)(DWORD newVal);

	STDMETHOD(get_planning_rigour)(unsigned int *pVal);
	STDMETHOD(put_planning_rigour)(unsigned int newVal);

	STDMETHOD(get_dither)(unsigned int *pVal);
	STDMETHOD(put_dither)(unsigned int newVal);

	STDMETHOD(get_noiseshaping)(unsigned int *pVal);
	STDMETHOD(put_noiseshaping)(unsigned int newVal);

	STDMETHOD(get_filter_description)(std::string* description);
	STDMETHOD(calculateOptimumAttenuation)(float & fAttenuation);

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
	TCHAR					m_szFilterFileName[MAX_PATH];
	unsigned int			m_nPlanningRigour;	// Estimate, Measure, Patient, Exhaustive



	typedef float			BaseT;
	typedef NoiseShaper<BaseT>::NoiseShapingType NoiseShapingType;
	typedef Ditherer<BaseT>::DitherType DitherType;

	DitherType				m_nDither;				// The dither type index
	NoiseShapingType		m_nNoiseShaping;	// The noise shaping type index

	Holder< ConvolutionList<BaseT> >	m_ConvolutionList;			// Processing class.
	Holder<ConvertSample<BaseT> >		m_InputSampleConvertor;		// functionoid conversion between BYTE and 
	Holder<ConvertSample<BaseT> >		m_OutputSampleConvertor;	// BaseT

	ConvertSampleMaker<BaseT>			m_FormatSpecs;				// Supported formats

	WAVEFORMATEXTENSIBLE				m_WaveInXT;					// The current formats;
	WAVEFORMATEXTENSIBLE				m_WaveOutXT;
};

