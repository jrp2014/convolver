// Convolver: DSP plug-in for Windows Media Player that convolves an impulse respose
// filter it with the input stream.
//
// Copyright (C) 2005  John Pavel
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
// convolverdll.cpp : Implementation of DLL Exports.

#include "stdafx.h"
#include "resource.h"
#include <initguid.h>

#include "convolver.h"
#include "ConvolverPropPage.h"

#define CRTDBG_MAP_ALLOC
#include <crtdbg.h>     // Debug header
#include <uuids.h>      // DirectX SDK media types and subtyes
#include <dmoreg.h>     // DirectX SDK registration

CComModule _Module;

BEGIN_OBJECT_MAP(ObjectMap)
#ifdef DMO
OBJECT_ENTRY(CLSID_ConvolverDMO, CConvolver)
OBJECT_ENTRY(CLSID_ConvolverPropPageDMO, CConvolverPropPage)
#else
OBJECT_ENTRY(CLSID_Convolver, CConvolver)
OBJECT_ENTRY(CLSID_ConvolverPropPage, CConvolverPropPage)
#endif

END_OBJECT_MAP()

/////////////////////////////////////////////////////////////////////////////
// DLL Entry Point
/////////////////////////////////////////////////////////////////////////////

extern "C"
BOOL WINAPI DllMain(HINSTANCE hInstance, DWORD dwReason, LPVOID /*lpReserved*/)
{
    if (dwReason == DLL_PROCESS_ATTACH)
    {
        _Module.Init(ObjectMap, hInstance);
        DisableThreadLibraryCalls(hInstance);
    }
    else if (dwReason == DLL_PROCESS_DETACH)
    {
        _Module.Term();
    }

    return TRUE;    // ok
}

/////////////////////////////////////////////////////////////////////////////
// Used to determine whether the DLL can be unloaded by OLE
/////////////////////////////////////////////////////////////////////////////

STDAPI DllCanUnloadNow(void)
{
    return (_Module.GetLockCount()==0) ? S_OK : S_FALSE;
}

/////////////////////////////////////////////////////////////////////////////
// Returns a class factory to create an object of the requested type
/////////////////////////////////////////////////////////////////////////////

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    return _Module.GetClassObject(rclsid, riid, ppv);
}

/////////////////////////////////////////////////////////////////////////////
// DllRegisterServer - Adds entries to the system registry
/////////////////////////////////////////////////////////////////////////////

STDAPI DllRegisterServer(void)
{
	HRESULT hr;
#ifndef DMO
    CComPtr<IWMPMediaPluginRegistrar> spRegistrar;

    // Create the registration object
    hr = spRegistrar.CoCreateInstance(CLSID_WMPMediaPluginRegistrar, NULL, CLSCTX_INPROC_SERVER);
    if (FAILED(hr))
    {
        return hr;
    }
#endif

    // Load friendly name and description strings
    CComBSTR    bstrFriendlyName;
    CComBSTR    bstrDescription;

    bstrFriendlyName.LoadString(IDS_FRIENDLYNAME);
    bstrDescription.LoadString(IDS_DESCRIPTION);

    // Describe the type of data handled by the plug-in
    DMO_PARTIAL_MEDIATYPE mt[3] = { 0, 0, 0};
    mt[0].type = MEDIATYPE_Audio;
    mt[0].subtype = MEDIASUBTYPE_PCM;
    mt[1].type = MEDIATYPE_Audio;
	mt[1].subtype = MEDIASUBTYPE_IEEE_FLOAT;
	mt[2].type = MEDIATYPE_AnalogAudio;	// TODO: Unclear what this does!

#ifdef DMO

    hr = DMORegister(
        bstrFriendlyName,          // Friendly name
        CLSID_ConvolverDMO,        // CLSID
        DMOCATEGORY_AUDIO_EFFECT,  // Category
        0,                         // Flags 
        3,                         // Number of input types
        mt,                       // Array of input types
        3,                         // Number of output types
        mt);                      // Array of output types

#else

    // Register the plug-in with WMP
    hr = spRegistrar->WMPRegisterPlayerPlugin(
                    bstrFriendlyName,   // friendly name (for menus, etc)
                    bstrDescription,    // description (for Tools->Options->Plug-ins)
                    NULL,               // path to app that uninstalls the plug-in
                    1,                  // DirectShow priority for this plug-in
                    WMP_PLUGINTYPE_DSP, // Plug-in type
                    CLSID_Convolver,	// Class ID of plug-in
                    3,                  // No. media types supported by plug-in
                    &mt);               // Array of media types supported by plug-in

#endif

    if (FAILED(hr))
    {
        return hr;
    }

#ifdef DMO

	// Registers the object, with no typelib.
    return _Module.RegisterServer(FALSE);

#else // WMP10 plug-in

    // registers object, typelib and all interfaces in typelib
    return _Module.RegisterServer();

#endif
}

/////////////////////////////////////////////////////////////////////////////
// DllUnregisterServer - Removes entries from the system registry
/////////////////////////////////////////////////////////////////////////////

STDAPI DllUnregisterServer(void)

{
#ifdef DMO

    DMOUnregister(CLSID_ConvolverDMO, DMOCATEGORY_AUDIO_EFFECT);
    return _Module.UnregisterServer(TRUE);

#else

    CComPtr<IWMPMediaPluginRegistrar> spRegistrar;
    HRESULT hr;

    // Create the registration object
    hr = spRegistrar.CoCreateInstance(CLSID_WMPMediaPluginRegistrar, NULL, CLSCTX_INPROC_SERVER);
    if (FAILED(hr))
    {
        return hr;
    }

    // Tell WMP to remove this plug-in
    hr = spRegistrar->WMPUnRegisterPlayerPlugin(WMP_PLUGINTYPE_DSP, CLSID_Convolver);

	return _Module.UnregisterServer();

#endif

}

