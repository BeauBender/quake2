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
 * Host test of ref_metal's temporal accumulation (RT7, TASK59 slice 1):
 * the rejection rule, the blend and the sample count of
 * common/mtl_rtt_math.h, which the kernel calls, and r_rt_temporal and the
 * reset events of rm_rttemporal.c; the shadows' light set and the one
 * reset state of the three effects' histories (slice 1b). Built and run by tests/check-ref-metal.sh,
 * which also builds it against mutants of both files that it must fail.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rttemporal.h"
#include "../../src/client/refresh/metal/common/mtl_shader_math.h"
#include "../../src/client/refresh/metal/common/mtl_rts_math.h"

static int checks, failures;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

static int
near(double a, double b, double eps)
{
	return fabs(a - b) <= eps;
}

static void
test_reproject(void)
{
	mtl_rtt_px_t r;

	/* no motion: the pixel itself */
	r = mtl_rtt_reproject(10, 20, 0.0f, 0.0f, 64, 48);
	CHECK(r.ok && (r.x == 10) && (r.y == 20));
	/* previous minus current: the point was 3.4 pixels right and 2.6 up */
	r = mtl_rtt_reproject(10, 20, 3.4f, -2.6f, 64, 48);
	CHECK(r.ok && (r.x == 13) && (r.y == 17));
	/* the edges: the last texel is on, one past it off */
	r = mtl_rtt_reproject(63, 47, 0.49f, 0.49f, 64, 48);
	CHECK(r.ok && (r.x == 63) && (r.y == 47));
	CHECK(!mtl_rtt_reproject(63, 0, 0.5f, 0.0f, 64, 48).ok);
	CHECK(!mtl_rtt_reproject(0, 47, 0.0f, 0.5f, 64, 48).ok);
	CHECK(!mtl_rtt_reproject(0, 0, -0.51f, 0.0f, 64, 48).ok);
	CHECK(!mtl_rtt_reproject(0, 0, 0.0f, -0.51f, 64, 48).ok);
	CHECK(mtl_rtt_reproject(0, 0, -0.5f, -0.5f, 64, 48).ok);
	CHECK(!mtl_rtt_reproject(5, 5, NAN, 0.0f, 64, 48).ok);
	CHECK(!mtl_rtt_reproject(5, 5, 0.0f, INFINITY, 64, 48).ok);
	CHECK(!mtl_rtt_reproject(5, 5, -INFINITY, 0.0f, 64, 48).ok);
}

