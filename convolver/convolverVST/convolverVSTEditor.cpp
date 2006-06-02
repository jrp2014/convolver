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
#include ".\convolvervsteditor.h"

ConvolverVSTEditor::ConvolverVSTEditor(AudioEffect *effect) : AEffGUIEditor(effect)
{
	//rect.left   = 0;
	//rect.top    = 0;
	//rect.right  = 100;
	//rect.bottom = 50;
}

ConvolverVSTEditor::~ConvolverVSTEditor(void)
{
	
}

bool ConvolverVSTEditor::open (void *ptr)
{
	// !!! always call this !!!
	AEffGUIEditor::open (ptr);

	// init the size of the plugin
	//rect.left   = 0;
	//rect.top    = 0;
	//rect.right  = 120;
	//rect.bottom = 120;


	CRect size (rect.left , rect.top, rect.right, rect.bottom);
	CFrame* frame = new CFrame (size, ptr, this);

	// last but not least set the class variable frame to our newly created frame
	//this->frame = frame;


	fs = new CFileSelector(effect);


	VstFileType aiffType ("AIFF File", "AIFF", "aif", "aiff", "audio/aiff", "audio/x-aiff");
	VstFileType aifcType ("AIFC File", "AIFC", "aif", "aifc", "audio/x-aifc");
	VstFileType waveType ("Wave File", ".WAV", "wav", "wav", "audio/wav", "audio/x-wav");
	VstFileType sdIIType ("SoundDesigner II File", "Sd2f", "sd2", "sd2");

	VstFileSelect vstFileSelect;
	memset (&vstFileSelect, 0, sizeof (vstFileSelect));

	vstFileSelect.command = kVstFileLoad;
	vstFileSelect.type = kVstFileType;
	strcpy (vstFileSelect.title, "Test for open file selector");
	vstFileSelect.nbFileTypes = 1;
	vstFileSelect.fileTypes = &waveType;
	vstFileSelect.returnPath = new char[1024];
	//vstFileSelect.initialPath = new char[1024];
	vstFileSelect.initialPath = 0;

	fs->run(&vstFileSelect);

	delete [] vstFileSelect.returnPath;


	return true;
}

//-----------------------------------------------------------------------------
void ConvolverVSTEditor::close ()
{
	delete frame;
	frame = 0;
}

//-----------------------------------------------------------------------------
void ConvolverVSTEditor::setParameter (VstInt32 index, float value)
{
	if (frame == 0)
		return;

	// called from ConvolverVSTEdit
	switch (index)
	{
	}

}


//-----------------------------------------------------------------------------
void ConvolverVSTEditor::valueChanged (VSTGUI::CDrawContext *pContext, VSTGUI::CControl *pControl)
{

	long tag = pControl->getTag ();
	//switch (tag)
	//{
	//case kDelay:
	//case kFeedBack:
	//case kOut:
		effect->setParameterAutomated (tag, pControl->getValue ());
		pControl->setDirty ();
	//	break;
	//}

}
