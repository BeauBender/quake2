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
 * The models file format
 *
 * =======================================================================
 */

#include "../ref_shared.h"

/*
=================
Mod_LoadAliasModel/Mod_LoadMD2
=================
*/
void *
Mod_LoadMD2 (const char *mod_name, const void *buffer, int modfilelen,
	vec3_t mins, vec3_t maxs, struct image_s **skins, findimage_t find_image,
	modtype_t *type)
{
	dmdl_t		*pinmodel, *pheader;
	dtriangle_t	*pintri, *pouttri;
	dstvert_t	*pinst, *poutst;
	int		*pincmd, *poutcmd;
	void	*extradata;
	int		version;
	int		ofs_end;
	int		i, j;

	if (modfilelen < (int)sizeof(dmdl_t))
	{
		Com_Printf("%s: model %s is too small (%d bytes)\n",
				__func__, mod_name, modfilelen);
		return NULL;
	}

	pinmodel = (dmdl_t *)buffer;

	version = LittleLong (pinmodel->version);
	if (version != ALIAS_VERSION)
	{
		Com_Printf("%s: %s has wrong version number (%i should be %i)",
				__func__, mod_name, version, ALIAS_VERSION);
		return NULL;
	}

	ofs_end = LittleLong(pinmodel->ofs_end);
	if (ofs_end < 0 || ofs_end > modfilelen)
	{
		Com_Printf("%s: model %s file size(%d) too small, should be %d",
				__func__, mod_name, modfilelen, ofs_end);
		return NULL;
	}

	extradata = Hunk_Begin(modfilelen);
	pheader = Hunk_Alloc(ofs_end);

	// byte swap the header fields and sanity check
	for (i=0 ; i<sizeof(dmdl_t)/sizeof(int) ; i++)
		((int *)pheader)[i] = LittleLong (((int *)buffer)[i]);

	if (pheader->skinheight > MAX_LBM_HEIGHT)
	{
		Com_Printf("%s: model %s has a skin taller than %d",
				__func__, mod_name, MAX_LBM_HEIGHT);
		return NULL;
	}

	if (pheader->num_xyz <= 0)
	{
		Com_Printf("%s: model %s has no vertices",
				__func__, mod_name);
		return NULL;
	}

	if (pheader->num_xyz > MAX_VERTS)
	{
		Com_Printf("%s: model %s has too many vertices",
				__func__, mod_name);
		return NULL;
	}

	if (pheader->num_st <= 0)
	{
		Com_Printf("%s: model %s has no st vertices",
				__func__, mod_name);
		return NULL;
	}

	if (pheader->num_tris <= 0)
	{
		Com_Printf("%s: model %s has no triangles",
				__func__, mod_name);
		return NULL;
	}

	if (pheader->num_frames <= 0)
	{
		Com_Printf("%s: model %s has no frames",
				__func__, mod_name);
		return NULL;
	}

	if (pheader->num_skins > MAX_MD2SKINS)
	{
		Com_Printf("%s has too many skins (%i > %i), "
				"extra sprites will be ignored\n",
				mod_name, pheader->num_skins, MAX_MD2SKINS);
		pheader->num_skins = MAX_MD2SKINS;
	}

	/* validate inner offsets and sizes against ofs_end. 64 bit math
	   keeps count * size from overflowing. */
	if (pheader->num_skins < 0 || pheader->num_glcmds <= 0
		|| pheader->framesize < (int)(sizeof(daliasframe_t)
			+ (pheader->num_xyz - 1) * sizeof(dtrivertx_t)))
	{
		Com_Printf("%s: model %s has bad counts or frame size\n",
				__func__, mod_name);
		return NULL;
	}

#define MD2_RANGE_BAD(ofs, cnt, size) \
	((ofs) < 0 || (long long)(ofs) + (long long)(cnt) * (long long)(size) > (long long)ofs_end)

	if (MD2_RANGE_BAD(pheader->ofs_skins, pheader->num_skins, MAX_SKINNAME)
		|| MD2_RANGE_BAD(pheader->ofs_st, pheader->num_st, sizeof(dstvert_t))
		|| MD2_RANGE_BAD(pheader->ofs_tris, pheader->num_tris, sizeof(dtriangle_t))
		|| MD2_RANGE_BAD(pheader->ofs_frames, pheader->num_frames, pheader->framesize)
		|| MD2_RANGE_BAD(pheader->ofs_glcmds, pheader->num_glcmds, sizeof(int)))
	{
		Com_Printf("%s: model %s has offsets outside of the file\n",
				__func__, mod_name);
		return NULL;
	}
#undef MD2_RANGE_BAD

	//
	// load base s and t vertices (not used in gl version)
	//
	pinst = (dstvert_t *) ((byte *)pinmodel + pheader->ofs_st);
	poutst = (dstvert_t *) ((byte *)pheader + pheader->ofs_st);

	for (i=0 ; i<pheader->num_st ; i++)
	{
		poutst[i].s = LittleShort (pinst[i].s);
		poutst[i].t = LittleShort (pinst[i].t);
	}

	//
	// load triangle lists
	//
	pintri = (dtriangle_t *) ((byte *)pinmodel + pheader->ofs_tris);
	pouttri = (dtriangle_t *) ((byte *)pheader + pheader->ofs_tris);

	for (i=0 ; i<pheader->num_tris ; i++)
	{
		for (j=0 ; j<3 ; j++)
		{
			pouttri[i].index_xyz[j] = LittleShort (pintri[i].index_xyz[j]);
			pouttri[i].index_st[j] = LittleShort (pintri[i].index_st[j]);
		}
	}

	//
	// load the frames
	//
	for (i=0 ; i<pheader->num_frames ; i++)
	{
		daliasframe_t		*pinframe, *poutframe;

		pinframe = (daliasframe_t *) ((byte *)pinmodel
			+ pheader->ofs_frames + i * pheader->framesize);
		poutframe = (daliasframe_t *) ((byte *)pheader
			+ pheader->ofs_frames + i * pheader->framesize);

		memcpy (poutframe->name, pinframe->name, sizeof(poutframe->name));
		for (j=0 ; j<3 ; j++)
		{
			poutframe->scale[j] = LittleFloat (pinframe->scale[j]);
			poutframe->translate[j] = LittleFloat (pinframe->translate[j]);
		}
		// verts are all 8 bit, so no swapping needed
		memcpy (poutframe->verts, pinframe->verts,
			pheader->num_xyz*sizeof(dtrivertx_t));
	}

	//
	// load the glcmds
	//
	pincmd = (int *) ((byte *)pinmodel + pheader->ofs_glcmds);
	poutcmd = (int *) ((byte *)pheader + pheader->ofs_glcmds);
	for (i=0; i < pheader->num_glcmds; i++)
	{
		poutcmd[i] = LittleLong (pincmd[i]);
	}

	if (poutcmd[pheader->num_glcmds-1] != 0)
	{
		Com_Printf("%s: Entity %s has possible last element issues with %d verts.\n",
			__func__, mod_name, poutcmd[pheader->num_glcmds-1]);
	}

	// register all skins
	memcpy ((char *)pheader + pheader->ofs_skins, (char *)pinmodel + pheader->ofs_skins,
		pheader->num_skins*MAX_SKINNAME);
	for (i=0 ; i<pheader->num_skins ; i++)
	{
		skins[i] = find_image((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME,
			it_skin);
	}

	*type = mod_alias;

	mins[0] = -32;
	mins[1] = -32;
	mins[2] = -32;
	maxs[0] = 32;
	maxs[1] = 32;
	maxs[2] = 32;

	return extradata;
}

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

/*
=================
Mod_LoadSP2

support for .sp2 sprites
=================
*/
void *
Mod_LoadSP2 (const char *mod_name, const void *buffer, int modfilelen,
	struct image_s **skins, findimage_t find_image, modtype_t *type)
{
	dsprite_t *sprin, *sprout;
	void	*extradata;
	int i;

	sprin = (dsprite_t *)buffer;
	extradata = Hunk_Begin(modfilelen);
	sprout = Hunk_Alloc(modfilelen);

	sprout->ident = LittleLong(sprin->ident);
	sprout->version = LittleLong(sprin->version);
	sprout->numframes = LittleLong(sprin->numframes);

	if (sprout->version != SPRITE_VERSION)
	{
		Com_Printf("%s has wrong version number (%i should be %i)",
				mod_name, sprout->version, SPRITE_VERSION);
		return NULL;
	}

	if (sprout->numframes > MAX_MD2SKINS)
	{
		Com_Printf("%s has too many frames (%i > %i), "
				"extra frames will be ignored\n",
				mod_name, sprout->numframes, MAX_MD2SKINS);
		sprout->numframes = MAX_MD2SKINS;
	}

	/* byte swap everything */
	for (i = 0; i < sprout->numframes; i++)
	{
		sprout->frames[i].width = LittleLong(sprin->frames[i].width);
		sprout->frames[i].height = LittleLong(sprin->frames[i].height);
		sprout->frames[i].origin_x = LittleLong(sprin->frames[i].origin_x);
		sprout->frames[i].origin_y = LittleLong(sprin->frames[i].origin_y);
		memcpy(sprout->frames[i].name, sprin->frames[i].name, MAX_SKINNAME);

		skins[i] = find_image((char *)sprout->frames[i].name, it_sprite);
	}

	*type = mod_sprite;

	return extradata;
}

/*
=================
Mod_ReLoad

Reload images in SP2/MD2 (mark registration_sequence)
=================
*/
int
Mod_ReLoadSkins(struct image_s **skins, findimage_t find_image, void *extradata,
	modtype_t type)
{
	if (type == mod_sprite)
	{
		dsprite_t	*sprout;
		int	i;

		sprout = (dsprite_t *)extradata;
		for (i=0; i < sprout->numframes; i++)
		{
			skins[i] = find_image(sprout->frames[i].name, it_sprite);
		}
		return sprout->numframes;
	}
	else if (type == mod_alias)
	{
		dmdl_t *pheader;
		int	i;

		pheader = (dmdl_t *)extradata;
		for (i=0; i < pheader->num_skins; i++)
		{
			skins[i] = find_image ((char *)pheader + pheader->ofs_skins + i*MAX_SKINNAME, it_skin);
		}
		return  pheader->num_frames;
	}
	/* Unknow format, no images associated with it */
	return 0;
}

/*
=================
Mod_SetParent
=================
*/
static void
Mod_SetParent(mnode_t *node, mnode_t *parent)
{
	node->parent = parent;
	if (node->contents != CONTENTS_NODE)
	{
		return;
	}

	Mod_SetParent (node->children[0], node);
	Mod_SetParent (node->children[1], node);
}

/*
=================
Mod_NumberLeafs
=================
*/
static void
Mod_NumberLeafs(mleaf_t *leafs, mnode_t *node, int *r_leaftovis, int *r_vistoleaf,
	int *numvisleafs)
{
	if (node->contents != CONTENTS_NODE)
	{
		mleaf_t *leaf;
		int leafnum;

		leaf = (mleaf_t *)node;
		leafnum = leaf - leafs;
		if (leaf->contents & CONTENTS_SOLID)
		{
			return;
		}

		r_leaftovis[leafnum] = *numvisleafs;
		r_vistoleaf[*numvisleafs] = leafnum;
		(*numvisleafs) ++;
		return;
	}

	Mod_NumberLeafs(leafs, node->children[0], r_leaftovis, r_vistoleaf,
		numvisleafs);
	Mod_NumberLeafs(leafs, node->children[1], r_leaftovis, r_vistoleaf,
		numvisleafs);
}

/*
 * The deepest path from a root of the node graph to a leaf, counted in
 * nodes, as in the collision model's check. The deepest stock map
 * (strike.bsp) is 79 nodes deep. Mod_SetParent, Mod_NumberLeafs and each
 * renderer's world walk recurse once per level, so this also bounds
 * their stack.
 */
#define MAX_NODE_DEPTH 1024

/*
 * Checks that the node graph in the lump is a forest before anything
 * walks it: no node or leaf is the child of two nodes (or twice of one),
 * every node is reached from a node that is nobody's child (the world
 * and the submodel head nodes; a node that is not lies on a cycle), and
 * no path from such a root holds more than MAX_NODE_DEPTH nodes. The walk
 * keeps its own stack. Returns NULL for a forest, else what is wrong,
 * with the child, node or leaf number (as the lump writes a child) in
 * *bad.
 */
/*
 * The renderers' walks tell a leaf from a node by its contents, so a
 * leaf whose contents read as CONTENTS_NODE would be walked as a node.
 * Returns the first such leaf, or -1.
 */
static int
Mod_LeafReadsAsNode(const mleaf_t *leafs, int numleafs)
{
	int i;

	for (i = 0; i < numleafs; i++)
	{
		if (leafs[i].contents == CONTENTS_NODE)
		{
			return i;
		}
	}

	return -1;
}

static const char *
Mod_CheckNodeTree(const dnode_t *in, int count, int numleafs, int *bad)
{
	/* 0: nobody's child, 1: has a parent, 2: reached from a root */
	static byte nodemark[MAX_MAP_NODES];
	static byte leafmark[MAX_MAP_LEAFS];
	/* node and depth; each level holds at most one waiting node, the
	   deepest two */
	static int stack[MAX_NODE_DEPTH + 1][2];
	int i, j;

	*bad = count;

	if ((count < 1) || (count > MAX_MAP_NODES) ||
		(numleafs < 0) || (numleafs > MAX_MAP_LEAFS))
	{
		return "node or leaf count";
	}

	memset(nodemark, 0, count);
	memset(leafmark, 0, numleafs);

	for (i = 0; i < count; i++)
	{
		for (j = 0; j < 2; j++)
		{
			int child = LittleLong(in[i].children[j]);
			byte *mark;

			*bad = child;

			if ((child >= count) || (child < -numleafs))
			{
				return "child out of range";
			}

			mark = (child >= 0) ? &nodemark[child] : &leafmark[-1 - child];

			if (*mark)
			{
				return "child with a second parent";
			}

			*mark = 1;
		}
	}

	for (i = 0; i < count; i++)
	{
		int sp = 0;

		if (nodemark[i])
		{
			continue;
		}

		stack[sp][0] = i;
		stack[sp][1] = 1;
		sp++;

		while (sp > 0)
		{
			int node, depth;

			sp--;
			node = stack[sp][0];
			depth = stack[sp][1];
			nodemark[node] = 2;

			for (j = 0; j < 2; j++)
			{
				int child = LittleLong(in[node].children[j]);

				if (child < 0)
				{
					continue;
				}

				if ((depth >= MAX_NODE_DEPTH) || (sp > MAX_NODE_DEPTH))
				{
					*bad = child;
					return "path too deep at";
				}

				stack[sp][0] = child;
				stack[sp][1] = depth + 1;
				sp++;
			}
		}
	}

	for (i = 0; i < count; i++)
	{
		if (nodemark[i] != 2)
		{
			*bad = i;
			return "cycle through node";
		}
	}

	return NULL;
}

/*
=================
Mod_LoadNodes
=================
*/
void
Mod_LoadNodes(const char *name, cplane_t *planes, int numplanes, mleaf_t *leafs,
	int numleafs, mnode_t **nodes, int *numnodes, const byte *mod_base,
	const lump_t *l)
{
	int	r_leaftovis[MAX_MAP_LEAFS], r_vistoleaf[MAX_MAP_LEAFS];
	int	i, count, numvisleafs, bad;
	const char *why;
	dnode_t	*in;
	mnode_t	*out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc(count * sizeof(*out));

	*nodes = out;
	*numnodes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		int j, planenum;

		for (j = 0; j < 3; j++)
		{
			out->minmaxs[j] = LittleShort(in->mins[j]);
			out->minmaxs[3 + j] = LittleShort(in->maxs[j]);
		}

		planenum = LittleLong(in->planenum);
		if (planenum  < 0 || planenum >= numplanes)
		{
			Com_Error(ERR_DROP, "%s: Incorrect %d < %d planenum.",
					__func__, planenum, numplanes);
		}
		out->plane = planes + planenum;

		out->firstsurface = LittleShort(in->firstface);
		out->numsurfaces = LittleShort(in->numfaces);
		out->contents = CONTENTS_NODE; /* differentiate from leafs */

		for (j = 0; j < 2; j++)
		{
			int leafnum;

			leafnum = LittleLong(in->children[j]);

			if (leafnum >= 0)
			{
				if (leafnum  < 0 || leafnum >= *numnodes)
				{
					Com_Error(ERR_DROP, "%s: Incorrect %d nodenum as leaf.",
							__func__, leafnum);
				}

				out->children[j] = *nodes + leafnum;
			}
			else
			{
				leafnum = -1 - leafnum;
				if (leafnum  < 0 || leafnum >= numleafs)
				{
					Com_Error(ERR_DROP, "%s: Incorrect %d leafnum.",
							__func__, leafnum);
				}

				out->children[j] = (mnode_t *)(leafs + leafnum);
			}
		}
	}

	why = Mod_CheckNodeTree((const dnode_t *)(mod_base + l->fileofs), count,
		numleafs, &bad);

	if (why)
	{
		Com_Error(ERR_DROP, "%s: node graph of %s is not a tree: %s %d",
				__func__, name, why, bad);
	}

	bad = Mod_LeafReadsAsNode(leafs, numleafs);

	if (bad >= 0)
	{
		Com_Error(ERR_DROP, "%s: leaf %d of %s has the node contents",
				__func__, bad, name);
	}

	Mod_SetParent(*nodes, NULL); /* sets nodes and leafs */

	numvisleafs = 0;
	Mod_NumberLeafs (leafs, *nodes, r_leaftovis, r_vistoleaf, &numvisleafs);
}

