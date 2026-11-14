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
 * is a uniform, not a function constant (design section 7); blending
 * and the colour mask are pipeline state.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

/* mgl_vertex_t in mgl_internal.h: 48 bytes. */
struct mgl_vertex
{
	float4 pos;
	float2 tc0;
	float2 tc1;
	float4 color;
};

/* mgl_uniforms_t in mgl_internal.h: 80 bytes. */
struct mgl_uniforms
{
	float4x4 mvp;
	float alpha_ref;
	int alpha_func;     /* GL_NEVER .. GL_ALWAYS minus GL_NEVER */
	int texenv0;        /* 0 none, 1 GL_REPLACE, 2 GL_MODULATE */
	int tex_opaque0;    /* an RGB texture: GL_REPLACE keeps the fragment's alpha */
};

struct mgl_varying
{
	float4 position [[position]];
	float2 tc0;
	float4 color;
};

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
	out.color = v[vid].color;
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
		sampler smp0 [[sampler(0)]])
{
	float4 c = saturate(in.color);

	if (u.texenv0 != 0)
	{
		float4 t = tex0.sample(smp0, in.tc0);

		if (u.texenv0 == 1)
		{
			c = float4(t.rgb, u.tex_opaque0 ? c.a : t.a);
		}
		else
		{
			c *= t;
		}
	}

	if (!mgl_alpha_pass(u.alpha_func, c.a, u.alpha_ref))
	{
		discard_fragment();
	}

	return c;
}
