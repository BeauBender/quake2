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
 * The decisions of ref_metal's teardown waits (rmb_device.m, FIX84) in
 * plain C with neither SDL nor Metal, host-tested by
 * tests/metal/rm_host_test.c: how many frame slots a drain waits for
 * (rmb_drain), and what a drain of the presents waits for and marks
 * (rmb_present_drain, rmb_ahead_drop, rmb_present_commit).
 *
 * This first version keeps the behaviour before FIX84, so that the host
 * test fails against it.
 *
 * =======================================================================
 */

#ifndef RM_RMB_DRAIN_H
#define RM_RMB_DRAIN_H

#include "rmb.h"

/* ---- the frame slots (rmb_drain) --------------------------------------------- */

/* The RMB_FRAMES_IN_FLIGHT frame slots of rmb_frame_sem: lost, 0 to RMB_FRAMES_IN_FLIGHT, were given up on. */
typedef struct
{
	int lost;
} rmb_frames_t;

/* Every slot is waited for again: the semaphore is new (rmb_open) or frames begin on a new layer (rmb_attach). */
static inline void
rmb_frames_reset(rmb_frames_t *f)
{
	f->lost = 0;
}

/* The slots a drain waits for, until the deadline; the others are taken only if free at once. */
static inline int
rmb_frames_wait(const rmb_frames_t *f)
{
	(void)f;
	return RMB_FRAMES_IN_FLIGHT;
}

/* After a drain took got slots back; 1 when it gave up on a slot it waited for (logged). */
static inline int
rmb_frames_drained(rmb_frames_t *f, int got)
{
	(void)f;
	return got < RMB_FRAMES_IN_FLIGHT;
}

/* ---- the presents (rmb_present_drain, rmb_ahead_drop, rmb_present_commit) ---- */

#define RMB_DRAIN_SPENT -1   /* the group was given up on already: nothing is waited for */
#define RMB_DRAIN_LATE 0     /* the wait ended at the bound: the group is marked spent */
#define RMB_DRAIN_DONE 1     /* every present of the group is done, or there is no group */

/* 1: the drain waits for live, at most until the deadline; 0: it waits for nothing. */
static inline int
rmb_present_drain_waits(const void *live, const void *spent)
{
	(void)spent;
	return live != NULL;
}

/* The drain's result; done, read only when it waits, is 1 when the wait ended with every present done. */
static inline int
rmb_present_drain_result(const void *live, const void *spent, int done)
{
	(void)spent;

	if (!live)
	{
		return RMB_DRAIN_DONE;
	}

	return done ? RMB_DRAIN_DONE : RMB_DRAIN_LATE;
}

/* 1: a present slot still held after a drain with result is waited for until the deadline; 0: only if free at once. */
static inline int
rmb_present_slot_waits(int result)
{
	return result == RMB_DRAIN_LATE;
}

/* 1: the drain marks live spent, late being 1 when a slot it waited for was still held at the bound. */
static inline int
rmb_present_drain_marks(int result, int late)
{
	(void)result;
	(void)late;
	return 0;
}

/* 1: a present starts a new group (rmb_present_commit). */
static inline int
rmb_present_new_group(const void *live, const void *spent)
{
	(void)live;
	(void)spent;
	return 0;
}

#endif
