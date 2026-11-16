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
 * The touch layer (TASK150, src/client/input/touch.c): fingers in, the
 * two sticks, the trigger and the buttons of a virtual gamepad out.
 *
 * - A finger down in a stick zone is that stick's origin; half the stick
 *   radius is half deflection, past the radius is full deflection and
 *   never outside -32768 to 32767; lifting returns the stick to 0.
 * - Fingers are followed by id, each bound to the zone it went down in;
 *   a finger that slides out of its stick zone keeps it, one that went
 *   down outside every zone does nothing, one zone takes one finger.
 * - A button presses on down inside it and releases on up or when the
 *   finger slides off it.
 * - Every zone lies inside the safe rectangle (an iPhone's and two
 *   iPads', at the scales 0.5, 1 and 2, mirrored or not), none overlaps
 *   another, and the mirror is exact.
 *
 * Not proven here: the SDL glue (the virtual joystick, the events), see
 * the TASK150 runbook.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/input/header/touch.h"

/* The safe rectangles of C2's cases: an iPhone 17 landscape (59 points of corner each side), two iPads. */
static const touch_rect_t screens[] = {
	{ 59, 0, 734, 372 },
	{ 0, 24, 1194, 810 },
	{ 0, 24, 1366, 1000 }
};
#define NSCREENS ((int)(sizeof(screens) / sizeof(screens[0])))

static const float scales[] = { 0.5f, 1.0f, 2.0f };
#define NSCALES ((int)(sizeof(scales) / sizeof(scales[0])))

static touch_t T;

static void
t_setup(touch_rect_t safe, float scale, int swap)
{
	memset(&T, 0, sizeof(T));
	Touch_SetLayout(&T, safe, scale, swap);
}

static float
cx(int zone)
{
	return T.layout.zone[zone].x + T.layout.zone[zone].w * 0.5f;
}

static float
cy(int zone)
{
	return T.layout.zone[zone].y + T.layout.zone[zone].h * 0.5f;
}

static void
t_out(int *axes, uint32_t *buttons)
{
	Touch_Output(&T, axes, buttons);
}

TEST(touch_half_radius_is_half_deflection_and_lift_returns_to_zero)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	float r;

	t_setup(screens[0], 1.0f, 0);
	r = T.layout.radius;
	T_CHECK(r > 0);

	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);

	Touch_Motion(&T, 1, cx(TZ_MOVE) + r * 0.5f, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] >= 16383 && axes[TOUCH_AXIS_LEFTX] <= 16384);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 0);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTY], 0);

	/* up is y negative */
	Touch_Motion(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE) - r * 0.5f);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
	T_CHECK(axes[TOUCH_AXIS_LEFTY] <= -16383 && axes[TOUCH_AXIS_LEFTY] >= -16384);

	Touch_Up(&T, 1);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 0);
	T_EQ_INT((int)btn, 0);
}

TEST(touch_past_the_radius_is_full_deflection_never_beyond_the_range)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	float r;

	t_setup(screens[0], 1.0f, 0);
	r = T.layout.radius;

	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 1, cx(TZ_MOVE) + r * 3.0f, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 0);

	Touch_Motion(&T, 1, cx(TZ_MOVE) - r * 3.0f, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] >= -32768);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] <= -32767);

	Touch_Motion(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE) + r * 1000.0f);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 32767);

	Touch_Motion(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE) - r * 1000.0f);
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTY] >= -32768 && axes[TOUCH_AXIS_LEFTY] <= -32767);

	/* a diagonal is clamped to the radius in its direction: the length stays within a full deflection */
	Touch_Motion(&T, 1, cx(TZ_MOVE) + r * 5.0f, cy(TZ_MOVE) + r * 5.0f);
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] > 0 && axes[TOUCH_AXIS_LEFTX] <= 32767);
	T_CHECK(axes[TOUCH_AXIS_LEFTY] > 0 && axes[TOUCH_AXIS_LEFTY] <= 32767);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], axes[TOUCH_AXIS_LEFTY]);
	T_CHECK((double)axes[TOUCH_AXIS_LEFTX] * axes[TOUCH_AXIS_LEFTX] * 2.0 <= 32768.0 * 32768.0);
}

TEST(touch_stick_radius_is_a_thumb_sized_share_of_the_short_side)
{
	int s;

	for (s = 0; s < NSCREENS; s++)
	{
		touch_layout_t l;
		const float u = screens[s].w < screens[s].h ? screens[s].w : screens[s].h;

		Touch_MakeLayout(&l, screens[s], 1.0f, 0);
		T_CHECK(l.radius >= 0.15f * u && l.radius <= 0.2f * u);
	}
}

TEST(touch_two_fingers_move_the_two_sticks_independently)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	float r;

	t_setup(screens[1], 1.0f, 0);
	r = T.layout.radius;

	Touch_Down(&T, 10, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Down(&T, 11, cx(TZ_LOOK), cy(TZ_LOOK));

	Touch_Motion(&T, 10, cx(TZ_MOVE) + r, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTY], 0);

	Touch_Motion(&T, 11, cx(TZ_LOOK), cy(TZ_LOOK) + r * 0.5f);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 0);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);
	T_CHECK(axes[TOUCH_AXIS_RIGHTY] >= 16383 && axes[TOUCH_AXIS_RIGHTY] <= 16384);

	/* lifting the first leaves the second */
	Touch_Up(&T, 10);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
	T_CHECK(axes[TOUCH_AXIS_RIGHTY] >= 16383);

	Touch_Up(&T, 11);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTY], 0);
}

