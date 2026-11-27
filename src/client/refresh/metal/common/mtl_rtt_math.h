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
 * The per-pixel arithmetic of ref_metal's temporal accumulation (RT7,
 * TASK59 slice 1; docs/plans/metal-raytracing-design.md, section 2) that
 * the kernels and the host test (tests/metal/rm_rttemporal_test.c) share:
 * where a pixel was in the history (the G-buffer's motion vectors, which
 * exclude the jitter), whether that history is kept, and the exponential
 * blend. Plain C99 and Metal Shading Language alike, as
 * common/mtl_rtao_math.h; no pointer, so no address space.
 *
 * =======================================================================
 */

#ifndef MTL_RTT_MATH_H
#define MTL_RTT_MATH_H

/* the reasons a pixel's history is rejected (mtl_rtt_reject), 0 when it is kept */
#define MTL_RTT_REJ_RESET 1     /* the view reset its history (rm_rtt_step's reasons) */
#define MTL_RTT_REJ_OFF 2       /* the point was off the screen in the history's frame */
#define MTL_RTT_REJ_DEPTH 4     /* its distance from the eye differs beyond the tolerance */
#define MTL_RTT_REJ_MAT 8       /* the G-buffer's material id differs */
#define MTL_RTT_REJ_LIGHTS 16   /* the set of dynamic lights that reach the pixel differs */
#define MTL_RTT_REJ_EMPTY 32    /* the history holds no sample there */

/*
 * The depth tolerance: the distances from each frame's own eye may differ
 * by the eye's move between the frames, plus MTL_RTT_DEPTH_ABS units, plus
 * MTL_RTT_DEPTH_REL of the current distance (a pixel's footprint grows
 * with it, and the history is read up to half a pixel off, design section 2).
 */
#define MTL_RTT_DEPTH_ABS 2.0f
#define MTL_RTT_DEPTH_REL 0.02f

/* the samples a history counts at most (the denoiser of slice 2 reads the count) */
#define MTL_RTT_COUNT_MAX 255.0f

/*
 * The guards of the history (a NaN fails, a count below 1 is empty, a light
 * outside [0, 1] is bad) test the float's bit pattern as an integer, not
 * with a float compare. The Metal shaders build with fast math, under which
 * a float compare may be folded as if no NaN came (the compiler emits
 * "fcmp fast" with nnan), so a NaN could pass a guard written as "a NaN
 * fails this compare" and be reprojected from frame to frame. An integer
 * test is no float operation and keeps its meaning (as mtl_rtm_index1's).
 * For every input each test gives the float compare's answer, -0 included
 * (-0 >= 0 holds, so -0 is non-negative, as in the compares).
 */
#ifdef __METAL_VERSION__
#define MTL_RTT_BITS(f) as_type<uint>(f)
#else
static inline unsigned
mtl_rtt_bits(float f)
{
	union
	{
		float f;
		unsigned u;
	} b;

	b.f = f;
	return b.u;
}

#define MTL_RTT_BITS(f) mtl_rtt_bits(f)
#endif

#define MTL_RTT_SIGN 0x80000000u        /* the sign bit; -0 is the sign alone */
#define MTL_RTT_BITS_INF 0x7f800000u    /* +inf: every pattern above it with the sign clear is a NaN */
#define MTL_RTT_BITS_1 0x3f800000u      /* 1.0f */
#define MTL_RTT_BITS_MAX 0x437f0000u    /* MTL_RTT_COUNT_MAX, 255.0f */

/* 1 when f >= 0 (+0, -0, every positive value and +inf; not a NaN, not a negative value) */
static inline int
mtl_rtt_nonneg(float f)
{
	const unsigned u = MTL_RTT_BITS(f);

	return (u <= MTL_RTT_BITS_INF) || (u == MTL_RTT_SIGN);
}

