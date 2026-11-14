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
 * The GL state machine of the Metal shim: one mgl_state_t, the sticky
 * error, and the entry points that only record state. They validate
 * their arguments as GL does and change nothing else; the draw path
 * (mgl_draw.c) reads the struct. The matrix calls (glOrtho, glFrustum,
 * glRotatef, glTranslatef, glScalef) multiply the top of the current
 * stack as GL does.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "mgl_internal.h"

mgl_state_t mgl_state;

static unsigned long unimplemented_calls;

static void
mgl_identity(GLfloat *m)
{
	memset(m, 0, 16 * sizeof(GLfloat));
	m[0] = m[5] = m[10] = m[15] = 1.0f;
}

/* The initial GL state (OpenGL 1.4 specification, tables 6.x). */
void
mgl_state_reset(void)
{
	mgl_state_t *s = &mgl_state;
	int i;

	memset(s, 0, sizeof(*s));

	s->error = GL_NO_ERROR;
	s->enables = MGL_EN_MULTISAMPLE;   /* GL_MULTISAMPLE starts enabled */

	s->blend_src = GL_ONE;
	s->blend_dst = GL_ZERO;

	s->depth_func = GL_LESS;
	s->depth_mask = GL_TRUE;
	s->depth_near = 0.0;
	s->depth_far = 1.0;

	s->alpha_func = GL_ALWAYS;
	s->alpha_ref = 0.0f;

	s->stencil_func = GL_ALWAYS;
	s->stencil_ref = 0;
	s->stencil_value_mask = ~0u;
	s->stencil_fail = s->stencil_zfail = s->stencil_zpass = GL_KEEP;
	s->stencil_write_mask = ~0u;
	s->clear_stencil = 0;

	s->cull_face = GL_BACK;
	s->color_mask[0] = s->color_mask[1] = s->color_mask[2] = s->color_mask[3] = GL_TRUE;
	s->polygon_mode_front = s->polygon_mode_back = GL_FILL;
	s->shade_model = GL_SMOOTH;

	s->point_size = 1.0f;
	s->point_size_min = 0.0f;
	s->point_size_max = MGL_POINT_ALIASED_MAX;   /* GL: the largest size the implementation has */
	s->point_attenuation[0] = 1.0f;

	s->color[0] = s->color[1] = s->color[2] = s->color[3] = 1.0f;

	s->draw_buffer = GL_BACK;
	s->hint_multisample_filter = GL_DONT_CARE;

	for (i = 0; i < MGL_MAX_UNITS; i++)
	{
		s->unit[i].env_mode = GL_MODULATE;
		s->unit[i].combine_rgb = GL_MODULATE;
		s->unit[i].combine_alpha = GL_MODULATE;
		s->unit[i].rgb_scale = 1;
		s->texcoord_array[i].size = 4;
		s->texcoord_array[i].type = GL_FLOAT;
	}

	s->vertex_array.size = 4;
	s->vertex_array.type = GL_FLOAT;
	s->color_array.size = 4;
	s->color_array.type = GL_FLOAT;

	s->matrix_mode = GL_MODELVIEW;
	mgl_identity(s->modelview[0]);
	mgl_identity(s->projection[0]);

	s->pack_alignment = 4;
	s->unpack_alignment = 4;
}

/* GL keeps the first error until glGetError reads it. */
void
mgl_error(GLenum error)
{
	if (mgl_state.error == GL_NO_ERROR)
	{
		mgl_state.error = error;
	}
}

void
mgl_unimplemented(const char *name, int *once)
{
	unimplemented_calls++;
	mgl_error(GL_INVALID_OPERATION);

	if (!*once)
	{
		*once = 1;
		fprintf(stderr, "mgl: %s is not implemented yet (GL_INVALID_OPERATION)\n", name);
	}
}

unsigned long
mgl_unimplemented_calls(void)
{
	return unimplemented_calls;
}

GLenum
glGetError(void)
{
	GLenum e = mgl_state.error;

	mgl_state.error = GL_NO_ERROR;
	return e;
}

/* ---- capabilities ---------------------------------------------------- */

