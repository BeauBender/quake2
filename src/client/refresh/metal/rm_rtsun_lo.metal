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
 * The sun traced at r_rt_sun_scale below 1 (TASK126 slice 3, option E),
 * in a translation unit of its own, linked into ref_metal.metallib beside
 * rm_rtsun.metal: that file's helpers and rm_rtsun_trace_lo,
 * rm_rtsun_temporal_lo, rm_rtsun_denoise_lo and rm_rtsun_up, without the
 * stock kernels (RM_RTSUN_LO_TU). rm_rtsun.metal's own unit then holds no
 * new caller of the helpers rm_rtsun_trace calls, so the compiler inlines
 * into it what it did before, and the stock frames stay byte-identical
 * (TASK59 slice 1b's rm_rtshadow_temporal.metal, the same arrangement).
 *
 * =======================================================================
 */

#define RM_RTSUN_LO_TU 1
#include "rm_rtsun.metal"
