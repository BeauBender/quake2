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
 * rectangle. N6 adds the rest of gl1's walk (rm_warp.c): the flowing
 * surfaces, which scroll and so are drawn from per-frame vertices in
 * their place in the chain; the warped water, after the chains; the sky
 * box, after the water; and the translucent surfaces
 * (R_DrawAlphaSurfaces, called by R_RenderView). The skipped counts are
 * now the surfaces whose draw was refused. A surface a dynamic light
 * touches is drawn with its static lightmap and counted until N8 adds
 * the light.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "common/mtl_matrix.h"

int r_framecount;     /* used for dlight push checking */
int r_visframecount;  /* bumped when going to a new PVS */
cplane_t frustum[4];
vec3_t vup, vpn, vright, r_origin;

static vec3_t modelorg; /* relative to viewpoint */

/* the surfaces of the current view whose draw was refused, by class, and those drawn without their dynamic light */
static unsigned long skip_sky, skip_warp, skip_trans, skip_flow, dlit_surfs;

/* gl1's r_alpha_surfaces: the translucent surfaces of the walk, the last visited first */
static msurface_t *r_alpha_surfaces;

/* gl1's gl_lms.lightmap_surfaces: the lit surfaces of this frame, per slice */
static msurface_t *lightmap_surfaces[RM_LM_MAX_SLICES];

void
RM_WorldSkipsClear(void)
{
	skip_sky = skip_warp = skip_trans = skip_flow = dlit_surfs = 0;
	r_alpha_surfaces = NULL;
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
			/* just adds to visible sky bounds */
			if (!RM_AddSkySurface(r_worldmodel->world, surf))
			{
				skip_sky++;
			}
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			/*
			 * add to the translucent chain; gl1 also sets the texinfo's image to the
			 * animated one (which its next frame's animation then starts from), kept
			 */
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
			r_alpha_surfaces->texinfo->image = R_TextureAnimation(currententity, surf->texinfo);
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
 * gl1's test of R_RegenAllLightmaps: whether the lightmap of a lit
 * surface of the chain is rebuilt this frame (a style changed since it
 * was recorded, a dynamic light touched it this frame, or it is still
 * dirty from the last one), with smax, tmax and a region inside slice
 * i; 0 for the classes gl1 skips there. It reads only what the rebuild
 * changes after the slice's upload, so both walks of the chain agree.
 */
static qboolean
R_LightmapRebuilt(msurface_t *surf, int i, int nslices, int *smax, int *tmax)
{
	int map;

	if (surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP))
	{
		return false;
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
		return false; /* no dynamic lights affect this surface in this frame */
	}

dynamic_surf:
	return rm_lm_size(surf->extents[0], surf->extents[1], smax, tmax) &&
		rm_lm_region_ok(i, nslices, surf->light_s, surf->light_t, *smax, *tmax);
}

