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
 * The per-pixel rule of the model pixels' accumulation under RT-primary
 * (TASK191 slice 4, PM9) that rm_rtmtemporal.metal and the host test
 * (tests/metal/rm_rtmtemporal_test.c) share: which pixels the model pass
 * accumulates, the key that keeps their history apart from the world's
 * and the weapon's apart from every other model's, and the blend of the
 * traced light, TASK190's (common/mtl_rtp_math.h, included unchanged).
 * The history holds the light; gl1's model colour (common/mtl_rtm_math.h)
 * is taken of each frame's blend, so the glow pulse and the minlight
 * floor are never averaged. Plain C99 and Metal Shading Language alike;
 * no pointer, so no address space.
 *
 * =======================================================================
 */

#ifndef MTL_RTMT_MATH_H
#define MTL_RTMT_MATH_H

#include "mtl_rtp_math.h"
#include "mtl_rtm_math.h"

/* the tag's flags sit above every G-buffer material id (R8Uint) in a model key */
#define MTL_RTMT_KEY_SHIFT 8

/*
 * 1 when the pixel is one the model pass traces: material 2 (alias), a
 * depth short of the far plane and a written normal (rm_rtm_trace's
 * model). No world pixel (mtl_rtp_world) is one.
 */
static inline int
mtl_rtmt_model(unsigned mat, float depth, float normal_a)
{
	return (mat == MTL_GB_MAT_ALIAS) && mtl_rtp_below1(depth) && mtl_rtp_above_half(normal_a);
}

/*
 * The material word of a model pixel's history key (the key's y, which
 * mtl_rtp_reject compares as the material) for tag flags f (MTL_RTM_*):
 * MTL_GB_MAT_ALIAS plus the flags above every G-buffer id. A model key is
 * no world id, so model history and world history are each rejected by
 * the other; each set of flags has its own, the weapon's among them.
 */
static inline unsigned
mtl_rtmt_key(unsigned f)
{
	return MTL_GB_MAT_ALIAS + ((f & MTL_RTM_FLAGS) << MTL_RTMT_KEY_SHIFT);
}

/*
 * One model pixel: l the traced light (rm_rtdir_out_open's, before gl1's
 * model colour), h the history's (h.a its count), rej mtl_rtp_reject's
 * reasons, alpha the view's. TASK190's blend of each channel; the alpha
 * is an opaque model's, 1. The history holds the blend with its count;
 * the caller takes gl1's model colour of out (mtl_rtm_color).
 */
static inline mtl_rtp_px_t
mtl_rtmt_pixel(unsigned rej, mtl_rtm_v3_t l, mtl_rtp_rgba_t h, float alpha)
{
	mtl_rtp_rgba_t cur;

	cur.r = l.x;
	cur.g = l.y;
	cur.b = l.z;
	cur.a = 1.0f;
	return mtl_rtp_pixel(1, rej, cur, h, alpha);
}

#endif /* MTL_RTMT_MATH_H */
