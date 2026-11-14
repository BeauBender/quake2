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
 * The light of gl1_light.c (docs/plans/ref-metal-native.md, sections 2
 * and 6): R_BuildLightMap (the arithmetic is rm_lm_styles and
 * rm_lm_store of rm_pure.c, host-tested) with gl1's dynamic-light step
 * between them (R_AddDynamicLights, rm_lm_dlight, N8) behind one
 * front-end flag, r_dlight_term, so RT3 can replace exactly that term;
 * R_SetCacheState; gl1's marking of the surfaces a dynamic light touches
 * (R_PushDlights, and R_MarkBrushLights for the brush entities); the
 * flash blobs of gl1_flashblend (R_RenderDlights, N8); and R_LightPoint
 * with R_SetLightLevel. The frame's dynamic lights are the renderer's
 * own checked copy of the refdef's (R_TakeDlights): every reader takes
 * them from there.
 *
 * =======================================================================
 */

#include "header/local.h"

static float s_blocklights[RM_LM_MAX_LUXELS * 3];
static byte minlight[256];
static qboolean minlight_set;
static int r_dlightframecount;

static vec3_t pointcolor;

/* the dynamic-light step of R_BuildLightMap (N8): RT3 turns it off and adds the term itself */
qboolean r_dlight_term = true;

/* the frame's dynamic lights as taken (R_TakeDlights), and those left out */
static dlight_t rm_dlights[MAX_DLIGHTS];
static unsigned long dlight_skips;

/* the frame's flash blobs (R_RenderDlights): one fan of RM_FLASH_VERTS per light */
static rm_avert_t flash_verts[MAX_DLIGHTS * RM_FLASH_VERTS];
static unsigned short flash_idx[MAX_DLIGHTS * 3 * (RM_FLASH_VERTS - 2)];

#if RM_MAX_DLIGHTS != MAX_DLIGHTS
#error RM_MAX_DLIGHTS must be ref.h's MAX_DLIGHTS
#endif
static cplane_t *lightplane; /* used as shadow plane */
static vec3_t lightspot;

/*
 * The refdef's dynamic lights as this renderer takes them for the frame:
 * at most MAX_DLIGHTS (rm_dlight_count, the 32 bits of a surface's mask),
 * each copied and checked (rm_dlight_ok: finite, inside its bounds) or
 * left out and counted; r_newrefdef then names the copy, so marking, the
 * lightmap step, R_LightPoint and the flash blobs read only checked
 * values. The refdef itself is copied once per view (R_RenderView), and
 * nothing copies the client's lights again before the next view.
 */
void
R_TakeDlights(void)
{
	const dlight_t *in = r_newrefdef.dlights;
	int i, k = 0, n = in ? rm_dlight_count(r_newrefdef.num_dlights) : 0;

	dlight_skips = 0;

	for (i = 0; i < n; i++)
	{
		dlight_t l = in[i];

		if (!rm_dlight_ok(l.origin, l.color, l.intensity))
		{
			dlight_skips++;
			continue;
		}

		rm_dlights[k++] = l;
	}

	r_newrefdef.dlights = rm_dlights;
	r_newrefdef.num_dlights = k;
}

/* the dynamic lights of the last view left out, into s (metal_stats) */
void
RM_DlightSkips(rmb_stats_t *s)
{
	s->dlight_skips = dlight_skips;
}

/* gl1's lightspot of the last R_LightPoint that reached a node (the alias shadows' plane) */
const float *
R_LightSpot(void)
{
	return lightspot;
}

/* gl1's minlight table, made at init as gl1's R_InitImages makes it (a change needs vid_restart) */
void
R_InitLight(void)
{
	minlight_set = rm_minlight_table(gl1_minlight->value, minlight);
}

/* the table when gl1 uses it (gl_state.minlight_set), else NULL: the alias models' colours (N7) */
const byte *
R_Minlight(void)
{
	return minlight_set ? minlight : NULL;
}

/* from gl1_light.c */
void
R_SetCacheState(msurface_t *surf)
{
	int maps;

	for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
	{
		surf->cached_light[maps] = r_newrefdef.lightstyles[surf->styles[maps]].white;
	}
}

/*
 * from gl1_light.c, the loop of R_AddDynamicLights: each light of the
 * frame the surface is marked for (its bit of dlightbits) added into the
 * surface's smax by tmax luxels of s_blocklights (rm_lm_dlight, which
 * refuses a light whose int conversions would be undefined)
 */
