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
 * The node graph, the visibility input and the area portal number of a
 * map, in the two loaders a map passes through: the collision model
 * (src/common/collision.c, CM_LoadMap, linked into the suite) and the
 * renderers' shared loader (src/client/refresh/files/models.c,
 * Mod_LoadNodes, compiled into this file whole because nothing else in
 * the suite links it). Each case generates its map here and feeds the
 * same bytes to both loaders.
 *
 * - The node graph must be a forest (FIX43): no node or leaf with two
 *   parents, no node on a cycle, no path from a root deeper than the
 *   bound. Both loaders refuse such a graph with Com_Error(ERR_DROP) and
 *   name the map; a stock-shaped map and a chain at the bound load as
 *   before.
 * - CM_DecompressVis reads no byte past the visibility lump (FIX23, D1):
 *   the lump is made exactly MAX_MAP_VISIBILITY bytes long, so a read
 *   past it leaves the global array and the address sanitizer reports
 *   it, and the rest of a row whose input runs out is visible.
 * - CM_SetAreaPortalState refuses a portal number equal to the portal
 *   count and above it (FIX23, D3).
 * - The collision model bounds a leaf's cluster and every visibility
 *   offset (FIX137, cmvis_*): a lump whose count or offsets do not fit
 *   it and a leaf whose cluster the lump has no offsets for are refused
 *   at load, and CM_ClusterPVS and CM_ClusterPHS answer any other
 *   cluster outside the lump with the no-vis row.
 *
 * =======================================================================
 */

#include "test.h"

#include "../../src/client/refresh/files/models.c"

/* Each renderer supplies this; the node loader never calls it. */
struct image_s *
GetTexImage(const char *name, findimage_t find_image)
{
	(void)name;
	(void)find_image;
	return NULL;
}

/* The depth bound of both loaders (MAX_NODE_DEPTH in each). */
#define T_DEPTH 1024

#define G_MAXNODES (T_DEPTH + 8)
#define G_MAXLEAFS (G_MAXNODES + 2)

typedef struct
{
	dnode_t nodes[G_MAXNODES];
	int nnodes;
	dleaf_t leafs[G_MAXLEAFS];
	int nleafs;
	dmodel_t models[2];
	int nmodels;
	dareaportal_t portals[4];
	int nportals;
	const byte *vistail;	/* the last bytes of the visibility lump */
	int nvistail;
	/* the face lumps (FIX118), written when nfaces is set */
	dface_t faces[2];
	int nfaces;
	int surfedges[8];
	int nsurfedges;
	dedge_t edges[8];
	int nedges;
	dvertex_t verts[4];
	int nverts;
	int vis[8];		/* a small visibility lump, written when nvis is set */
	int nvis;
	texinfo_t texinfos[3];	/* the texinfo lump when ntexinfo is set, else one texinfo */
	int ntexinfo;
} gmap_t;

static gmap_t g;
static byte *gbuf;
static int gserial;

/* Leaf 0 is the solid error leaf no node names, as in every stock map. */
static void
g_leafs(int n)
{
	int i;

	memset(g.leafs, 0, sizeof(g.leafs));
	g.nleafs = n;
	g.leafs[0].contents = CONTENTS_SOLID;

	for (i = 1; i < n; i++)
	{
		g.leafs[i].cluster = -1;
	}

	/* leaf 1 is solid and holds the one brush */
	g.leafs[1].contents = CONTENTS_SOLID;
	g.leafs[1].numleafbrushes = 1;
}

static void
g_node(int i, int plane, int front, int back)
{
	g.nodes[i].planenum = plane;
	g.nodes[i].children[0] = front;
	g.nodes[i].children[1] = back;
}

#define LEAF(n) (-1 - (n))

/*
 * Stock-shaped: the world is node 0 on x = 0 with the solid leaf 1 behind
 * it and node 1 on y = 0 in front (leafs 2 and 3, clusters 0 and 1); the
 * one submodel has its own node 2 and leafs 4 and 5.
 */
static void
g_default(void)
{
	memset(&g, 0, sizeof(g));
	g_leafs(6);
	g.leafs[2].cluster = 0;
	g.leafs[3].cluster = 1;
	g_node(0, 0, 1, LEAF(1));
	g_node(1, 1, LEAF(2), LEAF(3));
	g_node(2, 1, LEAF(4), LEAF(5));
	g.nnodes = 3;
	g.models[0].mins[0] = g.models[0].mins[1] = g.models[0].mins[2] = -64;
	g.models[0].maxs[0] = g.models[0].maxs[1] = g.models[0].maxs[2] = 64;
	g.models[0].headnode = 0;
	g.models[1].headnode = 2;
	g.nmodels = 2;
}

