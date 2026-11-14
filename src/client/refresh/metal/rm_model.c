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
 * Model registration and the BSP, from gl1_model.c
 * (docs/plans/ref-metal-native.md, sections 2 and 9), and the retained
 * world buffers built from it: one vertex buffer (float3 position at
 * offset 0, the diffuse and the lightmap coordinates of gl1's
 * LM_BuildPolygonFromSurface), one uint32 index buffer of the fans
 * (gl1's R_SetBufferIndices order) and one record per surface, in
 * surface order, so each submodel is a contiguous index range. Alias and
 * sprite models load through files/models.c; nothing draws them yet (N7).
 *
 * The BSP is untrusted (plan section 2, "Inputs and bounds"). The shared
 * loaders in files/ check lump sizes against their record sizes, plane,
 * child and leaf numbers of nodes, and (here, as gl1 does) a face's plane
 * and texinfo, a leaf's marksurfaces and a marksurface's surface. They
 * do not check a lump against the file, a texinfo's next or name, the
 * visibility offsets, an edge's vertices or a surfedge's edge; those are
 * checked here before a loader or the buffer builder reads them, with
 * the plain functions of rm_pure.c (host-tested). A failed check refuses
 * the map with ri.Sys_Error(ERR_DROP) after the model is freed.
 *
 * =======================================================================
 */

#include "header/local.h"

#define MAX_MOD_KNOWN 512

static int modfilelen;
static void *mod_filebuf;       /* the file a brush model is loading from, freed on a refusal */
static YQ2_ALIGNAS_TYPE(int) byte mod_novis[MAX_MAP_LEAFS / 8];
static model_t mod_known[MAX_MOD_KNOWN];
static int mod_numknown;
static int mod_max = 0;

model_t *r_worldmodel;
int r_viewcluster, r_viewcluster2, r_oldviewcluster, r_oldviewcluster2;

static void Mod_LoadBrushModel(model_t *mod, void *buffer, int modfilelen);

/* the retained buffers of a world, and the world itself */
static void
RM_FreeWorld(rm_world_t *w)
{
	if (!w)
	{
		return;
	}

	rmb_buffer_free(w->vbuf);
	rmb_buffer_free(w->ibuf);
	rmb_buffer_free(w->sbuf);
	free(w->verts);
	free(w->indices);
	free(w->recs);
	free(w->scratch);
	free(w);
}

/*
 * Refuses the brush model being loaded: frees it (its hunk, its file and
 * its retained buffers) so the slot does not keep a half-loaded map, then
 * drops to the console.
 */
static void
RM_Refuse(model_t *mod, const char *fmt, ...) PRINTF_ATTR(2, 3);

static void
RM_Refuse(model_t *mod, const char *fmt, ...)
{
	char text[256], name[MAX_QPATH];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(text, sizeof(text), fmt, ap);
	va_end(ap);

	Q_strlcpy(name, mod->name, sizeof(name));
	RM_FreeWorld(mod->world);
	mod->world = NULL;

	if (mod->extradata)
	{
		Hunk_End();   /* stores the hunk's size, which Hunk_Free unmaps */
		Hunk_Free(mod->extradata);
	}

	if (mod_filebuf)
	{
		ri.FS_FreeFile(mod_filebuf);
		mod_filebuf = NULL;
	}

	memset(mod, 0, sizeof(*mod));
	ri.Sys_Error(ERR_DROP, "%s: %s", name, text);
}

/* from gl1_model.c */
static qboolean
Mod_HasFreeSpace(void)
{
	int i, used = 0;
	model_t *mod;

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (mod->registration_sequence == registration_sequence)
		{
			used++;
		}
	}

	if (mod_max < used)
	{
		mod_max = used;
	}

	/* should same size of free slots as currently used */
	return (mod_numknown + mod_max) < MAX_MOD_KNOWN;
}

/* from gl1_model.c, with the bounded decompression (the offset is checked at load) */
const byte *
Mod_ClusterPVS(int cluster, const model_t *model)
{
	static YQ2_ALIGNAS_TYPE(int) byte row[MAX_MAP_LEAFS / 8];
	size_t ofs;

	if ((cluster == -1) || !model->vis || (cluster < 0) || (cluster >= model->vis->numclusters))
	{
		return mod_novis;
	}

	ofs = (size_t)model->vis->bitofs[cluster][DVIS_PVS];
	rm_decompress_vis((const byte *)model->vis + ofs, (size_t)model->vislen - ofs,
			((size_t)model->vis->numclusters + 7) >> 3, row, sizeof(row));
	return row;
}

