//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2005/11/30 13:04:21 $
//
// Category     : VST 2.x SDK Samples
// Filename     : convolverVST.cpp
// Created by   : Steinberg Media Technologies
// Description  : Simple Convolver plugin (Mono->Stereo)
//
// © 2005, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#ifndef __convolverVST__
#include "convolverVST.h"
#endif


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

//----------------------------------------------------------------------------- 
convolverVSTProgram::convolverVSTProgram ()
{
	// default Program Values

	szFilterFileName[0] = 0;

	CRegKey key;
	DWORD   dwValue = 0;

	LONG lResult = key.Open(HKEY_CURRENT_USER, kszPrefsRegKey, KEY_READ);
	if (ERROR_SUCCESS == lResult)
	{
		// Read the attenuation value from the registry. 
		lResult = key.QueryDWORDValue(kszPrefsAttenuation, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a float.
			fAttenuation_db = decode_Attenuationdb(dwValue);
		}

		// Read the filter file name from the registry. 
		TCHAR szValue[MAX_PATH]	= TEXT("");
		ULONG ulMaxPath			= MAX_PATH;

		// Read the filter filename value.
		lResult = key.QueryStringValue(kszPrefsFilterFileName, szValue, &ulMaxPath );

		if (ERROR_SUCCESS == lResult)
		{
			_tcsncpy(szFilterFileName, szValue, ulMaxPath);
		}

		// Read the number of partitions that the convolution routine should use
		lResult = key.QueryDWORDValue(kszPrefsPartitions, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a WORD.
			nPartitions = dwValue;
		}

		// Read the planning rigour that the convolution routine should use
		lResult = key.QueryDWORDValue(kszPrefsPlanningRigour, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			// Convert the DWORD to a unsigned int.
			nPlanningRigour = dwValue;
		}

		try // creating ConvolutionList might throw
		{
			ConvolutionList.set_ptr(new ConvolutionList<float>(szFilterFileName,  nPartitions == 0 ? 1 : nPartitions,
				nPlanningRigour)); // 0 partitions = overlap-save
		}
		catch (...) 
		{
			ConvolutionList.set_ptr(NULL);
		}
	} 

	strcpy (name, "Init");
}

//-----------------------------------------------------------------------------
convolverVST::convolverVST (audioMasterCallback audioMaster)
	: AudioEffectX (audioMaster, kNumPrograms, kNumParams)
{
	// init
	size = 44100;
	cursor = 0;
	delay = 0;
	buffer = new float[size];
	
	programs = new convolverVSTProgram[numPrograms];
	fConvolver = fFeedBack = fOut = 0;

	if (programs)
		setProgram (0);

	setNumInputs (1);	// mono input
	setNumOutputs (2);	// stereo output

	setUniqueID ('Conv');

	resume ();		// flush buffer
}

//------------------------------------------------------------------------
convolverVST::~convolverVST ()
{
	if (buffer)
		delete[] buffer;
	if (programs)
		delete[] programs;
}

//------------------------------------------------------------------------
void convolverVST::setProgram (long program)
{
	convolverVSTProgram* ap = &programs[program];

	curProgram = program;
	setParameter (kConvolver, ap->fConvolver);	
	setParameter (kFeedBack, ap->fFeedBack);
	setParameter (kOut, ap->fOut);
}

//------------------------------------------------------------------------
void convolverVST::setConvolver (float fdelay)
{
	fConvolver = fdelay;
	programs[curProgram].fConvolver = fdelay;
	cursor = 0;
	delay = (long)(fdelay * (float)(size - 1));
}

//------------------------------------------------------------------------
void convolverVST::setProgramName (char *name)
{
	strcpy (programs[curProgram].name, name);
}

//------------------------------------------------------------------------
void convolverVST::getProgramName (char *name)
{
	if (!strcmp (programs[curProgram].name, "Init"))
		sprintf (name, "%s %d", programs[curProgram].name, curProgram + 1);
	else
		strcpy (name, programs[curProgram].name);
}

//-----------------------------------------------------------------------------------------
bool convolverVST::getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text)
{
	if (index < kNumPrograms)
	{
		strcpy (text, programs[index].name);
		return true;
	}
	return false;
}

//------------------------------------------------------------------------
void convolverVST::resume ()
{
	memset (buffer, 0, size * sizeof (float));
	AudioEffectX::resume ();
}

//------------------------------------------------------------------------
void convolverVST::setParameter (VstInt32 index, float value)
{
	convolverVSTProgram* ap = &programs[curProgram];

	switch (index)
	{
		case kConvolver :    setConvolver (value);					break;
		case kFeedBack : fFeedBack = ap->fFeedBack = value; break;
		case kOut :      fOut = ap->fOut = value;			break;
	}
}

//------------------------------------------------------------------------
float convolverVST::getParameter (VstInt32 index)
{
	float v = 0;

	switch (index)
	{
		case kConvolver :    v = fConvolver;	break;
		case kFeedBack : v = fFeedBack; break;
		case kOut :      v = fOut;		break;
	}
	return v;
}

//------------------------------------------------------------------------
void convolverVST::getParameterName (VstInt32 index, char *label)
{
	switch (index)
	{
		case kConvolver :    strcpy (label, "Convolver");		break;
		case kFeedBack : strcpy (label, "FeedBack");	break;
		case kOut :      strcpy (label, "Volume");		break;
	}
}

//------------------------------------------------------------------------
void convolverVST::getParameterDisplay (VstInt32 index, char *text)
{
	switch (index)
	{
		case kConvolver :    int2string (delay, text, kVstMaxParamStrLen);			break;
		case kFeedBack : float2string (fFeedBack, text, kVstMaxParamStrLen);	break;
		case kOut :      dB2string (fOut, text, kVstMaxParamStrLen);			break;
	}
}

//------------------------------------------------------------------------
void convolverVST::getParameterLabel (VstInt32 index, char *label)
{
	switch (index)
	{
		case kConvolver :    strcpy (label, "samples");	break;
		case kFeedBack : strcpy (label, "amount");	break;
		case kOut :      strcpy (label, "dB");		break;
	}
}

//------------------------------------------------------------------------
bool convolverVST::getEffectName (char* name)
{
	strcpy (name, "Convolver");
	return true;
}

//------------------------------------------------------------------------
bool convolverVST::getProductString (char* text)
{
	strcpy (text, "Convolver");
	return true;
}

//------------------------------------------------------------------------
bool convolverVST::getVendorString (char* text)
{
	strcpy (text, "convolver.sf.net");
	return true;
}

//---------------------------------------------------------------------------
void convolverVST::processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames)
{
	float* in = inputs[0];
	float* out1 = outputs[0];
	float* out2 = outputs[1];

	while (--sampleFrames >= 0)
	{
		float x = *in++;
		float y = buffer[cursor];
		buffer[cursor++] = x + y * fFeedBack;
		if (cursor >= delay)
			cursor = 0;
		*out1++ = y;
		if (out2)
			*out2++ = y;
	}
}