static void
R_AddDynamicLights(const msurface_t *surf, int smax, int tmax)
{
	int lnum, n = rm_dlight_count(r_newrefdef.num_dlights);
	const unsigned bits = (unsigned)surf->dlightbits;
	const mtexinfo_t *tex = surf->texinfo;

	for (lnum = 0; lnum < n; lnum++)
	{
		const dlight_t *dl;

		if (!(bits & rm_dlight_bit(lnum)))
		{
			continue; /* not lit by this light */
		}

		dl = &r_newrefdef.dlights[lnum];
		rm_lm_dlight(s_blocklights, smax, tmax, surf->plane->normal, surf->plane->dist, tex->vecs[0], tex->vecs[1],
				surf->texturemins[0], surf->texturemins[1], dl->origin, dl->intensity, dl->color);
	}
}

/*
 * from gl1_light.c: combine and scale the styles of surf into dest (rows
 * stride bytes apart), with the dynamic lights that touch it this frame
 * when it has light data (gl1's R_AddDynamicLights, behind r_dlight_term);
 * the size is the one the loader checked. false when it is refused
 * (nothing written).
 */
qboolean
R_BuildLightMap(msurface_t *surf, byte *dest, int stride)
{
	float rgb[MAXLIGHTMAPS][3];
	int smax, tmax, nummaps, maps, i, styled;

	if (surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP))
	{
		ri.Sys_Error(ERR_DROP, "%s called for non-lit surface", __func__);
		return false;
	}

	if (!rm_lm_size(surf->extents[0], surf->extents[1], &smax, &tmax))
	{
		return false;
	}

	nummaps = rm_lm_nummaps(surf->styles);

	for (maps = 0; maps < nummaps; maps++)
	{
		for (i = 0; i < 3; i++)
		{
			rgb[maps][i] = r_newrefdef.lightstyles[surf->styles[maps]].rgb[i];
		}
	}

	if (stride < smax * RM_LM_BYTES)
	{
		return false;
	}

	styled = rm_lm_styles(surf->samples, smax, tmax, nummaps, (const float (*)[3])rgb, r_modulate->value,
			s_blocklights);

	if (!styled)
	{
		return false;
	}

	/* add all the dynamic lights (not on a surface without light data: gl1 goes to the store) */
	if ((styled == 2) && r_dlight_term && (surf->dlightframe == r_framecount))
	{
		R_AddDynamicLights(surf, smax, tmax);
	}

	return rm_lm_store(smax, tmax, minlight_set ? minlight : NULL, gammatable, dest, stride, s_blocklights) != 0;
}

/* from gl1_light.c */
void
R_MarkSurfaceLights(dlight_t *light, int bit, mnode_t *node, int lightframecount)
{
	msurface_t *surf;
	int i;

	/* mark the polygons */
	surf = r_worldmodel->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		int sidebit;
		float dist;

		dist = DotProduct(light->origin, surf->plane->normal) - surf->plane->dist;

		if (dist >= 0)
		{
			sidebit = 0;
		}
		else
		{
			sidebit = SURF_PLANEBACK;
		}

		if ((surf->flags & SURF_PLANEBACK) != sidebit)
		{
			continue;
		}

		if (surf->dlightframe != lightframecount)
		{
			surf->dlightbits = 0;
			surf->dlightframe = lightframecount;
		}

		surf->dlightbits |= bit;
	}
}

/*
 * from gl1_light.c: marks the surfaces each dynamic light touches, before
 * R_SetupFrame advances r_framecount; R_BuildLightMap adds the light to
 * the ones R_RegenAllLightmaps rebuilds (rm_surf.c).
 */
void
R_PushDlights(void)
{
	int i, n;
	dlight_t *l;

	if (gl1_flashblend->value || !r_worldmodel || (r_newrefdef.rdflags & RDF_NOWORLDMODEL))
	{
		return;
	}

	/* because the count hasn't advanced yet for this frame */
	r_dlightframecount = r_framecount + 1;

	l = r_newrefdef.dlights;
	n = rm_dlight_count(r_newrefdef.num_dlights); /* at most 32 lights, one bit each */

	for (i = 0; i < n; i++, l++)
	{
		R_MarkLights(l, (int)rm_dlight_bit(i), r_worldmodel->nodes, r_dlightframecount, R_MarkSurfaceLights);
	}
}

