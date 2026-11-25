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
 * The scene of RT-primary's light on rastered alias models (TASK191
 * slice 2; header/rm_rtmodelscene.h): r_rt_direct_models, whether this
 * frame's models are traced (only where the direct light traces the
 * frame), the frame's parameters and tables for the back end, and the
 * r_rt_stats 2 line of each completed frame.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rtdirscene.h"
#include "header/rm_rtmodel.h"
#include "header/rm_rtmodelscene.h"

static cvar_t *r_rt_direct_models;
static cvar_t *r_rt_stats_ref;
static int rtm_device;      /* rmb_caps_t.raytracing */
static int rtm_opened;      /* rmb_rtmodel_open succeeded */
static int rtm_refused;     /* it failed in this context: not tried again */
static int rtm_active;      /* this frame's rastered models are traced */

void
RM_RTModel_Init(const rmb_caps_t *caps)
{
	/* a setting of the direct light (rm_rtgate.c): it runs nothing where the direct light does not */
	r_rt_direct_models = ri.Cvar_Get("r_rt_direct_models", "1", CVAR_ARCHIVE);
	r_rt_stats_ref = ri.Cvar_Get("r_rt_stats", "0", 0);
	rtm_device = caps->raytracing != 0;
	rtm_opened = rtm_refused = rtm_active = 0;
}

void
RM_RTModel_Shutdown(void)
{
	if (rtm_opened)
	{
		rmb_rtmodel_close();
	}

	rtm_opened = rtm_active = 0;
}

/* the scene is the direct light's to ask for (RM_RTDir_Wanted) */
int
RM_RTModel_Wanted(void)
{
	return 0;
}

void
RM_RTModel_Frame(const refdef_t *fd, int scene_ready)
{
	rm_rtm_stats_t st[RMB_FRAMES_IN_FLIGHT];
	const int want = rtm_device && RM_RTDir_Active() && r_rt_direct_models && (RM_Effect(r_rt_direct_models) != 0.0f);
	int i, n;

	(void)fd;
	(void)scene_ready;
	rtm_active = 0;
	rmb_rtmodel_set_view(NULL, NULL);

	if (want && !rtm_opened && !rtm_refused)
	{
		char err[256];

		rtm_opened = rmb_rtmodel_open(err, sizeof(err));

		if (!rtm_opened)
		{
			rtm_refused = 1;
			R_Printf(PRINT_ALL, "ref_metal: rt models: %s; models stay stock-lit\n", err);
		}
	}

	rtm_active = want && rtm_opened;

	if (!rtm_opened)
	{
		return;
	}

	/* the lines of the frames completed since */
	n = rmb_rtmodel_stats(st, RMB_FRAMES_IN_FLIGHT);

	for (i = 0; i < n; i++)
	{
		char line[RM_STATS_LINE];

		if (r_rt_stats_ref && (r_rt_stats_ref->value >= 2) && rm_rtm_stats_format(&st[i], line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}

	if (!want)
	{
		rmb_rtmodel_close();
		rtm_opened = 0;
	}
}

int
RM_RTModel_Active(void)
{
	return rtm_active;
}

int
RM_TracedModels(void)
{
	return rtm_active;
}

/* its view is the direct light's (r_rt_direct_show) */
int
RM_RTModel_ShowOnly(void)
{
	return 0;
}

/* the frame's parameters: RF_GLOW's pulse, overbright, gl1's minlight table (none: the index itself) and gamma table */
void
RM_RTModel_View(const rm_view_t *view)
{
	unsigned char t[RM_RTM_TABLES];
	const byte *ml = R_Minlight();
	rm_rtm_params_t p;
	int i;

	(void)view;

	if (!rtm_active)
	{
		rmb_rtmodel_set_view(NULL, NULL);
		return;
	}

	memset(&p, 0, sizeof(p));
	p.glow = rm_rtm_glow_scale(r_newrefdef.time);
	p.overbright = gl1_overbrightbits->value;

	for (i = 0; i < 256; i++)
	{
		t[i] = ml ? ml[i] : (unsigned char)i;
		t[256 + i] = gammatable[i];
	}

	rmb_rtmodel_set_view(&p, t);
}
