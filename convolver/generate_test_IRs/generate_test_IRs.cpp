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
// generate_test_IRs.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "dxstdafx.h"

int generate_perfect_dirac_delta(int nSamplesPerSec, int nChannels, int nSilence);

int _tmain(int argc, _TCHAR* argv[])
{
	if (argc != 4)
	{
		_tprintf(TEXT("Usage: generate_test_IRs <SamplesPerSec> <Channels> <Silence>\n"));
		return 1;
	}




#ifdef UNICODE 
	CHAR strTmp[2*(sizeof(10)+1)];	// SIZE equals (2*(sizeof(tstr)+1)). This ensures enough
									// room for the multibyte characters if they are two
									// bytes long and a terminating null character.

	wcstombs(strTmp, (const wchar_t *) argv[1], sizeof(strTmp)); 
	int nSamplesPerSec =  atoi(strTmp);
	wcstombs(strTmp, (const wchar_t *) argv[2], sizeof(strTmp)); 
	int nChannels=  atoi(strTmp);
	wcstombs(strTmp, (const wchar_t *) argv[3], sizeof(strTmp)); 
	int nSilence=  atoi(strTmp); 

#else 

	int nSamplesPerSec = atoi(argv[1]);
	int nChannels= atoi(argv[2]);
	int nSilence= atoi(argv[3]);

#endif

	generate_perfect_dirac_delta(nSamplesPerSec, nChannels, nSilence);
	return 0;
}



int generate_perfect_dirac_delta(int nSamplesPerSec, int nChannels, int nSilence)
{
	HRESULT hr = S_OK;

	// Output file format
	// See 
	// http://msdn.microsoft.com/library/default.asp?url=/library/en-us/directshow/htm/waveformatexstructure.asp
	// http://www.microsoft.com/whdc/device/audio/multichaud.mspx

	WAVEFORMATEX  wfx;
	ZeroMemory( &wfx, sizeof(wfx));
	wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
	wfx.nSamplesPerSec = nSamplesPerSec;
	wfx.wBitsPerSample =  sizeof(float) * 8; 
	wfx.nChannels = nChannels;
	wfx.nBlockAlign = wfx.nChannels * (wfx.wBitsPerSample / 8);  // ie. sizeof(float)
	wfx.nAvgBytesPerSec = wfx.nBlockAlign * wfx.nSamplesPerSec;


	TCHAR sFileName[100];
	int j = swprintf(sFileName, TEXT("PerfectDiracDelta-%i"), nSamplesPerSec);
	j += swprintf(sFileName + j, TEXT("-%i"), nChannels);
	j += swprintf(sFileName + j, TEXT("-%i.wav"), nSilence);

	CWaveFile* impulseFile = new CWaveFile;
	if (FAILED(hr = impulseFile->Open(sFileName, &wfx, WAVEFILE_WRITE)))
	{
		SAFE_DELETE( impulseFile );
		return DXTRACE_ERR_MSGBOX(TEXT("Failed to open impulse file for writing"), hr);
	}

	UINT nSizeWrote = 0;
	float sample = 1.0;  // first sample
	for (int i = 0; i <= nSilence; i++) // <= as the first is the dirac impulse
	{
		for (int j = 0; j < nChannels; j++)
		{
			if (FAILED(hr = impulseFile->Write(sizeof(float), (BYTE *)&sample, &nSizeWrote)))
			{
				SAFE_DELETE( impulseFile );
				return DXTRACE_ERR_MSGBOX(TEXT("Failed to write to impulse file"), hr);
			}

		}
		sample = 0.0; // subsequent samples
	}

	hr = impulseFile->Close();

	return hr;
}