static GLuint
mgl_cap_bit(GLenum cap)
{
	switch (cap)
	{
		case GL_ALPHA_TEST: return MGL_EN_ALPHA_TEST;
		case GL_BLEND: return MGL_EN_BLEND;
		case GL_CULL_FACE: return MGL_EN_CULL_FACE;
		case GL_DEPTH_TEST: return MGL_EN_DEPTH_TEST;
		case GL_POINT_SMOOTH: return MGL_EN_POINT_SMOOTH;
		case GL_POLYGON_OFFSET_FILL: return MGL_EN_POLYGON_OFFSET;
		case GL_SCISSOR_TEST: return MGL_EN_SCISSOR_TEST;
		case GL_STENCIL_TEST: return MGL_EN_STENCIL_TEST;
		case GL_MULTISAMPLE: return MGL_EN_MULTISAMPLE;
		case GL_SHARED_TEXTURE_PALETTE_EXT: return MGL_EN_SHARED_PALETTE;
		default: return 0;
	}
}

static void
mgl_set_cap(GLenum cap, GLboolean on)
{
	GLuint bit;

	if (cap == GL_TEXTURE_2D)
	{
		mgl_state.unit[mgl_state.active_unit].texture_2d = on;
		return;
	}

	bit = mgl_cap_bit(cap);

	if (!bit)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	if (on)
	{
		mgl_state.enables |= bit;
	}
	else
	{
		mgl_state.enables &= ~bit;
	}

	if (bit == MGL_EN_SCISSOR_TEST)
	{
		mgl_layer_update_raster();
	}
}

void
glEnable(GLenum cap)
{
	mgl_set_cap(cap, GL_TRUE);
}

void
glDisable(GLenum cap)
{
	mgl_set_cap(cap, GL_FALSE);
}

/* ---- per-fragment state ----------------------------------------------- */

static int
mgl_is_func(GLenum f)
{
	return f >= GL_NEVER && f <= GL_ALWAYS;
}

static int
mgl_is_blend_factor(GLenum f, int src)
{
	switch (f)
	{
		case GL_ZERO:
		case GL_ONE:
		case GL_SRC_ALPHA:
		case GL_ONE_MINUS_SRC_ALPHA:
		case GL_DST_ALPHA:
		case GL_ONE_MINUS_DST_ALPHA:
			return 1;
		case GL_SRC_COLOR:
		case GL_ONE_MINUS_SRC_COLOR:
			return !src;
		case GL_DST_COLOR:
		case GL_ONE_MINUS_DST_COLOR:
		case GL_SRC_ALPHA_SATURATE:
			return src;
		default:
			return 0;
	}
}

static GLclampf
mgl_clampf(GLfloat v)
{
	return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

void
glBlendFunc(GLenum sfactor, GLenum dfactor)
{
	if (!mgl_is_blend_factor(sfactor, 1) || !mgl_is_blend_factor(dfactor, 0))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.blend_src = sfactor;
	mgl_state.blend_dst = dfactor;
}

void
glAlphaFunc(GLenum func, GLclampf ref)
{
	if (!mgl_is_func(func))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.alpha_func = func;
	mgl_state.alpha_ref = mgl_clampf(ref);
}

void
glDepthFunc(GLenum func)
{
	if (!mgl_is_func(func))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.depth_func = func;
}

void
glDepthMask(GLboolean flag)
{
	mgl_state.depth_mask = flag ? GL_TRUE : GL_FALSE;
}

void
glStencilFunc(GLenum func, GLint ref, GLuint mask)
{
	if (!mgl_is_func(func))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.stencil_func = func;
	mgl_state.stencil_ref = ref;
	mgl_state.stencil_value_mask = mask;
}

static int
mgl_is_stencil_op(GLenum op)
{
	switch (op)
	{
		case GL_ZERO:
		case GL_KEEP:
		case GL_REPLACE:
		case GL_INCR:
		case GL_DECR:
		case GL_INVERT:
			return 1;
		default:
			return 0;
	}
}

void
glStencilOp(GLenum fail, GLenum zfail, GLenum zpass)
{
	if (!mgl_is_stencil_op(fail) || !mgl_is_stencil_op(zfail) || !mgl_is_stencil_op(zpass))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.stencil_fail = fail;
	mgl_state.stencil_zfail = zfail;
	mgl_state.stencil_zpass = zpass;
}

void
glStencilMask(GLuint mask)
{
	mgl_state.stencil_write_mask = mask;
}

void
glColorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
	mgl_state.color_mask[0] = red ? GL_TRUE : GL_FALSE;
	mgl_state.color_mask[1] = green ? GL_TRUE : GL_FALSE;
	mgl_state.color_mask[2] = blue ? GL_TRUE : GL_FALSE;
	mgl_state.color_mask[3] = alpha ? GL_TRUE : GL_FALSE;
}