/* Serialises g into gbuf; every lump 4-byte aligned. */
static int
g_build(void)
{
	static const char ents[] = "{\n\"classname\" \"worldspawn\"\n}\n";
	dheader_t *h;
	dplane_t planes[2];
	texinfo_t tex;
	unsigned short leafbrush = 0;
	dbrush_t brush;
	dbrushside_t side;
	darea_t area;
	int ofs = sizeof(dheader_t);

	if (!gbuf)
	{
		gbuf = malloc(MAX_MAP_VISIBILITY + 0x40000);
	}

	memset(gbuf, 0, MAX_MAP_VISIBILITY + 0x40000);
	memset(planes, 0, sizeof(planes));
	planes[0].normal[0] = 1;
	planes[0].type = PLANE_X;
	planes[1].normal[1] = 1;
	planes[1].type = PLANE_Y;
	memset(&tex, 0, sizeof(tex));
	Q_strlcpy(tex.texture, "e1u1/floor1_1", sizeof(tex.texture));
	memset(&brush, 0, sizeof(brush));
	brush.numsides = 1;
	brush.contents = CONTENTS_SOLID;
	memset(&side, 0, sizeof(side));
	memset(&area, 0, sizeof(area));

	h = (dheader_t *)gbuf;
	h->ident = IDBSPHEADER;
	h->version = BSPVERSION;

#define LUMP(N, PTR, LEN) \
	do { \
		h->lumps[N].fileofs = ofs; \
		h->lumps[N].filelen = (LEN); \
		memcpy(gbuf + ofs, (PTR), (LEN)); \
		ofs += ((LEN) + 3) & ~3; \
	} while (0)

	LUMP(LUMP_ENTITIES, ents, (int)sizeof(ents));
	LUMP(LUMP_PLANES, planes, (int)sizeof(planes));
	LUMP(LUMP_NODES, g.nodes, g.nnodes * (int)sizeof(dnode_t));
	if (g.ntexinfo)
	{
		LUMP(LUMP_TEXINFO, g.texinfos, g.ntexinfo * (int)sizeof(texinfo_t));
	}
	else
	{
		LUMP(LUMP_TEXINFO, &tex, (int)sizeof(tex));
	}
	LUMP(LUMP_LEAFS, g.leafs, g.nleafs * (int)sizeof(dleaf_t));
	LUMP(LUMP_LEAFBRUSHES, &leafbrush, (int)sizeof(leafbrush));
	LUMP(LUMP_MODELS, g.models, g.nmodels * (int)sizeof(dmodel_t));
	LUMP(LUMP_BRUSHES, &brush, (int)sizeof(brush));
	LUMP(LUMP_BRUSHSIDES, &side, (int)sizeof(side));
	LUMP(LUMP_AREAS, &area, (int)sizeof(area));
	LUMP(LUMP_AREAPORTALS, g.portals, g.nportals * (int)sizeof(dareaportal_t));

	if (g.nfaces)
	{
		LUMP(LUMP_FACES, g.faces, g.nfaces * (int)sizeof(dface_t));
		LUMP(LUMP_SURFEDGES, g.surfedges, g.nsurfedges * (int)sizeof(int));
		LUMP(LUMP_EDGES, g.edges, g.nedges * (int)sizeof(dedge_t));
		LUMP(LUMP_VERTEXES, g.verts, g.nverts * (int)sizeof(dvertex_t));
	}

	if (g.nvis)
	{
		LUMP(LUMP_VISIBILITY, g.vis, g.nvis * (int)sizeof(int));
	}

#undef LUMP

	if (g.nvistail)
	{
		/* MAX_MAP_VISIBILITY bytes: numclusters, every offset at the
		   tail, zeros, the tail as the lump's last bytes */
		byte *vis = gbuf + ofs;
		int *ints = (int *)vis;
		int tailofs = MAX_MAP_VISIBILITY - g.nvistail;
		int c, numclusters = 24;

		ints[0] = numclusters;

		for (c = 0; c < numclusters; c++)
		{
			ints[1 + 2 * c] = tailofs;
			ints[2 + 2 * c] = tailofs;
		}

		memcpy(vis + tailofs, g.vistail, g.nvistail);
		h->lumps[LUMP_VISIBILITY].fileofs = ofs;
		h->lumps[LUMP_VISIBILITY].filelen = MAX_MAP_VISIBILITY;
		ofs += MAX_MAP_VISIBILITY;
	}

	return ofs;
}

/* Writes gbuf under a fresh name (CM_LoadMap caches by name). */
static const char *
g_write(int len)
{
	static char name[MAX_QPATH];
	char rel[MAX_OSPATH];

	Com_sprintf(name, sizeof(name), "maps/t_graph%d.bsp", gserial++);
	Com_sprintf(rel, sizeof(rel), "data/baseq2/%s", name);
	t_write_file(rel, gbuf, len);
	return name;
}

/* The renderer side: the shared loader over the same bytes. */
static mleaf_t r_leafs[G_MAXLEAFS];
static cplane_t r_planes[2];
static mnode_t *r_nodes;
static int r_numnodes;
static void *r_hunk;

