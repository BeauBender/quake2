/*
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
 * RT-primary's light on models (TASK191, PM9; docs/runbook/TASK191.md):
 * rm_rtm_probe_trace, one thread a probe, dispatched in the direct
 * light's encoder after rm_rtdir_trace with its bindings. A probe is the
 * point and normal of the face below a model where R_LightPoint reads the
 * lightmap; its light is the one a world pixel there takes, by
 * rm_rtdir.metal's own functions (the map's lights with their shadow
 * rays, the dynamic lights, the sky light, the sun, the bounce, the
 * world's exposure), the sky, sun and bounce averaged over
 * RM_RTM_SAMPLES rays. Its own translation unit: rm_rtdir.metal is
 * included, not edited, its two entry points renamed, and every ray's
 * mask (MTL_RTAO_MASK) is the map's (MTL_RTW_MASK_MAP: the world and the
 * inline brush models), so no alias model shadows a probe; the world's
 * kernel is compiled as before in its own unit.
 *
 * =======================================================================
 */

#include <metal_stdlib>
#include <metal_raytracing>

using namespace metal;
using namespace metal::raytracing;

#include "common/mtl_shader_math.h"
#include "common/mtl_gbuf_math.h"
#include "common/mtl_rtao_math.h"
#include "common/mtl_rtw_math.h"

#undef MTL_RTAO_MASK
#define MTL_RTAO_MASK MTL_RTW_MASK_MAP

#define rm_rtdir_trace rm_rtm_world_trace_unused
#define rm_fragment_rtdir_show rm_rtm_world_show_unused
#include "rm_rtdir.metal"
#undef rm_rtdir_trace
#undef rm_fragment_rtdir_show

#define RM_RTM_SAMPLES 16u   /* header/rm_rtmodel.h's */

/* the back end's probe record: the point (w 0) and the unit normal (w 0) */
struct rm_rtm_probe
{
	float4 spot;
	float4 normal;
};

kernel void
rm_rtm_probe_trace(constant rm_rtdir_view &v [[buffer(0)]],
		constant packed_float3 *styles [[buffer(1)]],
		instance_acceleration_structure ias [[buffer(2)]],
		device const uint *inst [[buffer(3)]],
		device const uchar *tmat [[buffer(4)]],
		constant uint &ntris [[buffer(5)]],
		device const rm_rtdir_light *lights [[buffer(9)]],
		constant rm_rtdir_dlight *dl [[buffer(10)]],
		device const uint *self [[buffer(11)]],
		device const ushort *tile [[buffer(12)]],
		texture2d_array<float> atlas [[texture(7)]],
		device const float4 *verts [[buffer(13)]],
		device const uint *idx [[buffer(14)]],
		constant rm_rtdir_geom &g [[buffer(15)]],
		device const uint *brec [[buffer(16)]],
		device const rm_rtm_probe *probes [[buffer(17)]],
		device float4 *out [[buffer(18)]],
		constant uint &nprobes [[buffer(19)]],
		uint i [[thread_position_in_grid]])
{
	rm_rtdir_tally t = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
	float3 open = float3(0.0f), bounce = float3(0.0f);

	if (i >= nprobes)
	{
		return;
	}

	const float3 p = probes[i].spot.xyz;
	const float3 n = probes[i].normal.xyz;

	/* the direct light, as rm_rtdir_trace's world pixel takes it (the view has no own-light table) */
	const float3 raw = rm_rtdir_direct(v, p, n, styles, ias, inst, tmat, ntris, lights, dl, self, t);

	/* the sky light, the sun and the bounce, each a mean of RM_RTM_SAMPLES rays at the tile's numbers of (k, i) */
	for (uint k = 0u; k < RM_RTM_SAMPLES; k++)
	{
		open += rm_rtdir_open(v, uint2(k, i), p, n, ias, inst, tmat, ntris, tile, t);
		bounce += rm_rtdir_bounce(v, uint2(k, i), p, n, ias, inst, tmat, ntris, tile, atlas, verts, idx, g, brec, t);
	}

	out[i] = rm_rtdir_out_open(rm_rtdir_out(raw, v.exposure), (open + bounce) / float(RM_RTM_SAMPLES), v.show);
}
