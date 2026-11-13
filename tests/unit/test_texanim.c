/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Texture animation: R_TextureAnimation (src/client/refresh/files/surf.c,
 * shared by all three renderers) walks a texinfo's animation chain by
 * the entity's frame. The frame comes from the server or from the
 * client time, and a negative one used to start the step count below
 * zero (FIX25, D11). The chains here are built the way Mod_LoadTexinfo
 * builds them: numframes is the length counted from each texinfo.
 * surf.c is linked on its own; it needs only shared.c.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/refresh/ref_shared.h"

/* Distinct addresses stand in for the images; only identity matters. */
static char images[4];

#define IMG(i) ((struct image_s *)&images[(i)])

/* A -> B -> C -> A */
static mtexinfo_t ring[3];

/* A -> B -> C -> NULL */
static mtexinfo_t line[3];

static void
build_chains(void)
{
	int i;

	memset(ring, 0, sizeof(ring));
	memset(line, 0, sizeof(line));

	for (i = 0; i < 3; i++)
	{
		ring[i].image = IMG(i);
		ring[i].next = &ring[(i + 1) % 3];
		ring[i].numframes = 3;

		line[i].image = IMG(i);
		line[i].next = (i < 2) ? &line[i + 1] : NULL;
		line[i].numframes = 3 - i;
	}
}

static struct image_s *
anim(const mtexinfo_t *tex, int frame)
{
	entity_t ent;

	memset(&ent, 0, sizeof(ent));
	ent.frame = frame;

	return R_TextureAnimation(&ent, tex);
}

TEST(texanim_in_range_frames)
{
	int f;

	build_chains();

	for (f = 0; f < 9; f++)
	{
		T_CHECK(anim(&ring[0], f) == IMG(f % 3));
		T_CHECK(anim(&ring[1], f) == IMG((1 + f) % 3));
		T_CHECK(anim(&line[0], f) == IMG(f % 3));
		T_CHECK(anim(&line[1], f) == IMG(1 + f % 2));
		T_CHECK(anim(&line[2], f) == IMG(2));
	}

	/* the largest frame selects by its remainder, as before */
	T_CHECK(anim(&ring[0], 0x7fffffff) == IMG(0x7fffffff % 3));
	T_CHECK(anim(&line[0], 0x7fffffff) == IMG(0x7fffffff % 3));

	/* no chain, or no entity: the texinfo's own image */
	T_CHECK(anim(&line[2], 5) == IMG(2));
	T_CHECK(R_TextureAnimation(NULL, &ring[0]) == IMG(0));
}

static void
negative_frames(void *arg)
{
	static const int frames[] = { -1, -2, -3, -7, -200, -0x7fffffff - 1 };
	size_t i;

	(void)arg;

	build_chains();

	for (i = 0; i < sizeof(frames) / sizeof(frames[0]); i++)
	{
		/* a circular chain: the walk must end, at the first frame */
		T_CHECK(anim(&ring[0], frames[i]) == IMG(0));
		T_CHECK(anim(&ring[2], frames[i]) == IMG(2));

		/* a chain with an end: the walk must not step past it */
		T_CHECK(anim(&line[0], frames[i]) == IMG(0));
		T_CHECK(anim(&line[1], frames[i]) == IMG(1));
	}
}

/* In a child: without the bound the walk on the circular chain runs
   about 2^32 steps and the one on the open chain reads through NULL. */
TEST(texanim_negative_frame_is_first_frame)
{
	char out[1024];
	unsigned saved = t_child_timeout;
	int status;

	t_child_timeout = 3;
	status = t_expect_exit(negative_frames, NULL, out, sizeof(out));
	t_child_timeout = saved;

	T_EQ_INT(status, 0);
}