TEST(touch_finger_sliding_out_of_its_zone_keeps_its_binding)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	float r;

	t_setup(screens[0], 1.0f, 0);
	r = T.layout.radius;

	/* the move stick's finger slides into the look zone, then off the screen */
	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 1, cx(TZ_LOOK), cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);

	Touch_Motion(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE) + r * 0.5f);
	Touch_Motion(&T, 1, -500.0f, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], -32767);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);

	/* and a second finger in the move zone is not given the stick meanwhile */
	Touch_Down(&T, 2, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 2, cx(TZ_MOVE), cy(TZ_MOVE) - r);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], -32767);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 0);
}

TEST(touch_finger_down_outside_every_zone_does_nothing)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	int i;

	t_setup(screens[0], 1.0f, 0);

	/* off the screen, the corner of the housing left of the safe rectangle, and the top strip's gap */
	Touch_Down(&T, 1, -10.0f, -10.0f);
	Touch_Down(&T, 2, screens[0].x - 5.0f, 100.0f);
	Touch_Down(&T, 3, screens[0].x + 1.0f, screens[0].y + 1.0f);

	for (i = 1; i <= 3; i++)
	{
		Touch_Motion(&T, i, 300.0f, 200.0f);
	}

	t_out(axes, &btn);

	for (i = 0; i < TOUCH_NUM_AXES; i++)
	{
		T_EQ_INT(axes[i], 0);
	}

	T_EQ_INT((int)btn, 0);

	/* it does not hold a zone either: a finger in the zone afterwards has the stick */
	Touch_Down(&T, 4, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 4, cx(TZ_MOVE) + T.layout.radius, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
}

TEST(touch_a_zone_takes_one_finger)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[0], 1.0f, 0);

	Touch_Down(&T, 1, cx(TZ_LOOK), cy(TZ_LOOK));
	Touch_Down(&T, 2, cx(TZ_LOOK) + 5.0f, cy(TZ_LOOK));
	Touch_Motion(&T, 2, cx(TZ_LOOK) + T.layout.radius, cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);

	/* lifting the first frees it */
	Touch_Up(&T, 1);
	Touch_Down(&T, 2, cx(TZ_LOOK), cy(TZ_LOOK));
	Touch_Motion(&T, 2, cx(TZ_LOOK) + T.layout.radius, cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 32767);
}

TEST(touch_up_for_an_unknown_finger_changes_nothing)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[0], 1.0f, 0);
	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 1, cx(TZ_MOVE) + T.layout.radius, cy(TZ_MOVE));

	/* a cancel and an up come as one call; an id never seen is ignored */
	Touch_Up(&T, 999);
	Touch_Motion(&T, 999, 0, 0);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);

	/* the cancel of the finger itself releases it like an up */
	Touch_Up(&T, 1);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
}

TEST(touch_reset_releases_everything)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[0], 1.0f, 0);
	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 1, cx(TZ_MOVE) + T.layout.radius, cy(TZ_MOVE));
	Touch_Down(&T, 2, cx(TZ_FIRE), cy(TZ_FIRE));
	Touch_Down(&T, 3, cx(TZ_JUMP), cy(TZ_JUMP));

	Touch_Reset(&T);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 0);
	T_EQ_INT((int)btn, 0);
}

TEST(touch_buttons_press_on_down_and_release_on_up)
{
	static const struct
	{
		int zone;
		int bit;
	} map[] = {
		{ TZ_JUMP, TOUCH_BTN_SOUTH },
		{ TZ_CROUCH, TOUCH_BTN_EAST },
		{ TZ_NEXTWEAP, TOUCH_BTN_NORTH },
		{ TZ_USE, TOUCH_BTN_WEST },
		{ TZ_MENU, TOUCH_BTN_START },
		{ TZ_CONSOLE, TOUCH_BTN_MISC1 }
	};
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	int i;

	t_setup(screens[1], 1.0f, 0);

	for (i = 0; i < (int)(sizeof(map) / sizeof(map[0])); i++)
	{
		Touch_Down(&T, 5, cx(map[i].zone), cy(map[i].zone));
		t_out(axes, &btn);
		T_EQ_INT((int)btn, 1 << map[i].bit);
		T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 0);

		Touch_Up(&T, 5);
		t_out(axes, &btn);
		T_EQ_INT((int)btn, 0);
	}

	/* fire is the right trigger, no button */
	Touch_Down(&T, 6, cx(TZ_FIRE), cy(TZ_FIRE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_LTRIG], 0);
	T_EQ_INT((int)btn, 0);
	Touch_Up(&T, 6);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 0);
}

