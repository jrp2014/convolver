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
//
//$Workfile: convolverVSTEditor.cpp $
//$Revision: 30 $
//$Date: 27/06/06 21:58 $
//$Modtime: 24/06/06 12:36 $
//

#include "ConvolverVSTEditor.h"

//--------------------------------
ConvolverVSTEditor::ConvolverVSTEditor(AudioEffect *effect) : AEffGUIEditor(effect),
tuningOptionMenu(NULL),
selectFilterKickButton(NULL),
attenuationHSlider(NULL),
filterFilename(NULL),
statusText(NULL),
partitionsHSlider(NULL),
attenuationDisplay(NULL),
hHSlider(NULL),
hHSliderButton (NULL),
hBack(new CBitmap(IDB_BACKGROUND))
{
	rect.left   = 0;
	rect.top    = 0;
	rect.right  = (short)hBack->getWidth();
	rect.bottom = (short)hBack->getHeight();
}

//--------------------------------
ConvolverVSTEditor::~ConvolverVSTEditor()
{
	if(hBack)
	{
		if(hBack->getNbReference()<=1)
		{
			hBack->forget();
			hBack = NULL;
		}
		else
			hBack->forget();
	}
}

//--------------------------------
bool ConvolverVSTEditor::open(void *ptr)
{
	CRect size;
	CPoint point;
	VstInt32 kStyle = 0;


	AEffGUIEditor::open(ptr);

	//frame_size 100,19
	//handle_rect 104,2,134,19
	//handle_range 0,70
	const VstInt32 HSliderWidth = 100;
	const VstInt32 HSliderMax = 70;
	hHSlider = new CBitmap(IDB_HSLIDER_MED);
	hHSlider->setTransparentColor(kBlackCColor);
	hHSliderButton = new CBitmap(IDB_HSLIDER_MED_BUTTON);
	hHSliderButton->setTransparentColor(kBlackCColor);

	// frame_size 35,18
	hButton = new CBitmap(IDB_BUTTON_SM);
	hButton->setTransparentColor(kBlackCColor);

	size(0, 0, hBack->getWidth(), hBack->getHeight());
	frame = new CFrame(size, ptr, this);
	frame->setBackground(hBack);

	const VstInt32 space = 11;
	const VstInt32 lmarginX = space;


	const VstInt32 firstrowYtop = space;
	const VstInt32 firstrowYbottom = firstrowYtop+hButton->getHeight()/2;
	size(lmarginX, 0, hBack->getWidth(), firstrowYtop);
	CTextLabel* GetFilterText = new CTextLabel(size,TEXT("Filter/config:"), NULL, kStyle);
	GetFilterText->setHoriAlign(kLeftText);
	GetFilterText->setTransparency(true);
	GetFilterText->setFont(kNormalFontSmaller);
	frame->addView(GetFilterText);

	//--selectFilterKickButton--------
	size(lmarginX, firstrowYtop, (lmarginX+hButton->getWidth()), firstrowYbottom);
	point(0, 0);
	selectFilterKickButton = new CKickButton(size, this, kSelectFilter, hButton->getHeight()/2, hButton, point);
	selectFilterKickButton->setTransparency(true);
	frame->addView(selectFilterKickButton);

	//--filterFilename--------
	size(lmarginX + hButton->getWidth(), firstrowYtop,
		hBack->getWidth() - space, firstrowYbottom);
	filterFilename = new CTextLabel(size, static_cast<ConvolverVST*>(effect)->getFilterFileName(), NULL, kStyle);
	filterFilename->setHoriAlign(kPathText);
	filterFilename->setTransparency(true);
	frame->addView(filterFilename);

	//--attenuationHSlider--------
	const VstInt32 secondrowYtop = firstrowYbottom + space;
	const VstInt32 secondrowYbottom = secondrowYtop + hHSlider->getHeight();
	size(lmarginX, secondrowYtop, (lmarginX+HSliderWidth), secondrowYbottom);
	point(0, 0);
	attenuationHSlider = new CHorizontalSlider(size, this, kAttenuationdB,
		size.left, size.left + HSliderMax,
		hHSliderButton, hHSlider, point, kLeft);
	attenuationHSlider->setOffsetHandle (point);
	attenuationHSlider->setValue(effect->getParameter(kAttenuationdB));
	attenuationHSlider->setTransparency(true);
	attenuationHSlider->setZoomFactor(50.0f);
	frame->addView(attenuationHSlider);

	size(lmarginX, firstrowYbottom, lmarginX + HSliderMax, secondrowYtop);
	CTextLabel* AttenuationText = new CTextLabel(size,TEXT("Attenuation:"), NULL, kStyle);
	AttenuationText->setHoriAlign(kLeftText);
	AttenuationText->setTransparency(true);
	AttenuationText->setFont(kNormalFontSmaller);
	frame->addView(AttenuationText);


	//--attenuationDisplay--------
	const VstInt32 attenuationDisplayWidth = 70;
	size(lmarginX+HSliderWidth, secondrowYtop, (lmarginX+HSliderWidth+space+attenuationDisplayWidth), secondrowYbottom);
	attenuationDisplay = new CParamDisplay(size, NULL, kStyle);
	//attenuationDisplay->setFont(0);
	attenuationDisplay->setValue (effect->getParameter (kAttenuationdB));
	attenuationDisplay->setStringConvert (&displayAttenuation);
	attenuationDisplay->setTransparency(true);
	attenuationDisplay->setHoriAlign(kLeftText);
	frame->addView(attenuationDisplay);


	size(lmarginX+HSliderWidth+space+attenuationDisplayWidth+space, firstrowYbottom, hBack->getWidth(), secondrowYtop);
	CTextLabel* TuningRigourText = new CTextLabel(size,TEXT("Tuning rigour:"), NULL, kStyle);
	TuningRigourText->setHoriAlign(kLeftText);
	TuningRigourText->setTransparency(true);
	TuningRigourText->setFont(kNormalFontSmaller);
	frame->addView(TuningRigourText);


	//--tuningOptionMenu--------
	size(lmarginX+HSliderWidth+space+attenuationDisplayWidth+space, secondrowYtop,
		(lmarginX+HSliderWidth+space+attenuationDisplayWidth+space+80), secondrowYbottom);
	tuningOptionMenu = new COptionMenu(size, this, kPlanningRigour, 0, 0, kPopupStyle|kCheckStyle|kStyle);
	for(unsigned int i=0; i<PlanningRigour::nDegrees; ++i)
	{
		tuningOptionMenu->addEntry(PlanningRigour::Rigour[i], i);
	}
	tuningOptionMenu->setCurrent((int)(effect->getParameter(kPlanningRigour)*tuningOptionMenu->getNbEntries()));
	tuningOptionMenu->setTransparency(true);
	tuningOptionMenu->setHoriAlign(kLeftText);
	frame->addView(tuningOptionMenu);


	//--partitionsHSlider--------

	const VstInt32 thirdrowYtop = secondrowYbottom + space;
	const VstInt32 thirdrowYbottom = thirdrowYtop + hHSlider->getHeight();
	size(lmarginX, thirdrowYtop, (lmarginX+HSliderWidth), thirdrowYbottom);
	point(0, 0);
	partitionsHSlider = new CHorizontalSlider(size, this, kPartitions,
		size.left, size.left + HSliderMax,
		hHSliderButton, hHSlider, point, kLeft);
	partitionsHSlider->setOffsetHandle (point);
	partitionsHSlider->setValue(effect->getParameter(kPartitions));
	partitionsHSlider->setTransparency(true);
	partitionsHSlider->setZoomFactor(0.5);
	frame->addView(partitionsHSlider);


	size(lmarginX, secondrowYbottom, lmarginX + HSliderMax, thirdrowYtop);
	CTextLabel* PartitionsText = new CTextLabel(size,TEXT("Partitions:"), NULL, kStyle);
	PartitionsText->setHoriAlign(kLeftText);
	PartitionsText->setTransparency(true);
	PartitionsText->setFont(kNormalFontSmaller);
	frame->addView(PartitionsText);


	//--partitionsDisplay--------
	size(lmarginX+HSliderWidth, thirdrowYtop, lmarginX+HSliderWidth+attenuationDisplayWidth, thirdrowYbottom);
	partitionsDisplay = new CParamDisplay(size, NULL, kStyle);
	partitionsDisplay->setValue (effect->getParameter (kPartitions));
	partitionsDisplay->setStringConvert (&displayPartitions);
	partitionsDisplay->setTransparency(true);
	partitionsDisplay->setHoriAlign(kLeftText);
	frame->addView(partitionsDisplay);

	//--statusText--------
	const VstInt32 fourthrowYtop = thirdrowYbottom + space;
	size(lmarginX, fourthrowYtop, hBack->getWidth() - space,  hBack->getHeight() - space);
	statusText = new CTextLabel(size,
		static_cast<ConvolverVST*>(effect)->getConvolution().get_ptr() == NULL ? 
		TEXT("Load an audio filter / config file.\nFor more help see http://convolver.sf.net\nPlease report problems or successes to jrp@dial.pipex.com\nSome controls elements created by Vera Kinter, http://www.artvera-music.com")
		: static_cast<ConvolverVST*>(effect)->getConvolution()->Mixer.DisplayChannelPaths().c_str(), NULL, kStyle);
	statusText->setHoriAlign(kWrapText);
	statusText->setTransparency(true);
	frame->addView(statusText);

	setKnobMode(kLinearMode);

	if(hHSlider)
		hHSlider->forget();
	if(hButton)
		hButton->forget();

	return true;
}

