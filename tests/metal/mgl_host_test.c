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
 * Host test of the pure-C parts of the Metal shim (TASK47, TASK48), run
 * by tests/check-metal-shim.sh with the system compiler: no SDL, no
 * Metal. It covers mgl_read_contract, mgl_compute_raster, the readback
 * conversion, the matrix product and the upload unpacking
 * (mgl_raster.c), the validators, matrix stacks and glOrtho
 * of mgl_state.c, the texture table, uploads and sampler keys of
 * mgl_tex.c, and the vertex assembly, index expansion and state keys of
 * mgl_draw.c, with the last accepted and the first refused value at
 * each boundary. mgl_layer.m is replaced by the recording stubs below.
 *
 * =======================================================================
 */

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/gl1/metal/mgl_internal.h"

/* GL enums the shim's subset header leaves out, which it must refuse. */
#define TEST_GL_LIGHTING     0x0B50
#define TEST_GL_NORMAL_ARRAY 0x8075
#define TEST_GL_VIEWPORT     0x0BA2

/* mgl_layer.m is not linked: the stubs record what the shim asked of it. */
static int raster_updates;

void
mgl_layer_update_raster(void)
{
	raster_updates++;
}

int
mgl_layer_ready(void)
{
	return 0;
}

static int tex_creates, tex_releases, tex_uploads, tex_create_fails, draws;
static int last_levels, last_up_level, last_up_x, last_up_y, last_up_w, last_up_h, last_up_gen;
static unsigned char last_up_rgba[64];
static mgl_draw_t last_draw;

void *
mgl_layer_tex_create(int w, int h, int levels)
{
	(void)w; (void)h;

	if (tex_create_fails)
	{
		return NULL;
	}

	tex_creates++;
	last_levels = levels;
	return (void *)(uintptr_t)(0x1000 + tex_creates);
}

void
mgl_layer_tex_release(void *tex)
{
	(void)tex;
	tex_releases++;
}

int
mgl_layer_tex_upload(void *tex, int level, int x, int y, int w, int h,
		const unsigned char *rgba, int gen_mips)
{
	size_t n = (size_t)w * (size_t)h * 4;

	(void)tex;
	tex_uploads++;
	last_up_level = level;
	last_up_x = x;
	last_up_y = y;
	last_up_w = w;
	last_up_h = h;
	last_up_gen = gen_mips;
	memcpy(last_up_rgba, rgba, n < sizeof(last_up_rgba) ? n : sizeof(last_up_rgba));
	return 1;
}

int
mgl_layer_draw(const mgl_draw_t *d)
{
	draws++;
	last_draw = *d;
	return 1;
}

static int failures, checks;

#define CHECK(cond) \
	do { checks++; if (!(cond)) { failures++; \
		fprintf(stderr, "mgl_host_test:%d: FAIL %s\n", __LINE__, #cond); } } while (0)

/* The error a call left, read and cleared as glGetError does. */
#define ERR() glGetError()

static void
test_read_contract(void)
{
	size_t bpp, row, total;

	/* the whole target, RGB, alignment 1: no padding */
	CHECK(mgl_read_contract(0, 0, 640, 480, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_NO_ERROR);
	CHECK(bpp == 3 && row == 1920 && total == 921600);

	/* padding on every row but the last */
	CHECK(mgl_read_contract(0, 0, 3, 2, GL_RGB, GL_UNSIGNED_BYTE, 4, 8, 8,
			&bpp, &row, &total) == GL_NO_ERROR);
	CHECK(bpp == 3 && row == 12 && total == 21);
	CHECK(mgl_read_contract(0, 0, 3, 2, GL_RGBA, GL_UNSIGNED_BYTE, 8, 8, 8,
			&bpp, &row, &total) == GL_NO_ERROR);
	CHECK(bpp == 4 && row == 16 && total == 28);
	CHECK(mgl_read_contract(0, 0, 1, 1, GL_BGRA, GL_UNSIGNED_BYTE, 2, 8, 8,
			&bpp, &row, &total) == GL_NO_ERROR && total == 4);

	/* the rectangle: the last column and row accepted, one past refused */
	CHECK(mgl_read_contract(639, 479, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_NO_ERROR && total == 3);
	CHECK(mgl_read_contract(640, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE && total == 0);
	CHECK(mgl_read_contract(0, 480, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	CHECK(mgl_read_contract(0, 0, 641, 480, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	CHECK(mgl_read_contract(0, 0, 640, 481, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	CHECK(mgl_read_contract(-1, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	CHECK(mgl_read_contract(0, -1, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	/* x + w would wrap in 32 bits */
	CHECK(mgl_read_contract(INT_MAX, 0, INT_MAX, 1, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	CHECK(mgl_read_contract(0, INT_MAX, 1, INT_MAX, GL_RGB, GL_UNSIGNED_BYTE, 1, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);

	/* sizes: zero reads nothing, negative is refused */
	CHECK(mgl_read_contract(0, 0, 0, 0, GL_RGB, GL_UNSIGNED_BYTE, 4, 640, 480,
			&bpp, &row, &total) == GL_NO_ERROR && total == 0);
	CHECK(mgl_read_contract(640, 480, 0, 0, GL_RGB, GL_UNSIGNED_BYTE, 4, 640, 480,
			&bpp, &row, &total) == GL_NO_ERROR && total == 0);
	CHECK(mgl_read_contract(0, 0, -1, 1, GL_RGB, GL_UNSIGNED_BYTE, 4, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	CHECK(mgl_read_contract(0, 0, 1, -1, GL_RGB, GL_UNSIGNED_BYTE, 4, 640, 480,
			&bpp, &row, &total) == GL_INVALID_VALUE);

	/* format, type, alignment */
	CHECK(mgl_read_contract(0, 0, 1, 1, GL_ALPHA, GL_UNSIGNED_BYTE, 4, 8, 8,
			&bpp, &row, &total) == GL_INVALID_ENUM && bpp == 0);
	CHECK(mgl_read_contract(0, 0, 1, 1, GL_RGB, GL_FLOAT, 4, 8, 8,
			&bpp, &row, &total) == GL_INVALID_ENUM && bpp == 0);
	CHECK(mgl_read_contract(0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 3, 8, 8,
			&bpp, &row, &total) == GL_INVALID_VALUE && total == 0);
	CHECK(mgl_read_contract(0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 16, 8, 8,
			&bpp, &row, &total) == GL_INVALID_VALUE);
	CHECK(mgl_read_contract(0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, 0, 8, 8,
			&bpp, &row, &total) == GL_INVALID_VALUE);
}

static void
test_raster(void)
{
	mgl_raster_t r;

	mgl_state_reset();

	/* the viewport flips from GL's bottom-left origin to Metal's top-left */
	mgl_state.viewport[0] = 5;
	mgl_state.viewport[1] = 10;
	mgl_state.viewport[2] = 200;
	mgl_state.viewport[3] = 100;
	mgl_state.depth_near = 0.25;
	mgl_state.depth_far = 0.75;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.x == 5 && r.y == 370 && r.w == 200 && r.h == 100);
	CHECK(r.znear == 0.25 && r.zfar == 0.75);

	/* scissor off: the whole target */
	mgl_state.scissor[0] = 1;
	mgl_state.scissor[1] = 2;
	mgl_state.scissor[2] = 3;
	mgl_state.scissor[3] = 4;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sx == 0 && r.sy == 0 && r.sw == 640 && r.sh == 480 && !r.scissor_empty);

	/* scissor on: flipped */
	mgl_state.enables |= MGL_EN_SCISSOR_TEST;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sx == 1 && r.sy == 474 && r.sw == 3 && r.sh == 4 && !r.scissor_empty);

	/* exactly the target, then one pixel past it on every side: clamped */
	mgl_state.scissor[0] = 0;
	mgl_state.scissor[1] = 0;
	mgl_state.scissor[2] = 640;
	mgl_state.scissor[3] = 480;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sx == 0 && r.sy == 0 && r.sw == 640 && r.sh == 480 && !r.scissor_empty);
	mgl_state.scissor[0] = -1;
	mgl_state.scissor[1] = -1;
	mgl_state.scissor[2] = 642;
	mgl_state.scissor[3] = 482;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sx == 0 && r.sy == 0 && r.sw == 640 && r.sh == 480 && !r.scissor_empty);

	/* the last column inside, then the first outside */
	mgl_state.scissor[0] = 639;
	mgl_state.scissor[1] = 0;
	mgl_state.scissor[2] = 10;
	mgl_state.scissor[3] = 1;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sx == 639 && r.sy == 479 && r.sw == 1 && r.sh == 1 && !r.scissor_empty);
	mgl_state.scissor[0] = 640;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sw == 0 && r.scissor_empty);

	/* the top row inside, then the first row above the target */
	mgl_state.scissor[0] = 0;
	mgl_state.scissor[1] = 479;
	mgl_state.scissor[2] = 1;
	mgl_state.scissor[3] = 5;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sy == 0 && r.sh == 1 && !r.scissor_empty);
	mgl_state.scissor[1] = 480;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sh == 0 && r.scissor_empty);

	/* an empty scissor rectangle */
	mgl_state.scissor[1] = 0;
	mgl_state.scissor[2] = 0;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.scissor_empty);

	/* huge values do not wrap */
	mgl_state.scissor[0] = INT_MAX;
	mgl_state.scissor[1] = INT_MIN;
	mgl_state.scissor[2] = INT_MAX;
	mgl_state.scissor[3] = INT_MAX;
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.sx == 640 && r.sw == 0 && r.scissor_empty);
}

static void
test_state(void)
{
	int before;

	mgl_state_reset();
	CHECK(ERR() == GL_NO_ERROR);

	/* the first error sticks until glGetError */
	glDepthFunc(GL_NEVER - 1);
	glCullFace(0);
	glPointSize(0.0f);
	CHECK(ERR() == GL_INVALID_ENUM);
	CHECK(ERR() == GL_NO_ERROR);

	/* capabilities; the scissor bit asks for a new raster */
	before = raster_updates;
	glEnable(GL_SCISSOR_TEST);
	CHECK(ERR() == GL_NO_ERROR && (mgl_state.enables & MGL_EN_SCISSOR_TEST) && raster_updates == before + 1);
	glDisable(GL_SCISSOR_TEST);
	CHECK(!(mgl_state.enables & MGL_EN_SCISSOR_TEST) && raster_updates == before + 2);
	glEnable(GL_BLEND);
	CHECK(ERR() == GL_NO_ERROR && (mgl_state.enables & MGL_EN_BLEND) && raster_updates == before + 2);
	glEnable(TEST_GL_LIGHTING);
	CHECK(ERR() == GL_INVALID_ENUM);

	/* comparison functions: GL_NEVER .. GL_ALWAYS */
	glDepthFunc(GL_ALWAYS);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.depth_func == GL_ALWAYS);
	glDepthFunc(GL_NEVER);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.depth_func == GL_NEVER);
	glDepthFunc(GL_ALWAYS + 1);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.depth_func == GL_NEVER);
	glDepthFunc(GL_NEVER - 1);
	CHECK(ERR() == GL_INVALID_ENUM);
	glAlphaFunc(GL_GREATER, 2.0f);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.alpha_func == GL_GREATER && mgl_state.alpha_ref == 1.0f);
	glAlphaFunc(GL_ALWAYS + 1, 0.5f);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.alpha_ref == 1.0f);
	glStencilFunc(GL_ALWAYS + 1, 1, 1);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.stencil_func == GL_ALWAYS);

	/* blend factors: source-only and destination-only ones */
	glBlendFunc(GL_SRC_ALPHA_SATURATE, GL_ONE_MINUS_SRC_COLOR);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.blend_src == GL_SRC_ALPHA_SATURATE);
	glBlendFunc(GL_SRC_COLOR, GL_ONE);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.blend_src == GL_SRC_ALPHA_SATURATE);
	glBlendFunc(GL_ONE, GL_DST_COLOR);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.blend_dst == GL_ONE_MINUS_SRC_COLOR);

	/* stencil ops: all three are checked before any is stored */
	glStencilOp(GL_INVERT, GL_DECR, GL_INCR);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.stencil_zpass == GL_INCR);
	glStencilOp(GL_KEEP, GL_KEEP, GL_ALWAYS);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.stencil_fail == GL_INVERT);

	/* point size: the smallest positive accepted, zero refused */
	glPointSize(1e-6f);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.point_size == 1e-6f);
	glPointSize(0.0f);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.point_size == 1e-6f);
	glPointParameterf(GL_POINT_SIZE_MIN, 0.0f);
	CHECK(ERR() == GL_NO_ERROR);
	glPointParameterf(GL_POINT_SIZE_MAX, -1e-6f);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.point_size_max == MGL_POINT_ALIASED_MAX);
	glPointParameterfv(GL_POINT_SIZE_MAX, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);

	/* texture units: GL_TEXTURE1 is the last, GL_TEXTURE2 the first refused */
	glActiveTexture(GL_TEXTURE0 + MGL_MAX_UNITS - 1);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.active_unit == MGL_MAX_UNITS - 1);
	glActiveTexture(GL_TEXTURE0 + MGL_MAX_UNITS);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.active_unit == MGL_MAX_UNITS - 1);
	glActiveTexture(GL_TEXTURE0 - 1);
	CHECK(ERR() == GL_INVALID_ENUM);
	glClientActiveTexture(GL_TEXTURE0 + MGL_MAX_UNITS);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_state.client_unit == 0);

	/* texture environment */
	glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 4);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.unit[MGL_MAX_UNITS - 1].rgb_scale == 4);
	glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 3);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.unit[MGL_MAX_UNITS - 1].rgb_scale == 4);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
	CHECK(ERR() == GL_INVALID_ENUM);
	glTexEnvi(GL_TEXTURE_2D, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	CHECK(ERR() == GL_INVALID_ENUM);

	/* client arrays: size and stride bounds */
	glVertexPointer(4, GL_FLOAT, 0, NULL);
	CHECK(ERR() == GL_NO_ERROR);
	glVertexPointer(5, GL_FLOAT, 0, NULL);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.vertex_array.size == 4);
	glVertexPointer(2, GL_SHORT, 0, NULL);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.vertex_array.size == 2);
	glVertexPointer(1, GL_FLOAT, 0, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
	glVertexPointer(3, GL_FLOAT, -1, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
	glVertexPointer(3, GL_UNSIGNED_BYTE, 0, NULL);
	CHECK(ERR() == GL_INVALID_ENUM);
	glColorPointer(3, GL_UNSIGNED_BYTE, 0, NULL);
	CHECK(ERR() == GL_NO_ERROR);
	glColorPointer(2, GL_UNSIGNED_BYTE, 0, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
	glTexCoordPointer(1, GL_FLOAT, 0, NULL);
	CHECK(ERR() == GL_NO_ERROR);
	glTexCoordPointer(0, GL_FLOAT, 0, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
	glEnableClientState(TEST_GL_NORMAL_ARRAY);
	CHECK(ERR() == GL_INVALID_ENUM);

	/* glPixelStorei: alignments 1, 2, 4, 8 */
	glPixelStorei(GL_PACK_ALIGNMENT, 8);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.pack_alignment == 8);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.pack_alignment == 1);
	glPixelStorei(GL_PACK_ALIGNMENT, 16);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.pack_alignment == 1);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.unpack_alignment == 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, -1);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.unpack_row_length == 0);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	CHECK(ERR() == GL_NO_ERROR);

	/* glMatrixMode */
	glMatrixMode(GL_TEXTURE);
	CHECK(ERR() == GL_INVALID_OPERATION && mgl_state.matrix_mode == GL_MODELVIEW);
	glMatrixMode(0);
	CHECK(ERR() == GL_INVALID_ENUM);
}