TEST(touch_button_releases_when_the_finger_slides_off_it)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[1], 1.0f, 0);

	Touch_Down(&T, 1, cx(TZ_JUMP), cy(TZ_JUMP));
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 1 << TOUCH_BTN_SOUTH);

	Touch_Motion(&T, 1, cx(TZ_JUMP) + 1.0f, cy(TZ_JUMP) + 1.0f);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 1 << TOUCH_BTN_SOUTH);

	/* off it, onto the look stick's zone: released, and it does not turn the view */
	Touch_Motion(&T, 1, cx(TZ_LOOK), cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);

	/* back on it, still released: a slide off is final until the lift */
	Touch_Motion(&T, 1, cx(TZ_JUMP), cy(TZ_JUMP));
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);

	Touch_Up(&T, 1);
	Touch_Down(&T, 1, cx(TZ_JUMP), cy(TZ_JUMP));
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 1 << TOUCH_BTN_SOUTH);

	/* the fire button likewise */
	Touch_Up(&T, 1);
	Touch_Down(&T, 2, cx(TZ_FIRE), cy(TZ_FIRE));
	Touch_Motion(&T, 2, cx(TZ_FIRE) - 1000.0f, cy(TZ_FIRE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 0);
}

TEST(touch_two_buttons_at_once)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[0], 1.0f, 0);

	Touch_Down(&T, 1, cx(TZ_FIRE), cy(TZ_FIRE));
	Touch_Down(&T, 2, cx(TZ_JUMP), cy(TZ_JUMP));
	Touch_Down(&T, 3, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 3, cx(TZ_MOVE), cy(TZ_MOVE) + T.layout.radius);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 32767);
	T_EQ_INT((int)btn, 1 << TOUCH_BTN_SOUTH);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 32767);
}

TEST(touch_finger_ids_reused_after_a_lift_start_fresh)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[0], 1.0f, 0);

	Touch_Down(&T, 7, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 7, cx(TZ_MOVE) + T.layout.radius, cy(TZ_MOVE));
	Touch_Up(&T, 7);

	/* the same id in the look zone: no trace of the move stick, no deflection yet */
	Touch_Down(&T, 7, cx(TZ_LOOK), cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);

	Touch_Motion(&T, 7, cx(TZ_LOOK) + T.layout.radius * 0.5f, cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
	T_CHECK(axes[TOUCH_AXIS_RIGHTX] >= 16383 && axes[TOUCH_AXIS_RIGHTX] <= 16384);

	/* a down for an id still followed (a lift that never came) starts over from where it is */
	Touch_Down(&T, 7, cx(TZ_LOOK) + 3.0f, cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);

	/* an id of 64 bits */
	Touch_Down(&T, 0xFFFFFFFFFFFFFFFFULL, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 0xFFFFFFFFFFFFFFFFULL, cx(TZ_MOVE) + T.layout.radius, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
	Touch_Up(&T, 0xFFFFFFFFFFFFFFFFULL);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
}

static int
t_inside(touch_rect_t r, touch_rect_t s)
{
	const float e = 0.001f;

	return r.x >= s.x - e && r.y >= s.y - e &&
		r.x + r.w <= s.x + s.w + e && r.y + r.h <= s.y + s.h + e;
}

static int
t_overlap(touch_rect_t a, touch_rect_t b)
{
	const float e = 0.001f;

	return a.x < b.x + b.w - e && b.x < a.x + a.w - e &&
		a.y < b.y + b.h - e && b.y < a.y + a.h - e;
}

TEST(touch_zones_lie_in_the_safe_rectangle_and_do_not_overlap)
{
	int s, k, swap, i, j;

	for (s = 0; s < NSCREENS; s++)
	{
		for (k = 0; k < NSCALES; k++)
		{
			for (swap = 0; swap < 2; swap++)
			{
				touch_layout_t l;

				Touch_MakeLayout(&l, screens[s], scales[k], swap);
				T_CHECK(l.radius > 0);

				for (i = 0; i < TZ_COUNT; i++)
				{
					/* every zone has an area */
					T_CHECK(l.zone[i].w > 0 && l.zone[i].h > 0);
					T_CHECK(t_inside(l.zone[i], screens[s]));

					for (j = i + 1; j < TZ_COUNT; j++)
					{
						T_CHECK(!t_overlap(l.zone[i], l.zone[j]));
					}
				}
			}
		}
	}
}

TEST(touch_swap_mirrors_every_zone)
{
	int s, k, i;

	for (s = 0; s < NSCREENS; s++)
	{
		for (k = 0; k < NSCALES; k++)
		{
			touch_layout_t a, b;

			Touch_MakeLayout(&a, screens[s], scales[k], 0);
			Touch_MakeLayout(&b, screens[s], scales[k], 1);

			T_EQ_FLT(a.radius, b.radius);

			for (i = 0; i < TZ_COUNT; i++)
			{
				const float mx = screens[s].x + screens[s].w - (a.zone[i].x - screens[s].x) - a.zone[i].w;

				T_EQ_FLT(b.zone[i].x, mx);
				T_EQ_FLT(b.zone[i].y, a.zone[i].y);
				T_EQ_FLT(b.zone[i].w, a.zone[i].w);
				T_EQ_FLT(b.zone[i].h, a.zone[i].h);
			}

			/* the move stick is on the left unmirrored, and on the right mirrored */
			T_CHECK(a.zone[TZ_MOVE].x < a.zone[TZ_LOOK].x);
			T_CHECK(b.zone[TZ_MOVE].x > b.zone[TZ_LOOK].x);
		}
	}
}

TEST(touch_swap_moves_the_move_stick_to_the_right_half)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[0], 1.0f, 1);
	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	T_CHECK(cx(TZ_MOVE) > screens[0].x + screens[0].w * 0.5f);
	Touch_Motion(&T, 1, cx(TZ_MOVE) + T.layout.radius, cy(TZ_MOVE));
	t_out(axes, &btn);

	/* still the left axes: only the place of the zone moved */
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);
}

