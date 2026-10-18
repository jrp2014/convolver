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
//$Workfile: convolverVST.cpp $ 
//$Revision: 32 $
//$Date: 10/07/06 23:14 $
//$Modtime: 10/07/06 23:14 $
//

#include <stdio.h>
#include <string.h>

#include ".\convolvervst.h"

static VstParameterProperties ParameterProperties[kNumParams] =
{
	{
		/* stepFloat */ 1.0f,
			/* smallStepFloat */ 0.1f,
			/* largeStepFloat */ 10.0f,
			/* label */ "Attenuation",
			/* flags */ kVstParameterUsesFloatStep | kVstParameterCanRamp,
			/* minInteger */ 0,
			/* maxInteger */ 0,
			/* stepInteger */ 0,
			/* largeStepInteger */	0,
			/* shortLabel */ "Attn'n",
			/* displayIndex */ kAttenuationdB,
			/* category */ 0,
			/* numParametersInCategory */ 0,
			/* reserved */ 0,
			/* categoryLabel */ "None",
			/* future */ { 0 }
	},
	{
		/* stepFloat */ 0,
			/* smallStepFloat */ 0,
			/* largeStepFloat */ 0,
			/* label */ "Partitions",
			/* flags */ kVstParameterUsesIntStep | kVstParameterUsesIntegerMinMax,
			/* minInteger */ 0,
			/* maxInteger */ 16,
			/* stepInteger */ 1,
			/* largeStepInteger */	2,
			/* shortLabel */ "Partns",
			/* displayIndex */ kPartitions,
			/* category */ 0,
			/* numParametersInCategory */ 0,
			/* reserved */ 0,
			/* categoryLabel */ "None",
			/* future */ { 0 }
	},
	{
		/* stepFloat */ 0,
			/* smallStepFloat */ 0,
			/* largeStepFloat */ 0,
			/* label */ "Tuning rigour",
			/* flags */ kVstParameterUsesIntStep | kVstParameterUsesIntegerMinMax,
			/* minInteger */ 0,
			/* maxInteger */ PlanningRigour::nDegrees - 1,
			/* stepInteger */ 1,
			/* largeStepInteger */	1,
			/* shortLabel */ "Tuning",
			/* displayIndex */ kPlanningRigour,
			/* category */ 0,
			/* numParametersInCategory */ 0,
			/* reserved */ 0,
			/* categoryLabel */ "None",
			/* future */ { 0 }
	}
};


// TODO: a long list seems to cause crashes
static VstFileType filterTypes[] = 
{
	VstFileType("Wave", ".WAV", "WAV", "wav", "audio/wav", "audio/x-wav"),
	VstFileType("Headerless raw 32-bit float", ".PCM", "PCM", "pcm", "audio/pcm", "audio/x-pcm"),
	VstFileType("Config File", ".TXT", "TXT", "txt", "text/plain", "application/plain"),
	/*VstFileType("SGI/Apple AIFF", "AIFF", "aif", "aiff", "audio/aiff", "audio/x-aiff"),
	VstFileType("SGI/Apple AIFC", "AIFC", "aif", "aifc", "audio/x-aifc"),
	VstFileType("Paris Audio File", ".PAF", "PAF", "paf", "audio/paf", "audio/x-paf"),
	VstFileType("Amiga IFF", ".IFF", "IFF", "iff", "audio/iff", "audio/x-iff"),
	VstFileType("Amiga SVX", ".SVX", "SVX", "svx", "audio/svx", "audio/x-svx"),
	VstFileType("IRCAM SF", ".SF", "SF", "sf", "audio/sf", "audio/x-sf"),
	VstFileType("Creative VOC", ".VOC", "VOC", "voc", "audio/voc", "audio/x-voc"),
	VstFileType("Soundforge W64", ".W64", "W64", "w64", "audio/w64", "audio/x-w64"),
	VstFileType("GNU Octave MAT4/5", ".MAT", "MAT", "mat", "audio/mat", "audio/x-mat"),
	VstFileType("Portable Voice Format PVF", ".PVF", "PVF", "pvf", "audio/pvf", "audio/x-pvf"),
	VstFileType("Fasttracker 2 XI", ".XI", "XI", "xi", "audio/xi", "audio/x-xi"),
	VstFileType("HMM Tool Kit HTK", ".HTK", "HTK", "htk", "audio/htk", "audio/x-htk"),
	VstFileType("Apple CAF", ".CAF", "CAF", "caf", "audio/caf", "audio/x-caf"),
	VstFileType("Sun/Dec/NeXT AU", ".AU", "AU", "audio/au", "audio/x-au"),
	VstFileType("Sun/Dec/NeXT SND", ".SND", "SND", "audio/snd", "audio/x-snd")*/
};

const char ConvolverVSTProgram::defaultname[] = "Default";


void ConvolverVST::abort(const TCHAR* lpText)
{
	MessageBox( GetForegroundWindow(), lpText,
		TEXT("convolverVST error"), MB_SYSTEMMODAL|MB_ICONERROR);

	m_bAborted = true;
}

