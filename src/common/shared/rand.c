/*
 * KISS PRNG (c) 2011 Shinobu
 *
 * This file was optained from zuttobenkyou.wordpress.com
 * and modified by the Yamagi Quake II developers.
 *
 * LICENSE: Public domain
 *
 * =======================================================================
 *
 * KISS PRNG, as devised by Dr. George Marsaglia
 *
 * =======================================================================
 */

#include <stddef.h>
#include <stdint.h>

void Sys_GetRandomBytes(void *buf, size_t len);

#define QSIZE 0x200000
#define CNG (cng = 6906969069ULL * cng + 13579)
#define XS (xs ^= (xs << 13), xs ^= (xs >> 17), xs ^= (xs << 43))
#define KISS (B64MWC() + CNG + XS)

static uint64_t QARY[QSIZE];
static int j;
static uint64_t carry;
static uint64_t xs;
static uint64_t cng;

static uint64_t
B64MWC(void)
{
	uint64_t t, x;

	j = (j + 1) & (QSIZE - 1);
	x = QARY[j];
	t = (x << 28) + carry;
	carry = (x >> 36) - (t < x);
	return QARY[j] = t - x;
}

/*
 * Generate a pseudorandom
 * non-negative integer.
 */
int
randk(void)
{
	return (int)(KISS & 0x7fffffff);
}

/*
 * Generate a pseudorandom
 * signed float between
 * 0 and 1.
 */
float
frandk(void)
{
	return (randk()&32767)* (1.0/32767);
}

/* Generate a pseudorandom
 * float between -1 and 1.
 */
float
crandk(void)
{
	return (randk()&32767)* (2.0/32767) - 1;
}

/* Fills QARY[] from xs and cng and warms the state up. */
static void
randk_fill(void)
{
	uint64_t i;

	/* never leave xs at 0 */
	if (xs == 0)
	{
		xs = 0x9E3779B97F4A7C15ULL;
	}

	/* Seed QARY[] with CNG+XS: */
	for (i = 0; i < QSIZE; i++)
	{
		QARY[i] = CNG + XS;
	}

	/* Run through several rounds
	   to warm up the state */
	for (i = 0; i < 256; i++)
	{
		randk();
	}
}

/*
 * Seeds the PRNG
 */
void
randk_seed(void)
{
	/* Seed the generators from the OS */
	Sys_GetRandomBytes(&xs, sizeof(xs));
	Sys_GetRandomBytes(&cng, sizeof(cng));

	randk_fill();
}

/* One splitmix64 step: spreads a small seed over 64 bits. */
static uint64_t
randk_mix(uint64_t *s)
{
	uint64_t z = (*s += 0x9E3779B97F4A7C15ULL);

	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
	z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
	return z ^ (z >> 31);
}

/*
 * Seeds the PRNG from a fixed seed, so that the same seed gives the same
 * stream. Only the pin harness calls this (pins_seed, while recording or
 * replaying a renderer trace; src/client/vid/vid.c); everything else
 * seeds from the OS with randk_seed().
 */
void
randk_seed_fixed(unsigned int seed)
{
	uint64_t s = seed;

	/* the whole state, also what randk_seed() leaves as it was */
	j = 0;
	carry = 0;
	xs = randk_mix(&s);
	cng = randk_mix(&s);

	randk_fill();
}
