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
 * The weight rule of ref_metal's denoiser (RT7, TASK59 slice 2;
 * docs/plans/metal-raytracing-design.md, section 2) that the kernels
 * (rm_rtdenoise.metal) and the host test (tests/metal/rm_rtdenoise_test.c)
 * share: a 5 x 5 bilateral blur of the accumulated AO and sun, guided by
 * the distance from the eye and the normal, its strength scaled by the
 * pixel's sample count (full at 1 sample, none from 16). Plain C99 and
 * Metal Shading Language alike, as common/mtl_rtt_math.h; no pointer, so
 * no address space.
 *
 * =======================================================================
 */

#ifndef MTL_RTD_MATH_H
#define MTL_RTD_MATH_H

#include "mtl_shader_math.h"
#include "mtl_rtao_math.h"
#include "mtl_rtt_math.h"

/* the blur's reach: 2 pixels on each side, 5 x 5 */
#define MTL_RTD_RADIUS 2

/* the sample counts of the strength: full at MTL_RTD_COUNT_FULL, none from MTL_RTD_COUNT_NONE, linear between */
#define MTL_RTD_COUNT_FULL 1.0f
#define MTL_RTD_COUNT_NONE 16.0f

/*
 * The depth guide: a neighbour's distance from the eye may differ from
 * the pixel's by MTL_RTD_DEPTH_ABS units plus MTL_RTD_DEPTH_REL of the
 * pixel's distance (the temporal rule's tolerance, common/mtl_rtt_math.h,
 * without the eye's move); its weight falls linearly to 0 there.
 */
#define MTL_RTD_DEPTH_ABS 2.0f
#define MTL_RTD_DEPTH_REL 0.02f

/* the normal guide: a neighbour whose normal's cosine with the pixel's is at or below this weighs 0 */
#define MTL_RTD_NORMAL_MIN 0.9f

/* the denoiser's view (rm_rtd_view_t in the back ends, the kernels' mtl_rtd_view_t): 48 bytes */
typedef struct
{
	float dir[4];       /* the sun's direction, toward it (rm_rtsun_view_t's dir) */
	float sun[4];       /* the sun's term's colour (rm_rtsun_view_t's sun) */
	float floor_;       /* the AO's floor, r_rt_ao_min (rm_rtao_view_t's floor_) */
	float pad[3];
} mtl_rtd_view_t;

/*
 * FIX222: the count's range as bit patterns, as FIX212's guards
 * (common/mtl_rtt_math.h): MTL_RTD_COUNT_FULL and MTL_RTD_COUNT_NONE.
 * Every pattern from the first up to, not at, the second is a positive
 * number in [1, 16); a negative value has the sign bit and lies above
 * both, a NaN lies above +inf.
 */
#define MTL_RTD_BITS_FULL 0x3f800000u   /* MTL_RTD_COUNT_FULL, 1.0f */
#define MTL_RTD_BITS_NONE 0x41800000u   /* MTL_RTD_COUNT_NONE, 16.0f */

/*
 * 1 when a pixel of n samples (the history's weight) has a strength above
 * 0: n in [1, 16). An integer test of the pattern, not a float compare:
 * the shaders build with fast math, under which a float compare may be
 * folded as if no NaN came, so a NaN count is not counted here. For
 * every n that is not a NaN it gives (n >= 1.0f) && (n < 16.0f).
 */
static inline int
mtl_rtd_counted(float n)
{
	const unsigned u = MTL_RTT_BITS(n);

	return (u >= MTL_RTD_BITS_FULL) && (u < MTL_RTD_BITS_NONE);
}

/* the blur's strength for a pixel of n samples: 1 at 1, 0 from 16, linear between; 0 when no sample or not finite */
static inline float
mtl_rtd_strength(float n)
{
	if (!mtl_rtd_counted(n))
	{
		return 0.0f;
	}

	return (MTL_RTD_COUNT_NONE - n) / (MTL_RTD_COUNT_NONE - MTL_RTD_COUNT_FULL);
}

/* the spatial weight of the tap (dx, dy), -2 to 2 each: the binomial 1 4 6 4 1 in each axis, over 256; 0 outside */
static inline float
mtl_rtd_tap(int dx, int dy)
{
	const int ax = (dx < 0) ? -dx : dx, ay = (dy < 0) ? -dy : dy;
	const int kx = (ax == 0) ? 6 : (ax == 1) ? 4 : (ax == 2) ? 1 : 0;
	const int ky = (ay == 0) ? 6 : (ay == 1) ? 4 : (ay == 2) ? 1 : 0;

	return (float)(kx * ky) / 256.0f;
}

/* the depth guide: 1 at the pixel's distance d, falling to 0 at the tolerance and beyond; 0 when not finite */
static inline float
mtl_rtd_depth_w(float d, float dn)
{
	const float e = d - dn;
	const float tol = MTL_RTD_DEPTH_ABS + MTL_RTD_DEPTH_REL * d;
	const float w = 1.0f - ((e < 0.0f) ? -e : e) / tol;

	/* written so that a NaN fails */
	return (w > 0.0f) && (w <= 1.0f) ? w : 0.0f;
}

