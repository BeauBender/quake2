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
#include "common/mtl_retro_math.h"
#include "common/mtl_hdr_math.h"

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

/* the fragment function's: x is 1 for a picture drawn with the sharp-bilinear coordinate (metal_2d_sharp), 0 otherwise; the rest is padding */
struct rm_params2d
{
	float4 sharp;
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
		constant rm_params2d &params [[buffer(0)]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]])
{
	float4 c = saturate(in.color);
	float2 uv = in.uv;

	/* metal_2d_sharp: the position in texels, and the texels a pixel covers, per axis (mtl_sharp_coord) */
	if ((rm_texenv != 2) && (params.sharp.x != 0.0f))
	{
		const float2 size = float2(tex.get_width(), tex.get_height());
		const float2 t = uv * size;
		const float2 d = fwidth(t);

		uv = float2(mtl_sharp_coord(t.x, d.x), mtl_sharp_coord(t.y, d.y)) / size;
	}

	if (rm_texenv == 0)
	{
		/* GL_REPLACE; every image is rgba8 with alpha 255 where gl1 asked for RGB */
		c = tex.sample(smp, uv);
	}
	else if (rm_texenv == 1)
	{
		c = c * tex.sample(smp, uv);   /* GL_MODULATE */
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

/* rm_wvert_t: the position at offset 0 with w = 1, then the coordinates and the face's lightmap rectangle, 48 bytes */
struct rm_wvert
{
	float4 pos;
	float2 uv;
	float2 lmuv;
	float4 lmrect;
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
	float4 lmrect [[flat]];     /* TASK120: the face's lightmap rectangle, 0 where there is none */
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
	out.lmrect = v[vid].lmrect;
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
	uint filter;     /* metal_lightmap_filter: 0 bilinear, 1 bicubic */
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
 * The same with gl1's alpha test, glAlphaFunc(GL_GREATER, 0.666) as the
 * shim tests it after the texture units: a sprite of alpha 1 (N8)
 */
fragment float4
rm_fragment_world_color_at(rm_varying3d in [[stage_in]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]])
{
	mtl_rgba_t c = mtl_world_color(rm_rgba(saturate(in.color)), rm_rgba(tex.sample(smp, in.uv)));

	if (!(c.a > 0.666f))
	{
		discard_fragment();
	}

	return float4(c.r, c.g, c.b, c.a);
}

/*
 * The lightmap texel at uv of the slice, filtered: bicubic for filter 1 on
 * a face with a rectangle, the hardware's bilinear tap otherwise
 * (metal_lightmap_filter 0 is the one call, as before). The bicubic is
 * mtl_lm_cubic's four taps (mtl_shader_math.h, held by the host test),
 * every tap clamped half a luxel inside rect = (left, top, right, bottom)
 * in luxels of the slice, so none reads a neighbouring face's luxels. The
 * taps use level 0 (the array has one level), so no derivative is taken
 * in the branch. RT2 (TASK54) calls this function to read the same filtered
 * sample at a hit's lightmap coordinate and face rectangle.
 */
static float4
rm_lm_sample_cubic(texture2d_array<float> lm, sampler s, float2 uv, uint slice, float4 rect, uint filter)
{
	if (filter == 0 || !mtl_lm_rect_valid(rect.x, rect.y, rect.z, rect.w))
	{
		return lm.sample(s, uv, slice);
	}

	float2 size = float2(float(lm.get_width()), float(lm.get_height()));
	float2 x = uv * size;
	mtl_cubic_taps_t t = mtl_lm_cubic(x.x, x.y, rect.x, rect.y, rect.z, rect.w);

	return t.w00 * lm.sample(s, float2(t.x0, t.y0) / size, slice, level(0.0f)) +
		t.w10 * lm.sample(s, float2(t.x1, t.y0) / size, slice, level(0.0f)) +
		t.w01 * lm.sample(s, float2(t.x0, t.y1) / size, slice, level(0.0f)) +
		t.w11 * lm.sample(s, float2(t.x1, t.y1) / size, slice, level(0.0f));
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
	mtl_rgba_t c = mtl_world_lm(rm_rgba(tex.sample(smp, in.uv)), rm_rgba(rm_lm_sample_cubic(lm, lsmp, in.lmuv, u.slice, in.lmrect, u.filter)),
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
	out.lmrect = float4(0.0f);
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
	out.lmrect = float4(0.0f);
	out.color = u.color;
	out.pt = float4(0.0f);
	return out;
}

fragment float4
rm_fragment_color(rm_varying3d in [[stage_in]])
{
	return saturate(in.color);
}

/*
 * particle (N9): the quads of gl1's GL_POINTS as the shim makes them
 * (rm_point_quads: clip coordinates, drawn with the identity), each
 * corner's offset from the centre in uv and the radius and the smooth
 * flag in pad, carried in pt as the shim's vertex carries them
 */
vertex rm_varying3d
rm_vertex_particle(uint vid [[vertex_id]],
		const device rm_avert *v [[buffer(0)]],
		constant rm_uniforms3d &u [[buffer(1)]])
{
	rm_varying3d out;
	float4 p = u.mvp * v[vid].pos;

	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	out.uv = float2(0.0f);
	out.lmuv = float2(0.0f);
	out.lmrect = float4(0.0f);
	out.color = v[vid].color;
	out.pt = float4(v[vid].uv, v[vid].pad);
	return out;
}

/* the shim's fragment function untextured: a smooth point's coverage multiplies the alpha (GL 1.4, section 3.12) */
fragment float4
rm_fragment_particle(rm_varying3d in [[stage_in]])
{
	float4 c = saturate(in.color);

	if (in.pt.w > 0.5)
	{
		c.a *= mtl_point_coverage(in.pt.x, in.pt.y, in.pt.z);
	}

	return c;
}

/*
 * FIX53: the alpha pass of a layer that is not opaque. One triangle over
 * the whole target, alpha 1 (the pipeline masks the colour channels).
 */
vertex float4
rm_vertex_alpha(uint vid [[vertex_id]])
{
	float2 p = float2(float((vid << 1) & 2), float(vid & 2));

	return float4(p * 2.0f - 1.0f, 0.0f, 1.0f);
}

fragment float4
rm_fragment_alpha()
{
	return float4(0.0f, 0.0f, 0.0f, 1.0f);
}

/*
 * FIX51: the present of a layer that is not opaque, in one pass. The
 * triangle of rm_vertex_alpha over the viewport the frame lands on; each
 * pixel is the frame's, ofs rows away, with alpha 1, so the copy and
 * FIX53's alpha pass are one pass over the drawable.
 */
fragment float4
rm_fragment_present(float4 pos [[position]], texture2d<float, access::read> frame [[texture(0)]],
		constant int2 &ofs [[buffer(0)]])
{
	return float4(frame.read(uint2(int2(pos.xy) + ofs)).rgb, 1.0f);
}

/*
 * TASK196 (HD2): the present under r_hdr 1, always this pass. The frame
 * target (RGBA16Float) holds the stock, sRGB-encoded values; each pixel
 * is decoded to linear light once (common/mtl_hdr_math.h, extended past
 * 1.0) for the RGBA16Float drawable in extended linear sRGB, alpha 1.
 * HD2 shows the frame at SDR white, so each channel is first clamped to
 * [0, 1] (mtl_hdr_present; the float16 target does not clamp an additive
 * blend, and a NaN is 0). HD3 replaces this clamp with the tone map.
 */
fragment float4
rm_fragment_present_hdr(float4 pos [[position]], texture2d<float, access::read> frame [[texture(0)]],
		constant int2 &ofs [[buffer(0)]])
{
	float3 c = frame.read(uint2(int2(pos.xy) + ofs)).rgb;

	return float4(mtl_hdr_present(c.r), mtl_hdr_present(c.g), mtl_hdr_present(c.b), 1.0f);
}

/*
 * TASK196 (HD2): the SDR fallback rmb_read takes of the RGBA16Float frame
 * target, a plain clamp of each channel to [0, 1], which the BGRA8Unorm
 * target it writes rounds to bytes.
 */
fragment float4
rm_fragment_hdr_read(float4 pos [[position]], texture2d<float, access::read> frame [[texture(0)]])
{
	float4 c = frame.read(uint2(pos.xy));

	return float4(mtl_hdr_clamp(c.r), mtl_hdr_clamp(c.g), mtl_hdr_clamp(c.b), mtl_hdr_clamp(c.a));
}

/*
 * TASK184 (PM2): the Retro post-process over the 3D view's rectangle, as a
 * 16-bit frame buffer of a 3dfx Voodoo-class board finished it
 * (common/mtl_retro_math.h): each pixel of the copy of the frame quantized
 * to RGB565 with the 4 x 4 ordered dither, with the filter on the 2 x 2
 * box of the quantized pixels (the right and lower ones clamped to the
 * rectangle), then the gamma curve. p: the rectangle's x, y, width and
 * height in pixels, the filter (0 or 1) and the curve (r_retro_gamma).
 */
static uint3
rm_retro_quant(texture2d<float, access::read> src, uint2 at)
{
	uint3 c = uint3(rint(saturate(src.read(at).rgb) * 255.0f));
	unsigned r = mtl_retro_bayer(at.x, at.y);

	return uint3(mtl_retro_quant(c.r, 5u, r), mtl_retro_quant(c.g, 6u, r), mtl_retro_quant(c.b, 5u, r));
}

fragment float4
rm_fragment_retro(float4 pos [[position]], texture2d<float, access::read> src [[texture(0)]],
		constant int *p [[buffer(0)]])
{
	uint2 at = uint2(pos.xy);
	uint3 q = rm_retro_quant(src, at);
	float3 v;

	if (p[4])
	{
		uint2 hi = uint2(uint(p[0] + p[2] - 1), uint(p[1] + p[3] - 1));
		uint2 nx = min(at + uint2(1u, 0u), hi), ny = min(at + uint2(0u, 1u), hi), nxy = min(at + 1u, hi);
		uint3 a = rm_retro_quant(src, nx), b = rm_retro_quant(src, ny), c = rm_retro_quant(src, nxy);

		q = uint3(mtl_retro_box(q.r, a.r, b.r, c.r), mtl_retro_box(q.g, a.g, b.g, c.g),
				mtl_retro_box(q.b, a.b, b.b, c.b));
	}

	v = float3(q) / 255.0f;

	if (p[5] > 0)
	{
		v = pow(v, float3(mtl_retro_gamma(p[5])));
	}

	return float4(v, src.read(at).a);
}