/* the accepted case and one case per reason, each alone */
static void
test_reject(void)
{
	const float d = 400.0f;

	/* accepted: on screen, the same distance, material and lights, a history with samples */
	CHECK(mtl_rtt_reject(0, 1, d, d, 0.0f, 1, 1, 0x0302u, 0x0302u, 4.0f) == 0);
	/* the view reset */
	CHECK(mtl_rtt_reject(RM_RTT_RESET_CUT, 1, d, d, 0.0f, 1, 1, 0, 0, 4.0f) == MTL_RTT_REJ_RESET);
	/* off the screen: nothing else is judged, the history there is not read */
	CHECK(mtl_rtt_reject(0, 0, d, d + 999.0f, 0.0f, 1, 2, 1, 2, 0.0f) == MTL_RTT_REJ_OFF);
	CHECK(mtl_rtt_reject(1, 0, d, d, 0.0f, 1, 1, 0, 0, 4.0f) == (MTL_RTT_REJ_RESET | MTL_RTT_REJ_OFF));
	/* the depth: a disocclusion, a nearer surface uncovered */
	CHECK(mtl_rtt_reject(0, 1, d, d + 100.0f, 0.0f, 1, 1, 0, 0, 4.0f) == MTL_RTT_REJ_DEPTH);
	CHECK(mtl_rtt_reject(0, 1, d, d - 100.0f, 0.0f, 1, 1, 0, 0, 4.0f) == MTL_RTT_REJ_DEPTH);
	CHECK(mtl_rtt_reject(0, 1, d, NAN, 0.0f, 1, 1, 0, 0, 4.0f) == MTL_RTT_REJ_DEPTH);
	/* the material id: a model's pixel over the world's history */
	CHECK(mtl_rtt_reject(0, 1, d, d, 0.0f, 2, 1, 0, 0, 4.0f) == MTL_RTT_REJ_MAT);
	CHECK(mtl_rtt_reject(0, 1, d, d, 0.0f, 1, 3, 0, 0, 4.0f) == MTL_RTT_REJ_MAT);
	/* the dynamic light set: a light came into reach, or another replaced one */
	CHECK(mtl_rtt_reject(0, 1, d, d, 0.0f, 1, 1, 0x01u, 0x00u, 4.0f) == MTL_RTT_REJ_LIGHTS);
	CHECK(mtl_rtt_reject(0, 1, d, d, 0.0f, 1, 1, 0x0201u, 0x0301u, 4.0f) == MTL_RTT_REJ_LIGHTS);
	/* a history without a sample there */
	CHECK(mtl_rtt_reject(0, 1, d, d, 0.0f, 1, 1, 0, 0, 0.0f) == MTL_RTT_REJ_EMPTY);
	CHECK(mtl_rtt_reject(0, 1, d, d, 0.0f, 1, 1, 0, 0, NAN) == MTL_RTT_REJ_EMPTY);
	/* every reason at once */
	CHECK(mtl_rtt_reject(1, 1, d, 0.0f, 0.0f, 1, 2, 1, 2, 0.0f) ==
		(MTL_RTT_REJ_RESET | MTL_RTT_REJ_DEPTH | MTL_RTT_REJ_MAT | MTL_RTT_REJ_LIGHTS | MTL_RTT_REJ_EMPTY));
}

/* the tolerance: the eye's move, MTL_RTT_DEPTH_ABS units and MTL_RTT_DEPTH_REL of the distance */
static void
test_depth(void)
{
	const float d = 1000.0f, tol = MTL_RTT_DEPTH_ABS + MTL_RTT_DEPTH_REL * d;

	CHECK(tol == 22.0f);
	CHECK(mtl_rtt_depth_ok(d, d - tol + 0.01f, 0.0f));
	CHECK(mtl_rtt_depth_ok(d, d + tol - 0.01f, 0.0f));
	CHECK(!mtl_rtt_depth_ok(d, d - tol - 0.01f, 0.0f));
	CHECK(!mtl_rtt_depth_ok(d, d + tol + 0.01f, 0.0f));
	/* the eye walked 10 units toward the wall: its distance shrank by that much */
	CHECK(mtl_rtt_depth_ok(d - 10.0f, d + tol - 1.0f, 10.0f));
	CHECK(!mtl_rtt_depth_ok(d - 10.0f, d + tol - 1.0f, 0.0f));
	/* near the eye the absolute part holds */
	CHECK(mtl_rtt_depth_ok(10.0f, 12.1f, 0.0f));
	CHECK(!mtl_rtt_depth_ok(10.0f, 12.3f, 0.0f));
	CHECK(!mtl_rtt_depth_ok(NAN, 10.0f, 0.0f));
	CHECK(!mtl_rtt_depth_ok(10.0f, 10.0f, NAN));
}