/* 1 when f is a number in [0, 1], -0 included */
static inline int
mtl_rtt_unit(float f)
{
	const unsigned u = MTL_RTT_BITS(f);

	return (u <= MTL_RTT_BITS_1) || (u == MTL_RTT_SIGN);
}

/* 1 when f >= 1 (+inf included; not a NaN, not a negative value, not a fraction) */
static inline int
mtl_rtt_atleast1(float f)
{
	const unsigned u = MTL_RTT_BITS(f);

	return (u >= MTL_RTT_BITS_1) && (u <= MTL_RTT_BITS_INF);
}

/* 1 when f > 0 (+inf included; not a NaN, not 0 or -0, not a negative value) */
static inline int
mtl_rtt_positive(float f)
{
	const unsigned u = MTL_RTT_BITS(f);

	return (u != 0u) && (u <= MTL_RTT_BITS_INF);
}

/* 1 when f <= MTL_RTT_COUNT_MAX (every negative value, -inf and -0 included; not a NaN, not +inf) */
static inline int
mtl_rtt_atmost_max(float f)
{
	const unsigned u = MTL_RTT_BITS(f);

	return (u <= MTL_RTT_BITS_MAX) || ((u >= MTL_RTT_SIGN) && (u <= (MTL_RTT_SIGN | MTL_RTT_BITS_INF)));
}

/*
 * 1 when f < lim, for an f that is not negative (the caller tested
 * mtl_rtt_nonneg; a negative f here is 0, not below) and a lim that is not
 * negative and not a NaN: with the sign clear, the patterns of non-negative
 * values order as the values do, -0 is 0, and a NaN f is not below.
 */
static inline int
mtl_rtt_below(float f, float lim)
{
	unsigned u = MTL_RTT_BITS(f);

	if (u == MTL_RTT_SIGN)
	{
		u = 0u;
	}

	return (u < MTL_RTT_BITS(lim)) && (u <= MTL_RTT_BITS_INF);
}

/* where a pixel was in the history: its texel, ok 0 when that is off the screen */
typedef struct
{
	int ok;
	unsigned x, y;
} mtl_rtt_px_t;

/*
 * Pixel (x, y) of a w x h render size, with its motion vector (mx, my):
 * pixels, previous minus current, +x right, +y down (design section 3,
 * "Motion units and sign"). Its centre moved by the vector is where it
 * was; the texel holding that point is the history's. A point outside
 * the size, or one that is not finite, is off the screen.
 */
static inline mtl_rtt_px_t
mtl_rtt_reproject(unsigned x, unsigned y, float mx, float my, unsigned w, unsigned h)
{
	mtl_rtt_px_t r;
	const float hx = (float)x + 0.5f + mx, hy = (float)y + 0.5f + my;

	r.ok = 0;
	r.x = 0;
	r.y = 0;

	/* a NaN fails (an integer test of the pattern, see MTL_RTT_BITS); a negative is already refused by mtl_rtt_nonneg */
	if (!(mtl_rtt_nonneg(hx) && mtl_rtt_nonneg(hy) && mtl_rtt_below(hx, (float)w) && mtl_rtt_below(hy, (float)h)))
	{
		return r;
	}

	r.ok = 1;
	r.x = (unsigned)hx;
	r.y = (unsigned)hy;
	return r;
}

/* 1 when the distances dist (this frame's) and hdist (the history's) agree within the tolerance for an eye move of move units */
static inline int
mtl_rtt_depth_ok(float dist, float hdist, float move)
{
	const float d = dist - hdist;
	const float tol = move + MTL_RTT_DEPTH_ABS + MTL_RTT_DEPTH_REL * dist;
	const unsigned ad = MTL_RTT_BITS(d) & ~MTL_RTT_SIGN;     /* |d|; a NaN stays above +inf */
	unsigned t = MTL_RTT_BITS(tol);

	/* |d| <= tol, a NaN in d or tol failing, as (d <= tol) && (-d <= tol) does; a negative tol fails (its sign makes it a pattern above +inf); -0 is 0 */
	if (t == MTL_RTT_SIGN)
	{
		t = 0u;
	}

	return (ad <= MTL_RTT_BITS_INF) && (t <= MTL_RTT_BITS_INF) && (ad <= t);
}

