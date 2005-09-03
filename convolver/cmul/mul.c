
// See http://softwareforums.intel.com/ids/board/message?board.id=16&message.id=2983
// requires C99
//
// This may be a inelegant, but it beats having to learn assembler

#if (defined(__ICC) || defined(__ICL) || defined(__ECC) || defined(__ECL))

#include<complex.h>

typedef float _Complex c32;

void cmul(c32* restrict a, c32* restrict b, c32* restrict c, int n)
{
	int i;
	int n2;
	n2 = n/2;
	c[0] = crealf(a[0]) * crealf(b[0]) + __I__* cimagf(a[0]) * cimagf(b[0]);
#pragma omp parallel for
#pragma ivdep
#pragma vector aligned
	for (i = 1; i < n2; ++i)
		c[i] = a[i] * b[i];
}

void cmuladd(c32* restrict a, c32* restrict b, c32* restrict c, int n)
{
	int i;
	int n2;
	n2 = n/2;
	c[0] += crealf(a[0]) * crealf(b[0]) + __I__* cimagf(a[0]) * cimagf(b[0]);
#pragma omp parallel for
#pragma ivdep
#pragma vector aligned
	for (i = 1; i < n2; ++i)
		c[i] += a[i] * b[i];
}
#endif
