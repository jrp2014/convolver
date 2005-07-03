// convolverDS.cpp : Defines the entry point for the DLL application.
//


#include "convolverWMP\convolver.h"
#include "convolverWMP\convolverPropPage.h"
//#include "stdafx.h"
#include "convolverDS.h"
#include <streams.h>

//BOOL APIENTRY DllMain( HANDLE hModule, 
//                       DWORD  ul_reason_for_call, 
//                       LPVOID lpReserved
//					 )
//{
//	switch (ul_reason_for_call)
//	{
//	case DLL_PROCESS_ATTACH:
//	case DLL_THREAD_ATTACH:
//	case DLL_THREAD_DETACH:
//	case DLL_PROCESS_DETACH:
//		break;
//	}
//    return TRUE;
//}

extern "C" BOOL WINAPI DllEntryPoint(HINSTANCE, ULONG, LPVOID);
BOOL APIENTRY DllMain(HANDLE hModule, DWORD dwReason, LPVOID lpReserved)
{
    return DllEntryPoint((HINSTANCE)(hModule), dwReason, lpReserved);
}


//// This is an example of an exported variable
//CONVOLVERDS_API int nconvolverDS=0;
//
//// This is an example of an exported function.
//CONVOLVERDS_API int fnconvolverDS(void)
//{
//	return 42;
//}

//// This is the constructor of a class that has been exported.
//// see convolverDS.h for the class definition
//CconvolverDS::CconvolverDS()
//{ 
//	return; 
//}

// Put out the name of a function and instance on the debugger.
// Invoke this at the start of functions to allow a trace.
#define DbgFunc(a) DbgLog(( LOG_TRACE                        \
                          , 2                                \
                          , TEXT("CConvolver(Instance %d)::%s") \
                          , m_nThisInstance                  \
                          , TEXT(a)                          \
                         ));


// Self-registration data structures

const AMOVIESETUP_MEDIATYPE
sudPinTypes =   { &MEDIATYPE_Audio        // clsMajorType
                , &MEDIASUBTYPE_NULL };   // clsMinorType

const AMOVIESETUP_PIN
psudPins[] = { { L"Input"            // strName
               , FALSE               // bRendered
               , FALSE               // bOutput
               , FALSE               // bZero
               , FALSE               // bMany
               , &CLSID_NULL         // clsConnectsToFilter
               , L"Output"           // strConnectsToPin
               , 1                   // nTypes
               , &sudPinTypes        // lpTypes
               }
             , { L"Output"           // strName
               , FALSE               // bRendered
               , TRUE                // bOutput
               , FALSE               // bZero
               , FALSE               // bMany
               , &CLSID_NULL         // clsConnectsToFilter
               , L"Input"            // strConnectsToPin
               , 1                   // nTypes
               , &sudPinTypes        // lpTypes
               }
             };

const AMOVIESETUP_FILTER
sudConvolver = { &CLSID_Convolver                   // class id
            , L"Convolver"                       // strName
            , MERIT_DO_NOT_USE                // dwMerit
            , 2                               // nPins
            , psudPins                        // lpPin
            };

// Needed for the CreateInstance mechanism
CFactoryTemplate g_Templates[2]= { { L"Convolver"
                                   , &CLSID_Convolver
                                   , CConvolver::CreateInstance
                                   , NULL
                                   , &sudConvolver
                                   }
                                 , { L"Convolver Property Page"
                                   , &CLSID_ConvolverPropPageDS
                                   , CConvolverProperties::CreateInstance
                                   }
                                 };

int g_cTemplates = sizeof(g_Templates)/sizeof(g_Templates[0]);

// initialise the static instance count.
int CConvolver::m_nInstanceCount = 0;
