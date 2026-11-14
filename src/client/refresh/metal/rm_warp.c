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
 * From gl1_warp.c and the buffered draws of gl1_buffer.c
 * (docs/plans/ref-metal-native.md, section 21): the sky box, the warped
 * water and the batches of per-frame vertices those and the flowing and
 * translucent surfaces are drawn from. The arithmetic is rm_pure.c's
 * (rm_sky_*, rm_warp_*, rm_flow_scroll), host-tested against gl1's own;
 * this file holds the state (the sky images and rotation, the batch) and
 * the draws. A warped surface's pieces are made once at registration
 * (RM_SubdivideSurface, from R_SubdivideSurface) and kept beside the
 * retained buffers, which hold the flat polygon.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "common/mtl_matrix.h"

/* gl1's sky state: the six images (suffix order), the rotation and the visible bounds */
static image_t *sky_images[6];
static float skyrotate;
static vec3_t skyaxis;
static rm_skybounds_t skybounds;

/* 3dstudio environment map names */
static const char *const suf[6] = {"rt", "bk", "lf", "ft", "up", "dn"};

/* ---- the sky ---------------------------------------------------------------------- */

/*
 * from gl1_warp.c, RI_SetSky. The name comes from the server: one that
 * is not a plain name (rm_sky_name_ok) loads nothing, as a name whose
 * images are not found, and the rotation and axis are taken only when
 * finite; a missing image is r_notexture.
 */
void
RM_SetSky(const char *name, float rotate, vec3_t axis)
{
	int i;
	qboolean named = rm_sky_name_ok(name);

	if (!named)
	{
		R_Printf(PRINT_ALL, "%s: refusing the sky name\n", __func__);
	}

	if (rm_sky_params_ok(rotate, axis))
	{
		skyrotate = rotate;
		VectorCopy(axis, skyaxis);
	}
	else
	{
		skyrotate = 0;
		VectorClear(skyaxis);
	}

	for (i = 0; i < 6; i++)
	{
		image_t *image = NULL;

		if (named)
		{
			image = (image_t *)GetSkyImage(name, suf[i], false, (findimage_t)R_FindImage);

			if (!image)
			{
				R_Printf(PRINT_ALL, "%s: can't load %s:%s sky\n", __func__, name, suf[i]);
			}
		}

		sky_images[i] = image ? image : r_notexture;
	}
}

/* from gl1_warp.c, R_ClearSkyBox */
void
RM_SkyClear(void)
{
	rm_sky_clear(&skybounds);
}

/*
 * from gl1_warp.c, R_AddSkySurface, on the surface's retained polygon
 * relative to the view origin. 1 when the polygon's part of the box was
 * taken; 0 when it was refused (rm_sky_clip: too many vertices).
 */
int
RM_AddSkySurface(const rm_world_t *w, const msurface_t *fa)
{
	float verts[RM_SKY_MAX_VERTS * 3];
	unsigned i;

	if (!w || !w->verts || (fa->num_vertices > RM_SKY_MAX_VERTS - 2) ||
		(fa->first_vertex > w->size.verts) || (fa->num_vertices > w->size.verts - fa->first_vertex))
	{
		return 0;
	}

	/* calculate vertex values for sky box */
	for (i = 0; i < fa->num_vertices; i++)
	{
		const float *p = w->verts[fa->first_vertex + i].pos;

		verts[i * 3] = p[0] - r_origin[0];
		verts[i * 3 + 1] = p[1] - r_origin[1];
		verts[i * 3 + 2] = p[2] - r_origin[2];
	}

	return rm_sky_clip(&skybounds, (int)fa->num_vertices, verts);
}

/*
 * from gl1_warp.c, R_DrawSkyBox: the faces the surfaces showed (all six
 * of a rotating sky), each its own draw of one quad of its image, on the
 * world matrix moved to the view origin and turned by the sky's rotation.
 * Returns the faces that could not be drawn (no texture, no room).
 */
