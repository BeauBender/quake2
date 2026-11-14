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
 * Stubs for linking four more client files into the test binary:
 * cl_parse.c, cl_network.c, cl_entities.c and cl_screen.c, plus the
 * sound frontend sound.c. They stand in for everything those files call
 * that lives in the renderer, the SDL sound mixer, the input code, the
 * console, the menu, the particle and temp-entity code and cl_main.c.
 * Every function is empty; the few that record what they were called
 * with (V_AddLight, Draw_CharScaled, Draw_PicScaled)
 * say so. Nothing here changes the code under test.
 *
 * What cannot be tested through these stubs:
 *
 * - The message types whose parser is a stub here read no bytes and
 *   apply none of their own checks: svc_temp_entity (CL_ParseTEnt),
 *   svc_muzzleflash and svc_muzzleflash2 (CL_AddMuzzleFlash, ...2),
 *   svc_inventory (CL_ParseInventory), the status bar configstrings
 *   (CL_ParseStatusMessage), lightstyle configstrings (CL_SetLightstyle)
 *   and entity events (CL_EntityEvent). Each of these stubs fails the
 *   running test when it is called, so a test that sends one of those
 *   messages cannot pass by accident.
 * - CL_AllocEntity gives a table that is already as large as an entity
 *   number can be, so the bounds checks on cl_numentities in
 *   cl_entities.c and sound.c are never the deciding check here; the
 *   entity-number check in CL_AllocEntity itself is the real one.
 * - CL_MaxClients returns a constant 1: the clamp the real function
 *   applies to the maxclients configstring is not exercised.
 *
 * =======================================================================
 */

#include "../../src/client/header/client.h"
#include "../../src/client/sound/header/local.h"
#include "stub_client.h"
#include "test.h"

/* ---- recorded calls ---- */

stub_calls_t stub_calls;

void V_AddLight(vec3_t org, float intensity, float r, float g, float b)
{
	stub_calls.lights++;
	stub_calls.last_light = intensity;
}

void Draw_CharScaled(int x, int y, int num, float scale) { stub_calls.chars++; }
void Draw_PicScaled(int x, int y, const char *pic, float f) { stub_calls.pics++; }
void Draw_GetPicSize(int *w, int *h, const char *name) { *w = 8; *h = 8; }

/* A parser that is only a stub: reaching it means the test is not
   testing what it thinks, so it fails the running test. */
static void
stub_not_reachable(const char *name)
{
	t_fail(__FILE__, __LINE__, "%s is an empty stub and was called; "
		"the real parser's checks are not under test", name);
}

/* ---- cl_main.c, cl_input.c, cl_keyboard.c state ---- */

centity_t *cl_entities;
int cl_numentities;
entity_state_t cl_parse_entities[MAX_PARSE_ENTITIES];
char cl_weaponmodels[MAX_CLIENTWEAPONMODELS][MAX_QPATH];
int num_cl_weaponmodels;
qboolean paused_at_load;
console_t con;
viddef_t viddef;
int anykeydown;
int gun_frame;
struct model_s *gun_model;

cvar_t *cl_gun, *cl_kickangles, *cl_laseralpha, *cl_loadpaused, *cl_noskins;
cvar_t *cl_paused, *cl_predict, *cl_r1q2_lightstyle, *cl_showclamp;
cvar_t *cl_showfps, *cl_shownet, *cl_showspeed, *cl_timedemo, *cl_timeout;
cvar_t *cl_vwep, *crosshair, *crosshair_color_b, *crosshair_color_g;
cvar_t *crosshair_color_r, *crosshair_scale, *gl1_stereo, *gl1_stereo_separation;
cvar_t *horplus, *rcon_address, *rcon_client_password, *vid_renderer;

void
stub_client_cvars(void)
{
	static int done;
	cvar_t **all[] = { &cl_gun, &cl_kickangles, &cl_laseralpha, &cl_loadpaused,
		&cl_noskins, &cl_paused, &cl_predict, &cl_r1q2_lightstyle, &cl_showclamp,
		&cl_showfps, &cl_shownet, &cl_showspeed, &cl_timedemo, &cl_timeout,
		&cl_vwep, &crosshair, &crosshair_color_b, &crosshair_color_g,
		&crosshair_color_r, &crosshair_scale, &gl1_stereo, &gl1_stereo_separation,
		&horplus, &rcon_address, &rcon_client_password, &vid_renderer };
	size_t i;

	for (i = 0; i < sizeof(all) / sizeof(all[0]); i++)
	{
		char name[32];

		Com_sprintf(name, sizeof(name), "t_stub_cvar_%d", (int)i);
		*all[i] = Cvar_Get(name, "0", 0);
	}

	/* The cl_screen.c cvars, once: it adds console commands too. */
	if (!done)
	{
		done = 1;
		SCR_Init();
	}
}

/* One slot per entity number a message can name (MAX_CL_ENTNUM). */
centity_t *
CL_AllocEntity(int entnum)
{
	static centity_t ents[MAX_CL_ENTNUM + 1];

	cl_entities = ents;
	cl_numentities = MAX_CL_ENTNUM + 1;
	return (entnum >= 0 && entnum <= MAX_CL_ENTNUM) ? &ents[entnum] : NULL;
}