/* ---- rasterization state ---------------------------------------------- */

void
glCullFace(GLenum mode)
{
	if (mode != GL_FRONT && mode != GL_BACK && mode != GL_FRONT_AND_BACK)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.cull_face = mode;
}

void
glPolygonMode(GLenum face, GLenum mode)
{
	if ((face != GL_FRONT && face != GL_BACK && face != GL_FRONT_AND_BACK) ||
		(mode != GL_POINT && mode != GL_LINE && mode != GL_FILL))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	if (face != GL_BACK)
	{
		mgl_state.polygon_mode_front = mode;
	}

	if (face != GL_FRONT)
	{
		mgl_state.polygon_mode_back = mode;
	}
}

void
glPolygonOffset(GLfloat factor, GLfloat units)
{
	mgl_state.polygon_offset_factor = factor;
	mgl_state.polygon_offset_units = units;
}

void
glShadeModel(GLenum mode)
{
	if (mode != GL_FLAT && mode != GL_SMOOTH)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.shade_model = mode;
}

void
glPointSize(GLfloat size)
{
	/* the negated form refuses NaN as well */
	if (!(size > 0.0f))
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	mgl_state.point_size = size;
}

void
glPointParameterf(GLenum pname, GLfloat param)
{
	switch (pname)
	{
		case GL_POINT_SIZE_MIN:
		case GL_POINT_SIZE_MAX:
			if (param < 0.0f)
			{
				mgl_error(GL_INVALID_VALUE);
				return;
			}

			if (pname == GL_POINT_SIZE_MIN)
			{
				mgl_state.point_size_min = param;
			}
			else
			{
				mgl_state.point_size_max = param;
			}
			break;
		default:
			mgl_error(GL_INVALID_ENUM);
	}
}

void
glPointParameterfv(GLenum pname, const GLfloat *params)
{
	if (!params)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (pname == GL_POINT_DISTANCE_ATTENUATION)
	{
		mgl_state.point_attenuation[0] = params[0];
		mgl_state.point_attenuation[1] = params[1];
		mgl_state.point_attenuation[2] = params[2];
		return;
	}

	glPointParameterf(pname, params[0]);
}

void
glHint(GLenum target, GLenum mode)
{
	if (mode != GL_DONT_CARE && mode != GL_FASTEST && mode != GL_NICEST)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	/* A hint changes nothing in the shim; only the one gl1 gives is known. */
	if (target != GL_MULTISAMPLE_FILTER_HINT_NV)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.hint_multisample_filter = mode;
}

/* ---- current color ------------------------------------------------------ */

void
glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
	mgl_state.color[0] = red;
	mgl_state.color[1] = green;
	mgl_state.color[2] = blue;
	mgl_state.color[3] = alpha;
}

void
glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
	mgl_state.color[0] = red / 255.0f;
	mgl_state.color[1] = green / 255.0f;
	mgl_state.color[2] = blue / 255.0f;
	mgl_state.color[3] = alpha / 255.0f;
}

/* ---- texture units and environment -------------------------------------- */

static int
mgl_unit_index(GLenum texture, GLuint *index)
{
	if (texture < GL_TEXTURE0 || texture >= GL_TEXTURE0 + MGL_MAX_UNITS)
	{
		return 0;
	}

	*index = texture - GL_TEXTURE0;
	return 1;
}

void
glActiveTexture(GLenum texture)
{
	GLuint i;

	if (!mgl_unit_index(texture, &i))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.active_unit = i;
}

void
glClientActiveTexture(GLenum texture)
{
	GLuint i;

	if (!mgl_unit_index(texture, &i))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	mgl_state.client_unit = i;
}

void
glTexEnvi(GLenum target, GLenum pname, GLint param)
{
	mgl_unit_t *u = &mgl_state.unit[mgl_state.active_unit];

	if (target != GL_TEXTURE_ENV)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	switch (pname)
	{
		case GL_TEXTURE_ENV_MODE:
			if (param != GL_MODULATE && param != GL_REPLACE && param != GL_DECAL &&
				param != GL_ADD && param != GL_COMBINE)
			{
				mgl_error(GL_INVALID_ENUM);
				return;
			}
			u->env_mode = (GLenum)param;
			break;
		case GL_COMBINE_RGB:
			u->combine_rgb = (GLenum)param;
			break;
		case GL_COMBINE_ALPHA:
			u->combine_alpha = (GLenum)param;
			break;
		case GL_RGB_SCALE:
			if (param != 1 && param != 2 && param != 4)
			{
				mgl_error(GL_INVALID_VALUE);
				return;
			}
			u->rgb_scale = param;
			break;
		default:
			mgl_error(GL_INVALID_ENUM);
	}
}

