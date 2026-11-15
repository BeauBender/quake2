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
 * The shim's viewport conversion (mgl_compute_raster, gl1/metal/
 * mgl_raster.c) for the host test of ref_metal, behind a plain C
 * function, so the GL header the shim includes stays out of
 * rm_host_test.c. tests/check-ref-metal.sh links this file and
 * mgl_raster.c into the host test.
 *
 * =======================================================================
 */

#include <string.h>

#include "../../src/client/refresh/gl1/metal/mgl_internal.h"

/* What the shim's draw gets for glViewport(vx, vy, vw, vh) with gl1's 3D depth range 0 to 1 and no scissor. */
void
rm_host_shim_raster(int vx, int vy, int vw, int vh, int tw, int th, double *out)
{
	mgl_state_t s;
	mgl_raster_t r;

	memset(&s, 0, sizeof(s));
	s.viewport[0] = vx;
	s.viewport[1] = vy;
	s.viewport[2] = vw;
	s.viewport[3] = vh;
	s.depth_near = 0;
	s.depth_far = 1;
	mgl_compute_raster(&s, tw, th, &r);
	out[0] = r.x;
	out[1] = r.y;
	out[2] = r.w;
	out[3] = r.h;
	out[4] = r.znear;
	out[5] = r.zfar;
}

/*
 * What the shim's fragment function computes for gl1's multitexture
 * world draw (gl1_buffer.c, R_ApplyGLBuffer with buf_mtex): the colour
 * glColor4f(1, 1, 1, 1), unit 0 GL_REPLACE on the diffuse texel d
 * (opaque0: an RGB texture), unit 1 on the lightmap texel l, an RGBA
 * texture, with env1 named as ref_metal names it (MTL_LM_MODULATE,
 * _REPLACE, _COMBINE with GL_RGB_SCALE scale) and given to the shim as
 * its own MGL_TEXENV_*.
 */
void
rm_host_shim_world_lm(int env1, float scale, int opaque0, const float *d, const float *l, float *out)
{
	mgl_rgba_t c = { 1, 1, 1, 1 }, t0, t1;

	t0.r = d[0];
	t0.g = d[1];
	t0.b = d[2];
	t0.a = d[3];
	t1.r = l[0];
	t1.g = l[1];
	t1.b = l[2];
	t1.a = l[3];
	c = mgl_texenv_stage(MGL_TEXENV_REPLACE, opaque0, 1.0f, c, t0);
	c = mgl_texenv_stage(env1 == MTL_LM_REPLACE ? MGL_TEXENV_REPLACE :
			(env1 == MTL_LM_COMBINE ? MGL_TEXENV_COMBINE : MGL_TEXENV_MODULATE), 0, scale, c, t1);
	out[0] = c.r;
	out[1] = c.g;
	out[2] = c.b;
	out[3] = c.a;
}

/*
 * What the shim's fragment function computes for one GL_MODULATE unit
 * (gl1's warped water and translucent surfaces, gl1_buffer.c) on the
 * colour p and the texel t.
 */
void
rm_host_shim_modulate(const float *p, const float *t, float *out)
{
	mgl_rgba_t c, t0;

	c.r = p[0];
	c.g = p[1];
	c.b = p[2];
	c.a = p[3];
	t0.r = t[0];
	t0.g = t[1];
	t0.b = t[2];
	t0.a = t[3];
	c = mgl_texenv_stage(MGL_TEXENV_MODULATE, 0, 1.0f, c, t0);
	out[0] = c.r;
	out[1] = c.g;
	out[2] = c.b;
	out[3] = c.a;
}

/*
 * The shim's point arithmetic (mgl_point_size and mgl_point_quads of gl1/
 * metal/mgl_draw.c, linked with mgl_state.c and mgl_tex.c) for the
 * particle tests of ref_metal: the original functions, not a copy. The
 * layer functions mgl_draw.c and mgl_tex.c call are stubbed; nothing here
 * draws.
 */
#include "../../src/client/refresh/metal/header/rmb.h"

void
mgl_layer_update_raster(void)
{
}