static void
test_matrix_stack(void)
{
	GLfloat m[16], got[16];
	int i;

	mgl_state_reset();

	for (i = 0; i < 16; i++)
	{
		m[i] = (GLfloat)(i + 1);
	}

	/* push copies the top; pop restores the one below */
	glLoadMatrixf(m);
	glPushMatrix();
	CHECK(ERR() == GL_NO_ERROR && mgl_state.modelview_depth == 1);
	glGetFloatv(GL_MODELVIEW_MATRIX, got);
	CHECK(memcmp(got, m, sizeof(m)) == 0);
	glLoadIdentity();
	glGetFloatv(GL_MODELVIEW_MATRIX, got);
	CHECK(got[0] == 1.0f && got[1] == 0.0f && got[15] == 1.0f);
	glPopMatrix();
	glGetFloatv(GL_MODELVIEW_MATRIX, got);
	CHECK(ERR() == GL_NO_ERROR && memcmp(got, m, sizeof(m)) == 0);

	/* the last push that fits, then the first that overflows */
	for (i = 1; i < MGL_STACK_DEPTH; i++)
	{
		glPushMatrix();
	}

	CHECK(ERR() == GL_NO_ERROR && mgl_state.modelview_depth == MGL_STACK_DEPTH - 1);
	glPushMatrix();
	CHECK(ERR() == GL_STACK_OVERFLOW && mgl_state.modelview_depth == MGL_STACK_DEPTH - 1);
	glGetFloatv(GL_MODELVIEW_MATRIX, got);
	CHECK(memcmp(got, m, sizeof(m)) == 0);

	/* the stacks are separate */
	glMatrixMode(GL_PROJECTION);
	CHECK(mgl_state.projection_depth == 0);
	glPopMatrix();
	CHECK(ERR() == GL_STACK_UNDERFLOW && mgl_state.projection_depth == 0);
	glPushMatrix();
	CHECK(ERR() == GL_NO_ERROR && mgl_state.projection_depth == 1);
	glPopMatrix();
	CHECK(ERR() == GL_NO_ERROR && mgl_state.projection_depth == 0);

	/* pop to the bottom, then the first pop that underflows */
	glMatrixMode(GL_MODELVIEW);

	for (i = 1; i < MGL_STACK_DEPTH; i++)
	{
		glPopMatrix();
	}

	CHECK(ERR() == GL_NO_ERROR && mgl_state.modelview_depth == 0);
	glPopMatrix();
	CHECK(ERR() == GL_STACK_UNDERFLOW && mgl_state.modelview_depth == 0);

	glLoadMatrixf(NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
	glGetFloatv(GL_MODELVIEW_MATRIX, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
	got[0] = 42.0f;
	glGetFloatv(TEST_GL_VIEWPORT, got);
	CHECK(ERR() == GL_INVALID_ENUM && got[0] == 42.0f);

	/* what the shim never implements is refused and counted */
	glColorTableEXT(GL_TEXTURE_2D, GL_RGBA, 256, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CHECK(ERR() == GL_INVALID_OPERATION && mgl_unimplemented_calls() == 1);
}

/* (x, y) through a column-major matrix, as GL does with z 0 and w 1. */
static void
xform(const GLfloat *m, float x, float y, float out[4])
{
	int r;

	for (r = 0; r < 4; r++)
	{
		out[r] = m[r] * x + m[4 + r] * y + m[12 + r];
	}
}

/* (x, y, z, 1) through a column-major matrix. */
static void
xform3(const GLfloat *m, float x, float y, float z, float out[4])
{
	int r;

	for (r = 0; r < 4; r++)
	{
		out[r] = m[r] * x + m[4 + r] * y + m[8 + r] * z + m[12 + r];
	}
}

/* Equal to a few float ulps, relative to the size of the value. */
static int
near1(float v, float want)
{
	float e = 1e-5f * (1.0f + (want < 0 ? -want : want));

	return v > want - e && v < want + e;
}

static int
near4(const float *v, float a, float b, float c, float d)
{
	return near1(v[0], a) && near1(v[1], b) && near1(v[2], c) && near1(v[3], d);
}

/* glFrustum, glRotatef, glTranslatef and glScalef against values worked out by hand. */
static void
test_perspective(void)
{
	GLfloat *top;
	float v[4];

	mgl_state_reset();
	top = mgl_state.modelview[0];

	/* the near plane maps to NDC z -1, the far one to 1; an off-centre volume shifts x and y */
	glMatrixMode(GL_PROJECTION);
	glFrustum(-1, 1, -1, 1, 1, 3);
	CHECK(ERR() == GL_NO_ERROR);
	xform3(mgl_state.projection[0], 0, 0, -1, v);
	CHECK(near4(v, 0, 0, -1, 1));
	xform3(mgl_state.projection[0], 0, 0, -3, v);
	CHECK(near4(v, 0, 0, 3, 3));
	xform3(mgl_state.projection[0], 1, -1, -1, v);
	CHECK(near4(v, 1, -1, -1, 1));
	glLoadIdentity();
	glFrustum(0, 2, -1, 3, 2, 4);
	xform3(mgl_state.projection[0], 2, 3, -2, v);
	CHECK(near4(v, 2, 2, -2, 2));
	CHECK(mgl_state.projection[0][8] == 1.0f && mgl_state.projection[0][9] == 0.5f &&
		mgl_state.projection[0][0] == 2.0f && mgl_state.projection[0][5] == 1.0f);
	CHECK(mgl_state.projection[0][14] == -8.0f && mgl_state.projection[0][15] == 0.0f);

	/* it multiplies the current matrix; volumes GL refuses leave it alone */
	glLoadIdentity();
	mgl_state.projection[0][0] = 3.0f;
	glFrustum(-1, 1, -1, 1, 1, 3);
	CHECK(mgl_state.projection[0][0] == 3.0f && mgl_state.projection[0][5] == 1.0f);
	glFrustum(-1, 1, -1, 1, 0, 3);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.projection[0][0] == 3.0f);
	glFrustum(-1, 1, -1, 1, 1, -3);
	CHECK(ERR() == GL_INVALID_VALUE);
	glFrustum(1, 1, -1, 1, 1, 3);
	CHECK(ERR() == GL_INVALID_VALUE);
	glFrustum(-1, 1, 1, 1, 1, 3);
	CHECK(ERR() == GL_INVALID_VALUE);
	glFrustum(-1, 1, -1, 1, 2, 2);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.projection[0][0] == 3.0f);
	glMatrixMode(GL_MODELVIEW);

	/* a quarter turn about each axis, and about an axis that is not unit length */
	glRotatef(90, 0, 0, 1);
	xform3(top, 1, 0, 0, v);
	CHECK(near4(v, 0, 1, 0, 1));
	glLoadIdentity();
	glRotatef(90, 2, 0, 0);
	xform3(top, 0, 1, 0, v);
	CHECK(near4(v, 0, 0, 1, 1));
	glLoadIdentity();
	glRotatef(-90, 0, 1, 0);
	xform3(top, 1, 0, 0, v);
	CHECK(near4(v, 0, 0, 1, 1));
	glLoadIdentity();
	/* a third of a turn about the diagonal cycles the axes */
	glRotatef(120, 1, 1, 1);
	xform3(top, 1, 0, 0, v);
	CHECK(near4(v, 0, 1, 0, 1));
	xform3(top, 0, 0, 1, v);
	CHECK(near4(v, 1, 0, 0, 1));
	xform3(top, 0, 1, 0, v);
	CHECK(near4(v, 0, 0, 1, 1));
	glLoadIdentity();
	glRotatef(45, 0, 0, 0);
	CHECK(ERR() == GL_NO_ERROR && top[0] == 1.0f && top[1] == 0.0f && top[5] == 1.0f);

	/* translate then scale: the scale applies first */
	glTranslatef(1, 2, 3);
	glScalef(2, 3, 4);
	xform3(top, 1, 1, 1, v);
	CHECK(near4(v, 3, 5, 7, 1));
	CHECK(top[12] == 1.0f && top[13] == 2.0f && top[14] == 3.0f && top[10] == 4.0f);
	/* and the other way round: the translation is scaled */
	glLoadIdentity();
	glScalef(2, 3, 4);
	glTranslatef(1, 1, 1);
	xform3(top, 0, 0, 0, v);
	CHECK(near4(v, 2, 3, 4, 1));

	/*
	 * gl1's R_PolyBlend: Quake's forward (x) becomes the eye's -z, left
	 * (y) its -x and up (z) its y, so the quad 10 units ahead covers
	 * every corner of the screen through gl1's projection.
	 */
	glLoadIdentity();
	glRotatef(-90, 1, 0, 0);
	glRotatef(90, 0, 0, 1);
	xform3(top, 10, 100, 100, v);
	CHECK(near4(v, -100, 100, -10, 1));
	xform3(top, 10, -100, -100, v);
	CHECK(near4(v, 100, -100, -10, 1));
	{
		/* R_SetPerspective of gl1_main.c at fov_y 73.74, 4:3, zNear 4, zFar 4096 */
		double ymax = 4 * tan(73.74 * M_PI / 360.0), xmax = ymax * 4.0 / 3.0;
		GLfloat mvp[16];

		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glFrustum(-xmax, xmax, -ymax, ymax, 4, 4096);
		mgl_mat_mul(mvp, mgl_state.projection[0], top);
		xform3(mvp, 10, 100, 100, v);
		CHECK(v[3] > 9.99f && v[3] < 10.01f && v[0] < -v[3] && v[1] > v[3] && v[2] > -v[3] && v[2] < v[3]);
		xform3(mvp, 10, -100, -100, v);
		CHECK(v[0] > v[3] && v[1] < -v[3]);
		glMatrixMode(GL_MODELVIEW);
	}
}

static void
test_matrices(void)
{
	GLfloat a[16], b[16], p[16];
	float v[4];
	int i;

	/* a product of two non-symmetric matrices, and in place */
	for (i = 0; i < 16; i++)
	{
		a[i] = (GLfloat)(i + 1);
		b[i] = (GLfloat)(16 - i);
	}

	mgl_mat_mul(p, a, b);
	/* row 0 of a is 1 5 9 13, column 0 of b is 16 15 14 13; row 1 of a by column 3 of b */
	CHECK(p[0] == 1 * 16 + 5 * 15 + 9 * 14 + 13 * 13);
	CHECK(p[13] == 2 * 4 + 6 * 3 + 10 * 2 + 14 * 1);
	CHECK(p[4] == 1 * 12 + 5 * 11 + 9 * 10 + 13 * 9);
	mgl_mat_mul(a, a, b);
	CHECK(memcmp(a, p, sizeof(p)) == 0);

	/* gl1's 2D projection: the top left of the screen is NDC (-1, 1), the bottom right (1, -1) */
	mgl_state_reset();
	glMatrixMode(GL_PROJECTION);
	glOrtho(0, 640, 480, 0, -99999, 99999);
	CHECK(ERR() == GL_NO_ERROR);
	xform(mgl_state.projection[0], 0, 0, v);
	CHECK(v[0] == -1.0f && v[1] == 1.0f && v[2] == 0.0f && v[3] == 1.0f);
	xform(mgl_state.projection[0], 640, 480, v);
	CHECK(v[0] == 1.0f && v[1] == -1.0f);
	xform(mgl_state.projection[0], 160, 360, v);
	CHECK(v[0] > -0.5f - 1e-6f && v[0] < -0.5f + 1e-6f && v[1] > -0.5f - 1e-6f && v[1] < -0.5f + 1e-6f);
	CHECK(mgl_state.projection[0][10] == (GLfloat)(-2.0 / 199998.0));
	CHECK(mgl_state.projection[0][14] == 0.0f);

	/* it multiplies the current matrix: ortho onto a scale by 2 */
	glLoadIdentity();
	mgl_state.projection[0][0] = 2.0f;
	glOrtho(0, 2, 0, 2, 1, 3);
	CHECK(mgl_state.projection[0][0] == 2.0f && mgl_state.projection[0][12] == -2.0f);
	CHECK(mgl_state.projection[0][13] == -1.0f && mgl_state.projection[0][14] == -2.0f);
	CHECK(mgl_state.projection[0][10] == -1.0f);

	/* an empty volume on any axis */
	glOrtho(1, 1, 0, 1, 0, 1);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.projection[0][0] == 2.0f);
	glOrtho(0, 1, 1, 1, 0, 1);
	CHECK(ERR() == GL_INVALID_VALUE);
	glOrtho(0, 1, 0, 1, 2, 2);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.projection[0][0] == 2.0f);

	test_perspective();
}

/*
 * The readback conversion on content that is not uniform in any
 * direction: Metal's BGRA rows top down become GL's rows bottom up in
 * RGB, RGBA or BGRA, and the padding the pack alignment adds is never
 * written.
 */
static void
test_readback_convert(void)
{
	/* 3 x 2 pixels, top row first: each pixel is (B, G, R, A) = (10r+c, 100+10r+c, 200+10r+c, 50+c) */
	unsigned char bgra[2 * 3 * 4];
	unsigned char out[64];
	size_t bpp, row, total;
	int r, c, ok;

	for (r = 0; r < 2; r++)
	{
		for (c = 0; c < 3; c++)
		{
			unsigned char *p = bgra + (r * 3 + c) * 4;

			p[0] = (unsigned char)(10 * r + c);
			p[1] = (unsigned char)(100 + 10 * r + c);
			p[2] = (unsigned char)(200 + 10 * r + c);
			p[3] = (unsigned char)(50 + c);
		}
	}

	/* RGB at alignment 4: 9 bytes a row, 12 apart; 3 padding bytes after the first row only */
	CHECK(mgl_read_contract(0, 0, 3, 2, GL_RGB, GL_UNSIGNED_BYTE, 4, 3, 2, &bpp, &row, &total) == GL_NO_ERROR);
	memset(out, 0xEE, sizeof(out));
	mgl_convert_readback(bgra, 3, 2, GL_RGB, bpp, row, out);

	ok = 1;

	for (r = 0; r < 2; r++)
	{
		/* GL row 0 is the bottom: Metal row 1 */
		int mr = 1 - r;

		for (c = 0; c < 3; c++)
		{
			const unsigned char *o = out + r * 12 + c * 3;

			ok &= o[0] == 200 + 10 * mr + c && o[1] == 100 + 10 * mr + c && o[2] == 10 * mr + c;
		}
	}

	CHECK(ok);
	CHECK(out[9] == 0xEE && out[10] == 0xEE && out[11] == 0xEE);
	CHECK(out[21] == 0xEE && out[total] == 0xEE);

	/* RGBA at alignment 8: 12 bytes a row, 16 apart, alpha kept */
	CHECK(mgl_read_contract(0, 0, 3, 2, GL_RGBA, GL_UNSIGNED_BYTE, 8, 3, 2, &bpp, &row, &total) == GL_NO_ERROR);
	memset(out, 0xEE, sizeof(out));
	mgl_convert_readback(bgra, 3, 2, GL_RGBA, bpp, row, out);
	CHECK(out[0] == 210 && out[1] == 110 && out[2] == 10 && out[3] == 50);
	CHECK(out[8] == 212 && out[11] == 52);
	CHECK(out[12] == 0xEE && out[15] == 0xEE);
	CHECK(out[16] == 200 && out[17] == 100 && out[18] == 0 && out[19] == 50);
	CHECK(out[16 + 11] == 52 && out[28] == 0xEE);

	/* BGRA: Metal's bytes, only the rows reordered */
	CHECK(mgl_read_contract(0, 0, 3, 2, GL_BGRA, GL_UNSIGNED_BYTE, 1, 3, 2, &bpp, &row, &total) == GL_NO_ERROR);
	memset(out, 0xEE, sizeof(out));
	mgl_convert_readback(bgra, 3, 2, GL_BGRA, bpp, row, out);
	CHECK(memcmp(out, bgra + 12, 12) == 0 && memcmp(out + 12, bgra, 12) == 0 && out[24] == 0xEE);
}

