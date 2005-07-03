// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the CONVOLVERDS_EXPORTS
// symbol defined on the command line. this symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// CONVOLVERDS_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef CONVOLVERDS_EXPORTS
#define CONVOLVERDS_API __declspec(dllexport)
#else
#define CONVOLVERDS_API __declspec(dllimport)
#endif

//// This class is exported from the convolverDS.dll
//class CONVOLVERDS_API CconvolverDS {
//public:
//	CconvolverDS(void);
//	// TODO: add your methods here.
//};
//
//extern CONVOLVERDS_API int nconvolverDS;
//
//CONVOLVERDS_API int fnconvolverDS(void);