/*
 * from gl1_surf.c, R_GetBrushesLighting (and R_DrawInlineBModel): marks
 * the dynamic lights on a brush entity's surfaces from its submodel's
 * head node, as gl1 marks them, with the frame's count R_PushDlights set
 * (r_framecount by now). A head node that is not a node of the world (a
 * submodel without faces holds a leaf, -1 - leaf) marks nothing
 * (rm_bmodel_headnode).
 */
void
R_MarkBrushLights(const model_t *currentmodel)
{
	int k, n;
	long head;
	dlight_t *lt;

	if (!r_worldmodel || !currentmodel || (currentmodel->nodes != r_worldmodel->nodes))
	{
		return;
	}

	head = rm_bmodel_headnode(currentmodel->firstnode, r_worldmodel->numnodes);

	if (head < 0)
	{
		return;
	}

	lt = r_newrefdef.dlights;
	n = rm_dlight_count(r_newrefdef.num_dlights);

	for (k = 0; k < n; k++, lt++)
	{
		R_MarkLights(lt, (int)rm_dlight_bit(k), r_worldmodel->nodes + head, r_dlightframecount, R_MarkSurfaceLights);
	}
}

/*
 * from gl1_light.c, R_RenderDlights and R_RenderDlight: with
 * gl1_flashblend, a blob for each dynamic light instead of its light on
 * the lightmaps: one fan of RM_FLASH_VERTS vertices (rm_flash_blob, gl1's
 * arithmetic), blended GL_ONE, GL_ONE without depth writes, untextured,
 * in one draw (gl1's buf_flash batch). 0 when the draw is refused
 * (counted by the back end).
 */
int
R_RenderDlights(const rm_view_t *view)
{
	rmb_alias_draw_t d;
	int i, n, ni = 0;
	dlight_t *l;

	if (!gl1_flashblend->value)
	{
		return 1;
	}

	/* because the count hasn't advanced yet for this frame */
	r_dlightframecount = r_framecount + 1;

	l = r_newrefdef.dlights;
	n = rm_dlight_count(r_newrefdef.num_dlights);

	for (i = 0; i < n; i++, l++)
	{
		rm_flash_blob(l->origin, l->intensity, l->color, vpn, vright, vup, gammatable, flash_verts + i * RM_FLASH_VERTS);
		ni += rm_prim_indices(0, RM_FLASH_VERTS, i * RM_FLASH_VERTS, flash_idx + ni,
				(int)(sizeof(flash_idx) / sizeof(flash_idx[0])) - ni);
	}

	if (!n)
	{
		return 1;
	}

	memset(&d, 0, sizeof(d));
	d.add = 1;
	d.cull = view->cull ? RMB_CULL_FRONT : RMB_CULL_NONE;
	d.zfar = 1;

	return rmb_draw_alias(flash_verts, (unsigned)(n * RM_FLASH_VERTS), flash_idx, (unsigned long)ni, &d, view->mvp,
			&view->vp);
}

/*
 * from gl1_light.c. The samples it reads are inside the lighting lump:
 * the loader checked every surface's run of styles with its extents
 * (rm_lm_samples_ok), and rm_lp_coords gives ds, dt of 0 .. extents:
 * it refuses a coordinate that is not finite or whose truncation does
 * not fit an int (gl1's conversion is undefined there), then runs gl1's
 * truncation and checks in 64-bit arithmetic, so no difference wraps.
 * On every input where gl1's arithmetic is defined it accepts the same
 * points with the same ds, dt (a point within one texture unit outside
 * an edge is truncated onto it, as in gl1); a point outside is a miss on
 * this surface and the walk goes on, as gl1's. Not reached in N5: the
 * only caller is R_SetLightLevel(NULL), and R_LightPoint returns before
 * the trace without an entity; N7 calls it with one and inherits this
 * bound.
 */