/*
 * The reasons (MTL_RTT_REJ_*) the history of a pixel is rejected; 0 when
 * it is kept. reset: the view's; ok: mtl_rtt_reproject's; dist, mat and
 * lights: this frame's distance from the eye, material id and dynamic
 * light set; hdist, hmat, hlights and hcount: the history's at the
 * reprojected texel; move: the eye's move since the history's frame.
 */
static inline unsigned
mtl_rtt_reject(unsigned reset, int ok, float dist, float hdist, float move, unsigned mat, unsigned hmat,
		unsigned lights, unsigned hlights, float hcount)
{
	unsigned r = 0;

	if (reset)
	{
		r |= MTL_RTT_REJ_RESET;
	}

	if (!ok)
	{
		return r | MTL_RTT_REJ_OFF;
	}

	if (!mtl_rtt_depth_ok(dist, hdist, move))
	{
		r |= MTL_RTT_REJ_DEPTH;
	}

	if (mat != hmat)
	{
		r |= MTL_RTT_REJ_MAT;
	}

	if (lights != hlights)
	{
		r |= MTL_RTT_REJ_LIGHTS;
	}

	if (!mtl_rtt_atleast1(hcount))
	{
		r |= MTL_RTT_REJ_EMPTY;
	}

	return r;
}

/* the samples the history counts after this frame: one more, at most MTL_RTT_COUNT_MAX; 1 after a rejection */
static inline float
mtl_rtt_count(unsigned rejected, float hcount)
{
	if (rejected || !mtl_rtt_atleast1(hcount))
	{
		return 1.0f;
	}

	return (hcount + 1.0f < MTL_RTT_COUNT_MAX) ? hcount + 1.0f : MTL_RTT_COUNT_MAX;
}

/*
 * The exponential blend: this frame's value cur and the history's hist
 * (hcount samples), the history weighted alpha (r_rt_temporal), but never
 * more than hcount / (hcount + 1), so a young history is the plain mean
 * of its samples. A rejected history is not read: cur.
 */
static inline float
mtl_rtt_blend(unsigned rejected, float cur, float hist, float hcount, float alpha)
{
	float w;

	if (rejected || !mtl_rtt_atleast1(hcount) || !mtl_rtt_positive(alpha))
	{
		return cur;
	}

	w = hcount / (hcount + 1.0f);
	w = (alpha < w) ? alpha : w;
	return w * hist + (1.0f - w) * cur;
}

/*
 * The dynamic light set of a shadow pixel (TASK59 slice 1b): the shadow
 * kernel's packed indices (mtl_rts_pack_idx, the second word of its out
 * texture), the four slots' light indices at 6 bits each, with its flag
 * bits (in range, shadowed) masked off, so that only a change of the
 * lights that reach the pixel, or of their order, rejects its history.
 */
#define MTL_RTT_LIGHTS_MASK 0x00ffffffu

static inline unsigned
mtl_rtt_lights(unsigned packed)
{
	return packed & MTL_RTT_LIGHTS_MASK;
}

/* the view of the accumulation (rm_rtt_view_t in C, the kernels' rm_rtt_view): 16 bytes */
typedef struct
{
	float alpha;        /* rm_rtt_alpha: 0 accumulates nothing */
	float move;         /* the eye's move since the last view, units */
	unsigned reset;     /* rm_rtt_step's reasons, 0 when the history is kept */
	unsigned seq;       /* the view's number (rm_rtt_state_t's seq); the kernels do not read it */
} mtl_rtt_view_t;

#endif /* MTL_RTT_MATH_H */