static void
test_blend(void)
{
	float h, n;
	int i, j;

	/* a rejected or empty history is not read */
	CHECK(mtl_rtt_blend(MTL_RTT_REJ_DEPTH, 0.25f, 1.0f, 10.0f, 0.9f) == 0.25f);
	CHECK(mtl_rtt_blend(0, 0.25f, 1.0f, 0.0f, 0.9f) == 0.25f);
	CHECK(mtl_rtt_blend(0, 0.25f, NAN, NAN, 0.9f) == 0.25f);
	/* alpha 0: off */
	CHECK(mtl_rtt_blend(0, 0.25f, 1.0f, 10.0f, 0.0f) == 0.25f);
	/* one sample in the history: the mean of the two */
	CHECK(near(mtl_rtt_blend(0, 0.0f, 1.0f, 1.0f, 0.9f), 0.5, 1e-6));
	/* nine: weight 0.9, which alpha 0.9 is too */
	CHECK(near(mtl_rtt_blend(0, 0.0f, 1.0f, 9.0f, 0.9f), 0.9, 1e-6));
	/* many: alpha */
	CHECK(near(mtl_rtt_blend(0, 0.0f, 1.0f, 200.0f, 0.9f), 0.9, 1e-6));
	CHECK(near(mtl_rtt_blend(0, 0.0f, 1.0f, 200.0f, 0.5f), 0.5, 1e-6));

	/* the count: one more, at most MTL_RTT_COUNT_MAX, 1 after a rejection */
	CHECK(mtl_rtt_count(0, 0.0f) == 1.0f);
	CHECK(mtl_rtt_count(0, 1.0f) == 2.0f);
	CHECK(mtl_rtt_count(0, 254.0f) == MTL_RTT_COUNT_MAX);
	CHECK(mtl_rtt_count(0, MTL_RTT_COUNT_MAX) == MTL_RTT_COUNT_MAX);
	CHECK(mtl_rtt_count(MTL_RTT_REJ_MAT, 100.0f) == 1.0f);
	CHECK(mtl_rtt_count(0, NAN) == 1.0f);

	/* a constant stays it */
	for (h = 0.375f, n = 0.0f, i = 0; i < 100; i++)
	{
		h = mtl_rtt_blend(0, 0.375f, h, n, 0.9f);
		n = mtl_rtt_count(0, n);
	}

	CHECK(near(h, 0.375, 1e-6) && (n == 100.0f));

	/*
	 * Two samples alternating, 0 and 1 (the AO of one ray, the noise
	 * moving): their plain mean while young, then within alpha's ripple of
	 * 0.5. The pixel's own samples are what a still camera averages.
	 */
	for (h = 0.0f, n = 0.0f, i = 0; i < 8; i++)
	{
		const float cur = (float)(i & 1);

		h = mtl_rtt_blend(0, cur, h, n, 0.9f);
		n = mtl_rtt_count(0, n);
	}

	CHECK(near(h, 0.5, 1e-6));

	for (j = 0; j < 200; j++, i++)
	{
		const float cur = (float)(i & 1);

		h = mtl_rtt_blend(0, cur, h, n, 0.9f);
		n = mtl_rtt_count(0, n);
	}

	CHECK(fabs(h - 0.5) < 0.06);

	/* a rejection starts again from the frame's value and one sample */
	h = mtl_rtt_blend(MTL_RTT_REJ_LIGHTS, 1.0f, h, n, 0.9f);
	n = mtl_rtt_count(MTL_RTT_REJ_LIGHTS, n);
	CHECK((h == 1.0f) && (n == 1.0f));
}

static void
test_alpha(void)
{
	CHECK(rm_rtt_alpha(0.0f) == 0.0f);
	CHECK(rm_rtt_alpha(-0.5f) == 0.0f);
	CHECK(rm_rtt_alpha(NAN) == 0.0f);
	CHECK(rm_rtt_alpha(INFINITY) == 0.0f);
	CHECK(rm_rtt_alpha(0.9f) == 0.9f);
	CHECK(rm_rtt_alpha(0.25f) == 0.25f);
	CHECK(rm_rtt_alpha(1.0f) == RM_RTT_ALPHA_MAX);
	CHECK(rm_rtt_alpha(5.0f) == RM_RTT_ALPHA_MAX);

	{
		const rm_rtt_view_t v = rm_rtt_view(0.9f, RM_RTT_RESET_SIZE, 3.0f), z = rm_rtt_view(NAN, 0, NAN);

		CHECK((v.alpha == 0.9f) && (v.reset == RM_RTT_RESET_SIZE) && (v.move == 3.0f) && (v.seq == 0));
		CHECK((z.alpha == 0.0f) && (z.reset == 0) && (z.move == 0.0f));
	}

	CHECK(sizeof(rm_rtt_view_t) == 16);
}