/* the normal guide for unit normals n and nn: 1 when they agree, 0 at a cosine of MTL_RTD_NORMAL_MIN or less */
static inline float
mtl_rtd_normal_w(mtl_rtao_v3_t n, mtl_rtao_v3_t nn)
{
	const float c = n.x * nn.x + n.y * nn.y + n.z * nn.z;
	const float w = (c - MTL_RTD_NORMAL_MIN) / (1.0f - MTL_RTD_NORMAL_MIN);

	/* written so that a NaN fails */
	if (!(w > 0.0f))
	{
		return 0.0f;
	}

	return (w < 1.0f) ? w : 1.0f;
}

/*
 * The weight of the neighbour at (dx, dy) from the pixel: d, n and m the
 * pixel's distance from the eye, normal and material id, dn, nn, mn and
 * cn the neighbour's and its sample count. 0 for a neighbour of another
 * material, with no sample, or across a depth or normal edge.
 */
static inline float
mtl_rtd_weight(int dx, int dy, float d, float dn, mtl_rtao_v3_t n, mtl_rtao_v3_t nn, unsigned m, unsigned mn,
		float cn)
{
	float w;

	if ((m != mn) || !mtl_rtt_atleast1(cn))
	{
		return 0.0f;
	}

	w = mtl_rtd_tap(dx, dy) * mtl_rtd_depth_w(d, dn) * mtl_rtd_normal_w(n, nn);

	/* written so that a NaN fails */
	return (w > 0.0f) ? w : 0.0f;
}

/* the blur's running sums */
typedef struct
{
	float sum;
	float wsum;
} mtl_rtd_acc_t;

static inline mtl_rtd_acc_t
mtl_rtd_acc(void)
{
	mtl_rtd_acc_t a;

	a.sum = 0.0f;
	a.wsum = 0.0f;
	return a;
}

/* a's sums with the value v at the weight w; a weight that is not above 0, or either not finite, adds nothing */
static inline mtl_rtd_acc_t
mtl_rtd_add(mtl_rtd_acc_t a, float w, float v)
{
	if (!(w > 0.0f) || !isfinite(w) || !isfinite(v))
	{
		return a;
	}

	a.sum += w * v;
	a.wsum += w;
	return a;
}

/*
 * The denoised value of a pixel whose accumulated value is v and sample
 * count n: v moved toward the weighted mean of a by the strength of n
 * (mtl_rtd_strength); v when nothing was added or the mean is not finite,
 * the mean itself when v is not finite and the strength above 0.
 */
static inline float
mtl_rtd_result(mtl_rtd_acc_t a, float v, float n)
{
	const float s = mtl_rtd_strength(n);
	float mean;

	if (!mtl_rtd_counted(n) || !(a.wsum > 0.0f))
	{
		return v;
	}

	mean = a.sum / a.wsum;

	if (!isfinite(mean))
	{
		return v;
	}

	if (!isfinite(v))
	{
		return mean;
	}

	return v + s * (mean - v);
}

/*
 * Slice 2b: the water reflections' blur (the design, section 2: "no
 * history, the warp moves every frame, and a 3 x 3 depth-guided blur").
 * No history, so no sample count: the strength is always full, and a
 * pixel's blurred value is the weighted mean of its 3 x 3 neighbours
 * that hold a reflection, guided by the same depth rule as the 5 x 5
 * blur (mtl_rtd_depth_w). It runs while r_rt_temporal is above 0.
 */
#define MTL_RTD_REFL_RADIUS 1

/*
 * The reflections' blur's view (rmb_rtwater.m fills it from the water's
 * view, rm_rtw_view_t): GL clip coordinates to world (column-major, as
 * its inv), the viewport at the render size and the eye. 96 bytes.
 */
typedef struct
{
	float inv[16];
	float vp[4];
	float eye[4];
} mtl_rtd_rview_t;

/* the spatial weight of the reflections' tap (dx, dy), -1 to 1 each: the binomial 1 2 1 in each axis, over 16; 0 outside */
static inline float
mtl_rtd_rtap(int dx, int dy)
{
	const int ax = (dx < 0) ? -dx : dx, ay = (dy < 0) ? -dy : dy;
	const int kx = (ax == 0) ? 2 : (ax == 1) ? 1 : 0;
	const int ky = (ay == 0) ? 2 : (ay == 1) ? 1 : 0;

	return (float)(kx * ky) / 16.0f;
}

/*
 * The weight of the reflection neighbour at (dx, dy) from the pixel: d
 * and dn the pixel's and the neighbour's distance from the eye, ok 0
 * when the neighbour holds no reflection to blur (the kernel's class
 * test). 0 for such a neighbour or across a depth edge.
 */
static inline float
mtl_rtd_rweight(int dx, int dy, float d, float dn, int ok)
{
	float w;

	if (!ok)
	{
		return 0.0f;
	}

	w = mtl_rtd_rtap(dx, dy) * mtl_rtd_depth_w(d, dn);

	/* written so that a NaN fails */
	return (w > 0.0f) ? w : 0.0f;
}

/* the blurred reflection channel of a pixel whose own value is v: the full-strength result (1 sample) of the sums */
static inline float
mtl_rtd_rresult(mtl_rtd_acc_t a, float v)
{
	return mtl_rtd_result(a, v, MTL_RTD_COUNT_FULL);
}

#endif /* MTL_RTD_MATH_H */
