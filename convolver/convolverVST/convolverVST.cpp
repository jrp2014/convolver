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

const char ConvolverVSTProgram::defaultname[kVstMaxProgNameLen+1] = "Default";


void ConvolverVST::abort(LPCTSTR lpText)
{
	MessageBox( GetForegroundWindow(), lpText,
		TEXT("convolverVST error"), MB_SYSTEMMODAL|MB_ICONERROR);

	bAborted = true;
}

int ConvolverVST::warn(LPCTSTR lpText)
{
	const int reply = MessageBox( GetForegroundWindow(), lpText,
		TEXT("convolverVST warning"), MB_SYSTEMMODAL|MB_ICONWARNING|MB_ABORTRETRYIGNORE);

	if(reply == IDABORT)
		bAborted = true;

	return reply;
}


//----------------------------------------------------------------------------- 
// default Program Values
ConvolverVSTProgram::ConvolverVSTProgram () : 
fAttenuationdB(0),
nPartitions(0),
nPlanningRigour(PlanningRigour::Measure)
{
	vst_strncpy (name, defaultname, kVstMaxProgNameLen);
}

//-----------------------------------------------------------------------------
ConvolverVST::ConvolverVST (audioMasterCallback audioMaster)
: AudioEffectX (audioMaster, kNumPrograms, kNumParams),
fAttenuationdB(0),
nPartitions(0),
nPlanningRigour(PlanningRigour::Measure),
plugInput(NULL),
plugOutput(NULL),
bOpened(false),
bSampleRateSet(false),
bAborted(false)
{
	// init

	m_szFilterFileName[0] = 0;
	m_Convertfloat.set_ptr(new ConvertSample_ieeefloat<BaseT>());
	m_Convertdouble.set_ptr(new ConvertSample_ieeedouble<BaseT>());

	programs = new ConvolverVSTProgram[kNumPrograms];

	if (programs)
		setProgram (0);

	//editor = new ConvolverVSTEditor(this);

	// We initialize the arrangements to default values.
	// Nevertheless, the host should modify them via
	// appropriate calls to setSpeakerArrangement.
	allocateArrangement (&plugInput, kMaxInputs);
	plugInput->type = kSpeakerArrMono;

	allocateArrangement (&plugOutput, kMaxOutputs);
	plugOutput->type = kSpeakerArrMono;

	if(audioMaster)
	{
		// TODO: Are we mixing up channels and inputs and pins?
		setNumInputs (kMaxInputs);
		setNumOutputs (kMaxOutputs);

		WaveIn.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		WaveOut.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
		WaveIn.wBitsPerSample  = sizeof(float) / 8;  // Not used.  Host could supply doubles
		WaveOut.wBitsPerSample  = sizeof(float) / 8;

		WaveIn.nChannels = kMaxInputs;
		WaveOut.nChannels = kMaxOutputs;

		WaveIn.cbSize= 0;
		WaveOut.cbSize= 0;

		isSynth(false);
		noTail(true);  // Need input to generate output
		//canMono(true);

		canProcessReplacing ( true );
		canDoubleReplacing  ( true );

		setUniqueID(CCONST ('C', 'o', 'n', 'v'));
	}
	else
	{
		bAborted = true;
	}
}

//------------------------------------------------------------------------
ConvolverVST::~ConvolverVST ()
{
	// the editor gets deleted by the AudioEffect base class
	delete [] programs; 
	programs = NULL;

	deallocateArrangement (&plugInput);
	deallocateArrangement (&plugOutput);
}

