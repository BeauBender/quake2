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
 * The shaders of the Metal shim, compiled by the Makefile into
 * ref_gl1.metallib next to ref_gl1.dylib; the shim checks for
 * mgl_vertex_main when it loads the library, so a missing or foreign
 * metallib fails the context instead of the first draw.
 *
 * One vertex function and one fragment function. The fixed-function
 * state that differs between draws (texture environment, alpha test)
 * is a uniform, not a function constant (design section 8); blending
 * and the colour mask are pipeline state. The texture environment of
 * each unit is mgl_texenv_stage, the same source the host test runs.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

#include "mgl_texenv.h"

/* mgl_vertex_t in mgl_internal.h: 64 bytes. */
struct mgl_vertex
{
	float4 pos;
	float2 tc0;
	float2 tc1;
	float4 color;
	float4 pt;          /* a smooth point's offset from its centre in pixels, its radius, 1 */
};

/* mgl_uniforms_t in mgl_internal.h: 96 bytes. */
struct mgl_uniforms
{
	float4x4 mvp;
	float alpha_ref;
	int alpha_func;     /* GL_NEVER .. GL_ALWAYS minus GL_NEVER */
	int texenv0;        /* MGL_TEXENV_* */
	int tex_opaque0;    /* an RGB texture: GL_REPLACE keeps the previous alpha */
	int texenv1;
	int tex_opaque1;
	float rgb_scale0;   /* GL_RGB_SCALE of GL_COMBINE */
	float rgb_scale1;
};

struct mgl_varying
{
	float4 position [[position]];
	float2 tc0;
	float2 tc1;
	float4 color;
	float4 pt;
};

static mgl_rgba_t
mgl_rgba(float4 v)
{
	mgl_rgba_t c;

	c.r = v.r;
	c.g = v.g;
	c.b = v.b;
	c.a = v.a;
	return c;
}

vertex mgl_varying
mgl_vertex_main(uint vid [[vertex_id]],
		const device mgl_vertex *v [[buffer(0)]],
		constant mgl_uniforms &u [[buffer(1)]])
{
	mgl_varying out;
	float4 p = u.mvp * v[vid].pos;

	/* GL clips z to [-w, w], Metal to [0, w]: the same depth after the viewport */
	p.z = (p.z + p.w) * 0.5;

	out.position = p;
	out.tc0 = v[vid].tc0;
	out.tc1 = v[vid].tc1;
	out.color = v[vid].color;
	out.pt = v[vid].pt;
	return out;
}

static bool
mgl_alpha_pass(int func, float a, float ref)
{
	switch (func)
	{
		case 0: return false;      /* GL_NEVER */
		case 1: return a < ref;    /* GL_LESS */
		case 2: return a == ref;   /* GL_EQUAL */
		case 3: return a <= ref;   /* GL_LEQUAL */
		case 4: return a > ref;    /* GL_GREATER */
		case 5: return a != ref;   /* GL_NOTEQUAL */
		case 6: return a >= ref;   /* GL_GEQUAL */
		default: return true;      /* GL_ALWAYS */
	}
}

fragment float4
mgl_fragment_main(mgl_varying in [[stage_in]],
		constant mgl_uniforms &u [[buffer(1)]],
		texture2d<float> tex0 [[texture(0)]],
		sampler smp0 [[sampler(0)]],
		texture2d<float> tex1 [[texture(1)]],
		sampler smp1 [[sampler(1)]])
{
	mgl_rgba_t c = mgl_rgba(saturate(in.color));

	/* GL 1.4, section 3.8.13: unit 0 on the fragment's colour, unit 1 on unit 0's result */
	if (u.texenv0 != MGL_TEXENV_NONE)
	{
		c = mgl_texenv_stage(u.texenv0, u.tex_opaque0, u.rgb_scale0, c,
				mgl_rgba(tex0.sample(smp0, in.tc0)));
	}

	if (u.texenv1 != MGL_TEXENV_NONE)
	{
		c = mgl_texenv_stage(u.texenv1, u.tex_opaque1, u.rgb_scale1, c,
				mgl_rgba(tex1.sample(smp1, in.tc1)));
	}

	/* GL 1.4, section 3.12: a smooth point's coverage multiplies the alpha, before the alpha test */
	if (in.pt.w > 0.5)
	{
		c.a *= mgl_point_coverage(in.pt.x, in.pt.y, in.pt.z);
	}

	if (!mgl_alpha_pass(u.alpha_func, c.a, u.alpha_ref))
	{
		discard_fragment();
	}

	return float4(c.r, c.g, c.b, c.a);
}
