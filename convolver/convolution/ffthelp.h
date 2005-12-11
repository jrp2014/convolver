#include "convolution\config.h"


struct OptimalDFT
{

	static const DWORD HalfLargestDFTSize = 1000000; // allow FFTs up to 2x this size

	static const DWORD OptimalDFTSize[];

	DWORD GetOptimalDFTSize( DWORD size0 );

};
