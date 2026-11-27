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
 * The dlight shadows traced at r_rt_shadows_scale below 1 (TASK126 slice
 * 4, option E), in a translation unit of its own, linked into
 * ref_metal.metallib beside rm_rtshadow.metal and
 * rm_rtshadow_temporal.metal: that file's helpers and the trace's copy
 * (rm_rts_shade), with rm_rts_trace_lo, rm_rts_temporal_lo and rm_rts_up,
 * without rm_rts_trace, the resolve (RM_RTS_TEMPORAL_TU) and
 * rm_rts_temporal (RM_RTS_LO_TU). The stock trace's unit and the stock
 * temporal kernel's then hold no new caller of their helpers, so the
 * compiler inlines into them what it did before, and the stock frames
 * stay byte-identical (TASK59 slice 1b's arrangement, as slice 3's
 * rm_rtsun_lo.metal).
 *
 * =======================================================================
 */

/* the resolve's helpers (rm_rts_rgba) and rm_rts_temporal's rm_rts_finish go unused here */
#pragma clang diagnostic ignored "-Wunused-function"

#define RM_RTS_TEMPORAL_TU 1
#define RM_RTS_LO_TU 1
#include "rm_rtshadow.metal"