/*
=================
Mod_LoadVisibility
=================
*/
void
Mod_LoadVisibility (dvis_t **vis, const byte *mod_base, const lump_t *l)
{
	dvis_t	*out;
	int	i;

	if (!l->filelen)
	{
		*vis = NULL;
		return;
	}

	out = Hunk_Alloc(l->filelen);
	*vis = out;
	memcpy(out, mod_base + l->fileofs, l->filelen);

	out->numclusters = LittleLong(out->numclusters);

	for (i = 0; i < out->numclusters; i++)
	{
		out->bitofs[i][0] = LittleLong(out->bitofs[i][0]);
		out->bitofs[i][1] = LittleLong(out->bitofs[i][1]);
	}
}

/*
=================
Mod_LoadVertexes

extra for skybox
=================
*/
void
Mod_LoadVertexes(const char *name, mvertex_t **vertexes, int *numvertexes,
	const byte *mod_base, const lump_t *l, int extra)
{
	dvertex_t	*in;
	mvertex_t	*out;
	int	i, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc((count + extra)*sizeof(*out));

	/*
	 * FIXME: Recheck with soft render
	 * Fix for the problem where the games dumped core
	 * when changing levels.
	 */
	memset(out, 0, (count + extra) * sizeof(*out));

	*vertexes = out;
	*numvertexes = count;

	for (i = 0; i < count; i++, in++, out++)
	{
		out->position[0] = LittleFloat(in->point[0]);
		out->position[1] = LittleFloat(in->point[1]);
		out->position[2] = LittleFloat(in->point[2]);
	}
}