/* from gl1_model.c */
void
Mod_Modellist_f(void)
{
	int i, total = 0, used = 0;
	model_t *mod;
	qboolean freeup;

	R_Printf(PRINT_ALL, "Loaded models:\n");

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		char *in_use = "";

		if (mod->registration_sequence == registration_sequence)
		{
			in_use = "*";
			used++;
		}

		if (!mod->name[0])
		{
			continue;
		}

		R_Printf(PRINT_ALL, "%8i : %s %s\n", mod->extradatasize, mod->name, in_use);
		total += mod->extradatasize;
	}

	R_Printf(PRINT_ALL, "Total resident: %i\n", total);
	freeup = Mod_HasFreeSpace();
	R_Printf(PRINT_ALL, "Used %d of %d models%s.\n", used, mod_max, freeup ? ", has free space" : "");
}

void
Mod_Init(void)
{
	mod_max = 0;
	memset(mod_novis, 0xff, sizeof(mod_novis));
}

/* from gl1_model.c: loads in a model for the given name */
static model_t *
Mod_ForName(const char *name, model_t *parent_model, qboolean crash)
{
	model_t *mod;
	void *buf;
	int i;

	if (!name[0])
	{
		ri.Sys_Error(ERR_DROP, "%s: NULL name", __func__);
	}

	/* inline models are grabbed only from worldmodel */
	if ((name[0] == '*') && parent_model)
	{
		i = (int)strtol(name + 1, (char **)NULL, 10);

		if ((i < 1) || (i >= parent_model->numsubmodels))
		{
			ri.Sys_Error(ERR_DROP, "%s: bad inline model number", __func__);
		}

		return &parent_model->submodels[i];
	}

	/* search the currently loaded models */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (mod->name[0] && !strcmp(mod->name, name))
		{
			return mod;
		}
	}

	/* find a free model slot spot */
	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			break;
		}
	}

	if (i == mod_numknown)
	{
		if (mod_numknown == MAX_MOD_KNOWN)
		{
			ri.Sys_Error(ERR_DROP, "%s: mod_numknown == MAX_MOD_KNOWN", __func__);
		}

		mod_numknown++;
	}

	if (strlen(name) >= sizeof(mod->name))
	{
		ri.Sys_Error(ERR_DROP, "%s: \"%s\" is too long", __func__, name);
	}

	strcpy(mod->name, name);

	/* load the file */
	modfilelen = ri.FS_LoadFile(mod->name, (void **)&buf);

	if (!buf)
	{
		if (crash)
		{
			ri.Sys_Error(ERR_DROP, "%s: %s not found", __func__, mod->name);
		}

		memset(mod->name, 0, sizeof(mod->name));
		return NULL;
	}

	if (modfilelen < 4)
	{
		ri.FS_FreeFile(buf);
		memset(mod->name, 0, sizeof(mod->name));
		ri.Sys_Error(ERR_DROP, "%s: %s is too short", __func__, name);
	}

	/* call the apropriate loader */
	switch (LittleLong(*(unsigned *)buf))
	{
		case IDALIASHEADER:
			mod->extradata = Mod_LoadMD2(mod->name, buf, modfilelen, mod->mins, mod->maxs,
					(struct image_s **)mod->skins, (findimage_t)R_FindImage, &(mod->type));

			if (!mod->extradata)
			{
				ri.Sys_Error(ERR_DROP, "%s: Failed to load %s", __func__, mod->name);
			}

			break;

		case IDSPRITEHEADER:
			mod->extradata = Mod_LoadSP2(mod->name, buf, modfilelen,
					(struct image_s **)mod->skins, (findimage_t)R_FindImage, &(mod->type));

			if (!mod->extradata)
			{
				ri.Sys_Error(ERR_DROP, "%s: Failed to load %s", __func__, mod->name);
			}

			break;

		case IDBSPHEADER:
			mod_filebuf = buf;
			Mod_LoadBrushModel(mod, buf, modfilelen);
			mod_filebuf = NULL;
			break;

		default:
			ri.Sys_Error(ERR_DROP, "%s: unknown fileid for %s", __func__, mod->name);
			break;
	}

	mod->extradatasize = Hunk_End();
	ri.FS_FreeFile(buf);

	return mod;
}

/* the lump's record count, after RM_CheckLumps: at most limit, or the map is refused */
static int
RM_LumpCount(model_t *mod, const lump_t *l, size_t size, int limit, const char *what)
{
	int count = (int)((size_t)l->filelen / size);

	if (count > limit)
	{
		RM_Refuse(mod, "%d %s, more than %d", count, what, limit);
	}

	return count;
}

