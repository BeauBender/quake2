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
 * The engine side of ref_metal's MetalFX scalers (TASK111; header/
 * rm_fxscene.h). The cvars are read when they change: a mode the device
 * lacks is written back as the best one below it, with a line. Only the
 * world view drives the scaler's history and its reset: a view without
 * the world (the player-setup model) takes the forward path and touches
 * nothing here, and a change of fov is not a reset (the motion vectors
 * carry it).
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/local.h"
#include "header/rm_fxscene.h"

/* a metal_stats line every this many scaled views, and on each that resets */
#define RM_FX_STATS_EVERY 60

static cvar_t *r_metalfx;
static cvar_t *r_metalfx_scale;
static cvar_t *r_metalfx_interp;
static cvar_t *r_metalfx_modes;

static unsigned fx_has;         /* RM_FX_HAS bits of the device */
static unsigned fx_families;
static int fx_mode;             /* the mode the world views run */
static float fx_read;           /* TASK195: r_metalfx as last read (RM_Effect), to see a flip of rt_ab */
static int fx_interp;
static rm_fx_state_t fx_state;  /* the history's last view, the jitter's index, this frame's view */

/* the view being drawn */
static int fx_view;
static unsigned fx_resets;
static rm_fx_size_t fx_size;
static unsigned long fx_views;

static void RM_FX_Read(void);

static void
RM_FX_Say(int ok, const char *line)
{
	if (ok)
	{
		R_Printf(PRINT_ALL, "%s\n", line);
	}
}

void
RM_FX_Init(const rmb_caps_t *caps)
{
	char v[16], line[256];

	r_metalfx = ri.Cvar_Get("r_metalfx", "0", CVAR_ARCHIVE);
	r_metalfx_scale = ri.Cvar_Get("r_metalfx_scale", "0", CVAR_ARCHIVE);
	r_metalfx_interp = ri.Cvar_Get("r_metalfx_interp", "0", CVAR_ARCHIVE);
	r_metalfx_modes = ri.Cvar_Get("r_metalfx_modes", "1", 0);

	fx_has = rmb_fx_support();
	fx_families = caps->families;
	snprintf(v, sizeof(v), "%u", (fx_has & RM_FX_HAS_ALL) | RM_FX_HAS(RM_FX_OFF));
	ri.Cvar_Set("r_metalfx_modes", v);
	r_metalfx_modes->modified = false;
	RM_FX_Say(rm_fx_support_format(fx_has, line, sizeof(line)), line);

	fx_mode = RM_FX_OFF;
	fx_interp = 0;
	fx_view = 0;
	fx_views = 0;
	RM_FX_Reset();

	/* read now, so a fallback's line comes with the renderer's own, before a map clears the notify lines */
	r_metalfx->modified = true;
	r_metalfx_interp->modified = true;
	RM_FX_Read();
}

void
RM_FX_Reset(void)
{
	rm_fx_state_reset(&fx_state);
}

/* the back end's note, when it has one (a scaler that cannot be made, targets that do not fit) */
static void
RM_FX_Notes(void)
{
	char line[256];
	rm_fx_size_t s;
	int mode;
	const int note = rmb_fx_note(&mode, &s);

	RM_FX_Say(rm_fx_note_format(note, mode, &s, line, sizeof(line)), line);
}