int
RM_DrawSkyBox(const rm_view_t *view)
{
	float mv[16], mvp[16];
	int i, failed = 0;

	if (!rm_sky_prepare(&skybounds, skyrotate))
	{
		return 0; /* nothing visible */
	}

	rm_sky_matrix(mv, view->world, r_origin, r_newrefdef.time, skyrotate, skyaxis);
	mtl_mat_mul(mvp, view->proj, mv);

	for (i = 0; i < 6; i++)
	{
		rm_wvert_t v[4];
		unsigned idx[6];
		rmb_dyn_t d;
		const image_t *image = sky_images[rm_sky_image_of_face(i)];
		float st[2];
		int k;

		if (!rm_sky_face_visible(&skybounds, i))
		{
			continue;
		}

		if (!image || !image->registration_sequence)
		{
			failed++;
			continue;
		}

		memset(v, 0, sizeof(v));

		/* the corners as gl1 emits them: (min, min), (min, max), (max, max), (max, min) */
		for (k = 0; k < 4; k++)
		{
			float s = (k < 2) ? skybounds.mins[0][i] : skybounds.maxs[0][i];
			float t = ((k == 1) || (k == 2)) ? skybounds.maxs[1][i] : skybounds.mins[1][i];

			rm_sky_vec(s, t, i, r_farsee->value != 0, v[k].pos, st);
			v[k].pos[3] = 1.0f;
			v[k].uv[0] = st[0];
			v[k].uv[1] = st[1];
		}

		memset(&d, 0, sizeof(d));
		d.kind = RM_DYN_TEX;
		d.tex = image->texnum;
		d.smp = image->sampler;

		if (!rm_world_fan(0, 4, idx, 6) || !rmb_draw_world_dyn(v, 4, idx, 6, &d, mvp, &view->vp, view->cull))
		{
			failed++;
		}
	}

	return failed;
}

/* ---- the batch of per-frame vertices ------------------------------------------------ */

/*
 * gl1 batches the fans of a texture and a state into one draw
 * (R_UpdateGLBuffer, R_ApplyGLBuffer) and draws them when either
 * changes; the batch here holds the fans of a run and is drawn when the
 * state changes (rm_alpha_break, and the kind or slice), when the next
 * fan does not fit (rm_batch_fits) and at the end of the run.
 */
static struct
{
	rm_wvert_t v[RM_DYN_MAX_VERTS];
	unsigned idx[RM_DYN_MAX_INDICES];
	unsigned nv;
	unsigned long ni;
	rmb_dyn_t state;
	const rm_view_t *view;
	unsigned long *skips;   /* the counter of the surfaces whose batch is refused */
	unsigned long surfs;    /* the surfaces in the batch */
} dyn;

/* draws what is batched; a refused draw counts its surfaces as skipped */
void
RM_DynFlush(void)
{
	if (dyn.nv && dyn.view)
	{
		if (!rmb_draw_world_dyn(dyn.v, dyn.nv, dyn.idx, dyn.ni, &dyn.state, dyn.view->mvp, &dyn.view->vp,
				dyn.view->cull) && dyn.skips)
		{
			*dyn.skips += dyn.surfs;
		}
	}

	dyn.nv = 0;
	dyn.ni = 0;
	dyn.surfs = 0;
}

/* starts a run of surfaces: what is batched is drawn, the counter takes the refused surfaces */
void
RM_DynBegin(const rm_view_t *view, unsigned long *skips)
{
	RM_DynFlush();
	dyn.view = view;
	dyn.skips = skips;
}

/*
 * Sets the state of the next fans: the batch is drawn first when it holds
 * something of another state (gl1's R_UpdateGLBuffer: another texture
 * or alpha; here also another kind, colour or slice).
 */