static void
test_unpack(void)
{
	/* RGB, 3 x 2, alignment 4: source rows 12 bytes apart, the 3 padding bytes are not pixels */
	static const unsigned char rgb[] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 0xAA, 0xAA, 0xAA,
		11, 12, 13, 14, 15, 16, 17, 18, 19
	};
	/* RGBA, 2 x 2 taken from rows 3 pixels long */
	static const unsigned char rgba[] = {
		1, 2, 3, 4, 5, 6, 7, 8, 0xAA, 0xAA, 0xAA, 0xAA,
		9, 10, 11, 12, 13, 14, 15, 16, 0xAA, 0xAA, 0xAA, 0xAA
	};
	unsigned char out[32];

	memset(out, 0, sizeof(out));
	mgl_unpack_rgba(rgb, GL_RGB, 3, 2, 4, 0, 0, out);
	CHECK(out[0] == 1 && out[2] == 3 && out[3] == 255 && out[4] == 4 && out[8] == 7 && out[11] == 255);
	CHECK(out[12] == 11 && out[14] == 13 && out[15] == 255 && out[20] == 17 && out[22] == 19);

	mgl_unpack_rgba(rgba, GL_RGBA, 2, 2, 4, 3, 0, out);
	CHECK(out[3] == 4 && out[7] == 8 && out[8] == 9 && out[15] == 16);
	mgl_unpack_rgba(rgba, GL_RGBA, 2, 2, 4, 3, 1, out);
	CHECK(out[3] == 255 && out[2] == 3 && out[15] == 255 && out[14] == 15);

	/* alignment 8 on a 2-pixel RGB row: 6 bytes padded to 8 */
	mgl_unpack_rgba(rgb, GL_RGB, 2, 2, 8, 0, 0, out);
	CHECK(out[4] == 4 && out[8] == 9 && out[9] == 0xAA);

}

static void
test_textures(void)
{
	static const unsigned char px[2 * 2 * 4] = {
		1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
	};
	mgl_texture_t *t;
	GLuint name;

	mgl_state_reset();
	mgl_textures_reset();
	tex_creates = tex_releases = tex_uploads = 0;

	/* the table: the last name is bound, the first past it refused and the binding kept */
	glBindTexture(GL_TEXTURE_2D, MGL_MAX_TEXTURES - 1);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.unit[0].bound == MGL_MAX_TEXTURES - 1);
	glBindTexture(GL_TEXTURE_2D, MGL_MAX_TEXTURES);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.unit[0].bound == MGL_MAX_TEXTURES - 1);
	CHECK(mgl_texture(MGL_MAX_TEXTURES) == NULL && mgl_texture(MGL_MAX_TEXTURES - 1) != NULL);
	glBindTexture(GL_TEXTURE_2D + 1, 5);
	CHECK(ERR() == GL_INVALID_ENUM);

	/* the initial parameters, and their sampler key */
	name = 1412;
	glBindTexture(GL_TEXTURE_2D, name);
	t = mgl_texture(name);
	CHECK(t->min_filter == GL_NEAREST_MIPMAP_LINEAR && t->mag_filter == GL_LINEAR && t->wrap_s == GL_REPEAT);
	CHECK(mgl_sampler_key(t) == (4u | 8u));

	/* level 0 creates the storage, with the whole mip chain, and uploads */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_NO_ERROR && t->mtl && tex_creates == 1 && last_levels == 2 && !t->opaque);
	CHECK(tex_uploads == 1 && last_up_level == 0 && last_up_w == 2 && last_up_h == 2 && !last_up_gen);
	CHECK(memcmp(last_up_rgba, px, sizeof(px)) == 0);

	/* the same size again keeps the storage; an RGB internal format makes it opaque */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_NO_ERROR && tex_creates == 1 && t->opaque && last_up_rgba[3] == 255 && last_up_rgba[0] == 1);

	/* GL_GENERATE_MIPMAP asks the layer to fill the chain from level 0 */
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, 4, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_NO_ERROR && last_up_gen && !t->opaque);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 0);

	/* another size replaces the storage */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 5, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CHECK(ERR() == GL_NO_ERROR && tex_creates == 2 && tex_releases == 1 && last_levels == 3);
	CHECK(t->width == 5 && t->height == 3 && tex_uploads == 3);

	/* levels above 0 fill the chain of the 5 x 3 level 0: 2 x 1, then 1 x 1, never mipmapped */
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 1);
	glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_NO_ERROR && tex_uploads == 4 && last_up_level == 1 && last_up_w == 2 &&
		last_up_h == 1 && !last_up_gen && tex_creates == 2);
	CHECK(memcmp(last_up_rgba, px, 8) == 0);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 0);
	glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_NO_ERROR && tex_uploads == 5 && last_up_level == 2);
	glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CHECK(ERR() == GL_NO_ERROR && tex_uploads == 5);
	/* past the chain, or a size that is not the level's: not modelled, refused, nothing uploaded */
	glTexImage2D(GL_TEXTURE_2D, 3, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_OPERATION && tex_uploads == 5);
	glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 3, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_OPERATION && tex_uploads == 5);
	glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_OPERATION && tex_uploads == 5);
	glBindTexture(GL_TEXTURE_2D, 1413);
	glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_OPERATION && tex_uploads == 5 && !mgl_texture(1413)->mtl);
	glBindTexture(GL_TEXTURE_2D, name);

	/* what the shim refuses: the paletted path, other types, a border, the size limit */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 2, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_ENUM);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_ENUM && t->width == 5);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_FLOAT, px);
	CHECK(ERR() == GL_INVALID_ENUM);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_VALUE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, MGL_MAX_TEXTURE_SIZE, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CHECK(ERR() == GL_NO_ERROR && t->width == MGL_MAX_TEXTURE_SIZE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, MGL_MAX_TEXTURE_SIZE + 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CHECK(ERR() == GL_INVALID_VALUE && t->height == 1);
	glTexImage2D(GL_TEXTURE_2D, -1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);

	/* storage that cannot be made: GL_OUT_OF_MEMORY, the old storage kept */
	tex_create_fails = 1;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CHECK(ERR() == GL_OUT_OF_MEMORY && t->width == MGL_MAX_TEXTURE_SIZE && t->mtl);
	tex_create_fails = 0;

	/* sub-images of the MGL_MAX_TEXTURE_SIZE x 1 storage: the last pixel that fits, then one past */
	{
		int up = tex_uploads;

		glTexSubImage2D(GL_TEXTURE_2D, 0, MGL_MAX_TEXTURE_SIZE - 1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_NO_ERROR && tex_uploads == up + 1 && last_up_x == MGL_MAX_TEXTURE_SIZE - 1 &&
			last_up_y == 0 && last_up_w == 1 && last_up_h == 1 && last_up_level == 0 && !last_up_gen);
		CHECK(memcmp(last_up_rgba, px, 4) == 0);
		glTexSubImage2D(GL_TEXTURE_2D, 0, MGL_MAX_TEXTURE_SIZE - 1, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, INT_MAX, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 0, INT_MAX, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, -1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, -1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, -1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, -1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		/* an empty region at the edge is valid and uploads nothing, as does no pixel pointer */
		glTexSubImage2D(GL_TEXTURE_2D, 0, MGL_MAX_TEXTURE_SIZE, 0, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_NO_ERROR && tex_uploads == up + 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		CHECK(ERR() == GL_NO_ERROR && tex_uploads == up + 1);
		/* the last level of the chain (1 x 1 at 13), then past it; formats and types the shim refuses */
		glTexSubImage2D(GL_TEXTURE_2D, 13, 0, 0, 1, 1, GL_RGB, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_NO_ERROR && tex_uploads == up + 2 && last_up_level == 13 && last_up_rgba[3] == 255);
		glTexSubImage2D(GL_TEXTURE_2D, 13, 1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_VALUE);
		glTexSubImage2D(GL_TEXTURE_2D, 14, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_OPERATION);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_ENUM);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_FLOAT, px);
		CHECK(ERR() == GL_INVALID_ENUM);
		glTexSubImage2D(GL_TEXTURE_2D + 1, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_ENUM && tex_uploads == up + 2);

		/* level 0 regenerates the chain under GL_GENERATE_MIPMAP, a level above it does not */
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 1);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 3, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_NO_ERROR && last_up_gen && last_up_x == 3);
		glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_NO_ERROR && !last_up_gen && last_up_level == 1);
		glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 0);

		/* no storage yet; an RGB texture stays opaque; the row length and offsets reach the copy */
		glBindTexture(GL_TEXTURE_2D, 1414);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_INVALID_OPERATION && tex_uploads == up + 4);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 2);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 2, 1, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, px);
		CHECK(ERR() == GL_NO_ERROR && last_up_x == 2 && last_up_y == 1 && last_up_w == 1 && last_up_h == 2);
		CHECK(last_up_rgba[0] == 1 && last_up_rgba[3] == 255 && last_up_rgba[4] == 9 && last_up_rgba[7] == 255);
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
		glBindTexture(GL_TEXTURE_2D, name);
	}

	/* a 1x1 texture has one level: GL_GENERATE_MIPMAP must not reach the layer */
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 1);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_NO_ERROR && t->levels == 1 && last_levels == 1 && !last_up_gen);
	glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, 0);

	/* the unpack alignment reaches the conversion: RGB rows of one pixel, padded to 4, then not */
	{
		static const unsigned char rgb[] = {1, 2, 3, 0xAA, 4, 5, 6, 0xAA};

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
		CHECK(ERR() == GL_NO_ERROR && last_up_rgba[2] == 3 && last_up_rgba[4] == 4 && last_up_rgba[7] == 255);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
		CHECK(ERR() == GL_NO_ERROR && last_up_rgba[4] == 0xAA && last_up_rgba[5] == 4);
		glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
	}

	/* a change of width alone, then of height alone, replaces the storage */
	{
		int before = tex_creates;

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		CHECK(ERR() == GL_NO_ERROR && tex_creates == before + 1 && t->width == 2);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 3, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		CHECK(ERR() == GL_NO_ERROR && tex_creates == before + 2 && t->height == 3);
	}

	glBindTexture(GL_TEXTURE_2D, 7);

	/* parameters and their sampler key */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	CHECK(ERR() == GL_NO_ERROR && mgl_sampler_key(mgl_texture(7)) == 0u);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	CHECK(ERR() == GL_NO_ERROR && mgl_sampler_key(mgl_texture(7)) == (5u | (1u << 4) | (2u << 6)));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	CHECK(mgl_sampler_key(mgl_texture(7)) == (1u | (1u << 4) | (2u << 6)));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	CHECK(mgl_sampler_key(mgl_texture(7)) == (1u | 8u | (1u << 4) | (2u << 6)));
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR + 1);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_texture(7)->min_filter == GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR_MIPMAP_NEAREST);
	CHECK(ERR() == GL_INVALID_ENUM);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_LINEAR);
	CHECK(ERR() == GL_INVALID_ENUM && mgl_texture(7)->wrap_s == GL_CLAMP);
	/* anisotropy: 1 to the maximum gl1 reads, clamped above it, refused below 1; bits 8-11 of the key */
	{
		GLfloat max = 0;
		uint32_t base = mgl_sampler_key(mgl_texture(7));

		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max);
		CHECK(ERR() == GL_NO_ERROR && max == (GLfloat)MGL_MAX_ANISOTROPY && mgl_texture(7)->max_anisotropy == 1);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4);
		CHECK(ERR() == GL_NO_ERROR && mgl_sampler_key(mgl_texture(7)) == (base | (3u << 8)));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, MGL_MAX_ANISOTROPY);
		CHECK(ERR() == GL_NO_ERROR && mgl_sampler_key(mgl_texture(7)) == (base | (15u << 8)));
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, MGL_MAX_ANISOTROPY + 1);
		CHECK(ERR() == GL_NO_ERROR && mgl_texture(7)->max_anisotropy == MGL_MAX_ANISOTROPY);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1);
		CHECK(ERR() == GL_NO_ERROR && mgl_sampler_key(mgl_texture(7)) == base);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 0);
		CHECK(ERR() == GL_INVALID_VALUE && mgl_texture(7)->max_anisotropy == 1);
	}

	/* delete: storage released, parameters reset, bindings to it fall back to 0; 0 and names past the table ignored */
	glBindTexture(GL_TEXTURE_2D, name);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, name);
	glActiveTexture(GL_TEXTURE0);
	{
		GLuint del[3] = {0, MGL_MAX_TEXTURES + 5, name};
		int before = tex_releases;

		glDeleteTextures(3, del);
		CHECK(ERR() == GL_NO_ERROR && tex_releases == before + 1 && !t->mtl && t->width == 0 && t->levels == 0);
		CHECK(mgl_state.unit[0].bound == 0 && mgl_state.unit[1].bound == 0);
	}
	glDeleteTextures(-1, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
}