TEST(touch_scale_is_held_to_its_range)
{
	touch_layout_t lo, hi, a, b;
	const float nan = 0.0f / 0.0f;
	int i;

	Touch_MakeLayout(&lo, screens[0], 0.5f, 0);
	Touch_MakeLayout(&hi, screens[0], 2.0f, 0);

	Touch_MakeLayout(&a, screens[0], 0.0f, 0);
	Touch_MakeLayout(&b, screens[0], -3.0f, 0);
	T_EQ_FLT(a.radius, lo.radius);
	T_EQ_FLT(b.radius, lo.radius);

	Touch_MakeLayout(&a, screens[0], 9.0f, 0);
	T_EQ_FLT(a.radius, hi.radius);

	Touch_MakeLayout(&a, screens[0], nan, 0);
	T_CHECK(a.radius == a.radius && a.radius > 0);

	for (i = 0; i < TZ_COUNT; i++)
	{
		T_CHECK(a.zone[i].w == a.zone[i].w);
	}

	/* the size follows the scale */
	T_CHECK(hi.radius > lo.radius * 3.9f);
	T_CHECK(hi.zone[TZ_FIRE].w > lo.zone[TZ_FIRE].w * 3.9f);
}

TEST(touch_an_empty_safe_rectangle_gives_no_zone)
{
	static const touch_rect_t bad[] = {
		{ 0, 0, 0, 0 }, { 0, 0, -5, 100 }, { 0, 0, 100, 0 }
	};
	int b, i;

	for (b = 0; b < (int)(sizeof(bad) / sizeof(bad[0])); b++)
	{
		touch_layout_t l;
		int axes[TOUCH_NUM_AXES];
		uint32_t btn;

		Touch_MakeLayout(&l, bad[b], 1.0f, 0);

		for (i = 0; i < TZ_COUNT; i++)
		{
			T_CHECK(l.zone[i].w == 0 && l.zone[i].h == 0);
		}

		/* a finger finds nothing to hold */
		memset(&T, 0, sizeof(T));
		Touch_SetLayout(&T, bad[b], 1.0f, 0);
		Touch_Down(&T, 1, 10, 10);
		Touch_Motion(&T, 1, 50, 50);
		t_out(axes, &btn);

		for (i = 0; i < TOUCH_NUM_AXES; i++)
		{
			T_EQ_INT(axes[i], 0);
		}

		T_EQ_INT((int)btn, 0);
	}
}

TEST(touch_default_bindings_are_the_gamepad_set)
{
	static const char *const want[][2] = {
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
	const char *key, *cmd;
	int i, n = (int)(sizeof(want) / sizeof(want[0]));

	for (i = 0; i < n; i++)
	{
		key = cmd = NULL;
		T_REQUIRE(Touch_DefaultBind(i, &key, &cmd));
		T_EQ_STR(key, want[i][0]);
		T_EQ_STR(cmd, want[i][1]);
	}

	/* past the end, and before it: none, and the outputs untouched */
	key = cmd = NULL;
	T_CHECK(!Touch_DefaultBind(n, &key, &cmd));
	T_CHECK(!Touch_DefaultBind(-1, &key, &cmd));
	T_CHECK(key == NULL && cmd == NULL);
}

/*
 * ---- TASK151: the overlay, the menu mode, and the rules moved out of the iOS glue ----
 */

/* The pixels per point of the cases: an iPhone at 3, the iPads at 2. */
static const float ppps[] = { 3.0f, 2.0f, 2.0f };

static const int menu_zones[] = { TZM_UP, TZM_DOWN, TZM_LEFT, TZM_RIGHT, TZM_OK, TZM_BACK, TZ_CONSOLE };
#define NMENUZ ((int)(sizeof(menu_zones) / sizeof(menu_zones[0])))

TEST(touch_menu_zones_lie_in_the_safe_rectangle_and_do_not_overlap)
{
	int s, k, swap, i, j;

	for (s = 0; s < NSCREENS; s++)
	{
		for (k = 0; k < NSCALES; k++)
		{
			for (swap = 0; swap < 2; swap++)
			{
				touch_layout_t l;

				Touch_MakeLayout(&l, screens[s], scales[k], swap);

				for (i = 0; i < NMENUZ; i++)
				{
					const touch_rect_t a = l.zone[menu_zones[i]];

					T_CHECK(a.w > 0 && a.h > 0);
					T_CHECK(t_inside(a, screens[s]));

					for (j = i + 1; j < NMENUZ; j++)
					{
						T_CHECK(!t_overlap(a, l.zone[menu_zones[j]]));
					}
				}
			}
		}
	}
}

TEST(touch_menu_pad_presses_the_dpad_south_and_east)
{
	static const struct
	{
		int zone;
		uint32_t bit;
	} want[] = {
		{ TZM_UP, TOUCH_BTN_DPAD_UP }, { TZM_DOWN, TOUCH_BTN_DPAD_DOWN },
		{ TZM_LEFT, TOUCH_BTN_DPAD_LEFT }, { TZM_RIGHT, TOUCH_BTN_DPAD_RIGHT },
		{ TZM_OK, TOUCH_BTN_SOUTH }, { TZM_BACK, TOUCH_BTN_EAST },
		{ TZ_CONSOLE, TOUCH_BTN_MISC1 }
	};
	int i, s, swap;

	for (s = 0; s < NSCREENS; s++)
	{
		for (swap = 0; swap < 2; swap++)
		{
			for (i = 0; i < (int)(sizeof(want) / sizeof(want[0])); i++)
			{
				int axes[TOUCH_NUM_AXES];
				uint32_t btn;

				t_setup(screens[s], 1.0f, swap);
				Touch_SetMode(&T, 1);
				Touch_Down(&T, 1, cx(want[i].zone), cy(want[i].zone));
				t_out(axes, &btn);
				T_EQ_INT((int)btn, (int)(1u << want[i].bit));
				T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
				T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 0);

				Touch_Up(&T, 1);
				t_out(axes, &btn);
				T_EQ_INT((int)btn, 0);
			}
		}
	}

	/* the bits are SDL's d-pad buttons */
	T_EQ_INT(TOUCH_BTN_DPAD_UP, 11);
	T_EQ_INT(TOUCH_BTN_DPAD_DOWN, 12);
	T_EQ_INT(TOUCH_BTN_DPAD_LEFT, 13);
	T_EQ_INT(TOUCH_BTN_DPAD_RIGHT, 14);
}

