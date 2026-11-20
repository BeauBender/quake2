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
 * =======================================================================
 */

#include "header/client.h"

char *keybindings[K_LAST];
/* if true, a server line wrote keybindings[] for this key (FIX156): a
   press queues it as server text and config.cfg leaves it out */
static qboolean keybindserver[K_LAST];

/*
 * A binding a server line writes is the server's (FIX156), as an alias
 * is (Cmd_Alias_f): pressing the key picks when it runs, the server
 * picked what, so Key_Event queues it as server text and a command that
 * refuses a server's use refuses it. Key_WriteBindings leaves it out of
 * config.cfg, which the next start runs as the player's text. The
 * player's text binding the key again makes it the player's; a server
 * line that writes the binding the key already has leaves its owner as
 * it was, so a server that runs the player's own configuration again
 * does not take over the player's keys.
 */
void
Key_SetBinding(int keynum, char *binding)
{
	char *new;
	int l;
	qboolean server;

	if (keynum == -1)
	{
		return;
	}

	server = Cbuf_ExecutingServerText();

	/* free old bindings */
	if (keybindings[keynum])
	{
		if (server && !strcmp(keybindings[keynum], binding))
		{
			server = keybindserver[keynum];
		}

		Z_Free(keybindings[keynum]);
		keybindings[keynum] = NULL;
	}

	/* allocate memory for new binding */
	l = strlen(binding);
	new = Z_Malloc(l + 1);
	strcpy(new, binding);
	new[l] = 0;
	keybindings[keynum] = new;
	keybindserver[keynum] = server;
}

/* True if a press of keynum queues its binding as the server's text. */
qboolean
Key_BindingIsServer(int keynum)
{
	return keybindserver[keynum];
}

/* Queues text from key's binding, as the server's if a server wrote it */
void
Key_AddBindingText(int key, const char *text)
{
	if (Key_BindingIsServer(key))
	{
		Cbuf_AddServerText(text);
	}
	else
	{
		Cbuf_AddText(text);
	}
}

/* unbindall: every bound key gets the empty binding. */
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

/* The "bind key value" lines of config.cfg. */
void
Key_WriteBindingLines(FILE *f)
{
	int i;

	for (i = 0; i < K_LAST; i++)
	{
		/* a server's binding would come back as the player's text */
		if (keybindings[i] && keybindings[i][0] && !keybindserver[i])
		{
			fprintf(f, "bind %s \"%s\"\n",
					Key_KeynumToString(i), keybindings[i]);
		}
	}
}

/* Frees every binding, at shutdown. */
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
	}
}
