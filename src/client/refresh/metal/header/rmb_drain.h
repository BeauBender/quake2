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
 * (rmb_present_drain, rmb_ahead_drop, rmb_present_commit). Both keep
 * teardown to one deadline per stage: what a drain gave up on is not
 * waited for again.
 *
 * =======================================================================
 */

#ifndef RM_RMB_DRAIN_H
#define RM_RMB_DRAIN_H

#include "rmb.h"

/* ---- the frame slots (rmb_drain) --------------------------------------------- */

/*
 * The RMB_FRAMES_IN_FLIGHT frame slots of rmb_frame_sem: lost, 0 to
 * RMB_FRAMES_IN_FLIGHT, of them were given up on by a drain since the
 * slots were last reset. A frame begins only with a layer
 * (rmb_frame_begin), so until the next rmb_attach those are the slots the
 * hung frames hold.
 */
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

/*
 * The slots a drain waits for, until the one deadline they share: those
 * not given up on already. The others are taken back only if free at
 * once, so the second rmb_detach of teardown does not wait for the frames
 * the first gave up on, as the shim's mgl_release_all waits for no frame.
 */
static inline int
rmb_frames_wait(const rmb_frames_t *f)
{
	if (f->lost <= 0)
	{
		return RMB_FRAMES_IN_FLIGHT;
	}

	if (f->lost >= RMB_FRAMES_IN_FLIGHT)
	{
		return 0;
	}

	return RMB_FRAMES_IN_FLIGHT - f->lost;
}

/*
 * After a drain took got slots back (bounded to 0 to
 * RMB_FRAMES_IN_FLIGHT): the others are given up on. 1 when the drain
 * gave up on a slot it waited for (it is logged); 0 when it took back
 * every slot it waited for.
 */
static inline int
rmb_frames_drained(rmb_frames_t *f, int got)
{
	int wait = rmb_frames_wait(f);

	if (got < 0)
	{
		got = 0;
	}
	else if (got > RMB_FRAMES_IN_FLIGHT)
	{
		got = RMB_FRAMES_IN_FLIGHT;
	}

	f->lost = RMB_FRAMES_IN_FLIGHT - got;
	return got < wait;
}

/* ---- the presents (rmb_present_drain, rmb_ahead_drop, rmb_present_commit) ---- */

/*
 * A drain of the presents is decided on the identities of two groups:
 * live, the one the presents handed to rmb_present_order are in (NULL
 * when there is none), and spent, the one a drain gave up on (NULL when
 * none has). A drain that gives up marks live spent: the presents it gave
 * up on are left to it, and a later drain of it waits for nothing. Only
 * that group is marked: the next present starts a new one, and a drain
 * of the new one waits the shared deadline again.
 */
#define RMB_DRAIN_SPENT -1   /* the group was given up on already: nothing is waited for */
#define RMB_DRAIN_LATE 0     /* the wait ended at the bound: the group is marked spent */
#define RMB_DRAIN_DONE 1     /* every present of the group is done, or there is no group */

/* 1: the drain waits for live, at most until the shared deadline; 0: it waits for nothing. */
static inline int
rmb_present_drain_waits(const void *live, const void *spent)
{
	return live && (live != spent);
}

/* The drain's result; done, read only when it waits, is 1 when the wait ended with every present done. */
static inline int
rmb_present_drain_result(const void *live, const void *spent, int done)
{
	if (!live)
	{
		return RMB_DRAIN_DONE;
	}

	if (live == spent)
	{
		return RMB_DRAIN_SPENT;
	}

	return done ? RMB_DRAIN_DONE : RMB_DRAIN_LATE;
}

/*
 * 1: a present slot still held after a drain with result is waited for
 * until the shared deadline; 0: taken back only if free at once. With the
 * new group done, a present of a group given up on earlier may still hold
 * one: it is waited for.
 */
static inline int
rmb_present_slot_waits(int result)
{
	return result != RMB_DRAIN_SPENT;
}

/*
 * 1: the drain marks live spent: its own wait ended at the bound, or late
 * is 1, a slot it waited for still held at the bound, so the second drain
 * of teardown waits for none.
 */
static inline int
rmb_present_drain_marks(int result, int late)
{
	return (result == RMB_DRAIN_LATE) || (late && (result != RMB_DRAIN_SPENT));
}

/* 1: a present starts a new group (rmb_present_commit): live was given up on and keeps only the presents it gave up on. */
static inline int
rmb_present_new_group(const void *live, const void *spent)
{
	return spent && (live == spent);
}

#endif
