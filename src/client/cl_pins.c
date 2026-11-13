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
 * Pin harness. pins_dir, pins_scenario and pins_capture are not cvars:
 * their values name files the client writes, so they come from the
 * process command line only. CL_PinsLatch copies them once, before any
 * config file, console command or server stufftext can run, into these
 * statics, which nothing else writes. A "set pins_dir ..." from a config
 * file, the console or a server (with or without flags) only creates an
 * unrelated cvar that nothing reads.
 */
static char pins_dir[MAX_OSPATH];
static char pins_scenario[64] = "pin";
static int pins_capture;
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
 * Called from Qcommon_Init right after COM_InitArgv, before
 * Cbuf_AddEarlyCommands blanks the "+set" arguments it consumes. Pairs
 * "+set <name> <value>" exactly as Cbuf_AddEarlyCommands does; the last
 * occurrence wins. Any refused value switches the harness off: nothing
 * is captured and nothing is written.
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
		else if (strcmp(name, "pins_capture") == 0)
		{
			l = strtol(value, &end, 10);

			if ((end == value) || (*end != '\0') || (l < 0) || (l > 1000000))
			{
				pins_invalid = true;
			}
			else
			{
				pins_capture = (int)l;
			}
		}
	}

	if (pins_invalid || !have_dir)
	{
		pins_dir[0] = '\0';
		CL_PinsCopy(pins_scenario, sizeof(pins_scenario), "pin");
		pins_capture = 0;
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