/*
 * from gl1_surf.c: rebuilds the lightmaps whose styles changed since they
 * were built (or that a dynamic light touched this frame or the last),
 * per slice, and uploads the joined rectangle of each changed slice
 * before the chains draw (multitexture path only). gl1's tile-rendering
 * copies are left out (design section 7). The rectangles are inside
 * their slice by the load's checks and are checked again before a
 * write (rm_lm_region_ok). Which surfaces stay dirty and which record
 * their styles as built is decided after the upload (rm_lm_regen_mark):
 * when the slice's region is refused, its rebuilt surfaces stay dirty
 * and record nothing, so the next frame rebuilds and uploads them again.
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
		int uploaded, smax, tmax;
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
			if (!R_LightmapRebuilt(surf, i, w->lmslices, &smax, &tmax))
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

			rm_lm_join(&current, &best);
		}

		if (!affected_lightmap)
		{
			continue;
		}

		/* upload changes */
		uploaded = 0;

		if (rm_lm_region_ok(i, w->lmslices, best.left, best.top, best.right - best.left, best.bottom - best.top))
		{
			base = slice;
			base += (best.top * RM_LM_WIDTH + best.left) * RM_LM_BYTES;

			uploaded = rmb_texture_region(w->lmtex, i, best.left, best.top, best.right - best.left,
					best.bottom - best.top, base, RM_LM_WIDTH * RM_LM_BYTES) != 0;
		}

		/* the same surfaces as above: nothing they test changed in between */
		for (surf = lightmap_surfaces[i], left = r_worldmodel->numsurfaces; surf != 0 && left > 0;
			 surf = surf->lightmapchain, left--)
		{
			int map, dirty, cacheable = 0;

			if (!R_LightmapRebuilt(surf, i, w->lmslices, &smax, &tmax))
			{
				continue;
			}

			for (map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
			{
				if ((surf->styles[map] >= 32) || (surf->styles[map] == 0))
				{
					cacheable = 1;
					break;
				}
			}

			if (rm_lm_regen_mark(uploaded, surf->dlightframe == r_framecount, cacheable, &dirty))
			{
				R_SetCacheState(surf);
			}

			surf->dirty_lightmap = dirty;
		}
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

	/* the flowing surfaces before this run in the chain are drawn before it */
	RM_DynFlush();

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

/* a flowing surface of a chain: its polygon scrolled (R_RenderLightmappedPoly), in the batch of per-frame vertices */
static void
R_DrawFlowing(rm_world_t *w, const image_t *image, const msurface_t *s)
{
	rmb_dyn_t d;

	memset(&d, 0, sizeof(d));
	d.tex = image->texnum;
	d.smp = image->sampler;

	if (r_fullbright->value)
	{
		d.kind = RM_DYN_TEX;
	}
	else
	{
		int obb = rm_overbright(gl1_overbrightbits->value);

		d.kind = RM_DYN_LM;
		d.lm.lmtex = w->lmtex;
		d.lm.slice = s->lightmaptexturenum;
		d.lm.env = rm_lm_env(obb, gl_lightmap->value != 0);
		d.lm.scale = obb ? (float)obb : 1.0f;
	}

	RM_DynState(&d);
	RM_DynSurface();

	if (!RM_DynPoly(w, s, rm_flow_scroll(r_newrefdef.time)))
	{
		skip_flow++;
	}
}

/*
 * from gl1_surf.c, R_DrawTextureChains, the multitexture path that gl1
 * takes on the reference (without the lightmap unit under r_fullbright):
 * first every chain's surfaces that are not warped, in runs of one
 * lightmap slice (a flowing one scrolled, in its place), then the warped
 * ones (R_EmitWaterPolys, one batch per chain), and the chains cleared.
 */
static void
R_DrawTextureChains(const rm_view_t *view)
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

		RM_DynBegin(view, &skip_flow);

		for (s = image->texturechain; s; s = s->texturechain)
		{
			if (s->flags & SURF_DRAWTURB)
			{
				continue;
			}

			if (s->texinfo->flags & SURF_FLOWING)
			{
				/* the run before it is drawn first, and the surface after the flowing ones before it */
				R_FlushChain(w, image, slice, n, view->mvp, &view->vp, view->cull);
				n = 0;
				R_DrawFlowing(w, image, s);
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
				R_FlushChain(w, image, slice, n, view->mvp, &view->vp, view->cull);
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

		R_FlushChain(w, image, slice, n, view->mvp, &view->vp, view->cull);
		RM_DynFlush();
	}

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		long left = r_worldmodel->numsurfaces;
		rmb_dyn_t d;

		if (!image->registration_sequence || !image->texturechain)
		{
			continue;
		}

		/* gl1: GL_COMBINE at scale 1 on white with overbright bits, else GL_MODULATE with the inverse intensity */
		memset(&d, 0, sizeof(d));
		d.kind = rm_warp_color(rm_overbright(gl1_overbrightbits->value) != 0, rm_inverse_intensity_value, d.color) ?
				RM_DYN_TEXC : RM_DYN_TEX;
		d.tex = image->texnum;
		d.smp = image->sampler;

		RM_DynBegin(view, &skip_warp);
		RM_DynState(&d);

		/* a chain holds each surface once (the nodes are a tree: rm_model.c); bounded all the same */
		for (s = image->texturechain; s && (left > 0); s = s->texturechain, left--)
		{
			if (s->flags & SURF_DRAWTURB)
			{
				RM_DynSurface();

				if (!RM_EmitWaterPolys(w, s))
				{
					skip_warp++;
				}
			}
		}

		RM_DynFlush();
		image->texturechain = NULL;
	}
}

/*
 * from gl1_surf.c, R_DrawAlphaSurfaces: the translucent surfaces of the
 * walk, drawn blended with depth writes (GL_SRC_ALPHA,
 * GL_ONE_MINUS_SRC_ALPHA), the inverse intensity in the colour and
 * the alpha of the surface's flags (GL_MODULATE). The walk is front to
 * back and the chain was built by pushing, so it draws back to front.
 */
