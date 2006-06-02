 
#ifndef __vstgui__
#include "vstgui.h"
#endif

#ifndef __CDropFile__
#include "DropFile.h"
#endif

#include <math.h>

//------------------------------------------------------------------------
// CXYPad Surface
//------------------------------------------------------------------------
/*
CDropFile::CDropFile (const CRect &size, CControlListener *listener, long tag, CBitmap *background)
:	CControl (size, listener, tag, background) 
*/ 
CDropFile::CDropFile(CRect &size, CControlListener *listener, int tag, CBitmap *pBackground)
:	CControl (size, listener, tag, pBackground)
{
 
	thefilename = new char[1024];
	//setDefaultValue (0.f);
}

//------------------------------------------------------------------------
CDropFile::~CDropFile ()
{
 
}

//------------------------------------------------------------------------
void CDropFile::draw (CDrawContext *pContext)
{
	if (pBackground)
	{
		if (bTransparencyEnabled)
			pBackground->drawTransparent (pContext, size);
		else
			pBackground->draw (pContext, size);
	}
 
	setDirty (false);
}

bool CDropFile::onDrop(CDrawContext* context, CDragContainer* drag, const CPoint& where)
{
	long size,type;
	char* mytext= 	(char*)drag->first(size,type);
			strcpy(thefilename,mytext);
	beginEdit ();
	
		listener->valueChanged (context, this);
	
		// end of edit parameter
		endEdit ();
	return false;
 
}
 
//------------------------------------------------------------------------
void CDropFile::mouse (CDrawContext *pContext, CPoint &where, long button)
{
	if (!bMouseEnabled)
		return;
 
}

  