/*
 * tests/pins/latch-test.c -- driver for tests/check-pins-latch.sh. Links the
 * real src/client/cl_pins.c and src/common/argproc.c, latches its own
 * command line exactly as Qcommon_Init does (COM_InitArgv, CL_PinsLatch),
 * then blanks the "+set" arguments as Cbuf_AddEarlyCommands(true) does and
 * prints what the pin harness will use. Needs no SDL and no engine.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/header/client.h"

void CL_PinsLatch(void);

void
Com_Error(int code, const char *fmt, ...)
{
	va_list ap;

	(void)code;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

void
Com_Printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

void *
Z_Malloc(size_t size)
{
	return calloc(1, size);
}

int
main(int argc, char **argv)
{
	int i;

	COM_InitArgv(argc, argv);
	CL_PinsLatch();

	/* What Cbuf_AddEarlyCommands(true) does to argv afterwards. */
	for (i = 0; i < COM_Argc(); i++)
	{
		if (strcmp(COM_Argv(i), "+set") == 0)
		{
			COM_ClearArgv(i);
			COM_ClearArgv(i + 1);
			COM_ClearArgv(i + 2);
			i += 2;
		}
	}

	printf("dir=%s|scenario=%s|capture=%d|invalid=%d|record=%s|replay=%s%s\n", CL_PinsDir(),
		CL_PinsScenario(), CL_PinsCapture(), (int)CL_PinsInvalid(), CL_PinsRecord(),
		CL_PinsReplayPath(), CL_PinsReplayRequested() ? "|requested" : "");
	/* the pin-run predicate the config, error and renderer guards test */
	printf("run=%d\n", (int)CL_PinsRun());
	/* the fixed PRNG seed R_BeginRegistration applies, or none */
	{
		unsigned int seed;

		if (CL_PinsSeed(&seed))
		{
			printf("seed=%u\n", seed);
		}
		else
		{
			printf("seed=off\n");
		}
	}
	return 0;
}
