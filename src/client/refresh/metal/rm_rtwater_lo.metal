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
 * The water reflections traced at r_rt_water_scale below 1 (TASK126
 * slice 5, option E), in a translation unit of its own, linked into
 * ref_metal.metallib beside rm_rtwater.metal: that file's helpers, its
 * compaction and its two traces compiled again at the traced size
 * (rm_rtw_compact_lo, rm_rtw_trace_lo, rm_rtw_trace_ws_lo: RM_RTW_AT,
 * RM_RTW_PIC and RM_RTW_OUT there), and the kernels it adds,
 * rm_rtw_denoise_lo and rm_rtw_up, without the stock kernels and
 * shaders. The stock unit then compiles what it did before, so the stock
 * frames stay byte-identical (slices 3 and 4's arrangement,
 * rm_rtsun_lo.metal and rm_rtshadow_lo.metal).
 *
 * =======================================================================
 */

/* the stock shaders' helpers (rm_rtw_rgba and the like) go unused here */
#pragma clang diagnostic ignored "-Wunused-function"

#define RM_RTW_LO_TU 1
#include "rm_rtwater.metal"
