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
 * The table of ref_metal's ray-traced effects and its walks (TASK179):
 * header/rm_rtfx.h says what each function does. An effect is added by a
 * row here, its id in rm_rtfx.h and rm_rtfx_order, its switch in the
 * gate's table (rm_rtgate.c, which takes the classification from here),
 * and its own files. tests/metal/rm_rtfx_test.c builds this file with
 * RM_RTFX_HOST, which names a recording stub of tests/metal/rm_rtfx_host.h
 * for every entry point (never defined by the real build).
 *
 * =======================================================================
 */

#include <string.h>

#include "header/local.h"
#include "header/rm_gbuf.h"
#include "header/rm_rtfx.h"
#include "header/rm_rtscene.h"
#include "header/rm_rtaoscene.h"
#include "header/rm_rtsunscene.h"
#include "header/rm_rtwaterscene.h"
#include "header/rm_rtdirscene.h"
#include "header/rm_rtshadow.h"
#include "header/rm_rtao.h"
#include "header/rm_rtsun.h"
#include "header/rm_rtwater.h"
#include "header/rm_rtdir.h"

#ifdef RM_RTFX_HOST
#include "rm_rtfx_host.h"
#define FX(f) rm_rtfx_host_##f
#else
#define FX(f) f
#endif

typedef struct
{
	const char *name;
	const char *sw;   /* its switch: an effect row of the gate (rm_rtgate.c) */

	/* the scene's, in rm_*scene.c */
	void (*init)(const rmb_caps_t *caps);
	void (*shutdown)(void);
	int (*wanted)(void);
	void (*frame)(const refdef_t *fd, int scene_ready);
	int (*active)(void);
	int (*show_only)(void);
	void (*view)(const rm_view_t *view);

	/* the backend's, in rmb_*.m */
	void (*mark)(rmb_rtfx_io_t *io);
	void (*trace)(rmb_rtfx_io_t *io);
	int (*resolve)(rmb_rtfx_io_t *io);
	void (*show)(rmb_rtfx_io_t *io);
	void (*post)(rmb_rtfx_io_t *io);
	int (*forward)(rmb_rtfx_io_t *io);
	void (*bind)(rmb_rtfx_io_t *io);
	void (*view_end)(void);
} rm_rtfx_t;

static const rm_rtfx_t rm_rtfx[RM_RTFX_COUNT] = {
	[RM_RTFX_SHADOWS] = {
		"shadows", "r_rt_shadows",
		FX(RM_RTS_Init), FX(RM_RTS_Shutdown), FX(RM_RTS_Wanted), FX(RM_RTS_Frame), FX(RM_RTS_Active),
		FX(RM_RTS_ShowOnly), FX(RM_RTS_View),
		NULL, FX(rmb_rts_fx_trace), FX(rmb_rts_fx_resolve), NULL, NULL, NULL, NULL, NULL
	},
	[RM_RTFX_AO] = {
		"ao", "r_rt_ao",
		FX(RM_RTAO_Init), FX(RM_RTAO_Shutdown), FX(RM_RTAO_Wanted), FX(RM_RTAO_Frame), FX(RM_RTAO_Active),
		FX(RM_RTAO_ShowOnly), FX(RM_RTAO_View),
		NULL, FX(rmb_rtao_fx_trace), NULL, FX(rmb_rtao_fx_show), NULL, NULL, NULL, NULL
	},
	[RM_RTFX_SUN] = {
		"sun", "r_rt_sun",
		FX(RM_RTSun_Init), FX(RM_RTSun_Shutdown), FX(RM_RTSun_Wanted), FX(RM_RTSun_Frame), FX(RM_RTSun_Active),
		FX(RM_RTSun_ShowOnly), FX(RM_RTSun_View),
		NULL, FX(rmb_rtsun_fx_trace), NULL, FX(rmb_rtsun_fx_show), NULL, NULL, NULL, NULL
	},
	[RM_RTFX_WATER] = {
		"water", "r_rt_water",
		FX(RM_RTW_Init), FX(RM_RTW_Shutdown), FX(RM_RTW_Wanted), FX(RM_RTW_Frame), FX(RM_RTW_Active),
		FX(RM_RTW_ShowOnly), FX(RM_RTW_View),
		FX(rmb_rtw_fx_mark), NULL, NULL, NULL, FX(rmb_rtw_fx_post), FX(rmb_rtw_fx_forward), FX(rmb_rtw_fx_bind),
		FX(rmb_rtw_view_end)
	},
	[RM_RTFX_DIRECT] = {
		"direct", "r_rt_direct",
		FX(RM_RTDir_Init), FX(RM_RTDir_Shutdown), FX(RM_RTDir_Wanted), FX(RM_RTDir_Frame), FX(RM_RTDir_Active),
		FX(RM_RTDir_ShowOnly), FX(RM_RTDir_View),
		NULL, FX(rmb_rtdir_fx_trace), NULL, FX(rmb_rtdir_fx_show), NULL, NULL, NULL, NULL
	},
};

