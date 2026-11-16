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
 * The touch layer: zones, the finger tracker and the gamepad output.
 * See header/touch.h.
 *
 * =======================================================================
 */

#include <math.h>
#include <string.h>

#include "header/touch.h"

/* Button zone to the gamepad button it presses; -1 for none. */
static const int zone_button[TZ_COUNT] = {
	[TZ_MOVE] = -1, [TZ_LOOK] = -1, [TZ_FIRE] = -1,
	[TZ_JUMP] = TOUCH_BTN_SOUTH,
	[TZ_CROUCH] = TOUCH_BTN_EAST,
	[TZ_NEXTWEAP] = TOUCH_BTN_NORTH,
	[TZ_USE] = TOUCH_BTN_WEST,
	[TZ_MENU] = TOUCH_BTN_START,
	[TZ_CONSOLE] = TOUCH_BTN_MISC1
};

static int
IsStick(int zone)
{
	return zone == TZ_MOVE || zone == TZ_LOOK;
}

static float
ClampScale(float scale)
{
	/* a NaN or a value out of range gives a usable scale */
	if (!(scale >= TOUCH_SCALE_MIN))
	{
		return scale > TOUCH_SCALE_MAX ? TOUCH_SCALE_MAX : TOUCH_SCALE_MIN;
	}

	return scale > TOUCH_SCALE_MAX ? TOUCH_SCALE_MAX : scale;
}

static touch_rect_t
Rect(float x, float y, float w, float h)
{
	touch_rect_t r;

	r.x = x;
	r.y = y;
	r.w = w > 0 ? w : 0;
	r.h = h > 0 ? h : 0;
	return r;
}

/*
 * The layout, for landscape: a strip at the top with the menu and the
 * console buttons; below it the move stick on the left half and the
 * look stick up to the buttons; two columns of buttons at the right
 * edge, the fire button at the bottom right.
 */
void
Touch_MakeLayout(touch_layout_t *l, touch_rect_t safe, float scale, int swap)
{
	float u, top, b, p, right, bottom, cx;
	int i;

	memset(l, 0, sizeof(*l));
	scale = ClampScale(scale);

	if (!(safe.w > 0) || !(safe.h > 0))
	{
		return;
	}

	u = safe.w < safe.h ? safe.w : safe.h;
	top = 0.1f * u * scale;			/* the strip of the small buttons */
	b = 0.11f * u * scale;			/* a button */
	p = 1.05f * b;				/* the pitch of the grid */
	right = safe.x + safe.w;
	bottom = safe.y + safe.h;
	cx = safe.x + safe.w * 0.5f;

	l->radius = 0.18f * u * scale;

	l->zone[TZ_MOVE] = Rect(safe.x, safe.y + top, safe.w * 0.5f, safe.h - top);
	l->zone[TZ_LOOK] = Rect(cx, safe.y + top, safe.w * 0.5f - 2.0f * p, safe.h - top);

	/* column 0 is the right-most, row 0 the bottom one */
	l->zone[TZ_FIRE] = Rect(right - b, bottom - b, b, b);
	l->zone[TZ_JUMP] = Rect(right - b, bottom - p - b, b, b);
	l->zone[TZ_USE] = Rect(right - b, bottom - 2.0f * p - b, b, b);
	l->zone[TZ_CROUCH] = Rect(right - p - b, bottom - b, b, b);
	l->zone[TZ_NEXTWEAP] = Rect(right - p - b, bottom - p - b, b, b);

	l->zone[TZ_MENU] = Rect(cx - top - 0.25f * top, safe.y, top, top);
	l->zone[TZ_CONSOLE] = Rect(cx + 0.25f * top, safe.y, top, top);

	if (swap)
	{
		for (i = 0; i < TZ_COUNT; i++)
		{
			l->zone[i].x = safe.x + safe.w - (l->zone[i].x - safe.x) - l->zone[i].w;
		}
	}
}

