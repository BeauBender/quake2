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
 * The metalfx backend of upscale_textures (TASK124, decision 0012):
 * MetalFX's spatial scaler on a Metal device of its own, in
 * src/client/cl_upscale_metalfx.m on macOS. Elsewhere
 * src/client/cl_upscale_cmd.c has the same functions, which say the
 * backend is not built. Plain C, no client header, so the host test
 * (tests/metal/upscale_metalfx_test.m) builds the file alone.
 *
 * =======================================================================
 */

#ifndef CL_HEADER_UPSCALE_METALFX_H
#define CL_HEADER_UPSCALE_METALFX_H

#include <stddef.h>

/*
 * 1 when the backend may be offered: it is built here, MetalFX's
 * spatial scaler class is present (the framework is weak-linked), and no
 * run of this process has found it unsupported. Else 0 and a one-line
 * reason in reason. Makes no Metal or MetalFX object: the menu asks it.
 */
int CL_UpscaleMetalFXAvailable(char *reason, size_t size);

/*
 * Starts a run: makes the Metal device and its command queue. 1 when a
 * device is there and MTLFXSpatialScalerDescriptor supportsDevice: says
 * yes; else 0, a one-line reason in reason, nothing kept, and later
 * calls of CL_UpscaleMetalFXAvailable give that reason.
 */
int CL_UpscaleMetalFXBegin(char *reason, size_t size);

/*
 * Scales the RGBA8 in (iw by ih, 4 * iw bytes a row) into out (ow by oh)
 * with MTLFXSpatialScaler, the scaler kept for the next tile of the
 * same sizes. 1 on success; 0 outside a run or when Metal fails. ctx is
 * not used (upscale_scaler_t). MetalFX gives alpha 255 everywhere.
 */
int CL_UpscaleMetalFXScale(void *ctx, const unsigned char *in, int iw,
		int ih, unsigned char *out, int ow, int oh);

/* Ends the run: every object CL_UpscaleMetalFXBegin and Scale made is let go. */
void CL_UpscaleMetalFXEnd(void);

#endif /* CL_HEADER_UPSCALE_METALFX_H */
