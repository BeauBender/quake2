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
 * The pin harness's command-line latch (docs/plans/metal-port-harness.md).
 * Depends on argproc.c only, so tests/check-pins-latch.sh can link it
 * without SDL and without the engine.
 *
 * =======================================================================
 */

#include "header/client.h"

/*
 * Pin harness. pins_dir, pins_scenario, pins_capture, pins_record,
 * pins_replay and pins_seed are not cvars: their values name files the
 * client writes or reads, or fix its random numbers, so they come from
 * the process command line only. CL_PinsLatch copies them once, before any
 * config file, console command or server stufftext can run, into these
 * statics, which nothing else writes. A "set pins_dir ..." from a config
 * file, the console or a server (with or without flags) only creates an
 * unrelated cvar that nothing reads.
 */
static char pins_dir[MAX_OSPATH];
static char pins_scenario[64] = "pin";
static int pins_capture;
static char pins_record[64];	/* a file name inside pins_dir, "" for no trace */
static char pins_replay[MAX_OSPATH];	/* the trace a replay reads, "" for none */
static qboolean pins_replay_requested;	/* given, accepted or not */
static unsigned int pins_seed;	/* the fixed PRNG seed of a record or replay */
static qboolean pins_seed_given;
static qboolean pins_invalid;

/* Copies src into dst; false when it is empty or does not fit. */
static qboolean
CL_PinsCopy(char *dst, size_t size, const char *src)
{
	size_t len = strlen(src);

	if ((len == 0) || (len >= size))
	{
		return false;
	}

	memcpy(dst, src, len + 1);
	return true;
}

/*
 * A trace path: [A-Za-z0-9_./-] only, never "..", a file name that
 * starts with [A-Za-z0-9_] and ends in ".q2r". The file is only read.
 */
static qboolean
CL_PinsReplayValid(const char *value)
{
	const char *p, *base = strrchr(value, '/');
	size_t len = strlen(value);

	base = base ? base + 1 : value;

	if (strstr(value, "..") || (strlen(base) <= 4) ||
		(strcmp(value + len - 4, ".q2r") != 0) ||
		!(((*base >= 'a') && (*base <= 'z')) || ((*base >= 'A') && (*base <= 'Z')) ||
		  ((*base >= '0') && (*base <= '9')) || (*base == '_')))
	{
		return false;
	}

	for (p = value; *p; p++)
	{
		if (!(((*p >= 'a') && (*p <= 'z')) || ((*p >= 'A') && (*p <= 'Z')) ||
			((*p >= '0') && (*p <= '9')) || (*p == '_') || (*p == '.') ||
			(*p == '/') || (*p == '-')))
		{
			return false;
		}
	}

	return true;
}

/*
 * Called from Qcommon_Init right after COM_InitArgv, before
 * Cbuf_AddEarlyCommands blanks the "+set" arguments it consumes. Pairs
 * "+set <name> <value>" exactly as Cbuf_AddEarlyCommands does. When an
 * argument is given more than once the last occurrence is used, but any
 * refused occurrence turns the harness off for good (pins_invalid is
 * sticky, a later valid one does not clear it): nothing is captured and
 * nothing is written.
 */