TEST(touch_each_mode_ignores_the_other_modes_zones)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	int i;

	/* the menu mode: the game's stick and buttons do nothing */
	t_setup(screens[0], 1.0f, 0);
	Touch_SetMode(&T, 1);
	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Down(&T, 2, cx(TZ_FIRE), cy(TZ_FIRE));
	Touch_Down(&T, 3, cx(TZ_NEXTWEAP), cy(TZ_NEXTWEAP));
	Touch_Down(&T, 4, cx(TZ_MENU), cy(TZ_MENU));
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);

	for (i = 0; i < TOUCH_NUM_AXES; i++)
	{
		T_EQ_INT(axes[i], 0);
	}

	/* the game mode: the d-pad does nothing (OK and Back lie over game buttons, which do their own) */
	t_setup(screens[0], 1.0f, 0);
	Touch_Down(&T, 1, cx(TZM_UP), cy(TZM_UP));
	Touch_Down(&T, 2, cx(TZM_DOWN), cy(TZM_DOWN));
	Touch_Down(&T, 3, cx(TZM_LEFT), cy(TZM_LEFT));
	Touch_Down(&T, 4, cx(TZM_RIGHT), cy(TZM_RIGHT));
	t_out(axes, &btn);
	T_EQ_INT((int)(btn & ((1u << TOUCH_BTN_DPAD_UP) | (1u << TOUCH_BTN_DPAD_DOWN) |
			(1u << TOUCH_BTN_DPAD_LEFT) | (1u << TOUCH_BTN_DPAD_RIGHT))), 0);

	/* the console button works in both */
	t_setup(screens[0], 1.0f, 0);
	Touch_Down(&T, 1, cx(TZ_CONSOLE), cy(TZ_CONSOLE));
	t_out(axes, &btn);
	T_EQ_INT((int)btn, (int)(1u << TOUCH_BTN_MISC1));
}

TEST(touch_a_change_of_mode_lets_every_finger_go)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;

	t_setup(screens[0], 1.0f, 0);
	Touch_Down(&T, 1, cx(TZ_FIRE), cy(TZ_FIRE));
	Touch_Down(&T, 2, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 2, cx(TZ_MOVE) + T.layout.radius, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], TOUCH_AXIS_MAX);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);

	/* the same mode again changes nothing */
	Touch_SetMode(&T, 0);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], TOUCH_AXIS_MAX);

	Touch_SetMode(&T, 1);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], 0);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);
	T_EQ_INT((int)btn, 0);

	/* and back: a held pad button is let go as well */
	Touch_Down(&T, 3, cx(TZM_OK), cy(TZM_OK));
	t_out(axes, &btn);
	T_EQ_INT((int)btn, (int)(1u << TOUCH_BTN_SOUTH));
	Touch_SetMode(&T, 0);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);

	/* any non-zero is the menu mode */
	Touch_SetMode(&T, 5);
	T_EQ_INT(T.menu, 1);
}

static int
t_find(const touch_draw_t *d, int n, touch_rect_t r)
{
	int i;

	for (i = 0; i < n; i++)
	{
		if (d[i].kind == TD_BUTTON && d[i].r.x == r.x && d[i].r.y == r.y && d[i].r.w == r.w && d[i].r.h == r.h)
		{
			return i;
		}
	}

	return -1;
}

