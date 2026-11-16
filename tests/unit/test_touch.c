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
