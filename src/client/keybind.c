/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * The key bindings and who wrote them (FIX156), apart from the rest of
 * the keyboard code so that the unit tests link it: it needs no SDL, no
 * menu and no key name table but Key_KeynumToString.
 *
 * A binding a server line writes is the server's, as an alias is
 * (Cmd_Alias_f): pressing the key picks when it runs, the server picked
 * what, so Key_Event queues it as server text and a command that
 * refuses a server's use refuses it.
 *
 * What config.cfg saves is only ever what the player's own text wrote:
 * each key keeps the player's last binding apart (keybindplayer[]),
 * which only the player's text writes, and Key_WriteBindingLines writes
 * that. A server write never removes or replaces it. A server line
 * that writes the player's own binding gives the key back to the
 * player, so a server line that runs the player's config.cfg, its
 * leading unbindall included (`exec config.cfg`, or `game <dir>` on a
 * remote client, which runs default.cfg and config.cfg inside that
 * line), leaves every key the player's and config.cfg as it was.
 *
 * =======================================================================
 */

#include "header/client.h"

char *keybindings[K_LAST];
/* the player's own last binding of each key: what config.cfg saves */
static char *keybindplayer[K_LAST];
/* if true, a server line wrote keybindings[] for this key: a press
   queues it as server text */
static qboolean keybindserver[K_LAST];

/* Replaces *slot by a copy of binding (which may be *slot itself). */
static void
Key_SetBody(char **slot, const char *binding)
{
	size_t l = strlen(binding);
	char *new = Z_Malloc(l + 1);

	memcpy(new, binding, l + 1);

	if (*slot)
	{
		Z_Free(*slot);
	}

	*slot = new;
}

void
Key_SetBinding(int keynum, char *binding)
{
	qboolean server;

	/* a key number outside the tables, for example a server's
	   `bind "<0x80>" x` where char is signed, binds nothing */
	if ((keynum < 0) || (keynum >= K_LAST) || !binding)
	{
		return;
	}

	server = Cbuf_ExecutingServerText();

	if (!server)
	{
		Key_SetBody(&keybindplayer[keynum], binding);
	}
	else if (!strcmp(binding, keybindplayer[keynum] ? keybindplayer[keynum] : ""))
	{
		/* the player's own binding: the key is the player's again */
		server = false;
	}

	Key_SetBody(&keybindings[keynum], binding);
	keybindserver[keynum] = server;
}

/* True if a press of keynum queues its binding as the server's text;
   a key outside the tables is never the player's. */
qboolean
Key_BindingIsServer(int keynum)
{
	if ((keynum < 0) || (keynum >= K_LAST))
	{
		return true;
	}

	return keybindserver[keynum];
}

/* Queues text from key's binding, as the server's if a server wrote it;
   a key outside the tables has no binding and queues nothing. */
void
Key_AddBindingText(int key, const char *text)
{
	if ((key < 0) || (key >= K_LAST))
	{
		return;
	}

	if (keybindserver[key])
	{
		Cbuf_AddServerText(text);
	}
	else
	{
		Cbuf_AddText(text);
	}
}

/* unbindall: every bound key gets the empty binding; from the player's
   text that clears what config.cfg saves too, from a server's it does
   not. */
void
Key_UnbindAll(void)
{
	int i;

	for (i = 0; i < K_LAST; i++)
	{
		if (keybindings[i])
		{
			Key_SetBinding(i, "");
		}
	}
}

/* The "bind key value" lines of config.cfg: the player's own bindings,
   never a server's, which the next start would run as the player's. */
void
Key_WriteBindingLines(FILE *f)
{
	int i;

	for (i = 0; i < K_LAST; i++)
	{
		if (keybindplayer[i] && keybindplayer[i][0])
		{
			fprintf(f, "bind %s \"%s\"\n",
					Key_KeynumToString(i), keybindplayer[i]);
		}
	}
}

/* Frees every binding, the player's saved ones too, at shutdown. */
void
Key_FreeBindings(void)
{
	int i;

	for (i = 0; i < K_LAST; ++i)
	{
		if (keybindings[i])
		{
			Z_Free(keybindings[i]);
			keybindings[i] = NULL;
		}

		if (keybindplayer[i])
		{
			Z_Free(keybindplayer[i]);
			keybindplayer[i] = NULL;
		}

		keybindserver[i] = false;
	}
}