void *
mgl_layer_tex_create(int w, int h, int levels)
{
	(void)w;
	(void)h;
	(void)levels;
	return NULL;
}

void
mgl_layer_tex_release(void *tex)
{
	(void)tex;
}

int
mgl_layer_tex_upload(void *tex, int level, int x, int y, int w, int h, const unsigned char *rgba, int gen_mips)
{
	(void)tex;
	(void)level;
	(void)x;
	(void)y;
	(void)w;
	(void)h;
	(void)rgba;
	(void)gen_mips;
	return 0;
}

int
mgl_layer_draw(const mgl_draw_t *d)
{
	(void)d;
	return 0;
}

/* the shim's limits, which rmb.h repeats */
void
rm_host_shim_point_limits(float *out)
{
	out[0] = MGL_POINT_ALIASED_MAX;
	out[1] = MGL_POINT_SMOOTH_MIN;
	out[2] = MGL_POINT_SMOOTH_MAX;
}

/* the state glPointSize, glPointParameterf and glPointParameterfv leave, as ref_metal keeps it */
static void
rm_host_shim_points_state(const rm_points_t *p)
{
	mgl_state_reset();
	glPointSize(p->size);
	glPointParameterf(GL_POINT_SIZE_MIN, p->min);
	glPointParameterf(GL_POINT_SIZE_MAX, p->max);
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, p->att);

	if (p->smooth)
	{
		glEnable(GL_POINT_SMOOTH);
	}
	else
	{
		glDisable(GL_POINT_SMOOTH);
	}

	mgl_state.viewport[0] = (GLint)p->vx;
	mgl_state.viewport[1] = (GLint)p->vy;
	mgl_state.viewport[2] = (GLint)p->vw;
	mgl_state.viewport[3] = (GLint)p->vh;
}

float
rm_host_shim_point_size(const rm_points_t *p, const float *eye)
{
	rm_host_shim_points_state(p);
	return mgl_point_size(&mgl_state, eye, p->smooth);
}

/*
 * mgl_point_quads on the points in (positions and colours) under the
 * matrices mv and proj; the corners' clip positions and their pt (the
 * offset, the radius, the smooth flag) go to pos and pt, the indices to
 * idx. The number of quads. n is at most 4096.
 */
long
rm_host_shim_point_quads(const rm_points_t *p, const float *mv, const float *proj, const rm_avert_t *in, long n,
		float (*pos)[4], float (*pt)[4], unsigned short *idx)
{
	static mgl_vertex_t pts[4096], quads[4 * 4096];
	size_t k, i;

	rm_host_shim_points_state(p);
	memcpy(mgl_state.modelview[0], mv, sizeof(mgl_state.modelview[0]));
	memcpy(mgl_state.projection[0], proj, sizeof(mgl_state.projection[0]));
	mgl_state.modelview_depth = mgl_state.projection_depth = 0;

	memset(pts, 0, sizeof(pts));

	for (i = 0; i < (size_t)n; i++)
	{
		memcpy(pts[i].pos, in[i].pos, sizeof(pts[i].pos));
		memcpy(pts[i].color, in[i].color, sizeof(pts[i].color));
	}

	k = mgl_point_quads(&mgl_state, pts, (size_t)n, quads, idx);

	for (i = 0; i < 4 * k; i++)
	{
		memcpy(pos[i], quads[i].pos, sizeof(pos[i]));
		memcpy(pt[i], quads[i].pt, sizeof(pt[i]));
	}

	return (long)k;
}

/* what glPointSize, glPointParameterf (the minimum, then the maximum) leave of GL's initial state: size, min, max */
void
rm_host_shim_point_set(float size, float min, float max, float *out)
{
	mgl_state_reset();
	glPointSize(size);
	glPointParameterf(GL_POINT_SIZE_MIN, min);
	glPointParameterf(GL_POINT_SIZE_MAX, max);
	out[0] = mgl_state.point_size;
	out[1] = mgl_state.point_size_min;
	out[2] = mgl_state.point_size_max;
}
