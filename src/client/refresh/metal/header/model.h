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
 * The model, surface and world types of ref_metal, from gl1's
 * header/model.h (docs/plans/ref-metal-native.md, sections 2 and 9). A
 * surface has no glpoly_t: its polygon is a range of the map's retained
 * vertex buffer and its fan a range of the index buffer, built once at
 * registration (rm_model.c). The world model owns the buffers; its
 * submodels are index ranges of them.
 *
 * =======================================================================
 */

#ifndef RM_MODEL_H
#define RM_MODEL_H

typedef struct msurface_s
{
	int visframe; /* should be drawn when node is crossed */

	cplane_t *plane;
	int flags;

	int firstedge;          /* look up in model->surfedges[], negative numbers */
	int numedges;           /* are backwards edges */

	short texturemins[2];
	short extents[2];

	int light_s, light_t;           /* lightmap coordinates in its slice (N5) */
	int dlight_s, dlight_t;

	/* the retained buffers: the polygon's vertices and its fan */
	unsigned first_vertex, num_vertices;
	unsigned first_index, num_indices;

	/* a warped surface's pieces, a range of the world's (N6) */
	unsigned first_wpoly, num_wpolys;

	struct msurface_s *texturechain;
	struct msurface_s *lightmapchain;
	int lmchain_frame;

	mtexinfo_t *texinfo;

	/* lighting info (N5) */
	int dlightframe;
	int dlightbits;
	qboolean dirty_lightmap;

	int lightmaptexturenum;     /* the lightmap slice, from 0 (gl1's block number less 1); -1 without one */
	byte styles[MAXLIGHTMAPS];
	float cached_light[MAXLIGHTMAPS];
	byte *samples;
} msurface_t;

/* One piece of a warped surface: a fan of n vertices (RM_WARP_FLOATS floats each) from first of the world's */
typedef struct
{
	unsigned first, n;
} rm_wpoly_t;

/* The retained world of one map: CPU copies and the back end's static buffers. */
typedef struct
{
	rm_world_size_t size;
	rm_wvert_t *verts;
	unsigned *indices;
	rm_surfrec_t *recs;
	unsigned *scratch;      /* a frame's indices of one texture chain, size.indices entries */
	int vbuf, ibuf, sbuf;   /* rmb_buffer_static ids, 0 when not made */
	int lmtex;              /* the lightmap array (rmb_texture_array), 0 when not made */
	int lmslices;           /* its slices */
	/* the warped surfaces' pieces (N6): x, y, z, s, t per vertex, the pieces' ranges, and their capacities */
	float *wverts;
	rm_wpoly_t *wpolys;
	unsigned long nwverts, wverts_cap;
	unsigned nwpolys, wpolys_cap;
} rm_world_t;

typedef struct model_s
{
	char name[MAX_QPATH];

	int registration_sequence;

	modtype_t type;
	int numframes;

	int flags;

	/* volume occupied by the model graphics */
	vec3_t mins, maxs;
	float radius;

	/* solid volume for clipping */
	qboolean clipbox;
	vec3_t clipmins, clipmaxs;

	/* brush model */
	int firstmodelsurface, nummodelsurfaces;
	int lightmap; /* only for submodels */

	/* the index range of this (sub)model in the world's index buffer */
	unsigned firstindex, numindices;

	int numsubmodels;
	struct model_s *submodels;

	int numplanes;
	cplane_t *planes;

	int numleafs; /* number of visible leafs, not counting 0 */
	mleaf_t *leafs;

	int numvertexes;
	mvertex_t *vertexes;

	int numedges;
	medge_t *edges;

	int numnodes;
	int firstnode;
	mnode_t *nodes;

	int numtexinfo;
	mtexinfo_t *texinfo;

	int numsurfaces;
	msurface_t *surfaces;

	int numsurfedges;
	int *surfedges;

	int nummarksurfaces;
	msurface_t **marksurfaces;

	dvis_t *vis;
	int vislen;     /* bytes of vis, for the bounded PVS */

	byte *lightdata;
	int lightlen;

	rm_world_t *world;  /* the world model's retained buffers; NULL for every other model */

	/* for alias models and skins */
	image_t *skins[MAX_MD2SKINS];
	rm_md2_info_t md2;  /* an alias model's counts, from rm_md2_check before the loader ran (N7) */

	int extradatasize;
	void *extradata;
	qboolean loaded;    /* every loader and check returned (rm_slot_state); set last */

	/* submodels */
	vec3_t origin; /* for sounds or lights */
} model_t;

extern model_t *r_worldmodel;
extern int r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

void Mod_Init(void);
void Mod_FreeAll(void);
void Mod_Modellist_f(void);
const byte *Mod_ClusterPVS(int cluster, const model_t *model);

void *Hunk_Begin(int maxsize);
void *Hunk_Alloc(int size);
int Hunk_End(void);
void Hunk_Free(void *base);

void RI_BeginRegistration(const char *map);
struct model_s *RI_RegisterModel(const char *name);
void RI_EndRegistration(void);

#endif /* RM_MODEL_H */
