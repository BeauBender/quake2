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
 * reduced to a uniform block and the packed key of the pipeline state,
 * which mgl_layer.m caches. Everything here is plain C; mgl_layer_draw
 * encodes the result.
 *
 * The triangle modes draw, textured on units 0 and 1 with GL_REPLACE,
 * GL_MODULATE or gl1's GL_COMBINE (modulate with GL_RGB_SCALE), with
 * blending, the alpha test, the depth and stencil tests, culling,
 * polygon offset and line fill (TASK48, TASK82, TASK49). Lines are
 * Metal lines; points are quads computed here, square or smooth, at the
 * size ARB_point_parameters derives (TASK52).
 *
 * =======================================================================
 */

#include <math.h>
#include <string.h>

#include "mgl_internal.h"

/* One draw's vertices and indices; the bounds are checked before either is written. */
static mgl_vertex_t draw_vertices[MGL_MAX_DRAW_VERTICES];
static uint16_t draw_indices[MGL_MAX_DRAW_INDICES];
static uint16_t draw_elements[MGL_MAX_DRAW_INDICES];
static mgl_vertex_t draw_points[MGL_MAX_DRAW_POINTS];

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

		memset(out->pt, 0, sizeof(out->pt));
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

long
mgl_line_indices(GLenum mode, GLsizei count, const uint16_t *elements,
		uint16_t *out, size_t cap)
{
	size_t n, i, k = 0, a, b;

	switch (mode)
	{
		case GL_LINES:
			n = (size_t)count / 2;
			break;
		case GL_LINE_STRIP:
			n = count > 1 ? (size_t)count - 1 : 0;
			break;
		case GL_LINE_LOOP:
			/* GL 1.4, section 2.6.1: the last vertex joins the first */
			n = count > 1 ? (size_t)count : 0;
			break;
		default:
			return -1;
	}

	if (n * 2 > cap)
	{
		return -1;
	}

	for (i = 0; i < n; i++)
	{
		a = mode == GL_LINES ? 2 * i : i;
		b = mode == GL_LINES ? 2 * i + 1 : (i + 1 == (size_t)count ? 0 : i + 1);
		out[k++] = elements ? elements[a] : (uint16_t)a;
		out[k++] = elements ? elements[b] : (uint16_t)b;
	}

	return (long)k;
}

/* out = m * (x, y, z, w), column-major as GL keeps m. */
static void
mgl_xform(const GLfloat *m, const float *v, double *out)
{
	int r;

	for (r = 0; r < 4; r++)
	{
		out[r] = (double)m[r] * v[0] + (double)m[4 + r] * v[1] +
			(double)m[8 + r] * v[2] + (double)m[12 + r] * v[3];
	}
}

float
mgl_point_size(const mgl_state_t *s, const float eye[4], int smooth)
{
	const GLfloat *a = s->point_attenuation;
	double d2 = (double)eye[0] * eye[0] + (double)eye[1] * eye[1] + (double)eye[2] * eye[2];
	double den = a[0] + a[1] * sqrt(d2) + a[2] * d2;
	double size = s->point_size;

	/*
	 * ARB_point_parameters: size * sqrt(1 / (a + b d + c d^2)), d the
	 * distance of the eye coordinates from the eye (Apple's renderer
	 * uses the distance, not |z|: TASK52 measured points off the axis).
	 * A denominator of 0 or less, which GL leaves undefined, leaves the
	 * size as it is.
	 */
	if (den > 0.0)
	{
		size /= sqrt(den);
	}

	size = size < s->point_size_min ? s->point_size_min : size;
	size = size > s->point_size_max ? s->point_size_max : size;

	if (smooth)
	{
		size = size < MGL_POINT_SMOOTH_MIN ? MGL_POINT_SMOOTH_MIN : size;
		return (float)(size > MGL_POINT_SMOOTH_MAX ? MGL_POINT_SMOOTH_MAX : size);
	}

	/* GL 1.4, section 3.3: a whole width, rounded, at least 1 */
	size = floor(size + 0.5);
	size = size < 1.0 ? 1.0 : size;
	return (float)(size > MGL_POINT_ALIASED_MAX ? MGL_POINT_ALIASED_MAX : size);
}