/* from gl1_model.c, with the submodel's index range of the retained buffers */
static void
Mod_LoadSubmodels(model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dmodel_t *in;
	model_t *out;
	int i, j, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		RM_Refuse(loadmodel, "%s: funny lump size", __func__);
	}

	count = RM_LumpCount(loadmodel, l, sizeof(*in), MAX_MAP_MODELS, "models");
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->submodels = out;
	loadmodel->numsubmodels = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		long first, num, k;

		if (i == 0)
		{
			/* copy parent as template for first model */
			memcpy(out, loadmodel, sizeof(*out));
		}
		else
		{
			/* copy first as template for model */
			memmove(out, loadmodel->submodels, sizeof(*out));
		}

		out->world = NULL;   /* the buffers are the world model's */
		Com_sprintf(out->name, sizeof(out->name), "*%d", i);

		for (j = 0; j < 3; j++)
		{
			/* spread the mins / maxs by a pixel */
			out->mins[j] = LittleFloat(in->mins[j]) - 1;
			out->maxs[j] = LittleFloat(in->maxs[j]) + 1;
			out->origin[j] = LittleFloat(in->origin[j]);
		}

		out->radius = Mod_RadiusFromBounds(out->mins, out->maxs);
		out->firstnode = LittleLong(in->headnode);
		out->firstmodelsurface = LittleLong(in->firstface);
		out->nummodelsurfaces = LittleLong(in->numfaces);
		/* visleafs */
		out->numleafs = 0;

		/*
		 * check limits; a negative headnode is a leaf (-1 - leafnum), which
		 * maps hold for submodels without faces (base2's *15), so gl1's
		 * check of the upper bound alone is kept; N8 must not mark lights
		 * from such a node (gl1's R_MarkLights reads nodes + firstnode)
		 */
		if (out->firstnode >= loadmodel->numnodes)
		{
			RM_Refuse(loadmodel, "%s: Inline model %i has bad firstnode", __func__, i);
		}

		first = out->firstmodelsurface;
		num = out->nummodelsurfaces;

		if (!rm_range_ok(first, num, loadmodel->numsurfaces))
		{
			RM_Refuse(loadmodel, "%s: Inline model %i has bad faces", __func__, i);
		}

		/* surfaces are in buffer order, so the range is contiguous */
		out->firstindex = num ? loadmodel->surfaces[first].first_index : 0;
		out->numindices = 0;

		for (k = first; k < first + num; k++)
		{
			out->numindices += loadmodel->surfaces[k].num_indices;
		}
	}
}

/* from gl1_model.c: fills in s->texturemins[] and s->extents[] (the edges are checked) */
static void
Mod_CalcSurfaceExtents(model_t *loadmodel, msurface_t *s)
{
	float mins[2], maxs[2], val;
	int i, j, e;
	mvertex_t *v;
	mtexinfo_t *tex;
	int bmins[2], bmaxs[2];

	mins[0] = mins[1] = 999999;
	maxs[0] = maxs[1] = -99999;

	tex = s->texinfo;

	for (i = 0; i < s->numedges; i++)
	{
		e = loadmodel->surfedges[s->firstedge + i];

		if (e >= 0)
		{
			v = &loadmodel->vertexes[loadmodel->edges[e].v[0]];
		}
		else
		{
			v = &loadmodel->vertexes[loadmodel->edges[-e].v[1]];
		}

		for (j = 0; j < 2; j++)
		{
			val = v->position[0] * tex->vecs[j][0] +
				  v->position[1] * tex->vecs[j][1] +
				  v->position[2] * tex->vecs[j][2] +
				  tex->vecs[j][3];

			if (val < mins[j])
			{
				mins[j] = val;
			}

			if (val > maxs[j])
			{
				maxs[j] = val;
			}
		}
	}

	for (i = 0; i < 2; i++)
	{
		bmins[i] = floor(mins[i] / 16);
		bmaxs[i] = ceil(maxs[i] / 16);

		s->texturemins[i] = bmins[i] * 16;
		s->extents[i] = (bmaxs[i] - bmins[i]) * 16;
	}
}