const int rm_rtfx_order[RM_RTFX_COUNT] = { RM_RTFX_DIRECT, RM_RTFX_SHADOWS, RM_RTFX_AO, RM_RTFX_SUN, RM_RTFX_WATER };

/* the k-th row of the order */
#define ROW(k) (&rm_rtfx[rm_rtfx_order[k]])

const char *
rm_rtfx_name(int id)
{
	return ((id < 0) || (id >= RM_RTFX_COUNT)) ? NULL : rm_rtfx[id].name;
}

const char *
rm_rtfx_switch(int id)
{
	return ((id < 0) || (id >= RM_RTFX_COUNT)) ? NULL : rm_rtfx[id].sw;
}

int
rm_rtfx_is_switch(const char *name)
{
	int id;

	for (id = 0; name && (id < RM_RTFX_COUNT); id++)
	{
		if (!strcmp(rm_rtfx[id].sw, name))
		{
			return 1;
		}
	}

	return 0;
}

int
rm_rtfx_pipe(int pass, int pipe, int surf, int (*forward)(rmb_rtfx_io_t *io), rmb_rtfx_io_t *io)
{
	if ((pipe < 0) || (pipe >= RM_PIPE_3D_COUNT))
	{
		return RM_RTFX_PIPE_NONE;
	}

	switch (pass)
	{
		case RMB_GB_PASS_RASTER:
			return surf ? RM_RTFX_PIPE_RASTER_SURF : RM_RTFX_PIPE_RASTER;
		case RMB_GB_PASS_MOTION:
			/* RT6 (TASK58): a translucent face takes the reflection where it is the water the kernel found */
			if ((pipe == RM_PIPE_WORLD_TEXC_BLEND) && forward && forward(io))
			{
				return RM_RTFX_PIPE_FORWARD;
			}

			return RM_RTFX_PIPE_MOTION;
		case RMB_GB_PASS_OTHER:
			return RM_RTFX_PIPE_NONE;
		default:
			return RM_RTFX_PIPE_STOCK;
	}
}

/* ---- the backend's walks ----------------------------------------------------------- */

void
rm_rtfx_mark(rmb_rtfx_io_t *io)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->mark)
		{
			ROW(k)->mark(io);
		}
	}
}

void
rm_rtfx_trace(rmb_rtfx_io_t *io)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->trace)
		{
			ROW(k)->trace(io);
		}
	}
}

int
rm_rtfx_resolve(rmb_rtfx_io_t *io)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->resolve && ROW(k)->resolve(io))
		{
			return 1;
		}
	}

	return 0;
}

void
rm_rtfx_show(rmb_rtfx_io_t *io)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->show)
		{
			ROW(k)->show(io);
		}
	}
}

void
rm_rtfx_post(rmb_rtfx_io_t *io)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->post)
		{
			ROW(k)->post(io);
		}
	}
}

int
rm_rtfx_forward(rmb_rtfx_io_t *io)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->forward && ROW(k)->forward(io))
		{
			return 1;
		}
	}

	return 0;
}

void
rm_rtfx_bind(rmb_rtfx_io_t *io)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->bind)
		{
			ROW(k)->bind(io);
		}
	}
}

void
rm_rtfx_view_end(void)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->view_end)
		{
			ROW(k)->view_end();
		}
	}
}

/* ---- the scene's walks ------------------------------------------------------------- */

void
rm_rtfx_init(const rmb_caps_t *caps)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->init)
		{
			ROW(k)->init(caps);
		}
	}
}

void
rm_rtfx_shutdown(void)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->shutdown)
		{
			ROW(k)->shutdown();
		}
	}
}

int
rm_rtfx_wanted(void)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->wanted && ROW(k)->wanted())
		{
			return 1;
		}
	}

	return 0;
}

void
rm_rtfx_frame(const refdef_t *fd, int scene_ready)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->frame)
		{
			ROW(k)->frame(fd, scene_ready);
		}
	}
}

int
rm_rtfx_active(void)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->active && ROW(k)->active())
		{
			return 1;
		}
	}

	return 0;
}

int
rm_rtfx_show_only(void)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->show_only && ROW(k)->show_only())
		{
			return 1;
		}
	}

	return 0;
}

void
rm_rtfx_view(const rm_view_t *view)
{
	int k;

	for (k = 0; k < RM_RTFX_COUNT; k++)
	{
		if (ROW(k)->view)
		{
			ROW(k)->view(view);
		}
	}
}
