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
 * The ray-traced presentation's data at map load (TASK187, PM5 of
 * docs/plans/presentation-modes.md, section 4.2), fed to rm_rtlights.c:
 *
 * - the light entities from the BSP's entity lump, read from the map file
 *   Mod_LoadBrushModel holds (not from the game, which frees an untargeted
 *   light at spawn);
 * - the SURF_LIGHT faces from the raw texinfo and faces lumps (the loaded
 *   mtexinfo_t drops the texinfo value), each distinct texture's average
 *   colour read once from its .wal through the palette: the raw texel mean,
 *   0.5 for a texture that cannot be read. (qrad3 normalizes the mean and
 *   doubles it when small, and leaves an unread texture at 0; whether
 *   PM6 follows it is open.)
 *   The texels the image loader had are freed after the upload, so this
 *   is one read of textures/<name>.wal a distinct emitter texture, the
 *   path Mod_LoadTexinfo already loads;
 * - the sky's colour: ref_metal holds the six sky images, not a SURF_SKY
 *   texture's texels, so it is the mean of the faces' means as uploaded
 *   (image_t.mean, TASK56).
 *
 * None of it runs while r_rt_stats is 0 and the presentation is not
 * RT-primary (TASK188, whose direct light reads the lights through
 * RM_RTD_Lights): the stock path is untouched.
 *
 * =======================================================================
 */

#include <stddef.h>

#include "header/local.h"
#include "header/rm_rtdata.h"
#include "header/rm_rtlights.h"
#include "header/rm_rtgate.h"

_Static_assert(sizeof(texinfo_t) == RM_RTL_TEXINFO_SIZE, "a texinfo is 76 bytes");
_Static_assert(offsetof(texinfo_t, flags) == RM_RTL_TEXINFO_FLAGS, "texinfo flags at 32");
_Static_assert(offsetof(texinfo_t, value) == RM_RTL_TEXINFO_VALUE, "texinfo value at 36");
_Static_assert(offsetof(texinfo_t, texture) == RM_RTL_TEXINFO_NAME, "texinfo texture at 40");
_Static_assert(sizeof(((texinfo_t *)0)->texture) == RM_RTL_TEXNAME, "a texture name is 32 bytes");
_Static_assert(sizeof(dface_t) == RM_RTL_FACE_SIZE, "a face is 20 bytes");
_Static_assert(offsetof(dface_t, texinfo) == RM_RTL_FACE_TEXINFO, "face texinfo at 10");
_Static_assert(sizeof(((dface_t *)0)->texinfo) == 2, "a face's texinfo is a short");
_Static_assert(SURF_LIGHT == RM_RTL_SURF_LIGHT, "SURF_LIGHT");
_Static_assert(SURF_SKY == RM_RTL_SURF_SKY, "SURF_SKY");
_Static_assert(sizeof(miptex_t) == 100, "a .wal header is 100 bytes");
_Static_assert(MAX_MAP_FACES == RM_RTL_MAX_EMITTERS, "an emitter a face at most");
_Static_assert(MAX_LIGHTSTYLES == RM_RTL_STYLES, "the lightstyles");

static rm_rtl_lights_t rtd_lights;
static rm_rtl_emitters_t rtd_emit;
static char rtd_map[MAX_QPATH];
static int rtd_pending;   /* gathered, its line not yet printed */
static int rtd_have;      /* rtd_lights and rtd_emit hold rtd_map's data */
static int rtd_say;       /* r_rt_stats was on at the gathering: print the line */

void
RM_RTD_BrushLoaded(const char *name, const byte *mod_base, const dheader_t *header)
{
	cvar_t *stats = ri.Cvar_Get("r_rt_stats", "0", 0);
	const lump_t *ent = &header->lumps[LUMP_ENTITIES];
	const lump_t *ti = &header->lumps[LUMP_TEXINFO];
	const lump_t *fa = &header->lumps[LUMP_FACES];
	int i;

	rtd_pending = rtd_have = 0;
	rtd_say = stats && stats->value;

	if (!rtd_say && (RM_PresentationMode() != RM_PRES_RTPRIMARY))
	{
		return;
	}

	/* Mod_CheckLumps has held every lump inside the file */
	rm_rtl_parse((const char *)mod_base + ent->fileofs, (size_t)ent->filelen, &rtd_lights);
	rm_rtl_emitters(mod_base + ti->fileofs, (size_t)ti->filelen, mod_base + fa->fileofs, (size_t)fa->filelen,
			&rtd_emit);

	for (i = 0; i < rtd_emit.ntex; i++)
	{
		rm_rtl_tex_t *t = &rtd_emit.tex[i];
		char path[MAX_QPATH + 16];
		byte *wal = NULL;
		int len;

		Com_sprintf(path, sizeof(path), "textures/%s.wal", t->name);
		len = ri.FS_LoadFile(path, (void **)&wal);

		if (wal)
		{
			t->read = (len > 0) && rm_rtl_wal_mean(wal, (size_t)len, d_8to24table, t->mean);
			ri.FS_FreeFile(wal);
		}
	}

	Q_strlcpy(rtd_map, name, sizeof(rtd_map));
	rtd_have = 1;
	rtd_pending = rtd_say;
}

const rm_rtl_lights_t *
RM_RTD_Lights(const char **map)
{
	if (!rtd_have)
	{
		return NULL;
	}

	*map = rtd_map;
	return &rtd_lights;
}

void
RM_RTD_MapLoaded(void)
{
	float means[6][3], sky[3];
	int have[6];
	char line[RM_STATS_LINE];

	if (!rtd_pending)
	{
		return;
	}

	rtd_pending = 0;
	RM_SkyMeans(means, have);

	if (rm_rtl_format(rtd_map, &rtd_lights, &rtd_emit, rm_rtl_sky_mean((const float (*)[3])means, have, 6, sky) ? sky : NULL,
			line, sizeof(line)))
	{
		R_Printf(PRINT_ALL, "%s\n", line);
	}
}