//--------------------------------
void ConvolverVSTEditor::close()
{
	delete frame;
	frame = NULL;
}

//--------------------------------
void ConvolverVSTEditor::setParameter(long index, float value)
{
	if(!frame)
		return;

	switch(index)
	{
	case kPlanningRigour:
		if(tuningOptionMenu)
			tuningOptionMenu->setCurrent((int)(value*tuningOptionMenu->getNbEntries()));
		break;
	case kAttenuationdB:
		if(attenuationHSlider)
			attenuationHSlider->setValue(effect->getParameter(index));
		if(attenuationDisplay)
			attenuationDisplay->setValue(effect->getParameter(index));
		break;
	case kPartitions:
		if(partitionsHSlider)
			partitionsHSlider->setValue(effect->getParameter(index));
		if(partitionsDisplay)
			partitionsDisplay->setValue(effect->getParameter(index));
		break;
	}
	    //postUpdate();
}

void ConvolverVSTEditor::setStatus(const char* txt)
{

	statusText->setText(txt);
	statusText->setDirty ();
}

void ConvolverVSTEditor::setFilterFilename(const char* txt)
{	
	filterFilename->setText(txt);
	filterFilename->setDirty();
}


//--------------------------------
void ConvolverVSTEditor::valueChanged(CControl *pControl)
{
	VstInt32 tag = pControl->getTag();

	switch(tag)
	{
	case kPlanningRigour:
		effect->setParameterAutomated(tag, ((float)tuningOptionMenu->getCurrent()/(float)tuningOptionMenu->getNbEntries()));
		break;
	case kAttenuationdB:
		effect->setParameterAutomated(tag, pControl->getValue());
		break;
	case kPartitions:
		effect->setParameterAutomated(tag, pControl->getValue());
		break;

	case kSelectFilter:
		{
			if (pControl->getValue() > 0.5f)
			{
				static_cast<ConvolverVST*>(effect)->SelectFilter();
				//dynamic_cast<ConvolverVST*>(effect)->GetConvolutionList()->get_description();
			}
		}
		break;
	}
	pControl->setDirty ();
}

//--------------------------------
void ConvolverVSTEditor::idle()
{
	AEffGUIEditor::idle();

}

