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
} stub_calls_t;

extern stub_calls_t stub_calls;

/* Creates the cvar pointers cl_*.c read without checking for NULL. */
void stub_client_cvars(void);

#endif