//------------------------------------------------------------------------
void ConvolverVST::open()
{
	// Some hosts call setSampleRate first
	if(!bOpened)
	{
		bOpened = true;

		MasterVersion = getMasterVersion (); //   the Host's version (for example 2400 for VST 2.4). 

		//  get a string identifying the vendor. 
		if(!getHostVendorString (HostVendorString))
			HostVendorString[0] = 0;

		// get a string with product name. 
		if(!getHostProductString (HostProductString))
			HostProductString[0] = 0;

		// Returns vendor-specific version (for example 3200 for Nuendo 3.2).
		HostVendorVersion = getHostVendorVersion ();

		// Returns the Host's language (VstHostLanguage).
		HostLanguage = static_cast<VstHostLanguage>(getHostLanguage());

		if(bAborted)
			return;

		// Load the filter that we are going to use
		VstFileSelect vstFileSelect;
		memset (&vstFileSelect, 0, sizeof (vstFileSelect));
		vstFileSelect.command = kVstFileLoad;
		vstFileSelect.type = kVstFileType;
		vst_strncpy (vstFileSelect.title, "Select MONO filter (sound or config file)",1024);
		vstFileSelect.nbFileTypes = (sizeof(filterTypes)/sizeof((filterTypes)[0]));
		vstFileSelect.fileTypes = filterTypes;
		vstFileSelect.returnPath = m_szFilterFileName;
		//vstFileSelect.initialPath = new char[1024];
		vstFileSelect.initialPath = NULL;

		CFileSelector fs(this);
		if(fs.run(&vstFileSelect) == 1)
		{
			const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
			try // creating m_ConvolutionList. Might throw
			{
				m_ConvolutionList.set_ptr(new ConvolutionList<BaseT>(m_szFilterFileName,
					nPartitions == 0 ? 1 : nPartitions, nPlanningRigour)); // 0 partitions = overlap-save
			}
			catch(std::exception& err)
			{
				abort(err.what());
			}
			catch(...)
			{
				abort(TEXT("Unexpected problem loading filter. Restart ConvolverVST."));
			}

			//setSampleRate(sampleRate);	// Selects the right filter from m_ConvolutionList
		}
		else
		{
			abort(TEXT("A filter must be selected. Restart ConvolverVST."));
		}
	}
}

void ConvolverVST::setSampleRate  (  float  sampleRate   )
{

	this->sampleRate = sampleRate;

	bSampleRateSet = true;

	// Some hosts don't call open() first even though they should
	if(!bOpened)
		open();

	if(!bAborted)
	{
		WaveIn.nSamplesPerSec = sampleRate;
		WaveOut.nSamplesPerSec = sampleRate;

		WaveIn.nBlockAlign = WaveIn.nChannels * WaveIn.wBitsPerSample / 8;
		WaveOut.nBlockAlign = WaveOut.nChannels * WaveOut.wBitsPerSample / 8;

		WaveIn.nAvgBytesPerSec = WaveIn.nSamplesPerSec * WaveIn.nBlockAlign;
		WaveOut.nAvgBytesPerSec = WaveOut.nSamplesPerSec * WaveOut.nBlockAlign;

		if(FAILED(m_ConvolutionList->SelectConvolution(&WaveIn, &WaveOut)))
		{
			abort(TEXT("Selected filter has unacceptable sample rate or number of i/o channels. Restart ConvolverVST."));
		}
		else
		{
			setInitialDelay(m_ConvolutionList->SelectedConvolution().Mixer.nHalfPartitionLength());
			//if(!ioChanged())
			//{
			//	abort("Failed to change i/o after setting initial delay.  Please report to jrp@dial.pipex.com");
			//}
		}
	}
}
//------------------------------------------------------------------------
VstInt32 ConvolverVST::getGetTailSize()
{
	if(!bSampleRateSet)
		setSampleRate(sampleRate);

	if (m_ConvolutionList.get_ptr() != NULL && m_ConvolutionList->ConvolutionSelected())
		return m_ConvolutionList->SelectedConvolution().Mixer.nHalfPartitionLength();
	else
		return 0;
}

//------------------------------------------------------------------------
void ConvolverVST::resume()
{
	// Some hosts don't call open() even though they should
	if(!bOpened)
		open();

	if(!bSampleRateSet)
		setSampleRate(sampleRate);

	AudioEffectX::resume();
}

//------------------------------------------------------------------------
bool ConvolverVST::getInputProperties (VstInt32 index, VstPinProperties* properties)
{
	bool returnCode = false;
	if (index >= 0 && index < kMaxInputs)
	{
		sprintf (properties->label, "Input pin %1d", index + 1); // kVstMaxLabelLen
		sprintf (properties->shortLabel, "In %1d", index + 1); // kVstMaxShortLabelLen
		properties->flags = kVstPinIsActive;
		properties->arrangementType = kSpeakerArrMono;
		returnCode = true;
	}
	return returnCode;
}