static void
r_load(const char *name)
{
	const dheader_t *h = (const dheader_t *)gbuf;
	int i;

	memset(r_leafs, 0, sizeof(r_leafs));

	for (i = 0; i < g.nleafs; i++)
	{
		r_leafs[i].contents = g.leafs[i].contents;
	}

	memset(r_planes, 0, sizeof(r_planes));
	r_planes[0].normal[0] = 1;
	r_planes[0].type = PLANE_X;
	r_planes[1].normal[1] = 1;
	r_planes[1].type = PLANE_Y;
	r_nodes = NULL;
	r_numnodes = 0;
	r_hunk = Hunk_Begin(G_MAXNODES * sizeof(mnode_t) + 4096);
	Mod_LoadNodes(name, r_planes, 2, r_leafs, g.nleafs, &r_nodes,
		&r_numnodes, gbuf, &h->lumps[LUMP_NODES]);
}

static void
r_free(void)
{
	/* Hunk_Free needs the size Hunk_End records */
	Hunk_End();
	Hunk_Free(r_hunk);
	r_hunk = NULL;
}

static int
r_leafat(float x, float y)
{
	vec3_t p;

	VectorSet(p, x, y, 0);
	return (int)(Mod_PointInLeaf(p, r_nodes) - r_leafs);
}

static int
cm_leafat(float x, float y)
{
	vec3_t p;

	VectorSet(p, x, y, 0);
	return CM_PointLeafnum(p);
}

TEST(bspgraph_stock_shape)
{
	const char *name;
	unsigned checksum;
	vec3_t a = { 10, 10, 0 }, b = { -10, 10, 0 }, zero = { 0, 0, 0 };
	trace_t tr;

	g_default();
	name = g_write(g_build());

	T_REQUIRE(CM_LoadMap(name, false, &checksum) != NULL);
	T_EQ_INT(cm_leafat(10, 10), 2);
	T_EQ_INT(cm_leafat(10, -10), 3);
	T_EQ_INT(cm_leafat(-10, 10), 1);
	T_EQ_INT(CM_PointContents(b, 0), CONTENTS_SOLID);
	T_EQ_INT(CM_PointContents(b, 2), 0);
	tr = CM_BoxTrace(a, b, zero, zero, 0, MASK_SOLID);
	T_CHECK(tr.fraction > 0.49f && tr.fraction < 0.5f);
	T_EQ_INT(tr.contents, CONTENTS_SOLID);

	r_load(name);
	T_EQ_INT(r_numnodes, 3);
	T_EQ_INT(r_leafat(10, 10), 2);
	T_EQ_INT(r_leafat(10, -10), 3);
	T_EQ_INT(r_leafat(-10, 10), 1);
	T_CHECK(r_nodes[0].parent == NULL);
	T_CHECK(r_nodes[1].parent == &r_nodes[0]);
	T_CHECK(r_leafs[3].parent == &r_nodes[1]);
	r_free();
}

/* A chain of n nodes, each with a leaf behind it, the last also in front. */
static void
g_chain(int n)
{
	int i;

	memset(&g, 0, sizeof(g));
	g_leafs(n + 2);

	for (i = 0; i < n; i++)
	{
		g_node(i, 0, (i + 1 < n) ? i + 1 : LEAF(n + 1), LEAF(i + 1));
	}

	g.nnodes = n;
	g.models[0].headnode = 0;
	g.nmodels = 1;
}

/* node 0 -> 1 -> 2, and node 2's front child is the root again */
static void
s_back_to_root(void)
{
	g_chain(3);
	g.nodes[2].children[0] = 0;
}

/* node 0 -> 1 -> 2, and node 2's front child is its parent */
static void
s_back_to_parent(void)
{
	g_chain(3);
	g.nodes[2].children[0] = 1;
}

/* node 0 -> 1 -> 2 and a ring 3 -> 4 -> 3 no root reaches, which the
   submodel names as its head */
static void
s_ring(void)
{
	g_chain(3);
	g_leafs(8);
	g_node(3, 0, 4, LEAF(5));
	g_node(4, 0, 3, LEAF(6));
	g.nnodes = 5;
	g.models[1].headnode = 3;
	g.nmodels = 2;
}

/* node 0 -> 1 and 2, both of which have node 3 in front */
static void
s_shared_node(void)
{
	memset(&g, 0, sizeof(g));
	g_leafs(6);
	g_node(0, 0, 1, 2);
	g_node(1, 1, 3, LEAF(1));
	g_node(2, 1, 3, LEAF(2));
	g_node(3, 1, LEAF(3), LEAF(4));
	g.nnodes = 4;
	g.nmodels = 1;
}

/* node 0 has leaf 2 behind it, and node 1 in front has it too */
static void
s_shared_leaf(void)
{
	memset(&g, 0, sizeof(g));
	g_leafs(4);
	g_node(0, 0, 1, LEAF(2));
	g_node(1, 1, LEAF(2), LEAF(3));
	g.nnodes = 2;
	g.nmodels = 1;
}

