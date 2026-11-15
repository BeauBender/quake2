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
 * The ray-tracing kernels of ref_metal (RT1, TASK53), linked into
 * ref_metal.metallib beside rm_shaders.metal. rm_rt_alias_lerp is gl1's
 * R_LerpVerts without the shell offset (shells are not in the structures,
 * rm_rt_instances): an alias instance's vertices between its oldframe and
 * frame, with the factors rm_alias_lerp_setup gives on the CPU, written
 * as float4 for the instance's refit acceleration structure (its stride
 * RM_RT_AVERT_BYTES). Nothing draws these vertices.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

/* rmb_rt.m's rmb_rt_lerp_u, 64 bytes */
struct rm_rt_lerp_u
{
	float4 move;
	float4 frontv;
	float4 backv;
	uint frame;     /* the frame's first vertex, rm_rt_frame_vertex */
	uint oldframe;  /* the oldframe's */
	uint n;         /* num_xyz */
	uint pad;
};

kernel void
rm_rt_alias_lerp(device const uchar4 *frames [[buffer(0)]], constant rm_rt_lerp_u &u [[buffer(1)]],
		device float4 *out [[buffer(2)]], uint i [[thread_position_in_grid]])
{
	if (i >= u.n)
	{
		return;
	}

	const float3 v = float3(frames[u.frame + i].xyz);
	const float3 ov = float3(frames[u.oldframe + i].xyz);

	/* R_LerpVerts: move + ov * backv + v * frontv */
	out[i] = float4(u.move.xyz + ov * u.backv.xyz + v * u.frontv.xyz, 1.0f);
}