/* every surfedge of a face names an edge, and both vertices of that edge exist */
static void
RM_CheckFaceEdges(model_t *loadmodel, int surfnum, int firstedge, int numedges)
{
	int i;

	if (!rm_face_edges_ok(firstedge, numedges, loadmodel->numsurfedges))
	{
		RM_Refuse(loadmodel, "face %d has %d edges from %d, the map has %d", surfnum,
				numedges, firstedge, loadmodel->numsurfedges);
	}

	for (i = 0; i < numedges; i++)
	{
		int e = loadmodel->surfedges[firstedge + i];
		const medge_t *edge;

		if (!rm_surfedge_ok(e, loadmodel->numedges))
		{
			RM_Refuse(loadmodel, "face %d names edge %d, the map has %d", surfnum, e, loadmodel->numedges);
		}

		edge = &loadmodel->edges[e >= 0 ? e : -e];

		if ((edge->v[0] >= loadmodel->numvertexes) || (edge->v[1] >= loadmodel->numvertexes))
		{
			RM_Refuse(loadmodel, "face %d has an edge past the %d vertices", surfnum, loadmodel->numvertexes);
		}
	}
}

/* from gl1_model.c; the lightmaps are N5's, the warp subdivision N6's */
static void
Mod_LoadFaces(model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dface_t *in;
	msurface_t *out;
	int i, count, surfnum;
	int planenum, side;
	int ti;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		RM_Refuse(loadmodel, "%s: funny lump size", __func__);
	}

	count = RM_LumpCount(loadmodel, l, sizeof(*in), RM_MAX_MAP_FACES, "faces");
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->surfaces = out;
	loadmodel->numsurfaces = count;

	for (surfnum = 0; surfnum < count; surfnum++, in++, out++)
	{
		memset(out, 0, sizeof(*out));
		out->firstedge = LittleLong(in->firstedge);
		out->numedges = LittleShort(in->numedges);
		out->flags = 0;

		RM_CheckFaceEdges(loadmodel, surfnum, out->firstedge, out->numedges);

		planenum = LittleShort(in->planenum);
		side = LittleShort(in->side);

		if (side)
		{
			out->flags |= SURF_PLANEBACK;
		}

		if ((planenum < 0) || (planenum >= loadmodel->numplanes))
		{
			RM_Refuse(loadmodel, "%s: Incorrect %d planenum.", __func__, planenum);
		}

		out->plane = loadmodel->planes + planenum;

		ti = LittleShort(in->texinfo);

		if ((ti < 0) || (ti >= loadmodel->numtexinfo))
		{
			RM_Refuse(loadmodel, "%s: bad texinfo number", __func__);
		}

		out->texinfo = loadmodel->texinfo + ti;

		Mod_CalcSurfaceExtents(loadmodel, out);

		/* lighting info */
		for (i = 0; i < MAXLIGHTMAPS; i++)
		{
			out->styles[i] = in->styles[i];
		}

		i = LittleLong(in->lightofs);

		/* gl1 takes any offset; one outside the lighting lump reads nothing here */
		out->samples = ((i >= 0) && (i < loadmodel->lightlen)) ? loadmodel->lightdata + i : NULL;

		/* set the drawing flags */
		if (out->texinfo->flags & SURF_WARP)
		{
			out->flags |= SURF_DRAWTURB;

			for (i = 0; i < 2; i++)
			{
				out->extents[i] = 16384;
				out->texturemins[i] = -8192;
			}
		}

		if (r_fixsurfsky->value)
		{
			if (out->texinfo->flags & SURF_SKY)
			{
				out->flags |= SURF_DRAWSKY;
			}
		}
	}
}

/*
 * The retained world buffers (design section 9), from gl1's
 * LM_BuildPolygonFromSurface (gl1_lightmap.c) for every surface, warped
 * and sky ones included (they stay unwarped here; N6 draws them): the
 * counts are checked against their limits and the buffers' capacity
 * (rm_world_add, rm_world_bytes) before anything is allocated or written.
 */
