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
 * rm_rts_temporal, RT7's accumulation of the shadows (TASK59 slice 1b),
 * in a translation unit of its own, linked into ref_metal.metallib beside
 * rm_rtshadow.metal: that file's helpers and rm_rts_temporal, without
 * rm_rts_trace and the resolve (RM_RTS_TEMPORAL_TU). rm_rtshadow.metal's
 * own unit then holds no second caller of the helpers rm_rts_trace calls,
 * so the compiler inlines into rm_rts_trace what it did before slice 1b,
 * and the stock frames stay byte-identical.
 *
 * =======================================================================
 */

/* the resolve's helpers (rm_rts_rgba) go unused here */
#pragma clang diagnostic ignored "-Wunused-function"

#define RM_RTS_TEMPORAL_TU 1
#include "rm_rtshadow.metal"
