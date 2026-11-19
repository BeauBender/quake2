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
 * The per-channel arithmetic of ref_metal's HDR output (TASK196, slice
 * HD2; docs/plans/hdr-output.md, sections 2.2 and 5) that the fragment
 * shaders (rm_fragment_present_hdr and rm_fragment_hdr_read in
 * rm_shaders.metal) and the host test (tests/metal/rm_host_test.c) share:
 * the frame target under r_hdr 1 keeps the stock, sRGB-encoded values,
 * the present decodes them to linear once (the sRGB transfer function
 * extended past 1.0 and sign-preserving, for the extended linear sRGB
 * colour space), and the readback of the SDR fallback clamps them to
 * [0, 1] before the 8-bit store rounds them. Plain C99 and Metal Shading
 * Language alike.
 *
 * =======================================================================
 */

#ifndef MTL_HDR_MATH_H
#define MTL_HDR_MATH_H

#ifdef __METAL_VERSION__
#define MTL_HDR_POW(a, b) pow((a), (b))
#else
#include <math.h>
#define MTL_HDR_POW(a, b) powf((a), (b))
#endif

/*
 * An sRGB-encoded channel c to linear light: c / 12.92 up to 0.04045,
 * ((c + 0.055) / 1.055)^2.4 above, for |c|, with c's sign. Past 1.0 the
 * same curve goes on (1.0 gives 1.0, SDR white), so a value above white
 * in the target is above white in the drawable.
 */
static inline float
mtl_hdr_linear(float c)
{
	float a = (c < 0.0f) ? -c : c;
	float l = (a <= 0.04045f) ? (a / 12.92f) : MTL_HDR_POW((a + 0.055f) / 1.055f, 2.4f);

	return (c < 0.0f) ? -l : l;
}

/*
 * The SDR fallback of a channel (HD2's readback, a plain clamp): c held
 * to [0, 1], a value that is not a number to 0. The BGRA8Unorm store the
 * readback pass writes then rounds it to the nearest byte (mtl_hdr_byte).
 */
static inline float
mtl_hdr_clamp(float c)
{
	return (c > 0.0f) ? ((c < 1.0f) ? c : 1.0f) : 0.0f;
}

/* the byte the 8-bit store makes of mtl_hdr_clamp(c): round to nearest (the host's reference) */
static inline unsigned
mtl_hdr_byte(float c)
{
	return (unsigned)(mtl_hdr_clamp(c) * 255.0f + 0.5f);
}

#endif