static rm_rtt_key_t
key(float x, float y, float z)
{
	rm_rtt_key_t k;

	k.w = 640;
	k.h = 480;
	k.vx = 0;
	k.vy = 0;
	k.vw = 640;
	k.vh = 480;
	k.org[0] = x;
	k.org[1] = y;
	k.org[2] = z;
	return k;
}

/* the reset events of the design's "Reset" row that the renderer sees in a view */
static void
test_reset(void)
{
	rm_rtt_state_t st;
	rm_rtt_key_t k = key(100, 200, 50);
	float move = -1.0f;

	memset(&st, 0, sizeof(st));

	/* the renderer's start (a vid_restart makes it again): the first view resets */
	rm_rtt_state_reset(&st);
	CHECK(rm_rtt_step(&st, &k, &move) == RM_RTT_RESET_FIRST);
	CHECK(move == 0.0f);
	rm_rtt_frame_end(&st);

	/* the next frame, the same view: kept */
	CHECK(rm_rtt_step(&st, &k, &move) == 0);
	rm_rtt_frame_end(&st);

	/* a walk: kept, its move measured */
	k = key(103, 204, 50);
	CHECK(rm_rtt_step(&st, &k, &move) == 0);
	CHECK(near(move, 5.0, 1e-5));
	CHECK(st.views == 3);
	rm_rtt_frame_end(&st);

	/* a move of exactly 256 on an axis is no cut; past it is (a teleport, a camera cut) */
	k = key(103 + 256, 204, 50);
	CHECK(rm_rtt_step(&st, &k, &move) == 0);
	rm_rtt_frame_end(&st);
	k = key(103 + 256, 204, 50 - 256.5f);
	CHECK(rm_rtt_step(&st, &k, &move) == RM_RTT_RESET_CUT);
	CHECK((move == 0.0f) && (st.views == 1));
	rm_rtt_frame_end(&st);

	/* an eye that is not finite cuts */
	k.org[1] = NAN;
	CHECK(rm_rtt_step(&st, &k, &move) == RM_RTT_RESET_CUT);
	CHECK(move == 0.0f);
	rm_rtt_frame_end(&st);
	k = key(0, 0, 0);
	CHECK(rm_rtt_step(&st, &k, &move) == RM_RTT_RESET_CUT);
	rm_rtt_frame_end(&st);

	/* the render size: r_rt_scale, the window */
	k.w = 320;
	k.vw = 320;
	CHECK(rm_rtt_step(&st, &k, &move) == (RM_RTT_RESET_SIZE | RM_RTT_RESET_RECT));
	rm_rtt_frame_end(&st);
	CHECK(rm_rtt_step(&st, &k, &move) == 0);
	rm_rtt_frame_end(&st);

	/* the rectangle alone: viewsize */
	k.vx = 16;
	k.vy = 12;
	k.vw = 288;
	k.vh = 456;
	CHECK(rm_rtt_step(&st, &k, &move) == RM_RTT_RESET_RECT);
	rm_rtt_frame_end(&st);

	/* two views in one frame (a second world view): the second is judged against the first */
	CHECK(rm_rtt_step(&st, &k, &move) == 0);
	CHECK(rm_rtt_step(&st, &k, &move) == 0);
	rm_rtt_frame_end(&st);

	/*
	 * A frame that accumulated no view (every effect off, r_rt_temporal 0,
	 * a cinematic): the first view with an effect on after it resets.
	 */
	rm_rtt_frame_end(&st);
	CHECK(rm_rtt_step(&st, &k, &move) == RM_RTT_RESET_FIRST);
	rm_rtt_frame_end(&st);
	CHECK(rm_rtt_step(&st, &k, &move) == 0);
	rm_rtt_frame_end(&st);

	/* a map load (BeginRegistration) */
	rm_rtt_state_reset(&st);
	CHECK(rm_rtt_step(&st, &k, &move) == RM_RTT_RESET_FIRST);
	CHECK(st.views == 1);
}

