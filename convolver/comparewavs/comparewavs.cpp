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
//////////////////////////////////////////////////////////////////////////////////
//
// comparewavs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "dxstdafx.h"

int _tmain(int argc, _TCHAR* argv[])
{

	HRESULT hr = S_OK;

	if (argc != 6)
	{
		_tprintf(TEXT("Usage: comparewavs inputwavfile1.wav skipblocks1 inputwavfile2.wav skipblocks2 differencewavfilename.wav\n"));
		return 1;
	}

	CWaveFile* WavF1 = new CWaveFile;
	CWaveFile* WavF2 = new CWaveFile;

	if( FAILED( hr = WavF1->Open( argv[1], NULL, WAVEFILE_READ ) ) )
	{
		_tprintf(TEXT("Failed to open %s for reading\n"), argv[1]);
		goto exit;
	}

	if( FAILED( hr = WavF2->Open( argv[3], NULL, WAVEFILE_READ ) ) )
	{
		_tprintf(TEXT("Failed to open %s for reading\n"), argv[3]);
		goto exit;
	}

	if ( WavF1->GetSize() != WavF2->GetSize() )
	{
		_tprintf(TEXT("File sizes not equal: %i, %i\n"), WavF1->GetSize(), WavF2->GetSize());
	}

	if ( WavF1->GetFormat()->nAvgBytesPerSec != WavF2->GetFormat()->nAvgBytesPerSec)
	{
		_tprintf(TEXT("AvgBytesPerSec not equal: %i, %i\n"), WavF1->GetFormat()->nAvgBytesPerSec, WavF2->GetFormat()->nAvgBytesPerSec);
		hr = 1;
		goto exit;
	}

	if ( WavF1->GetFormat()->nBlockAlign != WavF2->GetFormat()->nBlockAlign)
	{
		_tprintf(TEXT("BlockAlign not equal: %i, %i\n"), WavF1->GetFormat()->nBlockAlign, WavF2->GetFormat()->nBlockAlign);
		hr = 1;
		goto exit;
	}

	if ( WavF1->GetFormat()->nChannels != WavF2->GetFormat()->nChannels)
	{
		_tprintf(TEXT("Channels not equal: %i, %i\n"), WavF1->GetFormat()->nChannels, WavF2->GetFormat()->nChannels);
		hr = 1;
		goto exit;
	}

	if ( WavF1->GetFormat()->nChannels != WavF2->GetFormat()->nChannels)
	{
		_tprintf(TEXT("Channels not equal: %i, %i\n"), WavF1->GetFormat()->nChannels, WavF2->GetFormat()->nChannels);
		hr = 1;
		goto exit;
	}

	if ( WavF1->GetFormat()->nSamplesPerSec != WavF2->GetFormat()->nSamplesPerSec)
	{
		_tprintf(TEXT("SamplesPerSec not equal: %i, %i\n"), WavF1->GetFormat()->nSamplesPerSec, WavF2->GetFormat()->nSamplesPerSec);
		hr = 1;
		goto exit;
	}

	if ( WavF1->GetFormat()->wBitsPerSample != WavF2->GetFormat()->wBitsPerSample)
	{
		_tprintf(TEXT("BitsPerSample not equal: %i, %i\n"), WavF1->GetFormat()->wBitsPerSample, WavF2->GetFormat()->wBitsPerSample);
		hr = 1;
		goto exit;
	}

	if ( WavF1->GetFormat()->wFormatTag != WavF2->GetFormat()->wFormatTag)
	{
		_tprintf(TEXT("FormatTag not equal: %i, %i\n"), WavF1->GetFormat()->wFormatTag, WavF2->GetFormat()->wFormatTag);
		hr = 1;
		goto exit;
	}

	CWaveFile* WavDiff = new CWaveFile;
	if (FAILED(hr = WavDiff->Open(argv[5], WavF1->GetFormat(), WAVEFILE_WRITE)))
	{
		_tprintf(TEXT("Failed to open %s for writing\n"), argv[5]);
		goto exit;
	}

#ifdef UNICODE 
	CHAR strTmp[2*(sizeof(10)+1)];	// SIZE equals (2*(sizeof(tstr)+1)). This ensures enough
	// room for the multibyte characters if they are two
	// bytes long and a terminating null character.

	wcstombs(strTmp, (const wchar_t *) argv[2], sizeof(strTmp)); 
	int nSkipBlocks1 =  atoi(strTmp);
	wcstombs(strTmp, (const wchar_t *) argv[4], sizeof(strTmp)); 
	int nSkipBlocks2 =  atoi(strTmp); 

#else 

	int nSkipBlocks1 = atoi(argv[2]);
	int nSkipBlocks2 = atoi(argv[4]);

#endif

	DWORD dwSizeRead = 0;

	WavF1->ResetFile();
	WavF2->ResetFile();

	switch (WavF1->GetFormat()->wFormatTag)
	{
	case WAVE_FORMAT_PCM:
		_tprintf(TEXT("PCM WAV format not supported\n"));
		hr =  E_NOTIMPL; // TODO: Unimplemented filter type
		break;

	case WAVE_FORMAT_IEEE_FLOAT:
		{
			switch (WavF1->GetFormat()->wBitsPerSample)
			{
			case sizeof(float) * 8: //32
				{
					DWORD dwSampleSize = sizeof(float);
					UINT nSizeWrote = 0;
					float fSample1 = 0;
					float fSample2 = 0;
					float fSampleDiff = 0;

					for (unsigned int block=0; block < min(nSkipBlocks1, WavF1->GetSize() / WavF1->GetFormat()->nBlockAlign); block++)
					{
						for (unsigned int channel=0; channel < WavF1->GetFormat()->nChannels; channel++)
						{
							if (hr = FAILED(WavF1->Read((BYTE*)&fSample1, dwSampleSize, &dwSizeRead)))
							{
								_tprintf(TEXT("Failed to inital read sample from %s\n"), argv[1]);
								goto exit;
							}
						}
					}

					for (unsigned int block=0; block < min(nSkipBlocks2, WavF1->GetSize() / WavF1->GetFormat()->nBlockAlign); block++)
					{
						for (unsigned int channel=0; channel < WavF1->GetFormat()->nChannels; channel++)
						{
							if (hr = FAILED(WavF2->Read((BYTE*)&fSample2, dwSampleSize, &dwSizeRead)))
							{
								_tprintf(TEXT("Failed to read initial sample from %s\n"), argv[1]);
								goto exit;
							}
						}
					}

					for (unsigned int block=max(nSkipBlocks1, nSkipBlocks2); block < WavF1->GetSize() / WavF1->GetFormat()->nBlockAlign; block++)
					{
						for (unsigned int channel=0; channel <WavF1->GetFormat()->nChannels; channel++)
						{
							if (hr = FAILED(WavF1->Read((BYTE*)&fSample1, dwSampleSize, &dwSizeRead)))
							{
								_tprintf(TEXT("Failed to read sample from %s\n"), argv[1]);
								goto exit;
							}

							if (hr = FAILED(WavF2->Read((BYTE*)&fSample2, dwSampleSize, &dwSizeRead)))
							{
								_tprintf(TEXT("Failed to read sample from %s\n"), argv[3]);
								goto exit;
							}

							fSampleDiff = fSample1 - fSample2;

							if (FAILED(hr = WavDiff->Write(dwSampleSize, (BYTE *)&fSampleDiff, &nSizeWrote)))
							{
								_tprintf(TEXT("Failed to write sample to %s\n"), argv[5]);
								goto exit;
							};

#ifdef _DEBUG
							_tprintf(TEXT("%i,%i, %.6g, %.6g, %.6g\n"), block, channel, fSample1, fSample2, fSampleDiff);
#endif
						}
					}
				}
				break;
			default:
				_tprintf(TEXT("Unsupported IEEE float format\n"));
				hr =  E_NOTIMPL;
				break;
			}
		}
		break;
	default:
		_tprintf(TEXT("Unsupported WAV file format\n"));
		hr = E_NOTIMPL;
		break;
	}

exit:
	if (hr)
		_tprintf(TEXT("Error code: 0x%x\n"), hr);

	SAFE_DELETE(WavF1);
	SAFE_DELETE(WavF2);
	SAFE_DELETE(WavDiff);
	return hr;
}

