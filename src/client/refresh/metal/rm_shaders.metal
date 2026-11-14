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

/*
 * The 3D classes of N4. The vertex functions are the shim's: the CPU
 * product mvp times the position, GL clip depth mapped by mtl_gl_clip_z.
 * The world's position is read as one float4 with its stored w = 1, as
 * the shim's is: a float3 widened with a w of 1 (a constant or a uniform)
 * gave clip coordinates that differ in the last bits and moved texels on
 * 0.1 to 1 percent of the pixels (design section 19).
 */

/* rm_wvert_t: the position at offset 0 with w = 1, 32 bytes */
struct rm_wvert
{
	float4 pos;
	float2 uv;
	float2 lmuv;
};

/* rmb_uniforms3d_t */
struct rm_uniforms3d
{
	float4x4 mvp;
	float4 color;
};

/* the shim's mgl_varying, member for member, so the interpolation is laid out as the shim's */
struct rm_varying3d
{
	float4 position [[position]];
	float2 uv;
	float2 lmuv;
	float4 color;
	float4 pt;
};

/* world_tex: the diffuse texel alone, gl1's GL_REPLACE with the lightmap unit off (r_fullbright) */
vertex rm_varying3d
rm_vertex_world(uint vid [[vertex_id]],
		const device rm_wvert *v [[buffer(0)]],
		constant rm_uniforms3d &u [[buffer(1)]])
{
	rm_varying3d out;
	float4 p = u.mvp * v[vid].pos;

	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	out.uv = v[vid].uv;
	out.lmuv = v[vid].lmuv;
	out.color = u.color;
	out.pt = float4(0.0f);
	return out;
}

fragment float4
rm_fragment_world(rm_varying3d in [[stage_in]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]])
{
	return tex.sample(smp, in.uv);
}

/* rmb_uniforms_lm_t: gl1's lightmap unit (MTL_LM_*, GL_RGB_SCALE) and the draw's slice of the array */
struct rm_uniforms_lm
{
	int env;
	float scale;
	uint slice;
	uint pad;
};

static mtl_rgba_t
rm_rgba(float4 v)
{
	mtl_rgba_t c;

	c.r = v.r;
	c.g = v.g;
	c.b = v.b;
	c.a = v.a;
	return c;
}

/*
 * world_tex with a colour (N6): GL_MODULATE of the colour the vertex
 * function carries (glColor4f, saturated as the shim's fragment function
 * does) and the texel (mtl_world_color, held equal to the shim's by the
 * host test); the pipeline blends or not.
 */
fragment float4
rm_fragment_world_color(rm_varying3d in [[stage_in]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]])
{
	mtl_rgba_t c = mtl_world_color(rm_rgba(saturate(in.color)), rm_rgba(tex.sample(smp, in.uv)));

	return float4(c.r, c.g, c.b, c.a);
}

/*
 * world_lm (N5): gl1's two texture units on its multitexture path, the
 * diffuse texel replacing the colour and the lightmap slice's texel
 * applied by gl1's lightmap environment (mtl_world_lm, the arithmetic the
 * host test holds equal to the shim's mgl_texenv_stage); the vertex
 * function is world_tex's.
 */
fragment float4
rm_fragment_world_lm(rm_varying3d in [[stage_in]],
		constant rm_uniforms_lm &u [[buffer(1)]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]],
		texture2d_array<float> lm [[texture(1)]],
		sampler lsmp [[sampler(1)]])
{
	mtl_rgba_t c = mtl_world_lm(rm_rgba(tex.sample(smp, in.uv)), rm_rgba(lm.sample(lsmp, in.lmuv, u.slice)),
			u.env, u.scale);

	return float4(c.r, c.g, c.b, c.a);
}

/*
 * alias (N7): the vertices gl1 lerps and lights on the CPU, in the shim's
 * layout (rm_avert_t: the position with w = 1, the texture coordinate and
 * the colour bytes as the shim normalises them), so the vertex function
 * is the shim's on the same values; the fragment functions are
 * rm_fragment_world_color (GL_MODULATE of the vertex colour and the skin)
 * and rm_fragment_color (a shell, untextured).
 */
struct rm_avert
{
	float4 pos;
	float2 uv;
	float2 pad;
	float4 color;
};

vertex rm_varying3d
rm_vertex_alias(uint vid [[vertex_id]],
		const device rm_avert *v [[buffer(0)]],
		constant rm_uniforms3d &u [[buffer(1)]])
{
	rm_varying3d out;
	float4 p = u.mvp * v[vid].pos;

	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	out.uv = v[vid].uv;
	out.lmuv = float2(0.0f);
	out.color = v[vid].color;
	out.pt = float4(0.0f);
	return out;
}

/* color: one untextured colour per draw, carried as a vertex colour as the shim's glColor4f is; positions with w = 1 */
vertex rm_varying3d
rm_vertex_color(uint vid [[vertex_id]],
		const device float4 *v [[buffer(0)]],
		constant rm_uniforms3d &u [[buffer(1)]])
{
	rm_varying3d out;
	float4 p = u.mvp * v[vid];

	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	out.uv = float2(0.0f);
	out.lmuv = float2(0.0f);
	out.color = u.color;
	out.pt = float4(0.0f);
	return out;
}

fragment float4
rm_fragment_color(rm_varying3d in [[stage_in]])
{
	return saturate(in.color);
}