/*
=================
Mod_LoadLighting
=================
*/
void
Mod_LoadLighting(byte **lightdata, const byte *mod_base, const lump_t *l)
{
	int	size;

	if (!l->filelen)
	{
		*lightdata = NULL;
		return;
	}

	size = l->filelen;
	*lightdata = Hunk_Alloc(size);
	memcpy(*lightdata, mod_base + l->fileofs, size);
}

/*
=================
Mod_LoadTexinfo

extra for skybox in soft render
=================
*/
void
Mod_LoadTexinfo(const char *name, mtexinfo_t **texinfo, int *numtexinfo,
	const byte *mod_base, const lump_t *l, findimage_t find_image,
	struct image_s *notexture, int extra)
{
	texinfo_t *in;
	mtexinfo_t *out, *step;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc((count + extra)*sizeof(*out));

	*texinfo = out;
	*numtexinfo = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		struct image_s *image;
		int j, next;

		for (j = 0; j < 4; j++)
		{
			out->vecs[0][j] = LittleFloat(in->vecs[0][j]);
			out->vecs[1][j] = LittleFloat(in->vecs[1][j]);
		}

		out->flags = LittleLong (in->flags);

		next = LittleLong (in->nexttexinfo);
		if (next > 0)
		{
			out->next = *texinfo + next;
		}
		else
		{
			/*
			 * Fix for the problem where the game
			 * domed core when loading a new level.
			 */
			out->next = NULL;
		}

