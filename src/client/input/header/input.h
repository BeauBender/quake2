/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
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
 * Header file for the generic input backend.
 *
 * =======================================================================
 */

#ifndef GEN_INPUT_H
#define GEN_INPUT_H

#include "../../../common/header/shared.h"
#include "touch.h"

/*
 * Saves the time of the last input event.
 */
extern int sys_frame_time;

/*
 * Initializes the input backend
 */
void IN_Init(void);

/*
 * Move handling
 */
void IN_Move(usercmd_t *cmd);

/*
 * Shuts the backend down
 */
void IN_Shutdown(void);

/*
 * Updates the state of the input queue
 */
void IN_Update(void);

/*
 * Removes all pending events from SDLs queue.
 */
void In_FlushQueue(void);

/*
 * The input gate (FIX48, ingate.c): false in a pin run, where every
 * key, button, character and analog movement the window system delivers
 * is dropped; true otherwise. IN_KeyEvent and IN_CharEvent pass an event
 * to Key_Event and Char_Event only while it is true.
 */
qboolean IN_InputOpen(void);
void IN_KeyEvent(int key, qboolean down, qboolean special);
void IN_CharEvent(int key);

/*
 * The touch overlay (TASK151): what to draw now, at most max entries, and
 * the pixels one point of the window is. 0 when the touch controls are
 * off or a physical gamepad is in use.
 */
int IN_TouchOverlay(touch_draw_t *list, int max, float *pixels_per_point);

/* Clipboard get/set */

/* Copy clipboard to buffer of size n */
void IN_GetClipboardText(char *out, size_t n);

/* Copy text to clipboard */
int IN_SetClipboardText(const char *s);

#endif