void
RM_DynState(const rmb_dyn_t *d)
{
	if (rm_alpha_break((long)dyn.nv, dyn.state.tex, dyn.state.color[3], d->tex, d->color[3]) ||
		(dyn.nv && ((dyn.state.kind != d->kind) || (dyn.state.smp != d->smp) ||
			(dyn.state.color[0] != d->color[0]) || (dyn.state.lm.slice != d->lm.slice) ||
			(dyn.state.lm.env != d->lm.env) || (dyn.state.lm.scale != d->lm.scale) ||
			(dyn.state.lm.lmtex != d->lm.lmtex))))
	{
		RM_DynFlush();
	}

	dyn.state = *d;
}

/* one surface of the batch (for the skipped count) */
void
RM_DynSurface(void)
{
	dyn.surfs++;
}

/*
 * Makes room for a fan of n vertices (gl1's R_SetBufferIndices,
 * GL_TRIANGLE_FAN) in the batch, drawing it first when the fan does not
 * fit, and writes its indices: the n slots the caller fills at once, or
 * NULL when n cannot be a fan.
 */
static rm_wvert_t *
RM_DynReserve(unsigned n)
{
	rm_wvert_t *v;
	long made;

	if ((n < 3) || (n > RM_DYN_MAX_VERTS))
	{
		return NULL;
	}

	if (!rm_batch_fits((long)dyn.nv, (long)n, RM_DYN_MAX_VERTS) ||
		!rm_batch_fits((long)dyn.ni, 3 * ((long)n - 2), RM_DYN_MAX_INDICES))
	{
		RM_DynFlush();
	}

	made = rm_world_fan(dyn.nv, (long)n, dyn.idx + dyn.ni, RM_DYN_MAX_INDICES - dyn.ni);

	if (made <= 0)
	{
		return NULL;
	}

	v = dyn.v + dyn.nv;
	dyn.nv += n;
	dyn.ni += (unsigned long)made;
	return v;
}

/*
 * A surface's retained polygon as one fan, its s coordinate moved by
 * scroll (gl1's R_DrawGLPoly and R_RenderLightmappedPoly: v[3] + scroll),
 * keeping the lightmap coordinates for a lightmapped draw.
 */
int
RM_DynPoly(const rm_world_t *w, const msurface_t *fa, float scroll)
{
	rm_wvert_t *fan;
	unsigned i, n = fa->num_vertices;

	if (!w || !w->verts || (fa->first_vertex > w->size.verts) || (n > w->size.verts - fa->first_vertex))
	{
		return 0;
	}

	fan = RM_DynReserve(n);

	if (!fan)
	{
		return 0;
	}

	for (i = 0; i < n; i++)
	{
		fan[i] = w->verts[fa->first_vertex + i];
		fan[i].uv[0] = fan[i].uv[0] + scroll;
	}

	return 1;
}

/* ---- the warped water ---------------------------------------------------------------- */

/*
 * from gl1_warp.c, R_EmitWaterPolys: the pieces of the surface, last
 * emitted first (gl1 chains them so), each vertex's coordinates through
 * the turbulence table. 0 when the surface has none or one cannot be
 * a fan.
 */
int
RM_EmitWaterPolys(const rm_world_t *w, const msurface_t *fa)
{
	const float scroll = rm_warp_scroll(r_newrefdef.time, (fa->texinfo->flags & SURF_FLOWING) != 0);
	unsigned p;
	int ok = 1;

	if (!w || !w->wverts || !w->wpolys || !fa->num_wpolys || (fa->first_wpoly > w->nwpolys) ||
		(fa->num_wpolys > w->nwpolys - fa->first_wpoly))
	{
		return 0;
	}

	for (p = fa->num_wpolys; p > 0; p--)
	{
		const rm_wpoly_t *piece = &w->wpolys[fa->first_wpoly + p - 1];
		rm_wvert_t *fan;
		unsigned i;

		if ((piece->n < 3) || (piece->n > RM_WARP_MAX_VERTS + 2) || (piece->first > w->nwverts) ||
			(piece->n > w->nwverts - piece->first))
		{
			ok = 0;
			continue;
		}

		fan = RM_DynReserve(piece->n);

		if (!fan)
		{
			ok = 0;
			continue;
		}

		for (i = 0; i < piece->n; i++)
		{
			const float *v = w->wverts + ((size_t)piece->first + i) * RM_WARP_FLOATS;

			memset(&fan[i], 0, sizeof(fan[i]));
			fan[i].pos[0] = v[0];
			fan[i].pos[1] = v[1];
			fan[i].pos[2] = v[2];
			fan[i].pos[3] = 1.0f;
			rm_warp_st(v[3], v[4], r_newrefdef.time, scroll, &fan[i].uv[0], &fan[i].uv[1]);
		}

	}

	return ok;
}

