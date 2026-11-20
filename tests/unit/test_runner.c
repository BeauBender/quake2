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
 * The runner itself (tests/unit/main.c): the parts of t_expect_exit that
 * a gate depends on and no engine test exercises.
 *
 * =======================================================================
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "test.h"

static void
hang_forever(void *arg)
{
	(void)arg;

	for (;;)
	{
		pause();
	}
}

static void
exit_with(void *arg)
{
	exit(*(int *)arg);
}

static void
die_by_abort(void *arg)
{
	(void)arg;

	abort();
}

TEST(runner_expect_exit)
{
	char out[256];
	unsigned saved = t_child_timeout;
	int code = 7;

	/* The child's exit status comes back as is. */
	T_EQ_INT(t_expect_exit(exit_with, &code, out, sizeof(out)), 7);

	/* A child that never ends is killed after t_child_timeout seconds
	   instead of hanging the suite. */
	t_child_timeout = 1;
	T_EQ_INT(t_expect_exit(hang_forever, NULL, out, sizeof(out)), T_EXIT_TIMEOUT);
	t_child_timeout = saved;
}

TEST(runner_expect_exit_signalled)
{
	char out[512];
	int before = t_failures();
	int ret = t_expect_exit(die_by_abort, NULL, out, sizeof(out));
	int added = t_failures() - before;

	/* Put the count back first: the failure below is the expected one.
	   A green run's log therefore shows one FAIL line for this test. */
	t_set_failures(before);

	/* A child killed by a signal returns -1 and fails the calling test
	   once, with the signal in the message. */
	T_EQ_INT(ret, -1);
	T_EQ_INT(added, 1);
	T_CHECK(strstr(out, "died by a signal") != NULL);
}