void
R_DrawAlphaSurfaces(const rm_view_t *view)
{
	const rm_world_t *w = r_worldmodel ? r_worldmodel->world : NULL;
	long left = r_worldmodel ? r_worldmodel->numsurfaces : 0;
	msurface_t *s;

	RM_DynBegin(view, &skip_trans);

	for (s = r_alpha_surfaces; s && (left > 0); s = s->texturechain, left--)
	{
		const image_t *image = (const image_t *)s->texinfo->image;
		rmb_dyn_t d;
		int ok;

		if (!image)
		{
			skip_trans++;
			continue;
		}

		memset(&d, 0, sizeof(d));
		d.kind = RM_DYN_TEXC_BLEND;
		d.tex = image->texnum;
		d.smp = image->sampler;
		rm_trans_color(rm_inverse_intensity_value, rm_trans_alpha(s->texinfo->flags), d.color);

		RM_DynState(&d);
		RM_DynSurface();

		if (s->flags & SURF_DRAWTURB)
		{
			ok = RM_EmitWaterPolys(w, s);
		}
		else
		{
			ok = RM_DynPoly(w, s, (s->texinfo->flags & SURF_FLOWING) ? rm_flow_scroll(r_newrefdef.time) : 0.0f);
		}

		if (!ok)
		{
			skip_trans++;
		}
	}

	RM_DynFlush();
	r_alpha_surfaces = NULL;
}

/* ---- brush entities (N7) ------------------------------------------------------------- */

/*
 * from gl1_surf.c, R_GetBrushesLighting: the lit surfaces of the brush
 * entities in view join the per-slice chains of R_RegenAllLightmaps, so
 * their lightmaps are rebuilt and uploaded with the world's. gl1 tests
 * the facing with the world's modelorg here (it does not set the
 * entity's), kept. Marking the dynamic lights on the entity's nodes is
 * N8's (R_AddDynamicLights). An entity with a float that is not finite is
 * left out, as it is not drawn.
 */
static void
R_GetBrushesLighting(void)
{
	int i, k, n;

	if (!r_drawentities->value || gl1_flashblend->value)
	{
		return;
	}

	n = rm_entity_count(r_newrefdef.num_entities);

	for (i = 0; i < n; i++)
	{
		const entity_t *currententity = &r_newrefdef.entities[i];
		const model_t *currentmodel = currententity->model;
		vec3_t mins, maxs;
		msurface_t *surf;

		if (currententity->flags & RF_BEAM)
		{
			continue;
		}

		if (!currentmodel || (currentmodel->type != mod_brush) || (currentmodel->nummodelsurfaces == 0) ||
			!rm_entity_floats_ok(currententity->origin, currententity->oldorigin, currententity->angles,
				currententity->backlerp, currententity->alpha))
		{
			continue;
		}

		rm_brush_bounds(currententity->origin, currententity->angles, currentmodel->mins, currentmodel->maxs,
				currentmodel->radius, mins, maxs);

		if (r_cull->value && R_CullBox(mins, maxs, frustum))
		{
			continue;
		}

		/* the range is inside the world's surfaces (Mod_LoadSubmodels, rm_range_ok) */
		surf = &r_worldmodel->surfaces[currentmodel->firstmodelsurface];

		for (k = 0; k < currentmodel->nummodelsurfaces; k++, surf++)
		{
			if ((surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)) || (surf->flags & SURF_DRAWTURB) ||
				(surf->lmchain_frame == r_framecount))
			{
				continue; /* either not affected by light, or already in the chain */
			}

			if (rm_brush_face_visible(modelorg, surf->plane->normal, surf->plane->dist, (surf->flags & SURF_PLANEBACK) != 0) &&
				(surf->lightmaptexturenum >= 0) && (surf->lightmaptexturenum < RM_LM_MAX_SLICES))
			{
				surf->lmchain_frame = r_framecount; /* don't add this twice to the chain */
				surf->lightmapchain = lightmap_surfaces[surf->lightmaptexturenum];
				lightmap_surfaces[surf->lightmaptexturenum] = surf;
			}
		}
	}
}

/*
 * from gl1_surf.c, R_DrawInlineBModel on gl1's multitexture path: the
 * surfaces facing the viewer, in model order; the translucent ones join
 * the alpha chain (drawn by R_DrawAlphaSurfaces with the world's matrix,
 * as gl1 draws them), the lit ones in runs of one image and one slice
 * (gl1's buf_mtex batch), a flowing one scrolled and a warped one through
 * R_EmitWaterPolys in its place.
 */
