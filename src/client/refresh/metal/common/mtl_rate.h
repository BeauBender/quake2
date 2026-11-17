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
 * The presented rate over a window (TASK161, docs/runbook/TASK161.md),
 * shared by ref_metal and the shim: header-only, no Metal, no SDL.
 *
 * The first frame ended is the origin. The first frame to end at least
 * start seconds after it is the window's first sample; every frame ended
 * after that moves the last sample. The rate is the frames presented
 * between the two samples over the time between them.
 *
 * =======================================================================
 */

#ifndef MTL_RATE_H
#define MTL_RATE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct
{
	int seen;                   /* a first frame ended */
	int started;                /* the window has its first sample */
	uint64_t first_ns;          /* the first frame's end */
	uint64_t start_ns;          /* the window's first sample */
	uint64_t last_ns;           /* the last sample */
	unsigned long start_n;      /* frames presented at the first sample */
	unsigned long last_n;       /* frames presented at the last sample */
} mtl_rate_t;

static inline void
mtl_rate_reset(mtl_rate_t *r)
{
	r->seen = r->started = 0;
	r->first_ns = r->start_ns = r->last_ns = 0;
	r->start_n = r->last_n = 0;
}

/* A frame ended at now_ns; presented is the run's total presented so far, this frame included. */
static inline void
mtl_rate_frame(mtl_rate_t *r, uint64_t now_ns, unsigned long presented, double start_s)
{
	if (!r->seen)
	{
		r->seen = 1;
		r->first_ns = now_ns;
	}

	if (!r->started)
	{
		if ((double)(now_ns - r->first_ns) < start_s * 1e9)
		{
			return;
		}

		r->started = 1;
		r->start_ns = now_ns;
		r->start_n = presented;
	}

	r->last_ns = now_ns;
	r->last_n = presented;
}

/* The window's length in seconds; 0 before it has two samples apart. */
static inline double
mtl_rate_window_s(const mtl_rate_t *r)
{
	return (r->started && r->last_ns > r->start_ns) ? (double)(r->last_ns - r->start_ns) / 1e9 : 0.0;
}

static inline double
mtl_rate_per_second(const mtl_rate_t *r)
{
	double s = mtl_rate_window_s(r);

	return (s > 0.0) ? (double)(r->last_n - r->start_n) / s : 0.0;
}

/*
 * The line, without a newline: "<prefix> <rate> presented a second over
 * <s> s from <start> s, <n> presented, <h> with every present slot held,
 * <d> without a drawable". Returns 1 when it fits, 0 when truncated.
 */
static inline int
mtl_rate_format(const char *prefix, const mtl_rate_t *r, double start_s, unsigned long held,
		unsigned long no_drawable, char *buf, size_t len)
{
	int n;

	if (len == 0)
	{
		return 0;
	}

	n = snprintf(buf, len, "%s %.1f presented a second over %.2f s from %.2f s, %lu presented, "
			"%lu with every present slot held, %lu without a drawable",
			prefix, mtl_rate_per_second(r), mtl_rate_window_s(r), start_s,
			(mtl_rate_window_s(r) > 0.0) ? r->last_n - r->start_n : 0ul, held, no_drawable);
	return (n >= 0) && ((size_t)n < len);
}

#endif