//------------------------------------------------------------------------
bool ConvolverVST::getOutputProperties (VstInt32 index, VstPinProperties* properties)
{
	bool returnCode = false;
	if (index >= 0 && index < kMaxOutputs)
	{
		properties->flags = kVstPinIsActive;
		if (index == 0) // mono
		{
			vst_strncpy (properties->label, "Mono Out", kVstMaxLabelLen);
			properties->arrangementType = kSpeakerArrMono;
		}
		else if (index == 1) // stereo (1 -> 2)
		{
			vst_strncpy (properties->label, "Stereo Out", kVstMaxLabelLen);
			properties->flags |= kVstPinIsStereo;
			properties->arrangementType = kSpeakerArrStereo;
		}
		else if (index >= 3) // 5.1 (3 -> 8)
		{
			if(MasterVersion >= 2300)
			{
				vst_strncpy (properties->label, "5.1 Out", kVstMaxLabelLen);
				vst_strncpy (properties->shortLabel, "5.1Out", kVstMaxShortLabelLen);
				properties->flags |= kVstPinUseSpeaker;
				properties->arrangementType = kSpeakerArr51;
			}
			else
			{
				// for old VST Host < 2.3, make 5.1 to stereo/mono/mono/stereo (L R C Lfe Ls Rs)
				switch (index)
				{
				case 3: // (3 -> 4)
					{
						properties->flags |= kVstPinIsStereo;
						vst_strncpy (properties->label, "Front", kVstMaxLabelLen);
						vst_strncpy (properties->shortLabel, "Front", kVstMaxShortLabelLen);
					}
					break;
				case 5:
					{
						vst_strncpy (properties->label, "Centre", kVstMaxLabelLen);
						vst_strncpy (properties->shortLabel, "Centre", kVstMaxShortLabelLen);
					}
					break;
				case 6:
					{
						vst_strncpy (properties->label, "LFE", kVstMaxLabelLen);
						vst_strncpy (properties->shortLabel, "LFE", kVstMaxShortLabelLen);
					}
					break;
				case 7: // (7 -> 8)
					{
						properties->flags |= kVstPinIsStereo;
						vst_strncpy (properties->label, "Surround", kVstMaxLabelLen);
						vst_strncpy (properties->shortLabel, "Surrnd", kVstMaxShortLabelLen);
					}
					break;
				}
			}
		}
		returnCode = true;
	}
	return returnCode;
}


//------------------------------------------------------------------------
void ConvolverVST::setProgram (VstInt32 program)
{
	ConvolverVSTProgram* ap = &programs[program];

	fAttenuationdB = ap->fAttenuationdB;
	nPartitions = ap->nPartitions;
	nPlanningRigour = ap->nPlanningRigour;
}

//------------------------------------------------------------------------
void ConvolverVST::setProgramName (char *name)
{
	vst_strncpy (programs[curProgram].name, name, kVstMaxProgNameLen);
}

//------------------------------------------------------------------------
void ConvolverVST::getProgramName (char *name)
{
	if (!strcmp (programs[curProgram].name, programs[curProgram].defaultname))
		sprintf (name, "%s %d", programs[curProgram].name, curProgram + 1);
	else
		vst_strncpy (name, programs[curProgram].name, kVstMaxProgNameLen);
}

//-----------------------------------------------------------------------------------------
bool ConvolverVST::getProgramNameIndexed (VstInt32 category, VstInt32 index, char* text)
{
	if (index >= 0 && index < kNumPrograms)
	{
		vst_strncpy(text, programs[index].name, kVstMaxProgNameLen);
		return true;
	}
	return false;
}


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

	const float fAttenuationFactor = powf(10, value / 20.0f);

	// [-MAX_ATTENUATION_DB_VST, MAX_ATTENUATION_DB_VST] -> [0,1]
	const float scaled = (value + MAX_ATTENUATION_DB_VST) / (2.0 * MAX_ATTENUATION_DB_VST);

	assert(scaled >= 0);
	assert(scaled <= 1.0f);

	return scaled;
}