static void
RM_BuildWorld(model_t *mod)
{
	rm_world_size_t size;
	size_t vb, ib, sb;
	unsigned long vat = 0, iat = 0;
	rm_world_t *w;
	int s;

	memset(&size, 0, sizeof(size));

	for (s = 0; s < mod->numsurfaces; s++)
	{
		if (!rm_world_add(&size, mod->surfaces[s].numedges))
		{
			RM_Refuse(mod, "surface %d of %d vertices passes the retained buffers' limits", s,
					mod->surfaces[s].numedges);
		}
	}

	if (!rm_world_bytes(&size, RMB_STATIC_MAX_BYTES, &vb, &ib, &sb))
	{
		RM_Refuse(mod, "the retained buffers would pass %u bytes", RMB_STATIC_MAX_BYTES);
	}

	w = calloc(1, sizeof(*w));

	if (!w)
	{
		RM_Refuse(mod, "out of memory for the retained buffers");
	}

	mod->world = w;
	w->size = size;
	w->verts = malloc(vb ? vb : 1);
	w->indices = malloc(ib ? ib : 1);
	w->recs = malloc(sb ? sb : 1);
	w->scratch = malloc(ib ? ib : 1);

	if (!w->verts || !w->indices || !w->recs || !w->scratch)
	{
		RM_Refuse(mod, "out of memory for the retained buffers");
	}

	for (s = 0; s < mod->numsurfaces; s++)
	{
		msurface_t *fa = &mod->surfaces[s];
		const mtexinfo_t *tex = fa->texinfo;
		rm_surfrec_t *rec = &w->recs[s];
		int i, lindex, lnumverts = fa->numedges;
		long n;

		fa->first_vertex = (unsigned)vat;
		fa->num_vertices = (unsigned)lnumverts;

		for (i = 0; i < lnumverts; i++)
		{
			rm_wvert_t *v = &w->verts[vat + i];
			medge_t *r_pedge;
			float *vec, s1, t1;

			lindex = mod->surfedges[fa->firstedge + i];

			if (lindex > 0)
			{
				r_pedge = &mod->edges[lindex];
				vec = mod->vertexes[r_pedge->v[0]].position;
			}
			else
			{
				r_pedge = &mod->edges[-lindex];
				vec = mod->vertexes[r_pedge->v[1]].position;
			}

			s1 = DotProduct(vec, tex->vecs[0]) + tex->vecs[0][3];
			s1 /= tex->image->width;

			t1 = DotProduct(vec, tex->vecs[1]) + tex->vecs[1][3];
			t1 /= tex->image->height;

			VectorCopy(vec, v->pos);
			v->pos[3] = 1.0f;
			v->uv[0] = s1;
			v->uv[1] = t1;

			/* lightmap texture coordinates (light_s and light_t are N5's, 0 until then) */
			s1 = DotProduct(vec, tex->vecs[0]) + tex->vecs[0][3];
			s1 -= fa->texturemins[0];
			s1 += fa->light_s * 16;
			s1 += 8;
			s1 /= RM_LM_BLOCK_WIDTH * 16;

			t1 = DotProduct(vec, tex->vecs[1]) + tex->vecs[1][3];
			t1 -= fa->texturemins[1];
			t1 += fa->light_t * 16;
			t1 += 8;
			t1 /= RM_LM_BLOCK_HEIGHT * 16;

			v->lmuv[0] = s1;
			v->lmuv[1] = t1;
		}

		n = rm_world_fan(vat, lnumverts, w->indices + iat, size.indices - iat);

		if (n <= 0)
		{
			RM_Refuse(mod, "surface %d does not fit the index buffer", s);
		}

		fa->first_index = (unsigned)iat;
		fa->num_indices = (unsigned)n;

		rm_surf_normal(fa->plane->normal, fa->flags & SURF_PLANEBACK, rec->normal);
		rec->material = rm_material(tex->flags);
		rec->texflags = (unsigned)tex->flags;
		rec->image = (int)((image_t *)tex->image - gltextures);
		rec->lm_slice = -1;
		memset(rec->lm_rect, 0, sizeof(rec->lm_rect));
		memcpy(rec->styles, fa->styles, sizeof(rec->styles));
		rec->first_index = fa->first_index;
		rec->num_indices = fa->num_indices;

		vat += (unsigned long)lnumverts;
		iat += (unsigned long)n;
	}

	/* the static buffers, private, written through the upload command buffer */
	if (vb && (!(w->vbuf = rmb_buffer_static(w->verts, vb)) || !(w->ibuf = rmb_buffer_static(w->indices, ib)) ||
		!(w->sbuf = rmb_buffer_static(w->recs, sb))))
	{
		RM_Refuse(mod, "the retained buffers could not be made");
	}

	if (metal_stats->value)
	{
		char line[RM_STATS_LINE];

		if (rm_world_format(mod->name, &w->size, line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}
}

/* from gl1_model.c; the cluster and the area are checked (R_MarkLeaves and R_AreaVisible index by them) */
static void
Mod_LoadLeafs(model_t *loadmodel, byte *mod_base, lump_t *l)
{
	dleaf_t *in;
	mleaf_t *out;
	int i, j, count, p;
	int numclusters = loadmodel->vis ? loadmodel->vis->numclusters : 0;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		RM_Refuse(loadmodel, "%s: funny lump size", __func__);
	}

	/* Mod_LoadNodes numbers the leafs in arrays of MAX_MAP_LEAFS */
	count = RM_LumpCount(loadmodel, l, sizeof(*in), MAX_MAP_LEAFS, "leafs");
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->leafs = out;
	loadmodel->numleafs = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		unsigned firstleafface;

		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = LittleShort(in->mins[j]);
			out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
		}

		p = LittleLong(in->contents);
		out->contents = p;

		out->cluster = LittleShort(in->cluster);
		out->area = LittleShort(in->area);

		if (!rm_leaf_ok(out->cluster, out->area, numclusters, loadmodel->vis != NULL))
		{
			RM_Refuse(loadmodel, "leaf %d has cluster %d and area %d", i, out->cluster, out->area);
		}

		/* make unsigned long from signed short */
		firstleafface = LittleShort(in->firstleafface) & 0xFFFF;
		out->nummarksurfaces = LittleShort(in->numleaffaces) & 0xFFFF;

		out->firstmarksurface = loadmodel->marksurfaces + firstleafface;

		if ((firstleafface + out->nummarksurfaces) > loadmodel->nummarksurfaces)
		{
			RM_Refuse(loadmodel, "%s: wrong marksurfaces position", __func__);
		}
	}
}