		image = GetTexImage(in->texture, find_image);
		if (!image)
		{
			Com_Printf("%s: Couldn't load %s\n",
				__func__, in->texture);
			image = notexture;
		}

		out->image = image;
	}

	// count animation frames
	for (i=0 ; i<count ; i++)
	{
		out = (*texinfo) + i;
		out->numframes = 1;
		for (step = out->next ; step && step != out ; step=step->next)
		{
			out->numframes++;
		}
	}
}

/*
=================
Mod_LoadEdges

extra is used for skybox, which adds 6 surfaces
=================
*/
void
Mod_LoadEdges(const char *name, medge_t **edges, int *numedges,
	const byte *mod_base, const lump_t *l, int extra)
{
	dedge_t *in;
	medge_t *out;
	int 	i, count;

	in = (void *)(mod_base + l->fileofs);
	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc((count + extra) * sizeof(*out));

	*edges = out;
	*numedges = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		out->v[0] = (unsigned short)LittleShort(in->v[0]);
		out->v[1] = (unsigned short)LittleShort(in->v[1]);
	}
}

/*
=================
Mod_LoadPlanes

extra is used for skybox, which adds 6 surfaces
=================
*/
void
Mod_LoadPlanes(const char *name, cplane_t **planes, int *numplanes,
	const byte *mod_base, const lump_t *l, int extra)
{
	int i;
	cplane_t	*out;
	dplane_t 	*in;
	int			count;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, name);
	}

	count = l->filelen / sizeof(*in);
	// FIXME: why double of count
	out = Hunk_Alloc((count * 2 + extra) * sizeof(*out));

	*planes = out;
	*numplanes = count;

	for ( i=0 ; i<count ; i++, in++, out++)
	{
		int bits, j;

		bits = 0;
		for (j=0 ; j<3 ; j++)
		{
			out->normal[j] = LittleFloat (in->normal[j]);
			if (out->normal[j] < 0)
				bits |= 1<<j;
		}

		out->dist = LittleFloat (in->dist);
		out->type = LittleLong (in->type);
		out->signbits = bits;
	}
}

