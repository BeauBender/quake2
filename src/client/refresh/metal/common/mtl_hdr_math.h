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
 * The per-channel arithmetic of ref_metal's HDR output (TASK196, slices
 * HD2 and HD3; docs/plans/hdr-output.md, sections 2.2, 2.3, 2.4 and 5)
 * that the fragment shaders (rm_fragment_present_hdr, rm_fragment_hdr_read
 * and rm_fragment_tonemap in rm_shaders.metal) and the host test
 * (tests/metal/rm_host_test.c) share:
 * the frame target under r_hdr 1 keeps the stock, sRGB-encoded values,
 * the present decodes them to linear once (the sRGB transfer function
 * extended past 1.0 and sign-preserving, for the extended linear sRGB
 * colour space), and the readback of the SDR fallback clamps them to
 * [0, 1] before the 8-bit store rounds them. HD3's tone map, at the seat
 * of the Retro pass (the end of the 3D view, before the 2D layer), is the
 * identity up to SDR white and a Reinhard shoulder above it to the peak in
 * force, min(r_hdr_peak, the display's headroom), on the largest linear
 * component, so hue is kept; at a peak of 1.0 it is HD2's clamp. Plain C99
 * and Metal Shading Language alike.
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

/*
 * HD2's present for a channel of the float16 frame target under r_hdr 1:
 * c held to [0, 1] (not a number to 0) and then decoded to linear light,
 * so nothing shows above SDR white. HD3's present is mtl_hdr_show; this
 * stays as the reference it must equal at a peak of 1.0.
 */
static inline float
mtl_hdr_present(float c)
{
	return mtl_hdr_linear(mtl_hdr_clamp(c));
}

/*
 * The peak in force (the plan's section 2.4): r_hdr_peak held to 1.0 to
 * 16.0 (a value that is not a number is 1.0), then to the display's
 * headroom, which is 1.0 when it is unavailable (below 0, not a number)
 * or reports no extended range. So it is never below 1.0.
 */
#define MTL_HDR_PEAK_MAX 16.0f

static inline float
mtl_hdr_peak(float cvar, float headroom)
{
	float p = (cvar > 1.0f) ? ((cvar < MTL_HDR_PEAK_MAX) ? cvar : MTL_HDR_PEAK_MAX) : 1.0f;
	float h = (headroom > 1.0f) ? headroom : 1.0f;

	return (p < h) ? p : h;
}

/* a linear value made safe for the map: not a number and below 0 to 0, past float16's largest to it */
static inline float
mtl_hdr_safe(float l)
{
	return (l > 0.0f) ? ((l < 65504.0f) ? l : 65504.0f) : 0.0f;
}

/* the largest of a pixel's three linear components, each made safe: what the shoulder maps */
static inline float
mtl_hdr_max(float r, float g, float b)
{
	float a = mtl_hdr_safe(mtl_hdr_linear(r)), c = mtl_hdr_safe(mtl_hdr_linear(g)), d = mtl_hdr_safe(mtl_hdr_linear(b));
	float m = (a > c) ? a : c;

	return (m > d) ? m : d;
}

/*
 * The shoulder (the plan's H3): x up to 1.0 as it is, above it
 * f(x) = 1 + e / (1 + e / (P - 1)), e = x - 1, which has slope 1 at white,
 * rises monotonically and tends to P without passing it. A peak of 1.0 or
 * less (or not a number) is a clamp at 1.0: the formula would divide by 0.
 */
static inline float
mtl_hdr_shoulder(float x, float p)
{
	float e;

	if (!(x > 1.0f))
	{
		return (x > 0.0f) ? x : 0.0f;
	}

	if (!(p > 1.0f))
	{
		return 1.0f;
	}

	e = ((x < 65504.0f) ? x : 65504.0f) - 1.0f;
	x = 1.0f + e / (1.0f + e / (p - 1.0f));

	return (x < p) ? x : p;
}

/* a linear channel to sRGB encoding, the inverse of mtl_hdr_linear for l >= 0 (extended past 1.0) */
static inline float
mtl_hdr_encode(float l)
{
	return (l <= 0.0031308f) ? (l * 12.92f) : (1.055f * MTL_HDR_POW(l, 1.0f / 2.4f) - 0.055f);
}

/*
 * The tone map of one encoded channel c of a pixel whose largest linear
 * component is m (mtl_hdr_max), at the peak in force p, back to an encoded
 * value, in place in the float16 target:
 * - at p <= 1 (no headroom), HD2's clamp, mtl_hdr_clamp(c);
 * - not a number and below 0 to 0;
 * - a pixel at or below white (m <= 1) as it is, exactly;
 * - above white the pixel scaled by f(m) / m, so the ratio of its
 *   components (its hue) is kept.
 */
static inline float
mtl_hdr_tone(float c, float m, float p)
{
	if (!(p > 1.0f))
	{
		return mtl_hdr_clamp(c);
	}

	if (!(c > 0.0f))
	{
		return 0.0f;
	}

	if (!(m > 1.0f))
	{
		return c;
	}

	return mtl_hdr_encode(mtl_hdr_safe(mtl_hdr_linear(c)) * (mtl_hdr_shoulder(m, p) / m));
}

/*
 * The present's value for a channel of the float16 frame target under
 * r_hdr 1 from HD3: decoded to linear light and held to [0, p] (not a
 * number to 0), p the peak in force; the tone map ahead of it already
 * rolled the view off below p, so this only catches what float16 rounding
 * or a pass after the tone map put above it. At p = 1 it equals HD2's
 * mtl_hdr_present for every input.
 */
static inline float
mtl_hdr_show(float c, float p)
{
	float l = mtl_hdr_safe(mtl_hdr_linear(c));
	float q = (p > 1.0f) ? p : 1.0f;

	return (l < q) ? l : q;
}

/* the byte the 8-bit store makes of mtl_hdr_clamp(c): round to nearest (the host's reference) */
static inline unsigned
mtl_hdr_byte(float c)
{
	return (unsigned)(mtl_hdr_clamp(c) * 255.0f + 0.5f);
}

#endif