int ConvolverVST::warn(const TCHAR* lpText)
{
	const int reply = MessageBox( GetForegroundWindow(), lpText,
		TEXT("convolverVST warning"), MB_SYSTEMMODAL|MB_ICONWARNING);

	return reply;
}


//----------------------------------------------------------------------------- 
// default Program Values
ConvolverVSTProgram::ConvolverVSTProgram () : 
fAttenuation_db(0),
nPartitions(0),
nPlanningRigour(PlanningRigour::Measure)
{
	vst_strncpy (name, defaultname, kVstMaxProgNameLen);
}

//-----------------------------------------------------------------------------
ConvolverVST::ConvolverVST (audioMasterCallback audioMaster)
: AudioEffectX (audioMaster, kNumPrograms, kNumParams),
m_bAborted(false),
m_fAttenuation_db(0),
m_nPartitions(0),
m_nPlanningRigour(PlanningRigour::Measure),
m_Convolution(NULL),
m_ConvertAddfloat(new ConvertAddSample_ieeefloat<BaseT>()),
m_Convertfloat(new ConvertSample_ieeefloat<BaseT>()),
m_Convertdouble(new ConvertSample_ieeedouble<BaseT>()),
m_programs(NULL),
m_plugInput(NULL),
m_plugOutput(NULL),
m_MasterVersion(0), 
m_HostVendorVersion(0),
m_HostLanguage(kVstLangEnglish)
{
	// init

	m_szFilterFileName[0] = 0;
	m_HostVendorString[0] = 0; 
	m_HostProductString[0] = 0;

	m_programs = new ConvolverVSTProgram[kNumPrograms];

	if (m_programs)
		setProgram (0);

	setEditor(new ConvolverVSTEditor(this));

#if defined(DEBUG) | defined(_DEBUG)
	// 3 = function call trace
	apDebug::gOnly().set(3);

	DEBUGGING(3, cdebug << "ConvolverVST" << std::endl;);

	//const int	mode =   (1 * _CRTDBG_MODE_DEBUG) | (1 * _CRTDBG_MODE_WNDW);
	//::_CrtSetReportMode (_CRT_WARN, mode);
	//::_CrtSetReportMode (_CRT_ERROR, mode);
	//::_CrtSetReportMode (_CRT_ASSERT, mode);

	//const int	old_flags = ::_CrtSetDbgFlag (_CRTDBG_REPORT_FLAG);
	//::_CrtSetDbgFlag (  old_flags
	//	| (1 * _CRTDBG_LEAK_CHECK_DF)
	//	| (1 * _CRTDBG_CHECK_ALWAYS_DF)
	//	| (1 * _CRTDBG_ALLOC_MEM_DF));
	////::_CrtSetBreakAlloc (-1);	// Specify here a memory bloc number

	debugstream.sink (apDebugSinkWindows::sOnly);
	apDebugSinkWindows::sOnly.showHeader (true);
#endif

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
			m_nPartitions = dwValue;
		}

		// Read the planning rigour
		lResult = key.QueryDWORDValue(kszPrefsPlanningRigour, dwValue);
		if (ERROR_SUCCESS == lResult)
		{
			m_nPlanningRigour = static_cast<PlanningRigour::Degree>(dwValue);
			// limit planning rigour to avoid long startup times.
			if (m_nPlanningRigour > PlanningRigour::Measure)
			{
				m_nPlanningRigour = PlanningRigour::Measure;
			}
		}
	}
	else
	{
		// First time through -- no registry setting saved
	}

	try // creating m_Convolution. Might throw
	{
		if(m_szFilterFileName[0] == 0)
		{
			warn("No filter or config file selected");
		}
		else
		{
			m_Convolution.set_ptr(new Convolution<BaseT>(m_szFilterFileName,
				m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour)); // 0 partitions = overlap-save
		}
	}
	catch(std::exception& err)
	{
		warn(err.what());
	}
	catch (...) 
	{
		abort("Failed to open filter / config file.  Please report to jrp@dial.pipex.com");
	}

	if(m_Convolution.get_ptr() != NULL)
	{
		// TODO: Are we mixing up channels and inputs and pins?
		setNumInputs (m_Convolution->Mixer.nInputChannels());
		setNumOutputs (m_Convolution->Mixer.nOutputChannels());
		sampleRate = m_Convolution->Mixer.nSamplesPerSec();
		setInitialDelay(m_Convolution->Mixer.nHalfPartitionLength());
	}
	else
	{
		m_bAborted = true;
		// Default to stereo
		setNumInputs (2);
		setNumOutputs (2);
	}

	allocateArrangement(&m_plugInput, cEffect.numInputs);
	m_plugInput->type = kSpeakerArrMono;
	for(int i = 0; i < (cEffect.numInputs < 8 ? cEffect.numInputs : 8) ; ++i)
		m_plugInput->speakers[i].type = kSpeakerM;

	allocateArrangement(&m_plugOutput, cEffect.numOutputs);
	m_plugOutput->type = kSpeakerArrMono;
	for(int i = 0; i < (cEffect.numOutputs < 8 ? cEffect.numOutputs : 8); ++i)
		m_plugOutput->speakers[i].type = kSpeakerM;

	isSynth(false);
	noTail(true);  // Need input to generate output
	programsAreChunks(false);

	canProcessReplacing ( true );
	canDoubleReplacing  ( true );

	setUniqueID(CCONST ('C', 'o', 'n', 'v'));
}

