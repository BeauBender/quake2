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
 * Host test of the pure-C parts of the Metal shim (TASK47), run by
 * tests/check-metal-shim.sh with the system compiler: no SDL, no Metal.
 * It covers mgl_read_contract, mgl_compute_raster (mgl_raster.c), the
 * validators and matrix stacks of mgl_state.c and glPixelStorei
 * (mgl_tex.c), with the last accepted and the first refused value at
 * each boundary. mgl_layer.m is replaced by the two stubs below.
 *
 * =======================================================================
 */

#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/gl1/metal/mgl_internal.h"

/* GL enums the shim's subset header leaves out, which it must refuse. */
#define TEST_GL_LIGHTING     0x0B50
#define TEST_GL_NORMAL_ARRAY 0x8075
#define TEST_GL_VIEWPORT     0x0BA2

/* mgl_layer.m is not linked: the state calls only ask it to refresh the raster. */
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

	/* the matrix arithmetic is a later card's: refused, counted */
	glOrtho(0, 1, 0, 1, -1, 1);
	CHECK(ERR() == GL_INVALID_OPERATION && mgl_unimplemented_calls() == 1);
}

int
main(void)
{
	test_read_contract();
	test_raster();
	test_state();
	test_matrix_stack();

	printf("mgl_host_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