TEST(touch_drawables_at_rest_lie_in_the_safe_rectangle_and_do_not_overlap)
{
	int s, k, swap, mode, i, j;

	for (s = 0; s < NSCREENS; s++)
	{
		for (k = 0; k < NSCALES; k++)
		{
			for (swap = 0; swap < 2; swap++)
			{
				for (mode = 0; mode < 2; mode++)
				{
					touch_draw_t d[TOUCH_MAX_DRAW];
					int n;

					t_setup(screens[s], scales[k], swap);
					Touch_SetMode(&T, mode);
					n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);

					/* the game: seven buttons and two rings; a menu: the pad and the console button */
					T_EQ_INT(n, mode ? 7 : 9);

					for (i = 0; i < n; i++)
					{
						T_CHECK(d[i].r.w > 0 && d[i].r.h > 0);
						T_CHECK(t_inside(d[i].r, screens[s]));
						T_CHECK(!d[i].pressed);

						for (j = i + 1; j < n; j++)
						{
							T_CHECK(!t_overlap(d[i].r, d[j].r));
						}
					}
				}
			}
		}
	}
}

TEST(touch_drawables_are_bounded_by_the_room_given)
{
	touch_draw_t d[TOUCH_MAX_DRAW];

	t_setup(screens[0], 1.0f, 0);
	T_EQ_INT(Touch_Drawables(&T, 0, d, 3), 3);
	T_EQ_INT(Touch_Drawables(&T, 0, d, 0), 0);
	T_EQ_INT(Touch_Drawables(&T, 0, d, -4), 0);
	T_EQ_INT(Touch_Drawables(&T, 0, NULL, 5), 0);

	/* two sticks held: still within the largest list */
	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Down(&T, 2, cx(TZ_LOOK), cy(TZ_LOOK));
	T_CHECK(Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW) <= TOUCH_MAX_DRAW);
}

TEST(touch_a_pressed_button_is_marked_and_released_after_the_finger_lifts)
{
	touch_draw_t d[TOUCH_MAX_DRAW];
	int n, i, j, mode;
	static const int game_btn[] = { TZ_FIRE, TZ_JUMP, TZ_CROUCH, TZ_NEXTWEAP, TZ_USE, TZ_MENU, TZ_CONSOLE };

	for (mode = 0; mode < 2; mode++)
	{
		const int *zs = mode ? menu_zones : game_btn;

		for (i = 0; i < 7; i++)
		{
			int at;

			t_setup(screens[0], 1.0f, 0);
			Touch_SetMode(&T, mode);
			Touch_Down(&T, 1, cx(zs[i]), cy(zs[i]));
			n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);
			at = t_find(d, n, T.layout.zone[zs[i]]);
			T_REQUIRE(at >= 0);

			for (j = 0; j < n; j++)
			{
				T_EQ_INT(d[j].pressed, j == at);
			}

			/* a finger that slid off its button: the button is released */
			Touch_Motion(&T, 1, T.layout.safe.x - 5.0f, T.layout.safe.y - 5.0f);
			n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);

			for (j = 0; j < n; j++)
			{
				T_CHECK(!d[j].pressed);
			}

			Touch_Up(&T, 1);
			n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);

			for (j = 0; j < n; j++)
			{
				T_CHECK(!d[j].pressed);
			}
		}
	}
}

TEST(touch_a_held_stick_draws_its_ring_where_the_finger_went_down_and_a_knob)
{
	touch_draw_t d[TOUCH_MAX_DRAW];
	int s, n, i, rings, knobs, held;

	for (s = 0; s < NSCREENS; s++)
	{
		float ox, oy, r;

		t_setup(screens[s], 1.0f, 0);
		r = T.layout.radius;
		ox = cx(TZ_MOVE) - 10.0f;
		oy = cy(TZ_MOVE) + 5.0f;
		Touch_Down(&T, 1, ox, oy);
		Touch_Motion(&T, 1, ox + r * 0.5f, oy);

		n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);
		rings = knobs = held = 0;

		for (i = 0; i < n; i++)
		{
			T_CHECK(t_inside(d[i].r, screens[s]));

			if (d[i].kind == TD_RING)
			{
				rings++;

				if (d[i].pressed)
				{
					held++;

					/* centred on where the finger went down */
					T_CHECK(fabsf(d[i].r.x + d[i].r.w * 0.5f - ox) < 0.01f);
					T_CHECK(fabsf(d[i].r.y + d[i].r.h * 0.5f - oy) < 0.01f);
				}
			}
			else if (d[i].kind == TD_KNOB)
			{
				knobs++;
				T_CHECK(d[i].pressed);

				/* to the right of the ring's centre */
				T_CHECK(d[i].r.x + d[i].r.w * 0.5f > ox + 1.0f);
			}
		}

		T_EQ_INT(rings, 2);	/* the other stick rests */
		T_EQ_INT(held, 1);
		T_EQ_INT(knobs, 1);

		Touch_Up(&T, 1);
		n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);
		T_EQ_INT(n, 9);

		for (i = 0; i < n; i++)
		{
			T_CHECK(!d[i].pressed && d[i].kind != TD_KNOB);
		}
	}
}

TEST(touch_a_ring_held_at_the_edge_stays_inside_the_safe_rectangle)
{
	touch_draw_t d[TOUCH_MAX_DRAW];
	int s, n, i;

	for (s = 0; s < NSCREENS; s++)
	{
		t_setup(screens[s], 2.0f, 0);
		Touch_Down(&T, 1, T.layout.zone[TZ_MOVE].x, T.layout.zone[TZ_MOVE].y);
		Touch_Motion(&T, 1, T.layout.zone[TZ_MOVE].x - 500.0f, T.layout.zone[TZ_MOVE].y - 500.0f);
		n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);
		T_CHECK(n > 0);

		for (i = 0; i < n; i++)
		{
			T_CHECK(t_inside(d[i].r, screens[s]));
		}
	}
}

