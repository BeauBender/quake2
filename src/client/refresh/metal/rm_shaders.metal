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
 * The shaders of ref_metal (docs/plans/ref-metal-native.md, section 4).
 * N2 has the 2d class: the vertex function is the shim's (the position
 * times the CPU product mvp, GL clip depth mapped by mtl_gl_clip_z), the
 * fragment function the shim's texture environment of one unit as gl1
 * sets it for 2D (replace, modulate, or the vertex colour alone), with
 * gl1's alpha test (GL_GREATER 0.666) chosen by a function constant.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

#include "common/mtl_shader_math.h"

constant int rm_texenv [[function_constant(0)]];       /* rm_texenv_t */
constant bool rm_alpha_test [[function_constant(1)]];

/* rmb_vert2d_t */
struct rm_vertex2d
{
	float4 pos;
	float2 uv;
	float4 color;
};

struct rm_uniforms2d
{
	float4x4 mvp;
};

struct rm_varying2d
{
	float4 position [[position]];
	float2 uv;
	float4 color;
};

vertex rm_varying2d
rm_vertex_2d(uint vid [[vertex_id]],
		const device rm_vertex2d *v [[buffer(0)]],
		constant rm_uniforms2d &u [[buffer(1)]])
{
	rm_varying2d out;
	float4 p = u.mvp * v[vid].pos;

	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	out.uv = v[vid].uv;
	out.color = v[vid].color;
	return out;
}

fragment float4
rm_fragment_2d(rm_varying2d in [[stage_in]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]])
{
	float4 c = saturate(in.color);

	if (rm_texenv == 0)
	{
		/* GL_REPLACE; every image is rgba8 with alpha 255 where gl1 asked for RGB */
		c = tex.sample(smp, in.uv);
	}
	else if (rm_texenv == 1)
	{
		c = c * tex.sample(smp, in.uv);   /* GL_MODULATE */
	}

	if (rm_alpha_test && !(c.a > 0.666f))
	{
		discard_fragment();
	}

	return c;
}