static int
Inside(touch_rect_t r, float x, float y)
{
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void
Touch_Reset(touch_t *t)
{
	memset(t->finger, 0, sizeof(t->finger));
}

void
Touch_SetLayout(touch_t *t, touch_rect_t safe, float scale, int swap)
{
	Touch_MakeLayout(&t->layout, safe, scale, swap);
}

static touch_finger_t *
Find(touch_t *t, uint64_t id)
{
	int i;

	for (i = 0; i < TOUCH_MAX_FINGERS; i++)
	{
		if (t->finger[i].active && t->finger[i].id == id)
		{
			return &t->finger[i];
		}
	}

	return NULL;
}

void
Touch_Up(touch_t *t, uint64_t id)
{
	touch_finger_t *f = Find(t, id);

	if (f)
	{
		memset(f, 0, sizeof(*f));
	}
}

void
Touch_Down(touch_t *t, uint64_t id, float x, float y)
{
	int i, zone = -1;
	touch_finger_t *f;

	/* a finger down again without a lift starts fresh */
	Touch_Up(t, id);

	/* the buttons first, then the sticks */
	for (i = TZ_FIRE; i < TZ_COUNT && zone < 0; i++)
	{
		if (Inside(t->layout.zone[i], x, y))
		{
			zone = i;
		}
	}

	for (i = TZ_MOVE; i <= TZ_LOOK && zone < 0; i++)
	{
		if (Inside(t->layout.zone[i], x, y))
		{
			zone = i;
		}
	}

	if (zone < 0)
	{
		return;
	}

	/* one finger to a zone */
	for (i = 0; i < TOUCH_MAX_FINGERS; i++)
	{
		if (t->finger[i].active && t->finger[i].zone == zone)
		{
			return;
		}
	}

	for (i = 0; i < TOUCH_MAX_FINGERS; i++)
	{
		if (!t->finger[i].active)
		{
			break;
		}
	}

	if (i == TOUCH_MAX_FINGERS)
	{
		return;
	}

	f = &t->finger[i];
	memset(f, 0, sizeof(*f));
	f->active = 1;
	f->id = id;
	f->zone = zone;
	f->ox = f->x = x;
	f->oy = f->y = y;
}

void
Touch_Motion(touch_t *t, uint64_t id, float x, float y)
{
	touch_finger_t *f = Find(t, id);

	if (!f)
	{
		return;
	}

	f->x = x;
	f->y = y;

	/* a button finger that left its button stays released until it lifts */
	if (!IsStick(f->zone) && !Inside(t->layout.zone[f->zone], x, y))
	{
		f->off = 1;
	}
}

static int
ToAxis(float v)
{
	int a;

	/* the range is symmetric, 32767 each way */
	if (!(v == v))
	{
		return 0;
	}

	if (v > 1.0f)
	{
		v = 1.0f;
	}

	if (v < -1.0f)
	{
		v = -1.0f;
	}

	a = (int)lroundf(v * (float)TOUCH_AXIS_MAX);

	if (a > TOUCH_AXIS_MAX)
	{
		a = TOUCH_AXIS_MAX;
	}

	if (a < TOUCH_AXIS_MIN)
	{
		a = TOUCH_AXIS_MIN;
	}

	return a;
}

void
Touch_Output(const touch_t *t, int *axes, uint32_t *buttons)
{
	int i;

	memset(axes, 0, TOUCH_NUM_AXES * sizeof(int));
	*buttons = 0;

	for (i = 0; i < TOUCH_MAX_FINGERS; i++)
	{
		const touch_finger_t *f = &t->finger[i];

		if (!f->active)
		{
			continue;
		}

		if (IsStick(f->zone))
		{
			float dx, dy, len, r = t->layout.radius;
			int base = (f->zone == TZ_MOVE) ? TOUCH_AXIS_LEFTX : TOUCH_AXIS_RIGHTX;

			if (!(r > 0))
			{
				continue;
			}

			dx = (f->x - f->ox) / r;
			dy = (f->y - f->oy) / r;
			len = sqrtf(dx * dx + dy * dy);

			/* the deflection is clamped to the radius, in its direction */
			if (len > 1.0f)
			{
				dx /= len;
				dy /= len;
			}

			axes[base] = ToAxis(dx);
			axes[base + 1] = ToAxis(dy);
		}
		else if (!f->off)
		{
			if (f->zone == TZ_FIRE)
			{
				axes[TOUCH_AXIS_RTRIG] = TOUCH_AXIS_MAX;
			}
			else if (zone_button[f->zone] >= 0 && zone_button[f->zone] < TOUCH_NUM_BUTTONS)
			{
				*buttons |= 1u << zone_button[f->zone];
			}
		}
	}
}

static const struct
{
	const char *key;
	const char *cmd;
} default_binds[] = {
	{ "TRIG_RIGHT", "+attack" },
	{ "BTN_SOUTH", "+moveup" },
	{ "BTN_EAST", "+movedown" },
	{ "BTN_NORTH", "weapnext" },
	{ "BTN_WEST", "invuse" },
	{ "SHOULDR_LEFT", "weapprev" },
	{ "SHOULDR_RIGHT", "weapnext" },
	{ "BTN_BACK", "inven" },
	{ "BTN_MISC1", "toggleconsole" }
};

int
Touch_DefaultBind(int i, const char **key, const char **cmd)
{
	if (i < 0 || i >= (int)(sizeof(default_binds) / sizeof(default_binds[0])))
	{
		return 0;
	}

	*key = default_binds[i].key;
	*cmd = default_binds[i].cmd;
	return 1;
}

/* TASK151 scaffolding: the overlay, the modes and the pure rules. */
void
Touch_SetMode(touch_t *t, int menu)
{
	(void)t;
	(void)menu;
}

int
Touch_Drawables(const touch_t *t, int physical, touch_draw_t *out, int max)
{
	(void)t;
	(void)physical;
	(void)out;
	(void)max;
	return 0;
}

int
Touch_LabelScale(const char *label, float w_px, int max_scale)
{
	(void)label;
	(void)w_px;
	(void)max_scale;
	return 0;
}

int
Touch_Supported(void)
{
	return 1;
}

int
Touch_WantBind(int have_config, int bound)
{
	(void)have_config;
	(void)bound;
	return 0;
}

int
Touch_WantText(int console_or_message, int menu_open, int menu_field)
{
	(void)console_or_message;
	(void)menu_open;
	(void)menu_field;
	return 0;
}

int
Touch_TextInput(int want, int applied, int active)
{
	(void)want;
	(void)applied;
	(void)active;
	return -1;
}