//------------------------------------------------------------------------
ConvolverVST::~ConvolverVST ()
{
	// update the registry
	CRegKey key;
	if (ERROR_SUCCESS != key.Create(HKEY_CURRENT_USER, kszPrefsRegKey))
	{
		warn(TEXT("Failed to create registry key for saving settings"));
	}
	else
	{
		// Write the attenuation value to the registry.
		if (ERROR_SUCCESS != key.SetDWORDValue( kszPrefsAttenuation, encode_Attenuationdb(m_fAttenuation_db)))
			warn(TEXT("Failed to save attenuation to registry.") );

		// Write the filter filename to the registry.
		if (ERROR_SUCCESS != key.SetStringValue( kszPrefsFilterFileName, m_szFilterFileName ))
			warn(TEXT("Failed to save filename to registry.") );

		// Write the number of partitions to the registry.
		if (ERROR_SUCCESS != key.SetDWORDValue( kszPrefsPartitions, m_nPartitions ))
			warn(TEXT("Failed to save number of partitions to registry") );

		// Write the planning rigour to the registry.
		if (ERROR_SUCCESS != key.SetDWORDValue( kszPrefsPlanningRigour, m_nPlanningRigour ))
			warn(TEXT("Failed to save tuning rigour to registry") );
	}

	// the editor gets deleted by the AudioEffect base class
	delete [] m_programs; 
	m_programs = NULL;

	deallocateArrangement (&m_plugInput);
	deallocateArrangement (&m_plugOutput);
}

void ConvolverVST::open()
{
	AudioEffect::open();

	m_MasterVersion = getMasterVersion (); //   the Host's version (for example 2400 for VST 2.4). 

	//  get a string identifying the vendor. 
	if(!getHostVendorString (m_HostVendorString))
		m_HostVendorString[0] = 0;

	// get a string with product name. 
	if(!getHostProductString (m_HostProductString))
		m_HostProductString[0] = 0;

	// Returns vendor-specific version (for example 3200 for Nuendo 3.2).
	m_HostVendorVersion = getHostVendorVersion ();

	// Returns the Host's language (VstHostLanguage).
	m_HostLanguage = static_cast<VstHostLanguage>(getHostLanguage());
}

//------------------------------------------------------------------------
void ConvolverVST::SelectFilter()
{
	const Mutex_Guard<Thread_Mutex> lock(m_Convolution.lock);

	// Load the filter that we are going to use
	VstFileSelect vstFileSelect;
	memset (&vstFileSelect, 0, sizeof (vstFileSelect));
	vstFileSelect.command = kVstFileLoad;
	vstFileSelect.type = kVstFileType;
	vst_strncpy (vstFileSelect.title, "Select filter (sound or config file)",1024);
	vstFileSelect.nbFileTypes = (sizeof(filterTypes)/sizeof((filterTypes)[0]));
	vstFileSelect.fileTypes = filterTypes;
	vstFileSelect.returnPath = m_szFilterFileName;
	//vstFileSelect.initialPath = new char[1024];
	vstFileSelect.initialPath = NULL;

	CFileSelector fs(this);
	if(fs.run(&vstFileSelect) == 1)
	{
		try // creating m_Convolution. Might throw
		{
			m_Convolution.set_ptr(new Convolution<BaseT>(m_szFilterFileName,
				m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour)); // 0 partitions = overlap-save
		}
		catch(std::exception& err)
		{
			warn(err.what());
		}
		catch(...)
		{
			warn(TEXT("Unexpected problem loading filter.  Please report to jrp@dial.pipex.com"));
		}

		if(m_Convolution.get_ptr() != NULL)
		{

			setInitialDelay(m_Convolution->Mixer.nHalfPartitionLength());

			if(editor->isOpen())
			{
				static_cast<ConvolverVSTEditor*>(editor)->setFilterFilename(m_szFilterFileName);

				static_cast<ConvolverVSTEditor*>(editor)->setStatus(m_Convolution->Mixer.DisplayChannelPaths().c_str());
			}

			if(m_Convolution->Mixer.nInputChannels() != cEffect.numInputs)
			{
				std::basic_ostringstream<TCHAR> diagnostic;
				diagnostic << "Number of input channels changed from " <<
					cEffect.numInputs << " to " << m_Convolution->Mixer.nInputChannels() << ". Reload ConvolverVST." << std::ends;
				abort(diagnostic.str().c_str());
			}
			if(m_Convolution->Mixer.nOutputChannels() != cEffect.numOutputs)
			{
				std::basic_ostringstream<TCHAR> diagnostic;
				diagnostic << "Number of output channels changed from " << 
					cEffect.numOutputs << " to " << m_Convolution->Mixer.nOutputChannels() << ". Reload ConvolverVST." << std::ends;
				abort(diagnostic.str().c_str());
			}
			if(m_Convolution->Mixer.nSamplesPerSec() != this->sampleRate)
			{
				std::basic_ostringstream<TCHAR> diagnostic;
				diagnostic << "Filter sample rate changed from " << 
					sampleRate << "Hz to " << m_Convolution->Mixer.nSamplesPerSec() << "Hz. Reload ConvolverVST." << std::ends;
				warn(diagnostic.str().c_str());
			}
		}
	}
	//else
	//{
	//	warn(TEXT("No filter selected"));
	//}
}

