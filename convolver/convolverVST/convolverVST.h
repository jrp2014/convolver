//-------------------------------------------------------------------------------------------------------
// VST Plug-Ins SDK
// Version 2.4		$Date: 2005/12/06 09:09:26 $
//
// Category     : VST 2.x SDK Samples
// Filename     : convolverVST.h
// Created by   : Steinberg Media Technologies
// Description  : Simple Delay plugin (Mono->Stereo)
//
// © 2005, Steinberg Media Technologies, All Rights Reserved
//-------------------------------------------------------------------------------------------------------

#ifndef __convolverVST__
#define __convolverVST__

#include "convolution\config.h"
#include "public.sdk/source/vst2.x/audioeffectx.h"

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\VST Filter");
const TCHAR kszPrefsAttenuation[] = _T("Attenuation");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");
const TCHAR kszPrefsPartitions[] = _T("Partitions");
const TCHAR kszPrefsPlanningRigour[] = _T("PlanningRigour");

enum
{
	// Global
	kNumPrograms = 16,

	// Parameters Tags
	kAttenuation_db = 0,
	kPartitions,
	kFilterFileName,
	kPlanningRigour

	kNumParams
};

class convolverVST;

//------------------------------------------------------------------------
class convolverVSTProgram
{
friend class convolverVST;
public:
	convolverVSTProgram ();
	~convolverVSTProgram () {}

private:	
	float					fAttenuation_db;	// attenuation (up to +/-20dB).  What is displayed.
	WORD					nPartitions;		// Number of partitions to be used in convolution algorithm
	TCHAR					szFilterFileName[MAX_PATH];
	unsigned int			nPlanningRigour;	// Estimate, Measure, Patient, Exhaustive

	Holder< ConvolutionList<float> >	ConvolutionList;				// Processing class.  Handle manages resources
};

//------------------------------------------------------------------------
class convolverVST : public AudioEffectX
{
public:
	convolverVST (audioMasterCallback audioMaster);
	~convolverVST ();

	virtual void processReplacing (float **inputs, float **outputs, VstInt32 sampleFrames);

	virtual void setProgram (long program);
	virtual void setProgramName (char *name);
	virtual void getProgramName (char *name);
	virtual bool getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text);
	
	virtual void setParameter (VstInt32 index, float value);
	virtual float getParameter (VstInt32 index);
	virtual void getParameterLabel (VstInt32 index, char *label);
	virtual void getParameterDisplay (VstInt32 index, char *text);
	virtual void getParameterName (VstInt32 index, char *text);

	virtual void resume ();

	virtual bool getEffectName (char* name);
	virtual bool getVendorString (char* text);
	virtual bool getProductString (char* text);
	virtual VstInt32 getVendorVersion () { return 1000; }
	
	virtual VstPlugCategory getPlugCategory () { return kPlugCategRoomFx; }

protected:
	void setDelay (float delay);

	convolverVSTProgram *programs;
	
	float *buffer;
	float fDelay, fFeedBack, fOut;

	float					fAttenuation_db;	// attenuation (up to +/-20dB).  What is displayed.
	WORD					nPartitions;		// Number of partitions to be used in convolution algorithm
	TCHAR					szFilterFileName[MAX_PATH];
	unsigned int			nPlanningRigour;	// Estimate, Measure, Patient, Exhaustive

	Holder< ConvolutionList<float> >	ConvolutionList;

	
	long delay;
	long size;
	long cursor;
};

#endif