static int
R_DrawInlineBModel(const entity_t *currententity, const model_t *currentmodel, const rm_view_t *view,
		const float *emodelorg)
{
	rm_world_t *w = r_worldmodel->world;
	msurface_t *psurf = &r_worldmodel->surfaces[currentmodel->firstmodelsurface];
	const image_t *run_image = NULL;
	unsigned long n = 0;
	int i, slice = -1, ok = 1;

	RM_DynBegin(view, &skip_flow);

	for (i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
	{
		image_t *image;

		/* find which side of the node we are on */
		if (!rm_brush_face_visible(emodelorg, psurf->plane->normal, psurf->plane->dist, (psurf->flags & SURF_PLANEBACK) != 0))
		{
			continue;
		}

		if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			/* add to the translucent chain */
			psurf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = psurf;
			continue;
		}

		image = R_TextureAnimation(currententity, psurf->texinfo);

		if (!image)
		{
			continue;
		}

		if (!(psurf->flags & SURF_DRAWTURB))
		{
			if (psurf->texinfo->flags & SURF_FLOWING)
			{
				/* the run before it is drawn first */
				if (run_image)
				{
					R_FlushChain(w, run_image, slice, n, view->mvp, &view->vp, view->cull);
				}

				n = 0;
				R_DrawFlowing(w, image, psurf);
				continue;
			}

			/* gl1's R_UpdateGLBuffer(buf_mtex, ...): another image or lightmap draws the batch */
			if (run_image && ((image != run_image) || (!r_fullbright->value && (psurf->lightmaptexturenum != slice))))
			{
				R_FlushChain(w, run_image, slice, n, view->mvp, &view->vp, view->cull);
				n = 0;
			}

			run_image = image;
			slice = psurf->lightmaptexturenum;

			/* the model's surfaces are distinct and their fans fit the scratch together (they are the world's) */
			if (psurf->num_indices > w->size.indices - n)
			{
				ok = 0;
				continue;
			}

			memcpy(w->scratch + n, w->indices + psurf->first_index, psurf->num_indices * sizeof(unsigned));
			n += psurf->num_indices;
		}
		else
		{
			rmb_dyn_t d;

			if (run_image)
			{
				R_FlushChain(w, run_image, slice, n, view->mvp, &view->vp, view->cull);
			}

			n = 0;
			run_image = NULL;

			/* gl1's buf_singletex with SURF_DRAWTURB: the warped water's colour (R_ApplyGLBuffer) */
			memset(&d, 0, sizeof(d));
			d.kind = rm_warp_color(rm_overbright(gl1_overbrightbits->value) != 0, rm_inverse_intensity_value, d.color) ?
					RM_DYN_TEXC : RM_DYN_TEX;
			d.tex = image->texnum;
			d.smp = image->sampler;
			RM_DynState(&d);
			RM_DynSurface();

			if (!RM_EmitWaterPolys(w, psurf))
			{
				skip_warp++;
			}
		}
	}

	if (run_image)
	{
		R_FlushChain(w, run_image, slice, n, view->mvp, &view->vp, view->cull);
	}

	RM_DynFlush();
	return ok;
}

/*
 * from gl1_surf.c, R_DrawBrushModel: culled by its box, its modelorg
 * turned into the entity's axes, drawn with R_RotateForEntity (pitch and
 * roll turned, as gl1 calls it) on the world's matrix. gl_zfix's polygon
 * offset is not honoured (doc/040_cvarlist.md).
 */
int
R_DrawBrushModel(const entity_t *currententity, const model_t *currentmodel, const rm_view_t *view)
{
	vec3_t mins, maxs, emodelorg, forward, right, up;
	float a[3];
	rm_view_t ev;
	int rotated;

	if (currentmodel->nummodelsurfaces == 0)
	{
		return 1;
	}

	if (!r_worldmodel || !r_worldmodel->world || !r_worldmodel->world->scratch ||
		!rm_entity_floats_ok(currententity->origin, currententity->oldorigin, currententity->angles,
			currententity->backlerp, currententity->alpha))
	{
		return 0;
	}

	rotated = rm_brush_bounds(currententity->origin, currententity->angles, currentmodel->mins, currentmodel->maxs,
			currentmodel->radius, mins, maxs);

	if (r_cull->value && R_CullBox(mins, maxs, frustum))
	{
		return 1;
	}

	AngleVectors(currententity->angles, forward, right, up);
	rm_brush_modelorg(r_newrefdef.vieworg, currententity->origin, rotated, forward, right, up, emodelorg);

	ev = *view;
	a[0] = -currententity->angles[0];
	a[1] = currententity->angles[1];
	a[2] = -currententity->angles[2];
	rm_entity_matrix(ev.world, view->world, currententity->origin, a);
	mtl_mat_mul(ev.mvp, view->proj, ev.world);

	return R_DrawInlineBModel(currententity, currentmodel, &ev, emodelorg);
}

/* from gl1_surf.c */
void
R_DrawWorld(const rm_view_t *view)
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
	ent.frame = rm_world_frame(r_newrefdef.time);

	memset(lightmap_surfaces, 0, sizeof(lightmap_surfaces));

	RM_SkyClear();
	R_RecursiveWorldNode(&ent, r_worldmodel->nodes);
	R_GetBrushesLighting();
	R_RegenAllLightmaps();
	R_DrawTextureChains(view);
	skip_sky += (unsigned long)RM_DrawSkyBox(view);
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