TEST(touch_a_physical_gamepad_in_use_empties_the_list_in_both_modes)
{
	touch_draw_t d[TOUCH_MAX_DRAW];
	int mode;

	for (mode = 0; mode < 2; mode++)
	{
		t_setup(screens[0], 1.0f, 0);
		Touch_SetMode(&T, mode);
		T_CHECK(Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW) > 0);
		T_EQ_INT(Touch_Drawables(&T, 1, d, TOUCH_MAX_DRAW), 0);
		T_EQ_INT(Touch_Drawables(&T, -1, d, TOUCH_MAX_DRAW), 0);
	}
}

TEST(touch_without_a_layout_nothing_is_drawn)
{
	touch_draw_t d[TOUCH_MAX_DRAW];
	touch_rect_t none = { 0, 0, 0, 0 };

	t_setup(none, 1.0f, 0);
	T_EQ_INT(Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW), 0);
	Touch_SetMode(&T, 1);
	T_EQ_INT(Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW), 0);
}

TEST(touch_every_label_fits_its_rectangle_at_the_smallest_scale)
{
	int s, mode, n, i;

	for (s = 0; s < NSCREENS; s++)
	{
		for (mode = 0; mode < 2; mode++)
		{
			touch_draw_t d[TOUCH_MAX_DRAW];

			t_setup(screens[s], 0.5f, 0);
			Touch_SetMode(&T, mode);
			n = Touch_Drawables(&T, 0, d, TOUCH_MAX_DRAW);

			for (i = 0; i < n; i++)
			{
				if (d[i].kind == TD_BUTTON)
				{
					const float w_px = d[i].r.w * ppps[s];
					int sc;

					T_REQUIRE(d[i].label != NULL && d[i].label[0]);
					sc = Touch_LabelScale(d[i].label, w_px, 4);
					T_CHECK(sc >= 1);
					T_CHECK((float)strlen(d[i].label) * 8.0f * (float)sc <= w_px);
				}
				else
				{
					T_CHECK(d[i].label == NULL);
				}
			}
		}
	}
}

TEST(touch_label_scale_is_the_largest_whole_scale_that_fits)
{
	T_EQ_INT(Touch_LabelScale("OK", 16.0f, 4), 1);
	T_EQ_INT(Touch_LabelScale("OK", 15.9f, 4), 0);
	T_EQ_INT(Touch_LabelScale("OK", 32.0f, 4), 2);
	T_EQ_INT(Touch_LabelScale("OK", 63.9f, 4), 3);
	T_EQ_INT(Touch_LabelScale("OK", 1000.0f, 4), 4);
	T_EQ_INT(Touch_LabelScale("OK", 1000.0f, 1), 1);
	T_EQ_INT(Touch_LabelScale("OK", 1000.0f, 0), 0);
	T_EQ_INT(Touch_LabelScale("Back", 32.0f, 4), 1);
	T_EQ_INT(Touch_LabelScale("Back", 31.0f, 4), 0);
	T_EQ_INT(Touch_LabelScale("", 100.0f, 4), 0);
	T_EQ_INT(Touch_LabelScale(NULL, 100.0f, 4), 0);
	T_EQ_INT(Touch_LabelScale("OK", -5.0f, 4), 0);
	T_EQ_INT(Touch_LabelScale("OK", 0.0f / 0.0f, 4), 0);
}

TEST(touch_a_finger_on_the_split_between_the_sticks_is_the_look_stick)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	float split;

	t_setup(screens[0], 1.0f, 0);
	split = T.layout.zone[TZ_LOOK].x;
	T_EQ_FLT(split, T.layout.zone[TZ_MOVE].x + T.layout.zone[TZ_MOVE].w);

	Touch_Down(&T, 1, split, cy(TZ_LOOK));
	Touch_Motion(&T, 1, split + T.layout.radius, cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 0);

	/* one step to the left is the move stick */
	Touch_Up(&T, 1);
	Touch_Down(&T, 2, split - 0.01f, cy(TZ_LOOK));
	Touch_Motion(&T, 2, split - 0.01f + T.layout.radius, cy(TZ_LOOK));
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTX], 32767);
	T_EQ_INT(axes[TOUCH_AXIS_RIGHTX], 0);
}

TEST(touch_a_rectangle_takes_its_top_left_edge_and_not_its_far_edge)
{
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	touch_rect_t c;

	t_setup(screens[0], 1.0f, 0);
	c = T.layout.zone[TZ_CROUCH];

	/* the top left corner is in */
	Touch_Down(&T, 1, c.x, c.y);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, (int)(1u << TOUCH_BTN_EAST));
	Touch_Up(&T, 1);

	/* the far edges are out: right of the column, and below the row */
	Touch_Down(&T, 1, c.x + c.w, c.y + c.h * 0.5f);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);
	Touch_Up(&T, 1);

	Touch_Down(&T, 1, c.x + c.w * 0.5f, c.y + c.h);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);
	Touch_Up(&T, 1);

	/* just inside the far corner is in */
	Touch_Down(&T, 1, c.x + c.w - 0.01f, c.y + c.h - 0.01f);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, (int)(1u << TOUCH_BTN_EAST));
	Touch_Up(&T, 1);

	/* just outside the near edges is out */
	Touch_Down(&T, 1, c.x - 0.01f, c.y + c.h * 0.5f);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);
	Touch_Up(&T, 1);

	Touch_Down(&T, 1, c.x + c.w * 0.5f, c.y - 0.01f);
	t_out(axes, &btn);
	T_EQ_INT((int)btn, 0);
}

