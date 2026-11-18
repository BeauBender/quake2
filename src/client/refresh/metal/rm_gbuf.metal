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
 * The shaders of ref_metal's G-buffer (RT2, TASK54;
 * docs/plans/metal-raytracing-design.md, section 3), beside
 * rm_shaders.metal's, whose stock functions they repeat and extend:
 *
 * - the G-buffer raster: the world's and an alias model's vertices as
 *   rm_vertex_world and rm_vertex_alias place them, with this frame's
 *   unjittered and the previous frame's clip position and the normal;
 *   one fragment function writes the albedo (the texel), the normal
 *   (rgb10a2: alpha 1 with a normal, 0 without; at r_rt_water_hits 1 the
 *   water's marks, rm_rtwater.metal's last draw of the raster, blend it to
 *   2/3 and 1/3, TASK162), the material id (r8uint), the motion vector (rg16float)
 *   and the light the stock product multiplies by (rgba8: the lightmap
 *   sample, the vertex colour, the warp's colour, or white), and while
 *   the shadows trace (rm_gb_surf, TASK131) the surface (r32uint: a
 *   static world draw's vertex and entity, mtl_rts_gb_value);
 * - the forward pass with a motion vector: the stock fragment's colour
 *   and the motion vector as a second attachment;
 * - the lighting resolve: the stock product from the attachments
 *   (mtl_gb_resolve, common/mtl_gbuf_math.h, held by the host test);
 * - the copy of a render-size view into the frame (a bilinear stretch).
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"

/* 0 the texel alone, 1 the texel and the lightmap, 2 the vertex colour times the texel, 3 the vertex colour alone */
constant int rm_gb_kind [[function_constant(2)]];
constant bool rm_gb_normals [[function_constant(3)]];   /* the world's per-vertex normals at buffer 2 */
constant bool rm_gb_atest [[function_constant(4)]];     /* gl1's alpha test, GL_GREATER 0.666 */
constant bool rm_gb_surf [[function_constant(5)]];      /* TASK131: the surface attachment at colour 5 (the raster while the shadows trace) */
constant bool rm_gb_lm = (rm_gb_kind == 1);
constant bool rm_gb_tex = (rm_gb_kind != 3);

/* rm_wvert_t, rm_avert_t, rm_gb_avx_t, rmb_uniforms3d_t, rmb_uniforms_lm_t, rm_gb_uniforms_t, rm_gb_resolve_t */
struct rm_wvert
{
	float4 pos;
	float2 uv;
	float2 lmuv;
	float4 lmrect;
};

struct rm_avert
{
	float4 pos;
	float2 uv;
	float2 pad;
	float4 color;
};

struct rm_gb_avx
{
	float4 prev;
	float4 normal;
};

struct rm_uniforms3d
{
	float4x4 mvp;
	float4 color;
};

struct rm_uniforms_lm
{
	int env;
	float scale;
	uint slice;
	uint filter;
	float flat;   /* TASK173: r_rt_flatlight, the raster's light in place of the lightmap sample; 0 the sample */
};

struct rm_uniforms_mv
{
	float4x4 cur;
	float4x4 prev;
	float4x4 nrm;
	float2 half_vp;
	uint material;
	uint ent;           /* the surface attachment's entity (rm_gb_uniforms_t); read by rmb_gbuf_bind_world, not here */
};

struct rm_uniforms_resolve
{
	int env;
	float scale;
	uint pad0;
	uint pad1;
};

/* rm_shaders.metal's rm_varying3d, member for member, then the G-buffer's */
struct rm_varying_gb
{
	float4 position [[position]];
	float2 uv;
	float2 lmuv;
	float4 color;
	float4 pt;
	float4 lmrect [[flat]];
	float4 cur;         /* this frame's clip position, unjittered */
	float4 prev;        /* the previous frame's */
	float3 normal;      /* world space */
	uint surf [[flat, function_constant(rm_gb_surf)]];   /* the surface attachment's value (mtl_rts_gb_value) */
};

static mtl_rgba_t
rm_gb_rgba(float4 v)
{
	mtl_rgba_t c;

	c.r = v.r;
	c.g = v.g;
	c.b = v.b;
	c.a = v.a;
	return c;
}

static float4
rm_gb_f4(mtl_rgba_t c)
{
	return float4(c.r, c.g, c.b, c.a);
}

/* rm_shaders.metal's rm_lm_sample_cubic: the lightmap texel the stock fragment reads */
static float4
rm_gb_lm_sample(texture2d_array<float> lm, sampler s, float2 uv, uint slice, float4 rect, uint filter)
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

static float2
rm_gb_motion(rm_varying_gb in, constant rm_uniforms_mv &m)
{
	mtl_mv_t mv = mtl_gb_motion(in.cur.x, in.cur.y, in.cur.w, in.prev.x, in.prev.y, in.prev.w, m.half_vp.x, m.half_vp.y);

	return float2(mv.x, mv.y);
}

/* the world's vertices: rm_vertex_world's position (jittered with the projection), and the unjittered ones */
vertex rm_varying_gb
rm_vertex_world_gb(uint vid [[vertex_id]],
		const device rm_wvert *v [[buffer(0)]],
		constant rm_uniforms3d &u [[buffer(1)]],
		const device float4 *n [[buffer(2), function_constant(rm_gb_normals)]],
		constant rm_uniforms_mv &m [[buffer(3)]],
		constant uint &tag [[buffer(4), function_constant(rm_gb_surf)]])
{
	rm_varying_gb out;
	float4 p = u.mvp * v[vid].pos;

	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	out.uv = v[vid].uv;
	out.lmuv = v[vid].lmuv;
	out.lmrect = v[vid].lmrect;
	out.color = u.color;
	out.pt = float4(0.0f);
	out.cur = m.cur * v[vid].pos;
	out.prev = m.prev * v[vid].pos;
	out.normal = float3(0.0f);

	if (rm_gb_normals)
	{
		out.normal = (m.nrm * float4(n[vid].xyz, 0.0f)).xyz;
	}

	/* the vertex of the world's buffer and the entity (mtl_rts_gb_value); tag 0 for a dynamic draw */
	if (rm_gb_surf)
	{
		out.surf = (tag != 0u) ? tag + vid : 0u;
	}

	return out;
}

/* an alias model's: rm_vertex_alias's position, the previous frame's from x */
vertex rm_varying_gb
rm_vertex_alias_gb(uint vid [[vertex_id]],
		const device rm_avert *v [[buffer(0)]],
		constant rm_uniforms3d &u [[buffer(1)]],
		const device rm_gb_avx *x [[buffer(2)]],
		constant rm_uniforms_mv &m [[buffer(3)]])
{
	rm_varying_gb out;
	float4 p = u.mvp * v[vid].pos;

	p.z = mtl_gl_clip_z(p.z, p.w);
	out.position = p;
	out.uv = v[vid].uv;
	out.lmuv = float2(0.0f);
	out.lmrect = float4(0.0f);
	out.color = v[vid].color;
	out.pt = float4(0.0f);
	out.cur = m.cur * v[vid].pos;
	out.prev = m.prev * x[vid].prev;
	out.normal = (m.nrm * float4(x[vid].normal.xyz, 0.0f)).xyz;

	/* an alias model names no surface */
	if (rm_gb_surf)
	{
		out.surf = 0u;
	}

	return out;
}

struct rm_gb_out
{
	float4 albedo [[color(0)]];
	float4 normal [[color(1)]];
	uint material [[color(2)]];
	float2 motion [[color(3)]];
	float4 light [[color(4)]];
	uint surface [[color(5), function_constant(rm_gb_surf)]];
};

fragment rm_gb_out
rm_fragment_gbuf(rm_varying_gb in [[stage_in]],
		constant rm_uniforms_lm &l [[buffer(1), function_constant(rm_gb_lm)]],
		constant rm_uniforms_mv &m [[buffer(3)]],
		texture2d<float> tex [[texture(0)]],
		sampler smp [[sampler(0)]],
		texture2d_array<float> lm [[texture(1), function_constant(rm_gb_lm)]],
		sampler lsmp [[sampler(1), function_constant(rm_gb_lm)]])
{
	rm_gb_out out;
	float4 t = tex.sample(smp, in.uv);
	float4 light = float4(1.0f);
	float len = length(in.normal);
	float3 nn = (len > 0.0f) ? in.normal / len : float3(0.0f);

	if (rm_gb_lm)
	{
		light = (l.flat > 0.0f) ? float4(l.flat, l.flat, l.flat, 1.0f) :
			rm_gb_lm_sample(lm, lsmp, in.lmuv, l.slice, in.lmrect, l.filter);
	}
	else if (rm_gb_kind == 2)
	{
		light = saturate(in.color);
	}

	out.albedo = t;
	out.light = light;
	out.normal = rm_gb_f4(mtl_gb_normal(nn.x, nn.y, nn.z));
	out.material = m.material;
	out.motion = rm_gb_motion(in, m);

	if (rm_gb_surf)
	{
		out.surface = in.surf;
	}

	return out;
}

struct rm_mv_out
{
	float4 color [[color(0)]];
	float2 motion [[color(1)]];
};

/* the forward pass with a motion vector: the stock fragment functions' colour (rm_shaders.metal) and the motion */
fragment rm_mv_out
rm_fragment_mv(rm_varying_gb in [[stage_in]],
		constant rm_uniforms_lm &l [[buffer(1), function_constant(rm_gb_lm)]],
		constant rm_uniforms_mv &m [[buffer(3)]],
		texture2d<float> tex [[texture(0), function_constant(rm_gb_tex)]],
		sampler smp [[sampler(0), function_constant(rm_gb_tex)]],
		texture2d_array<float> lm [[texture(1), function_constant(rm_gb_lm)]],
		sampler lsmp [[sampler(1), function_constant(rm_gb_lm)]])
{
	rm_mv_out out;
	float4 c = saturate(in.color);

	if (rm_gb_kind == 0)
	{
		c = tex.sample(smp, in.uv);
	}
	else if (rm_gb_lm)
	{
		c = rm_gb_f4(mtl_world_lm(rm_gb_rgba(tex.sample(smp, in.uv)),
				rm_gb_rgba(rm_gb_lm_sample(lm, lsmp, in.lmuv, l.slice, in.lmrect, l.filter)), l.env, l.scale));
	}
	else if (rm_gb_kind == 2)
	{
		c = rm_gb_f4(mtl_world_color(rm_gb_rgba(saturate(in.color)), rm_gb_rgba(tex.sample(smp, in.uv))));
	}

	if (rm_gb_atest && !(c.a > 0.666f))
	{
		discard_fragment();
	}

	out.color = c;
	out.motion = rm_gb_motion(in, m);
	return out;
}

/* ---- the lighting resolve and the copy ---------------------------------------------- */

struct rm_fs_varying
{
	float4 position [[position]];
	float2 uv;
};

/* one triangle over the whole target */
vertex rm_fs_varying
rm_vertex_fullscreen(uint vid [[vertex_id]])
{
	rm_fs_varying out;
	float2 p = float2(float((vid << 1) & 2), float(vid & 2));

	out.position = float4(p * 2.0f - 1.0f, 0.0f, 1.0f);
	out.uv = float2(p.x, 1.0f - p.y);
	return out;
}

/*
 * The resolve with every ray-traced effect at 0: each pixel the raster
 * wrote is the stock product of its albedo and light (design section 3,
 * step 5); a pixel no draw wrote (material 0) keeps the target's colour.
 */
fragment float4
rm_fragment_resolve(rm_fs_varying in [[stage_in]],
		constant rm_uniforms_resolve &r [[buffer(0)]],
		texture2d<float> albedo [[texture(0)]],
		texture2d<float> light [[texture(1)]],
		texture2d<uint> mat [[texture(2)]])
{
	uint2 px = uint2(in.position.xy);
	uint m = mat.read(px).r;

	if (m == MTL_GB_MAT_NONE)
	{
		discard_fragment();
	}

	return rm_gb_f4(mtl_gb_resolve(rm_gb_rgba(albedo.read(px)), rm_gb_rgba(light.read(px)), m, r.env, r.scale));
}

/* the copy of a render-size view: dst and src as (x0, y0, x1, y1), dst in clip coordinates, src in texture coordinates */
struct rm_uniforms_blit
{
	float4 dst;
	float4 src;
};

vertex rm_fs_varying
rm_vertex_blit(uint vid [[vertex_id]], constant rm_uniforms_blit &b [[buffer(0)]])
{
	rm_fs_varying out;
	float2 c = float2(float(vid & 1), float(vid >> 1));

	out.position = float4(mix(b.dst.x, b.dst.z, c.x), mix(b.dst.y, b.dst.w, c.y), 0.0f, 1.0f);
	out.uv = float2(mix(b.src.x, b.src.z, c.x), mix(b.src.y, b.src.w, c.y));
	return out;
}

fragment float4
rm_fragment_blit(rm_fs_varying in [[stage_in]], texture2d<float> src [[texture(0)]])
{
	constexpr sampler s(filter::linear, address::clamp_to_edge);

	return src.sample(s, in.uv);
}

/*
 * r_rt_gbuffer_show (a development view, TASK54): an attachment drawn over
 * the view instead of the image. Motion (what 4): red 0.5 + x / scale,
 * green 0.5 + y / scale (pixels, clamped), blue 1 where a draw wrote depth;
 * depth (6): its value in all three channels.
 */
struct rm_uniforms_show
{
	int what;
	float scale;
	uint pad0;
	uint pad1;
};

fragment float4
rm_fragment_show_motion(rm_fs_varying in [[stage_in]],
		constant rm_uniforms_show &s [[buffer(0)]],
		texture2d<float> motion [[texture(0)]],
		depth2d<float> depth [[texture(1)]])
{
	uint2 px = uint2(in.position.xy);
	float d = depth.read(px);
	float2 m = motion.read(px).xy;

	if (s.what == 6)
	{
		return float4(d, d, d, 1.0f);
	}

	return float4(saturate(0.5f + m.x / s.scale), saturate(0.5f + m.y / s.scale), (d < 1.0f) ? 1.0f : 0.0f, 1.0f);
}

/* the G-buffer's own: albedo (1), normal (2), material id (3, times 40), light (5) */
fragment float4
rm_fragment_show_gbuf(rm_fs_varying in [[stage_in]],
		constant rm_uniforms_show &s [[buffer(0)]],
		texture2d<float> albedo [[texture(0)]],
		texture2d<float> normal [[texture(1)]],
		texture2d<uint> mat [[texture(2)]],
		texture2d<float> light [[texture(3)]])
{
	uint2 px = uint2(in.position.xy);

	switch (s.what)
	{
		case 1:
			return float4(albedo.read(px).rgb, 1.0f);
		case 2:
			return float4(normal.read(px).rgb, 1.0f);
		case 3:
			return float4(float3(float(mat.read(px).r) * 40.0f / 255.0f), 1.0f);
		default:
			return float4(light.read(px).rgb, 1.0f);
	}
}
