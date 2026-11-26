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
 * RT-primary's light on alias models (TASK191): the rules, plain C with
 * neither SDL nor Metal. See header/rm_rtmodel.h.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rtmodel.h"
#include "header/rm_rtmodelscene.h"

int
rm_rtm_stock_light(int flags, int rdflags)
{
	return ((flags & (RM_RF_SHELLS | RM_RF_FULLBRIGHT)) != 0) ||
		((rdflags & RM_RDF_IRGOGGLES) && (flags & RM_RF_IR_VISIBLE));
}

int
rm_rtm_stock_model(int flags, int rdflags)
{
	/* the depth hack without the weapon (the crosshair model), or the weapon without the depth range the kernel reads */
	return rm_rtm_stock_light(flags, rdflags) || (((flags & RM_RF_DEPTHHACK) != 0) != ((flags & RM_RF_WEAPONMODEL) != 0));
}

int
rm_rtm_entity_split(int flags, int kind, float shadows, int rdflags, int mode)
{
	if (!mode)
	{
		return rm_gb_entity_split(flags, kind, shadows);
	}

	if ((flags & (RM_RF_TRANSLUCENT | RM_RF_BEAM | RM_RF_SHELLS)) ||
		((kind != RM_GB_KIND_ALIAS) && (kind != RM_GB_KIND_BRUSH)))
	{
		return RM_GB_ENT_FORWARD;
	}

	/* slice 3: the view weapon is rastered as an alias model alone, as gl1 draws it */
	if ((flags & RM_RF_WEAPONMODEL) && (kind != RM_GB_KIND_ALIAS))
	{
		return RM_GB_ENT_FORWARD;
	}

	/*
	 * the alias models that stay stock-lit, the crosshair model and a weapon without its depth hack among them;
	 * a glowing or minlight model is rastered, its flags in its tag (slice 3)
	 */
	if ((kind == RM_GB_KIND_ALIAS) && rm_rtm_stock_model(flags, rdflags))
	{
		return RM_GB_ENT_FORWARD;
	}

	/* the weapon casts no shadow: rm_shadow_drawn leaves it out, as gl1 does */
	return RM_GB_ENT_RASTER | (((kind == RM_GB_KIND_ALIAS) && rm_shadow_drawn(flags, shadows)) ? RM_GB_ENT_SHADOW : 0);
}

int
rm_rtm_traced(int flags, int rdflags, int extra_ok)
{
	return extra_ok && (rm_rtm_entity_split(flags, RM_GB_KIND_ALIAS, 0, rdflags, 1) & RM_GB_ENT_RASTER) != 0;
}

unsigned
rm_rtm_flags(int flags)
{
	return ((flags & RM_RF_GLOW) ? MTL_RTM_GLOW : 0u) | ((flags & RM_RF_MINLIGHT) ? MTL_RTM_MINLIGHT : 0u) |
		((flags & RM_RF_WEAPONMODEL) ? MTL_RTM_WEAPON : 0u);
}

float
rm_rtm_glow_scale(float time)
{
	return 0.1 * sin(time * 7);
}

void
rm_rtm_color(const float *light, float dot, int flags, int rdflags, float time, float overbright,
		const unsigned char *minlight, const unsigned char *gamma, unsigned char alpha, float *rgba)
{
	mtl_rtm_idx_t i;
	int idx[3], k;

	if (rm_rtm_stock_light(flags, rdflags))
	{
		float shadelight[3];
		const int shell = (flags & RM_RF_SHELLS_RGB) != 0;

		/* as R_DrawAliasModel: rm_alias_emit's shell is the RGB shells' alone */
		rm_alias_shade(flags, rdflags, light, time, overbright, shadelight);
		rm_alias_color(shell ? 0 : dot, shadelight, shell, minlight, gamma, alpha, rgba);
		return;
	}

	i = mtl_rtm_color(mtl_rtm_v3(light[0], light[1], light[2]), dot, rm_rtm_flags(flags), rm_rtm_glow_scale(time),
			overbright);
	idx[0] = i.r;
	idx[1] = i.g;
	idx[2] = i.b;

	for (k = 0; k < 3; k++)
	{
		const int m = minlight ? minlight[idx[k]] : idx[k];

		rgba[k] = gamma[m] / 255.0f;
	}

	rgba[3] = alpha / 255.0f;
}