TEST(touch_a_position_that_is_not_a_number_is_ignored)
{
	const float nan = 0.0f / 0.0f;
	const float inf = 1.0f / 0.0f;
	int axes[TOUCH_NUM_AXES];
	uint32_t btn;
	int i;

	t_setup(screens[0], 1.0f, 0);

	/* down at a bad position starts nothing, even if the other coordinate is in a zone */
	Touch_Down(&T, 1, nan, cy(TZ_MOVE));
	Touch_Down(&T, 2, cx(TZ_MOVE), nan);
	Touch_Down(&T, 3, inf, cy(TZ_MOVE));
	Touch_Down(&T, 4, cx(TZ_MOVE), -inf);

	for (i = 0; i < TOUCH_MAX_FINGERS; i++)
	{
		T_EQ_INT(T.finger[i].active, 0);
	}

	/* a bad motion leaves the finger where it was */
	Touch_Down(&T, 1, cx(TZ_MOVE), cy(TZ_MOVE));
	Touch_Motion(&T, 1, cx(TZ_MOVE) + T.layout.radius * 0.5f, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] >= 16383 && axes[TOUCH_AXIS_LEFTX] <= 16384);

	Touch_Motion(&T, 1, nan, cy(TZ_MOVE));
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] >= 16383 && axes[TOUCH_AXIS_LEFTX] <= 16384);

	Touch_Motion(&T, 1, cx(TZ_MOVE), inf);
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] >= 16383 && axes[TOUCH_AXIS_LEFTX] <= 16384);
	T_EQ_INT(axes[TOUCH_AXIS_LEFTY], 0);

	Touch_Motion(&T, 1, -inf, nan);
	t_out(axes, &btn);
	T_CHECK(axes[TOUCH_AXIS_LEFTX] >= 16383 && axes[TOUCH_AXIS_LEFTY] == 0);

	/* a button finger is not let off by a bad motion */
	Touch_Down(&T, 5, cx(TZ_FIRE), cy(TZ_FIRE));
	Touch_Motion(&T, 5, nan, nan);
	t_out(axes, &btn);
	T_EQ_INT(axes[TOUCH_AXIS_RTRIG], TOUCH_AXIS_MAX);
}

TEST(touch_default_binds_are_made_only_without_a_config_and_over_no_binding)
{
	T_EQ_INT(Touch_WantBind(0, 0), 1);
	T_EQ_INT(Touch_WantBind(1, 0), 0);
	T_EQ_INT(Touch_WantBind(0, 1), 0);
	T_EQ_INT(Touch_WantBind(1, 1), 0);
	T_EQ_INT(Touch_WantBind(0, 7), 0);
	T_EQ_INT(Touch_WantBind(5, 0), 0);
}

TEST(touch_the_keyboard_is_wanted_in_the_console_a_message_or_a_menu_text_field)
{
	T_EQ_INT(Touch_WantText(0, 0, 0), 0);
	T_EQ_INT(Touch_WantText(1, 0, 0), 1);
	T_EQ_INT(Touch_WantText(0, 1, 1), 1);
	T_EQ_INT(Touch_WantText(0, 1, 0), 0);
	/* a field under a menu that is not up is not typed into */
	T_EQ_INT(Touch_WantText(0, 0, 1), 0);
	T_EQ_INT(Touch_WantText(1, 1, 1), 1);
	T_EQ_INT(Touch_WantText(1, 1, 0), 1);
}

TEST(touch_text_input_follows_the_want_and_is_asked_again_when_the_system_hid_it)
{
	/* nothing asked yet */
	T_EQ_INT(Touch_TextInput(0, -1, 0), 0);
	T_EQ_INT(Touch_TextInput(1, -1, 0), 1);
	T_EQ_INT(Touch_TextInput(1, -1, 1), 1);

	/* a change */
	T_EQ_INT(Touch_TextInput(1, 0, 0), 1);
	T_EQ_INT(Touch_TextInput(0, 1, 1), 0);

	/* no change, and the system agrees */
	T_EQ_INT(Touch_TextInput(1, 1, 1), -1);
	T_EQ_INT(Touch_TextInput(0, 0, 0), -1);

	/* wanted, asked, but the system stopped it (the iPad hide key): again */
	T_EQ_INT(Touch_TextInput(1, 1, 0), 1);

	/* not wanted: a system that keeps it on is left alone, it is not ours to stop every frame */
	T_EQ_INT(Touch_TextInput(0, 0, 1), -1);
}

TEST(touch_is_not_supported_off_ios)
{
	/* the unit suite is built for the host, never for iOS */
	T_EQ_INT(Touch_Supported(), 0);
}