//
void ConvolverVST::setSampleRate  (  float  sampleRate   )
{
	this->sampleRate = sampleRate;

	const Mutex_Guard<Thread_Mutex> lock(m_Convolution.lock);
	if(m_Convolution.get_ptr() != NULL)
	{
		if(m_Convolution->Mixer.nSamplesPerSec() != sampleRate)
		{
			std::basic_ostringstream<TCHAR> diagnostic;
			diagnostic << "Filter sample rate (" << m_Convolution->Mixer.nSamplesPerSec() <<
				sampleRate << "Hz) different from input sample rate (" << sampleRate << "Hz)" << std::ends;
			warn(diagnostic.str().c_str());
		}
	}
}
//------------------------------------------------------------------------
VstInt32 ConvolverVST::getGetTailSize()
{
	if (m_Convolution.get_ptr() != NULL)
		return m_Convolution->Mixer.nHalfPartitionLength();
	else
		return 0;
}

//------------------------------------------------------------------------
//void ConvolverVST::resume()
//{
//	AudioEffectX::resume();
//}

//------------------------------------------------------------------------
bool ConvolverVST::getInputProperties (VstInt32 index, VstPinProperties* properties)
{
	if(m_Convolution.get_ptr() == NULL)
		return false;

	if (index >= 0 && index < m_Convolution->Mixer.nInputChannels())
	{
		sprintf (properties->label, "Input pin %1d", index + 1); // kVstMaxLabelLen
		sprintf (properties->shortLabel, "In %1d", index + 1); // kVstMaxShortLabelLen
		properties->flags = kVstPinIsActive | kVstPinUseSpeaker;
		properties->arrangementType = kSpeakerArrMono;
		return true;
	}
	return false;
}

//------------------------------------------------------------------------
bool ConvolverVST::getOutputProperties (VstInt32 index, VstPinProperties* properties)
{
	if(m_Convolution.get_ptr() == NULL)
		return false;

	if (index >= 0 && index < m_Convolution->Mixer.nOutputChannels())
	{
		sprintf (properties->label, "Output pin %1d", index + 1); // kVstMaxLabelLen
		sprintf (properties->shortLabel, "Out %1d", index + 1); // kVstMaxShortLabelLen
		properties->flags = kVstPinIsActive | kVstPinUseSpeaker;
		properties->arrangementType = kSpeakerArrMono;
		return true;
	}
	return false;
}


//------------------------------------------------------------------------
void ConvolverVST::setProgram (VstInt32 program)
{
	ConvolverVSTProgram* ap = &m_programs[program];

	m_fAttenuation_db = ap->fAttenuation_db;
	m_nPartitions = ap->nPartitions;
	m_nPlanningRigour = ap->nPlanningRigour;

	curProgram = program;
}

//------------------------------------------------------------------------
void ConvolverVST::setProgramName (char *name)
{
	vst_strncpy (m_programs[curProgram].name, name, kVstMaxProgNameLen);
}

//------------------------------------------------------------------------
void ConvolverVST::getProgramName (char *name)
{
	if (!strcmp (m_programs[curProgram].name, m_programs[curProgram].defaultname))
		sprintf (name, "%s %d", m_programs[curProgram].name, curProgram + 1);
	else
		vst_strncpy (name, m_programs[curProgram].name, kVstMaxProgNameLen);
}

//-----------------------------------------------------------------------------------------
bool ConvolverVST::getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text)
{
	if (index >= 0 && index < kNumPrograms)
	{
		vst_strncpy(text, m_programs[index].name, kVstMaxProgNameLen);
		return true;
	}
	return false;
}


