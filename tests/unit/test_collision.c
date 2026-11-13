/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Collision BSP loading (src/common/collision.c, CM_LoadMap) on a
 * synthetic map built here: one plane x = 0, one node, a solid leaf
 * behind it holding one one-sided brush, an empty leaf in front, one
 * submodel, one area and a worldspawn entity string. The valid map is
 * loaded and queried (contents, a trace); a table of single corruptions
 * of it must each be refused with Com_Error(ERR_DROP), including the
 * lump, node, brushside and cross-reference checks TASK2 added.
 *
 * Not covered on purpose, because the loader does not check them yet
 * and a test would corrupt memory: leaf cluster/area values below -1,
 * the box-hull planes past a maximal leaf count, visibility offsets at
 * the end of the lump, negative area portal numbers. Those are FIX3
 * (#44) and FIX23 (#121); their authors should add the refusals here.
 *
 * =======================================================================
 */

#include "test.h"

typedef struct
{
	int version;
	dplane_t planes[2];
	int nplanes;
	dnode_t nodes[2];
	int nnodes;
	texinfo_t tex[1];
	int ntex;
	dleaf_t leafs[3];
	int nleafs;
	unsigned short leafbrushes[2];
	int nleafbrushes;
	dmodel_t models[1];
	int nmodels;
	dbrush_t brushes[1];
	int nbrushes;
	dbrushside_t sides[1];
	int nsides;
	darea_t areas[1];
	int nareas;
	const char *ents;
} bsp_t;

static void
bsp_default(bsp_t *b)
{
	memset(b, 0, sizeof(*b));
	b->version = BSPVERSION;

	/* plane 0: x = 0, facing +x */
	b->planes[0].normal[0] = 1;
	b->planes[0].dist = 0;
	b->planes[0].type = PLANE_X;
	b->nplanes = 1;

	/* node 0: front (x > 0) is leaf 1, back is leaf 0 */
	b->nodes[0].planenum = 0;
	b->nodes[0].children[0] = -2;
	b->nodes[0].children[1] = -1;
	b->nnodes = 1;

	Q_strlcpy(b->tex[0].texture, "e1u1/floor1_1", sizeof(b->tex[0].texture));
	b->tex[0].flags = 0;
	b->tex[0].value = 7;
	b->ntex = 1;

	/* leaf 0: solid, holds brush 0; leaf 1: empty */
	b->leafs[0].contents = CONTENTS_SOLID;
	b->leafs[0].cluster = -1;
	b->leafs[0].firstleafbrush = 0;
	b->leafs[0].numleafbrushes = 1;
	b->leafs[1].contents = 0;
	b->leafs[1].cluster = 0;
	b->nleafs = 2;

	b->leafbrushes[0] = 0;
	b->nleafbrushes = 1;

	b->models[0].mins[0] = -64;
	b->models[0].mins[1] = -64;
	b->models[0].mins[2] = -64;
	b->models[0].maxs[0] = 64;
	b->models[0].maxs[1] = 64;
	b->models[0].maxs[2] = 64;
	b->models[0].headnode = 0;
	b->nmodels = 1;

	/* brush 0: the half space behind plane 0 */
	b->brushes[0].firstside = 0;
	b->brushes[0].numsides = 1;
	b->brushes[0].contents = CONTENTS_SOLID;
	b->nbrushes = 1;
	b->sides[0].planenum = 0;
	b->sides[0].texinfo = 0;
	b->nsides = 1;

	b->nareas = 1;

	b->ents = "{\n\"classname\" \"worldspawn\"\n\"message\" \"synthetic\"\n}\n";
}

/* Serialises the map: header, then each lump 4-byte aligned. */
static byte *
bsp_build(const bsp_t *b, int *outlen)
{
	static byte buf[8192];
	dheader_t *h = (dheader_t *)buf;
	int ofs = sizeof(dheader_t);

	memset(buf, 0, sizeof(buf));
	h->ident = IDBSPHEADER;
	h->version = b->version;

#define LUMP(N, PTR, LEN) \
	do { \
		h->lumps[N].fileofs = ofs; \
		h->lumps[N].filelen = (LEN); \
		memcpy(buf + ofs, (PTR), (LEN)); \
		ofs += ((LEN) + 3) & ~3; \
	} while (0)

	LUMP(LUMP_ENTITIES, b->ents, (int)strlen(b->ents) + 1);
	LUMP(LUMP_PLANES, b->planes, b->nplanes * (int)sizeof(dplane_t));
	LUMP(LUMP_NODES, b->nodes, b->nnodes * (int)sizeof(dnode_t));
	LUMP(LUMP_TEXINFO, b->tex, b->ntex * (int)sizeof(texinfo_t));
	LUMP(LUMP_LEAFS, b->leafs, b->nleafs * (int)sizeof(dleaf_t));
	LUMP(LUMP_LEAFBRUSHES, b->leafbrushes, b->nleafbrushes * (int)sizeof(unsigned short));
	LUMP(LUMP_MODELS, b->models, b->nmodels * (int)sizeof(dmodel_t));
	LUMP(LUMP_BRUSHES, b->brushes, b->nbrushes * (int)sizeof(dbrush_t));
	LUMP(LUMP_BRUSHSIDES, b->sides, b->nsides * (int)sizeof(dbrushside_t));
	LUMP(LUMP_AREAS, b->areas, b->nareas * (int)sizeof(darea_t));

#undef LUMP

	/* The other lumps (vertexes, visibility, faces, ...) stay empty
	   at offset 0, which is inside the file. */
	*outlen = ofs;
	return buf;
}

static int map_serial;

/* Writes the map under a fresh name (CM_LoadMap caches by name). */
static const char *
bsp_write(const byte *data, int len)
{
	static char name[MAX_QPATH];
	char rel[MAX_OSPATH];

	Com_sprintf(name, sizeof(name), "maps/t_map%d.bsp", map_serial++);
	Com_sprintf(rel, sizeof(rel), "data/baseq2/%s", name);
	t_write_file(rel, data, len);
	return name;
}

TEST(cm_load_valid_map)
{
	bsp_t b;
	byte *data;
	int len;
	unsigned checksum = 0;
	cmodel_t *world;
	const char *name;
	vec3_t front = { 10, 0, 0 }, back = { -10, 0, 0 };
	vec3_t zero = { 0, 0, 0 };
	trace_t tr;
	t_text_t pin = {0};

	bsp_default(&b);
	data = bsp_build(&b, &len);
	name = bsp_write(data, len);

	/* The last lump ends exactly at the end of the file. */
	T_EQ_INT(((dheader_t *)data)->lumps[LUMP_AREAS].fileofs +
		((dheader_t *)data)->lumps[LUMP_AREAS].filelen, len);

	world = CM_LoadMap(name, false, &checksum);
	T_REQUIRE(world != NULL);

	t_text_printf(&pin, "size %d checksum %08x\n", len, checksum);
	t_text_printf(&pin, "inline models %d clusters %d\n", CM_NumInlineModels(), CM_NumClusters());
	t_text_printf(&pin, "world mins (%g %g %g) maxs (%g %g %g) headnode %d\n",
		world->mins[0], world->mins[1], world->mins[2],
		world->maxs[0], world->maxs[1], world->maxs[2], world->headnode);
	t_text_printf(&pin, "entity string: %s", CM_EntityString());
	t_text_printf(&pin, "contents front %d back %d\n",
		CM_PointContents(front, 0), CM_PointContents(back, 0));
	t_text_printf(&pin, "leaf front %d back %d, leaf 0 contents %d\n",
		CM_PointLeafnum(front), CM_PointLeafnum(back), CM_LeafContents(0));

	tr = CM_BoxTrace(front, back, zero, zero, 0, MASK_SOLID);
	t_text_printf(&pin, "trace front->back: fraction %.4f end (%.3f %g %g) startsolid %d allsolid %d contents %d surface %s/%d\n",
		tr.fraction, tr.endpos[0], tr.endpos[1], tr.endpos[2], tr.startsolid, tr.allsolid,
		tr.contents, tr.surface ? tr.surface->name : "-", tr.surface ? tr.surface->value : -1);
	tr = CM_BoxTrace(back, front, zero, zero, 0, MASK_SOLID);
	t_text_printf(&pin, "trace back->front: fraction %.4f startsolid %d allsolid %d\n",
		tr.fraction, tr.startsolid, tr.allsolid);
	tr = CM_BoxTrace(front, front, zero, zero, 0, MASK_SOLID);
	t_text_printf(&pin, "trace in front: fraction %.4f startsolid %d\n", tr.fraction, tr.startsolid);

	/* Loading the same name again returns the cached map. */
	T_CHECK(CM_LoadMap(name, false, &checksum) == world);

	t_pin("cm-valid-map", pin.data);
	t_text_free(&pin);
}

/* One corruption of the default map. */
typedef struct
{
	const char *what;
	void (*apply)(bsp_t *b);
	/* applied to the serialised bytes, after apply */
	void (*patch)(byte *data, int *len);
} corruption_t;

static void c_version(bsp_t *b) { b->version = 37; }
static void c_no_planes(bsp_t *b) { b->nplanes = 0; }
static void c_no_leafs(bsp_t *b) { b->nleafs = 0; }
static void c_leaf0_empty(bsp_t *b) { b->leafs[0].contents = 0; }
static void c_no_empty_leaf(bsp_t *b) { b->leafs[1].contents = CONTENTS_WATER; }
static void c_no_texinfo(bsp_t *b) { b->ntex = 0; }
static void c_no_nodes(bsp_t *b) { b->nnodes = 0; }
static void c_no_models(bsp_t *b) { b->nmodels = 0; }
static void c_no_leafbrushes(bsp_t *b) { b->nleafbrushes = 0; }
static void c_node_plane_high(bsp_t *b) { b->nodes[0].planenum = 1; }
static void c_node_plane_neg(bsp_t *b) { b->nodes[0].planenum = -1; }
static void c_node_child_node(bsp_t *b) { b->nodes[0].children[0] = 1; }
static void c_node_child_leaf(bsp_t *b) { b->nodes[0].children[1] = -3; }
static void c_side_plane(bsp_t *b) { b->sides[0].planenum = 1; }
static void c_side_texinfo(bsp_t *b) { b->sides[0].texinfo = 1; }
static void c_leaf_brush_range(bsp_t *b) { b->leafs[0].numleafbrushes = 2; }
static void c_leafbrush_index(bsp_t *b) { b->leafbrushes[0] = 1; }
static void c_brush_sides(bsp_t *b) { b->brushes[0].numsides = 2; }
static void c_model_headnode(bsp_t *b) { b->models[0].headnode = 1; }
static void c_model_headleaf(bsp_t *b) { b->models[0].headnode = -3; }

static void
p_truncate_header(byte *data, int *len)
{
	(void)data;
	*len = sizeof(dheader_t) - 1;
}

static void
p_lump_past_end(byte *data, int *len)
{
	dheader_t *h = (dheader_t *)data;

	h->lumps[LUMP_LEAFS].filelen = *len;
}

/* The areas lump is the last one bsp_build writes and ends exactly at
   the end of the file (cm_load_valid_map loads that, the accepted side
   of the boundary); cutting the file by one byte leaves it ending one
   byte past the end. */
static void
p_lump_one_past_end(byte *data, int *len)
{
	dheader_t *h = (dheader_t *)data;

	if (h->lumps[LUMP_AREAS].fileofs + h->lumps[LUMP_AREAS].filelen == *len)
	{
		*len -= 1;
	}
}

static void
p_lump_negative_ofs(byte *data, int *len)
{
	dheader_t *h = (dheader_t *)data;

	(void)len;
	h->lumps[LUMP_PLANES].fileofs = -4;
}

static void
p_lump_negative_len(byte *data, int *len)
{
	dheader_t *h = (dheader_t *)data;

	(void)len;
	h->lumps[LUMP_VISIBILITY].filelen = -1;
}

static void
p_lump_overflow(byte *data, int *len)
{
	dheader_t *h = (dheader_t *)data;

	(void)len;
	/* ofs + len wraps a 32-bit int */
	h->lumps[LUMP_ENTITIES].fileofs = 0x7ffffff0;
	h->lumps[LUMP_ENTITIES].filelen = 0x20;
}

static void
p_funny_planes(byte *data, int *len)
{
	dheader_t *h = (dheader_t *)data;

	(void)len;
	h->lumps[LUMP_PLANES].filelen -= 1;
}

static void
p_funny_leafs(byte *data, int *len)
{
	dheader_t *h = (dheader_t *)data;

	(void)len;
	h->lumps[LUMP_LEAFS].filelen += 2;
}

TEST(cm_refused_maps)
{
	static const corruption_t cases[] = {
		{ "header cut short", NULL, p_truncate_header },
		{ "wrong version", c_version, NULL },
		{ "lump past end of file (TASK2)", NULL, p_lump_past_end },
		{ "lump ends one byte past the file", NULL, p_lump_one_past_end },
		{ "lump negative offset (TASK2)", NULL, p_lump_negative_ofs },
		{ "lump negative length (TASK2)", NULL, p_lump_negative_len },
		{ "lump offset+length wraps (TASK2)", NULL, p_lump_overflow },
		{ "planes lump not a multiple", NULL, p_funny_planes },
		{ "leafs lump not a multiple", NULL, p_funny_leafs },
		{ "no planes", c_no_planes, NULL },
		{ "no leafs", c_no_leafs, NULL },
		{ "leaf 0 not solid", c_leaf0_empty, NULL },
		{ "no empty leaf", c_no_empty_leaf, NULL },
		{ "no texinfo", c_no_texinfo, NULL },
		{ "no nodes", c_no_nodes, NULL },
		{ "no models", c_no_models, NULL },
		{ "no leafbrushes", c_no_leafbrushes, NULL },
		{ "node plane index too high (TASK2)", c_node_plane_high, NULL },
		{ "node plane index negative (TASK2)", c_node_plane_neg, NULL },
		{ "node child node out of range (TASK2)", c_node_child_node, NULL },
		{ "node child leaf out of range (TASK2)", c_node_child_leaf, NULL },
		{ "brushside plane out of range (TASK2)", c_side_plane, NULL },
		{ "brushside texinfo out of range", c_side_texinfo, NULL },
		{ "leaf leafbrush range (TASK2)", c_leaf_brush_range, NULL },
		{ "leafbrush brush index (TASK2)", c_leafbrush_index, NULL },
		{ "brush brushside range (TASK2)", c_brush_sides, NULL },
		{ "submodel headnode node (TASK2)", c_model_headnode, NULL },
		{ "submodel headnode leaf (TASK2)", c_model_headleaf, NULL },
	};
	t_text_t pin = {0};
	unsigned checksum;
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		bsp_t b;
		byte *data;
		int len;
		const char *name, *err;

		bsp_default(&b);

		if (cases[i].apply)
		{
			cases[i].apply(&b);
		}

		data = bsp_build(&b, &len);

		if (cases[i].patch)
		{
			cases[i].patch(data, &len);
		}

		name = bsp_write(data, len);

		T_EXPECT_DROP(CM_LoadMap(name, false, &checksum));

		/* Com_Error prints "ERROR: <message>"; pin it with the map's
		   serial name replaced so that reordering cases is harmless. */
		err = strstr(t_console(), "ERROR: ");
		t_text_printf(&pin, "%s:\n  ", cases[i].what);

		if (!err)
		{
			t_text_printf(&pin, "(loaded)\n");
			continue;
		}

		err += strlen("ERROR: ");

		for (; *err && *err != '\n'; err++)
		{
			if (strncmp(err, name, strlen(name)) == 0)
			{
				t_text_printf(&pin, "<map>");
				err += strlen(name) - 1;
			}
			else
			{
				t_text_printf(&pin, "%c", *err);
			}
		}

		t_text_printf(&pin, "\n");
	}

	/* A missing map, and the loader recovers for a good one. */
	T_EXPECT_DROP(CM_LoadMap("maps/t_does_not_exist.bsp", false, &checksum));
	T_CONSOLE_HAS("Couldn't load maps/t_does_not_exist.bsp");

	{
		bsp_t b;
		byte *data;
		int len;

		bsp_default(&b);
		data = bsp_build(&b, &len);
		T_CHECK(CM_LoadMap(bsp_write(data, len), false, &checksum) != NULL);
		T_EQ_INT(CM_NumInlineModels(), 1);
	}

	t_pin("cm-refused-maps", pin.data);
	t_text_free(&pin);
}

TEST(cm_empty_name)
{
	unsigned checksum = 1;

	/* An empty name is the "no map" of cinematic servers. */
	T_CHECK(CM_LoadMap("", false, &checksum) != NULL);
	T_EQ_INT(checksum, 0);
	T_EQ_INT(CM_NumClusters(), 1);
	T_EQ_STR(CM_EntityString(), "");
}
