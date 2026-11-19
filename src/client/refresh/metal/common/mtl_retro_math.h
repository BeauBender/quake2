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
 * The per-pixel arithmetic of ref_metal's Retro post-process (TASK184,
 * PM2; docs/plans/presentation-modes.md, section 5.1) that the fragment
 * shader (rm_fragment_retro in rm_shaders.metal) and the host test
 * (tests/metal/rm_rtgate_test.c) share: the 4 x 4 ordered (Bayer)
 * threshold of a pixel, a channel of 8 bits quantized to 5 or 6 bits with
 * it (RGB565, as a 16-bit frame buffer stored it), the 2 x 2 box output
 * filter's average and the gamma curve's exponent. Integer arithmetic on
 * 8-bit values, so the shader and the test give the same bytes. Plain C99
 * and Metal Shading Language alike.
 *
 * =======================================================================
 */

#ifndef MTL_RETRO_MATH_H
#define MTL_RETRO_MATH_H

/* the gamma curves r_retro_gamma selects: none, then two exponents (mtl_retro_gamma) */
#define MTL_RETRO_CURVES 3

/*
 * The 4 x 4 Bayer rank, 0 to 15, of the pixel (x, y): the bits of x ^ y
 * and y interleaved and reversed, so row 0 reads 0 8 2 10, row 1 12 4 14
 * 6, row 2 3 11 1 9 and row 3 15 7 13 5.
 */
static inline unsigned
mtl_retro_bayer(unsigned x, unsigned y)
{
	unsigned a = (x ^ y) & 3u, b = y & 3u;

	return ((a & 1u) << 3) | ((b & 1u) << 2) | ((a >> 1) << 1) | (b >> 1);
}

/*
 * The 8-bit channel c quantized to bits (5 or 6) with the Bayer rank r
 * as its threshold, floor(c / 255 * L + (r + 0.5) / 16) for L = 2^bits -
 * 1, and widened back to 8 bits, the level's nearest byte.
 */
static inline unsigned
mtl_retro_quant(unsigned c, unsigned bits, unsigned r)
{
	unsigned l = (1u << bits) - 1u;
	unsigned k = (32u * (c > 255u ? 255u : c) * l + (2u * (r & 15u) + 1u) * 255u) / (32u * 255u);

	return (k * 255u + l / 2u) / l;
}

/* the 2 x 2 box filter of four quantized bytes, rounded */
static inline unsigned
mtl_retro_box(unsigned a, unsigned b, unsigned c, unsigned d)
{
	return (a + b + c + d + 2u) / 4u;
}

/* the exponent the curve applies to a byte's 0 to 1 value: 1 for none (0) and a curve out of range */
static inline float
mtl_retro_gamma(int curve)
{
	return (curve == 1) ? (1.0f / 1.15f) : (curve == 2) ? (1.0f / 1.3f) : 1.0f;
}

#endif