/* ---- client arrays -------------------------------------------------------- */

static mgl_array_t *
mgl_client_array(GLenum array)
{
	switch (array)
	{
		case GL_VERTEX_ARRAY: return &mgl_state.vertex_array;
		case GL_COLOR_ARRAY: return &mgl_state.color_array;
		case GL_TEXTURE_COORD_ARRAY: return &mgl_state.texcoord_array[mgl_state.client_unit];
		default: return NULL;
	}
}

void
glEnableClientState(GLenum array)
{
	mgl_array_t *a = mgl_client_array(array);

	if (!a)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	a->enabled = GL_TRUE;
}

void
glDisableClientState(GLenum array)
{
	mgl_array_t *a = mgl_client_array(array);

	if (!a)
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	a->enabled = GL_FALSE;
}

static void
mgl_set_pointer(mgl_array_t *a, GLint size, GLenum type, GLsizei stride,
		const GLvoid *pointer, GLint min_size, int allow_ubyte, int allow_short)
{
	if (size < min_size || size > 4 || stride < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (type != GL_FLOAT && !(allow_ubyte && type == GL_UNSIGNED_BYTE) &&
		!(allow_short && type == GL_SHORT))
	{
		mgl_error(GL_INVALID_ENUM);
		return;
	}

	a->size = size;
	a->type = type;
	a->stride = stride;
	a->pointer = pointer;
}

void
glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	mgl_set_pointer(&mgl_state.vertex_array, size, type, stride, pointer, 2, 0, 1);
}

void
glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	mgl_set_pointer(&mgl_state.texcoord_array[mgl_state.client_unit],
			size, type, stride, pointer, 1, 0, 1);
}

void
glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *pointer)
{
	mgl_set_pointer(&mgl_state.color_array, size, type, stride, pointer, 3, 1, 0);
}

/* ---- matrix stacks ---------------------------------------------------------- */

static GLfloat *
mgl_top(void)
{
	if (mgl_state.matrix_mode == GL_PROJECTION)
	{
		return mgl_state.projection[mgl_state.projection_depth];
	}

	return mgl_state.modelview[mgl_state.modelview_depth];
}

