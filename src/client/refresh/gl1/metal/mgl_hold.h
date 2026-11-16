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
 * The present slots' holds, and the waits for them when the present state
 * is dropped (FIX69, FIX77), apart from Metal and libdispatch, so that the
 * host test (tests/metal/mgl_host_test.c) reaches them. mgl_layer.m holds
 * the rest of the present path; its header comment tells the design.
 *
 * =======================================================================
 */

#ifndef MGL_HOLD_H
#define MGL_HOLD_H

#include <stdatomic.h>
#include <stdint.h>

/*
 * A present slot after its frame: the command buffers that hold it, the
 * present, and past the bound the one that ends the present's wait
 * (mgl_present_end_wait). mgl_layer.m makes one for each slot of each
 * context, and the completed handlers capture it, so a handler that
 * completes after its context was torn down counts down that context's
 * hold, never the next one's (FIX77).
 */
typedef struct mgl_hold_s
{
	atomic_int holds;   /* 0, 1 (the present), or 2 past the bound */
	int lost;           /* the engine's thread: a drop gave up on the slot, and no later drop waits for it */
} mgl_hold_t;

/* The present of the slot's frame is committed: it holds the slot. */
static inline void
mgl_hold_commit(mgl_hold_t *h)
{
	h->lost = 0;   /* the present given up on has completed: the frame took the slot */
	atomic_store(&h->holds, 1);
}

/* A frame takes the slot (mgl_present_take). */
static inline void
mgl_hold_take(mgl_hold_t *h)
{
	(void)h;
}

/*
 * The present's wait is to be ended: 1 when the present still holds the
 * slot alone, and the command buffer that ends the wait now holds it too;
 * 0 when the present has completed or its wait was ended already.
 */
static inline int
mgl_hold_extend(mgl_hold_t *h)
{
	int one = 1;

	return atomic_compare_exchange_strong(&h->holds, &one, 2);
}

/* A command buffer that held the slot has completed: 1 when it was the last, and the slot is to be given back. */
static inline int
mgl_hold_release(mgl_hold_t *h)
{
	return atomic_fetch_sub(&h->holds, 1) == 1;
}

/*
 * The caller's waits on the slots (mgl_ahead_drop; fakes in the host
 * test). A deadline is the caller's own value (a dispatch_time_t there),
 * and 0 is now.
 */
typedef struct mgl_drop_ops_s
{
	void *ctx;
	uint64_t (*deadline)(void *ctx);                    /* MGL_GPU_WAIT_MS from now */
	int (*take)(void *ctx, int slot, uint64_t until);   /* 1: the slot was free by until and is taken */
	void (*give)(void *ctx, int slot);                  /* gives a slot taken back */
	void (*end_wait)(void *ctx, int slot);              /* ends the wait of the slot's present for its frame */
} mgl_drop_ops_t;

/*
 * Waits for every present slot of holds[0..n) but skip (the open frame's)
 * and those without a hold (at most 32), and gives each back, in two
 * stages of one deadline each, MGL_GPU_WAIT_MS: the slots still held at
 * the first have their presents' waits ended and share the second. A slot
 * still held then is given up on (lost): left held, and a later drop does
 * not wait for it again, it only takes it back if it is free by then. So a
 * hung GPU holds a drop two deadlines at most, whatever the number of
 * slots, and a second drop no longer than it takes to look. Returns the
 * number of slots given up on in this drop.
 */
static inline int
mgl_drop_presents(mgl_hold_t *const *holds, int n, int skip, const mgl_drop_ops_t *ops)
{
	uint64_t until = ops->deadline(ops->ctx);
	unsigned late = 0;
	int i, lost = 0;

	for (i = 0; (i < n) && (i < 32); i++)
	{
		if (!holds[i] || (i == skip))
		{
			continue;
		}

		if (ops->take(ops->ctx, i, holds[i]->lost ? 0 : until))
		{
			holds[i]->lost = 0;
			ops->give(ops->ctx, i);
		}
		else if (!holds[i]->lost)
		{
			ops->end_wait(ops->ctx, i);
			late |= 1u << i;
		}
	}

	if (!late)
	{
		return 0;
	}

	until = ops->deadline(ops->ctx);

	for (i = 0; (i < n) && (i < 32); i++)
	{
		if (!(late & (1u << i)))
		{
			continue;
		}

		if (ops->take(ops->ctx, i, until))
		{
			ops->give(ops->ctx, i);
		}
		else
		{
			holds[i]->lost = 1;
			lost++;
		}
	}

	return lost;
}

#endif
