// Convolver: VST plug-in that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005,2006  John Pavel
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
#pragma once

#include "convolution\config.h"


#include "convolvervsteditor.h"

#include "public.sdk\source\vst2.x\audioeffectx.h"



#include "convolution\convolution.h"

enum
{
	// Global
	kNumPrograms = 16,

	// Parameters Tags
	kAttenuationdB = 0,
	kPartitions,
	kPlanningRigour,

	kNumParams
};

const VstInt32 MAX_ATTENUATION_DB_VST = 200;

const VstInt32 kMaxInputs = 1;
const VstInt32 kMaxOutputs = 1;

class ConvolverVSTProgram
{
public:
	ConvolverVSTProgram ();
	~ConvolverVSTProgram () {}

	float fAttenuationdB;
	DWORD nPartitions;
	PlanningRigour::Degree nPlanningRigour;

	char name[kVstMaxProgNameLen+1];

	static const char defaultname[kVstMaxProgNameLen+1];
};


class ConvolverVST :
	public AudioEffectX
{
public:
	ConvolverVST (audioMasterCallback audioMaster);
	~ConvolverVST ();

	//virtual void process (float **inputs, float **outputs, VstInt32 sampleFrames);
	virtual void processReplacing (float **inputs, float **outputs, VstInt32 sampleFrames);
	virtual void processDoubleReplacing (double** inputs, double** outputs, VstInt32 sampleFrames);

	virtual void setProgram (VstInt32 program);
	virtual void setProgramName (char *name);
	virtual void getProgramName (char *name);
	virtual bool getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text);

	virtual void setParameter (VstInt32 index, float value);
	virtual float getParameter (VstInt32 index);
	virtual void getParameterLabel (VstInt32 index, char *label);
	virtual void getParameterDisplay (VstInt32 index, char *text);
	virtual void getParameterName (VstInt32 index, char *text);
	virtual bool getParameterProperties  (VstInt32  index, VstParameterProperties *  p);
	virtual bool string2parameter (VstInt32 index, char* text);
	virtual bool setProcessPrecision  (VstInt32  precision) 
	{
		return precision == kVstProcessPrecision32;  // Change to 64 if BaseT is double
	};

	virtual VstInt32 getGetTailSize();

	virtual void open ();	// Called when plug-in is initialized
	virtual void setSampleRate  (  float  sampleRate   );
	//virtual void close () {}	// Called when plug-in will be released
	//virtual void suspend () {}	// Called when plug-in is switched to off
	virtual void resume ();	// Called when plug-in is switched to on

	virtual bool getEffectName (char* name);
	virtual bool getVendorString (char* text);
	virtual bool getProductString (char* text);
	virtual VstInt32 getVendorVersion () { return 4000; }

	virtual VstPlugCategory getPlugCategory () { return /* kPlugCategEffect */ kPlugCategRoomFx ; }
	virtual VstInt32 ConvolverVST::canDo  (char*  text);

	virtual bool getInputProperties (VstInt32 index, VstPinProperties* properties);
	virtual bool getOutputProperties (VstInt32 index, VstPinProperties* properties);

	virtual bool canParameterBeAutomated  (  VstInt32  index   );

	virtual bool getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput);
	virtual bool setSpeakerArrangement (VstSpeakerArrangement* pluginInput, VstSpeakerArrangement* pluginOutput);

protected:
	void setFilterFilename(TCHAR m_szFilterFileName[MAX_PATH]);

	float fAttenuationdB;
	DWORD nPartitions;
	PlanningRigour::Degree nPlanningRigour;
	WAVEFORMATEX WaveIn, WaveOut;				// Used to select the right filter

	TCHAR m_szFilterFileName[MAX_PATH];
	Holder< ConvolutionList<BaseT>, Thread_Mutex >	m_ConvolutionList;			// Processing class.
	Holder< ConvertSample<BaseT> > m_Convertfloat;
	Holder< ConvertSample<BaseT> > m_Convertdouble;

	ConvolverVSTProgram *programs;
	VstSpeakerArrangement* plugInput;
	VstSpeakerArrangement* plugOutput;

	VstInt32  MasterVersion;	// The Host's version (for example 2400 for VST 2.4). 

	char HostVendorString[64];		// A string identifying the vendor. 
	char HostProductString[64];		// product name. 
	VstInt32  HostVendorVersion;	// vendor-specific version (for example 3200 for Nuendo 3.2). 
	VstHostLanguage HostLanguage;	// the Host's language (VstHostLanguage).  


	bool bOpened;
	bool bSampleRateSet;
	bool bAborted;

	void abort(LPCTSTR lpText);
	int warn(LPCTSTR lpText);

};