//------------------------------------------------------------------------
void ConvolverVST::setParameter (VstInt32 index, float value)
{
	ConvolverVSTProgram* ap = &programs[curProgram];

	switch (index)
	{
	case kAttenuationdB:
		fAttenuationdB = ap->fAttenuationdB = float2dB(value); 
		break;
	case kPartitions:
		{

			const DWORD newvalue = float2int(index, value);

			if(nPartitions != newvalue)
			{
				nPartitions = ap->nPartitions = newvalue;
				assert(nPartitions <= ParameterProperties[index].maxInteger);
				assert(nPartitions >= ParameterProperties[index].minInteger);

				try
				{
					const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
					m_ConvolutionList.set_ptr(updateConvolutionList<BaseT>(&WaveIn, &WaveOut,
						m_szFilterFileName,	nPartitions == 0 ? 1 : nPartitions, nPlanningRigour));// May throw
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

			if(nPlanningRigour != newvalue)
			{
				nPlanningRigour = ap->nPlanningRigour = newvalue;
				assert(nPlanningRigour <= ParameterProperties[index].maxInteger);
				assert(nPlanningRigour >= ParameterProperties[index].minInteger);

				try
				{
					const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);
					m_ConvolutionList.set_ptr(updateConvolutionList<BaseT>(&WaveIn, &WaveOut,
						m_szFilterFileName,	nPartitions == 0 ? 1 : nPartitions, nPlanningRigour));// May throw
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
}

//-----------------------------------------------------------------------------------------
bool ConvolverVST::string2parameter (VstInt32 index, char* text)
{

	ConvolverVSTProgram* ap = &programs[curProgram];

	switch (index)
	{
	case kAttenuationdB:
		{
			if(text == NULL)
				return true;
			const float result = atof(text);
			if(result > MAX_ATTENUATION_DB_VST || result < -MAX_ATTENUATION_DB_VST)
				return false;
			fAttenuationdB = ap->fAttenuationdB = result;
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
			nPartitions = ap->nPartitions = result;
			assert(nPartitions <= ParameterProperties[index].maxInteger);
			assert(nPartitions >= ParameterProperties[index].minInteger);
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
			nPlanningRigour = ap->nPlanningRigour = static_cast<PlanningRigour::Degree>(result);
			assert(nPlanningRigour <= ParameterProperties[index].maxInteger);
			assert(nPlanningRigour >= ParameterProperties[index].minInteger);
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
		v = db201float(fAttenuationdB);
		break;
	case kPartitions: 
		v = int201float(index, nPartitions);
		break;
	case kPlanningRigour: 
		v = int201float(index, nPlanningRigour); 
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
	if(index >= 0 || index < kNumParams)
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
		float2string (fAttenuationdB, text, kVstMaxParamStrLen); 
		break;
	case kPartitions:
		{
			assert(nPartitions <= ParameterProperties[index].maxInteger);
			assert(nPartitions >= ParameterProperties[index].minInteger);
			int2string (nPartitions, text, kVstMaxParamStrLen);
		}
		break;
	case kPlanningRigour: 
		{
			assert(nPlanningRigour <= ParameterProperties[index].maxInteger);
			assert(nPlanningRigour >= ParameterProperties[index].minInteger);
			vst_strncpy (text, PlanningRigour::Rigour[nPlanningRigour], kVstMaxParamStrLen); 
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
		strcpy (label, "dB"); 
		break;
	case kPartitions:
		if(nPartitions == 0)
		{
			vst_strncpy (label, "no partitions", kVstMaxLabelLen);
		}
		else if(nPartitions == 1)
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
	const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);

	// These seem to have gone from 2.4
	if(stricmp(text, "1in1out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 1 && 
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 1 ? yes : no; 

	if(stricmp(text, "1in2out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 1 && 
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 2 ? yes : no; 

	if(stricmp(text, "2in1out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 2 && 
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 1 ? yes : no;  

	if(stricmp(text, "2in2out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 2 
		&&  m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 2 ? yes : no;

	if(stricmp(text, "2in4out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 2 && 
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 4 ? yes : no;

	if(stricmp(text, "4in2out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 4 && 
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 2 ? yes : no;

	if(stricmp(text, "4in4out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 4 &&  
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 4 ? yes : no;

	// 4:2 matrix to surround bus
	if(stricmp(text, "4in8out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 4 &&  
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 8 ? yes : no;

	// surround bus to 4:2 matrix
	if(stricmp(text, "8in4out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 8 &&
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 4 ? yes : no;

	if(stricmp(text, "8in8out") == 0)
		return m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 8 && 
		m_ConvolutionList->SelectedConvolution().Mixer.nInputChannels() == 8 ? yes : no;
#endif

	return dunno;
}
//------------------------------------------------------------------------
bool ConvolverVST::getSpeakerArrangement (VstSpeakerArrangement** pluginInput, VstSpeakerArrangement** pluginOutput)
{
	*pluginInput  = plugInput;
	*pluginOutput = plugOutput;
	return true;
}

//------------------------------------------------------------------------
bool ConvolverVST::setSpeakerArrangement (VstSpeakerArrangement* pluginInput,
										  VstSpeakerArrangement* pluginOutput)
{
	if (!pluginOutput || !pluginInput)
		return false;

	bool result = true;

	// This plug-in can act on any speaker arrangement
	if (pluginInput->numChannels > kMaxInputs)
	{
		// This plug-in can't have so many channels. So we answer
		// false, and we set the input arrangement with the maximum
		// number of channels possible
		result = false;
		allocateArrangement (&plugInput, kMaxInputs);
		plugInput->type = kSpeakerArrMono;
	}
	else
	{
		matchArrangement (&plugInput, pluginInput);
	}

	// TODO: fix this when moving to stereo, etc
	if (pluginOutput->numChannels != plugInput->numChannels)
	{
		// This plug-in only deals with symetric IO configurations...
		result = false;
		matchArrangement (&plugOutput, plugInput);
	}
	else
	{
		matchArrangement (&plugOutput, pluginOutput);
	}

	return result;
}


////---------------------------------------------------------------------------
//void ConvolverVST::process (float** inputs, float** outputs, VstInt32 sampleFrames)
//{
//	float* in1  =  inputs[0];
//	float* out1 = outputs[0];
//
//	while (--sampleFrames >= 0)
//	{
//		(*out1++) += (*in1++) * fAttenuationdB;
//	}
//}

// TODO: check what happens when inputs == outputs
//---------------------------------------------------------------------------
void ConvolverVST::processReplacing (float** inputs, float** outputs, VstInt32 sampleFrames)
{
	assert(m_ConvolutionList.get_ptr() != NULL);

	float* in1  =  inputs[0];
	float* out1 = outputs[0];

	// Some hosts don't do the right thing
	if(!bOpened)
		open();

	if(!bSampleRateSet)
		setSampleRate(sampleRate);

	::ZeroMemory(out1, sampleFrames * sizeof(float));

	if(!bAborted)
	{
		const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);

		// nPartitions == 0 => use overlap-save version		
		DWORD cbOutputBytesGenerated = nPartitions == 0 ?
			m_ConvolutionList->SelectedConvolution().doConvolution(reinterpret_cast<BYTE*>(in1), reinterpret_cast<BYTE*>(out1),
			m_Convertfloat.get_ptr(), m_Convertfloat.get_ptr(),
			sampleFrames, fAttenuationdB)
			: m_ConvolutionList->SelectedConvolution().doPartitionedConvolution(reinterpret_cast<BYTE*>(in1), reinterpret_cast<BYTE*>(out1),
			m_Convertfloat.get_ptr(), m_Convertfloat.get_ptr(),
			sampleFrames, fAttenuationdB);
	}
}

void ConvolverVST::processDoubleReplacing (double** inputs, double** outputs, VstInt32 sampleFrames)
{
	assert(m_ConvolutionList.get_ptr() != NULL);

	double* in1  =  inputs[0];
	double* out1 = outputs[0];

	// Some hosts don't do the right thing
	if(!bOpened)
		open();

	if(!bSampleRateSet)
		setSampleRate(sampleRate);

	::ZeroMemory(out1, sampleFrames * sizeof(double));

	if(!bAborted)
	{
		const Mutex_Guard<Thread_Mutex> lock(m_ConvolutionList.lock);

		// nPartitions == 0 => use overlap-save version		
		DWORD cbOutputBytesGenerated = nPartitions == 0 ?
			m_ConvolutionList->SelectedConvolution().doConvolution(reinterpret_cast<BYTE*>(in1), reinterpret_cast<BYTE*>(out1),
			m_Convertdouble.get_ptr(), m_Convertfloat.get_ptr(),
			sampleFrames, fAttenuationdB)
			: m_ConvolutionList->SelectedConvolution().doPartitionedConvolution(reinterpret_cast<BYTE*>(in1), reinterpret_cast<BYTE*>(out1),
			m_Convertdouble.get_ptr(), m_Convertfloat.get_ptr(),
			sampleFrames, fAttenuationdB);
	}
}
