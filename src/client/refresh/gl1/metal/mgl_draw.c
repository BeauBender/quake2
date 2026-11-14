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
 * The draw path of the Metal shim: glDrawArrays and glDrawElements
 * become encoder draws (design section 2). The client arrays are
 * assembled into one interleaved vertex list (mgl_vertex_t, a missing
 * array filled from the current colour or a zero texture coordinate),
 * fans and strips are expanded into a triangle list, and the state is
 * reduced to a uniform block and the packed keys of the pipeline and
 * depth state, which mgl_layer.m caches. Everything here is plain C;
 * mgl_layer_draw encodes the result.
 *
 * As of TASK48 S2a the triangle modes draw, textured on unit 0 with
 * GL_REPLACE or GL_MODULATE, with blending, the alpha test and the
 * depth test. Points, lines, unit 1, GL_COMBINE, culling, stencil and
 * polygon offset are S2b and later cards: a draw that needs one sets
 * the sticky error and logs once (points and lines are not drawn, the
 * state is ignored).
 *
 * =======================================================================
 */

#include <string.h>

#include "mgl_internal.h"

/* One draw's vertices and indices; the bounds are checked before either is written. */
static mgl_vertex_t draw_vertices[MGL_MAX_DRAW_VERTICES];
static uint16_t draw_indices[MGL_MAX_DRAW_INDICES];
static uint16_t draw_elements[MGL_MAX_DRAW_INDICES];

/* Element i of a client array as four floats, GL's defaults (0, 0, 0, 1) for the rest. */
static void
mgl_fetch(const mgl_array_t *a, GLint i, float out[4])
{
	size_t esize = a->type == GL_FLOAT ? sizeof(GLfloat) :
		(a->type == GL_SHORT ? sizeof(GLshort) : sizeof(GLubyte));
	size_t stride = a->stride ? (size_t)a->stride : esize * (size_t)a->size;
	const unsigned char *p = (const unsigned char *)a->pointer + stride * (size_t)i;
	GLint k;

	out[0] = out[1] = out[2] = 0.0f;
	out[3] = 1.0f;

	for (k = 0; k < a->size; k++)
	{
		if (a->type == GL_FLOAT)
		{
			GLfloat f;

			memcpy(&f, p + k * sizeof(f), sizeof(f));
			out[k] = f;
		}
		else if (a->type == GL_SHORT)
		{
			GLshort v;

			memcpy(&v, p + k * sizeof(v), sizeof(v));
			out[k] = v;
		}
		else
		{
			/* unsigned bytes are colours: normalised */
			out[k] = p[k] / 255.0f;
		}
	}
}

void
mgl_assemble(const mgl_state_t *s, GLint first, GLsizei count, mgl_vertex_t *out)
{
	GLsizei i;
	int u;
	float t[4];

	for (i = 0; i < count; i++, out++)
	{
		mgl_fetch(&s->vertex_array, first + i, out->pos);

		for (u = 0; u < MGL_MAX_UNITS; u++)
		{
			/* gl1 never sets a current texture coordinate: GL's initial one is 0 */
			t[0] = t[1] = 0.0f;

			if (s->texcoord_array[u].enabled)
			{
				mgl_fetch(&s->texcoord_array[u], first + i, t);
			}

			out->tc[u][0] = t[0];
			out->tc[u][1] = t[1];
		}

		if (s->color_array.enabled)
		{
			mgl_fetch(&s->color_array, first + i, out->color);
		}
		else
		{
			memcpy(out->color, s->color, sizeof(out->color));
		}
	}
}