size_t
mgl_point_quads(const mgl_state_t *s, const mgl_vertex_t *in, size_t n, mgl_vertex_t *out, uint16_t *idx)
{
	static const double corner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
	const GLfloat *mv = s->modelview[s->modelview_depth];
	GLfloat mvp[16];
	double clip[4], eye[4], vx = s->viewport[0], vy = s->viewport[1];
	double vw = s->viewport[2], vh = s->viewport[3], xw, yw, cx, cy, half;
	float e[4], size;
	int smooth = (s->enables & MGL_EN_POINT_SMOOTH) != 0, c;
	size_t i, k = 0;

	if (vw <= 0 || vh <= 0)
	{
		return 0;
	}

	mgl_mat_mul(mvp, s->projection[s->projection_depth], mv);

	for (i = 0; i < n; i++)
	{
		const mgl_vertex_t *p = &in[i];

		mgl_xform(mvp, p->pos, clip);

		/* GL 1.4, section 2.12: a point outside the clip volume is discarded */
		if (!(clip[3] > 0) || fabs(clip[0]) > clip[3] || fabs(clip[1]) > clip[3] || fabs(clip[2]) > clip[3])
		{
			continue;
		}

		mgl_xform(mv, p->pos, eye);
		e[0] = (float)eye[0];
		e[1] = (float)eye[1];
		e[2] = (float)eye[2];
		e[3] = (float)eye[3];
		size = mgl_point_size(s, e, smooth);

		/* window coordinates, GL's bottom-left origin */
		xw = vx + (clip[0] / clip[3] + 1) * vw * 0.5;
		yw = vy + (clip[1] / clip[3] + 1) * vh * 0.5;

		if (smooth)
		{
			/* section 3.3.2: a disc about the exact centre; no pixel past r + 1/2 is covered */
			cx = xw;
			cy = yw;
			half = size * 0.5 + 0.5;
		}
		else if ((long)size & 1)
		{
			/* section 3.3.1: an odd width is centred on a pixel, an even one on a corner */
			cx = floor(xw) + 0.5;
			cy = floor(yw) + 0.5;
			half = size * 0.5;
		}
		else
		{
			cx = floor(xw + 0.5);
			cy = floor(yw + 0.5);
			half = size * 0.5;
		}

		for (c = 0; c < 4; c++)
		{
			mgl_vertex_t *q = &out[4 * k + c];
			double x = cx + corner[c][0] * half, y = cy + corner[c][1] * half;

			*q = *p;
			/* back to clip coordinates at the point's w: the same depth on every corner */
			q->pos[0] = (float)(((x - vx) * 2 / vw - 1) * clip[3]);
			q->pos[1] = (float)(((y - vy) * 2 / vh - 1) * clip[3]);
			q->pos[2] = (float)clip[2];
			q->pos[3] = (float)clip[3];
			q->pt[0] = (float)(corner[c][0] * half);
			q->pt[1] = (float)(corner[c][1] * half);
			q->pt[2] = size * 0.5f;
			q->pt[3] = smooth ? 1.0f : 0.0f;
		}

		idx[6 * k] = (uint16_t)(4 * k);
		idx[6 * k + 1] = (uint16_t)(4 * k + 1);
		idx[6 * k + 2] = (uint16_t)(4 * k + 2);
		idx[6 * k + 3] = (uint16_t)(4 * k);
		idx[6 * k + 4] = (uint16_t)(4 * k + 2);
		idx[6 * k + 5] = (uint16_t)(4 * k + 3);
		k++;
	}

	return k;
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

int
mgl_stencil_op_index(GLenum op)
{
	static const GLenum order[] = {GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT};
	int i;

	for (i = 0; i < (int)(sizeof(order) / sizeof(order[0])); i++)
	{
		if (order[i] == op)
		{
			return i;
		}
	}

	return -1;
}

uint64_t
mgl_depth_stencil_key(const mgl_state_t *s)
{
	uint64_t key = 0;

	/* GL writes depth only where it tests it (GL 1.4, section 4.1.5) */
	if (s->enables & MGL_EN_DEPTH_TEST)
	{
		key = 1u | ((uint64_t)(s->depth_func - GL_NEVER) << 1) | (s->depth_mask ? (1u << 4) : 0u);
	}

	/* without the stencil test GL neither tests nor changes the stencil buffer */
	if (s->enables & MGL_EN_STENCIL_TEST)
	{
		/* glStencilFunc and glStencilOp store only values these know */
		key |= (1u << 5) | ((uint64_t)(s->stencil_func - GL_NEVER) << 6) |
			((uint64_t)mgl_stencil_op_index(s->stencil_fail) << 9) |
			((uint64_t)mgl_stencil_op_index(s->stencil_zfail) << 12) |
			((uint64_t)mgl_stencil_op_index(s->stencil_zpass) << 15) |
			((uint64_t)(s->stencil_value_mask & 0xff) << 18) |
			((uint64_t)(s->stencil_write_mask & 0xff) << 26);
	}

	return key;
}

/*
 * State this card does not draw yet: the sticky error and one line
 * each, then drawn without it.
 */
static void
mgl_check_unsupported(const mgl_state_t *s)
{
	static int once_fill;

	/* Metal has one fill mode for both faces and no point fill */
	if (s->polygon_mode_front != s->polygon_mode_back || s->polygon_mode_front == GL_POINT)
	{
		mgl_unimplemented("glPolygonMode GL_POINT, or a different mode per face", &once_fill);
	}
}

/*
 * The texture environment of unit i in a draw. A unit that is off, or
 * whose texture has no storage (incomplete in GL), is MGL_TEXENV_NONE
 * and passes the previous colour. GL_DECAL, GL_ADD and GL_COMBINE with
 * a function other than GL_MODULATE, which gl1 never sets, set the
 * sticky error, log once and draw as GL_MODULATE.
 */
static void
mgl_unit_env(const mgl_state_t *s, int i, int32_t *env, int32_t *opaque, float *scale,
		void **tex, uint32_t *sampler)
{
	static int once_env, once_combine;
	const mgl_unit_t *u = &s->unit[i];
	const mgl_texture_t *t = mgl_texture(u->bound);

	*env = MGL_TEXENV_NONE;
	*opaque = 0;
	*scale = 1.0f;
	*tex = NULL;
	*sampler = 0;

	if (!u->texture_2d || !t || !t->mtl)
	{
		return;
	}

	switch (u->env_mode)
	{
		case GL_REPLACE:
			*env = MGL_TEXENV_REPLACE;
			break;
		case GL_MODULATE:
			*env = MGL_TEXENV_MODULATE;
			break;
		case GL_COMBINE:
			if (u->combine_rgb != GL_MODULATE || u->combine_alpha != GL_MODULATE)
			{
				mgl_unimplemented("GL_COMBINE with a function other than GL_MODULATE", &once_combine);
			}

			*env = MGL_TEXENV_COMBINE;
			/* glTexEnvi stores only 1, 2 and 4 */
			*scale = (float)u->rgb_scale;
			break;
		default:
			mgl_unimplemented("GL_DECAL and GL_ADD", &once_env);
			*env = MGL_TEXENV_MODULATE;
	}

	*opaque = t->opaque ? 1 : 0;
	*tex = t->mtl;
	*sampler = mgl_sampler_key(t);
}

/* What a draw's indices form: GL's triangles, GL's lines, or the quads of GL's points. */
#define MGL_KIND_TRIANGLES 0
#define MGL_KIND_LINES 1
#define MGL_KIND_POINTS 2

/*
 * The rasterizer state of a draw: cull, fill mode, polygon offset and
 * the stencil reference. Returns 0 when GL draws no triangle at all
 * (both faces culled). Culling, the polygon mode and
 * GL_POLYGON_OFFSET_FILL are for polygons: GL applies none of them to
 * lines and points, so neither does the shim (a point's quad included).
 */
static int
mgl_raster_state(const mgl_state_t *s, int kind, mgl_draw_t *d)
{
	d->cull = MGL_CULL_NONE;

	/* GL clamps the reference to the stencil bits (8) */
	d->stencil_ref = s->stencil_ref < 0 ? 0u : (s->stencil_ref > 0xff ? 0xffu : (uint32_t)s->stencil_ref);
	d->depth_stencil_key = mgl_depth_stencil_key(s);
	d->lines = kind == MGL_KIND_LINES;

	if (kind != MGL_KIND_TRIANGLES)
	{
		return 1;
	}

	if (s->enables & MGL_EN_CULL_FACE)
	{
		if (s->cull_face == GL_FRONT_AND_BACK)
		{
			return 0;
		}

		/* the front face is counter-clockwise, GL's default (gl1 never calls glFrontFace) */
		d->cull = s->cull_face == GL_FRONT ? MGL_CULL_FRONT : MGL_CULL_BACK;
	}

	d->fill_lines = s->polygon_mode_front == GL_LINE;

	if (s->enables & MGL_EN_POLYGON_OFFSET)
	{
		d->depth_bias = s->polygon_offset_units;
		d->depth_slope = s->polygon_offset_factor;
	}

	return 1;
}

/* Builds the draw from the state and the assembled vertices and indices, and encodes it. */
static void
mgl_submit(int kind, size_t nvertices, size_t nindices)
{
	static const GLfloat identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	const mgl_state_t *s = &mgl_state;
	mgl_draw_t d;

	if (nindices == 0)
	{
		return;
	}

	if (kind == MGL_KIND_TRIANGLES)
	{
		mgl_check_unsupported(s);
	}

	memset(&d, 0, sizeof(d));
	d.vertices = draw_vertices;
	d.nvertices = nvertices;
	d.indices = draw_indices;
	d.nindices = nindices;

	/* a point's quad is already in clip coordinates (mgl_point_quads) */
	if (kind == MGL_KIND_POINTS)
	{
		memcpy(d.u.mvp, identity, sizeof(identity));
	}
	else
	{
		mgl_mat_mul(d.u.mvp, s->projection[s->projection_depth], s->modelview[s->modelview_depth]);
	}

	d.u.alpha_func = (int32_t)(((s->enables & MGL_EN_ALPHA_TEST) ? s->alpha_func : GL_ALWAYS) - GL_NEVER);
	d.u.alpha_ref = s->alpha_ref;

	mgl_unit_env(s, 0, &d.u.texenv0, &d.u.tex_opaque0, &d.u.rgb_scale0, &d.texture0, &d.sampler0);
	mgl_unit_env(s, 1, &d.u.texenv1, &d.u.tex_opaque1, &d.u.rgb_scale1, &d.texture1, &d.sampler1);

	d.pipeline_key = mgl_pipeline_key(s);

	if (!mgl_raster_state(s, kind, &d))
	{
		return;
	}

	if (!mgl_layer_draw(&d))
	{
		mgl_error(GL_OUT_OF_MEMORY);
	}
}

/* The kind of a primitive mode, or -1 (GL_INVALID_ENUM set) for one GL does not have. */
static int
mgl_check_mode(GLenum mode)
{
	switch (mode)
	{
		case GL_TRIANGLES:
		case GL_TRIANGLE_STRIP:
		case GL_TRIANGLE_FAN:
			return MGL_KIND_TRIANGLES;
		case GL_LINES:
		case GL_LINE_LOOP:
		case GL_LINE_STRIP:
			return MGL_KIND_LINES;
		case GL_POINTS:
			return MGL_KIND_POINTS;
		default:
			/* quads and polygons are GL too, but gl1 never draws them and the header has no name for them */
			mgl_error(GL_INVALID_ENUM);
			return -1;
	}
}

/*
 * Assembles the vertices of a draw (first .. first + count - 1, or 0 ..
 * the largest element), makes its indices and submits it. The bounds
 * were checked by the caller; a list longer than the shim's bound sets
 * GL_INVALID_VALUE and draws nothing.
 */
static void
mgl_draw(int kind, GLenum mode, GLint first, GLsizei count, const uint16_t *elements, GLsizei nvertices)
{
	long n;
	size_t i, k;

	if (kind == MGL_KIND_POINTS)
	{
		/* each point becomes four vertices: at most MGL_MAX_DRAW_POINTS of them */
		if ((size_t)count > MGL_MAX_DRAW_POINTS)
		{
			mgl_error(GL_INVALID_VALUE);
			return;
		}

		if (elements)
		{
			mgl_assemble(&mgl_state, 0, nvertices, draw_vertices);

			for (i = 0; i < (size_t)count; i++)
			{
				draw_points[i] = draw_vertices[elements[i]];
			}
		}
		else
		{
			mgl_assemble(&mgl_state, first, count, draw_points);
		}

		k = mgl_point_quads(&mgl_state, draw_points, (size_t)count, draw_vertices, draw_indices);
		mgl_submit(kind, 4 * k, 6 * k);
		return;
	}

	n = kind == MGL_KIND_LINES ?
		mgl_line_indices(mode, count, elements, draw_indices, MGL_MAX_DRAW_INDICES) :
		mgl_triangle_indices(mode, count, elements, draw_indices, MGL_MAX_DRAW_INDICES);

	if (n < 0)
	{
		mgl_error(GL_INVALID_VALUE);
		return;
	}

	mgl_assemble(&mgl_state, first, nvertices, draw_vertices);
	mgl_submit(kind, (size_t)nvertices, (size_t)n);
}

void
glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
	int kind = mgl_check_mode(mode);

	if (kind < 0)
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

	mgl_draw(kind, mode, first, count, NULL, count);
}

void
glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices)
{
	static int once_type;
	int kind = mgl_check_mode(mode);
	GLsizei i;
	uint16_t max = 0;

	if (kind < 0)
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

	/* every vertex up to the largest index: gl1's buffer starts at vertex 0 */
	mgl_draw(kind, mode, 0, count, draw_elements, (GLsizei)max + 1);
}
