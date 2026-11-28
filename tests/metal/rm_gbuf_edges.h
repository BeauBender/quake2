/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * FIX219: the edges of the G-buffer's depth and normal alpha tests that the
 * host tests share (rm_rtptemporal_test.c for the helpers themselves,
 * rm_rtpdenoise_test.c, rm_rtmtemporal_test.c and rm_rtwater_test.c for the
 * header functions that call them): +-0, the negative subnormal, the floats
 * beside each threshold (0, 0.5 and 1), +-inf and the NaNs, FIX212's
 * edge_bits[] ones included (0xff800001 and 0xffffffff). Each test that
 * includes this compares a helper with the float compare it replaces,
 * compiled without fast math (refused below), and a NaN must be refused.
 *
 * =======================================================================
 */

#ifndef RM_GBUF_EDGES_H
#define RM_GBUF_EDGES_H

#include <string.h>

#if defined(__FAST_MATH__) || (defined(__FINITE_MATH_ONLY__) && __FINITE_MATH_ONLY__)
#error "the G-buffer tests' reference compares need IEEE compares; build without fast math"
#endif

typedef struct
{
	unsigned bits;
	const char *name;
} gbe_edge_t;

static const gbe_edge_t gbe_edges[] = {
	{ 0x00000000u, "+0" },
	{ 0x80000000u, "-0" },
	{ 0x80000001u, "negative subnormal" },
	{ 0x00000001u, "positive subnormal" },
	{ 0x3effffffu, "next below 0.5" },
	{ 0x3f000000u, "0.5" },
	{ 0x3f000001u, "next above 0.5" },
	{ 0x3f7fffffu, "largest below 1" },
	{ 0x3f800000u, "1" },
	{ 0x3f800001u, "smallest above 1" },
	{ 0xbf800000u, "-1" },
	{ 0x7f7fffffu, "FLT_MAX" },
	{ 0x7f800000u, "+inf" },
	{ 0xff800000u, "-inf" },
	{ 0x7fc00000u, "quiet NaN" },
	{ 0x7f800001u, "signalling NaN" },
	{ 0xffc00000u, "negative NaN" },
	{ 0xff800001u, "smallest negative NaN" },
	{ 0xffffffffu, "largest negative NaN" },
	{ 0x7fffffffu, "largest positive NaN" },
};
#define GBE_NEDGE ((int)(sizeof(gbe_edges) / sizeof(gbe_edges[0])))

/* the sweep: every 2^12th pattern, 2^20 of them */
#define GBE_SWEEP_SHIFT 12
#define GBE_SWEEP_N (1u << (32 - GBE_SWEEP_SHIFT))

static inline float
gbe_float(unsigned u)
{
	float f;

	memcpy(&f, &u, sizeof(f));
	return f;
}

/* 1 when the pattern is a NaN, either sign */
static inline int
gbe_is_nan(unsigned u)
{
	return (u & 0x7fffffffu) > 0x7f800000u;
}

#endif /* RM_GBUF_EDGES_H */