/*
=================
Mod_LoadSurfedges
=================
*/
void
Mod_LoadSurfedges(const char *name, int **surfedges, int *numsurfedges,
	const byte *mod_base, const lump_t *l, int extra)
{
	int		i, count;
	int		*in, *out;

	in = (void *)(mod_base + l->fileofs);

	if (l->filelen % sizeof(*in))
	{
		Com_Error(ERR_DROP, "%s: funny lump size in %s",
				__func__, name);
	}

	count = l->filelen / sizeof(*in);
	out = Hunk_Alloc((count + extra)*sizeof(*out));	// extra for skybox

	*surfedges = out;
	*numsurfedges = count;

	for ( i=0 ; i<count ; i++)
		out[i] = LittleLong (in[i]);
}

/*
=================
Mod_LoadSurfedges

calculate the size that Hunk_Alloc(), called by Mod_Load*() from Mod_LoadBrushModel(),
will use (=> includes its padding), so we'll know how big the hunk needs to be
extra is used for skybox, which adds 6 surfaces
=================
*/
int
Mod_CalcLumpHunkSize(const lump_t *l, int inSize, int outSize, int extra)
{
	if (l->filelen % inSize)
	{
		// Mod_Load*() will error out on this because of "funny size"
		// don't error out here because in Mod_Load*() it can print the functionname
		// (=> tells us what kind of lump) before shutting down the game
		return 0;
	}

	int count = l->filelen / inSize + extra;
	int size = count * outSize;

	// round to cacheline, like Hunk_Alloc() does
	size = (size + 31) & ~31;
	return size;
}