/* Depth, stencil, cull, fill and polygon offset as they reach a draw. */
static void
test_draw_state(void)
{
	static const GLfloat vtx[] = {0, 0, 1, 0, 0, 1};
	int i;

	mgl_state_reset();
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vtx);

	/* the defaults: nothing tested or written, nothing culled, filled, no offset, reference 0 */
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.depth_stencil_key == 0 && last_draw.cull == MGL_CULL_NONE);
	CHECK(!last_draw.fill_lines && last_draw.depth_bias == 0 && last_draw.depth_slope == 0 && last_draw.stencil_ref == 0);

	/* the depth test with GL's initial GL_LESS and writes; a function or mask set while it is off does nothing */
	glDepthFunc(GL_GREATER);
	glDepthMask(GL_FALSE);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.depth_stencil_key == 0);
	glEnable(GL_DEPTH_TEST);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.depth_stencil_key == (1u | ((uint64_t)(GL_GREATER - GL_NEVER) << 1)));
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_TRUE);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.depth_stencil_key == (1u | (3u << 1) | (1u << 4)));
	glDepthFunc(GL_ALWAYS);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.depth_stencil_key == (1u | (7u << 1) | (1u << 4)));
	glDisable(GL_DEPTH_TEST);

	/* gl1's shadow stencil: GL_EQUAL 1 under mask 2, GL_INCR on pass; masks keep the 8 bits */
	glStencilFunc(GL_EQUAL, 1, 2);
	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.depth_stencil_key == 0 && last_draw.stencil_ref == 1);
	glEnable(GL_STENCIL_TEST);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.depth_stencil_key == ((1u << 5) | (2ull << 6) | (3ull << 15) |
		(2ull << 18) | (0xffull << 26)));
	/* every operation in its own field, the write mask, and the reference clamped to 0 .. 255 */
	glStencilOp(GL_INVERT, GL_REPLACE, GL_DECR);
	glStencilMask(0x1fe);
	glStencilFunc(GL_NOTEQUAL, 300, 0x3ff);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.depth_stencil_key == ((1u << 5) | (5ull << 6) | (5ull << 9) | (2ull << 12) | (4ull << 15) |
		(0xffull << 18) | (0xfeull << 26)) && last_draw.stencil_ref == 255);
	glStencilFunc(GL_NEVER, -4, 1);
	glStencilOp(GL_ZERO, GL_KEEP, GL_KEEP);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.stencil_ref == 0 && ((last_draw.depth_stencil_key >> 9) & 7) == 1 &&
		((last_draw.depth_stencil_key >> 6) & 7) == 0);
	glStencilFunc(GL_ALWAYS, 255, 1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.stencil_ref == 255);
	CHECK(mgl_stencil_op_index(GL_KEEP) == 0 && mgl_stencil_op_index(GL_INVERT) == 5 &&
		mgl_stencil_op_index(GL_INCR) == 3 && mgl_stencil_op_index(GL_ONE) == -1);
	/* depth and stencil together */
	glEnable(GL_DEPTH_TEST);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK((last_draw.depth_stencil_key & 0x3f) == (1u | (7u << 1) | (1u << 4) | (1u << 5)));
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_STENCIL_TEST);

	/* cull: GL_BACK by default, gl1's GL_FRONT, and both faces, which draws nothing */
	glEnable(GL_CULL_FACE);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.cull == MGL_CULL_BACK);
	glCullFace(GL_FRONT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.cull == MGL_CULL_FRONT);
	i = draws;
	glCullFace(GL_FRONT_AND_BACK);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && draws == i);
	glDisable(GL_CULL_FACE);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(draws == i + 1 && last_draw.cull == MGL_CULL_NONE);

	/* gl1's polygon offset, only while GL_POLYGON_OFFSET_FILL is on */
	glPolygonOffset(-0.05f, -1);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.depth_bias == 0 && last_draw.depth_slope == 0);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.depth_bias == -1.0f && last_draw.depth_slope == -0.05f);
	glDisable(GL_POLYGON_OFFSET_FILL);

	/* gl1's wireframe: GL_LINE on both faces; a mode per face or GL_POINT is drawn as the front's, reported */
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.fill_lines);
	glPolygonMode(GL_BACK, GL_FILL);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_INVALID_OPERATION && last_draw.fill_lines);
	glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_INVALID_OPERATION && !last_draw.fill_lines);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && !last_draw.fill_lines);
}

/* The ring allocator: aligned offsets, the last allocation that fits, then the first that does not. */
static void
test_ring(void)
{
	mgl_ring_t r = {MGL_RING_ALIGN * 4, 0};

	CHECK(mgl_ring_alloc(&r, 48) == 0 && r.used == 48);
	CHECK(mgl_ring_alloc(&r, 1) == MGL_RING_ALIGN && r.used == MGL_RING_ALIGN + 1);
	CHECK(mgl_ring_alloc(&r, MGL_RING_ALIGN) == 2 * MGL_RING_ALIGN);
	/* exactly the rest */
	CHECK(mgl_ring_alloc(&r, MGL_RING_ALIGN) == 3 * MGL_RING_ALIGN && r.used == r.size);
	/* full: refused and nothing changes */
	CHECK(mgl_ring_alloc(&r, 1) == -1 && r.used == r.size);
	/* one byte over the rest */
	r.used = MGL_RING_ALIGN * 3;
	CHECK(mgl_ring_alloc(&r, MGL_RING_ALIGN + 1) == -1 && r.used == MGL_RING_ALIGN * 3);
	/* the alignment itself past the end, and sizes that would wrap */
	r.used = MGL_RING_ALIGN * 3 + 1;
	CHECK(mgl_ring_alloc(&r, 1) == -1);
	r.size = MGL_RING_ALIGN * 4 + 10;
	r.used = MGL_RING_ALIGN * 4 + 1;
	CHECK(mgl_ring_alloc(&r, 1) == -1 && r.used == MGL_RING_ALIGN * 4 + 1);
	r.size = MGL_RING_ALIGN * 4;
	r.used = 0;
	CHECK(mgl_ring_alloc(&r, (size_t)-1) == -1 && mgl_ring_alloc(&r, 0) == -1 && r.used == 0);
	CHECK(mgl_ring_alloc(&r, r.size) == 0);
	/* the real ring holds the largest draw the shim accepts */
	r.size = MGL_RING_BYTES;
	r.used = 0;
	CHECK(mgl_ring_alloc(&r, MGL_MAX_DRAW_VERTICES * sizeof(mgl_vertex_t)) == 0 &&
		mgl_ring_alloc(&r, MGL_MAX_DRAW_INDICES * sizeof(uint16_t)) > 0);
}

/* Large enough for the largest draw the shim accepts. */
static GLfloat big_vertices[2 * (MGL_MAX_DRAW_VERTICES + 1)];
static GLushort big_elements[MGL_MAX_DRAW_INDICES + 1];

static mgl_rgba_t
rgba(float r, float g, float b, float a)
{
	mgl_rgba_t c;

	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

static int
same(mgl_rgba_t c, float r, float g, float b, float a)
{
	return c.r == r && c.g == g && c.b == b && c.a == a;
}

/*
 * The blend of a pipeline key as GL 1.4, section 4.1.7 defines it, for
 * the factors in mgl_blend_index's order: what Metal is asked to do,
 * written out so the hand-worked values below can be checked against it.
 */
static float
test_factor(int index, int ch, const float *s, const float *d)
{
	switch (index)
	{
		case 0: return 0.0f;
		case 1: return 1.0f;
		case 2: return s[ch];
		case 3: return 1.0f - s[ch];
		case 4: return s[3];
		case 5: return 1.0f - s[3];
		case 6: return d[3];
		case 7: return 1.0f - d[3];
		case 8: return d[ch];
		case 9: return 1.0f - d[ch];
		default: return ch == 3 ? 1.0f : (s[3] < 1.0f - d[3] ? s[3] : 1.0f - d[3]);
	}
}

static mgl_rgba_t
test_blend(uint32_t key, mgl_rgba_t src, mgl_rgba_t dst)
{
	float s[4] = {src.r, src.g, src.b, src.a}, d[4] = {dst.r, dst.g, dst.b, dst.a}, o[4];
	int sf = (int)((key >> 1) & 15), df = (int)((key >> 5) & 15), ch;

	if (!(key & 1))
	{
		return src;
	}

	for (ch = 0; ch < 4; ch++)
	{
		o[ch] = mgl_clamp01(s[ch] * test_factor(sf, ch, s, d) + d[ch] * test_factor(df, ch, s, d));
	}

	return rgba(o[0], o[1], o[2], o[3]);
}

/* The texture environment of one unit (mgl_texenv.h, which shaders.metal runs), by hand. */
static void
test_texenv(void)
{
	const mgl_rgba_t p = rgba(0.5f, 1.0f, 0.25f, 0.5f), t = rgba(0.75f, 0.25f, 1.0f, 0.5f);

	CHECK(same(mgl_texenv_stage(MGL_TEXENV_NONE, 0, 4.0f, p, t), 0.5f, 1.0f, 0.25f, 0.5f));
	/* REPLACE: the texel; an opaque (RGB) texture keeps the previous alpha */
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_REPLACE, 0, 1.0f, p, t), 0.75f, 0.25f, 1.0f, 0.5f));
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_REPLACE, 1, 1.0f, p, rgba(0.75f, 0.25f, 1.0f, 1.0f)),
		0.75f, 0.25f, 1.0f, 0.5f));
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_REPLACE, 0, 1.0f, p, rgba(0, 0, 0, 0.125f)), 0, 0, 0, 0.125f));
	/* MODULATE: the product, alpha too, never scaled */
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_MODULATE, 0, 4.0f, p, t), 0.375f, 0.25f, 0.25f, 0.25f));
	/* COMBINE: the product times GL_RGB_SCALE, clamped; alpha unscaled */
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_COMBINE, 0, 1.0f, p, t), 0.375f, 0.25f, 0.25f, 0.25f));
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_COMBINE, 0, 2.0f, p, t), 0.75f, 0.5f, 0.5f, 0.25f));
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_COMBINE, 0, 4.0f, p, t), 1.0f, 1.0f, 1.0f, 0.25f));
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_COMBINE, 0, 4.0f, rgba(0.125f, 0.5f, 0.25f, 1), t),
		0.375f, 0.5f, 1.0f, 0.5f));
	/* each channel clamped on its own */
	CHECK(same(mgl_texenv_stage(MGL_TEXENV_COMBINE, 0, 4.0f, rgba(1, 1, 1, 1), rgba(0.5f, 0.375f, 0.75f, 1)),
		1.0f, 1.0f, 1.0f, 1.0f));
	CHECK(mgl_clamp01(-0.5f) == 0.0f && mgl_clamp01(1.5f) == 1.0f && mgl_clamp01(0.25f) == 0.25f);
}

/*
 * The world as gl1 draws it, one texel of wall T and one of lightmap L:
 * gl1_multitexture 1 is one draw, unit 0 GL_REPLACE with T and unit 1
 * GL_MODULATE (GL_COMBINE with gl1_overbrightbits) with L
 * (R_EnableMultitexture, R_ApplyGLBuffer); gl1_multitexture 0 draws T
 * with GL_REPLACE, then L over it with blending (R_BlendLightmaps).
 */
static void
test_world_units(void)
{
	static const GLfloat vtx[] = {0, 0, 0, 1, 0, 0, 0, 1, 0};
	static const GLfloat tc0[] = {0, 0, 1, 0, 0, 1};
	static const GLfloat tc1[] = {0.25f, 0.5f, 0.75f, 0.5f, 0.25f, 1};
	static const GLushort idx[] = {0, 1, 2};
	static const unsigned char wall[4] = {191, 64, 255, 255}, light[4] = {128, 191, 64, 255};
	static unsigned char block[4 * 4 * 4];
	const mgl_rgba_t T = rgba(0.75f, 0.25f, 1.0f, 1.0f), L = rgba(0.5f, 0.75f, 0.25f, 1.0f);
	const mgl_rgba_t white = rgba(1, 1, 1, 1), black = rgba(0, 0, 0, 1);
	mgl_rgba_t c, dst;
	int i;

	mgl_state_reset();
	mgl_textures_reset();

	/* the wall on unit 0 (an RGB image), the lightmap on unit 1 (TEXNUM_LIGHTMAPS + 1, RGBA) */
	glBindTexture(GL_TEXTURE_2D, 2000);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, wall);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glActiveTexture(GL_TEXTURE1);
	glClientActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 1025);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, light);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glEnable(GL_TEXTURE_2D);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, tc1);
	glActiveTexture(GL_TEXTURE0);
	glClientActiveTexture(GL_TEXTURE0);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, tc0);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, vtx);
	glColor4f(1, 1, 1, 1);

	/* a name past the table on unit 1 is refused and leaves the lightmap bound */
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, MGL_MAX_TEXTURES);
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.unit[1].bound == 1025 && mgl_state.unit[0].bound == 2000);
	glActiveTexture(GL_TEXTURE0);

	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.nindices == 3);
	CHECK(last_draw.u.texenv0 == MGL_TEXENV_REPLACE && last_draw.u.tex_opaque0 == 1 &&
		last_draw.texture0 == mgl_texture(2000)->mtl && last_draw.sampler0 == mgl_sampler_key(mgl_texture(2000)));
	CHECK(last_draw.u.texenv1 == MGL_TEXENV_MODULATE && last_draw.u.tex_opaque1 == 0 &&
		last_draw.texture1 == mgl_texture(1025)->mtl && last_draw.sampler1 == mgl_sampler_key(mgl_texture(1025)));
	CHECK(last_draw.texture0 != last_draw.texture1 && last_draw.sampler0 != last_draw.sampler1);
	CHECK(last_draw.u.rgb_scale0 == 1.0f && last_draw.u.rgb_scale1 == 1.0f);
	/* each unit its own coordinates */
	CHECK(last_draw.vertices[1].tc[0][0] == 1 && last_draw.vertices[1].tc[1][0] == 0.75f &&
		last_draw.vertices[2].tc[0][1] == 1 && last_draw.vertices[2].tc[1][1] == 1);

	/* the fragment through both units, with the draw's uniforms: T * L */
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, white, T);
	c = mgl_texenv_stage(last_draw.u.texenv1, last_draw.u.tex_opaque1, last_draw.u.rgb_scale1, c, L);
	CHECK(same(c, 0.375f, 0.1875f, 0.25f, 1.0f));

	/* gl1_overbrightbits 2 and 4: GL_COMBINE on unit 1 */
	glActiveTexture(GL_TEXTURE1);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 2);
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.u.texenv1 == MGL_TEXENV_COMBINE &&
		last_draw.u.rgb_scale1 == 2.0f && last_draw.u.rgb_scale0 == 1.0f);
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, white, T);
	c = mgl_texenv_stage(last_draw.u.texenv1, last_draw.u.tex_opaque1, last_draw.u.rgb_scale1, c, L);
	CHECK(same(c, 0.75f, 0.375f, 0.5f, 1.0f));
	glActiveTexture(GL_TEXTURE1);
	glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 4);
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, white, T);
	c = mgl_texenv_stage(last_draw.u.texenv1, last_draw.u.tex_opaque1, last_draw.u.rgb_scale1, c, L);
	CHECK(last_draw.u.rgb_scale1 == 4.0f && same(c, 1.0f, 0.75f, 1.0f, 1.0f));

	/* what gl1 never sets: reported, drawn as the nearest it has */
	glActiveTexture(GL_TEXTURE1);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_REPLACE);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_INVALID_OPERATION && last_draw.u.texenv1 == MGL_TEXENV_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_INVALID_OPERATION && last_draw.u.texenv1 == MGL_TEXENV_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_MODULATE);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_INVALID_OPERATION && last_draw.u.texenv1 == MGL_TEXENV_MODULATE);

	/* gl1_multitexture 0: unit 1 off (R_EnableMultitexture(false)) */
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.u.texenv1 == MGL_TEXENV_NONE && last_draw.texture1 == NULL);
	CHECK(last_draw.pipeline_key == (15u << 9));
	/* the first pass, R_DrawTextureChains: T, no blend, over whatever was there */
	dst = test_blend(last_draw.pipeline_key,
		mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, white, T), black);
	CHECK(same(dst, 0.75f, 0.25f, 1.0f, 1.0f));

	/* the lightmap pass, R_BlendLightmaps: L on unit 0, GL_ZERO, GL_SRC_COLOR, no depth write */
	glBindTexture(GL_TEXTURE_2D, 1025);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ZERO, GL_SRC_COLOR);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glDepthMask(GL_FALSE);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.texture0 == mgl_texture(1025)->mtl && last_draw.u.tex_opaque0 == 0);
	CHECK(last_draw.pipeline_key == (1u | (0u << 1) | (2u << 5) | (15u << 9)));
	CHECK(last_draw.depth_stencil_key == (1u | ((uint64_t)(GL_LEQUAL - GL_NEVER) << 1)));
	c = test_blend(last_draw.pipeline_key,
		mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, white, L), dst);
	/* the same as one multitextured draw */
	CHECK(same(c, 0.375f, 0.1875f, 0.25f, 1.0f));

	/* with gl1_overbrightbits 2, GL_COMBINE on unit 0: T * clamp(2 L), not clamp(2 T L) as above */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 2);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.u.texenv0 == MGL_TEXENV_COMBINE && last_draw.u.rgb_scale0 == 2.0f);
	c = test_blend(last_draw.pipeline_key,
		mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, white, L), dst);
	CHECK(same(c, 0.75f, 0.25f, 0.5f, 1.0f));

	/* gl1_saturatelighting 1: GL_ONE, GL_ONE adds, clamped */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glBlendFunc(GL_ONE, GL_ONE);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 3);
	CHECK(last_draw.pipeline_key == (1u | (1u << 1) | (1u << 5) | (15u << 9)));
	c = test_blend(last_draw.pipeline_key,
		mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, white, L), dst);
	CHECK(same(c, 1.0f, 1.0f, 1.0f, 1.0f));
	c = test_blend(last_draw.pipeline_key, rgba(0.125f, 0.25f, 0, 0), rgba(0.25f, 0.5f, 0.5f, 0.5f));
	CHECK(same(c, 0.375f, 0.75f, 0.5f, 0.5f));
	/* and gl1's default blend, for the pass after it */
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 3);
	c = test_blend(last_draw.pipeline_key, rgba(1, 0, 0.5f, 0.25f), rgba(0, 1, 0.5f, 1));
	CHECK(same(c, 0.25f, 0.75f, 0.5f, 0.8125f));

	/*
	 * A dynamic lightmap, R_UpdateGLBuffer: a sub-rectangle of the 4 x 4
	 * block written from the block's own buffer with GL_UNPACK_ROW_LENGTH.
	 */
	for (i = 0; i < (int)sizeof(block); i++)
	{
		block[i] = (unsigned char)i;
	}

	glBindTexture(GL_TEXTURE_2D, 1024);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, block);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 4);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 2, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, block + (2 * 4 + 1) * 4);
	glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	CHECK(ERR() == GL_NO_ERROR && last_up_x == 1 && last_up_y == 2 && last_up_w == 2 && last_up_h == 2);
	/* rows of the block: (1, 2) (2, 2) then (1, 3) (2, 3) */
	CHECK(last_up_rgba[0] == 36 && last_up_rgba[4] == 40 && last_up_rgba[8] == 52 && last_up_rgba[15] == 59);

	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
}

