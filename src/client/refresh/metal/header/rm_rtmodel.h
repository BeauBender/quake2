/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
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
 * RT-primary's light on alias models and the view weapon (TASK191, PM9
 * of docs/plans/presentation-modes.md, section 4.1): under the RT-primary
 * presentation an opaque, rastered alias model takes the traced terms a
 * world pixel takes, at its own point and normal, in place of gl1's light
 * point and shadedots. Slice 1 holds the rules alone: which models are
 * traced, their colour rule, the tag that carries their flags and the
 * view weapon's unprojection. Nothing in the renderer calls them yet.
 *
 * The rm_rtm_* functions are plain C with neither SDL nor Metal
 * (rm_rtmodel.c), host-tested by tests/metal/rm_rtmodel_test.c with the
 * per-pixel arithmetic of common/mtl_rtm_math.h.
 *
 * =======================================================================
 */

#ifndef RM_RTMODEL_H
#define RM_RTMODEL_H

#include "rmb.h"
#include "rm_gbuf.h"
#include "rm_rtshadow.h"
#include "../common/mtl_shader_math.h"
#include "../common/mtl_rtm_math.h"

/*
 * 1 when a model of flags (RF_*, as RM_RF_*) keeps gl1's light under any
 * presentation, the light substitutions of rm_alias_shade: a shell, an
 * RF_FULLBRIGHT model, an RF_IR_VISIBLE one under the IR goggles
 * (RDF_IRGOGGLES in rdflags).
 */
int rm_rtm_stock_light(int flags, int rdflags);

/*
 * 1 when an alias model of flags stays stock-lit under the mode for good:
 * rm_rtm_stock_light's, and a model with RF_DEPTHHACK that is not the
 * view weapon (the crosshair model; the weapon has RF_DEPTHHACK too, and
 * slice 3 traces it).
 */
int rm_rtm_stock_model(int flags, int rdflags);

/*
 * The pass of a GBUFFER view that draws an entity, as rm_gb_entity_split,
 * under the RT-primary presentation when mode is not 0. With mode 0 it is
 * rm_gb_entity_split(flags, kind, shadows) for every input. Under the
 * mode the raster takes what rm_gb_entity_split's raster takes, less the
 * alias models that stay stock-lit (rm_rtm_stock_model) and, in slice 2,
 * an RF_GLOW or RF_MINLIGHT model, whose flags no tag carries yet. The
 * view weapon, a translucent model, a shell and a beam go forward, as now
 * (slice 3 rasters the weapon and tags the flagged models).
 */
int rm_rtm_entity_split(int flags, int kind, float shadows, int rdflags, int mode);

/*
 * 1 when the pixels of an alias model of flags under the mode take
 * traced light: a model the mode's raster takes (rm_rtm_entity_split)
 * whose normals were written (extra_ok, rm_gb_alias_extra's result); a
 * model without them never does.
 */
int rm_rtm_traced(int flags, int rdflags, int extra_ok);

/* the tag flags (MTL_RTM_*) of a model of flags: RF_GLOW, RF_MINLIGHT and RF_WEAPONMODEL */
unsigned rm_rtm_flags(int flags);

/* RF_GLOW's pulse at time, as rm_alias_shade computes it: 0.1 sin(7 time) */
float rm_rtm_glow_scale(float time);

/*
 * The colour rgba of a model pixel of flags with light (rgb) and shadedot
 * factor dot (MTL_RTM_DOT_TRACED for a traced light), as gl1's
 * rm_alias_shade and rm_alias_color give it with point light and
 * shadedot dot: mtl_rtm_color's indexes through the minlight table
 * (none when NULL) and the gamma table; alpha as rm_alias_color's. A
 * model rm_rtm_stock_light keeps is gl1's colour from the two functions.
 */
void rm_rtm_color(const float *light, float dot, int flags, int rdflags, float time, float overbright,
		const unsigned char *minlight, const unsigned char *gamma, unsigned char alpha, float *rgba);

/*
 * Overwrites the colour of the n vertices v (rm_alias_emit's) with the
 * tag of flags f (mtl_rtm_tag) and returns 1, only when extra_ok (the
 * model's normals were written, rm_gb_alias_extra); else leaves them and
 * returns 0, since a model without its normals is not traced.
 */
int rm_rtm_tag_verts(rm_avert_t *v, long n, unsigned f, int extra_ok);

/* the inputs of the view weapon's projection (rm_weapon_projection) and its jitter (RM_GB_JitterProj) */
typedef struct
{
	float fovy, gunfov, hand;
	int width, height;
	float znear;
	int farsee, underwater;
	float time, waterwarp;
	float jx, jy;               /* the frame's jitter in pixels; 0 0 for none */
} rm_rtm_weapon_t;

/*
 * The view weapon's view: as rm_rts_view, with the weapon's own
 * projection (r_gunfov, the left hand, the water warp) jittered as the
 * view's is (rm_gb_jitter on vp's size) in place of the view's. 0 when
 * glFrustum refuses the projection or the product cannot be inverted.
 */
int rm_rtm_weapon_view(const rm_rtm_weapon_t *w, const float *world, const rm_viewport_t *vp, const float *eye,
		rm_rts_view_t *out);

/* the world position of a weapon pixel (px, py) at stored depth d (0 .. 0.3), as rm_rts_unproject */
int rm_rtm_weapon_unproject(const rm_rts_view_t *v, float px, float py, float d, float *out);

#endif