/* from gl1_model.c */
static void
Mod_LoadMarksurfaces(model_t *loadmodel, byte *mod_base, lump_t *l)
{
	int i, j, count;
	short *in;
	msurface_t **out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		RM_Refuse(loadmodel, "%s: funny lump size", __func__);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	loadmodel->marksurfaces = out;
	loadmodel->nummarksurfaces = count;

	for (i = 0; i < count; i++)
	{
		j = LittleShort(in[i]);

		if ((j < 0) || (j >= loadmodel->numsurfaces))
		{
			RM_Refuse(loadmodel, "%s: bad surface number", __func__);
		}

		out[i] = loadmodel->surfaces + j;
	}
}

/*
 * The checks before the shared loaders read the file: every lump inside
 * it, the counts the loaders size arrays by within the limits, the
 * texinfo names terminated and their chains bounded, the visibility
 * offsets inside their lump.
 */
static void
RM_CheckLumps(model_t *mod, byte *mod_base, const dheader_t *header, int len)
{
	static int next[MAX_MAP_TEXINFO];
	const lump_t *l;
	int i;

	for (i = 0; i < HEADER_LUMPS; i++)
	{
		if (!rm_lump_ok(header->lumps[i].fileofs, header->lumps[i].filelen, len))
		{
			RM_Refuse(mod, "lump %d (%d bytes at %d) is not inside the %d bytes of the file", i,
					header->lumps[i].filelen, header->lumps[i].fileofs, len);
		}
	}

	/* the loaders' arrays: Mod_LoadNodes numbers leafs and nodes, the edges hold 16-bit vertices */
	RM_LumpCount(mod, &header->lumps[LUMP_NODES], sizeof(dnode_t), MAX_MAP_NODES, "nodes");
	RM_LumpCount(mod, &header->lumps[LUMP_PLANES], sizeof(dplane_t), MAX_MAP_PLANES, "planes");
	RM_LumpCount(mod, &header->lumps[LUMP_VERTEXES], sizeof(dvertex_t), MAX_MAP_VERTS, "vertices");
	RM_LumpCount(mod, &header->lumps[LUMP_EDGES], sizeof(dedge_t), MAX_MAP_EDGES, "edges");
	RM_LumpCount(mod, &header->lumps[LUMP_SURFEDGES], sizeof(int), RM_MAX_MAP_SURFEDGES, "surfedges");
	RM_LumpCount(mod, &header->lumps[LUMP_LEAFFACES], sizeof(short), MAX_MAP_LEAFFACES, "leaf faces");

	if (header->lumps[LUMP_LIGHTING].filelen > MAX_MAP_LIGHTING)
	{
		RM_Refuse(mod, "%d bytes of lighting, more than %d", header->lumps[LUMP_LIGHTING].filelen,
				MAX_MAP_LIGHTING);
	}

	/* texinfo: Mod_LoadTexinfo prints the name with %s and walks nexttexinfo unchecked */
	l = &header->lumps[LUMP_TEXINFO];

	if ((l->filelen % sizeof(texinfo_t)) == 0)
	{
		const texinfo_t *in = (const void *)(mod_base + l->fileofs);
		int count = RM_LumpCount(mod, l, sizeof(texinfo_t), MAX_MAP_TEXINFO, "texinfos");

		for (i = 0; i < count; i++)
		{
			if (!memchr(in[i].texture, 0, sizeof(in[i].texture)))
			{
				RM_Refuse(mod, "texinfo %d has an unterminated texture name", i);
			}

			next[i] = LittleLong(in[i].nexttexinfo);
		}

		if (!rm_texinfo_chains_ok(next, count))
		{
			RM_Refuse(mod, "a texinfo animation chain leaves the lump or never ends");
		}
	}

	/* visibility: Mod_LoadVisibility swaps 2 * numclusters offsets in place */
	l = &header->lumps[LUMP_VISIBILITY];

	if (l->filelen)
	{
		int numclusters;

		if ((l->filelen > MAX_MAP_VISIBILITY) || (l->filelen < 4))
		{
			RM_Refuse(mod, "%d bytes of visibility", l->filelen);
		}

		numclusters = LittleLong(*(const int *)(mod_base + l->fileofs));

		if (!rm_vis_header_ok(numclusters, l->filelen))
		{
			RM_Refuse(mod, "visibility of %d clusters in %d bytes", numclusters, l->filelen);
		}
	}
}

