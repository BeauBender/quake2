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
static int last_levels, last_up_level, last_up_w, last_up_h, last_up_gen;
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

	(void)tex; (void)x; (void)y;
	tex_uploads++;
	last_up_level = level;
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
	CHECK(ERR() == GL_INVALID_VALUE && mgl_state.point_size_max == 1.0f);
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

	/* the rest of the matrix arithmetic is a later card's: refused, counted */
	glFrustum(-1, 1, -1, 1, 1, 2);
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

	/* levels above 0 are S2b: refused, nothing uploaded */
	glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_OPERATION && tex_uploads == 3);

	/* what the shim refuses: the paletted path, other types, a border, the size limit */
	glTexImage2D(GL_TEXTURE_2D, 0, GL_COLOR_INDEX8_EXT, 2, 2, 0, GL_COLOR_INDEX, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_ENUM);
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

	/* sub-images are S2b: refused */
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
	CHECK(ERR() == GL_INVALID_OPERATION && tex_uploads == 3);

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
	/* anisotropy is not advertised */
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 4);
	CHECK(ERR() == GL_INVALID_ENUM);

	/* delete: storage released, parameters reset, bindings to it fall back to 0; 0 and names past the table ignored */
	glBindTexture(GL_TEXTURE_2D, name);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, name);
	glActiveTexture(GL_TEXTURE0);
	{
		GLuint del[3] = {0, MGL_MAX_TEXTURES + 5, name};
		int before = tex_releases;

		glDeleteTextures(3, del);
		CHECK(ERR() == GL_NO_ERROR && tex_releases == before + 1 && !t->mtl && t->width == 0);
		CHECK(mgl_state.unit[0].bound == 0 && mgl_state.unit[1].bound == 0);
	}
	glDeleteTextures(-1, NULL);
	CHECK(ERR() == GL_INVALID_VALUE);
}

/* Large enough for the largest draw the shim accepts. */
static GLfloat big_vertices[2 * (MGL_MAX_DRAW_VERTICES + 1)];
static GLushort big_elements[MGL_MAX_DRAW_INDICES + 1];

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
	glDrawElements(GL_TRIANGLE_FAN, 4, GL_UNSIGNED_SHORT, elems);
	CHECK(last_draw.nindices == 6 && last_draw.indices[0] == 3 && last_draw.indices[1] == 1 &&
		last_draw.indices[2] == 2 && last_draw.indices[4] == 2 && last_draw.indices[5] == 4);

	/* a stride */
	glDisableClientState(GL_COLOR_ARRAY);
	glVertexPointer(2, GL_FLOAT, 4 * sizeof(GLfloat), vtx);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(last_draw.vertices[1].pos[0] == 30 && last_draw.vertices[1].pos[1] == 40 &&
		last_draw.vertices[2].pos[0] == 50);

	/* what is not drawn: no vertex array, the modes of later cards, unknown modes and types */
	i = draws;
	glDisableClientState(GL_VERTEX_ARRAY);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && draws == i);
	glEnableClientState(GL_VERTEX_ARRAY);
	glDrawArrays(GL_POINTS, 0, 3);
	CHECK(ERR() == GL_INVALID_OPERATION && draws == i);
	glDrawArrays(GL_LINE_STRIP, 0, 3);
	CHECK(ERR() == GL_INVALID_OPERATION && draws == i);
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

	/* state this card does not draw yet: drawn without it, the sticky error set */
	i = draws;
	glEnable(GL_CULL_FACE);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_INVALID_OPERATION && draws == i + 1);
	glDisable(GL_CULL_FACE);
	glEnable(GL_DEPTH_TEST);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_INVALID_OPERATION && draws == i + 2);
	glDisable(GL_DEPTH_TEST);
	i++;
	glActiveTexture(GL_TEXTURE1);
	glEnable(GL_TEXTURE_2D);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_INVALID_OPERATION && draws == i + 2);
	glDisable(GL_TEXTURE_2D);
	glActiveTexture(GL_TEXTURE0);
	glDrawArrays(GL_TRIANGLES, 0, 3);
	CHECK(ERR() == GL_NO_ERROR && draws == i + 3);
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
	test_draw();

	printf("mgl_host_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