//------------------------------------------------------------------------
void ConvolverVST::setParameter (VstInt32 index, float value)
{
	ConvolverVSTProgram* ap = &m_programs[curProgram];

	switch (index)
	{
	case kAttenuationdB:
		m_fAttenuation_db = ap->fAttenuation_db = float2dB(value); 
		break;
	case kPartitions:
		{

			const DWORD newvalue = float2int(index, value);

			if(m_nPartitions != newvalue)
			{
				m_nPartitions = ap->nPartitions = newvalue;
				assert(m_nPartitions <= ParameterProperties[index].maxInteger);
				assert(m_nPartitions >= ParameterProperties[index].minInteger);

				try
				{
					const Mutex_Guard<Thread_Mutex> lock(m_Convolution.lock);
					if(m_Convolution.get_ptr() != NULL)
					{
						m_Convolution.set_ptr(new Convolution<BaseT>(
							m_szFilterFileName,	m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour));

						setInitialDelay(m_Convolution->Mixer.nHalfPartitionLength());

						if(editor->isOpen())
						{

							static_cast<ConvolverVSTEditor*>(editor)->setStatus(m_Convolution->Mixer.DisplayChannelPaths().c_str());
						}
					}
				}
				catch(std::exception& err)
				{
					abort(err.what());
				}
				catch(...)
				{
					abort(TEXT("Unexpected problem loading filter. Restart ConvolverVST."));
				}
			}
		}
		break;
	case kPlanningRigour:
		{
			const PlanningRigour::Degree newvalue = static_cast<PlanningRigour::Degree>(float2int(index, value));

			if(m_nPlanningRigour != newvalue)
			{
				m_nPlanningRigour = ap->nPlanningRigour = newvalue;
				assert(m_nPlanningRigour <= ParameterProperties[index].maxInteger);
				assert(m_nPlanningRigour >= ParameterProperties[index].minInteger);

				try
				{
					const Mutex_Guard<Thread_Mutex> lock(m_Convolution.lock);
					if(m_Convolution.get_ptr() != NULL)
					{
						m_Convolution.set_ptr(new Convolution<BaseT>(
							m_szFilterFileName,	m_nPartitions == 0 ? 1 : m_nPartitions, m_nPlanningRigour));
					}
				}
				catch(std::exception& err)
				{
					abort(err.what());
				}
				catch(...)
				{
					abort(TEXT("Unexpected problem loading filter. Restart ConvolverVST."));
				}
			}
		}
		break;
	default:
		assert(false);
	}

	if(editor)
		static_cast<AEffGUIEditor*>(editor)->setParameter(index, value);
		//((AEffGUIEditor*)editor)->setParameter(index, value);
}

//-----------------------------------------------------------------------------------------
bool ConvolverVST::string2parameter (VstInt32 index, char* text)
{

	ConvolverVSTProgram* ap = &m_programs[curProgram];

	switch (index)
	{
	case kAttenuationdB:
		{
			if(text == NULL)
				return true;
			const float result = atof(text);
			if(result > MAX_ATTENUATION_DB_VST || result < -MAX_ATTENUATION_DB_VST)
				return false;
			m_fAttenuation_db = ap->fAttenuation_db = result;
		}
		break;
	case kPartitions:
		{
			if(text == NULL)
				return true;
			const int result = atoi(text);
			assert(ParameterProperties[index].flags & kVstParameterUsesIntegerMinMax);
			if(result < ParameterProperties[index].minInteger || result > ParameterProperties[index].maxInteger)
				return false;
			m_nPartitions = ap->nPartitions = result;
			assert(m_nPartitions <= ParameterProperties[index].maxInteger);
			assert(m_nPartitions >= ParameterProperties[index].minInteger);
		}
		break;
	case kPlanningRigour:
		{
			if(text == NULL)
				return true;
			const int result = atoi(text);
			assert(ParameterProperties[index].flags & kVstParameterUsesIntegerMinMax);
			if(result < ParameterProperties[index].minInteger || result > ParameterProperties[index].maxInteger)
				return false;
			m_nPlanningRigour = ap->nPlanningRigour = static_cast<PlanningRigour::Degree>(result);
			assert(m_nPlanningRigour <= ParameterProperties[index].maxInteger);
			assert(m_nPlanningRigour >= ParameterProperties[index].minInteger);
		}
		break;
	default:
		assert(false);
	}

	return false;
}


void ConvolverVST::setFilterFilename(TCHAR szFilterFileName[MAX_PATH])
{
	vst_strncpy (m_szFilterFileName, szFilterFileName, MAX_PATH);
}


//------------------------------------------------------------------------
float ConvolverVST::getParameter (VstInt32 index)
{
	float v = 0;

	switch (index)
	{
	case kAttenuationdB:
		v = db201float(m_fAttenuation_db);
		break;
	case kPartitions: 
		v = int201float(index, m_nPartitions);
		break;
	case kPlanningRigour: 
		v = int201float(index, m_nPlanningRigour); 
		break;
	default:
		assert(false);
	}

	assert(v >= 0);
	assert(v <= 1.0);
	return v;
}


