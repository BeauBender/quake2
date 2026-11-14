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
 * The engine's PRNG (src/common/shared/rand.c) and its fixed seed
 * (randk_seed_fixed, the pin harness's pins_seed, TASK45): a fixed seed
 * gives the same stream every time and a different one per seed,
 * randk_seed() still seeds from OS entropy afterwards, and no seed of
 * the PRNG reaches the server's connectionless challenge, which draws
 * on Sys_GetRandomBytes alone.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/server/header/server.h"

void SV_ConnectionlessPacket(void);

#define T_RAND_N 16

static void
t_rand_stream(int *out)
{
	int i;

	for (i = 0; i < T_RAND_N; i++)
	{
		out[i] = randk();
	}
}

TEST(randk_fixed_seed_repeats)
{
	int a[T_RAND_N], b[T_RAND_N], c[T_RAND_N], d[T_RAND_N], i;

	randk_seed_fixed(7);
	t_rand_stream(a);
	randk_seed_fixed(7);
	t_rand_stream(b);
	randk_seed_fixed(8);
	t_rand_stream(c);
	/* 0 is a seed like any other, and does not leave the state at zero */
	randk_seed_fixed(0);
	t_rand_stream(d);

	T_EQ_MEM(a, b, sizeof(a));
	T_CHECK(memcmp(a, c, sizeof(a)) != 0);
	T_CHECK(memcmp(a, d, sizeof(a)) != 0);
	T_CHECK(memcmp(d + 1, d, sizeof(d) - sizeof(d[0])) != 0);

	for (i = 0; i < T_RAND_N; i++)
	{
		T_CHECK(a[i] >= 0 && d[i] >= 0);
	}

	/* the same numbers on every platform and every build: a pin recorded
	   with pins_seed 7 on one host replays its particles on another */
	randk_seed_fixed(7);
	T_EQ_INT(randk(), 638105636);
	T_EQ_INT(randk(), 1110157193);
	T_EQ_INT(randk(), 1925703639);
	T_EQ_INT(randk(), 411274451);
	randk_seed_fixed(0);
	T_EQ_INT(randk(), 1549435686);
	T_EQ_INT(randk(), 1760712810);

	/* the stream after a fixed seed is not the one before it */
	randk_seed_fixed(7);
	t_rand_stream(a);
	t_rand_stream(b);
	T_CHECK(memcmp(a, b, sizeof(a)) != 0);

	/* and the float helpers follow it */
	randk_seed_fixed(7);
	{
		float f = frandk(), g = crandk();

		randk_seed_fixed(7);
		T_EQ_FLT(frandk(), f);
		T_EQ_FLT(crandk(), g);
	}
}

/* With no fixed seed (no pins_seed), seeding is from entropy, as before. */
TEST(randk_entropy_seed_after_fixed)
{
	int a[T_RAND_N], b[T_RAND_N], c[T_RAND_N];

	randk_seed_fixed(7);
	t_rand_stream(a);
	randk_seed();
	t_rand_stream(b);
	/* from the same state as before b, so only the entropy can tell them apart */
	randk_seed_fixed(7);
	t_rand_stream(a);
	randk_seed();
	t_rand_stream(c);

	/* 16 draws of 31 bits: equal streams by chance are out of reach */
	T_CHECK(memcmp(a, b, sizeof(a)) != 0);
	T_CHECK(memcmp(b, c, sizeof(b)) != 0);
}

static int
t_rand_challenge(void)
{
	static byte inbuf[MAX_MSGLEN], replybuf[MAX_MSGLEN];
	sizebuf_t reply;
	netadr_t from;
	int i, challenge = -1;

	/* forget every challenge, so the next one is drawn anew */
	for (i = 0; i < MAX_CHALLENGES; i++)
	{
		memset(&svs.challenges[i], 0, sizeof(svs.challenges[i]));
		svs.challenges[i].adr.type = NA_BROADCAST;
	}

	SZ_Init(&net_message, inbuf, sizeof(inbuf));
	MSG_WriteLong(&net_message, -1);
	SZ_Print(&net_message, "getchallenge");
	memset(&net_from, 0, sizeof(net_from));
	net_from.type = NA_LOOPBACK;
	SV_ConnectionlessPacket();

	SZ_Init(&reply, replybuf, sizeof(replybuf));

	if (!NET_GetPacket(NS_CLIENT, &from, &reply))
	{
		return -1;
	}

	MSG_BeginReading(&reply);
	MSG_ReadLong(&reply);
	sscanf(MSG_ReadStringLine(&reply), "challenge %d", &challenge);

	return challenge;
}

/*
 * The fixed seed never makes the challenge predictable: after the same
 * fixed seed, the challenges still differ. Six draws of 15 bits all
 * equal by chance: 2^-75.
 */
TEST(randk_fixed_seed_not_in_challenge)
{
	int first, same = 0, i;

	randk_seed_fixed(1234);
	first = t_rand_challenge();
	T_REQUIRE(first >= 0 && first <= 0x7fff);

	for (i = 0; i < 5; i++)
	{
		int c;

		randk_seed_fixed(1234);
		c = t_rand_challenge();
		T_REQUIRE(c >= 0 && c <= 0x7fff);
		same += (c == first);
	}

	T_CHECK(same < 5);
	randk_seed();
}
