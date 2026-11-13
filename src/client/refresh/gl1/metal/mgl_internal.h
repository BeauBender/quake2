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
 * Shared declarations of the OpenGL-subset shim on Metal
 * (docs/plans/metal-shim-design.md). Nothing here is visible to gl1:
 * gl1 sees only include/OpenGL/gl.h and shim_sdl.h.
 *
 * The shim is single-threaded, like gl1: every gl* call and every
 * mgl_* SDL replacement runs on the thread that created the context.
 * The only other thread is Metal's completion handlers, which signal
 * semaphores and touch nothing else.
 *
 * =======================================================================
 */

#ifndef MGL_INTERNAL_H
#define MGL_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "include/OpenGL/gl.h"

#ifndef MGL_RENDERER_SUFFIX
#define MGL_RENDERER_SUFFIX "gl1-metal"
#endif

/* Texture units gl1 uses (GL_TEXTURE0, GL_TEXTURE1). */
#define MGL_MAX_UNITS 2

/* Matrix stack depth per mode (GL 1.x guarantees 32 modelview, 2 projection). */
#define MGL_STACK_DEPTH 32

/* Bits of mgl_state_t.enables */
#define MGL_EN_ALPHA_TEST     (1u << 0)
#define MGL_EN_BLEND          (1u << 1)
#define MGL_EN_CULL_FACE      (1u << 2)
#define MGL_EN_DEPTH_TEST     (1u << 3)
#define MGL_EN_POINT_SMOOTH   (1u << 4)
#define MGL_EN_POLYGON_OFFSET (1u << 5)
#define MGL_EN_SCISSOR_TEST   (1u << 6)
#define MGL_EN_STENCIL_TEST   (1u << 7)
#define MGL_EN_MULTISAMPLE    (1u << 8)
#define MGL_EN_SHARED_PALETTE (1u << 9)

/* A client vertex array (glVertexPointer, glTexCoordPointer, glColorPointer). */
typedef struct
{
	GLboolean enabled;
	GLint size;
	GLenum type;
	GLsizei stride;
	const GLvoid *pointer;
} mgl_array_t;

/* Per texture unit: what glEnable(GL_TEXTURE_2D), glBindTexture and glTexEnvi set. */
typedef struct
{
	GLboolean texture_2d;
	GLuint bound;
	GLenum env_mode;
	GLenum combine_rgb;
	GLenum combine_alpha;
	GLint rgb_scale;
} mgl_unit_t;

/*
 * Every piece of GL state the 54 entry points can touch. gl* calls only
 * mutate this struct; Metal is touched by the frame calls (clear,
 * finish, readback, present) and, from later cards on, by the draws.
 */
typedef struct
{
	GLenum error;                 /* sticky: the first error since the last glGetError */

	GLuint enables;               /* MGL_EN_* */

	GLenum blend_src, blend_dst;

	GLenum depth_func;
	GLboolean depth_mask;
	GLclampd depth_near, depth_far;

	GLenum alpha_func;
	GLclampf alpha_ref;

	GLenum stencil_func;
	GLint stencil_ref;
	GLuint stencil_value_mask;
	GLenum stencil_fail, stencil_zfail, stencil_zpass;
	GLuint stencil_write_mask;
	GLint clear_stencil;

	GLenum cull_face;
	GLboolean color_mask[4];
	GLenum polygon_mode_front, polygon_mode_back;
	GLenum shade_model;
	GLfloat polygon_offset_factor, polygon_offset_units;

	GLfloat point_size;
	GLfloat point_size_min, point_size_max;
	GLfloat point_attenuation[3];

	GLfloat color[4];             /* glColor4f / glColor4ub */
	GLclampf clear_color[4];

	GLint viewport[4];            /* x, y, w, h; GL bottom-left origin */
	GLint scissor[4];

	GLenum draw_buffer;
	GLenum hint_multisample_filter;

	GLuint active_unit;           /* index, not the GL_TEXTUREn enum */
	GLuint client_unit;
	mgl_unit_t unit[MGL_MAX_UNITS];

	mgl_array_t vertex_array;
	mgl_array_t color_array;
	mgl_array_t texcoord_array[MGL_MAX_UNITS];

	GLenum matrix_mode;
	GLfloat modelview[MGL_STACK_DEPTH][16];
	GLfloat projection[MGL_STACK_DEPTH][16];
	GLint modelview_depth, projection_depth;   /* index of the top */

	GLint pack_alignment;
	GLint unpack_alignment;
	GLint unpack_row_length;
} mgl_state_t;

