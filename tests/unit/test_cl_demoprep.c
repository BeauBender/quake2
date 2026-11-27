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
 * A demo's first frame and the client's collision map (FIX215).
 *
 * A demo server (SV_SpawnServer with ss_demo) loads no real map:
 * CM_LoadMap("") leaves the collision model, which the client shares,
 * with no model at all. The client loads the demo's map itself when the
 * demo's own "precache" text runs (CL_Precache_f). That text arrives as
 * svc_stufftext and goes to the end of the command buffer, so a script
 * that started the demo and then waits (`demomap demo1.dm2` followed by
 * `wait`s, exec'd at startup) holds it back while the demo's frames
 * keep coming. The first valid frame makes the client active, and
 * CL_PrepRefresh then asks CM_InlineModel for "*1" of a map that is not
 * loaded: "CM_InlineModel: bad number".
 *
 * CL_ParseFrame in src/client/cl_parse.c now loads the map the
 * client's configstrings name when the client becomes active, as the
 * old demos' precache does; a map already loaded (every live
 * connection) is the cached one, unchanged. The tests reach it through
 * CL_ParseServerMessage with a real svc_frame message.
 *
 * =======================================================================
 */

#include "test.h"
#include "../../src/client/header/client.h"
#include "stub_client.h"

static byte msgbuf[4096];   /* the message the client parses */
static byte chanbuf[4096];  /* the netchan message the client writes */

/* The smallest map with an inline model: the world and submodel 1, both
   over one node splitting a solid leaf from an empty one. */
static const char *
write_map(void)
{
	static byte buf[4096];
	static char name[MAX_QPATH];
	static int serial;
	static const char ents[] =
		"{\n\"classname\" \"worldspawn\"\n}\n"
		"{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n}\n";
	char rel[MAX_OSPATH];
	dheader_t *h = (dheader_t *)buf;
	dplane_t plane;
	dnode_t node;
	texinfo_t tex;
	dleaf_t leafs[2];
	unsigned short leafbrush = 0;
	dmodel_t models[2];
	dbrush_t brush;
	dbrushside_t side;
	darea_t area;
	int ofs = sizeof(dheader_t);
	int i;

	memset(buf, 0, sizeof(buf));
	memset(&plane, 0, sizeof(plane));
	memset(&node, 0, sizeof(node));
	memset(&tex, 0, sizeof(tex));
	memset(leafs, 0, sizeof(leafs));
	memset(models, 0, sizeof(models));
	memset(&brush, 0, sizeof(brush));
	memset(&side, 0, sizeof(side));
	memset(&area, 0, sizeof(area));

	plane.normal[0] = 1;
	plane.type = PLANE_X;
	node.children[0] = -2;
	node.children[1] = -1;
	Q_strlcpy(tex.texture, "e1u1/floor1_1", sizeof(tex.texture));
	leafs[0].contents = CONTENTS_SOLID;
	leafs[0].cluster = -1;
	leafs[0].numleafbrushes = 1;
	leafs[1].cluster = 0;

	for (i = 0; i < 2; i++)
	{
		VectorSet(models[i].mins, -64, -64, -64);
		VectorSet(models[i].maxs, 64, 64, 64);
	}

	brush.numsides = 1;
	brush.contents = CONTENTS_SOLID;

	h->ident = IDBSPHEADER;
	h->version = BSPVERSION;

#define LUMP(N, PTR, LEN) \
	do { \
		h->lumps[N].fileofs = ofs; \
		h->lumps[N].filelen = (LEN); \
		memcpy(buf + ofs, (PTR), (LEN)); \
		ofs += ((LEN) + 3) & ~3; \
	} while (0)

	LUMP(LUMP_ENTITIES, ents, (int)sizeof(ents));
	LUMP(LUMP_PLANES, &plane, (int)sizeof(plane));
	LUMP(LUMP_NODES, &node, (int)sizeof(node));
	LUMP(LUMP_TEXINFO, &tex, (int)sizeof(tex));
	LUMP(LUMP_LEAFS, leafs, (int)sizeof(leafs));
	LUMP(LUMP_LEAFBRUSHES, &leafbrush, (int)sizeof(leafbrush));
	LUMP(LUMP_MODELS, models, (int)sizeof(models));
	LUMP(LUMP_BRUSHES, &brush, (int)sizeof(brush));
	LUMP(LUMP_BRUSHSIDES, &side, (int)sizeof(side));
	LUMP(LUMP_AREAS, &area, (int)sizeof(area));

#undef LUMP

	/* a fresh name each time: CM_LoadMap caches by name */
	Com_sprintf(name, sizeof(name), "maps/t_demoprep%d.bsp", serial++);
	Com_sprintf(rel, sizeof(rel), "data/baseq2/%s", name);
	t_write_file(rel, buf, ofs);
	return name;
}