/* the shadow kernel's packed indices with the four slots' lights a, b, c, d and the flags */
static unsigned
packed(unsigned a, unsigned b, unsigned c, unsigned d, unsigned flags)
{
	mtl_rts_top_t t = mtl_rts_top_init();

	t.idx[0] = a;
	t.idx[1] = b;
	t.idx[2] = c;
	t.idx[3] = d;
	return mtl_rts_pack_idx(t, flags);
}

/* reject's reasons for two light sets, everything else kept */
static unsigned
rej_lights(unsigned lights, unsigned hlights)
{
	return mtl_rtt_reject(0, 1, 100.0f, 100.0f, 0.0f, 1, 1, lights, hlights, 4.0f);
}

/* the shadows' dynamic light set (slice 1b): the packed indices, the flag bits masked off */
static void
test_lights(void)
{
	const unsigned n = MTL_RTS_NO_LIGHT;
	const unsigned p = packed(3, 7, n, n, MTL_RTS_IN_RANGE | MTL_RTS_SHADOWED);
	const unsigned lit = packed(3, 7, n, n, MTL_RTS_IN_RANGE), none = packed(n, n, n, n, 0);

	/* the mask is the four indices' bits, none of the flags' */
	CHECK((MTL_RTT_LIGHTS_MASK & (MTL_RTS_IN_RANGE | MTL_RTS_SHADOWED)) == 0);
	CHECK(mtl_rtt_lights(none) == MTL_RTT_LIGHTS_MASK);
	CHECK(mtl_rtt_lights(0xffffffffu) == MTL_RTT_LIGHTS_MASK);

	/* the flags masked: a pixel shadowed in one frame and lit in the next keeps its set */
	CHECK(mtl_rtt_lights(p) == mtl_rtt_lights(lit));
	CHECK((mtl_rtt_lights(p) & (MTL_RTS_IN_RANGE | MTL_RTS_SHADOWED)) == 0);
	CHECK(mtl_rtt_lights(p) == packed(3, 7, n, n, 0));

	/* the same set accepts */
	CHECK(rej_lights(mtl_rtt_lights(p), mtl_rtt_lights(lit)) == 0);

	/* a set change rejects: another light, one more, one gone, the slots' order, none left */
	CHECK(rej_lights(mtl_rtt_lights(packed(3, 8, n, n, MTL_RTS_IN_RANGE)), mtl_rtt_lights(p)) == MTL_RTT_REJ_LIGHTS);
	CHECK(rej_lights(mtl_rtt_lights(packed(3, 7, 9, n, 0)), mtl_rtt_lights(p)) == MTL_RTT_REJ_LIGHTS);
	CHECK(rej_lights(mtl_rtt_lights(packed(3, n, n, n, 0)), mtl_rtt_lights(p)) == MTL_RTT_REJ_LIGHTS);
	CHECK(rej_lights(mtl_rtt_lights(packed(7, 3, n, n, 0)), mtl_rtt_lights(p)) == MTL_RTT_REJ_LIGHTS);
	CHECK(rej_lights(mtl_rtt_lights(none), mtl_rtt_lights(p)) == MTL_RTT_REJ_LIGHTS);
}

/* one view of the state for every effect: its step and the view the kernels take */
static rm_rtt_view_t
step(rm_rtt_state_t *st, const rm_rtt_key_t *k)
{
	float move = 0.0f;
	const unsigned r = rm_rtt_step(st, k, &move);
	rm_rtt_view_t t = rm_rtt_view(0.9f, r, move);

	t.seq = st->seq;
	return t;
}

/*
 * The one reset state of the three effects (slice 1b): a reset or a cut
 * resets every history in the same view, and a history whose dispatch was
 * not encoded for a view (the trace's early return after rm_rtt_step) is
 * not read in the next.
 */