/* one node with the same leaf on both sides */
static void
s_same_leaf_twice(void)
{
	memset(&g, 0, sizeof(g));
	g_leafs(3);
	g_node(0, 0, LEAF(2), LEAF(2));
	g.nnodes = 1;
	g.nmodels = 1;
}

static void s_chain_past_bound(void) { g_chain(T_DEPTH + 1); }

/* Both loaders refuse the map with ERR_DROP, name it and say why. */
static void
refused_by_both(const char *what, void (*shape)(void), const char *why)
{
	const char *name;
	unsigned checksum;

	shape();
	name = g_write(g_build());

	T_EXPECT_DROP(CM_LoadMap(name, false, &checksum));
	if (!strstr(t_console(), name) || !strstr(t_console(), why))
	{
		t_fail(__FILE__, __LINE__, "%s: collision message: %s", what, t_console());
	}

	T_EXPECT_DROP(r_load(name));
	if (!strstr(t_console(), name) || !strstr(t_console(), why))
	{
		t_fail(__FILE__, __LINE__, "%s: renderer message: %s", what, t_console());
	}

	r_free();
}

#define CYCLE "not a tree: cycle through node 0"
#define TWICE "not a tree: child with a second parent"
#define DEEP "not a tree: path too deep at 1024"

TEST(bspgraph_back_to_root) { refused_by_both("back to root", s_back_to_root, CYCLE); }
TEST(bspgraph_back_to_parent) { refused_by_both("back to parent", s_back_to_parent, TWICE " 1"); }
TEST(bspgraph_ring) { refused_by_both("ring", s_ring, "not a tree: cycle through node 3"); }
TEST(bspgraph_shared_node) { refused_by_both("shared node", s_shared_node, TWICE " 3"); }
TEST(bspgraph_shared_leaf) { refused_by_both("shared leaf", s_shared_leaf, TWICE " -3"); }
TEST(bspgraph_same_leaf_twice) { refused_by_both("same leaf twice", s_same_leaf_twice, TWICE " -3"); }
TEST(bspgraph_chain_past_bound) { refused_by_both("chain past bound", s_chain_past_bound, DEEP); }

/* The collision model refuses a map without nodes first ("with no
   nodes"); the renderer's loader must not walk an empty node array. */
TEST(bspgraph_renderer_no_nodes)
{
	const char *name;

	g_default();
	g.nnodes = 0;
	name = g_write(g_build());

	T_EXPECT_DROP(r_load(name));
	T_CONSOLE_HAS("not a tree: node or leaf count 0");
	r_free();
}

/* The renderers' walks tell a leaf from a node by its contents: a leaf
   whose contents read as CONTENTS_NODE is refused before any walk, and
   a leaf with other contents loads. */
TEST(bspgraph_renderer_leaf_node_contents)
{
	const char *name;

	g_default();
	g.leafs[2].contents = CONTENTS_NODE;
	name = g_write(g_build());

	T_EXPECT_DROP(r_load(name));
	T_CONSOLE_HAS("leaf 2 of");
	T_CONSOLE_HAS("has the node contents");
	r_free();

	g_default();
	g.leafs[2].contents = CONTENTS_NODE - 1;
	name = g_write(g_build());
	r_load(name);
	T_CHECK(r_leafs[2].parent != NULL);
	r_free();
}

TEST(bspgraph_chain_at_bound)
{
	const char *name;
	unsigned checksum;
	vec3_t a = { 10, 0, 0 }, b = { -10, 0, 0 }, zero = { 0, 0, 0 };
	trace_t tr;

	T_EQ_INT(MAX_NODE_DEPTH, T_DEPTH);
	g_chain(T_DEPTH);
	name = g_write(g_build());

	T_REQUIRE(CM_LoadMap(name, false, &checksum) != NULL);
	T_EQ_INT(cm_leafat(10, 0), T_DEPTH + 1);
	T_EQ_INT(cm_leafat(-10, 0), 1);
	tr = CM_BoxTrace(a, b, zero, zero, 0, MASK_SOLID);
	T_CHECK(tr.fraction > 0.49f && tr.fraction < 0.5f);

	r_load(name);
	T_EQ_INT(r_leafat(10, 0), T_DEPTH + 1);
	T_CHECK(r_leafs[T_DEPTH + 1].parent == &r_nodes[T_DEPTH - 1]);
	r_free();
}

/* The PVS row of cluster 0 (24 clusters, 3 bytes) decoded from a lump
   that ends with the given bytes. */
static void
vis_row(const byte *tail, int ntail, byte *row)
{
	const char *name;
	unsigned checksum;

	g_default();
	g.leafs[3].cluster = 23;
	g.vistail = tail;
	g.nvistail = ntail;
	name = g_write(g_build());
	T_REQUIRE(CM_LoadMap(name, false, &checksum) != NULL);
	T_EQ_INT(CM_NumClusters(), 24);
	memcpy(row, CM_ClusterPVS(0), 3);
}

