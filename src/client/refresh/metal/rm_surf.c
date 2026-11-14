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
 * The world part of gl1_surf.c (docs/plans/ref-metal-native.md, section
 * 2): R_MarkLeaves, R_RecursiveWorldNode, the texture chains of gl1's
 * multitexture path and R_DrawWorld. A chain is drawn as one indexed
 * draw of the retained vertex buffer, its surfaces' fans copied from the
 * retained index buffer in chain order, which is gl1's draw order (gl1
 * batches by texture and lightmap; the batch boundaries change no pixel).
 * N5 draws the chains lightmapped: one draw per run of a chain in one
 * lightmap slice (gl1's R_UpdateGLBuffer flushes where the lightmap
 * changes), the slice's texel times the diffuse one as gl1's two texture
 * units compute it; with r_fullbright the lightmap unit is off (N4's
 * world_tex). Before the chains, R_RegenAllLightmaps rebuilds the
 * lightmaps whose styles changed and uploads each slice's joined dirty
 * rectangle. Sky, translucent, warped and flowing surfaces are counted
 * and skipped until N6 draws them; a surface a dynamic light touches is
 * drawn with its static lightmap and counted until N8 adds the light.
 *
 * =======================================================================
 */

#include "header/local.h"

int r_framecount;     /* used for dlight push checking */
int r_visframecount;  /* bumped when going to a new PVS */
cplane_t frustum[4];
vec3_t vup, vpn, vright, r_origin;

static vec3_t modelorg; /* relative to viewpoint */

/* the surfaces of the current view not drawn yet, by class, and those drawn without their dynamic light */
static unsigned long skip_sky, skip_warp, skip_trans, skip_flow, dlit_surfs;

/* gl1's gl_lms.lightmap_surfaces: the lit surfaces of this frame, per slice */
static msurface_t *lightmap_surfaces[RM_LM_MAX_SLICES];

void
RM_WorldSkipsClear(void)
{
	skip_sky = skip_warp = skip_trans = skip_flow = dlit_surfs = 0;
}

void
RM_WorldSkips(rmb_stats_t *s)
{
	s->sky_skips = skip_sky;
	s->warp_skips = skip_warp;
	s->trans_skips = skip_trans;
	s->flow_skips = skip_flow;
	s->dlit_surfs = dlit_surfs;
}

/* from gl1_surf.c */
static void
R_RecursiveWorldNode(entity_t *currententity, mnode_t *node)
{
	int c, side, sidebit;
	cplane_t *plane;
	msurface_t *surf, **mark;
	mleaf_t *pleaf;
	float dot;
	image_t *image;

	if (node->contents == CONTENTS_SOLID)
	{
		return; /* solid */
	}

	if (node->visframe != r_visframecount)
	{
		return;
	}

	if (r_cull->value && R_CullBox(node->minmaxs, node->minmaxs + 3, frustum))
	{
		return;
	}

	/* if a leaf node, draw stuff */
	if (node->contents != CONTENTS_NODE)
	{
		pleaf = (mleaf_t *)node;

		/* check for door connected areas */
		if (!R_AreaVisible(r_newrefdef.areabits, pleaf))
		{
			return; /* not visible */
		}

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			}
			while (--c);
		}

		return;
	}

	/* node is just a decision point, so go down the apropriate
	   sides find which side of the node we are on */
	plane = node->plane;

	switch (plane->type)
	{
		case PLANE_X:
			dot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = modelorg[2] - plane->dist;
			break;
		default:
			dot = DotProduct(modelorg, plane->normal) - plane->dist;
			break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

	/* recurse down the children, front side first */
	R_RecursiveWorldNode(currententity, node->children[side]);

	/* draw stuff (the node is visited once and its surfaces are inside the map and in no other node: rm_model.c) */
	for (c = node->numsurfaces, surf = r_worldmodel->surfaces + node->firstsurface; c; c--, surf++)
	{
		if (surf->visframe != r_framecount)
		{
			continue;
		}

		if ((surf->flags & SURF_PLANEBACK) != sidebit)
		{
			continue; /* wrong side */
		}

		if (surf->texinfo->flags & SURF_SKY)
		{
			/* gl1 adds it to the visible sky bounds (R_AddSkySurface): N6 */
			skip_sky++;
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			/* gl1 adds it to the translucent chain (R_DrawAlphaSurfaces): N6 */
			skip_trans++;
		}
		else
		{
			/* the polygon is visible, so add it to the texture sorted chain */
			image = R_TextureAnimation(currententity, surf->texinfo);
			surf->texturechain = image->texturechain;
			image->texturechain = surf;
		}

		/*
		 * needed for R_RegenAllLightmaps(): gl1 chains every surface but the
		 * warped ones, and skips the sky and translucent ones there; a
		 * surface without a lightmap (lightmaptexturenum -1) is never chained
		 */
		if (!(surf->texinfo->flags & SURF_WARP) && (surf->lightmaptexturenum >= 0) &&
			(surf->lightmaptexturenum < RM_LM_MAX_SLICES))
		{
			surf->lightmapchain = lightmap_surfaces[surf->lightmaptexturenum];
			lightmap_surfaces[surf->lightmaptexturenum] = surf;
		}
	}

	/* recurse down the back side */
	R_RecursiveWorldNode(currententity, node->children[!side]);
}

