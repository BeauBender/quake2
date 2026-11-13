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
 * Client state stubs. The test binary links client objects, among them
 * src/client/cl_download.c, so that CL_DownloadFilter (static, reached
 * through the public CL_Download_f and CL_CheckOrDownloadFile) and
 * CL_ParseDownload run as shipped. Linking all of the client would
 * pull in SDL, the renderer and sound; these definitions stand in for the
 * client state cl_download.c reads. Everything else it calls (MSG_*, FS_*,
 * Cmd_*, Com_*, the collision map tables) is the real dedicated-server
 * code.
 *
 * =======================================================================
 */

#include "../../src/client/header/client.h"

/* Client globals (defined in cl_main.c and cl_parse.c in the client). */
client_static_t cls;
client_state_t cl;
cvar_t *cl_nodownload_list;

int precache_check;
int precache_spawncount;
int precache_tex;
int precache_model_skin;
byte *precache_model;

/* Called by CL_RequestNextDownload only once all downloads are done
   with cls.state == ca_connected, which no test sets up. */
void
CL_PrepRefresh(void)
{
}