/* Clip coordinates of vertex i of the last draw: its mvp (column-major) times the position. */
static void
test_clip(int i, float out[4])
{
	const float *m = last_draw.u.mvp, *p = last_draw.vertices[i].pos;
	int r;

	for (r = 0; r < 4; r++)
	{
		out[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r] * p[3];
	}
}

/*
 * The stencil test and update of one fragment as GL 1.4, section 4.1.5
 * defines them, read from a depth and stencil key (mgl_depth_stencil_key)
 * and the draw's reference: what Metal is asked to do, written out so the
 * hand-worked values below can be checked against it. Returns 1 when the
 * fragment passes; *stored is the stencil value, updated.
 */
static int
test_stencil(uint64_t key, uint32_t ref, int depth_pass, unsigned *stored)
{
	unsigned rmask = (unsigned)((key >> 18) & 0xff), wmask = (unsigned)((key >> 26) & 0xff);
	unsigned a = ref & rmask, b = *stored & rmask, v = *stored;
	int pass, op;

	if (!(key & (1u << 5)))
	{
		return 1;
	}

	switch ((key >> 6) & 7)
	{
		case 0: pass = 0; break;
		case 1: pass = a < b; break;
		case 2: pass = a == b; break;
		case 3: pass = a <= b; break;
		case 4: pass = a > b; break;
		case 5: pass = a != b; break;
		case 6: pass = a >= b; break;
		default: pass = 1;
	}

	op = (int)((key >> (!pass ? 9 : (!depth_pass ? 12 : 15))) & 7);

	switch (op)
	{
		case 1: v = 0; break;
		case 2: v = ref; break;
		case 3: v = v < 255 ? v + 1 : 255; break;
		case 4: v = v > 0 ? v - 1 : 0; break;
		case 5: v = ~v & 0xff; break;
		default: break;
	}

	*stored = (*stored & ~wmask) | (v & wmask);
	return pass && depth_pass;
}

/*
 * The model path as gl1 sets it up (R_ApplyGLBuffer buf_alias, buf_flash
 * and buf_shadow in gl1_buffer.c, R_DrawAliasModel and R_DrawAliasShadow
 * in gl1_mesh.c, R_RenderDlights in gl1_light.c), checked against values
 * worked out by hand. gl1 computes the vertex colours (shadedots times
 * shadelight, as bytes), the flash blob's fan and the shadow's projected
 * points on the CPU; the shim normalises the bytes, multiplies the
 * matrices, maps GL's clip depth to Metal's and passes the depth range,
 * cull, blend and stencil state on.
 */
static void
test_models(void)
{
	static const GLfloat vtx[] = {1, 1, 0, -1, 1, 0, 1, -1, 0};
	static const GLfloat tex[] = {0, 0, 1, 0, 0, 1};
	static const GLubyte clr[] = {255, 128, 0, 255, 51, 102, 204, 255, 0, 0, 0, 77};
	static const GLushort idx[] = {0, 1, 2};
	static const unsigned char skin[4] = {128, 255, 64, 255};
	static const GLfloat bbox[24] = {0};
	GLfloat proj[16], got[16];
	float c[4];
	mgl_rgba_t f, dst;
	mgl_raster_t r;
	unsigned st;
	int i, before;

	mgl_state_reset();
	mgl_textures_reset();

	/* R_SetupGL: the world's frustum (l, r, b, t, n, f) = (-2, 2, -1, 1, 1, 3), the eye 2 units back */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glFrustum(-2, 2, -1, 1, 1, 3);
	glGetFloatv(GL_PROJECTION_MATRIX, proj);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0, 0, -2);
	glCullFace(GL_FRONT);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBindTexture(GL_TEXTURE_2D, 1500);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, skin);
	glEnable(GL_TEXTURE_2D);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	CHECK(ERR() == GL_NO_ERROR);

	/*
	 * The view weapon, left-handed: RF_DEPTHHACK and RF_WEAPONMODEL with
	 * gl_lefthand 1. gl1 computes the range on the CPU,
	 * gldepthmin + 0.3 * (gldepthmax - gldepthmin) = 0.3; the shim's
	 * glDepthRange (mgl_frame.c, not linked here) stores it clamped, and
	 * mgl_compute_raster hands it to the Metal viewport.
	 */
	mgl_state.depth_near = 0.0;
	mgl_state.depth_far = 0.0 + 0.3 * (1.0 - 0.0);
	glMatrixMode(GL_PROJECTION);
	glPushMatrix();
	glLoadIdentity();
	glScalef(-1, 1, 1);
	glFrustum(-1, 3, -1, 1, 1, 3);
	glMatrixMode(GL_MODELVIEW);
	glCullFace(GL_BACK);
	glShadeModel(GL_SMOOTH);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(3, GL_FLOAT, 0, vtx);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glTexCoordPointer(2, GL_FLOAT, 0, tex);
	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, clr);
	/* gl1's c4ub layout is accepted as it is */
	CHECK(ERR() == GL_NO_ERROR && mgl_state.color_array.type == GL_UNSIGNED_BYTE && mgl_state.color_array.size == 4 &&
		mgl_state.color_array.pointer == clr);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.projection_depth == 1 && last_draw.nindices == 3);
	/* the left-handed weapon culls the other face; depth tested and written as the world */
	CHECK(last_draw.cull == MGL_CULL_BACK && last_draw.pipeline_key == (15u << 9));
	CHECK(last_draw.depth_stencil_key == (1u | ((uint64_t)(GL_LEQUAL - GL_NEVER) << 1) | (1u << 4)));
	/*
	 * The weapon's frustum (-1, 3, -1, 1, 1, 3) is asymmetric, so the
	 * order of mirror and frustum shows: GL multiplies the current matrix
	 * by each, P = S F. (1, 1, 0) is (1, 1, -2) in the eye; F gives clip
	 * x 0.5 * 1 + 0.5 * -2 = -0.5, y 1, z -2 * -2 - 3 = 1, w 2, and S
	 * mirrors x to 0.5 (F S would give -1.5): NDC z 0.5. GL's window
	 * depth in the range 0 .. 0.3 is 0 + 0.3 * (0.5 + 1) / 2 = 0.225;
	 * the shim's clip depth for Metal is (1 + 2) / 2 = 1.5, and
	 * 0 + 0.3 * 1.5 / 2 is the same 0.225.
	 */
	test_clip(0, c);
	CHECK(c[0] == 0.5f && c[1] == 1.0f && c[2] == 1.0f && c[3] == 2.0f);
	CHECK(mgl_gl_clip_z(c[2], c[3]) == 1.5f);
	mgl_compute_raster(&mgl_state, 640, 480, &r);
	CHECK(r.znear == 0.0 && r.zfar == 0.3 &&
		fabs(r.znear + (r.zfar - r.znear) * mgl_gl_clip_z(c[2], c[3]) / c[3] - 0.225) < 1e-6);
	/* the near and far planes land on the ends of the range: clip z -w and w */
	CHECK(mgl_gl_clip_z(-2.0f, 2.0f) == 0.0f && mgl_gl_clip_z(2.0f, 2.0f) == 2.0f && mgl_gl_clip_z(0, 4.0f) == 2.0f);
	/* (-1, 1, 0): F gives x -0.5 - 1 = -1.5, mirrored 1.5 */
	test_clip(1, c);
	CHECK(c[0] == 1.5f && c[1] == 1.0f);
	/* the colours gl1 computed, normalised; the skin modulated by them (an RGB skin: the vertex alpha stays) */
	CHECK(last_draw.vertices[0].color[0] == 1.0f && last_draw.vertices[0].color[1] == 128.0f / 255.0f &&
		last_draw.vertices[0].color[2] == 0.0f && last_draw.vertices[2].color[3] == 77.0f / 255.0f);
	CHECK(last_draw.u.texenv0 == MGL_TEXENV_MODULATE && last_draw.u.tex_opaque0 == 1 &&
		last_draw.texture0 == mgl_texture(1500)->mtl);
	f = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0,
		rgba(1.0f, 0.5f, 0.25f, 1.0f), rgba(0.5f, 1.0f, 0.25f, 1.0f));
	CHECK(same(f, 0.5f, 0.5f, 0.0625f, 1.0f));

	/* and back: GL_REPLACE, GL_FLAT, the world's projection, GL_FRONT, the whole range */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glShadeModel(GL_FLAT);
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glCullFace(GL_FRONT);
	mgl_state.depth_far = 1.0;
	glGetFloatv(GL_PROJECTION_MATRIX, got);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.projection_depth == 0 && memcmp(got, proj, sizeof(proj)) == 0);

	/* a right-handed model: not mirrored, GL_FRONT culled; flat shading is not modelled (design doc, as built by TASK51) */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glShadeModel(GL_SMOOTH);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	test_clip(0, c);
	CHECK(last_draw.cull == MGL_CULL_FRONT && c[0] == 0.5f && c[2] == 1.0f && c[3] == 2.0f);

	/* RF_SHELL_*: the texture off, the colours alone; RF_TRANSLUCENT blends them over what is there */
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.u.texenv0 == MGL_TEXENV_NONE && last_draw.texture0 == NULL);
	CHECK(last_draw.pipeline_key == (1u | (4u << 1) | (5u << 5) | (15u << 9)));
	dst = test_blend(last_draw.pipeline_key, rgba(1, 0, 0, 0.25f), rgba(0, 0, 1, 1));
	CHECK(same(dst, 0.25f, 0, 0.75f, 0.8125f));
	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);

	/*
	 * The flash blobs, R_RenderDlights with gl1_flashblend 1: untextured,
	 * the colour array (centre light->color * 51, rim black), GL_ONE,
	 * GL_ONE and no depth write; the blob adds, clamped.
	 */
	glDepthMask(GL_FALSE);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && last_draw.u.texenv0 == MGL_TEXENV_NONE && last_draw.nindices == 3);
	CHECK(last_draw.pipeline_key == (1u | (1u << 1) | (1u << 5) | (15u << 9)));
	CHECK(last_draw.depth_stencil_key == (1u | ((uint64_t)(GL_LEQUAL - GL_NEVER) << 1)));
	CHECK(last_draw.vertices[1].color[0] == 0.2f && last_draw.vertices[1].color[1] == 0.4f &&
		last_draw.vertices[1].color[2] == 0.8f);
	dst = test_blend(last_draw.pipeline_key, rgba(0.2f, 0.4f, 0.8f, 1), rgba(0.25f, 0.5f, 0.5f, 1));
	CHECK(fabsf(dst.r - 0.45f) < 1e-6f && fabsf(dst.g - 0.9f) < 1e-6f && dst.b == 1.0f && dst.a == 1.0f);
	glDisableClientState(GL_COLOR_ARRAY);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_BLEND);
	glDepthMask(GL_TRUE);

	/*
	 * The shadow, R_DrawAliasShadow: untextured, the current colour
	 * (0, 0, 0, 0.5) blended, depth tested and written. R_Clear clears
	 * the stencil to 1 (glClearStencil(1)) when gl1_stencilshadow is on.
	 */
	glColor4f(0, 0, 0, 0.5f);
	glEnable(GL_BLEND);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.vertices[2].color[3] == 0.5f && last_draw.vertices[2].color[0] == 0);
	CHECK(last_draw.depth_stencil_key == (1u | ((uint64_t)(GL_LEQUAL - GL_NEVER) << 1) | (1u << 4)));
	/* gl1_stencilshadow 0: two triangles of the shadow over one pixel darken it twice, 0.8 to 0.4 to 0.2 */
	st = 1;
	dst = rgba(0.8f, 0.8f, 0.8f, 1);

	for (i = 0; i < 2; i++)
	{
		if (test_stencil(last_draw.depth_stencil_key, last_draw.stencil_ref, 1, &st))
		{
			const float *v = last_draw.vertices[0].color;

			dst = test_blend(last_draw.pipeline_key, rgba(v[0], v[1], v[2], v[3]), dst);
		}
	}

	CHECK(fabsf(dst.r - 0.2f) < 1e-6f && st == 1);
	/* gl1_stencilshadow 1: GL_EQUAL 1 under mask 2 passes on 1 and on 0 ((1 & 2) == (s & 2)), GL_INCR makes 2 */
	glEnable(GL_STENCIL_TEST);
	glStencilFunc(GL_EQUAL, 1, 2);
	glStencilOp(GL_KEEP, GL_KEEP, GL_INCR);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.stencil_ref == 1);
	CHECK(last_draw.depth_stencil_key == (1u | ((uint64_t)(GL_LEQUAL - GL_NEVER) << 1) | (1u << 4) | (1u << 5) |
		(2ull << 6) | (3ull << 15) | (2ull << 18) | (0xffull << 26)));
	st = 1;
	dst = rgba(0.8f, 0.8f, 0.8f, 1);

	for (i = 0; i < 2; i++)
	{
		if (test_stencil(last_draw.depth_stencil_key, last_draw.stencil_ref, 1, &st))
		{
			dst = test_blend(last_draw.pipeline_key, rgba(0, 0, 0, 0.5f), dst);
		}
	}

	/* the second fragment fails ((2 & 2) != 0): darkened once, the stencil 2 */
	CHECK(fabsf(dst.r - 0.4f) < 1e-6f && st == 2);
	/* a fragment behind the wall (depth fails) keeps the stencil, so a later one may still darken there */
	st = 1;
	CHECK(!test_stencil(last_draw.depth_stencil_key, last_draw.stencil_ref, 0, &st) && st == 1);
	/* the stencil cleared to 0 (gl1_stencilshadow 0, or a stereo mode): (0 & 2) == (1 & 2), and INCR makes 1, then 2 */
	st = 0;
	CHECK(test_stencil(last_draw.depth_stencil_key, last_draw.stencil_ref, 1, &st) && st == 1);
	CHECK(test_stencil(last_draw.depth_stencil_key, last_draw.stencil_ref, 1, &st) && st == 2);
	CHECK(!test_stencil(last_draw.depth_stencil_key, last_draw.stencil_ref, 1, &st) && st == 2);
	/* the increment stops at the eight bits */
	st = 255;
	test_stencil((1u << 5) | (7ull << 6) | (3ull << 15) | (0xffull << 18) | (0xffull << 26), 0, 1, &st);
	CHECK(st == 255);
	glDisable(GL_STENCIL_TEST);
	glDisable(GL_BLEND);
	glColor4f(1, 1, 1, 1);

	/* gl_showbbox: an 8-vertex strip in lines, culling and texture off: six triangles, all faces drawn */
	glDisable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glDisable(GL_TEXTURE_2D);
	glVertexPointer(3, GL_FLOAT, 0, bbox);
	before = draws;
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 8);
	CHECK(ERR() == GL_NO_ERROR && draws == before + 1 && last_draw.nindices == 18 && last_draw.fill_lines &&
		last_draw.cull == MGL_CULL_NONE && last_draw.u.texenv0 == MGL_TEXENV_NONE);
	/* the strip keeps GL's order: the second triangle is (2 1 3), the last (6 5 7) */
	CHECK(last_draw.indices[3] == 2 && last_draw.indices[4] == 1 && last_draw.indices[5] == 3 &&
		last_draw.indices[15] == 6 && last_draw.indices[16] == 5 && last_draw.indices[17] == 7);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glEnable(GL_CULL_FACE);
	glEnable(GL_TEXTURE_2D);
	glDisableClientState(GL_VERTEX_ARRAY);

	/* the projection stack the weapon pushes: the last push that fits, then the first that overflows */
	glMatrixMode(GL_PROJECTION);

	for (i = 1; i < MGL_STACK_DEPTH; i++)
	{
		glPushMatrix();
	}

	CHECK(ERR() == GL_NO_ERROR && mgl_state.projection_depth == MGL_STACK_DEPTH - 1);
	glLoadIdentity();
	glPushMatrix();
	CHECK(ERR() == GL_STACK_OVERFLOW && mgl_state.projection_depth == MGL_STACK_DEPTH - 1);
	glGetFloatv(GL_PROJECTION_MATRIX, got);
	CHECK(got[0] == 1.0f && got[14] == 0.0f);

	for (i = 1; i < MGL_STACK_DEPTH; i++)
	{
		glPopMatrix();
	}

	glGetFloatv(GL_PROJECTION_MATRIX, got);
	CHECK(ERR() == GL_NO_ERROR && mgl_state.projection_depth == 0 && memcmp(got, proj, sizeof(proj)) == 0);
	glPopMatrix();
	CHECK(ERR() == GL_STACK_UNDERFLOW && mgl_state.projection_depth == 0);
	glMatrixMode(GL_MODELVIEW);
}