/*
===============
Mod_PointInLeaf
===============
*/
mleaf_t *
Mod_PointInLeaf(const vec3_t p, mnode_t *node)
{
	if (!node)
	{
		Com_Error(ERR_DROP, "%s: bad node.", __func__);
		return NULL;
	}

	while (1)
	{
		float d;
		cplane_t *plane;

		if (node->contents != CONTENTS_NODE)
		{
			return (mleaf_t *)node;
		}

		plane = node->plane;
		d = DotProduct(p, plane->normal) - plane->dist;

		if (d > 0)
		{
			node = node->children[0];
		}
		else
		{
			node = node->children[1];
		}
	}

	return NULL; /* never reached */
}

/*
 * The BSP checks every renderer's Mod_LoadBrushModel runs before it reads
 * a lump (FIX118, from ref_metal's RM_Check*). They read the file in place
 * (the header swapped, the lumps as stored) and change nothing. Each
 * returns true when the map passes, or false with the reason in why, and
 * the caller refuses the map (Com_Error, or ref_metal's RM_Refuse), as
 * Mod_LoadNodes refuses on Mod_CheckNodeTree's reason. Mod_CheckLumps
 * runs first: the other three rely on every lump being inside the file.
 */

/* [first, first + count) inside [0, total], with no overflow */
static qboolean
Mod_RangeOk(long first, long count, long total)
{
	return (first >= 0) && (count >= 0) && (total >= 0) && (first <= total) &&
		(count <= total - first);
}

/* the lump's record count at most limit, or false with why */
static qboolean
Mod_LumpCountOk(const dheader_t *header, int lump, size_t size, long limit,
	const char *what, char *why, size_t whysize)
{
	long count = (long)((size_t)header->lumps[lump].filelen / size);

	if (count > limit)
	{
		snprintf(why, whysize, "%ld %s, more than %ld", count, what, limit);
		return false;
	}

	return true;
}

/* the next texinfo of an animation chain, or -1 at its end */
static long
Mod_TexinfoNext(const int *next, long i)
{
	return next[i] > 0 ? next[i] : -1;
}

/*
 * Every lump inside the file (its offset 4-aligned: the loaders read the
 * records' int and float fields in place), the counts the loaders size
 * their arrays by inside the limits, the lighting inside its limit, the
 * texinfo names terminated and their animation chains bounded
 * (Mod_LoadTexinfo prints the name with %s and walks nexttexinfo).
 */
qboolean
Mod_CheckLumps(const byte *mod_base, const dheader_t *header, int len,
	char *why, size_t whysize)
{
	static int next[MAX_MAP_TEXINFO];
	const lump_t *l;
	long i, step, steps, count;

	if (len < (int)sizeof(dheader_t))
	{
		snprintf(why, whysize, "%d bytes, too short for a BSP header", len);
		return false;
	}

	for (i = 0; i < HEADER_LUMPS; i++)
	{
		l = &header->lumps[i];

		if ((l->fileofs & 3) || !Mod_RangeOk(l->fileofs, l->filelen, len))
		{
			snprintf(why, whysize, "lump %ld (%d bytes at %d) is not inside the %d bytes of the file",
				i, l->filelen, l->fileofs, len);
			return false;
		}
	}

	if (!Mod_LumpCountOk(header, LUMP_NODES, sizeof(dnode_t), MAX_MAP_NODES, "nodes", why, whysize) ||
		!Mod_LumpCountOk(header, LUMP_PLANES, sizeof(dplane_t), MAX_MAP_PLANES, "planes", why, whysize) ||
		!Mod_LumpCountOk(header, LUMP_VERTEXES, sizeof(dvertex_t), MAX_MAP_VERTS, "vertices", why, whysize) ||
		!Mod_LumpCountOk(header, LUMP_EDGES, sizeof(dedge_t), MAX_MAP_EDGES, "edges", why, whysize) ||
		!Mod_LumpCountOk(header, LUMP_SURFEDGES, sizeof(int), MAX_MAP_SURFEDGES, "surfedges", why, whysize) ||
		!Mod_LumpCountOk(header, LUMP_LEAFFACES, sizeof(short), MAX_MAP_LEAFFACES, "leaf faces", why, whysize))
	{
		return false;
	}

	if (header->lumps[LUMP_LIGHTING].filelen > MAX_MAP_LIGHTING)
	{
		snprintf(why, whysize, "%d bytes of lighting, more than %d",
			header->lumps[LUMP_LIGHTING].filelen, MAX_MAP_LIGHTING);
		return false;
	}

	l = &header->lumps[LUMP_TEXINFO];

	if ((l->filelen % sizeof(texinfo_t)) == 0)
	{
		const texinfo_t *in = (const texinfo_t *)(mod_base + l->fileofs);

		if (!Mod_LumpCountOk(header, LUMP_TEXINFO, sizeof(texinfo_t), MAX_MAP_TEXINFO,
				"texinfos", why, whysize))
		{
			return false;
		}

		count = (long)((size_t)l->filelen / sizeof(texinfo_t));

		for (i = 0; i < count; i++)
		{
			if (!memchr(in[i].texture, 0, sizeof(in[i].texture)))
			{
				snprintf(why, whysize, "texinfo %ld has an unterminated texture name", i);
				return false;
			}

			next[i] = LittleLong(in[i].nexttexinfo);

			if (next[i] >= count)
			{
				snprintf(why, whysize, "a texinfo animation chain leaves the lump or never ends");
				return false;
			}
		}

		/* Mod_LoadTexinfo's count loop: from each texinfo until the chain ends or comes back */
		for (i = 0; i < count; i++)
		{
			for (step = Mod_TexinfoNext(next, i), steps = 0; (step != -1) && (step != i);
				step = Mod_TexinfoNext(next, step))
			{
				if (++steps > count)
				{
					snprintf(why, whysize, "a texinfo animation chain leaves the lump or never ends");
					return false;
				}
			}
		}
	}

	return true;
}