TEST(bspgraph_vis_ends_at_lump_end)
{
	static const byte run_last[] = { 0x11, 0x00, 0x02 };
	static const byte byte_last[] = { 0x00, 0x02, 0x33 };
	byte row[3];

	/* the count of a zero run is the lump's last byte */
	vis_row(run_last, sizeof(run_last), row);
	T_EQ_INT(row[0], 0x11);
	T_EQ_INT(row[1], 0x00);
	T_EQ_INT(row[2], 0x00);

	/* a literal byte is the lump's last byte */
	vis_row(byte_last, sizeof(byte_last), row);
	T_EQ_INT(row[0], 0x00);
	T_EQ_INT(row[1], 0x00);
	T_EQ_INT(row[2], 0x33);
}

TEST(bspgraph_vis_run_count_past_end)
{
	static const byte tail[] = { 0x11, 0x00 };
	byte row[3];

	vis_row(tail, sizeof(tail), row);
	T_EQ_INT(row[0], 0x11);
	T_EQ_INT(row[1], 0xff);
	T_EQ_INT(row[2], 0xff);
}

TEST(bspgraph_vis_zero_runs_at_end)
{
	static const byte tail[] = { 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	byte row[3];

	vis_row(tail, sizeof(tail), row);
	T_EQ_INT(row[0], 0x11);
	T_EQ_INT(row[1], 0xff);
	T_EQ_INT(row[2], 0xff);
}

TEST(bspgraph_areaportal_bound)
{
	const char *name;
	unsigned checksum;

	g_default();
	g.nportals = 3;
	name = g_write(g_build());
	T_REQUIRE(CM_LoadMap(name, false, &checksum) != NULL);

	CM_SetAreaPortalState(2, true);
	T_EXPECT_DROP(CM_SetAreaPortalState(3, true));
	T_CONSOLE_HAS("areaportal > numareaportals");
	T_EXPECT_DROP(CM_SetAreaPortalState(4, true));
	T_CONSOLE_HAS("areaportal > numareaportals");
	T_EXPECT_DROP(CM_SetAreaPortalState(-1, true));
	CM_SetAreaPortalState(2, false);
}

/*
 * The collision model's visibility (FIX137): CMod_LoadVisibility refuses
 * a lump whose cluster count does not fit it or whose PVS or PHS offset
 * is outside it, CM_LoadMap refuses a leaf whose cluster is neither -1
 * nor below the lump's count, and CM_ClusterPVS and CM_ClusterPHS answer
 * a cluster outside the lump with the no-vis row (all visible). The
 * stock shape is g_default's two clusters in a 24-byte lump: the count,
 * the four offsets, then one byte of each row.
 */
static void
v_map(void)
{
	g.vis[0] = 2;
	g.vis[1] = 20;	/* cluster 0 PVS */
	g.vis[2] = 21;	/* cluster 0 PHS */
	g.vis[3] = 22;	/* cluster 1 PVS */
	g.vis[4] = 23;	/* cluster 1 PHS */
	g.vis[5] = LittleLong(0x02030201);	/* bytes 20..23: 01 02 03 02 */
	g.nvis = 6;
}

static const char *
v_load(void)
{
	return g_write(g_build());
}

TEST(cmvis_stock_shape_rows)
{
	unsigned checksum;

	g_default();
	v_map();
	T_REQUIRE(CM_LoadMap(v_load(), false, &checksum) != NULL);
	T_EQ_INT(CM_NumClusters(), 2);
	T_EQ_INT(CM_ClusterPVS(0)[0], 0x01);
	T_EQ_INT(CM_ClusterPHS(0)[0], 0x02);
	T_EQ_INT(CM_ClusterPVS(1)[0], 0x03);
	T_EQ_INT(CM_ClusterPHS(1)[0], 0x02);
	T_EQ_INT(CM_ClusterPVS(-1)[0], 0x00);
	T_EQ_INT(CM_ClusterPHS(-1)[0], 0x00);
}

/* a map that must be refused at load, with why in the message */
static void
v_refused(const char *why)
{
	const char *name = v_load();
	unsigned checksum;

	T_EXPECT_DROP(CM_LoadMap(name, false, &checksum));
	if (!strstr(t_console(), name) || !strstr(t_console(), why))
	{
		t_fail(__FILE__, __LINE__, "message: %s", t_console());
	}
}

TEST(cmvis_leaf_cluster_minus_two)
{
	g_default();
	v_map();
	g.leafs[3].cluster = -2;
	v_refused("leaf 3 has cluster -2, the visibility has 2 clusters");
}

TEST(cmvis_leaf_cluster_at_count)
{
	g_default();
	v_map();
	g.leafs[3].cluster = 2;
	v_refused("leaf 3 has cluster 2, the visibility has 2 clusters");
}

TEST(cmvis_pvs_offset_negative)
{
	g_default();
	v_map();
	g.vis[3] = -4;
	v_refused("cluster 1 has its visibility at -4, outside 24 bytes");
}

TEST(cmvis_phs_offset_past_lump)
{
	g_default();
	v_map();
	g.vis[2] = 25;
	v_refused("cluster 0 has its visibility at 25, outside 24 bytes");
}

TEST(cmvis_clusters_past_lump)
{
	g_default();
	v_map();
	g.vis[0] = 3;	/* three clusters' offsets do not fit in 24 bytes */
	v_refused("visibility of 3 clusters in 24 bytes");

	g_default();
	v_map();
	g.vis[0] = -1;
	v_refused("visibility of -1 clusters in 24 bytes");

	/* a lump of 2 bytes, too short for its count */
	g_default();
	v_map();
	g.nvis = 1;
	{
		int len = g_build();
		const char *name;
		unsigned checksum;

		((dheader_t *)gbuf)->lumps[LUMP_VISIBILITY].filelen = 2;
		name = g_write(len);
		T_EXPECT_DROP(CM_LoadMap(name, false, &checksum));
		T_CONSOLE_HAS("has 2 bytes of visibility");
	}
}

/*
 * Defence in depth: a cluster the lump has no offsets for (a leaf of a
 * map without visibility may carry any cluster, and a caller may pass
 * one) is answered with the no-vis row, reading nothing before or after
 * the lump's table. Cluster 2's offsets would be the row bytes, and -2's
 * the 16 bytes before map_visibility.
 */
TEST(cmvis_cluster_outside_lump_is_novis)
{
	unsigned checksum;

	g_default();
	v_map();
	g.vis[5] = LittleLong(-1000);	/* cluster 2's PVS offset, if read */
	T_REQUIRE(CM_LoadMap(v_load(), false, &checksum) != NULL);
	T_EQ_INT(CM_ClusterPVS(2)[0], 0xff);
	T_EQ_INT(CM_ClusterPHS(2)[0], 0xff);
	T_EQ_INT(CM_ClusterPVS(-2)[0], 0xff);
	T_EQ_INT(CM_ClusterPHS(-2)[0], 0xff);
	T_EQ_INT(CM_ClusterPVS(0)[0], 0x18);	/* the low byte of -1000 */
}

/* Without visibility a leaf's cluster is not checked, and every row but
   -1's is all visible, as before. */
TEST(cmvis_no_vis_any_cluster)
{
	unsigned checksum;

	g_default();
	g.leafs[3].cluster = -2;
	T_REQUIRE(CM_LoadMap(v_load(), false, &checksum) != NULL);
	T_EQ_INT(CM_ClusterPVS(0)[0], 0xff);
	T_EQ_INT(CM_ClusterPVS(-2)[0], 0xff);
	T_EQ_INT(CM_ClusterPHS(-2)[0], 0xff);
	T_EQ_INT(CM_ClusterPVS(-1)[0], 0x00);
}

/*
 * The shared BSP checks (FIX118) every renderer's Mod_LoadBrushModel runs
 * before it reads a lump, in its order: the lumps, the visibility, the
 * nodes, the face edges. The stock shape gets one triangle (face 0 of
 * node 0) and a two-cluster visibility lump; each hostile map breaks one
 * bound and is refused by its check with the reason, the checks before
 * it passing.
 */
static char c_why[256];

/* the index of the check that refuses gbuf, or 4 when all four pass */
static int
c_check(int len)
{
	const dheader_t *h = (const dheader_t *)gbuf;

	c_why[0] = '\0';

	if (!Mod_CheckLumps(gbuf, h, len, c_why, sizeof(c_why)))
	{
		return 0;
	}

	if (!Mod_CheckVis(gbuf, h, c_why, sizeof(c_why)))
	{
		return 1;
	}

	if (!Mod_CheckNodes(gbuf, h, c_why, sizeof(c_why)))
	{
		return 2;
	}

	if (!Mod_CheckFaceEdges(gbuf, h, c_why, sizeof(c_why)))
	{
		return 3;
	}

	return 4;
}

static void
c_default(void)
{
	g_default();
	g.nverts = 3;
	g.edges[1].v[0] = 0;
	g.edges[1].v[1] = 1;
	g.edges[2].v[0] = 1;
	g.edges[2].v[1] = 2;
	g.edges[3].v[0] = 2;
	g.edges[3].v[1] = 0;
	g.nedges = 4;	/* edge 0 is unused, as in every stock map */
	g.surfedges[0] = 1;
	g.surfedges[1] = 2;
	g.surfedges[2] = -3;
	g.nsurfedges = 3;
	g.faces[0].firstedge = 0;
	g.faces[0].numedges = 3;
	g.nfaces = 1;
	g.nodes[0].firstface = 0;
	g.nodes[0].numfaces = 1;
	/* two clusters in 24 bytes: the offsets, then one byte of each row */
	g.vis[0] = 2;
	g.vis[1] = 20;
	g.vis[2] = 21;
	g.vis[3] = 22;
	g.vis[4] = 23;
	g.nvis = 6;
}

#define C_REFUSED(LEN, CHECK, WHY) \
	do { \
		T_EQ_INT(c_check(LEN), (CHECK)); \
		if (!strstr(c_why, (WHY))) \
		{ \
			t_fail(__FILE__, __LINE__, "reason: %s", c_why); \
		} \
	} while (0)

TEST(bspcheck_stock_shape_passes)
{
	c_default();
	T_EQ_INT(c_check(g_build()), 4);
	T_EQ_STR(c_why, "");
}

TEST(bspcheck_lump_past_file_end)
{
	int len;

	c_default();
	len = g_build();
	((dheader_t *)gbuf)->lumps[LUMP_LEAFS].filelen = len;
	C_REFUSED(len, 0, "lump 8 (");

	/* the file cut 4 bytes short: its last lump passes the end */
	c_default();
	len = g_build();
	C_REFUSED(len - 4, 0, "is not inside the");

	/* a lump at an offset that is not 4-aligned */
	c_default();
	len = g_build();
	((dheader_t *)gbuf)->lumps[LUMP_PLANES].fileofs += 2;
	C_REFUSED(len, 0, "lump 1 (");

	/* a header cut short */
	C_REFUSED((int)sizeof(dheader_t) - 1, 0, "too short for a BSP header");
}

TEST(bspcheck_vis_offset_past_lump)
{
	c_default();
	g.vis[3] = 25;	/* the PVS of cluster 1 at 25 of 24 bytes */
	C_REFUSED(g_build(), 1, "cluster 1 has its visibility at 25, outside 24 bytes");

	c_default();
	g.vis[0] = 3;	/* three clusters' offsets do not fit in 24 bytes */
	C_REFUSED(g_build(), 1, "visibility of 3 clusters in 24 bytes");
}

TEST(bspcheck_node_child_out_of_range)
{
	c_default();
	g_node(1, 1, LEAF(2), 3);	/* node 3 of 3 */
	C_REFUSED(g_build(), 2, "node 1 names node 3");

	c_default();
	g_node(1, 1, LEAF(6), LEAF(3));	/* leaf 6 of 6 */
	C_REFUSED(g_build(), 2, "node 1 names leaf 6, the map has 6");

	c_default();
	g.nodes[1].firstface = 0;	/* face 0 in node 0 and node 1 */
	g.nodes[1].numfaces = 1;
	C_REFUSED(g_build(), 2, "node 1 has surfaces 0 to 1 of 1, or one of another node");

	c_default();
	g.nodes[0].numfaces = 2;	/* faces 0 and 1 of 1 */
	C_REFUSED(g_build(), 2, "node 0 has surfaces 0 to 2 of 1");

	c_default();
	g.models[0].headnode = 3;	/* model 0 at node 3 of 3 */
	C_REFUSED(g_build(), 2, "3 nodes and 2 models, model 0 at node 3");
}

TEST(bspcheck_face_edges_past_surfedges)
{
	c_default();
	g.faces[0].firstedge = 1;	/* surfedges 1 to 3 of 3 */
	C_REFUSED(g_build(), 3, "face 0 has 3 edges from 1, the map has 3");

	c_default();
	g.faces[0].numedges = 2;	/* fewer than 3 */
	C_REFUSED(g_build(), 3, "face 0 has 2 edges from 0");

	c_default();
	g.surfedges[2] = -4;	/* edge 4 of 4 */
	C_REFUSED(g_build(), 3, "face 0 names edge -4, the map has 4");

	c_default();
	g.edges[3].v[0] = 3;	/* vertex 3 of 3 */
	C_REFUSED(g_build(), 3, "face 0 has an edge past the 3 vertices");
}

/*
 * dnode_t.firstface and numfaces are unsigned short on disk: a map with
 * more than 32768 faces has nodes whose faces start at 32768 or beyond,
 * and Mod_LoadNodes reads them unsigned. The face lump is sized for
 * 0x8001 faces after the built lumps; node 1 holds the last of them.
 */
TEST(bspcheck_node_faces_past_32k)
{
	dheader_t *h;
	int len, ofs;

	c_default();
	g.nodes[1].firstface = 0x8000;
	g.nodes[1].numfaces = 1;
	len = g_build();
	h = (dheader_t *)gbuf;
	ofs = (len + 3) & ~3;
	h->lumps[LUMP_FACES].fileofs = ofs;
	h->lumps[LUMP_FACES].filelen = 0x8001 * (int)sizeof(dface_t);
	len = ofs + h->lumps[LUMP_FACES].filelen;
	T_REQUIRE(len <= MAX_MAP_VISIBILITY + 0x40000);

	c_why[0] = '\0';
	T_EQ_INT(Mod_CheckLumps(gbuf, h, len, c_why, sizeof(c_why)), true);
	T_EQ_INT(Mod_CheckNodes(gbuf, h, c_why, sizeof(c_why)), true);
	T_EQ_STR(c_why, "");

	/* one past the lump is still refused */
	((dnode_t *)(gbuf + h->lumps[LUMP_NODES].fileofs))[1].firstface = 0x8001;
	T_EQ_INT(Mod_CheckNodes(gbuf, h, c_why, sizeof(c_why)), false);
	T_CHECK(strstr(c_why, "node 1 has surfaces 32769 to 32770 of 32769") != NULL);
}

/* n texinfos and their nexts (0 or less ends a chain, as Mod_LoadTexinfo reads it) */
static void
c_texinfos(int n, int next0, int next1, int next2)
{
	int i;

	c_default();
	g.ntexinfo = n;

	for (i = 0; i < n; i++)
	{
		Q_strlcpy(g.texinfos[i].texture, "e1u1/floor1_1", sizeof(g.texinfos[i].texture));
	}

	g.texinfos[0].nexttexinfo = next0;
	g.texinfos[1].nexttexinfo = next1;
	g.texinfos[2].nexttexinfo = next2;
}

TEST(bspcheck_texinfo_chains)
{
	/* a stock animation, 1 -> 2 -> 1, and texinfo 0 not animated */
	c_texinfos(3, -1, 2, 1);
	T_EQ_INT(c_check(g_build()), 4);

	/* texinfo 0 runs into the 2-cycle 1 -> 2 -> 1 and never comes back */
	c_texinfos(3, 1, 2, 1);
	C_REFUSED(g_build(), 0, "a texinfo animation chain leaves the lump or never ends");

	/* a next of 3 in a lump of 3 */
	c_texinfos(3, -1, 3, -1);
	C_REFUSED(g_build(), 0, "a texinfo animation chain leaves the lump or never ends");

	/* a 32-byte name with no NUL */
	c_texinfos(3, -1, -1, -1);
	memset(g.texinfos[2].texture, 'a', sizeof(g.texinfos[2].texture));
	C_REFUSED(g_build(), 0, "texinfo 2 has an unterminated texture name");
}

/*
 * A header whose lumps are all empty at the end of the header, but lump,
 * filelen bytes of zeros there; the caller frees it.
 */
static byte *
c_flat(int lump, int filelen, int *len)
{
	byte *buf = calloc(1, sizeof(dheader_t) + (size_t)filelen);
	dheader_t *h = (dheader_t *)buf;
	int i;

	T_REQUIRE(buf != NULL);

	h->ident = IDBSPHEADER;
	h->version = BSPVERSION;

	for (i = 0; i < HEADER_LUMPS; i++)
	{
		h->lumps[i].fileofs = (int)sizeof(dheader_t);
	}

	h->lumps[lump].filelen = filelen;
	*len = (int)sizeof(dheader_t) + filelen;
	return buf;
}

TEST(bspcheck_lump_limits)
{
	byte *buf;
	int len;

	/* the lighting at its limit passes, one byte over it is refused */
	buf = c_flat(LUMP_LIGHTING, MAX_MAP_LIGHTING, &len);
	T_EQ_INT(Mod_CheckLumps(buf, (dheader_t *)buf, len, c_why, sizeof(c_why)), true);
	free(buf);

	buf = c_flat(LUMP_LIGHTING, MAX_MAP_LIGHTING + 1, &len);
	T_EQ_INT(Mod_CheckLumps(buf, (dheader_t *)buf, len, c_why, sizeof(c_why)), false);
	T_EQ_STR(c_why, "2097153 bytes of lighting, more than 2097152");
	free(buf);

	/* the edges at their limit pass, one over it is refused */
	buf = c_flat(LUMP_EDGES, MAX_MAP_EDGES * (int)sizeof(dedge_t), &len);
	T_EQ_INT(Mod_CheckLumps(buf, (dheader_t *)buf, len, c_why, sizeof(c_why)), true);
	free(buf);

	buf = c_flat(LUMP_EDGES, (MAX_MAP_EDGES + 1) * (int)sizeof(dedge_t), &len);
	T_EQ_INT(Mod_CheckLumps(buf, (dheader_t *)buf, len, c_why, sizeof(c_why)), false);
	T_EQ_STR(c_why, "128001 edges, more than 128000");
	free(buf);
}

/*
 * More clusters than MAX_MAP_LEAFS, in a visibility lump with room for
 * all their offsets (each 0, inside the lump): only the leaf bound
 * refuses it.
 */
TEST(bspcheck_vis_clusters_past_leafs)
{
	byte *buf;
	int len;

	buf = c_flat(LUMP_VISIBILITY, 4 + 8 * MAX_MAP_LEAFS, &len);
	*(int *)(buf + sizeof(dheader_t)) = MAX_MAP_LEAFS;
	T_EQ_INT(Mod_CheckVis(buf, (dheader_t *)buf, c_why, sizeof(c_why)), true);
	free(buf);

	buf = c_flat(LUMP_VISIBILITY, 4 + 8 * (MAX_MAP_LEAFS + 1), &len);
	*(int *)(buf + sizeof(dheader_t)) = MAX_MAP_LEAFS + 1;
	T_EQ_INT(Mod_CheckVis(buf, (dheader_t *)buf, c_why, sizeof(c_why)), false);
	T_EQ_STR(c_why, "visibility of 65537 clusters in 524300 bytes");
	free(buf);
}