int
rm_rtm_tag_wanted(int flags, int rdflags, int extra_ok, int mode, unsigned *f)
{
	*f = 0u;

	/* a model without a flag keeps its own colour: it reads as no flags (rm_rtm_px_flags) */
	if (!mode || !rm_rtm_traced(flags, rdflags, extra_ok) || (rm_rtm_flags(flags) == 0u))
	{
		return 0;
	}

	*f = rm_rtm_flags(flags);
	return 1;
}

int
rm_rtm_gunshadow(int gunshadow, int models)
{
	return gunshadow && !models;
}

int
rm_rtm_wants(int device, int refused, float models, int direct)
{
	return device && !refused && (models != 0.0f) && direct;
}

int
rm_rtm_tag_verts(rm_avert_t *v, long n, unsigned f, int extra_ok)
{
	const mtl_rgba_t c = mtl_rtm_tag(f);
	long i;

	/* a model without its normals is not traced: its own colour stays, never a tag */
	if (!extra_ok)
	{
		return 0;
	}

	for (i = 0; i < n; i++)
	{
		v[i].color[0] = c.r;
		v[i].color[1] = c.g;
		v[i].color[2] = c.b;
		v[i].color[3] = c.a;
	}

	return 1;
}

int
rm_rtm_weapon_view(const rm_rtm_weapon_t *w, const float *world, const rm_viewport_t *vp, const float *eye,
		rm_rts_view_t *out)
{
	float p[16], pj[16];

	memset(out, 0, sizeof(*out));

	if (!rm_weapon_projection(p, w->fovy, w->gunfov, w->hand, w->width, w->height, w->znear, w->farsee,
			w->underwater, w->time, w->waterwarp))
	{
		return 0;
	}

	rm_gb_jitter(p, w->jx, w->jy, vp->w, vp->h, pj);
	return rm_rts_view(pj, world, vp, eye, w->znear, out);
}

int
rm_rtm_weapon_params(const rm_rtm_weapon_t *w, const float *world, const rm_viewport_t *vp, const float *eye,
		rm_rtm_params_t *p)
{
	rm_rts_view_t v;

	p->weapon = (unsigned)rm_rtm_weapon_view(w, world, vp, eye, &v);
	memcpy(p->winv, v.inv, sizeof(p->winv));   /* all 0 when refused: rm_rtm_weapon_view clears it */
	return (int)p->weapon;
}

int
rm_rtm_weapon_unproject(const rm_rts_view_t *v, float px, float py, float d, float *out)
{
	return rm_rts_unproject(v, px, py, mtl_rtm_weapon_depth(d), out);
}

int
rm_rtm_stats_format(const rm_rtm_stats_t *s, char *buf, int len)
{
	int n;

	if (len <= 0)
	{
		return 0;
	}

	if (s->ms >= 0)
	{
		n = snprintf(buf, (size_t)len, "ref_metal: rt models: frame %lu: %lu model pixels, %lu shadow rays, "
				"%lu sky, sun and bounce rays, %.3f ms", s->frame, s->pixels, s->rays, s->open, s->ms);
	}
	else
	{
		n = snprintf(buf, (size_t)len, "ref_metal: rt models: frame %lu: %lu model pixels, %lu shadow rays, "
				"%lu sky, sun and bounce rays, - ms", s->frame, s->pixels, s->rays, s->open);
	}

	if ((n < 0) || (n >= len))
	{
		buf[0] = '\0';
		return 0;
	}

	return 1;
}
