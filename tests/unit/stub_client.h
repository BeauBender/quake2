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
 * What the client stubs in stub_client_full.c record, and the one call
 * that gives their cvars a value.
 *
 * =======================================================================
 */

#ifndef YQ2_TESTS_UNIT_STUB_CLIENT_H
#define YQ2_TESTS_UNIT_STUB_CLIENT_H

typedef struct
{
	int lights;       /* V_AddLight calls */
	float last_light; /* the intensity of the last one */
	int chars;        /* Draw_CharScaled calls */
	int pics;         /* Draw_PicScaled calls */
	int begins;       /* R_BeginFrame calls */
	int ends;         /* R_EndFrame calls */
	int ents;         /* V_AddEntity calls */
	int min_ent_frame; /* the lowest frame of them */
} stub_calls_t;

extern stub_calls_t stub_calls;

/* The calls of Key_Event and Char_Event, in order (test_ingate.c). */
#define STUB_MAX_KEYS 16

typedef struct
{
	int n;                    /* Key_Event calls */
	int key[STUB_MAX_KEYS];   /* their keys */
	int down[STUB_MAX_KEYS];  /* and down flags */
	int chars;                /* Char_Event calls */
	int last_char;            /* the last character */
} stub_keys_t;

extern stub_keys_t stub_keys;

/* What the CL_PinsRun stub returns: false but in the input gate's test. */
extern int stub_pins_run;

/* Creates the cvar pointers cl_*.c read without checking for NULL. */
void stub_client_cvars(void);

/* A client connected to a remote server, for a file that cannot include
   client.h (server.h has its own client_state_t): an IP address with no
   open socket, so CL_Disconnect's goodbye sends nothing. */
void stub_client_connect(void);
int stub_client_connected(void);  /* cls.state >= ca_connected */

#endif
