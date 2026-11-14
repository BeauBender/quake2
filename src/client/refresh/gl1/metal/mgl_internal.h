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
#include "mgl_texenv.h"

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

/*
 * Converts what Metal read (BGRA rows, top row first, w * 4 bytes each)
 * into the caller's glReadPixels buffer: GL rows bottom up, rowbytes
 * apart, bpp bytes per pixel in format (GL_RGB, GL_RGBA or GL_BGRA).
 * The padding at the end of each row is not written.
 */
void mgl_convert_readback(const unsigned char *bgra, int w, int h, GLenum format,
		size_t bpp, size_t rowbytes, unsigned char *dst);

/* out = a * b, column-major 4x4 as GL keeps them; out may be a or b. */
#include "../../metal/common/mtl_matrix.h"
#define mgl_mat_mul mtl_mat_mul

/* ---- textures (mgl_tex.c) ------------------------------------------------ */

/* GL texture names gl1 uses run to about 2500 (TEXNUM_IMAGES + MAX_GLTEXTURES). */
#define MGL_MAX_TEXTURES 4096
#define MGL_MAX_TEXTURE_SIZE 8192

typedef struct
{
	void *mtl;                    /* the layer's texture, NULL until level 0 is defined */
	int width, height, levels;    /* of level 0; levels is the whole chain, 0 while mtl is NULL */
	GLboolean opaque;             /* an RGB internal format: alpha reads 1, GL_REPLACE keeps the fragment's */
	GLenum min_filter, mag_filter, wrap_s, wrap_t;
	GLboolean generate_mipmap;
	int max_anisotropy;           /* 1 .. MGL_MAX_ANISOTROPY */
} mgl_texture_t;

/* What glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT) answers: Metal's maxAnisotropy limit. */
#define MGL_MAX_ANISOTROPY 16

/* The texture object of a name, or NULL for a name past the table. */
mgl_texture_t *mgl_texture(GLuint name);
/* Releases every texture's Metal storage and resets the table. */
void mgl_textures_reset(void);
/* The sampler a texture's parameters ask for, packed: see mgl_layer.m. */
uint32_t mgl_sampler_key(const mgl_texture_t *t);

/*
 * Converts w x h pixels of format (GL_RGBA or GL_RGB, GL_UNSIGNED_BYTE)
 * laid out with the unpack alignment and row length into tightly packed
 * RGBA; opaque sets every alpha to 255 (an RGB internal format).
 */
void mgl_unpack_rgba(const unsigned char *src, GLenum format, int w, int h,
		int alignment, int row_length, int opaque, unsigned char *out);

/* ---- the draw path (mgl_draw.c) -------------------------------------------- */

/*
 * One vertex as the shaders read it (shaders.metal, struct mgl_vertex):
 * 64 bytes. pt is zero except on the corners of a smooth point's quad
 * (mgl_point_quads): the corner's offset from the point's centre in
 * pixels, the point's radius, and 1.
 */
typedef struct
{
	float pos[4];
	float tc[MGL_MAX_UNITS][2];
	float color[4];
	float pt[4];
} mgl_vertex_t;

/* The uniform block of a draw (shaders.metal, struct mgl_uniforms): 96 bytes. */
typedef struct
{
	float mvp[16];                /* projection * modelview */
	float alpha_ref;
	int32_t alpha_func;           /* GL_NEVER .. GL_ALWAYS minus GL_NEVER; GL_ALWAYS when the test is off */
	int32_t texenv0;              /* MGL_TEXENV_* (mgl_texenv.h) */
	int32_t tex_opaque0;
	int32_t texenv1;
	int32_t tex_opaque1;
	float rgb_scale0;             /* GL_RGB_SCALE of GL_COMBINE: 1, 2 or 4 */
	float rgb_scale1;
} mgl_uniforms_t;

/* Bounds of one draw: a GL_UNSIGNED_SHORT index reaches 65535. */
#define MGL_MAX_DRAW_VERTICES 65536
#define MGL_MAX_DRAW_INDICES (3 * MGL_MAX_DRAW_VERTICES)

typedef struct
{
	const mgl_vertex_t *vertices;
	size_t nvertices;
	const uint16_t *indices;      /* triangles */
	size_t nindices;
	mgl_uniforms_t u;
	uint32_t pipeline_key;        /* mgl_pipeline_key */
	void *texture0;               /* NULL when texenv0 is MGL_TEXENV_NONE */
	uint32_t sampler0;
	void *texture1;               /* NULL when texenv1 is MGL_TEXENV_NONE */
	uint32_t sampler1;
	uint64_t depth_stencil_key;   /* mgl_depth_stencil_key */
	uint32_t stencil_ref;         /* clamped to the 8 stencil bits */
	int cull;                     /* MGL_CULL_* */
	int fill_lines;               /* glPolygonMode GL_LINE */
	float depth_bias, depth_slope;   /* glPolygonOffset units and factor; 0 without GL_POLYGON_OFFSET_FILL */
	int lines;                    /* indices are pairs of a line list, not triangles */
} mgl_draw_t;

#define MGL_CULL_NONE 0
#define MGL_CULL_FRONT 1
#define MGL_CULL_BACK 2

/* Fills out[0..count) from the enabled client arrays, vertices first..first+count-1. */
void mgl_assemble(const mgl_state_t *s, GLint first, GLsizei count, mgl_vertex_t *out);

