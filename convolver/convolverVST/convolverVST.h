// Convolver: VST plug-in that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005,2006  jrp
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
//$Workfile: convolverVST.h $ 
//$Revision: 32 $
//$Date: 10/07/06 23:14 $
//$Modtime: 10/07/06 23:14 $
//
#pragma once



#include "convolution\config.h"
#include "public.sdk\source\vst2.x\audioeffectx.h"
#include "convolvervsteditor.h"
#include "cfileselector.h"
#include "convolution\convolution.h"

// registry location for preferences
const TCHAR kszPrefsRegKey[] = _T("Software\\Convolver\\VST Plugin");
const TCHAR kszPrefsAttenuation[] = _T("Attenuation");
const TCHAR kszPrefsFilterFileName[] = _T("FilterFileName");
const TCHAR kszPrefsPartitions[] = _T("Partitions");
const TCHAR kszPrefsPlanningRigour[] = _T("PlanningRigour");

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

class ConvolverVSTProgram
{
public:
	ConvolverVSTProgram ();
	~ConvolverVSTProgram () {}

	float fAttenuation_db;
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

	virtual void process (float **inputs, float **outputs, VstInt32 sampleFrames);
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
	//virtual void resume ();	// Called when plug-in is switched to on

	virtual bool getEffectName (char* name);
	virtual bool getVendorString (char* text);
	virtual bool getProductString (char* text);
	virtual VstInt32 getVendorVersion () { return 4300; }

	virtual VstPlugCategory getPlugCategory () { return /* kPlugCategEffect */ kPlugCategRoomFx ; }
	virtual VstInt32 ConvolverVST::canDo  (char*  text);

	virtual bool getInputProperties (VstInt32 index, VstPinProperties* properties);
	virtual bool getOutputProperties (VstInt32 index, VstPinProperties* properties);

	virtual bool canParameterBeAutomated  (  VstInt32  index   );

	virtual bool getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput);
	virtual bool setSpeakerArrangement (VstSpeakerArrangement* pluginInput, VstSpeakerArrangement* pluginOutput);

	virtual void SelectFilter();

	virtual const Holder< Convolution<BaseT>, Thread_Mutex >& getConvolution(){ return m_Convolution; }
	virtual const TCHAR* getFilterFileName ()
	{
		return m_szFilterFileName[0] == 0 ? TEXT("<- Select a filter or config") : m_szFilterFileName;
	}

protected:

	void setFilterFilename(TCHAR m_szFilterFileName[MAX_PATH]);

	template<typename T>
	void processBase (T **inputs, T **outputs, VstInt32 sampleFrames, 
		const ConvertSample<BaseT> * InputConvertor,
		const ConvertSample<BaseT> * OutputConvertor);

	void abort(const TCHAR* lpText);
	int warn(const TCHAR* lpText);

	bool m_bAborted;

	float m_fAttenuation_db;
	DWORD m_nPartitions;
	PlanningRigour::Degree m_nPlanningRigour;

	TCHAR m_szFilterFileName[MAX_PATH];
	Holder< Convolution<BaseT>, Thread_Mutex >	m_Convolution;			// Processing class.
	Holder< ConvertSample<BaseT> > m_ConvertAddfloat;
	Holder< ConvertSample<BaseT> > m_Convertfloat;
	Holder< ConvertSample<BaseT> > m_Convertdouble;

	ConvolverVSTProgram *m_programs;

	VstSpeakerArrangement* m_plugInput;
	VstSpeakerArrangement* m_plugOutput;

	VstInt32  m_MasterVersion;	// The Host's version (for example 2400 for VST 2.4). 

	char m_HostVendorString[64];		// A string identifying the vendor. 
	char m_HostProductString[64];		// product name. 
	VstInt32  m_HostVendorVersion;	// vendor-specific version (for example 3200 for Nuendo 3.2). 
	VstHostLanguage m_HostLanguage;	// the Host's language (VstHostLanguage).  
};


// Helper functions
float int201float(const VstInt32 index, const int value);
int float2int(const VstInt32 index, const float value);
float float2dB(const float value);
float db201float(const float value);

float decode_Attenuationdb(const DWORD dwValue);
DWORD encode_Attenuationdb(const float fValue);

void displayPartitions(float value, char *string);
void displayAttenuation(float value, char *string);