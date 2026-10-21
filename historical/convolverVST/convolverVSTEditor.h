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
//$Workfile: convolverVSTEditor.h $
//$Revision: 30 $
//$Date: 27/06/06 21:58 $
//$Modtime: 24/06/06 12:19 $
//

#ifndef ConvolverVSTEditor_H_
#define ConvolverVSTEditor_H_

#define ENABLE_VST_EXTENSION_IN_VSTGUI 1
//#define VSTGUI_ENABLE_DEPRECATED_METHODS 0

#include <stdio.h>

#include "ConvolverVST.h"
#include "vstgui.h"
#include "ctooltipsupport.h"
#include "resource.h"


#include "aeffguieditor.h"

enum
{
	kSelectFilter = 100
};


class ConvolverVSTEditor : public AEffGUIEditor, public CControlListener
{
  public:
    ConvolverVSTEditor(AudioEffect *effect);
    ~ConvolverVSTEditor();

	void setStatus(const char* txt);
	void setFilterFilename(const char* txt);
	//void setParameter(VstInt32 index, float value);
	void setParameter (long index, float value);

  protected:
    bool open(void *ptr);
    void close();

    long controlModifierClicked(CDrawContext *pContext, CControl *pControl, long button) {return 0;};

	void valueChanged(CControl* control);

    void idle();

    //Controls
    COptionMenu *tuningOptionMenu;
    CKickButton *selectFilterKickButton;
    CHorizontalSlider *attenuationHSlider;
    CTextLabel *filterFilename;
    CTextLabel *statusText;
    CHorizontalSlider *partitionsHSlider;

	CParamDisplay *attenuationDisplay;
	CParamDisplay *partitionsDisplay;

    //Bitmaps
    CBitmap *hBack;
    CBitmap *hHSlider;
    CBitmap *hButton;
	CBitmap *hHSliderButton;
};


enum
{
	kBack = 128
};

#endif
			