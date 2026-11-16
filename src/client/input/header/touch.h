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
#define TOUCH_BTN_DPAD_UP 11
#define TOUCH_BTN_DPAD_DOWN 12
#define TOUCH_BTN_DPAD_LEFT 13
#define TOUCH_BTN_DPAD_RIGHT 14
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
	TZ_CONSOLE,	/* misc1, in both modes */
	TZ_COUNT,	/* the zones of the game mode */

	/* The menu mode (TASK151): a d-pad, OK and Back on the same pad, and the console button. */
	TZM_UP = TZ_COUNT,	/* dpad up */
	TZM_DOWN,		/* dpad down */
	TZM_LEFT,		/* dpad left */
	TZM_RIGHT,		/* dpad right */
	TZM_OK,			/* south */
	TZM_BACK,		/* east */
	TZ_ALL
} touch_zone_t;

typedef struct
{
	touch_rect_t zone[TZ_ALL];
	touch_rect_t safe;	/* the safe rectangle it was made from */
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
	int menu;	/* the menu mode: a menu, the console or a message is up */
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

/*
 * The mode: 0 is the game, 1 a menu, the console or a message. A
 * change lets every finger go.
 */
void Touch_SetMode(touch_t *t, int menu);

/* What the overlay draws: a rectangle, its label, whether it is held. */
typedef enum
{
	TD_BUTTON,	/* an outline, a label */
	TD_RING,	/* a stick's ring, no label */
	TD_KNOB		/* a stick's knob, a small filled square */
} touch_draw_kind_t;

typedef struct
{
	touch_rect_t r;
	const char *label;	/* NULL for a ring or a knob */
	int pressed;
	int kind;
} touch_draw_t;

#define TOUCH_MAX_DRAW 16

/*
 * The overlay of the mode now, at most max entries, the count back. With
 * a physical gamepad in use (physical not 0) or no layout, none. Every
 * rectangle lies inside the safe rectangle.
 */
int Touch_Drawables(const touch_t *t, int physical, touch_draw_t *out, int max);

/*
 * The largest whole scale, up to max_scale, at which the label (8 pixels
 * a character) fits in w_px pixels; 0 when it does not fit even once.
 */
int Touch_LabelScale(const char *label, float w_px, int max_scale);

/* The touch layer exists on this platform (iOS): elsewhere in_touch does nothing. */
int Touch_Supported(void);

/*
 * Whether a default gamepad binding is made: only on a start without a
 * config.cfg, and never over a key that is bound.
 */
int Touch_WantBind(int have_config, int bound);

/*
 * Whether the on-screen keyboard is wanted: typing in the console or a
 * chat message, or a menu with a text field under its cursor.
 */
int Touch_WantText(int console_or_message, int menu_open, int menu_field);

/*
 * What to do with text input: want is whether it is wanted, applied what
 * was last asked of the system (-1 nothing yet), active what the system
 * says now. 1 start it, 0 stop it, -1 leave it. A keyboard the system
 * hid while it is wanted is started again, at most TOUCH_TEXT_ASKS times
 * in a row (asks counts them, the caller keeps it between frames, a
 * change or a system that agrees sets it back to 0).
 */
#define TOUCH_TEXT_ASKS 3
int Touch_TextInput(int want, int applied, int active, int *asks);

/* The default gamepad bindings of iOS: entry i, or 0 past the end. */
int Touch_DefaultBind(int i, const char **key, const char **cmd);

#endif