//------------------------------------------------------------------------
bool ConvolverVST::getParameterProperties  (VstInt32 index, VstParameterProperties *  p)
{
	if(index < 0 || index >= kNumParams)
		return false;

	::ZeroMemory(p, sizeof(VstParameterProperties));

	p->stepFloat = ParameterProperties[index].stepFloat;
	p->smallStepFloat = ParameterProperties[index].smallStepFloat;
	p->largeStepFloat = ParameterProperties[index].largeStepFloat;
	vst_strncpy(p->label, ParameterProperties[index].label, kVstMaxLabelLen);
	p->flags = ParameterProperties[index].flags;
	p->minInteger = ParameterProperties[index].minInteger;
	p->maxInteger = ParameterProperties[index].maxInteger;
	p->stepInteger = ParameterProperties[index].stepInteger;
	p->largeStepInteger = ParameterProperties[index].largeStepInteger;
	vst_strncpy(p->shortLabel, ParameterProperties[index].shortLabel, kVstMaxShortLabelLen);
	p->displayIndex = ParameterProperties[index].displayIndex;
	p->category = ParameterProperties[index].category;
	p->numParametersInCategory = ParameterProperties[index].numParametersInCategory;
	p->largeStepInteger = ParameterProperties[index].largeStepInteger;
	p->reserved = ParameterProperties[index].reserved;
	vst_strncpy(p->shortLabel, ParameterProperties[index].shortLabel, kVstMaxCategLabelLen);
	vst_strncpy(p->future, ParameterProperties[index].future, 16);

	return true;
}

//------------------------------------------------------------------------
void ConvolverVST::getParameterName (VstInt32 index, char *label)
{
	if(index >= 0 && index < kNumParams)
	{
		vst_strncpy(label, ParameterProperties[index].shortLabel, kVstMaxParamStrLen);
	}
	else
	{
		vst_strncpy(label, "Invalid parameter", kVstMaxParamStrLen);
	}
}

//------------------------------------------------------------------------
void ConvolverVST::getParameterDisplay (VstInt32 index, char *text)
{
	switch (index)
	{
	case kAttenuationdB:
		float2string (m_fAttenuation_db, text, kVstMaxParamStrLen); 
		break;
	case kPartitions:
		{
			assert(m_nPartitions <= ParameterProperties[index].maxInteger);
			assert(m_nPartitions >= ParameterProperties[index].minInteger);
			int2string (m_nPartitions, text, kVstMaxParamStrLen);
		}
		break;
	case kPlanningRigour: 
		{
			assert(m_nPlanningRigour <= ParameterProperties[index].maxInteger);
			assert(m_nPlanningRigour >= ParameterProperties[index].minInteger);
			vst_strncpy (text, PlanningRigour::Rigour[m_nPlanningRigour], kVstMaxParamStrLen); 
		}
		break;
	default:
		assert(false);
	}
}

//------------------------------------------------------------------------
void ConvolverVST::getParameterLabel (VstInt32 index, char *label)
{
	switch (index)
	{
	case kAttenuationdB:
		vst_strncpy (label, "dB", kVstMaxLabelLen); 
		break;
	case kPartitions:
		if(m_nPartitions == 0)
		{
			vst_strncpy (label, "no partitions", kVstMaxLabelLen);
		}
		else if(m_nPartitions == 1)
		{
			vst_strncpy (label, " partition", kVstMaxLabelLen);
		}
		else
		{
			vst_strncpy (label, " partitions", kVstMaxLabelLen);
		}
		break;
	case kPlanningRigour: 
		vst_strncpy (label, "", kVstMaxLabelLen); 
		break;
	default:
		assert(false);
	}
}

//------------------------------------------------------------------------
bool ConvolverVST::canParameterBeAutomated  (  VstInt32  index   ) 
{
	return index == kAttenuationdB;
}

//------------------------------------------------------------------------
bool ConvolverVST::getEffectName (char* name)
{
	vst_strncpy (name, "ConvolverVST", 32);
	return true;
}

//------------------------------------------------------------------------
bool ConvolverVST::getProductString (char* text)
{
	vst_strncpy (text, "ConvolverVST", 64);
	return true;
}

//------------------------------------------------------------------------
bool ConvolverVST::getVendorString (char* text)
{
	vst_strncpy (text, "convolver.sf.net", 64);
	return true;
}