int CL_MaxClients(void) { return 1; }
int CL_PinsCapture(void) { return 0; }
qboolean CL_PinsRun(void) { return false; }
void CL_ClearState(void) { }
void CL_Stop_f(void) { }
void CL_WriteConfiguration(void) { }
void CL_WriteDemoMessage(void) { }
void CL_CheckPredictionError(void) { }
void CL_ParseStatusMessage(void) { stub_not_reachable("CL_ParseStatusMessage"); }
void CL_ParseInventory(void) { stub_not_reachable("CL_ParseInventory"); }
void CL_DrawInventory(void) { }
void In_FlushQueue(void) { }
void IN_Update(void) { }
void M_ForceMenuOff(void) { }
void M_Draw(void) { }
void Con_CheckResize(void) { }
void Con_ClearNotify(void) { }
void Con_DrawConsole(float frac) { }
void Con_DrawNotify(void) { }
void SCR_PlayCinematic(char *name) { }
void SCR_StopCinematic(void) { }
qboolean SCR_DrawCinematic(void) { return false; }
void Controller_Rumble(const char *n, vec3_t s, qboolean p, unsigned int d, unsigned short int v) { }
void Haptic_Feedback(const char *n, int a, int b, int c, int d, int e, int f, int g, int h, float i) { }

/* ---- particles, temp entities, effects ---- */

void CL_AddDLights(void) { }
void CL_AddLightStyles(void) { }
void CL_AddMuzzleFlash(void) { stub_not_reachable("CL_AddMuzzleFlash"); }
void CL_AddMuzzleFlash2(void) { stub_not_reachable("CL_AddMuzzleFlash2"); }
void CL_AddParticles(void) { }
void CL_AddTEnts(void) { }
void CL_BfgParticles(entity_t *ent) { }
void CL_BlasterTrail(vec3_t s, vec3_t e) { }
void CL_BlasterTrail2(vec3_t s, vec3_t e) { }
void CL_DiminishingTrail(vec3_t s, vec3_t e, centity_t *o, int f) { }
void CL_EntityEvent(entity_state_t *ent) { stub_not_reachable("CL_EntityEvent"); }
void CL_FlagTrail(vec3_t s, vec3_t e, int c) { }
void CL_FlyEffect(centity_t *ent, vec3_t o) { }
void CL_IonripperTrail(vec3_t s, vec3_t e) { }
void CL_ParseTEnt(void) { stub_not_reachable("CL_ParseTEnt"); }
struct model_s *CL_PowerScreenModel(void) { return NULL; }
void CL_RegisterTEntSounds(void) { }
void CL_RocketTrail(vec3_t s, vec3_t e, centity_t *o) { }
void CL_SetLightstyle(int i) { stub_not_reachable("CL_SetLightstyle"); }
void CL_TagTrail(vec3_t s, vec3_t e, int c) { }
void CL_TeleporterParticles(entity_state_t *ent) { }
void CL_TrackerTrail(vec3_t s, vec3_t e, int c) { }
void CL_Tracker_Shell(vec3_t o) { }
void CL_TrapParticles(entity_t *ent) { }

/* ---- renderer ---- */

void V_AddEntity(entity_t *ent) { }
void V_RenderView(float s) { }
void R_BeginFrame(float s) { }
void R_EndFrame(void) { }
qboolean R_EndWorldRenderpass(void) { return false; }
void R_RenderFrame(refdef_t *fd) { }
void R_SetPalette(const unsigned char *p) { }
void R_SetSky(const char *n, float r, vec3_t a) { }
struct model_s *R_RegisterModel(const char *n) { return NULL; }
struct image_s *R_RegisterSkin(const char *n) { return NULL; }
struct image_s *Draw_FindPic(const char *n) { return NULL; }
void Draw_Fill(int x, int y, int w, int h, int c) { }
void Draw_PicScaledCol(int x, int y, const char *p, float f, const float c[3]) { }
void Draw_TileClear(int x, int y, int w, int h, const char *n) { }
void DrawAltStringScaled(int x, int y, const char *s, float f) { }
void DrawStringScaled(int x, int y, const char *s, float f) { }

/* ---- sound: the frontend sound.c is linked, the mixer is not ---- */

void OGG_Init(void) { }
void OGG_Shutdown(void) { }
void OGG_Stop(void) { }
void OGG_PlayTrack(const char *t, qboolean c, qboolean i) { }
void OGG_LoadAsWav(char *f, wavinfo_t *i, void **b) { }
wavinfo_t GetWavinfo(char *n, byte *w, int l) { wavinfo_t i = {0}; return i; }
qboolean SDL_BackendInit(void) { return false; }
void SDL_BackendShutdown(void) { }
qboolean SDL_Cache(sfx_t *s, wavinfo_t *i, byte *d, short v, int a, int b, int c, int e) { return false; }
void SDL_ClearBuffer(void) { }
int SDL_DriftBeginofs(float f) { return 0; }
void SDL_RawSamples(int s, int r, int w, int c, const byte *d, float v) { }
void SDL_SoundInfo(void) { }
void SDL_Spatialize(channel_t *ch) { }
void SDL_Update(void) { }