/* r_metalfx and r_metalfx_interp as the device allows, written back with a line when they are not */
static void
RM_FX_Read(void)
{
	char line[256], v[16];

	if (!r_metalfx || !r_metalfx_interp)
	{
		return;
	}

	/* r_metalfx_modes is the renderer's: a value written to it is put back */
	if (r_metalfx_modes->modified)
	{
		snprintf(v, sizeof(v), "%u", (fx_has & RM_FX_HAS_ALL) | RM_FX_HAS(RM_FX_OFF));
		ri.Cvar_Set("r_metalfx_modes", v);
		r_metalfx_modes->modified = false;
	}

	/* TASK195: the tuning harness's B side may change the value read without a write */
	if (r_metalfx->modified || !((RM_Effect(r_metalfx) == fx_read) || (isnan(RM_Effect(r_metalfx)) && isnan(fx_read))))
	{
		const float asked_v = RM_Effect(r_metalfx);
		const int asked = rm_fx_mode_of(asked_v), got = rm_fx_supported(asked, fx_has);

		r_metalfx->modified = false;
		fx_read = asked_v;

		if ((float)got != asked_v)
		{
			RM_FX_Say((got != asked) && rm_fx_fallback_format("r_metalfx", asked, got,
					"is not supported on this device, it reads", line, sizeof(line)), line);

			/* the user's own value is set back; an overlaid one is only read as got */
			if (asked_v == r_metalfx->value)
			{
				ri.Cvar_SetValue("r_metalfx", (float)got);
				r_metalfx->modified = false;
			}
			fx_read = (asked_v == r_metalfx->value) ? (float)got : asked_v;
		}

		/* no ray-traced effect exists before RT3: the denoised scaler runs as the temporal one */
		fx_mode = rm_fx_run(got, 0, fx_has);

		if (fx_mode == RM_FX_OFF)
		{
			rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0);   /* turned off: the scaler is freed */
		}

		RM_FX_Say((fx_mode != got) && rm_fx_fallback_format("r_metalfx", got, fx_mode,
				"runs without a ray-traced effect as", line, sizeof(line)), line);
	}

	if (r_metalfx_interp->modified)
	{
		const float asked_v = r_metalfx_interp->value;
		const int asked = isfinite(asked_v) && (asked_v != 0.0f);

		r_metalfx_interp->modified = false;

		/* frame generation is not built yet (TASK111's leftover): it reads 0 on every device for now */
		fx_interp = rm_fx_interp(asked_v, fx_has & ~RM_FX_HAS_INTERP);

		if ((float)fx_interp != asked_v)
		{
			RM_FX_Say(asked && rm_fx_fallback_format("r_metalfx_interp", asked, fx_interp,
					(fx_has & RM_FX_HAS_INTERP) ? "has no frame generation in this build, it reads" :
					"is not supported on this device, it reads", line, sizeof(line)), line);
			ri.Cvar_SetValue("r_metalfx_interp", (float)fx_interp);
			r_metalfx_interp->modified = false;
		}
	}
}

int
RM_FX_WantsMotion(void)
{
	RM_FX_Read();
	return rm_fx_wants_motion(fx_mode, fx_interp);
}

int
RM_FX_Begin(const rm_viewport_t *out, const float *vieworg, int *rw, int *rh, rm_viewport_t *in, float *jitter)
{
	rm_fx_size_t s;
	unsigned reset;
	float scale, org[3];

	fx_view = 0;
	rmb_fx_unready();   /* nothing left prepared from an earlier view, whatever this one does */
	RM_FX_Read();
	rmb_fx_set_timing(metal_stats && metal_stats->value);

	if (fx_mode == RM_FX_OFF)
	{
		rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0);   /* frees a scaler left from a mode turned off */
		return 0;
	}

	scale = rm_fx_scale(r_metalfx_scale->value, rm_fx_default_rows(fx_families), (int)out->h,
			rmb_fx_max_ratio(fx_mode));

	if (!rm_fx_sizes(out, scale, &s))
	{
		return 0;
	}

	VectorCopy(vieworg, org);
	reset = rm_fx_step(&fx_state, fx_mode, &s, org, RM_FX_CUT_DIST, &jitter[0], &jitter[1]);

	if (!rmb_fx_prepare(fx_mode, &s, jitter[0], jitter[1], reset != 0))
	{
		RM_FX_Notes();
		jitter[0] = jitter[1] = 0.0f;
		return 0;
	}

	*rw = s.iw;
	*rh = s.ih;
	rm_fx_input_viewport(out, &s, in);
	fx_view = 1;
	fx_resets = reset;
	fx_size = s;
	return 1;
}

void
RM_FX_End(void)
{
	char line[256];

	RM_FX_Notes();   /* the seam may have refused the view's targets */

	if (!fx_view)
	{
		return;
	}

	fx_view = 0;

	/* metal_stats 1: on a view that resets and on every RM_FX_STATS_EVERY-th; 2 and above: on every one */
	if (metal_stats && metal_stats->value &&
		(fx_resets || (metal_stats->value >= 2) || ((fx_views % RM_FX_STATS_EVERY) == 0)))
	{
		RM_FX_Say(rm_fx_stats_format(fx_mode, &fx_size, fx_resets, rmb_fx_gpu_ms(), line, sizeof(line)), line);
	}

	fx_views++;
}

void
RM_FX_EndFrame(void)
{
	rm_fx_frame_end(&fx_state);
}