/* A colour equal to a few float ulps, for blends of gl1's 0.33 and 0.66, which have no exact float. */
static int
close4(mgl_rgba_t c, float r, float g, float b, float a)
{
	return near1(c.r, r) && near1(c.g, g) && near1(c.b, b) && near1(c.a, a);
}

/*
 * Sky, warped water and translucent surfaces as gl1 submits them
 * (TASK50). The warp and the sky's texture coordinates are computed by
 * gl1 on the CPU (R_EmitWaterPolys, R_MakeSkyVec); the shim only copies
 * them, so the checks are that they arrive unchanged and that the
 * state of each draw reduces to the uniforms, keys and matrix gl1 asks
 * for. One texel T of an RGB texture and gl1's inverse_intensity 0.5
 * (intensity 2) throughout.
 */
static void
test_sky_water_alpha(void)
{
	/* R_EmitWaterPolys: s and t already warped and divided by 64 */
	static const GLfloat wvtx[] = {0, 0, 0, 64, 0, 0, 64, 64, 0, 0, 64, 0};
	static const GLfloat wtc[] = {0.015625f, 1.15625f, 1.0703125f, -0.0390625f, 0.9921875f, 0.984375f, -0.03125f, 1.0f};
	static const GLushort widx[] = {0, 1, 2, 0, 2, 3};
	/* R_MakeSkyVec: one face, s and t clamped to [1/512, 511/512], t flipped */
	static const GLfloat svtx[] = {10, 0, 0, 10, 5, 0, 10, 5, 5, 10, 0, 5};
	static const GLfloat stc[] = {0.001953125f, 0.998046875f, 0.001953125f, 0.001953125f,
		0.998046875f, 0.001953125f, 0.998046875f, 0.998046875f};
	static const unsigned char texel[4] = {191, 64, 255, 255};
	const mgl_rgba_t T = rgba(0.75f, 0.25f, 1.0f, 1.0f), dst = rgba(0.25f, 0.5f, 1.0f, 1.0f);
	const float ii = 0.5f;
	const uint64_t depth_write = 1u | ((uint64_t)(GL_LEQUAL - GL_NEVER) << 1) | (1u << 4);
	mgl_rgba_t c;
	float v[4];
	int n, k;

	mgl_state_reset();
	mgl_textures_reset();
	draws = 0;

	/* R_SetupGL and R_Clear: front faces culled, depth test GL_LEQUAL with writes */
	glCullFace(GL_FRONT);
	glEnable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBindTexture(GL_TEXTURE_2D, 1700);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texel);
	glEnable(GL_TEXTURE_2D);
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);

	/*
	 * D14, R_DrawSkyBox: the box follows the eye and turns with the sky
	 * (glTranslatef to r_origin, glRotatef by time * skyrotate about
	 * skyaxis, here 90 degrees about an axis of length 2), one fan of
	 * four per face with GL_REPLACE and depth writes. The world matrix
	 * under it is a move by (1, 2, 3).
	 */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1, 1, 1, 1);
	glTranslatef(1, 2, 3);
	glPushMatrix();
	glTranslatef(100, 200, 300);
	glRotatef(90, 0, 0, 2);
	glVertexPointer(3, GL_FLOAT, 0, svtx);
	glTexCoordPointer(2, GL_FLOAT, 0, stc);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	n = draws;
	CHECK(ERR() == GL_NO_ERROR && n == 1 && last_draw.nvertices == 4 && last_draw.nindices == 6);
	CHECK(last_draw.indices[0] == 0 && last_draw.indices[1] == 1 && last_draw.indices[2] == 2 &&
		last_draw.indices[3] == 0 && last_draw.indices[4] == 2 && last_draw.indices[5] == 3);
	/* (10, 5, 5) turned 90 degrees about z is (-5, 10, 5), then moved by the origin and the world */
	xform3(last_draw.u.mvp, last_draw.vertices[2].pos[0], last_draw.vertices[2].pos[1],
		last_draw.vertices[2].pos[2], v);
	CHECK(near4(v, 96, 212, 308, 1));
	xform3(last_draw.u.mvp, 10, 0, 0, v);
	CHECK(near4(v, 101, 212, 303, 1));
	CHECK(last_draw.vertices[0].tc[0][0] == 0.001953125f && last_draw.vertices[0].tc[0][1] == 0.998046875f &&
		last_draw.vertices[2].tc[0][0] == 0.998046875f && last_draw.vertices[3].tc[0][1] == 0.998046875f);
	CHECK(last_draw.pipeline_key == (15u << 9) && last_draw.depth_stencil_key == depth_write &&
		last_draw.cull == MGL_CULL_FRONT);
	CHECK(last_draw.u.texenv0 == MGL_TEXENV_REPLACE && last_draw.u.tex_opaque0 == 1 &&
		last_draw.u.texenv1 == MGL_TEXENV_NONE && last_draw.texture0 == mgl_texture(1700)->mtl);
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0,
		rgba(1, 1, 1, 1), T);
	CHECK(same(test_blend(last_draw.pipeline_key, c, dst), 0.75f, 0.25f, 1.0f, 1.0f));
	/* glPopMatrix: the world matrix again, the next draw is not turned */
	glPopMatrix();
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	xform3(last_draw.u.mvp, 10, 5, 5, v);
	CHECK(ERR() == GL_NO_ERROR && near4(v, 11, 7, 8, 1) && mgl_state.modelview_depth == 0);
	/* another sky axis, of length 2 along x: (10, 5, 5) turns to (10, -5, 5) */
	glPushMatrix();
	glRotatef(90, 2, 0, 0);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	xform3(last_draw.u.mvp, 10, 5, 5, v);
	CHECK(ERR() == GL_NO_ERROR && near4(v, 11, -3, 8, 1));
	glPopMatrix();
	n = draws;

	/*
	 * The turb path, R_RenderBrushPoly then R_ApplyGLBuffer with
	 * SURF_DRAWTURB: unit 1 off, and with gl1_overbrightbits 0
	 * GL_MODULATE by inverse_intensity, alpha 1.
	 */
	glVertexPointer(3, GL_FLOAT, 0, wvtx);
	glTexCoordPointer(2, GL_FLOAT, 0, wtc);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glColor4f(ii, ii, ii, 1.0f);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, widx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.nvertices == 4 && last_draw.nindices == 6);

	for (k = 0; k < 4; k++)
	{
		CHECK(last_draw.vertices[k].tc[0][0] == wtc[2 * k] && last_draw.vertices[k].tc[0][1] == wtc[2 * k + 1]);
		CHECK(last_draw.vertices[k].color[0] == ii && last_draw.vertices[k].color[3] == 1.0f);
	}

	CHECK(last_draw.u.texenv0 == MGL_TEXENV_MODULATE && last_draw.u.texenv1 == MGL_TEXENV_NONE &&
		last_draw.pipeline_key == (15u << 9) && last_draw.depth_stencil_key == depth_write);
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0,
		rgba(last_draw.vertices[0].color[0], last_draw.vertices[0].color[1],
			last_draw.vertices[0].color[2], last_draw.vertices[0].color[3]), T);
	CHECK(same(c, 0.375f, 0.125f, 0.5f, 1.0f));
	/* gl1_overbrightbits 1, 2 or 4: GL_COMBINE with GL_RGB_SCALE 1, the colour left white */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
	glColor4f(1, 1, 1, 1);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, widx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.u.texenv0 == MGL_TEXENV_COMBINE && last_draw.u.rgb_scale0 == 1.0f);
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0, rgba(1, 1, 1, 1), T);
	CHECK(same(c, 0.75f, 0.25f, 1.0f, 1.0f));

	/*
	 * B4, R_DrawAlphaSurfaces then R_ApplyGLBuffer with buf_alpha:
	 * blending on, GL_MODULATE, the colour (inverse_intensity, alpha),
	 * drawn after everything opaque. gl1 leaves the depth writes on
	 * here (it never calls glDepthMask in that path), so the shim does.
	 */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_BLEND);
	glColor4f(ii, ii, ii, 0.33f);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, widx);
	CHECK(ERR() == GL_NO_ERROR && draws == n + 3);
	CHECK(last_draw.pipeline_key == (1u | (4u << 1) | (5u << 5) | (15u << 9)) &&
		last_draw.depth_stencil_key == depth_write && last_draw.cull == MGL_CULL_FRONT);
	CHECK(last_draw.vertices[3].color[0] == ii && last_draw.vertices[3].color[3] == 0.33f &&
		last_draw.vertices[1].tc[0][1] == wtc[3]);
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0,
		rgba(ii, ii, ii, last_draw.vertices[0].color[3]), T);
	/* the opaque texture keeps the colour's alpha: 0.33 of (0.375, 0.125, 0.5) over 0.67 of dst */
	CHECK(close4(c, 0.375f, 0.125f, 0.5f, 0.33f));
	CHECK(close4(test_blend(last_draw.pipeline_key, c, dst), 0.29125f, 0.37625f, 0.835f, 0.7789f));
	/* SURF_TRANS66 */
	glColor4f(ii, ii, ii, 0.66f);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, widx);
	c = mgl_texenv_stage(last_draw.u.texenv0, last_draw.u.tex_opaque0, last_draw.u.rgb_scale0,
		rgba(ii, ii, ii, last_draw.vertices[0].color[3]), T);
	CHECK(close4(test_blend(last_draw.pipeline_key, c, dst), 0.3325f, 0.2525f, 0.67f, 0.7756f));

	/* the restore at the end of R_DrawAlphaSurfaces */
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glColor4f(1, 1, 1, 1);
	glDisable(GL_BLEND);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, widx);
	CHECK(ERR() == GL_NO_ERROR && last_draw.pipeline_key == (15u << 9) &&
		last_draw.u.texenv0 == MGL_TEXENV_REPLACE && last_draw.vertices[0].color[3] == 1.0f);

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

/* The window coordinates (GL's, bottom-left origin) of a clip-space position under the viewport. */
static void
win(const mgl_vertex_t *q, float *x, float *y)
{
	const GLint *v = mgl_state.viewport;

	*x = v[0] + (q->pos[0] / q->pos[3] + 1) * v[2] * 0.5f;
	*y = v[1] + (q->pos[1] / q->pos[3] + 1) * v[3] * 0.5f;
}

/* glViewport lives in mgl_frame.c, which the host test does not link: what it stores. */
static void
set_viewport(GLint x, GLint y, GLint w, GLint h)
{
	mgl_state.viewport[0] = x;
	mgl_state.viewport[1] = y;
	mgl_state.viewport[2] = w;
	mgl_state.viewport[3] = h;
}

/* The quad of point k spans x0..x1, y0..y1 in window coordinates, corners in the order mgl_point_quads writes. */
static int
quad_at(int k, float x0, float y0, float x1, float y1)
{
	const mgl_vertex_t *q = &last_draw.vertices[4 * k];
	float x[4], y[4];
	int c;

	for (c = 0; c < 4; c++)
	{
		win(&q[c], &x[c], &y[c]);
	}

	return near1(x[0], x0) && near1(y[0], y0) && near1(x[1], x1) && near1(y[1], y0) &&
		near1(x[2], x1) && near1(y[2], y1) && near1(x[3], x0) && near1(y[3], y1) &&
		last_draw.indices[6 * k] == 4 * k && last_draw.indices[6 * k + 1] == 4 * k + 1 &&
		last_draw.indices[6 * k + 2] == 4 * k + 2 && last_draw.indices[6 * k + 3] == 4 * k &&
		last_draw.indices[6 * k + 4] == 4 * k + 2 && last_draw.indices[6 * k + 5] == 4 * k + 3;
}