long
mgl_triangle_indices(GLenum mode, GLsizei count, const uint16_t *elements,
		uint16_t *out, size_t cap)
{
	size_t n, i, k = 0;
	size_t pos[3];

	switch (mode)
	{
		case GL_TRIANGLES:
			n = (size_t)count / 3;
			break;
		case GL_TRIANGLE_STRIP:
		case GL_TRIANGLE_FAN:
			n = count > 2 ? (size_t)count - 2 : 0;
			break;
		default:
			return -1;
	}

	if (n * 3 > cap)
	{
		return -1;
	}

	for (i = 0; i < n; i++)
	{
		if (mode == GL_TRIANGLES)
		{
			pos[0] = 3 * i;
			pos[1] = 3 * i + 1;
			pos[2] = 3 * i + 2;
		}
		else if (mode == GL_TRIANGLE_FAN)
		{
			pos[0] = 0;
			pos[1] = i + 1;
			pos[2] = i + 2;
		}
		else
		{
			/* GL 1.4, section 2.6.1: every odd triangle swaps its first two vertices */
			pos[0] = (i & 1) ? i + 1 : i;
			pos[1] = (i & 1) ? i : i + 1;
			pos[2] = i + 2;
		}

		out[k++] = elements ? elements[pos[0]] : (uint16_t)pos[0];
		out[k++] = elements ? elements[pos[1]] : (uint16_t)pos[1];
		out[k++] = elements ? elements[pos[2]] : (uint16_t)pos[2];
	}

	return (long)k;
}

int
mgl_blend_index(GLenum factor)
{
	static const GLenum order[] = {
		GL_ZERO, GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR,
		GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA_SATURATE
	};
	int i;

	for (i = 0; i < (int)(sizeof(order) / sizeof(order[0])); i++)
	{
		if (order[i] == factor)
		{
			return i;
		}
	}

	return -1;
}

/* Bit 0 blend on, bits 1-4 and 5-8 the source and destination factors, bits 9-12 the colour mask. */
uint32_t
mgl_pipeline_key(const mgl_state_t *s)
{
	uint32_t key = 0;
	int i;

	if (s->enables & MGL_EN_BLEND)
	{
		/* glBlendFunc stores only factors mgl_blend_index knows */
		key = 1u | ((uint32_t)mgl_blend_index(s->blend_src) << 1) |
			((uint32_t)mgl_blend_index(s->blend_dst) << 5);
	}

	for (i = 0; i < 4; i++)
	{
		key |= s->color_mask[i] ? (1u << (9 + i)) : 0u;
	}

	return key;
}

/*
 * Bits 0-2 the compare function (GL_NEVER .. GL_ALWAYS minus GL_NEVER),
 * bit 3 depth writes. GL neither tests nor writes depth while
 * GL_DEPTH_TEST is off, whatever glDepthMask says.
 */
uint32_t
mgl_depth_key(const mgl_state_t *s)
{
	if (!(s->enables & MGL_EN_DEPTH_TEST))
	{
		return (uint32_t)(GL_ALWAYS - GL_NEVER);
	}

	return (uint32_t)(s->depth_func - GL_NEVER) | (s->depth_mask ? 8u : 0u);
}

/* State this card does not draw yet: the sticky error and one line each, then drawn without it. */
static void
mgl_check_unsupported(const mgl_state_t *s)
{
	static int once_unit1, once_env, once_cull, once_stencil, once_offset, once_fill;
	const mgl_unit_t *u0 = &s->unit[0];

	if (s->unit[1].texture_2d)
	{
		mgl_unimplemented("drawing with texture unit 1", &once_unit1);
	}

	if (u0->texture_2d && u0->env_mode != GL_REPLACE && u0->env_mode != GL_MODULATE)
	{
		mgl_unimplemented("a texture environment other than GL_REPLACE and GL_MODULATE", &once_env);
	}

	if (s->enables & MGL_EN_CULL_FACE)
	{
		mgl_unimplemented("GL_CULL_FACE", &once_cull);
	}

	if (s->enables & MGL_EN_STENCIL_TEST)
	{
		mgl_unimplemented("GL_STENCIL_TEST", &once_stencil);
	}

	if (s->enables & MGL_EN_POLYGON_OFFSET)
	{
		mgl_unimplemented("GL_POLYGON_OFFSET_FILL", &once_offset);
	}

	if (s->polygon_mode_front != GL_FILL || s->polygon_mode_back != GL_FILL)
	{
		mgl_unimplemented("glPolygonMode other than GL_FILL", &once_fill);
	}
}