//------------------------------------------------------------------------
VstInt32 ConvolverVST::canDo  (char*  text)
{
	const VstInt32 dunno =  0; // don't know (default)
	const VstInt32 yes =  1;
	const VstInt32 no =  -1;

	// plug-in supports function setBypass ()?
	if(stricmp(text,  "Bypass") == 0)
		return no;

	// plug-in supports function getMidiProgramName ()?
	if(stricmp(text, "MidiProgramNames") == 0)
#if VST_2_1_EXTENSIONS
		return no;
#else
		return no;
#endif

	// TODO:: will be able to 
	// plug-in supports offline functions (offlineNotify, offlinePrepare, offlineRun)?
	if(stricmp(text, "Offline") == 0)
		return no;

	// plug-in can receive VST events from Host?
	if(stricmp(text, "ReceiveVstEvents") == 0)
		return no;

	//  plug-in can receive MIDI events from Host?
	if(stricmp(text, "ReceiveVstMidiEvent") == 0)
		return no;

	// plug-in can receive Time info from Host?
	if(stricmp(text, "ReceiveVstTimeInfo") == 0)
		return no;

	// plug-in will send Vst events to Host?
	if(stricmp(text, "SendVstEvents") == 0)
		return no;

	// plug-in will send MIDI events to Host?
	if(stricmp(text, "SendVstMidiEvent") == 0)
		return no;

	if(stricmp(text, "sendVstTimeInfo") == 0)
		return no;

	if(stricmp(text, "plugAsChannelInsert") == 0)
		return no;

	if(stricmp(text, "plugAsSend") == 0)
		return no;

	if(stricmp(text, "mixDryWet") == 0)
		return no;

	if(stricmp(text, "noRealTime") == 0)
		return no;

	if(stricmp(text, "multipass") == 0)
		return no;

	if(stricmp(text, "metapass") == 0)
		return no;

	// mac: doesn't mess with grafport. general: may want
	// to call sizeWindow (). if you want to use sizeWindow (),
	// you must return true (1) in canDo ("conformsToWindowRules")
	if(stricmp(text, "conformsToWindowRules") == 0)
#if VST_2_1_EXTENSIONS
		return yes;
#else
		return no;
#endif

	if(stricmp(text, "bypass") == 0)
#if VST_2_3_EXTENSIONS
		return no;
#else
		return no;
#endif

#ifdef UNDEF
	const Mutex_Guard<Thread_Mutex> lock(m_Convolution.lock);

	if(m_Convolution.get_ptr() != NULL)
	{
		// These seem to have gone from 2.4
		if(stricmp(text, "1in1out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 1 && 
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 1 ? yes : no; 

		if(stricmp(text, "1in2out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 1 && 
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 2 ? yes : no; 

		if(stricmp(text, "2in1out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 2 && 
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 1 ? yes : no;  

		if(stricmp(text, "2in2out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 2 
			&&  m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 2 ? yes : no;

		if(stricmp(text, "2in4out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 2 && 
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 4 ? yes : no;

		if(stricmp(text, "4in2out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 4 && 
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 2 ? yes : no;

		if(stricmp(text, "4in4out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 4 &&  
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 4 ? yes : no;

		// 4:2 matrix to surround bus
		if(stricmp(text, "4in8out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 4 &&  
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 8 ? yes : no;

		// surround bus to 4:2 matrix
		if(stricmp(text, "8in4out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 8 &&
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 4 ? yes : no;

		if(stricmp(text, "8in8out") == 0)
			return m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 8 && 
			m_Convolution->MixerSelectedConvolution().Mixer.nInputChannels() == 8 ? yes : no;
	}
#endif

	return dunno;
}
//------------------------------------------------------------------------
bool ConvolverVST::getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput)
{
	*pluginInput  = m_plugInput;
	*pluginOutput = m_plugOutput;
	return true;
}

//------------------------------------------------------------------------
bool ConvolverVST::setSpeakerArrangement (VstSpeakerArrangement* pluginInput,
										  VstSpeakerArrangement* pluginOutput)
{
	if (!pluginOutput || !pluginInput || m_Convolution.get_ptr() == NULL)
		return false;

	bool result = true;

	// This plug-in can act on any speaker arrangement
	if (pluginInput->numChannels > m_Convolution->Mixer.nInputChannels())
	{
		// This plug-in can't have so many channels. So we answer
		// false, and we set the input arrangement with the maximum
		// number of channels possible
		result = false;

		assert(m_Convolution->Mixer.nInputChannels() <= 8);
		allocateArrangement(&m_plugInput, m_Convolution->Mixer.nInputChannels());
		m_plugInput->type = kSpeakerArrMono;

		for(int i = 0; i < m_Convolution->Mixer.nInputChannels(); ++i)
			m_plugInput->speakers[i].type = kSpeakerM;
	}
	else
	{
		matchArrangement (&m_plugInput, pluginInput);
	}

	if (pluginOutput->numChannels > m_Convolution->Mixer.nOutputChannels())
	{
		// This plug-in can't have so many channels. So we answer
		// false, and we set the input arrangement with the maximum
		// number of channels possible
		result = false;

		assert(m_Convolution->Mixer.nOutputChannels() <= 8);
		allocateArrangement(&m_plugOutput, m_Convolution->Mixer.nOutputChannels());
		m_plugOutput->type = kSpeakerArrMono;

		for(int i = 0; i < m_Convolution->Mixer.nOutputChannels(); ++i)
			m_plugOutput->speakers[i].type = kSpeakerM;
	}
	else
	{
		matchArrangement (&m_plugOutput, pluginOutput);
	}

	return result;
}

template<typename T>
void ConvolverVST::processBase (T **inputs, T **outputs, VstInt32 sampleFrames,
								const ConvertSample<BaseT> * InputConvertor,
								const ConvertSample<BaseT> * OutputConvertor)
{
	const Mutex_Guard<Thread_Mutex> lock(m_Convolution.lock);
	if(m_Convolution.get_ptr() != NULL)
	{
		if(m_bAborted)
			return;

		VSTIO<T, BaseT> convertor(inputs, outputs,
			InputConvertor, OutputConvertor);

		// m_nPartitions == 0 => use overlap-save version		
		DWORD cbOutputBytesGenerated = m_nPartitions == 0 ?
			m_Convolution->doConvolution(convertor,
			sampleFrames, m_fAttenuation_db)
			: m_Convolution->doPartitionedConvolution(convertor, 
			sampleFrames, m_fAttenuation_db);
	}
	else
	{
		// Cubase can send a signal before 
		//abort(TEXT("Lost filter. Please report to jrp@dial.pipex.com"));
	}
}

// TODO: check what happens when inputs == outputs
//---------------------------------------------------------------------------
void ConvolverVST::process (float** inputs, float** outputs, VstInt32 sampleFrames)
{
	processBase<float>(inputs, outputs, sampleFrames, m_ConvertAddfloat.get_ptr(), m_ConvertAddfloat.get_ptr());	
}

void ConvolverVST::processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames)
{
	processBase<float>(inputs, outputs, sampleFrames, m_Convertfloat.get_ptr(), m_Convertfloat.get_ptr());	
}

void ConvolverVST::processDoubleReplacing (double** inputs, double** outputs, VstInt32 sampleFrames)
{
	processBase<double>(inputs, outputs, sampleFrames, m_Convertdouble.get_ptr(), m_Convertdouble.get_ptr());	
}


// Helper functions


float int201float(const VstInt32 index, const int value)
{
	if(index < 0 || index >= kNumParams)
		return 0;

	assert(ParameterProperties[index].flags & kVstParameterUsesIntegerMinMax);
	assert(value >= ParameterProperties[index].minInteger);
	assert(value <= ParameterProperties[index].maxInteger);

	const float result = static_cast<float>(value - ParameterProperties[index].minInteger) / 
		(ParameterProperties[index].maxInteger - ParameterProperties[index].minInteger);

	assert(result >= 0);
	assert(result <= 1.0f);

	return  result;
}

int float2int(const VstInt32 index, const float value)
{
	if(index < 0 || index >= kNumParams)
		return 0;

	assert(value >= 0);
	assert(value <= 1.0f);

	assert(ParameterProperties[index].flags & kVstParameterUsesIntegerMinMax);

	const int result = ParameterProperties[index].minInteger + value *
		(ParameterProperties[index].maxInteger - ParameterProperties[index].minInteger);

	assert(result >= ParameterProperties[index].minInteger);
	assert(result <= ParameterProperties[index].maxInteger);

	return result;
}

float float2dB(const float value)
{
	// [0,1] -> [-MAX_ATTENUATION_DB_VST, MAX_ATTENUATION_DB_VST]
	const float scaled = -MAX_ATTENUATION_DB_VST + value *  2.0 * MAX_ATTENUATION_DB_VST;

	assert(scaled >= -MAX_ATTENUATION_DB_VST);
	assert(scaled <= MAX_ATTENUATION_DB_VST);

	return  scaled;
}

float db201float(const float value)
{
	//const float fAttenuationFactor = powf(10, value / 20.0f);

	// [-MAX_ATTENUATION_DB_VST, MAX_ATTENUATION_DB_VST] -> [0,1]
	const float scaled = (value + MAX_ATTENUATION_DB_VST) / (2.0 * MAX_ATTENUATION_DB_VST);

	assert(scaled >= 0);
	assert(scaled <= 1.0f);

	return scaled;
}

// For VST GUI
void displayPartitions(float value, char *string)
{
	const VstInt32 nPartitions = float2int(kPartitions, value);

		if(nPartitions == 0)
		{
			vst_strncpy (string, "no partitions", 13);
		}
		else if(nPartitions == 1)
		{
			vst_strncpy (string, "1 partition", 13);
		}
		else
		{
			sprintf(string, "%i partitions", nPartitions);
		}

	
}

void displayAttenuation(float value, char *string)
{
	sprintf(string, "%.1fdB", float2dB(value));
}

// The following pair is needed because DWORD is unsigned
float decode_Attenuationdb(const DWORD dwValue)
{
	assert(dwValue <= (MAX_ATTENUATION_DB_VST + MAX_ATTENUATION_DB_VST) * 100);
	return static_cast<BaseT>(dwValue) / 100.0 - MAX_ATTENUATION_DB_VST;
}
DWORD encode_Attenuationdb(const float fValue)
{	
	assert (fabs(fValue) <= MAX_ATTENUATION_DB_VST);
	return static_cast<DWORD>((fValue + MAX_ATTENUATION_DB_VST) * 100.0);
}