/*
 * Points and lines (TASK52). The point rules are GL 1.4 section 3.3 and
 * ARB_point_parameters; the expected pixels and 8-bit coverages are what
 * Apple's software renderer drew for the same points (a scratch probe
 * of TASK52, CGL offscreen, white points blended over black), worked
 * out again by hand below.
 */
static void
test_points_lines(void)
{
	/* the 8-bit values of a smooth point of size 4 at (32.5, 32.6), pixels x 30..34, y 34 down to 30 */
	static const int apple4[5][5] = {
		{0, 90, 153, 90, 0}, {78, 255, 255, 255, 78}, {127, 255, 255, 255, 127},
		{55, 255, 255, 255, 55}, {0, 44, 102, 44, 0}
	};
	static const GLfloat gl1_att[3] = {0.01f, 0.0f, 0.01f}, no_att[3] = {0, 0, 0}, lin_att[3] = {0, 1, 0};
	static const GLubyte clr[] = {255, 128, 0, 51, 1, 2, 3, 4, 9, 8, 7, 6, 0, 0, 255, 255};
	static const GLushort elems[] = {3, 1, 2, 4};
	GLfloat pv[9] = {0}, seg[] = {0, 0.5f, 640, 0.5f, 0, 2.5f, 640, 2.5f, 7, 7};
	uint16_t idx[16];
	float e[4] = {0, 0, -20, 1};
	int i, j, n;

	mgl_state_reset();
	mgl_textures_reset();
	draws = 0;
	set_viewport(0, 0, 640, 480);
	glMatrixMode(GL_PROJECTION);
	glOrtho(0, 640, 0, 480, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glEnableClientState(GL_VERTEX_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, pv);

	/* square points: an even width centred on the nearest corner, an odd one on the pixel; sizes rounded */
	glPointSize(4);
	pv[0] = 32.5f; pv[1] = 32.6f;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(ERR() == GL_NO_ERROR && draws == 1 && last_draw.nvertices == 4 && last_draw.nindices == 6 &&
		!last_draw.lines && quad_at(0, 31, 31, 35, 35) && last_draw.vertices[0].pt[3] == 0.0f);
	pv[0] = 32.0f; pv[1] = 32.1f;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(quad_at(0, 30, 30, 34, 34));
	glPointSize(1.4f);
	pv[0] = 32.3f; pv[1] = 32.4f;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(quad_at(0, 32, 32, 33, 33));
	glPointSize(1.6f);
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(quad_at(0, 31, 31, 33, 33));
	glPointSize(2.5f);
	pv[0] = 32.5f; pv[1] = 32.6f;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(quad_at(0, 31, 31, 34, 34));
	glPointSize(7.3f);
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(quad_at(0, 29, 29, 36, 36));

	/* smooth: about the exact centre, out to r + 1/2, and the coverage Apple's renderer drew, to 1 in 255 */
	glEnable(GL_POINT_SMOOTH);
	glPointSize(4);
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(ERR() == GL_NO_ERROR && quad_at(0, 30, 30.1f, 35, 35.1f));
	CHECK(last_draw.vertices[0].pt[0] == -2.5f && last_draw.vertices[0].pt[1] == -2.5f &&
		last_draw.vertices[2].pt[0] == 2.5f && last_draw.vertices[2].pt[2] == 2.0f &&
		last_draw.vertices[1].pt[3] == 1.0f);

	for (j = 0, n = 0; j < 5; j++)
	{
		for (i = 0; i < 5; i++)
		{
			float c = mgl_point_coverage(30 + i + 0.5f - 32.5f, 34 - j + 0.5f - 32.6f, last_draw.vertices[0].pt[2]);
			int v = (int)floorf(c * 255 + 0.5f);

			n += v - apple4[j][i] <= 1 && apple4[j][i] - v <= 1;
		}
	}

	CHECK(n == 25);
	/* by hand: pixel (34, 33) is (2, 0.9) from the centre, 2.5 - 2.19317 = 0.30683; clamped at 1 and 0 */
	CHECK(near1(mgl_point_coverage(2, 0.9f, 2), 0.306833f) && mgl_point_coverage(0.1f, 0, 2) == 1.0f &&
		mgl_point_coverage(2.5f, 0, 2) == 0.0f && mgl_point_coverage(-3, 0, 2) == 0.0f);
	/* size 7.3 is not rounded: (28, 32) is 4.00125 from the centre, 4.15 - 4.00125 = 0.14875, Apple's 38 */
	glPointSize(7.3f);
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(last_draw.vertices[0].pt[2] == 3.65f && quad_at(0, 28.35f, 28.45f, 36.65f, 36.75f) &&
		(int)(mgl_point_coverage(-4, -0.1f, 3.65f) * 255 + 0.5f) == 38);
	glDisable(GL_POINT_SMOOTH);
	/* the next triangles carry no point data */
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	CHECK(last_draw.nvertices == 4 && last_draw.vertices[0].pt[3] == 0 && last_draw.vertices[2].pt[0] == 0 &&
		last_draw.vertices[3].pt[2] == 0);

	/* the viewport moves the window: NDC (0, 0) is (200, 100); size 3 covers 199..202 */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	set_viewport(100, 50, 200, 100);
	glPointSize(3);
	pv[0] = pv[1] = 0;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(quad_at(0, 199, 99, 202, 102));
	/* a centre outside the clip volume drops the point, not its corners; one draw of two keeps the inside one */
	pv[0] = 1.001f; pv[2] = 0.999f; pv[3] = -0.999f;
	n = draws;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(ERR() == GL_NO_ERROR && draws == n);
	glDrawArrays(GL_POINTS, 0, 2);
	CHECK(draws == n + 1 && last_draw.nvertices == 4 && quad_at(0, 298, 49, 301, 52));
	pv[0] = 0; pv[1] = -1.001f;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(draws == n + 1);
	/* past the far plane, and at w 0 (the eye itself): dropped */
	glVertexPointer(3, GL_FLOAT, 0, pv);
	pv[0] = pv[1] = 0; pv[2] = 1.001f;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(draws == n + 1);
	glVertexPointer(4, GL_FLOAT, 0, pv);
	pv[2] = pv[3] = 0;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(ERR() == GL_NO_ERROR && draws == n + 1);
	pv[3] = 1;
	glDrawArrays(GL_POINTS, 0, 1);
	CHECK(draws == n + 2);
	glVertexPointer(2, GL_FLOAT, 0, pv);
	n = draws;
	set_viewport(0, 0, 0, 0);
	glDrawArrays(GL_POINTS, 0, 2);
	CHECK(ERR() == GL_NO_ERROR && draws == n);
	set_viewport(0, 0, 640, 480);

	/*
	 * ARB_point_parameters as gl1 sets them (gl1_misc.c, R_InitParticleTexture:
	 * min 2, max 40, attenuation 0.01, 0, 0.01; glPointSize 40 at a height
	 * of 480): size 40 / sqrt(0.01 + 0.01 d^2), d the eye distance. The
	 * widths are what Apple's renderer drew: 20 (d 20), 16 (d 25,
	 * 15.987), 6 (d 62.5, 6.399), 3 (d 125, 3.200), 2 (clamped from
	 * 1.0000 at d 400), 40 (clamped from 78.4 at d 5).
	 */
	glPointSize(40);
	glPointParameterf(GL_POINT_SIZE_MIN, 2);
	glPointParameterf(GL_POINT_SIZE_MAX, 40);
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, gl1_att);
	CHECK(ERR() == GL_NO_ERROR && mgl_point_size(&mgl_state, e, 0) == 20.0f);
	e[0] = 15;
	CHECK(mgl_point_size(&mgl_state, e, 0) == 16.0f && near1(mgl_point_size(&mgl_state, e, 1), 15.98721f));
	e[0] = 37.5f; e[2] = -50;
	CHECK(mgl_point_size(&mgl_state, e, 0) == 6.0f && near1(mgl_point_size(&mgl_state, e, 1), 6.39918f));
	e[0] = 75; e[2] = -100;
	CHECK(mgl_point_size(&mgl_state, e, 0) == 3.0f);
	e[0] = 0; e[2] = -400;
	CHECK(mgl_point_size(&mgl_state, e, 0) == 2.0f && mgl_point_size(&mgl_state, e, 1) == 2.0f);
	e[2] = -5;
	CHECK(mgl_point_size(&mgl_state, e, 0) == 40.0f && mgl_point_size(&mgl_state, e, 1) == 40.0f);
	/* without attenuation glPointSize itself, rounded; past the ranges clamped to them */
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, no_att);
	glPointParameterf(GL_POINT_SIZE_MIN, 0);
	glPointParameterf(GL_POINT_SIZE_MAX, 10000);
	glPointSize(2.5f);
	CHECK(mgl_point_size(&mgl_state, e, 0) == 3.0f && mgl_point_size(&mgl_state, e, 1) == 2.5f);
	glPointSize(9000);
	CHECK(mgl_point_size(&mgl_state, e, 0) == MGL_POINT_ALIASED_MAX && mgl_point_size(&mgl_state, e, 1) == MGL_POINT_SMOOTH_MAX);
	glPointSize(0.01f);
	CHECK(mgl_point_size(&mgl_state, e, 0) == 1.0f && mgl_point_size(&mgl_state, e, 1) == MGL_POINT_SMOOTH_MIN);
	/* the linear term alone, which gl1 leaves at 0: 40 / sqrt(d) at d 5 (eye (3, 0, -4)) is 17.8885 */
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, lin_att);
	glPointSize(40);
	e[0] = 3; e[2] = -4;
	CHECK(near1(mgl_point_size(&mgl_state, e, 1), 17.88854f) && mgl_point_size(&mgl_state, e, 0) == 18.0f);
	e[0] = 0;

	/*
	 * R_DrawParticles: a perspective view, the eye moved by the modelview
	 * (so the distance is the eye's, not the object's), the colour array,
	 * blend on, no depth writes; culling, GL_LINE fill and polygon offset
	 * left on from the world do not apply to points.
	 */
	glMatrixMode(GL_PROJECTION);
	glFrustum(-1, 1, -1, 1, 1, 1000);
	glMatrixMode(GL_MODELVIEW);
	glTranslatef(0, 0, -10);
	glPointSize(40);
	glPointParameterf(GL_POINT_SIZE_MIN, 2);
	glPointParameterf(GL_POINT_SIZE_MAX, 40);
	glPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, gl1_att);
	glEnable(GL_CULL_FACE);
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glEnable(GL_POLYGON_OFFSET_FILL);
	glPolygonOffset(1, 2);
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, clr);
	glVertexPointer(3, GL_FLOAT, 0, pv);
	pv[0] = 0; pv[1] = 0; pv[2] = -10;    /* eye (0, 0, -20) */
	pv[3] = 15; pv[4] = 0; pv[5] = -15;   /* eye (15, 0, -25), d 29.155: 13.712, 14 */
	pv[6] = 0; pv[7] = 0; pv[8] = 100;    /* eye (0, 0, 90): behind the eye, dropped */
	glDrawArrays(GL_POINTS, 0, 2);
	CHECK(ERR() == GL_NO_ERROR && last_draw.nvertices == 8 && last_draw.nindices == 12);
	/* (0, 0, -20): w 20, z (1001 * 20 - 2000) / 999; NDC (0, 0) is (320, 240): width 20 from 310 to 330 */
	CHECK(near1(last_draw.vertices[1].pos[3], 20) && near1(last_draw.vertices[3].pos[2], 18020.0f / 999) &&
		last_draw.vertices[0].pos[2] == last_draw.vertices[2].pos[2] && quad_at(0, 310, 230, 330, 250));
	/* (15, 0, -25): x 15 / 25 of the half width, 320 + 192 = 512, 14 wide about the corner 512 */
	CHECK(quad_at(1, 505, 233, 519, 247));
	CHECK(last_draw.vertices[0].color[0] == 1.0f && last_draw.vertices[3].color[3] == 0.2f &&
		last_draw.vertices[4].color[0] == 1.0f / 255 && last_draw.vertices[7].color[3] == 4.0f / 255);
	CHECK(last_draw.u.mvp[0] == 1 && last_draw.u.mvp[5] == 1 && last_draw.u.mvp[10] == 1 &&
		last_draw.u.mvp[15] == 1 && last_draw.u.mvp[14] == 0 && last_draw.u.mvp[11] == 0);
	CHECK(last_draw.cull == MGL_CULL_NONE && !last_draw.fill_lines && last_draw.depth_bias == 0 &&
		last_draw.depth_slope == 0 && !last_draw.lines);
	CHECK(last_draw.pipeline_key == (1u | (4u << 1) | (5u << 5) | (15u << 9)) &&
		last_draw.depth_stencil_key == (1u | ((uint64_t)(GL_LESS - GL_NEVER) << 1)));
	/* glDrawElements picks the points; the third lies behind the eye (w < 0) */
	glDrawElements(GL_POINTS, 2, GL_UNSIGNED_SHORT, elems + 1);
	CHECK(ERR() == GL_NO_ERROR && last_draw.nvertices == 4 && quad_at(0, 505, 233, 519, 247) &&
		last_draw.vertices[0].color[1] == 2.0f / 255);
	glDisableClientState(GL_COLOR_ARRAY);

	/* the bound: MGL_MAX_DRAW_POINTS points fill a draw, one more is refused */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glVertexPointer(2, GL_FLOAT, 0, big_vertices);
	memset(big_vertices, 0, sizeof(big_vertices));
	n = draws;
	glDrawArrays(GL_POINTS, 0, MGL_MAX_DRAW_POINTS);
	CHECK(ERR() == GL_NO_ERROR && draws == n + 1 && last_draw.nvertices == MGL_MAX_DRAW_VERTICES &&
		last_draw.indices[last_draw.nindices - 1] == MGL_MAX_DRAW_VERTICES - 1);
	glDrawArrays(GL_POINTS, 0, MGL_MAX_DRAW_POINTS + 1);
	CHECK(ERR() == GL_INVALID_VALUE && draws == n + 1);
	glDrawElements(GL_POINTS, MGL_MAX_DRAW_POINTS + 1, GL_UNSIGNED_SHORT, big_elements);
	CHECK(ERR() == GL_INVALID_VALUE && draws == n + 1);

	/* line lists: pairs, strips, loops; elements pick; a list past cap refused */
	CHECK(mgl_line_indices(GL_LINES, 5, NULL, idx, 16) == 4 && idx[0] == 0 && idx[1] == 1 && idx[2] == 2 && idx[3] == 3);
	CHECK(mgl_line_indices(GL_LINE_STRIP, 4, NULL, idx, 16) == 6 && idx[1] == 1 && idx[2] == 1 &&
		idx[4] == 2 && idx[5] == 3);
	CHECK(mgl_line_indices(GL_LINE_LOOP, 3, NULL, idx, 16) == 6 && idx[3] == 2 && idx[4] == 2 && idx[5] == 0);
	CHECK(mgl_line_indices(GL_LINE_STRIP, 4, elems, idx, 16) == 6 && idx[0] == 3 && idx[1] == 1 &&
		idx[2] == 1 && idx[5] == 4);
	CHECK(mgl_line_indices(GL_LINE_LOOP, 1, NULL, idx, 16) == 0 && mgl_line_indices(GL_LINE_STRIP, 1, NULL, idx, 16) == 0);
	CHECK(mgl_line_indices(GL_LINE_LOOP, 5, NULL, idx, 10) == 10 && mgl_line_indices(GL_LINE_LOOP, 5, NULL, idx, 9) == -1);
	CHECK(mgl_line_indices(GL_TRIANGLES, 3, NULL, idx, 16) == -1);

	/*
	 * The interleaved-stereo mask (gl1_main.c, R_SetGL2D's caller): 2D
	 * GL_LINES of two floats, colour writes off, the stencil test
	 * GL_NEVER with GL_INVERT on fail. Culling left on does not apply.
	 */
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glEnable(GL_STENCIL_TEST);
	glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
	glStencilOp(GL_INVERT, GL_KEEP, GL_KEEP);
	glStencilFunc(GL_NEVER, 1, 1);
	glVertexPointer(2, GL_FLOAT, 0, seg);
	glDrawArrays(GL_LINES, 0, 4);
	CHECK(ERR() == GL_NO_ERROR && last_draw.lines && last_draw.nvertices == 4 && last_draw.nindices == 4 &&
		last_draw.vertices[3].pos[0] == 640 && last_draw.vertices[3].pos[1] == 2.5f);
	CHECK(last_draw.cull == MGL_CULL_NONE && !last_draw.fill_lines && last_draw.depth_bias == 0 &&
		last_draw.pipeline_key == 0 && (last_draw.depth_stencil_key & (1u << 5)) &&
		((last_draw.depth_stencil_key >> 9) & 7) == (uint64_t)mgl_stencil_op_index(GL_INVERT));
	/* gl_showtris: a line strip of four closes a triangle */
	glDisable(GL_STENCIL_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDrawArrays(GL_LINE_STRIP, 0, 4);
	CHECK(ERR() == GL_NO_ERROR && last_draw.lines && last_draw.nindices == 6 && last_draw.indices[5] == 3);
	glDrawElements(GL_LINE_LOOP, 4, GL_UNSIGNED_SHORT, elems);
	CHECK(ERR() == GL_NO_ERROR && last_draw.nindices == 8 && last_draw.indices[6] == 4 &&
		last_draw.indices[7] == 3 && last_draw.nvertices == 5);
	n = draws;
	glDrawElements(GL_LINE_LOOP, MGL_MAX_DRAW_INDICES, GL_UNSIGNED_SHORT, big_elements);
	CHECK(ERR() == GL_INVALID_VALUE && draws == n);
	/* back to triangles: the lines flag goes */
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(!last_draw.lines && last_draw.cull == MGL_CULL_BACK && last_draw.fill_lines);

	glDisable(GL_CULL_FACE);
	glDisable(GL_POLYGON_OFFSET_FILL);
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
	glDepthMask(GL_TRUE);
}