/*
 * The visibility lump: its size inside the limit, room in it for the
 * numclusters (at most MAX_MAP_LEAFS) offset pairs, and each PVS and PHS
 * offset inside it (Mod_LoadVisibility swaps them in place, the draw
 * decompresses from them).
 */
qboolean
Mod_CheckVis(const byte *mod_base, const dheader_t *header, char *why, size_t whysize)
{
	const lump_t *l = &header->lumps[LUMP_VISIBILITY];
	const int *ofs;
	long numclusters, i;

	if (!l->filelen)
	{
		return true;
	}

	if ((l->filelen > MAX_MAP_VISIBILITY) || (l->filelen < 4))
	{
		snprintf(why, whysize, "%d bytes of visibility", l->filelen);
		return false;
	}

	ofs = (const int *)(mod_base + l->fileofs);
	numclusters = LittleLong(ofs[0]);

	if ((numclusters < 0) || (numclusters > MAX_MAP_LEAFS) ||
		(numclusters > (l->filelen - 4) / 8))
	{
		snprintf(why, whysize, "visibility of %ld clusters in %d bytes", numclusters, l->filelen);
		return false;
	}

	for (i = 0; i < 2 * numclusters; i++)
	{
		long bitofs = LittleLong(ofs[1 + i]);

		if (!Mod_RangeOk(bitofs, 0, l->filelen))
		{
			snprintf(why, whysize, "cluster %ld has its visibility at %ld, outside %d bytes",
				i / 2, bitofs, l->filelen);
			return false;
		}
	}

	return true;
}

/*
 * The nodes: at least one, model 0's head one of them, a tree from node 0
 * (every child a leaf of the map, or a node other than 0 with one parent
 * at most: Mod_SetParent and Mod_NumberLeafs recurse through them and the
 * draw would chain a node's surfaces once per visit), and each node's
 * faces inside the face lump and in that node only.
 */
qboolean
Mod_CheckNodes(const byte *mod_base, const dheader_t *header, char *why, size_t whysize)
{
	static byte seen[MAX_MAP_NODES];
	const lump_t *l = &header->lumps[LUMP_NODES];
	const dnode_t *in = (const dnode_t *)(mod_base + l->fileofs);
	long numnodes = (long)((size_t)l->filelen / sizeof(dnode_t));
	long nummodels = (long)((size_t)header->lumps[LUMP_MODELS].filelen / sizeof(dmodel_t));
	long numleafs = (long)((size_t)header->lumps[LUMP_LEAFS].filelen / sizeof(dleaf_t));
	long numfaces = (long)((size_t)header->lumps[LUMP_FACES].filelen / sizeof(dface_t));
	long headnode, i, k;
	byte *owner;
	int j;

	headnode = nummodels ? LittleLong(((const dmodel_t *)(mod_base +
		header->lumps[LUMP_MODELS].fileofs))->headnode) : -1;

	if ((numnodes < 1) || (numnodes > MAX_MAP_NODES) || (nummodels < 1) ||
		(headnode < 0) || (headnode >= numnodes))
	{
		snprintf(why, whysize, "%ld nodes and %ld models, model 0 at node %ld",
			numnodes, nummodels, headnode);
		return false;
	}

	memset(seen, 0, (size_t)numnodes);

	for (i = 0; i < numnodes; i++)
	{
		for (j = 0; j < 2; j++)
		{
			long c = LittleLong(in[i].children[j]);

			if (c < 0)
			{
				/* a leaf, -1 - c; a leaf may have more than one parent */
				if (-1 - c >= numleafs)
				{
					snprintf(why, whysize, "node %ld names leaf %ld, the map has %ld",
						i, -1 - c, numleafs);
					return false;
				}

				continue;
			}

			/* node 0 is the root; any other node has one parent at most */
			if ((c == 0) || (c >= numnodes) || seen[c])
			{
				snprintf(why, whysize, "the %ld nodes are not a tree from node 0: node %ld names node %ld",
					numnodes, i, c);
				return false;
			}

			seen[c] = 1;
		}
	}

	owner = calloc(numfaces ? numfaces : 1, 1);

	if (!owner)
	{
		snprintf(why, whysize, "out of memory");
		return false;
	}

	for (i = 0; i < numnodes; i++)
	{
		/* unsigned short on disk, read as Mod_LoadNodes reads them */
		long first = (unsigned short)LittleShort(in[i].firstface);
		long count = (unsigned short)LittleShort(in[i].numfaces);
		qboolean ok = Mod_RangeOk(first, count, numfaces);

		for (k = first; ok && (k < first + count); k++)
		{
			ok = !owner[k];
			owner[k] = 1;
		}

		if (!ok)
		{
			free(owner);
			snprintf(why, whysize, "node %ld has surfaces %ld to %ld of %ld, or one of another node",
				i, first, first + count, numfaces);
			return false;
		}
	}

	free(owner);
	return true;
}

