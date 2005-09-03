// Faster random number generator: the standard one seems to take as much time as the convolution itself
//
// From http://www.qbrundage.com/michaelb/pubs/essays/random_number_generation
#pragma once

// To seed random number generator
#include <stdlib.h>
#include <time.h>
#include <limits>

const int MT_LEN   =    624;

// The result is cast to type T
template <typename T>
class MT
{
private:

	int mt_index;
	unsigned long mt_buffer[MT_LEN];

public:

	MT() : mt_index(0)
	{
#if	defined(DEBUG) | defined(_DEBUG)
		cdebug << "Using MT" << std::endl;
#endif
		// Seed the random-number generator with current time so that
		// the numbers will be different every time we run.
		srand( (unsigned)time( NULL ) );

		for (int i = 0; i < MT_LEN; ++i)
		{
			mt_buffer[i] = rand() * static_cast<float>(ULONG_MAX) / static_cast<float>(RAND_MAX);
		}
			//std::numeric_limits<unsigned long>::max();
	}

#define MT_IA           397
#define MT_IB           (MT_LEN - MT_IA)
#define UPPER_MASK      0x80000000
#define LOWER_MASK      0x7FFFFFFF
#define MATRIX_A        0x9908B0DF
#define TWIST(b,i,j)    ((b)[i] & UPPER_MASK) | ((b)[j] & LOWER_MASK)
#define MAGIC(s)        (((s)&1)*MATRIX_A)

	// Generates a random 32-bit integer
	unsigned long mt_random()
	{
		unsigned long * b = mt_buffer;
		int idx = mt_index;
		unsigned long s;

		if (idx == MT_LEN*sizeof(unsigned long))
		{
			idx = 0;

			for (int i=0; i < MT_IB; ++i) {
				s = TWIST(b, i, i+1);
				b[i] = b[i + MT_IA] ^ (s >> 1) ^ MAGIC(s);
			}
			for (int i=0; i < MT_LEN-1; ++i) {
				s = TWIST(b, i, i+1);
				b[i] = b[i - MT_IB] ^ (s >> 1) ^ MAGIC(s);
			}

			s = TWIST(b, MT_LEN-1, 0);
			b[MT_LEN-1] = b[MT_IA-1] ^ (s >> 1) ^ MAGIC(s);
		}
		mt_index = idx + sizeof(unsigned long);
		return static_cast<T>(*(unsigned long *)((unsigned char *)b + idx));
		/*
		Matsumoto and Nishimura additionally confound the bits returned to the caller
		but this doesn't increase the randomness, and slows down the generator by
		as much as 25%.  So I omit these operations here.

		r ^= (r >> 11);
		r ^= (r << 7) & 0x9D2C5680;
		r ^= (r << 15) & 0xEFC60000;
		r ^= (r >> 18);
		*/
	}

	T mt_max() const
	{
		return static_cast<T>(ULONG_MAX);
	}
};