static void
test_draw(void)
{
	static const GLfloat vtx[] = {10, 20, 30, 20, 30, 40, 10, 40, 50, 60};
	static const GLfloat tex[] = {0, 0, 1, 0, 1, 1, 0, 1, 0.5f, 0.5f};
	static const GLshort svtx[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
	static const GLubyte clr[] = {255, 0, 51, 102, 0, 255, 0, 255, 1, 2, 3, 4, 0, 0, 0, 0, 9, 9, 9, 9};
	static const GLushort elems[] = {3, 1, 2, 4, 0, 1};
	static const unsigned char px[4] = {1, 2, 3, 4};
	const mgl_vertex_t *v;
	GLfloat expect[16];
	int i;

	mgl_state_reset();
	mgl_textures_reset();
	draws = 0;

	/* a fan of four, gl1's RDraw_Pic: two triangles, (0 1 2) and (0 2 3) */
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_FLOAT, 0, vtx);
	glTexCoordPointer(2, GL_FLOAT, 0, tex);
	glColor4f(0.25f, 0.5f, 0.75f, 1.0f);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	CHECK(ERR() == GL_NO_ERROR && draws == 1 && last_draw.nvertices == 4 && last_draw.nindices == 6);
	CHECK(last_draw.indices[0] == 0 && last_draw.indices[1] == 1 && last_draw.indices[2] == 2);
	CHECK(last_draw.indices[3] == 0 && last_draw.indices[4] == 2 && last_draw.indices[5] == 3);
	v = last_draw.vertices;
	CHECK(v[1].pos[0] == 30 && v[1].pos[1] == 20 && v[1].pos[2] == 0 && v[1].pos[3] == 1);
	CHECK(v[2].tc[0][0] == 1 && v[2].tc[0][1] == 1 && v[3].tc[0][0] == 0 && v[3].tc[0][1] == 1);
	CHECK(v[3].tc[1][0] == 0 && v[3].tc[1][1] == 0);
	CHECK(v[0].color[0] == 0.25f && v[3].color[2] == 0.75f && v[3].color[3] == 1.0f);

	/* GL_TEXTURE_2D starts disabled; enabled, texture 0 has no storage: drawn untextured either way */
	CHECK(last_draw.u.texenv0 == MGL_TEXENV_NONE && last_draw.texture0 == NULL);
	glEnable(GL_TEXTURE_2D);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	CHECK(draws == 2 && last_draw.u.texenv0 == MGL_TEXENV_NONE && last_draw.texture0 == NULL);
	draws = 1;

	/* defaults: no blend, all channels written, alpha test off */
	CHECK(last_draw.pipeline_key == (15u << 9) && last_draw.u.alpha_func == GL_ALWAYS - GL_NEVER);

	/* first: the vertices from first on */
	glDrawArrays(GL_TRIANGLE_FAN, 1, 3);
	CHECK(draws == 2 && last_draw.nindices == 3 && last_draw.vertices[0].pos[0] == 30 &&
		last_draw.vertices[2].pos[0] == 10 && last_draw.vertices[2].tc[0][1] == 1);

	/* a strip of five: the odd triangle swaps its first two */
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 5);
	CHECK(last_draw.nindices == 9);
	CHECK(last_draw.indices[0] == 0 && last_draw.indices[1] == 1 && last_draw.indices[2] == 2);
	CHECK(last_draw.indices[3] == 2 && last_draw.indices[4] == 1 && last_draw.indices[5] == 3);
	CHECK(last_draw.indices[6] == 2 && last_draw.indices[7] == 3 && last_draw.indices[8] == 4);

	/* triangles: the vertices past the last whole triangle are dropped; under three draw nothing */
	glDrawArrays(GL_TRIANGLES, 0, 5);
	CHECK(last_draw.nindices == 3 && last_draw.indices[2] == 2 && draws == 4);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 2);
	CHECK(draws == 4 && ERR() == GL_NO_ERROR);

	/* the state of gl1's 2D path: a texture with REPLACE, alpha test GREATER 0.666, then blend */
	glBindTexture(GL_TEXTURE_2D, 1500);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glAlphaFunc(GL_LESS, 0.5f);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	/* a function set while the test is off does not apply */
	CHECK(last_draw.u.alpha_func == GL_ALWAYS - GL_NEVER);
	glEnable(GL_ALPHA_TEST);
	glAlphaFunc(GL_GREATER, 0.666f);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	CHECK(ERR() == GL_NO_ERROR && last_draw.u.texenv0 == MGL_TEXENV_REPLACE && last_draw.u.tex_opaque0 == 1);
	CHECK(last_draw.texture0 == mgl_texture(1500)->mtl && last_draw.sampler0 == mgl_sampler_key(mgl_texture(1500)));
	CHECK(last_draw.u.alpha_func == GL_GREATER - GL_NEVER && last_draw.u.alpha_ref == 0.666f);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColorMask(GL_TRUE, GL_FALSE, GL_TRUE, GL_FALSE);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	CHECK(last_draw.u.texenv0 == MGL_TEXENV_MODULATE);
	CHECK(last_draw.pipeline_key == (1u | (4u << 1) | (5u << 5) | (1u << 9) | (1u << 11)));
	CHECK(mgl_blend_index(GL_ZERO) == 0 && mgl_blend_index(GL_SRC_ALPHA_SATURATE) == 10 &&
		mgl_blend_index(GL_DST_COLOR) == 8 && mgl_blend_index(GL_KEEP) == -1);
	/* the destination alpha pair, in the order of mgl_blend_factors in mgl_layer.m */
	CHECK(mgl_blend_index(GL_DST_ALPHA) == 6 && mgl_blend_index(GL_ONE_MINUS_DST_ALPHA) == 7 &&
		mgl_blend_index(GL_ONE_MINUS_DST_COLOR) == 9);

	/* the texture unit off: untextured whatever is bound */
	glDisable(GL_TEXTURE_2D);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	CHECK(last_draw.u.texenv0 == MGL_TEXENV_NONE && last_draw.texture0 == NULL);

	/* the matrices: projection times modelview */
	glMatrixMode(GL_PROJECTION);
	glOrtho(0, 640, 480, 0, -99999, 99999);
	glMatrixMode(GL_MODELVIEW);
	mgl_state.modelview[0][12] = 5.0f;
	mgl_mat_mul(expect, mgl_state.projection[0], mgl_state.modelview[0]);
	glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
	CHECK(memcmp(last_draw.u.mvp, expect, sizeof(expect)) == 0 && expect[12] != mgl_state.projection[0][12]);

	/* glDrawElements: the elements pick the vertices, which run from 0 to the largest */
	glEnableClientState(GL_COLOR_ARRAY);
	glColorPointer(4, GL_UNSIGNED_BYTE, 0, clr);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_SHORT, 0, svtx);
	glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, elems);
	CHECK(ERR() == GL_NO_ERROR && last_draw.nindices == 6 && last_draw.nvertices == 5);
	CHECK(last_draw.indices[0] == 3 && last_draw.indices[3] == 4 && last_draw.indices[5] == 1);
	v = last_draw.vertices;
	CHECK(v[2].pos[0] == 5 && v[2].pos[1] == 6 && v[0].tc[0][0] == 0);
	CHECK(v[0].color[0] == 1.0f && v[0].color[1] == 0.0f && v[0].color[2] == 0.2f && v[0].color[3] == 0.4f);
	CHECK(v[2].color[0] == 1.0f / 255.0f && v[2].color[3] == 4.0f / 255.0f);
	/* the largest element is assembled too */
	CHECK(v[4].pos[0] == 9 && v[4].pos[1] == 10 && v[4].color[0] == 9.0f / 255.0f);
	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, elems);
	CHECK(last_draw.nindices == 6 && last_draw.indices[0] == 3 && last_draw.indices[1] == 1 &&
		last_draw.indices[2] == 2 && last_draw.indices[4] == 2 && last_draw.indices[5] == 4);

	/* a stride */
	glDisableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 4 * sizeof(GLfloat), vtx);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.vertices[1].pos[0] == 30 && last_draw.vertices[1].pos[1] == 40 &&
		last_draw.vertices[2].pos[0] == 50);

	/* what is not drawn: no vertex array, unknown modes and types (points and lines: test_points_lines) */
	i = draws;
	glDisableClientState(GL_VERTEX_ARRAY);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && draws == i);
	glEnableClientState(GL_VERTEX_ARRAY);
	glDrawArrays(GL_TRIANGLE_FAN + 1, 0, 3);
	CHECK(ERR() == GL_INVALID_ENUM && draws == i);
	glDrawArrays(GL_TRIANGLES, -1, 3);
	CHECK(ERR() == GL_INVALID_VALUE && draws == i);
	glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, elems);
	CHECK(ERR() == GL_INVALID_OPERATION && draws == i);
	glDrawElements(GL_TRIANGLES, 3, GL_FLOAT, elems);
	CHECK(ERR() == GL_INVALID_ENUM && draws == i);
	glDrawElements(GL_TRIANGLES, -1, GL_UNSIGNED_SHORT, elems);
	CHECK(ERR() == GL_INVALID_VALUE && draws == i);

	/* the bounds of one draw: the largest accepted, then one past it */
	glVertexPointer(2, GL_FLOAT, 0, big_vertices);
	glDrawArrays(GL_TRIANGLE_FAN, 0, MGL_MAX_DRAW_VERTICES);
	CHECK(ERR() == GL_NO_ERROR && draws == i + 1 && last_draw.nvertices == MGL_MAX_DRAW_VERTICES &&
		last_draw.nindices == 3 * (MGL_MAX_DRAW_VERTICES - 2) &&
		last_draw.indices[last_draw.nindices - 1] == MGL_MAX_DRAW_VERTICES - 1);
	glDrawArrays(GL_TRIANGLE_FAN, 0, MGL_MAX_DRAW_VERTICES + 1);
	CHECK(ERR() == GL_INVALID_VALUE && draws == i + 1);

	for (i = 0; i <= MGL_MAX_DRAW_INDICES; i++)
	{
		big_elements[i] = (GLushort)(i % 7);
	}

	i = draws;
	glDrawElements(GL_TRIANGLES, MGL_MAX_DRAW_INDICES, GL_UNSIGNED_SHORT, big_elements);
	CHECK(ERR() == GL_NO_ERROR && draws == i + 1 && last_draw.nindices == MGL_MAX_DRAW_INDICES &&
		last_draw.nvertices == 7);
	glDrawElements(GL_TRIANGLES, MGL_MAX_DRAW_INDICES + 1, GL_UNSIGNED_SHORT, big_elements);
	CHECK(ERR() == GL_INVALID_VALUE && draws == i + 1);
	/* a fan of that many elements needs three times as many indices: refused, not truncated */
	glDrawElements(GL_TRIANGLE_FAN, MGL_MAX_DRAW_INDICES, GL_UNSIGNED_SHORT, big_elements);
	CHECK(ERR() == GL_INVALID_VALUE && draws == i + 1);
	CHECK(mgl_triangle_indices(GL_TRIANGLE_FAN, 5, NULL, (uint16_t *)big_elements, 9) == 9);
	CHECK(mgl_triangle_indices(GL_TRIANGLE_FAN, 5, NULL, (uint16_t *)big_elements, 8) == -1);
	CHECK(mgl_triangle_indices(GL_POINTS, 5, NULL, (uint16_t *)big_elements, 99) == -1);

	/* unit 1 enabled with nothing bound (texture 0, no storage): drawn as if it were off, no error */
	i = draws;
	glActiveTexture(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && draws == i + 1 && last_draw.u.texenv1 == MGL_TEXENV_NONE &&
		last_draw.texture1 == NULL && last_draw.sampler1 == 0);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);

	test_draw_state();
	test_world_units();
	test_sky_water_alpha();
	test_models();
	test_points_lines();
}

int
main(void)
{
	test_read_contract();
	test_raster();
	test_state();
	test_matrix_stack();
	test_matrices();
	test_readback_convert();
	test_unpack();
	test_textures();
	test_texenv();
	test_draw();
	test_ring();

	printf("mgl_host_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
