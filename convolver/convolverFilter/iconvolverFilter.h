#pragma once

// 2A0F29DC-2C05-4716-9522-3618687EAA5F
DEFINE_GUID(CLSID_convolverFilter, 
			0x2a0f29dc, 0x2c05, 0x4716, 0x95, 0x22, 0x36, 0x18, 0x68, 0x7e, 0xaa, 0x5f);

// AD837255-8BC0-42F4-BAB9-F48D3E1A2227
DEFINE_GUID(CLSID_convolverFilterPropertyPage, 
			0xad837255, 0x8bc0, 0x42f4, 0xba, 0xb9, 0xf4, 0x8d, 0x3e, 0x1a, 0x22, 0x27);

struct convolverFilterParameters 
{
	TCHAR szFilterFileName[MAX_PATH * 2];
	float fAttenuation_db;	// attenuation (up to +/-20dB).  What is displayed.
	DWORD nPartitions;		// Number of partitions to be used in convolution algorithm
};

#ifdef __cplusplus
extern "C" {
#endif

	// D5468D97-B327-48D9-A536-CF13CF27FA2D
	DEFINE_GUID(IID_IconvolverFilter, 
		0xd5468d97, 0xb327, 0x48d9, 0xa5, 0x36, 0xcf, 0x13, 0xcf, 0x27, 0xfa, 0x2d);

	DECLARE_INTERFACE_(IconvolverFilter, IUnknown)
	{
		STDMETHOD(get_filterfilename) (THIS_
			TCHAR *pVal[]
			) PURE;

		STDMETHOD(put_filterfilename) (THIS_
			TCHAR newVal[]
			) PURE;

		STDMETHOD(get_partitions) (THIS_
			DWORD *pVal
			) PURE;

		STDMETHOD(put_partitions) (THIS_
			DWORD newVal
			) PURE;

		STDMETHOD(get_filter_description)(THIS_
			std::string* description
			) PURE;

		STDMETHOD(get_attenuation)(THIS_
			float *pVal
			) PURE;

		STDMETHOD(put_attenuation)(THIS_
			float newVal
			) PURE;

		// The following pair is needed because DWORD is unsigned
		virtual float decode_Attenuationdb(const DWORD dwValue) PURE;
		virtual DWORD encode_Attenuationdb(const float fValue) PURE;

	};

#ifdef __cplusplus
}
#endif