extern mgl_state_t mgl_state;

/* mgl_state.c */
void mgl_state_reset(void);
void mgl_error(GLenum error);
void mgl_unimplemented(const char *name, int *once);
unsigned long mgl_unimplemented_calls(void);

/* A call a later card implements: sets GL_INVALID_OPERATION, logs its name once. */
#define MGL_UNIMPLEMENTED() \
	do { static int mgl_once_; mgl_unimplemented(__func__, &mgl_once_); } while (0)

/*
 * mgl_frame.c: the GL-to-Metal rectangle flip. GL's origin is the bottom
 * left of the target, Metal's the top left; a rectangle of height h at
 * GL y in a target of height th starts at Metal y = th - y - h.
 */
typedef struct
{
	double x, y, w, h, znear, zfar;   /* Metal viewport */
	int sx, sy, sw, sh;               /* Metal scissor, clamped to the target */
	int scissor_empty;                /* the GL scissor lies outside the target */
} mgl_raster_t;

void mgl_compute_raster(const mgl_state_t *s, int tw, int th, mgl_raster_t *out);

/*
 * The read contract of glReadPixels / glReadnPixels, checked before
 * anything is read: returns GL_NO_ERROR and the bytes per pixel, the
 * padded row length and the total size the caller's buffer must hold,
 * or the GL error to set (and then nothing is read or written).
 */
GLenum mgl_read_contract(GLint x, GLint y, GLsizei w, GLsizei h,
		GLenum format, GLenum type, GLint alignment, int tw, int th,
		size_t *bpp, size_t *rowbytes, size_t *total);

/* mgl_layer.m: the only file that talks to Metal and CAMetalLayer. */
typedef struct
{
	int depth_bits;
	int stencil_bits;
	int msaa_buffers;
	int msaa_samples;
} mgl_config_t;

int mgl_layer_create(void *sdl_window, const mgl_config_t *config, char *err, size_t errlen);
void mgl_layer_destroy(void);
int mgl_layer_ready(void);
const char *mgl_layer_device_name(void);
void mgl_layer_set_sync(int on);
int mgl_layer_get_sync(void);

/* Opens the frame if none is open: 1 with the target size, 0 when the frame is skipped. */
int mgl_layer_frame_begin(int *tw, int *th);
/* Ends the current pass and opens the next; clear_mask is GL_*_BUFFER_BIT. */
int mgl_layer_pass(GLbitfield clear_mask, const GLclampf color[4], double depth, unsigned stencil);
/* Opens a first pass when the frame has none, so the target is defined. */
int mgl_layer_ensure_pass(void);
/*
 * Recomputes the viewport, scissor and depth range from mgl_state and
 * sets them on the open pass (each new pass gets them too).
 */
void mgl_layer_update_raster(void);
/* Commits what the frame has so far and waits for it, bounded; the frame stays open. */
int mgl_layer_finish(void);
/* Reads a rectangle of the frame's target in Metal coordinates as BGRA rows, top row first. */
int mgl_layer_read(int x, int y, int w, int h, unsigned char *bgra);
/* Ends the frame: presents the drawable (if there is one) and commits. */
int mgl_layer_present(void);

/* Upper bounds on every wait, in milliseconds. */
#define MGL_FRAME_WAIT_MS 2000
#define MGL_GPU_WAIT_MS 5000

#endif /* MGL_INTERNAL_H */
