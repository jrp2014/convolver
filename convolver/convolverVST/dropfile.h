/*---------
header:
---------*/
#pragma once
#ifndef __AEffEditor__
#include "AEffEditor.hpp"
#endif

#ifndef __vstgui__
#include "vstgui.h"
#endif

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
class CDropFile : public CControl
{
public:
	//CControl (CRect &size, CControlListener *listener, int tag, CBitmap *pBackground = 0); );
	CDropFile (CRect &size, CControlListener *listener, int tag, CBitmap *pBackground = 0); 

	/*CDropFile (const CRect &size, CControlListener *listener, long tag, 
           CBitmap *background,
           CBitmap *handle, const CPoint &offset, float resolution);*/
	virtual ~CDropFile ();

	virtual void draw (CDrawContext *pContext);
	virtual bool onDrop (CDrawContext* context, CDragContainer* drag, const CPoint& where);
 	virtual void mouse (CDrawContext *pContext, CPoint &where, long button = -1);
	char* getFileName( )
	{return thefilename;}
	CLASS_METHODS(CDropFile, CControl)
	

protected:
 char* thefilename;
 	
 
};