/*
 * from gl1_warp.c, R_SubdivideSurface (R_SubdividePolygon in rm_pure.c):
 * converts the edges back to a polygon and cuts it. 1 with the pieces
 * appended to the world's (grown to fit, inside the map's cap); 0 when
 * the polygon, a piece or the totals are refused (the map is then refused).
 */
int
RM_SubdivideSurface(model_t *loadmodel, rm_world_t *w, msurface_t *fa)
{
	static float out[RM_WARP_MAX_OUT * RM_WARP_FLOATS];
	static unsigned len[RM_WARP_MAX_POLYS];
	float verts[RM_WARP_MAX_VERTS * 3];
	unsigned long nverts, at;
	unsigned np, i;
	int k;

	if ((fa->numedges < 3) || (fa->numedges > RM_WARP_MAX_VERTS))
	{
		return 0;
	}

	/* convert edges back to a normal polygon (the edges were checked by the loader) */
	for (k = 0; k < fa->numedges; k++)
	{
		int lindex = loadmodel->surfedges[fa->firstedge + k];
		const float *vec;

		if (lindex > 0)
		{
			vec = loadmodel->vertexes[loadmodel->edges[lindex].v[0]].position;
		}
		else
		{
			vec = loadmodel->vertexes[loadmodel->edges[-lindex].v[1]].position;
		}

		memcpy(&verts[k * 3], vec, 3 * sizeof(float));
	}

	if (!rm_warp_subdivide(verts, fa->numedges, fa->texinfo->vecs[0], fa->texinfo->vecs[1], out, RM_WARP_MAX_OUT,
			len, RM_WARP_MAX_POLYS, &np, &nverts))
	{
		return 0;
	}

	if (!rm_batch_fits((long)w->nwverts, (long)nverts, (long)RM_WARP_MAP_MAX_OUT) ||
		!rm_batch_fits((long)w->nwpolys, (long)np, (long)RM_WARP_MAP_MAX_OUT / 3))
	{
		return 0;
	}

	if (w->nwverts + nverts > w->wverts_cap)
	{
		unsigned long cap = w->wverts_cap ? w->wverts_cap * 2 : 4096;
		float *grown;

		while (cap < w->nwverts + nverts)
		{
			cap *= 2;
		}

		grown = realloc(w->wverts, cap * RM_WARP_FLOATS * sizeof(float));

		if (!grown)
		{
			return 0;
		}

		w->wverts = grown;
		w->wverts_cap = cap;
	}

	if (w->nwpolys + np > w->wpolys_cap)
	{
		unsigned cap = w->wpolys_cap ? w->wpolys_cap * 2 : 1024;
		rm_wpoly_t *grown;

		while (cap < w->nwpolys + np)
		{
			cap *= 2;
		}

		grown = realloc(w->wpolys, (size_t)cap * sizeof(*grown));

		if (!grown)
		{
			return 0;
		}

		w->wpolys = grown;
		w->wpolys_cap = cap;
	}

	memcpy(w->wverts + w->nwverts * RM_WARP_FLOATS, out, nverts * RM_WARP_FLOATS * sizeof(float));
	fa->first_wpoly = w->nwpolys;
	fa->num_wpolys = np;

	for (i = 0, at = w->nwverts; i < np; i++)
	{
		w->wpolys[w->nwpolys + i].first = (unsigned)at;
		w->wpolys[w->nwpolys + i].n = len[i];
		at += len[i];
	}

	w->nwverts += nverts;
	w->nwpolys += np;
	return 1;
}