/*
 * The triangle list of count vertices drawn as mode (GL_TRIANGLES,
 * GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN). elements maps a position to a
 * vertex (NULL: the position is the vertex). Returns the number of
 * indices written, or -1 when mode is none of the three or the list
 * needs more than cap.
 */
long mgl_triangle_indices(GLenum mode, GLsizei count, const uint16_t *elements,
		uint16_t *out, size_t cap);

/*
 * The line list of count vertices drawn as mode (GL_LINES,
 * GL_LINE_STRIP, GL_LINE_LOOP), two indices a segment; elements as for
 * mgl_triangle_indices. Returns the number of indices written, or -1
 * when mode is none of the three or the list needs more than cap.
 */
long mgl_line_indices(GLenum mode, GLsizei count, const uint16_t *elements,
		uint16_t *out, size_t cap);

/*
 * Points (TASK52). GL's point size ranges are those of Apple's software
 * renderer, which made the gl1 references: GL_ALIASED_POINT_SIZE_RANGE
 * 1 to 8191, GL_SMOOTH_POINT_SIZE_RANGE 0.1 to 256. A point is drawn as
 * a quad of two triangles, so one draw holds at most a quarter of
 * MGL_MAX_DRAW_VERTICES points.
 */
#define MGL_POINT_ALIASED_MAX 8191.0f
#define MGL_POINT_SMOOTH_MIN 0.1f
#define MGL_POINT_SMOOTH_MAX 256.0f
#define MGL_MAX_DRAW_POINTS (MGL_MAX_DRAW_VERTICES / 4)

/*
 * The width in pixels GL rasterizes a point at whose eye coordinates are
 * eye: glPointSize attenuated by ARB_point_parameters and clamped to
 * GL_POINT_SIZE_MIN and _MAX, then to the smooth range, or rounded to a
 * whole width in the aliased range when smooth is 0.
 */
float mgl_point_size(const mgl_state_t *s, const float eye[4], int smooth);

/*
 * Expands the n points of in into quads in clip coordinates, to be
 * drawn with the identity matrix: GL 1.4 section 3.3. A point whose centre lies outside the clip volume is dropped
 * (section 2.12). A square point covers the size x size pixels around
 * its centre snapped as GL snaps it; a smooth one (GL_POINT_SMOOTH)
 * covers every pixel within its radius plus half a pixel and carries pt
 * for the coverage the fragment function applies (mgl_point_coverage).
 * Writes 4 vertices and 6 indices a point drawn and returns the number
 * of points drawn; out and idx must hold 4 n and 6 n.
 */
size_t mgl_point_quads(const mgl_state_t *s, const mgl_vertex_t *in, size_t n,
		mgl_vertex_t *out, uint16_t *idx);

/* The packed blend and colour-mask state of a draw. */
uint32_t mgl_pipeline_key(const mgl_state_t *s);
/* GL blend factor -> index 0..10 in the order of mgl_layer.m's table; -1 if none. */
int mgl_blend_index(GLenum factor);

/*
 * The packed depth and stencil state of a draw: bit 0 depth test, bits
 * 1-3 its function minus GL_NEVER, bit 4 depth write; bit 5 stencil
 * test, bits 6-8 its function, bits 9-11, 12-14 and 15-17 the fail,
 * depth-fail and pass operations (mgl_stencil_op_index), bits 18-25 the
 * read mask, bits 26-33 the write mask. 0 is "neither tests nor writes".
 */
uint64_t mgl_depth_stencil_key(const mgl_state_t *s);
/* GL stencil operation -> index 0..5 in the order of mgl_layer.m's table; -1 if none. */
int mgl_stencil_op_index(GLenum op);

/*
 * The per-frame vertex ring (design section 2): each frame in flight has
 * one shared buffer of MGL_RING_BYTES, and a draw's vertices and indices
 * are bump-allocated in it at MGL_RING_ALIGN. mgl_ring_alloc answers the
 * offset of n bytes, or -1 when the rest of the ring cannot hold them
 * (the layer then gives that draw a buffer of its own).
 */
#ifndef MGL_RING_BYTES
#define MGL_RING_BYTES (8u << 20)   /* a -D can shrink it to exercise the overflow path */
#endif
#include "../../metal/common/mtl_ring.h"
#define MGL_RING_ALIGN MTL_RING_ALIGN
typedef mtl_ring_t mgl_ring_t;
#define mgl_ring_alloc mtl_ring_alloc

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

/* A private rgba8Unorm texture with a full mip chain; NULL on failure. */
void *mgl_layer_tex_create(int w, int h, int levels);
void mgl_layer_tex_release(void *tex);
/*
 * Copies tightly packed RGBA into a region of one level, ordered after
 * every draw encoded so far (and before every later one); with
 * gen_mips levels 1 and up are then regenerated from level 0.
 */
int mgl_layer_tex_upload(void *tex, int level, int x, int y, int w, int h,
		const unsigned char *rgba, int gen_mips);
/* Encodes one draw into the frame, opening the frame or a Load pass as needed. */
int mgl_layer_draw(const mgl_draw_t *d);

/* Upper bounds on every wait, in milliseconds. */
#define MGL_FRAME_WAIT_MS 2000
#define MGL_GPU_WAIT_MS 5000

#endif /* MGL_INTERNAL_H */