/* Builds the draw from the state and the assembled vertices and indices, and encodes it. */
static void
mgl_submit(size_t nvertices, size_t nindices)
{
	const mgl_state_t *s = &mgl_state;
	const mgl_unit_t *u0 = &s->unit[0];
	const mgl_texture_t *t = mgl_texture(u0->bound);
	mgl_draw_t d;

	if (nindices == 0)
	{
		return;
	}

	mgl_check_unsupported(s);

	memset(&d, 0, sizeof(d));
	d.vertices = draw_vertices;
	d.nvertices = nvertices;
	d.indices = draw_indices;
	d.nindices = nindices;

	mgl_mat_mul(d.u.mvp, s->projection[s->projection_depth], s->modelview[s->modelview_depth]);

	d.u.alpha_func = (int32_t)(((s->enables & MGL_EN_ALPHA_TEST) ? s->alpha_func : GL_ALWAYS) - GL_NEVER);
	d.u.alpha_ref = s->alpha_ref;

	/* a texture without storage is incomplete: GL draws as if the unit were off */
	if (u0->texture_2d && t && t->mtl)
	{
		d.u.texenv0 = u0->env_mode == GL_REPLACE ? MGL_TEXENV_REPLACE : MGL_TEXENV_MODULATE;
		d.u.tex_opaque0 = t->opaque ? 1 : 0;
		d.texture0 = t->mtl;
		d.sampler0 = mgl_sampler_key(t);
	}

	d.pipeline_key = mgl_pipeline_key(s);
	d.depth_key = mgl_depth_key(s);

	if (!mgl_layer_draw(&d))
	{
		mgl_error(GL_OUT_OF_MEMORY);
	}
}

static int
mgl_check_mode(GLenum mode)
{
	static int once_points, once_lines;

	switch (mode)
	{
		case GL_TRIANGLES:
		case GL_TRIANGLE_STRIP:
		case GL_TRIANGLE_FAN:
			return 1;
		case GL_POINTS:
			mgl_unimplemented("GL_POINTS", &once_points);
			return 0;
		case GL_LINES:
		case GL_LINE_LOOP:
		case GL_LINE_STRIP:
			mgl_unimplemented("line primitives", &once_lines);
			return 0;
		default:
			/* quads and polygons are GL too, but gl1 never draws them and the header has no name for them */
			mgl_error(GL_INVALID_ENUM);
			return 0;
	}
}

void
glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	long n;

	if (!mgl_check_mode(mode))
	{
		return;
	}

	if (first < 0 || count < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	/* the shim's bound on one draw, not GL's */
	if (count > MGL_MAX_DRAW_VERTICES)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (!mgl_state.vertex_array.enabled || count == 0)
	{
		return;
	}

	n = mgl_triangle_indices(mode, count, NULL, draw_indices, MGL_MAX_DRAW_INDICES);

	if (n < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	mgl_assemble(&mgl_state, first, count, draw_vertices);
	mgl_submit((size_t)count, (size_t)n);
}

void
glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	static int once_type;
	GLsizei i;
	long n;
	uint16_t max = 0;

	if (!mgl_check_mode(mode))
	{
		return;
	}

	if (count < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (type != GL_UNSIGNED_SHORT)
	{
		/* GL_UNSIGNED_BYTE and GL_UNSIGNED_INT are valid GL that gl1 never uses */
		if (type == GL_UNSIGNED_BYTE || type == GL_UNSIGNED_INT)
		{
			mgl_unimplemented("glDrawElements without GL_UNSIGNED_SHORT", &once_type);
		}
		else
		{
			mgl_error(GL_INVALID_ENUM);
		}

		return;
	}

	/* the shim's bound on one draw, not GL's */
	if (count > MGL_MAX_DRAW_INDICES)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	if (!mgl_state.vertex_array.enabled || count == 0 || !indices)
	{
		return;
	}

	memcpy(draw_elements, indices, (size_t)count * sizeof(uint16_t));

	for (i = 0; i < count; i++)
	{
		max = draw_elements[i] > max ? draw_elements[i] : max;
	}

	n = mgl_triangle_indices(mode, count, draw_elements, draw_indices, MGL_MAX_DRAW_INDICES);

	if (n < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	/* every vertex up to the largest index: gl1's buffer starts at vertex 0 */
	mgl_assemble(&mgl_state, 0, (GLsizei)max + 1, draw_vertices);
	mgl_submit((size_t)max + 1, (size_t)n);
}
