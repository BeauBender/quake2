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
 * ref_gl1.metallib next to ref_gl1.dylib. The draw path (TASK48 on)
 * adds one vertex function and the fragment variants of design
 * section 2. Until then the library holds a placeholder pair; the shim
 * checks for mgl_vertex_stub when it loads the library, so a missing or
 * foreign metallib fails the context instead of the first draw.
 *
 * =======================================================================
 */

#include <metal_stdlib>

using namespace metal;

struct mgl_stub_out
{
	float4 position [[position]];
};

/* A full-screen triangle from the vertex id; no vertex buffers. */
vertex mgl_stub_out
mgl_vertex_stub(uint vid [[vertex_id]])
{
	mgl_stub_out out;
	float2 p = float2((vid << 1) & 2, vid & 2);

	out.position = float4(p * 2.0 - 1.0, 0.0, 1.0);
	return out;
}

fragment float4
mgl_fragment_stub(mgl_stub_out in [[stage_in]])
{
	return float4(0.0, 0.0, 0.0, 1.0);
}
