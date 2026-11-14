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
 * The shim's viewport conversion (mgl_compute_raster, gl1/metal/
 * mgl_raster.c) for the host test of ref_metal, behind a plain C
 * function, so the GL header the shim includes stays out of
 * rm_host_test.c. tests/check-ref-metal.sh links this file and
 * mgl_raster.c into the host test.
 *
 * =======================================================================
 */

#include <string.h>

#include "../../src/client/refresh/gl1/metal/mgl_internal.h"

/* What the shim's draw gets for glViewport(vx, vy, vw, vh) with gl1's 3D depth range 0 to 1 and no scissor. */
void
rm_host_shim_raster(int vx, int vy, int vw, int vh, int tw, int th, double *out)
{
	mgl_state_t s;
	mgl_raster_t r;

	memset(&s, 0, sizeof(s));
	s.viewport[0] = vx;
	s.viewport[1] = vy;
	s.viewport[2] = vw;
	s.viewport[3] = vh;
	s.depth_near = 0;
	s.depth_far = 1;
	mgl_compute_raster(&s, tw, th, &r);
	out[0] = r.x;
	out[1] = r.y;
	out[2] = r.w;
	out[3] = r.h;
	out[4] = r.znear;
	out[5] = r.zfar;
}