/* One valid frame from scratch, with no entities: what a demo sends
   after its signon. */
static void
build_frame(void)
{
	SZ_Init(&net_message, msgbuf, sizeof(msgbuf));
	MSG_WriteByte(&net_message, svc_frame);
	MSG_WriteLong(&net_message, 1);      /* server frame */
	MSG_WriteLong(&net_message, -1);     /* no delta frame: from scratch */
	MSG_WriteByte(&net_message, 0);      /* suppress count */
	MSG_WriteByte(&net_message, 0);      /* no area bits */
	MSG_WriteByte(&net_message, svc_playerinfo);
	MSG_WriteShort(&net_message, 0);     /* no player state fields */
	MSG_WriteLong(&net_message, 0);      /* no stats */
	MSG_WriteByte(&net_message, svc_packetentities);
	MSG_WriteByte(&net_message, 0);      /* entity 0 ends the list */
	MSG_WriteByte(&net_message, 0);
	MSG_BeginReading(&net_message);
}

/* The client after a demo's signon: connected, its configstrings set,
   attractloop from the demo's serverdata. */
static void
reset_demo_client(const char *map)
{
	stub_client_cvars();
	memset(&cls, 0, sizeof(cls));
	memset(&cl, 0, sizeof(cl));
	SZ_Init(&cls.netchan.message, chanbuf, sizeof(chanbuf));
	cls.state = ca_connected;
	cls.serverProtocol = PROTOCOL_VERSION;
	cl.attractloop = true;
	Q_strlcpy(cl.configstrings[CS_MODELS + 1], map,
		sizeof(cl.configstrings[CS_MODELS + 1]));
}

/* The demo's first frame arrives before its precache ran: the client
   loads the demo's map, so its inline models are there for
   CL_PrepRefresh. */
TEST(cl_demo_first_frame_loads_collision_map)
{
	unsigned checksum;
	const char *map;

	map = write_map();
	reset_demo_client(map);

	/* what SV_SpawnServer does for a demo: no model at all */
	CM_LoadMap("", false, &checksum);
	T_EQ_INT(CM_NumInlineModels(), 0);

	build_frame();
	CL_ParseServerMessage();

	T_EQ_INT(cls.state, ca_active);
	T_EQ_INT(CM_NumInlineModels(), 2);
	T_CHECK(CM_InlineModel("*1") != NULL);
	T_EQ_STR(CM_EntityString(), "{\n\"classname\" \"worldspawn\"\n}\n"
		"{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n}\n");
}

/* The map is loaded already (a live connection, or a demo whose
   precache ran first): it is the cached one, not loaded again. With
   flushmap set and the map file cut short, any load that is not a
   client load (a reload, or the cached path that closes the area
   portals a listen server's game opened) ERR_DROPs, and the runner
   fails the test on it. */
TEST(cl_first_frame_keeps_loaded_map)
{
	unsigned checksum;
	const char *map;
	char rel[MAX_OSPATH];

	map = write_map();
	reset_demo_client(map);
	cl.attractloop = false;
	CM_LoadMap(map, true, &checksum);
	T_EQ_INT(CM_NumInlineModels(), 2);

	Com_sprintf(rel, sizeof(rel), "data/baseq2/%s", map);
	t_write_file(rel, "x", 1);
	Cvar_Get("flushmap", "0", 0);
	Cvar_Set("flushmap", "1");

	build_frame();
	CL_ParseServerMessage();

	Cvar_Set("flushmap", "0");
	T_EQ_INT(cls.state, ca_active);
	T_EQ_INT(CM_NumInlineModels(), 2);
	T_EQ_STR(CM_EntityString(), "{\n\"classname\" \"worldspawn\"\n}\n"
		"{\n\"classname\" \"func_door\"\n\"model\" \"*1\"\n}\n");
	T_CHECK(strstr(t_console(), "ERROR") == NULL);
}

/* No map in the configstrings (a cinematic or a picture): nothing is
   loaded and nothing is dropped. */
TEST(cl_first_frame_without_map_loads_nothing)
{
	unsigned checksum;

	reset_demo_client("");
	CM_LoadMap("", false, &checksum);

	build_frame();
	CL_ParseServerMessage();

	T_EQ_INT(cls.state, ca_active);
	T_EQ_INT(CM_NumInlineModels(), 0);
	T_EQ_STR(t_console(), "");

	/* leave a real map loaded: a second CM_LoadMap("") would return
	   the cached checksum of the last real map, which cm_empty_name
	   does not expect */
	CM_LoadMap(write_map(), false, &checksum);
}
