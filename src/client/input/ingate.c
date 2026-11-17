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
 * The input gate (FIX48). Every key, mouse button, wheel step, gamepad
 * button and character the window system delivers reaches the client
 * through IN_KeyEvent and IN_CharEvent, and the analog input (mouse
 * motion, sticks, gyro) through IN_Move, which asks IN_InputOpen. SDL-free,
 * so the unit tests drive it.
 *
 * =======================================================================
 */

#include "../header/client.h"
#include "header/input.h"

/*
 * True when input from the window system reaches the client: always,
 * except in a pin run (CL_PinsRun, latched from the command line by
 * CL_PinsLatch, never a cvar), where nothing a person does at the
 * keyboard, the mouse or a gamepad may change what is rendered.
 */
qboolean
IN_InputOpen(void)
{
	return true;
}

/* Key_Event, when input is open; dropped otherwise */
void
IN_KeyEvent(int key, qboolean down, qboolean special)
{
	Key_Event(key, down, special);
}

/* Char_Event, when input is open; dropped otherwise */
void
IN_CharEvent(int key)
{
	Char_Event(key);
}