static void
test_hist(void)
{
	rm_rtt_state_t st;
	rm_rtt_hist_t sh, sun, ao;
	rm_rtt_key_t k = key(100, 200, 50);
	rm_rtt_view_t t;
	unsigned seq;

	memset(&st, 0, sizeof(st));
	rm_rtt_state_reset(&st);
	rm_rtt_hist_reset(&sh);
	rm_rtt_hist_reset(&sun);
	rm_rtt_hist_reset(&ao);

	/* the first view: every history resets */
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sh, &t) == RM_RTT_RESET_FIRST);
	CHECK(rm_rtt_hist_take(&sun, &t) == RM_RTT_RESET_FIRST);
	CHECK(rm_rtt_hist_take(&ao, &t) == RM_RTT_RESET_FIRST);
	rm_rtt_frame_end(&st);

	/* the next: every history kept */
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sh, &t) == 0);
	CHECK(rm_rtt_hist_take(&sun, &t) == 0);
	CHECK(rm_rtt_hist_take(&ao, &t) == 0);
	rm_rtt_frame_end(&st);

	/* a cut resets the three in the same view */
	k = key(100, 200, 50 + 300);
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sh, &t) == RM_RTT_RESET_CUT);
	CHECK(rm_rtt_hist_take(&sun, &t) == RM_RTT_RESET_CUT);
	CHECK(rm_rtt_hist_take(&ao, &t) == RM_RTT_RESET_CUT);
	rm_rtt_frame_end(&st);

	/*
	 * The early return: the shadows' trace returns after this view's step,
	 * its dispatch not encoded, its history a view behind. The next view
	 * resets it alone; the others are kept.
	 */
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sun, &t) == 0);
	CHECK(rm_rtt_hist_take(&ao, &t) == 0);
	rm_rtt_frame_end(&st);
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sh, &t) == RM_RTT_RESET_FIRST);
	CHECK(rm_rtt_hist_take(&sun, &t) == 0);
	CHECK(rm_rtt_hist_take(&ao, &t) == 0);
	rm_rtt_frame_end(&st);
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sh, &t) == 0);
	rm_rtt_frame_end(&st);

	/* the same on a reset view: its reason is not lost with the skipped dispatch */
	rm_rtt_state_reset(&st);
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sun, &t) == RM_RTT_RESET_FIRST);
	rm_rtt_frame_end(&st);
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sun, &t) == 0);
	CHECK(rm_rtt_hist_take(&ao, &t) == RM_RTT_RESET_FIRST);
	rm_rtt_frame_end(&st);

	/* a view taken twice (no new step) is not a history of it */
	CHECK(rm_rtt_hist_take(&ao, &t) == RM_RTT_RESET_FIRST);

	/* the views' numbers outlive a map load, so a history that skipped views across it is not read */
	seq = st.seq;
	rm_rtt_state_reset(&st);
	CHECK(st.seq == seq);

	/*
	 * r_rt_temporal 0 and back: the histories are released and made again
	 * (rm_rtt_hist_reset); even one kept up with the views is not read then.
	 */
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sun, &t) == RM_RTT_RESET_FIRST);
	rm_rtt_frame_end(&st);
	t = step(&st, &k);
	CHECK(rm_rtt_hist_take(&sun, &t) == 0);
	rm_rtt_frame_end(&st);
	rm_rtt_hist_reset(&sun);
	t = step(&st, &k);
	CHECK(t.reset == 0);
	CHECK(rm_rtt_hist_take(&sun, &t) == RM_RTT_RESET_FIRST);
	CHECK(rm_rtt_hist_take(&sh, &t) == RM_RTT_RESET_FIRST);
}

int
main(void)
{
	test_reproject();
	test_reject();
	test_depth();
	test_blend();
	test_alpha();
	test_reset();
	test_lights();
	test_hist();

	printf("rm_rttemporal_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