/* after Mod_LoadVisibility: every PVS and PHS offset inside the lump */
static void
RM_CheckVis(model_t *mod)
{
	int i, j;

	if (!mod->vis)
	{
		return;
	}

	for (i = 0; i < mod->vis->numclusters; i++)
	{
		for (j = 0; j < 2; j++)
		{
			if (!rm_range_ok(mod->vis->bitofs[i][j], 0, mod->vislen))
			{
				RM_Refuse(mod, "cluster %d has its visibility at %d, outside %d bytes", i,
						mod->vis->bitofs[i][j], mod->vislen);
			}
		}
	}
}

/* after Mod_LoadNodes: each node's surfaces inside the map, each surface in one node only */
static void
RM_CheckNodes(model_t *mod)
{
	unsigned char *owner = calloc(mod->numsurfaces ? mod->numsurfaces : 1, 1);
	int i;

	if (!owner)
	{
		RM_Refuse(mod, "out of memory");
	}

	for (i = 0; i < mod->numnodes; i++)
	{
		if (!rm_mark_once(owner, mod->numsurfaces, mod->nodes[i].firstsurface, mod->nodes[i].numsurfaces))
		{
			free(owner);
			RM_Refuse(mod, "node %d has surfaces %d to %d of %d, or one of another node", i,
					mod->nodes[i].firstsurface, mod->nodes[i].firstsurface + mod->nodes[i].numsurfaces,
					mod->numsurfaces);
		}
	}

	free(owner);
}

/* from gl1_model.c; the hunk holds no polygons (they are the retained buffers) */
static void
Mod_LoadBrushModel(model_t *mod, void *buffer, int modfilelen)
{
	int i, hunkSize = 0;
	dheader_t *header;
	byte *mod_base;

	if (mod != mod_known)
	{
		ri.Sys_Error(ERR_DROP, "%s: Loaded a brush model after the world", __func__);
	}

	if (modfilelen < (int)sizeof(dheader_t))
	{
		RM_Refuse(mod, "%d bytes, too short for a BSP header", modfilelen);
	}

	header = (dheader_t *)buffer;

	i = LittleLong(header->version);

	if (i != BSPVERSION)
	{
		RM_Refuse(mod, "%s: wrong version number (%i should be %i)", __func__, i, BSPVERSION);
	}

	/* swap all the lumps */
	mod_base = (byte *)header;

	for (i = 0; i < (int)(sizeof(dheader_t) / 4); i++)
	{
		((int *)header)[i] = LittleLong(((int *)header)[i]);
	}

	RM_CheckLumps(mod, mod_base, header, modfilelen);

	/* calculate the needed hunksize from the lumps */
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_VERTEXES], sizeof(dvertex_t), sizeof(mvertex_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_EDGES], sizeof(dedge_t), sizeof(medge_t), 0);
	hunkSize += sizeof(medge_t) + 31; /* for count+1 in Mod_LoadEdges() */
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_SURFEDGES], sizeof(int), sizeof(int), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LIGHTING], 1, 1, 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_PLANES], sizeof(dplane_t), sizeof(cplane_t) * 2, 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_TEXINFO], sizeof(texinfo_t), sizeof(mtexinfo_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_FACES], sizeof(dface_t), sizeof(msurface_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFFACES], sizeof(short), sizeof(msurface_t *), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_VISIBILITY], 1, 1, 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_LEAFS], sizeof(dleaf_t), sizeof(mleaf_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_NODES], sizeof(dnode_t), sizeof(mnode_t), 0);
	hunkSize += Mod_CalcLumpHunkSize(&header->lumps[LUMP_MODELS], sizeof(dmodel_t), sizeof(model_t), 0);

	mod->extradata = Hunk_Begin(hunkSize);
	mod->type = mod_brush;

	/* load into heap */
	Mod_LoadVertexes(mod->name, &mod->vertexes, &mod->numvertexes, mod_base,
		&header->lumps[LUMP_VERTEXES], 0);
	Mod_LoadEdges(mod->name, &mod->edges, &mod->numedges,
		mod_base, &header->lumps[LUMP_EDGES], 1);
	Mod_LoadSurfedges(mod->name, &mod->surfedges, &mod->numsurfedges,
		mod_base, &header->lumps[LUMP_SURFEDGES], 0);
	Mod_LoadLighting(&mod->lightdata, mod_base, &header->lumps[LUMP_LIGHTING]);
	mod->lightlen = header->lumps[LUMP_LIGHTING].filelen;
	Mod_LoadPlanes(mod->name, &mod->planes, &mod->numplanes,
		mod_base, &header->lumps[LUMP_PLANES], 0);
	Mod_LoadTexinfo(mod->name, &mod->texinfo, &mod->numtexinfo,
		mod_base, &header->lumps[LUMP_TEXINFO], (findimage_t)R_FindImage,
		r_notexture, 0);
	Mod_LoadFaces(mod, mod_base, &header->lumps[LUMP_FACES]);
	RM_BuildWorld(mod);
	Mod_LoadMarksurfaces(mod, mod_base, &header->lumps[LUMP_LEAFFACES]);
	Mod_LoadVisibility(&mod->vis, mod_base, &header->lumps[LUMP_VISIBILITY]);
	mod->vislen = header->lumps[LUMP_VISIBILITY].filelen;
	RM_CheckVis(mod);
	Mod_LoadLeafs(mod, mod_base, &header->lumps[LUMP_LEAFS]);
	Mod_LoadNodes(mod->name, mod->planes, mod->numplanes, mod->leafs,
		mod->numleafs, &mod->nodes, &mod->numnodes, mod_base,
		&header->lumps[LUMP_NODES]);
	RM_CheckNodes(mod);
	Mod_LoadSubmodels(mod, mod_base, &header->lumps[LUMP_MODELS]);
	mod->numframes = 2; /* regular and alternate animation */
}

