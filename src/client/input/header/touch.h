/*
 * Copyright (C) 2026 Yamagi Quake II contributors
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
 * =======================================================================
 *
 * The touch layer (TASK150): fingers in, a virtual gamepad out. Pure C,
 * no SDL, built for every platform; src/client/input/sdl3.c feeds it
 * the finger events and hands its output to a virtual joystick.
 *
 * Coordinates are points inside the window; the layout is made from the
 * safe rectangle (so nothing sits under a notch or a home indicator).
 *
 * =======================================================================
 */

#ifndef CL_INPUT_TOUCH_H
#define CL_INPUT_TOUCH_H

#include <stdint.h>

/* Fingers followed at once, and the axes and buttons of the output. */
#define TOUCH_MAX_FINGERS 10
#define TOUCH_NUM_AXES 6

/* SDL_GamepadAxis order, the values in SDL's range (triggers 0 to 32767). */
#define TOUCH_AXIS_LEFTX 0
#define TOUCH_AXIS_LEFTY 1
#define TOUCH_AXIS_RIGHTX 2
#define TOUCH_AXIS_RIGHTY 3
#define TOUCH_AXIS_LTRIG 4
#define TOUCH_AXIS_RTRIG 5

#define TOUCH_AXIS_MAX 32767
#define TOUCH_AXIS_MIN (-32768)

/* SDL_GamepadButton order; the glue asserts that these equal SDL's. */
#define TOUCH_BTN_SOUTH 0
#define TOUCH_BTN_EAST 1
#define TOUCH_BTN_WEST 2
#define TOUCH_BTN_NORTH 3
#define TOUCH_BTN_BACK 4
#define TOUCH_BTN_START 6
#define TOUCH_BTN_MISC1 15
#define TOUCH_NUM_BUTTONS 16

/* The scale is held to this range. */
#define TOUCH_SCALE_MIN 0.5f
#define TOUCH_SCALE_MAX 2.0f

typedef struct
{
	float x, y, w, h;
} touch_rect_t;

typedef enum
{
	TZ_MOVE,	/* floating stick, left axes */
	TZ_LOOK,	/* floating stick, right axes */
	TZ_FIRE,	/* right trigger */
	TZ_JUMP,	/* south */
	TZ_CROUCH,	/* east */
	TZ_NEXTWEAP,	/* north */
	TZ_USE,		/* west */
	TZ_MENU,	/* start */
	TZ_CONSOLE,	/* misc1 */
	TZ_COUNT
} touch_zone_t;

typedef struct
{
	touch_rect_t zone[TZ_COUNT];
	float radius;	/* the stick radius, points */
} touch_layout_t;

typedef struct
{
	int active;
	uint64_t id;
	int zone;
	int off;	/* a button finger that slid off its button */
	float ox, oy;	/* where it went down */
	float x, y;	/* where it is */
} touch_finger_t;

typedef struct
{
	touch_layout_t layout;
	touch_finger_t finger[TOUCH_MAX_FINGERS];
} touch_t;

/* The zones for a safe rectangle; scale is held to [0.5, 2], swap mirrors them. */
void Touch_MakeLayout(touch_layout_t *l, touch_rect_t safe, float scale, int swap);

/* No finger followed. */
void Touch_Reset(touch_t *t);

void Touch_SetLayout(touch_t *t, touch_rect_t safe, float scale, int swap);
void Touch_Down(touch_t *t, uint64_t id, float x, float y);
void Touch_Motion(touch_t *t, uint64_t id, float x, float y);

/* A finger lifted, or the system took it away: the same. */
void Touch_Up(touch_t *t, uint64_t id);

/* The state now: axes (TOUCH_NUM_AXES) and the pressed buttons as a bit mask. */
void Touch_Output(const touch_t *t, int *axes, uint32_t *buttons);

/* The default gamepad bindings of iOS: entry i, or 0 past the end. */
int Touch_DefaultBind(int i, const char **key, const char **cmd);

#endif