void
glMatrixMode(GLenum mode)
{
	if (mode != GL_MODELVIEW && mode != GL_PROJECTION)
	{
		/* GL_TEXTURE is valid GL, but gl1 never uses it and the shim has no texture stack. */
		mgl_error(mode == GL_TEXTURE ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
		return;
	}

	mgl_state.matrix_mode = mode;
}

void
glLoadIdentity(void)
{
	mgl_identity(mgl_top());
}

void
glLoadMatrixf(const GLfloat *m)
{
	if (!m)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	memcpy(mgl_top(), m, 16 * sizeof(GLfloat));
}

void
glPushMatrix(void)
{
	GLint *depth = mgl_state.matrix_mode == GL_PROJECTION ?
		&mgl_state.projection_depth : &mgl_state.modelview_depth;
	GLfloat (*stack)[16] = mgl_state.matrix_mode == GL_PROJECTION ?
		mgl_state.projection : mgl_state.modelview;

	if (*depth + 1 >= MGL_STACK_DEPTH)
	{
		mgl_error(GL_STACK_OVERFLOW);
		return;
	}

	memcpy(stack[*depth + 1], stack[*depth], 16 * sizeof(GLfloat));
	(*depth)++;
}

void
glPopMatrix(void)
{
	GLint *depth = mgl_state.matrix_mode == GL_PROJECTION ?
		&mgl_state.projection_depth : &mgl_state.modelview_depth;

	if (*depth == 0)
	{
		mgl_error(GL_STACK_UNDERFLOW);
		return;
	}

	(*depth)--;
}

void
glGetFloatv(GLenum pname, GLfloat *params)
{
	if (!params)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	switch (pname)
	{
		case GL_MODELVIEW_MATRIX:
			memcpy(params, mgl_state.modelview[mgl_state.modelview_depth], 16 * sizeof(GLfloat));
			break;
		case GL_PROJECTION_MATRIX:
			memcpy(params, mgl_state.projection[mgl_state.projection_depth], 16 * sizeof(GLfloat));
			break;
		case GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT:
			params[0] = (GLfloat)MGL_MAX_ANISOTROPY;
			break;
		default:
			/* Not answered: nothing written. */
			mgl_error(GL_INVALID_ENUM);
	}
}

/*
 * The matrix arithmetic (GL 1.4, section 2.11.2): each call multiplies
 * the top of the current stack on the right by its matrix, so the last
 * one named is the first applied to a vertex.
 */

static void
mgl_mult(const GLfloat *m)
{
	mgl_mat_mul(mgl_top(), mgl_top(), m);
}

void
glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
	double len = sqrt((double)x * x + (double)y * y + (double)z * z);
	double a = angle * (M_PI / 180.0), c = cos(a), s = sin(a), nx, ny, nz;
	GLfloat m[16];

	/* GL leaves a rotation about the zero vector undefined: the matrix stays */
	if (len == 0.0)
	{
		return;
	}

	nx = x / len;
	ny = y / len;
	nz = z / len;

	memset(m, 0, sizeof(m));
	m[0] = (GLfloat)(nx * nx * (1 - c) + c);
	m[1] = (GLfloat)(ny * nx * (1 - c) + nz * s);
	m[2] = (GLfloat)(nx * nz * (1 - c) - ny * s);
	m[4] = (GLfloat)(nx * ny * (1 - c) - nz * s);
	m[5] = (GLfloat)(ny * ny * (1 - c) + c);
	m[6] = (GLfloat)(ny * nz * (1 - c) + nx * s);
	m[8] = (GLfloat)(nx * nz * (1 - c) + ny * s);
	m[9] = (GLfloat)(ny * nz * (1 - c) - nx * s);
	m[10] = (GLfloat)(nz * nz * (1 - c) + c);
	m[15] = 1.0f;

	mgl_mult(m);
}

void
glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16];

	mgl_identity(m);
	m[12] = x;
	m[13] = y;
	m[14] = z;
	mgl_mult(m);
}

void
glScalef(GLfloat x, GLfloat y, GLfloat z)
{
	GLfloat m[16];

	mgl_identity(m);
	m[0] = x;
	m[5] = y;
	m[10] = z;
	mgl_mult(m);
}

/* The current matrix times the orthographic projection (GL 1.4, section 2.11.2). */
void
glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
		GLdouble zNear, GLdouble zFar)
{
	GLfloat m[16];

	if (left == right || bottom == top || zNear == zFar)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	memset(m, 0, sizeof(m));
	m[0] = (GLfloat)(2.0 / (right - left));
	m[5] = (GLfloat)(2.0 / (top - bottom));
	m[10] = (GLfloat)(-2.0 / (zFar - zNear));
	m[12] = (GLfloat)(-(right + left) / (right - left));
	m[13] = (GLfloat)(-(top + bottom) / (top - bottom));
	m[14] = (GLfloat)(-(zFar + zNear) / (zFar - zNear));
	m[15] = 1.0f;

	mgl_mult(m);
}

/* The current matrix times the perspective projection (GL 1.4, section 2.11.2). */
void
glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top,
		GLdouble zNear, GLdouble zFar)
{
	GLfloat m[16];

	if (zNear <= 0 || zFar <= 0 || left == right || bottom == top || zNear == zFar)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	memset(m, 0, sizeof(m));
	m[0] = (GLfloat)(2.0 * zNear / (right - left));
	m[5] = (GLfloat)(2.0 * zNear / (top - bottom));
	m[8] = (GLfloat)((right + left) / (right - left));
	m[9] = (GLfloat)((top + bottom) / (top - bottom));
	m[10] = (GLfloat)(-(zFar + zNear) / (zFar - zNear));
	m[11] = -1.0f;
	m[14] = (GLfloat)(-2.0 * zFar * zNear / (zFar - zNear));

	mgl_mult(m);
}

/* Paletted textures: never advertised, so gl1 never loads this pointer. */
void
glColorTableEXT(GLenum target, GLenum internalformat, GLsizei width,
		GLenum format, GLenum type, const GLvoid *table)
{
	(void)target; (void)internalformat; (void)width; (void)format; (void)type; (void)table;
	MGL_UNIMPLEMENTED();
}