/*
 * from gl1_surf.c: rebuilds the lightmaps whose styles changed since they
 * were built (or that a dynamic light touched this frame or the last),
 * per slice, and uploads the joined rectangle of each changed slice
 * before the chains draw (multitexture path only). gl1's tile-rendering
 * copies are left out (design section 7). The rectangles are inside
 * their slice by the load's checks and are checked again before a
 * write (rm_lm_region_ok).
 */
static void
R_RegenAllLightmaps(void)
{
	rm_world_t *w = r_worldmodel->world;
	int i;

	if (r_fullbright->value || !gl1_dynamic->value || !w || !w->lmtex)
	{
		return;
	}

	for (i = 0; i < w->lmslices; i++)
	{
		rm_lmrect_t current, best;
		msurface_t *surf;
		byte *base, *slice = LM_SliceBuffer(i);
		qboolean affected_lightmap;
		long left = r_worldmodel->numsurfaces;

		if (!lightmap_surfaces[i] || !slice)
		{
			continue;
		}

		affected_lightmap = false;
		best.top = RM_LM_HEIGHT;
		best.left = RM_LM_WIDTH;
		best.bottom = best.right = 0;

		/* a chain holds each surface once (the walk visits it once); bounded all the same */
		for (surf = lightmap_surfaces[i]; surf != 0 && left > 0; surf = surf->lightmapchain, left--)
		{
			int map, smax, tmax;

			if (surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP))
			{
				continue;
			}

			/* Any dynamic lights on this surface? */
			for (map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
			{
				if (r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map])
				{
					goto dynamic_surf;
				}
			}

			/* Surface is considered to have dynamic lights if it had them in the previous frame */
			if ((surf->dlightframe != r_framecount) && !surf->dirty_lightmap)
			{
				continue; /* no dynamic lights affect this surface in this frame */
			}

dynamic_surf:
			if (!rm_lm_size(surf->extents[0], surf->extents[1], &smax, &tmax) ||
				!rm_lm_region_ok(i, w->lmslices, surf->light_s, surf->light_t, smax, tmax))
			{
				continue;
			}

			affected_lightmap = true;

			current.left = surf->light_s;
			current.right = surf->light_s + smax;
			current.top = surf->light_t;
			current.bottom = surf->light_t + tmax;

			base = slice;
			base += (current.top * RM_LM_WIDTH + current.left) * RM_LM_BYTES;

			R_BuildLightMap(surf, base, RM_LM_WIDTH * RM_LM_BYTES);

			surf->dirty_lightmap = (surf->dlightframe == r_framecount);

			if (!surf->dirty_lightmap)
			{
				for (map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
				{
					if ((surf->styles[map] >= 32) || (surf->styles[map] == 0))
					{
						R_SetCacheState(surf);
						break;
					}
				}
			}

			rm_lm_join(&current, &best);
		}

		if (!affected_lightmap ||
			!rm_lm_region_ok(i, w->lmslices, best.left, best.top, best.right - best.left, best.bottom - best.top))
		{
			continue;
		}

		/* upload changes */
		base = slice;
		base += (best.top * RM_LM_WIDTH + best.left) * RM_LM_BYTES;

		rmb_texture_region(w->lmtex, i, best.left, best.top, best.right - best.left, best.bottom - best.top,
				base, RM_LM_WIDTH * RM_LM_BYTES);
	}
}

/* draws the n indices of the scratch gathered for one texture and, lightmapped, one slice */
static void
R_FlushChain(rm_world_t *w, const image_t *image, int slice, unsigned long n, const float *mvp,
		const rm_viewport_t *vp, int cull)
{
	rmb_lm_draw_t d;
	int obb;

	if (!n)
	{
		return;
	}

	if (r_fullbright->value)
	{
		/* gl1 leaves the lightmap unit off: the texel alone */
		rmb_draw_world(w->vbuf, w->scratch, n, image->texnum, image->sampler, mvp, vp, cull);
		return;
	}

	obb = rm_overbright(gl1_overbrightbits->value);
	d.tex = image->texnum;
	d.smp = image->sampler;
	d.lmtex = w->lmtex;
	d.slice = slice;
	d.env = rm_lm_env(obb, gl_lightmap->value != 0);
	d.scale = obb ? (float)obb : 1.0f;
	rmb_draw_world_lm(w->vbuf, w->scratch, n, &d, mvp, vp, cull);
}

/*
 * from gl1_surf.c, R_DrawTextureChains, the multitexture path that gl1
 * takes on the reference (without the lightmap unit under r_fullbright):
 * first every chain's surfaces that are not warped, in runs of one
 * lightmap slice, then the warped ones (skipped here), and the chains
 * cleared.
 */