void
CL_PinsLatch(void)
{
	qboolean have_dir = false;
	const char *name, *value, *p;
	char *end;
	long l;
	int i;

	for (i = 0; i < COM_Argc(); i++)
	{
		if (strcmp(COM_Argv(i), "+set") != 0)
		{
			continue;
		}

		name = COM_Argv(i + 1);
		value = COM_Argv(i + 2);
		i += 2;

		if (strcmp(name, "pins_dir") == 0)
		{
			have_dir = true;

			if (!CL_PinsCopy(pins_dir, sizeof(pins_dir), value) ||
				strstr(value, ".."))
			{
				pins_invalid = true;
			}
		}
		else if (strcmp(name, "pins_scenario") == 0)
		{
			if (!CL_PinsCopy(pins_scenario, sizeof(pins_scenario), value))
			{
				pins_invalid = true;
			}

			/* [a-z0-9_][a-z0-9_-]*, as scripts/pins.sh check_name */
			for (p = value; *p; p++)
			{
				if (!(((*p >= 'a') && (*p <= 'z')) ||
					((*p >= '0') && (*p <= '9')) || (*p == '_') ||
					((*p == '-') && (p != value))))
				{
					pins_invalid = true;
				}
			}
		}
		else if (strcmp(name, "pins_record") == 0)
		{
			/* a file name, written inside pins_dir only:
			   [A-Za-z0-9_][A-Za-z0-9_.-]*, never "..", no directory,
			   ending in ".q2r" so it never names a pin frame (.tga)
			   or the manifest the harness writes beside it */
			if (!CL_PinsCopy(pins_record, sizeof(pins_record), value) ||
				strstr(value, "..") || (strlen(value) <= 4) ||
				(strcmp(value + strlen(value) - 4, ".q2r") != 0))
			{
				pins_invalid = true;
			}

			for (p = value; *p; p++)
			{
				if (!(((*p >= 'a') && (*p <= 'z')) ||
					((*p >= 'A') && (*p <= 'Z')) ||
					((*p >= '0') && (*p <= '9')) || (*p == '_') ||
					(((*p == '-') || (*p == '.')) && (p != value))))
				{
					pins_invalid = true;
				}
			}
		}
		else if (strcmp(name, "pins_replay") == 0)
		{
			pins_replay_requested = true;

			if (!CL_PinsReplayValid(value) ||
				!CL_PinsCopy(pins_replay, sizeof(pins_replay), value))
			{
				pins_invalid = true;
			}
		}
		else if (strcmp(name, "pins_seed") == 0)
		{
			/* 1 to 9 digits: 0 to 999999999, the same on every platform */
			pins_seed_given = true;
			pins_seed = 0;

			if ((value[0] == '\0') || (strlen(value) > 9))
			{
				pins_invalid = true;
			}

			for (p = value; *p; p++)
			{
				if ((*p < '0') || (*p > '9'))
				{
					pins_invalid = true;
					break;
				}

				pins_seed = pins_seed * 10 + (unsigned int)(*p - '0');
			}
		}
		else if (strcmp(name, "pins_capture") == 0)
		{
			/* digits only: strtol alone skips leading space and takes a sign */
			l = strtol(value, &end, 10);

			if ((value[0] < '0') || (value[0] > '9') || (end == value) ||
				(*end != '\0') || (l < 0) || (l > 1000000))
			{
				pins_invalid = true;
			}
			else
			{
				pins_capture = (int)l;
			}
		}
	}

	/* a replay is not recorded, and writes its frames to a store */
	if (pins_replay[0] && (pins_record[0] || !have_dir))
	{
		pins_invalid = true;
	}

	/* a fixed seed is for a record or a replay only (vid.c) */
	if (pins_seed_given && !pins_record[0] && !pins_replay[0])
	{
		pins_invalid = true;
	}

	if (pins_invalid || !have_dir)
	{
		pins_seed_given = false;
		pins_seed = 0;
		pins_dir[0] = '\0';
		CL_PinsCopy(pins_scenario, sizeof(pins_scenario), "pin");
		pins_capture = 0;
		pins_record[0] = '\0';
		pins_replay[0] = '\0';
	}
}

/* The latched pin store directory, "" when the harness is off. */
const char *
CL_PinsDir(void)
{
	return pins_dir;
}

/* The latched file-name prefix of pin frames. */
const char *
CL_PinsScenario(void)
{
	return pins_scenario;
}

/* The latched trace file name inside CL_PinsDir, "" when nothing is recorded. */
const char *
CL_PinsRecord(void)
{
	return pins_record;
}

/* The latched capture interval, 0 when the harness is off. */
int
CL_PinsCapture(void)
{
	return pins_capture;
}

/* True when a pins_* argument was given and refused by CL_PinsLatch. */
qboolean
CL_PinsInvalid(void)
{
	return pins_invalid;
}

/* The latched trace a replay reads, "" when there is no replay. */
const char *
CL_PinsReplayPath(void)
{
	return pins_replay;
}

/*
 * True when pins_seed was given and accepted, with pins_record or
 * pins_replay: *seed is then the fixed seed R_BeginRegistration applies
 * (the fixed variant in rand.c). False otherwise, and the PRNG keeps its entropy seed.
 */
qboolean
CL_PinsSeed(unsigned int *seed)
{
	*seed = pins_seed;
	return pins_seed_given;
}

/* True when pins_replay was given, whether or not it was accepted. */
qboolean
CL_PinsReplayRequested(void)
{
	return pins_replay_requested;
}

/*
 * True for a pin run: a capture, or a replay (requested, whether or not
 * it was accepted; a refused one stops the engine in CL_Init). A pin run
 * never reads or writes the saved configuration, turns a dropped run
 * into a fatal error and never falls back to another renderer.
 */
qboolean
CL_PinsRun(void)
{
	return (pins_capture > 0) || pins_replay_requested;
}
