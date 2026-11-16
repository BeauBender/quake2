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

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "header/touch.h"

/* Button zone to the gamepad button it presses; -1 for none. */
static const int zone_button[TZ_ALL] = {
	[TZ_MOVE] = -1, [TZ_LOOK] = -1, [TZ_FIRE] = -1,
	[TZ_JUMP] = TOUCH_BTN_SOUTH,
	[TZ_CROUCH] = TOUCH_BTN_EAST,
	[TZ_NEXTWEAP] = TOUCH_BTN_NORTH,
	[TZ_USE] = TOUCH_BTN_WEST,
	[TZ_MENU] = TOUCH_BTN_START,
	[TZ_CONSOLE] = TOUCH_BTN_MISC1,
	[TZM_UP] = TOUCH_BTN_DPAD_UP,
	[TZM_DOWN] = TOUCH_BTN_DPAD_DOWN,
	[TZM_LEFT] = TOUCH_BTN_DPAD_LEFT,
	[TZM_RIGHT] = TOUCH_BTN_DPAD_RIGHT,
	[TZM_OK] = TOUCH_BTN_SOUTH,
	[TZM_BACK] = TOUCH_BTN_EAST
};

/* A coordinate the layer can use: not a NaN, not infinite. */
static int
Usable(float x, float y)
{
	return isfinite(x) && isfinite(y);
}

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

	/*
	 * The menu mode: a d-pad in a cross at the bottom left, OK and Back
	 * in the corner at the bottom right (where the game's buttons are),
	 * the console button where it always is.
	 */
	l->zone[TZM_UP] = Rect(safe.x + p, bottom - 2.0f * p - b, b, b);
	l->zone[TZM_DOWN] = Rect(safe.x + p, bottom - b, b, b);
	l->zone[TZM_LEFT] = Rect(safe.x, bottom - p - b, b, b);
	l->zone[TZM_RIGHT] = Rect(safe.x + 2.0f * p, bottom - p - b, b, b);
	l->zone[TZM_OK] = Rect(right - b, bottom - p - b, b, b);
	l->zone[TZM_BACK] = Rect(right - p - b, bottom - b, b, b);
	l->safe = safe;

	if (swap)
	{
		for (i = 0; i < TZ_ALL; i++)
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

	/*
	 * No check of the position here: Inside() is false for a NaN and for
	 * an infinity (every comparison with a NaN is false, and an infinite x
	 * is not below the zone's edge), so such a finger finds no zone.
	 */
	if (t->menu)
	{
		/* the console button, then the pad: the zones after the game's */
		for (i = TZ_CONSOLE; i < TZ_ALL && zone < 0; i++)
		{
			if (Inside(t->layout.zone[i], x, y))
			{
				zone = i;
			}
		}
	}
	else
	{
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

	if (!f || !Usable(x, y))
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

	/*
	 * The range is symmetric, 32767 each way. v is clamped to it first. v
	 * is not always finite: a very small layout can overflow to an
	 * infinity, and inf / inf is a NaN. A NaN passes both comparisons, so
	 * the result is whatever lroundf gives, and the clamp of the integer
	 * below keeps it in the range either way.
	 */
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

void
Touch_SetMode(touch_t *t, int menu)
{
	menu = menu != 0;

	if (t->menu != menu)
	{
		Touch_Reset(t);
		t->menu = menu;
	}
}

static float
ClampF(float v, float lo, float hi)
{
	if (hi < lo)
	{
		return lo;
	}

	return v < lo ? lo : (v > hi ? hi : v);
}

/* A held finger in a zone, or NULL. */
static const touch_finger_t *
HeldIn(const touch_t *t, int zone)
{
	int i;

	for (i = 0; i < TOUCH_MAX_FINGERS; i++)
	{
		if (t->finger[i].active && t->finger[i].zone == zone)
		{
			return &t->finger[i];
		}
	}

	return NULL;
}

static int
AddDraw(touch_draw_t *out, int n, int max, touch_rect_t r, const char *label, int pressed, int kind)
{
	if (n < max)
	{
		out[n].r = r;
		out[n].label = label;
		out[n].pressed = pressed;
		out[n].kind = kind;
		n++;
	}

	return n;
}

/*
 * The ring of a stick: as wide as two radii, held to what the zone
 * can take; at rest in the middle of the zone, held where the finger
 * went down, moved inside the safe rectangle. A held stick has a knob
 * that follows the finger as far as the axis reads it.
 */
static int
AddStick(const touch_t *t, int zone, touch_draw_t *out, int n, int max)
{
	const touch_layout_t *l = &t->layout;
	const touch_rect_t z = l->zone[zone];
	const touch_finger_t *f = HeldIn(t, zone);
	float side = 2.0f * l->radius, x, y;
	touch_rect_t ring;

	if (side > z.w)
	{
		side = z.w;
	}

	if (side > z.h)
	{
		side = z.h;
	}

	if (f)
	{
		x = f->ox - side * 0.5f;
		y = f->oy - side * 0.5f;
	}
	else
	{
		x = z.x + (z.w - side) * 0.5f;
		y = z.y + (z.h - side) * 0.5f;
	}

	ring = Rect(ClampF(x, l->safe.x, l->safe.x + l->safe.w - side),
			ClampF(y, l->safe.y, l->safe.y + l->safe.h - side), side, side);
	n = AddDraw(out, n, max, ring, NULL, f != NULL, TD_RING);

	if (f && l->radius > 0)
	{
		const float k = side * 0.35f;
		const float reach = (side - k) * 0.5f;
		float dx = f->x - f->ox, dy = f->y - f->oy;
		const float len = sqrtf(dx * dx + dy * dy);
		touch_rect_t knob;

		if (len > l->radius)
		{
			dx *= l->radius / len;
			dy *= l->radius / len;
		}

		dx *= reach / l->radius;
		dy *= reach / l->radius;

		/* inside the ring, which is inside the safe rectangle */
		knob = Rect(ring.x + (side - k) * 0.5f + dx, ring.y + (side - k) * 0.5f + dy, k, k);
		n = AddDraw(out, n, max, knob, NULL, 1, TD_KNOB);
	}

	return n;
}

typedef struct
{
	int zone;
	const char *label;
} button_def_t;

static const button_def_t game_buttons[] = {
	{ TZ_FIRE, "Fire" }, { TZ_JUMP, "Jump" }, { TZ_USE, "Use" }, { TZ_CROUCH, "Duck" },
	{ TZ_NEXTWEAP, "Next" }, { TZ_MENU, "Menu" }, { TZ_CONSOLE, "Con" }
};

static const button_def_t menu_buttons[] = {
	{ TZM_UP, "^" }, { TZM_DOWN, "v" }, { TZM_LEFT, "<" }, { TZM_RIGHT, ">" },
	{ TZM_OK, "OK" }, { TZM_BACK, "Back" }, { TZ_CONSOLE, "Con" }
};

#define NUM_BUTTONS_SHOWN 7

int
Touch_Drawables(const touch_t *t, int physical, touch_draw_t *out, int max)
{
	const button_def_t *set = t->menu ? menu_buttons : game_buttons;
	int i, n = 0;

	if (physical || !out || !(t->layout.safe.w > 0))
	{
		return 0;
	}

	if (!t->menu)
	{
		n = AddStick(t, TZ_MOVE, out, n, max);
		n = AddStick(t, TZ_LOOK, out, n, max);
	}

	for (i = 0; i < NUM_BUTTONS_SHOWN; i++)
	{
		const touch_finger_t *f = HeldIn(t, set[i].zone);

		n = AddDraw(out, n, max, t->layout.zone[set[i].zone], set[i].label, f != NULL && !f->off, TD_BUTTON);
	}

	return n;
}

int
Touch_LabelScale(const char *label, float w_px, int max_scale)
{
	size_t len;
	int s = 0;

	if (!label || !label[0] || !(w_px > 0) || max_scale < 1)
	{
		return 0;
	}

	len = strlen(label);

	while (s < max_scale && (float)(len * 8 * (size_t)(s + 1)) <= w_px)
	{
		s++;
	}

	return s;
}

int
Touch_Supported(void)
{
#if defined(__APPLE__) && TARGET_OS_IPHONE && !TARGET_OS_MACCATALYST
	return 1;
#else
	return 0;
#endif
}

int
Touch_WantBind(int have_config, int bound)
{
	return !have_config && !bound;
}

int
Touch_WantText(int console_or_message, int menu_open, int menu_field)
{
	return console_or_message || (menu_open && menu_field);
}

int
Touch_TextInput(int want, int applied, int active, int *asks)
{
	want = want != 0;

	if (want != applied)
	{
		*asks = 0;
		return want;
	}

	if (!want || active)
	{
		/* the system agrees: the next stop is a new one */
		*asks = 0;
		return -1;
	}

	/*
	 * Asked for, and the system stopped it on its own (the hide key of an
	 * iPad), or never started it: ask again, but only a few times in a row,
	 * so a start that keeps failing, or a keyboard that is never shown
	 * (a hardware one), is not asked for on every frame.
	 */
	if (*asks < TOUCH_TEXT_ASKS)
	{
		(*asks)++;
		return 1;
	}

	return -1;
}