static int
R_RecursiveLightPoint(mnode_t *node, vec3_t start, vec3_t end)
{
	float front, back, frac;
	int side;
	cplane_t *plane;
	vec3_t mid;
	msurface_t *surf;
	float s, t;
	int ds, dt;
	int i;
	mtexinfo_t *tex;
	byte *lightmap;
	int maps;
	int r;

	if (node->contents != CONTENTS_NODE)
	{
		return -1; /* didn't hit anything */
	}

	/* calculate mid point */
	plane = node->plane;
	front = DotProduct(start, plane->normal) - plane->dist;
	back = DotProduct(end, plane->normal) - plane->dist;
	side = front < 0;

	if ((back < 0) == side)
	{
		return R_RecursiveLightPoint(node->children[side], start, end);
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	/* go down front side */
	r = R_RecursiveLightPoint(node->children[side], start, mid);

	if (r >= 0)
	{
		return r; /* hit something */
	}

	if ((back < 0) == side)
	{
		return -1; /* didn't hit anuthing */
	}

	/* check for impact on this node */
	VectorCopy(mid, lightspot);
	lightplane = plane;

	surf = r_worldmodel->surfaces + node->firstsurface;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY))
		{
			continue; /* no lightmaps */
		}

		tex = surf->texinfo;

		s = DotProduct(mid, tex->vecs[0]) + tex->vecs[0][3];
		t = DotProduct(mid, tex->vecs[1]) + tex->vecs[1][3];

		if (!rm_lp_coords(s, t, surf->texturemins[0], surf->texturemins[1], surf->extents[0], surf->extents[1],
				&ds, &dt))
		{
			continue;
		}

		if (!surf->samples)
		{
			return 0;
		}

		ds >>= 4;
		dt >>= 4;

		lightmap = surf->samples;
		VectorCopy(vec3_origin, pointcolor);

		lightmap += 3 * (dt * ((surf->extents[0] >> 4) + 1) + ds);

		for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			const float *rgb;
			int j;

			rgb = r_newrefdef.lightstyles[surf->styles[maps]].rgb;

			/* Apply light level to models */
			for (j = 0; j < 3; j++)
			{
				float scale;

				scale = rgb[j] * r_modulate->value;
				pointcolor[j] += lightmap[j] * scale * (1.0 / 255);
			}

			lightmap += 3 * ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1);
		}

		return 1;
	}

	/* go down back side */
	return R_RecursiveLightPoint(node->children[!side], mid, end);
}

/* from gl1_light.c (N7 lights the models with it) */
void
R_LightPoint(entity_t *currententity, vec3_t p, vec3_t color)
{
	vec3_t end;
	float r;
	int lnum, n;
	dlight_t *dl;
	vec3_t dist;
	float add;

	if (!r_worldmodel || !r_worldmodel->lightdata || !currententity)
	{
		color[0] = color[1] = color[2] = 1.0;
		return;
	}

	end[0] = p[0];
	end[1] = p[1];
	end[2] = p[2] - 2048;

	r = R_RecursiveLightPoint(r_worldmodel->nodes, p, end);

	if (r == -1)
	{
		VectorCopy(vec3_origin, color);
	}
	else
	{
		VectorCopy(pointcolor, color);
	}

	/* add dynamic lights */
	dl = r_newrefdef.dlights;
	n = rm_dlight_count(r_newrefdef.num_dlights);

	for (lnum = 0; lnum < n; lnum++, dl++)
	{
		VectorSubtract(currententity->origin, dl->origin, dist);
		add = dl->intensity - VectorLength(dist);
		add *= (1.0 / 256);

		if (add > 0)
		{
			VectorMA(color, add, dl->color, color);
		}
	}

	VectorScale(color, r_modulate->value, color);
}

/* from gl1_main.c: the light value for the server (gl1 passes no entity, so it is 150) */
void
R_SetLightLevel(entity_t *currententity)
{
	vec3_t shadelight;

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		return;
	}

	/* save off light value for server to look at */
	R_LightPoint(currententity, r_newrefdef.vieworg, shadelight);

	/* pick the greatest component, which should be the same as the mono value returned by software */
	if (shadelight[0] > shadelight[1])
	{
		if (shadelight[0] > shadelight[2])
		{
			r_lightlevel->value = 150 * shadelight[0];
		}
		else
		{
			r_lightlevel->value = 150 * shadelight[2];
		}
	}
	else
	{
		if (shadelight[1] > shadelight[2])
		{
			r_lightlevel->value = 150 * shadelight[1];
		}
		else
		{
			r_lightlevel->value = 150 * shadelight[2];
		}
	}
}
