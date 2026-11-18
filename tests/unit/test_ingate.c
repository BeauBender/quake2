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
 * The input gate (FIX48, src/client/input/ingate.c): what the window
 * system delivers reaches Key_Event and Char_Event, the entry points of
 * the console, the menu and the key bindings, unless a pin run is on.
 *
 * - With no pin run, every key (escape, which opens the menu; the console
 *   key; a key bound to quit), every mouse button and every character
 *   reaches the client exactly as given, in order, and the analog input
 *   is open.
 * - In a pin run (CL_PinsRun, the command-line latch) nothing reaches
 *   either entry point and the analog input is closed, so a person at
 *   the keyboard cannot open the menu, pause the demo or quit.
 *
 * Not proven here: the SDL glue (sdl3.c sends every key and character
 * through IN_KeyEvent and IN_CharEvent and asks IN_InputOpen in IN_Move
 * and on a focus change), see the FIX48 runbook.
 *
 * =======================================================================
 */

#include "test.h"
#include "stub_client.h"
#include "../../src/client/header/client.h"
#include "../../src/client/input/header/input.h"

static void
t_deliver(void)
{
	memset(&stub_keys, 0, sizeof(stub_keys));
	IN_KeyEvent(K_ESCAPE, true, true);
	IN_KeyEvent(K_ESCAPE, false, true);
	IN_KeyEvent(K_CONSOLE, true, true);
	IN_KeyEvent('q', true, false);	/* bound to quit, say */
	IN_KeyEvent(K_MOUSE1, true, true);
	IN_KeyEvent(K_MWHEELUP, true, true);
	IN_CharEvent('a');
}

TEST(ingate_without_a_pin_run_every_event_reaches_the_client)
{
	stub_pins_run = 0;
	t_deliver();

	T_CHECK(IN_InputOpen());
	T_EQ_INT(stub_keys.n, 6);
	T_EQ_INT(stub_keys.key[0], K_ESCAPE);
	T_EQ_INT(stub_keys.down[0], 1);
	T_EQ_INT(stub_keys.key[1], K_ESCAPE);
	T_EQ_INT(stub_keys.down[1], 0);
	T_EQ_INT(stub_keys.key[2], K_CONSOLE);
	T_EQ_INT(stub_keys.key[3], 'q');
	T_EQ_INT(stub_keys.key[4], K_MOUSE1);
	T_EQ_INT(stub_keys.key[5], K_MWHEELUP);
	T_EQ_INT(stub_keys.chars, 1);
	T_EQ_INT(stub_keys.last_char, 'a');
}

TEST(ingate_in_a_pin_run_no_event_reaches_the_client)
{
	stub_pins_run = 1;
	t_deliver();

	T_CHECK(!IN_InputOpen());
	T_EQ_INT(stub_keys.n, 0);
	T_EQ_INT(stub_keys.chars, 0);

	/* the latch is read on every event: off again, open again */
	stub_pins_run = 0;
	t_deliver();
	T_CHECK(IN_InputOpen());
	T_EQ_INT(stub_keys.n, 6);
	T_EQ_INT(stub_keys.chars, 1);
}