static void
Mod_Free(model_t *mod)
{
	RM_FreeWorld(mod->world);
	Hunk_Free(mod->extradata);
	memset(mod, 0, sizeof(*mod));
}

void
Mod_FreeAll(void)
{
	int i;

	for (i = 0; i < mod_numknown; i++)
	{
		if (mod_known[i].extradatasize)
		{
			Mod_Free(&mod_known[i]);
		}
	}

	r_worldmodel = NULL;
}

/* from gl1_model.c: specifies the model that will be used as the world */
void
RI_BeginRegistration(const char *model)
{
	char fullname[MAX_QPATH];
	cvar_t *flushmap;

	R_BeginImageRegistration();   /* gl1's registration_sequence++, shared with the images */
	r_oldviewcluster = -1; /* force markleafs */

	Com_sprintf(fullname, sizeof(fullname), "maps/%s.bsp", model);

	/* explicitly free the old map if different: this guarantees that mod_known[0] is the world map */
	flushmap = ri.Cvar_Get("flushmap", "0", 0);

	if (strcmp(mod_known[0].name, fullname) || flushmap->value)
	{
		Mod_Free(&mod_known[0]);
	}

	r_worldmodel = NULL;
	r_worldmodel = Mod_ForName(fullname, NULL, true);

	r_viewcluster = -1;
}

/* from gl1_model.c */
struct model_s *
RI_RegisterModel(const char *name)
{
	model_t *mod;

	mod = Mod_ForName(name, r_worldmodel, false);

	if (mod)
	{
		mod->registration_sequence = registration_sequence;

		/* register any images used by the models */
		if (mod->type == mod_brush)
		{
			int i;

			for (i = 0; i < mod->numtexinfo; i++)
			{
				((image_t *)mod->texinfo[i].image)->registration_sequence = registration_sequence;
			}
		}
		else
		{
			/* numframes is unused for SP2 but lets set it also */
			mod->numframes = Mod_ReLoadSkins((struct image_s **)mod->skins,
					(findimage_t)R_FindImage, mod->extradata, mod->type);
		}
	}

	return mod;
}

/* from gl1_model.c */
void
RI_EndRegistration(void)
{
	int i;
	model_t *mod;

	if (Mod_HasFreeSpace() && R_ImageHasFreeSpace())
	{
		/* should be enough space for load next maps */
		return;
	}

	for (i = 0, mod = mod_known; i < mod_numknown; i++, mod++)
	{
		if (!mod->name[0])
		{
			continue;
		}

		if (mod->registration_sequence != registration_sequence)
		{
			/* don't need this model */
			Mod_Free(mod);
		}
	}

	R_FreeUnusedImages();
}
