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
 * The host's entry points of ref_metal's effect table (TASK179): under
 * RM_RTFX_HOST, rm_rtfx.c includes this file after the effects' headers
 * and its FX(f) names rm_rtfx_host_f, a recording stub defined here, so
 * the host links no backend and tests/metal/rm_rtfx_test.c sees what
 * every walk calls. Each stub appends "<effect>:<entry>" to
 * rm_rtfx_host_log (blank-separated) and returns 1 when that token is in
 * rm_rtfx_host_yes, a blank-separated list the test sets. The stubs'
 * types are the rows' own: a row given a stub of another entry's type
 * does not compile. Never part of the real build.
 *
 * =======================================================================
 */

#ifndef RM_RTFX_HOST_H
#define RM_RTFX_HOST_H

#include <stdio.h>
#include <string.h>

char rm_rtfx_host_log[1024];
const char *rm_rtfx_host_yes = "";

/* logs what, and says whether it is in rm_rtfx_host_yes */
static int
rm_rtfx_host_call(const char *what)
{
	size_t n = strlen(rm_rtfx_host_log), w = strlen(what);
	const char *p = rm_rtfx_host_yes;

	snprintf(rm_rtfx_host_log + n, sizeof(rm_rtfx_host_log) - n, "%s%s", n ? " " : "", what);

	while (p && (p = strstr(p, what)) != NULL)
	{
		if (((p == rm_rtfx_host_yes) || (p[-1] == ' ')) && ((p[w] == '\0') || (p[w] == ' ')))
		{
			return 1;
		}

		p += w;
	}

	return 0;
}

/* unused: a mutant that drops a row's entry must still build */
#define RM_RTFX_HOST_STUB static __attribute__((unused))

#define RM_RTFX_HOST_CAPS(f, what) \
	RM_RTFX_HOST_STUB void rm_rtfx_host_##f(const rmb_caps_t *caps) { (void)caps; rm_rtfx_host_call(what); }
#define RM_RTFX_HOST_VOID(f, what) \
	RM_RTFX_HOST_STUB void rm_rtfx_host_##f(void) { rm_rtfx_host_call(what); }
#define RM_RTFX_HOST_SAY(f, what) \
	RM_RTFX_HOST_STUB int rm_rtfx_host_##f(void) { return rm_rtfx_host_call(what); }
#define RM_RTFX_HOST_FRAME(f, what) \
	RM_RTFX_HOST_STUB void rm_rtfx_host_##f(const refdef_t *fd, int scene_ready) \
	{ (void)fd; (void)scene_ready; rm_rtfx_host_call(what); }
#define RM_RTFX_HOST_VIEW(f, what) \
	RM_RTFX_HOST_STUB void rm_rtfx_host_##f(const rm_view_t *view) { (void)view; rm_rtfx_host_call(what); }
#define RM_RTFX_HOST_IO(f, what) \
	RM_RTFX_HOST_STUB void rm_rtfx_host_##f(rmb_rtfx_io_t *io) { (void)io; rm_rtfx_host_call(what); }
#define RM_RTFX_HOST_IO_SAY(f, what) \
	RM_RTFX_HOST_STUB int rm_rtfx_host_##f(rmb_rtfx_io_t *io) { (void)io; return rm_rtfx_host_call(what); }

/* the scene's entries of every effect */
#define RM_RTFX_HOST_SCENE(p, name) \
	RM_RTFX_HOST_CAPS(p##_Init, name ":init") \
	RM_RTFX_HOST_VOID(p##_Shutdown, name ":shutdown") \
	RM_RTFX_HOST_SAY(p##_Wanted, name ":wanted") \
	RM_RTFX_HOST_FRAME(p##_Frame, name ":frame") \
	RM_RTFX_HOST_SAY(p##_Active, name ":active") \
	RM_RTFX_HOST_SAY(p##_ShowOnly, name ":show_only") \
	RM_RTFX_HOST_VIEW(p##_View, name ":view")

RM_RTFX_HOST_SCENE(RM_RTS, "shadows")
RM_RTFX_HOST_SCENE(RM_RTAO, "ao")
RM_RTFX_HOST_SCENE(RM_RTSun, "sun")
RM_RTFX_HOST_SCENE(RM_RTW, "water")
RM_RTFX_HOST_SCENE(RM_RTDir, "direct")
RM_RTFX_HOST_SCENE(RM_RTModel, "models")

/* the backend's, each under the entry it is in its row */
RM_RTFX_HOST_IO(rmb_rts_fx_trace, "shadows:trace")
RM_RTFX_HOST_IO_SAY(rmb_rts_fx_resolve, "shadows:resolve")
RM_RTFX_HOST_IO(rmb_rtao_fx_trace, "ao:trace")
RM_RTFX_HOST_IO(rmb_rtao_fx_show, "ao:show")
RM_RTFX_HOST_IO(rmb_rtsun_fx_trace, "sun:trace")
RM_RTFX_HOST_IO(rmb_rtsun_fx_show, "sun:show")
RM_RTFX_HOST_IO(rmb_rtw_fx_mark, "water:mark")
RM_RTFX_HOST_IO(rmb_rtw_fx_post, "water:post")
RM_RTFX_HOST_IO_SAY(rmb_rtw_fx_forward, "water:forward")
RM_RTFX_HOST_IO(rmb_rtw_fx_bind, "water:bind")
RM_RTFX_HOST_VOID(rmb_rtw_view_end, "water:view_end")
RM_RTFX_HOST_IO(rmb_rtdir_fx_trace, "direct:trace")
RM_RTFX_HOST_IO(rmb_rtdir_fx_show, "direct:show")
RM_RTFX_HOST_IO(rmb_rtmodel_fx_trace, "models:trace")

#endif