static void
R_DrawTextureChains(const float *mvp, const rm_viewport_t *vp, int cull)
{
	rm_world_t *w = r_worldmodel->world;
	image_t *image;
	msurface_t *s;
	int i;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		unsigned long n = 0;
		int slice = -1;

		if (!image->registration_sequence || !image->texturechain)
		{
			continue;
		}

		for (s = image->texturechain; s; s = s->texturechain)
		{
			if (s->flags & SURF_DRAWTURB)
			{
				continue;
			}

			if (s->texinfo->flags & SURF_FLOWING)
			{
				skip_flow++;    /* gl1 scrolls its coordinates (R_RenderLightmappedPoly): N6 */
				continue;
			}

			/* a lit surface a dynamic light touches: its static lightmap until N8 */
			if (s->dlightframe == r_framecount)
			{
				dlit_surfs++;
			}

			/* gl1's R_UpdateGLBuffer: another lightmap flushes the batch */
			if (!r_fullbright->value && (s->lightmaptexturenum != slice))
			{
				R_FlushChain(w, image, slice, n, mvp, vp, cull);
				n = 0;
				slice = s->lightmaptexturenum;
			}

			/* each surface is in one node of a tree, so a chain holds it once and fits; never written past */
			if (s->num_indices > w->size.indices - n)
			{
				break;
			}

			memcpy(w->scratch + n, w->indices + s->first_index, s->num_indices * sizeof(unsigned));
			n += s->num_indices;
		}

		R_FlushChain(w, image, slice, n, mvp, vp, cull);
	}

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		long left = r_worldmodel->numsurfaces;

		if (!image->registration_sequence || !image->texturechain)
		{
			continue;
		}

		/* a chain holds each surface once (the nodes are a tree: rm_model.c); bounded all the same */
		for (s = image->texturechain; s && (left > 0); s = s->texturechain, left--)
		{
			if (s->flags & SURF_DRAWTURB)
			{
				skip_warp++;    /* gl1's R_RenderBrushPoly, R_EmitWaterPolys: N6 */
			}
		}

		image->texturechain = NULL;
	}
}

/* from gl1_surf.c, without the sky box (N6) and the brush entities' lighting (R_GetBrushesLighting, N7) */
void
R_DrawWorld(const float *mvp, const rm_viewport_t *vp, int cull)
{
	entity_t ent;

	if (!r_drawworld->value)
	{
		return;
	}

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		return;
	}

	VectorCopy(r_newrefdef.vieworg, modelorg);

	/* auto cycle the world frame for texture animation */
	memset(&ent, 0, sizeof(ent));
	ent.frame = (int)(r_newrefdef.time * 2);

	memset(lightmap_surfaces, 0, sizeof(lightmap_surfaces));

	R_RecursiveWorldNode(&ent, r_worldmodel->nodes);
	R_RegenAllLightmaps();
	R_DrawTextureChains(mvp, vp, cull);
}

/* from gl1_surf.c: marks the leaves and nodes in the PVS of the current cluster */
void
R_MarkLeaves(void)
{
	const byte *vis;
	YQ2_ALIGNAS_TYPE(int) byte fatvis[MAX_MAP_LEAFS / 8];
	mnode_t *node;
	int i, cluster;
	mleaf_t *leaf;

	if ((r_oldviewcluster == r_viewcluster) &&
		(r_oldviewcluster2 == r_viewcluster2) &&
		!r_novis->value &&
		(r_viewcluster != -1))
	{
		return;
	}

	/* development aid to let you run around and see exactly where the pvs ends */
	if (r_lockpvs->value)
	{
		return;
	}

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || (r_viewcluster == -1) || !r_worldmodel->vis)
	{
		/* mark everything */
		for (i = 0; i < r_worldmodel->numleafs; i++)
		{
			r_worldmodel->leafs[i].visframe = r_visframecount;
		}

		for (i = 0; i < r_worldmodel->numnodes; i++)
		{
			r_worldmodel->nodes[i].visframe = r_visframecount;
		}

		return;
	}

	vis = Mod_ClusterPVS(r_viewcluster, r_worldmodel);

	/*
	 * may have to combine two clusters because of solid water boundaries;
	 * gl1 combines (numleafs + 31) / 32 words, of which only the row's
	 * bytes hold clusters (every leaf's cluster is below numclusters)
	 */
	if (r_viewcluster2 != r_viewcluster)
	{
		size_t row = ((size_t)r_worldmodel->vis->numclusters + 7) >> 3, j;

		memcpy(fatvis, vis, row);
		vis = Mod_ClusterPVS(r_viewcluster2, r_worldmodel);

		for (j = 0; j < row; j++)
		{
			fatvis[j] |= vis[j];
		}

		vis = fatvis;
	}

	for (i = 0, leaf = r_worldmodel->leafs; i < r_worldmodel->numleafs; i++, leaf++)
	{
		cluster = leaf->cluster;

		if (cluster == -1)
		{
			continue;
		}

		if (vis[cluster >> 3] & (1 << (cluster & 7)))
		{
			node = (mnode_t *)leaf;

			do
			{
				if (node->visframe == r_visframecount)
				{
					break;
				}

				node->visframe = r_visframecount;
				node = node->parent;
			}
			while (node);
		}
	}
}