/*
 * Every face's edges: at least 3, inside the surfedge lump; every
 * surfedge names an edge, and both vertices of that edge exist (the
 * extents and the polygon builders index them unchecked).
 */
qboolean
Mod_CheckFaceEdges(const byte *mod_base, const dheader_t *header, char *why, size_t whysize)
{
	const lump_t *fl = &header->lumps[LUMP_FACES];
	const dface_t *in = (const dface_t *)(mod_base + fl->fileofs);
	const int *surfedges = (const int *)(mod_base + header->lumps[LUMP_SURFEDGES].fileofs);
	const dedge_t *edges = (const dedge_t *)(mod_base + header->lumps[LUMP_EDGES].fileofs);
	long numfaces = (long)((size_t)fl->filelen / sizeof(dface_t));
	long numsurfedges = (long)((size_t)header->lumps[LUMP_SURFEDGES].filelen / sizeof(int));
	long numedges = (long)((size_t)header->lumps[LUMP_EDGES].filelen / sizeof(dedge_t));
	long numvertexes = (long)((size_t)header->lumps[LUMP_VERTEXES].filelen / sizeof(dvertex_t));
	long f, i;

	for (f = 0; f < numfaces; f++)
	{
		/* read as the loaders read them */
		long firstedge = LittleLong(in[f].firstedge);
		long n = LittleShort(in[f].numedges);

		if ((n < 3) || !Mod_RangeOk(firstedge, n, numsurfedges))
		{
			snprintf(why, whysize, "face %ld has %ld edges from %ld, the map has %ld",
				f, n, firstedge, numsurfedges);
			return false;
		}

		for (i = 0; i < n; i++)
		{
			long e = LittleLong(surfedges[firstedge + i]);
			const dedge_t *edge;

			if ((e >= numedges) || (e <= -numedges))
			{
				snprintf(why, whysize, "face %ld names edge %ld, the map has %ld", f, e, numedges);
				return false;
			}

			edge = &edges[e >= 0 ? e : -e];

			if (((unsigned short)LittleShort(edge->v[0]) >= numvertexes) ||
				((unsigned short)LittleShort(edge->v[1]) >= numvertexes))
			{
				snprintf(why, whysize, "face %ld has an edge past the %ld vertices", f, numvertexes);
				return false;
			}
		}
	}

	return true;
}

/*
 * Every leaf's cluster, when the map has visibility, -1 or one of its
 * clusters (FIX134): the cluster is a signed short the loaders store as
 * it is, and ClusterPVS (the offset table) and R_MarkLeaves (the row's
 * bits) index by it. After Mod_CheckVis. Without visibility no cluster is
 * read, but one below -1 is still refused, so the renderers and the
 * collision model accept the same leaves (FIX137).
 */
qboolean
Mod_CheckLeafs(const byte *mod_base, const dheader_t *header, char *why, size_t whysize)
{
	const lump_t *vl = &header->lumps[LUMP_VISIBILITY];
	const lump_t *l = &header->lumps[LUMP_LEAFS];
	const dleaf_t *in = (const dleaf_t *)(mod_base + l->fileofs);
	long numleafs = (long)((size_t)l->filelen / sizeof(dleaf_t));
	long numclusters = 0, i;

	/* without visibility only a cluster below -1 is refused, as the
	   collision model's CMod_CheckLeafClusters does (FIX137) */
	if (vl->filelen)
	{
		numclusters = LittleLong(*(const int *)(mod_base + vl->fileofs));
	}

	for (i = 0; i < numleafs; i++)
	{
		long cluster = LittleShort(in[i].cluster);

		if ((cluster < -1) || (vl->filelen && (cluster >= numclusters)))
		{
			snprintf(why, whysize, "leaf %ld has cluster %ld, the visibility has %ld clusters",
				i, cluster, numclusters);
			return false;
		}
	}

	return true;
}
