/*
 * Copyright (C) 2026 Yamagi Quake II contributors
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
 * =======================================================================
 *
 * The settings optimizer's command (TASK203; docs/plans/settings-optimizer.md,
 * section 5): optimize run|cancel|report|undo and the optimize_* cvars. A
 * run plays the benchmark (timedemo 1 on optimize_demo) once as the
 * probe and once per trial, each with its candidate published as the B
 * side (rtpreset.c), and hands each trial's frame times to the search
 * (optsearch.c). At the end it saves the result as a preset and makes it
 * the B side. It never writes a tuning cvar: the player's settings (A)
 * stay as they are, and accepting is the player's own rt_preset load.
 *
 * optimize_demo and optimize_rank are untrusted (a server may set client
 * cvars): the demo name is checked as a download path is and goes to the
 * playback as an argument, never into a console line.
 *
 * =======================================================================
 */

#include <math.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "../../common/header/common.h"
#include "header/vidrules.h"
#include "header/optsearch.h"

/* src/server/sv_init.c: what demomap calls */
void SV_Map(qboolean attractloop, char *levelstring, qboolean loadgame, qboolean isautosave);

#if defined(__APPLE__) && defined(TARGET_OS_TV) && TARGET_OS_TV
#define OPT_HDRPLAT VIDHDR_TVOS
#elif defined(__APPLE__) && defined(TARGET_OS_VISION) && TARGET_OS_VISION
#define OPT_HDRPLAT VIDHDR_VISIONOS
#elif defined(__APPLE__) && TARGET_OS_IPHONE
#define OPT_HDRPLAT VIDHDR_IOS
#else
#define OPT_HDRPLAT VIDHDR_MAC
#endif

#define OPT_MAXDEMO 64
#define OPT_FRAMES 8192          /* demo1.dm2 draws 689; frames past this are not counted */
#define OPT_LABEL "optimize"     /* the B side's name during a trial */
#define OPT_BEFORE "before-optimize"

enum
{
	OPT_STATE_NONE,
	OPT_STATE_TRIAL,
	OPT_STATE_PROBE
};

static cvar_t *optimize_rank, *optimize_target, *optimize_stat, *optimize_rounds, *optimize_seconds,
	*optimize_warmup, *optimize_demo;

static void (*opt_play)(const char *demo);
static int (*opt_exists)(const char *demo);

static optsearch_t opt_s;
static optinput_t opt_in;
static optcand_t opt_cand;
static int opt_state;            /* OPT_STATE_*: OPT_STATE_NONE is no run */
static int opt_pending;          /* a play is queued for "optimize _play" */
static int opt_have_report;
static char opt_demo[OPT_MAXDEMO];
static char opt_name[VIDPRESET_MAXNAME + 1];
static char opt_timedemo[16];
static float opt_warmup;
static long long opt_run_start;
static long long opt_frames[OPT_FRAMES];
static int opt_nframes;
static int opt_probe_frames;
static float opt_pres_warm;      /* metal_presented after the warm-up, and when */
static long long opt_t_warm;
static rtpreset_state_t opt_saved;   /* the B side and A/B state before the run */
static int opt_have_saved;

static void
Opt_SetState(int state)
{
	opt_state = state;
	Cvar_ForceSet("optimize_state", va("%d", state));
}

const char *
Optimize_DemoError(const char *demo)
{
	const size_t len = demo ? strlen(demo) : 0;

	if (!len)
	{
		return "no demo is named";
	}
	if (len >= OPT_MAXDEMO)
	{
		return "the demo's name is too long";
	}
	if (strstr(demo, "..") || demo[0] == '.' || demo[0] == '/' || strchr(demo, ':') || strchr(demo, '\\'))
	{
		return "the demo's name may not hold \"..\", ':' or '\\', or start with '.' or '/'";
	}
	for (const char *p = demo; *p; p++)
	{
		const unsigned char c = (unsigned char)*p;

		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
			c == '_' || c == '-' || c == '.' || c == '/'))
		{
			return "the demo's name holds a character other than letters, digits, '_', '-', '.' and '/'";
		}
	}
	if (len < 5 || strcmp(demo + len - 4, ".dm2"))
	{
		return "the demo is not a .dm2";
	}
	return NULL;
}

static void
Opt_EnginePlay(const char *demo)
{
	char name[OPT_MAXDEMO];

	Q_strlcpy(name, demo, sizeof(name));
	SV_Map(true, name, false, false);
}

static int
Opt_EngineExists(const char *demo)
{
	char path[OPT_MAXDEMO + 8];

	snprintf(path, sizeof(path), "demos/%s", demo);
	return FS_LoadFile(path, NULL) > 0;
}

void
Optimize_SetHooks(void (*play)(const char *demo), int (*exists)(const char *demo))
{
	opt_play = play;
	opt_exists = exists;
}

int
Optimize_Running(void)
{
	return opt_state != OPT_STATE_NONE;
}

/* the candidate's cvars as a set for rtpreset.c */
static int
Opt_Set(const optcand_t *c, const char *names[OPT_NCV], char vals[OPT_NCV][16])
{
	int n = 0;

	for (int cv = 0; cv < OPT_NCV; cv++)
	{
		if (c->set[cv])
		{
			names[n] = OptSearch_CvarName(cv);
			snprintf(vals[n], sizeof(vals[n]), "%g", (double)c->val[cv]);
			n++;
		}
	}
	return n;
}

static int
Opt_Publish(const char *label, const optcand_t *c)
{
	const char *names[OPT_NCV];
	const char *vp[OPT_NCV];
	char vals[OPT_NCV][16];
	const int n = Opt_Set(c, names, vals);

	for (int i = 0; i < n; i++)
	{
		vp[i] = vals[i];
	}
	return RTPreset_PublishSet(label, names, vp, n);
}

static int
Opt_Save(const char *name, const optcand_t *c)
{
	const char *names[OPT_NCV];
	const char *vp[OPT_NCV];
	char vals[OPT_NCV][16];
	const int n = Opt_Set(c, names, vals);

	for (int i = 0; i < n; i++)
	{
		vp[i] = vals[i];
	}
	return RTPreset_SaveSet(name, names, vp, n);
}

static void
Opt_PrintReport(void)
{
	for (int i = 0; i < OptSearch_NumLines(&opt_s); i++)
	{
		Com_Printf("%s\n", OptSearch_Line(&opt_s, i));
	}
}

/* the run is over: timedemo back, the state 0 */
static void
Opt_Stop(void)
{
	Cvar_Set("timedemo", opt_timedemo);
	opt_pending = 0;
	Opt_SetState(OPT_STATE_NONE);
}

/* the result saved as preset opt_name and made the B side; 0 when there is none */
static int
Opt_SaveResult(void)
{
	const optcand_t *r;
	int met = 0;

	if (!(r = OptSearch_Result(&opt_s, &met)))
	{
		return 0;
	}
	if (!Opt_Save(opt_name, r))
	{
		return 0;
	}
	Com_Printf("optimize: the %s is saved as preset %s\n", met ? "result" : "closest result", opt_name);
	return 1;
}

/* cancel or a failure: the B side and A/B state put back, never a candidate left on B */
static void
Opt_Fail(const char *why)
{
	Com_Printf("optimize: stopped: %s\n", why);
	RTPreset_SetState(&opt_saved);
	if (opt_s.ntrials > 0)
	{
		Opt_PrintReport();
		opt_have_report = 1;
		if (strcmp(why, "cancelled"))
		{
			Opt_SaveResult();
		}
	}
	Com_Printf("optimize: the B side is as it was: %s\n", RTPreset_Side());
	Opt_Stop();
}

static void
Opt_Finish(void)
{
	const optcand_t *r;
	int met = 0;

	Opt_PrintReport();
	opt_have_report = 1;
	r = OptSearch_Result(&opt_s, &met);
	if (r && Opt_SaveResult() && Opt_Publish(opt_name, r))
	{
		Com_Printf("optimize: B is preset %s; rt_ab flips; rt_preset load %s keeps it\n", opt_name, opt_name);
	}
	else
	{
		RTPreset_SetState(&opt_saved);
	}
	Opt_Stop();
}

static void Opt_PlayQueued(void);

/* plays the benchmark with candidate c on the B side: now from the console, else on the next command buffer run */
static void
Opt_Begin(int state, const optcand_t *c, int now)
{
	opt_cand = *c;
	if (!Opt_Publish(OPT_LABEL, c))
	{
		Opt_Fail("the candidate could not be published");
		return;
	}
	opt_nframes = 0;
	opt_pres_warm = 0;
	opt_t_warm = 0;
	Opt_SetState(state);
	opt_pending = 1;
	if (now)
	{
		Opt_PlayQueued();
		return;
	}
	/* a constant line: the demo's name is never in it */
	Cbuf_AddText("optimize _play\n");
}

static void
Opt_PlayQueued(void)
{
	if (!opt_pending || !Optimize_Running())
	{
		return;
	}
	opt_pending = 0;
	(opt_play ? opt_play : Opt_EnginePlay)(opt_demo);
}

void
Optimize_FrameAt(long long us)
{
	if (!Optimize_Running())
	{
		return;
	}
	if (!opt_run_start)
	{
		opt_run_start = us;
	}
	if (us - opt_run_start > (long long)(opt_in.seconds * 1000000.0f))
	{
		Opt_Fail("the time cap ended it");
		return;
	}
	if (opt_nframes < OPT_FRAMES)
	{
		opt_frames[opt_nframes++] = us;
	}
	if (!opt_t_warm && us - opt_frames[0] > (long long)(opt_warmup * 1000000.0f))
	{
		opt_t_warm = us;
		opt_pres_warm = Cvar_VariableValue("metal_presented");
	}
}

void
Optimize_Frame(void)
{
	if (Optimize_Running())
	{
		Optimize_FrameAt(Sys_Microseconds());
	}
}

/* the probe's cost hints, metal_rt_cost "shadows=<ms> sun=<ms> ao=<ms> water=<ms>" */
static void
Opt_ReadHints(void)
{
	float h[4];

	opt_in.have_hints = 0;
	if (sscanf(Cvar_VariableString("metal_rt_cost"), "shadows=%f sun=%f ao=%f water=%f", &h[0], &h[1], &h[2],
		&h[3]) == 4)
	{
		for (int i = 0; i < 4; i++)
		{
			if (!isfinite(h[i]) || h[i] < 0 || h[i] > 1000)
			{
				return;
			}
			opt_in.hint_ms[i] = h[i];
		}
		opt_in.have_hints = 1;
	}
}

void
Optimize_DemoEnd(void)
{
	optmeasure_t m;
	optcand_t next;
	const long long t_end = opt_nframes ? opt_frames[opt_nframes - 1] : 0;

	if (!Optimize_Running() || opt_pending)
	{
		return;
	}
	if (!opt_nframes)
	{
		return;   /* the game the benchmark ended, before the demo's first frame */
	}
	if (VidRules_Renderer(Cvar_VariableString("vid_renderer")) != VIDREF_METAL)
	{
		Opt_Fail("the renderer changed");
		return;
	}

	if (opt_state == OPT_STATE_PROBE)
	{
		opt_probe_frames = opt_nframes;
		Opt_ReadHints();
		opt_in.probe_seconds = (float)(t_end - opt_frames[0]) / 1000000.0f;
		OptSearch_Init(&opt_s, &opt_in);
		if (!OptSearch_Next(&opt_s, &next))
		{
			Opt_Finish();
			return;
		}
		Opt_Begin(OPT_STATE_TRIAL, &next, 0);
		return;
	}

	if (opt_nframes < opt_probe_frames)
	{
		Opt_Fail("a disconnect came before the demo's end");
		return;
	}
	if (!OptSearch_Stats(opt_frames, opt_nframes, opt_warmup, opt_in.stat, &m))
	{
		Opt_Fail("no frame was left after the warm-up");
		return;
	}
	m.presented_fps = 0;
	if (opt_t_warm && t_end > opt_t_warm)
	{
		const float pres = Cvar_VariableValue("metal_presented") - opt_pres_warm;

		if (pres > 0)
		{
			m.presented_fps = pres / ((float)(t_end - opt_t_warm) / 1000000.0f);
		}
	}
	OptSearch_Record(&opt_s, &m);
	if (!OptSearch_Next(&opt_s, &next))
	{
		Opt_Finish();
		return;
	}
	Opt_Begin(OPT_STATE_TRIAL, &next, 0);
}

/* the player's inputs; 0, with the reason, when one does not parse */
static int
Opt_Inputs(void)
{
	char err[160];
	const char *headroom = Cvar_VariableString("r_hdr_headroom");
	vidrule_t hdr;

	memset(&opt_in, 0, sizeof(opt_in));
	if (!OptSearch_ParseRank(optimize_rank->string, opt_in.rank, &opt_in.nrank, err, sizeof(err)))
	{
		Com_Printf("optimize run: refused: optimize_rank: %s\n", err);
		return 0;
	}
	if (!OptSearch_ParseTarget(optimize_target->string, &opt_in.target_ms))
	{
		Com_Printf("optimize run: refused: optimize_target is a frame rate (60) or a frame time (12ms)\n");
		return 0;
	}
	if (!OptSearch_ParseStat(optimize_stat->string, &opt_in.stat))
	{
		Com_Printf("optimize run: refused: optimize_stat is mean or p50 to p99\n");
		return 0;
	}
	opt_in.rt_available = Cvar_VariableValue("r_rt_available") != 0;
	opt_in.metalfx_modes = (int)Cvar_VariableValue("r_metalfx_modes");
	opt_in.presentation = (int)Cvar_VariableValue("r_presentation");
	opt_in.player_metalfx = Cvar_VariableValue("r_metalfx");
	opt_in.player_scale = Cvar_VariableValue("r_metalfx_scale");
	hdr = VidRules_Hdr(VIDHDR_ITEM_ON, "metal", OPT_HDRPLAT, headroom[0] ? (float)atof(headroom) : -1.0f,
		(float)opt_in.presentation, Cvar_VariableValue("r_hdr"));
	opt_in.hdr_why = hdr.enabled ? NULL : hdr.text;
	opt_in.rounds = (int)Q_clamp(optimize_rounds->value, 1, OPT_MAXROUNDS);
	opt_in.seconds = (float)Q_clamp(optimize_seconds->value, 10, 3600);
	opt_warmup = (float)Q_clamp(optimize_warmup->value, 0, 30);
	return 1;
}

static void
Opt_Run(const char *name)
{
	const char *why;
	optcand_t first;

	if (Optimize_Running())
	{
		Com_Printf("optimize run: refused: a run is on (optimize cancel stops it)\n");
		return;
	}
	if (VidRules_Renderer(Cvar_VariableString("vid_renderer")) != VIDREF_METAL)
	{
		Com_Printf("optimize run: refused: the optimizer turns the Metal renderer's settings\n");
		return;
	}
	if ((why = VidRules_PresetNameError(name)))
	{
		Com_Printf("optimize run: refused: %s\n", why);
		return;
	}
	if ((why = Optimize_DemoError(optimize_demo->string)))
	{
		Com_Printf("optimize run: refused: optimize_demo: %s\n", why);
		return;
	}
	if (!(opt_exists ? opt_exists : Opt_EngineExists)(optimize_demo->string))
	{
		Com_Printf("optimize run: refused: the demo demos/%s is missing\n", optimize_demo->string);
		return;
	}
	if (!Opt_Inputs())
	{
		return;
	}

	Q_strlcpy(opt_demo, optimize_demo->string, sizeof(opt_demo));
	Q_strlcpy(opt_name, name, sizeof(opt_name));
	Com_Printf("optimize: the benchmark plays demos/%s and ends the current game\n", opt_demo);

	RTPreset_GetState(&opt_saved);
	opt_have_saved = 1;
	RTPreset_Save(OPT_BEFORE);
	Q_strlcpy(opt_timedemo, Cvar_VariableString("timedemo"), sizeof(opt_timedemo));
	Cvar_Set("timedemo", "1");
	opt_have_report = 0;
	opt_probe_frames = 0;
	opt_run_start = 0;   /* the first frame drawn starts the clock */

	/* the probe: the first candidate, untimed */
	OptSearch_Init(&opt_s, &opt_in);
	if (!OptSearch_Next(&opt_s, &first))
	{
		Opt_Fail("the search gave no candidate");
		return;
	}
	Opt_Begin(OPT_STATE_PROBE, &first, 1);
}

static void
Optimize_f(void)
{
	const char *verb = Cmd_Argv(1);

	if (!strcmp(verb, "run") && Cmd_Argc() <= 3)
	{
		Opt_Run((Cmd_Argc() == 3) ? Cmd_Argv(2) : "optimized");
	}
	else if (!strcmp(verb, "cancel") && Cmd_Argc() == 2)
	{
		if (Optimize_Running())
		{
			Opt_Fail("cancelled");
		}
		else
		{
			Com_Printf("optimize cancel: no run is on\n");
		}
	}
	else if (!strcmp(verb, "report") && Cmd_Argc() == 2)
	{
		if (opt_have_report)
		{
			Opt_PrintReport();
		}
		else
		{
			Com_Printf("optimize report: no run has ended\n");
		}
	}
	else if (!strcmp(verb, "undo") && Cmd_Argc() == 2)
	{
		if (Optimize_Running() || !opt_have_saved)
		{
			Com_Printf("optimize undo: refused: %s\n", Optimize_Running() ? "a run is on" : "no run to undo");
			return;
		}
		RTPreset_SetState(&opt_saved);
		Com_Printf("optimize undo: %s; rt_preset load " OPT_BEFORE " puts back an accept\n", RTPreset_Side());
	}
	else if (!strcmp(verb, "_play") && Cmd_Argc() == 2)
	{
		Opt_PlayQueued();
	}
	else
	{
		Com_Printf("usage: optimize run [name] | cancel | report | undo\n");
	}
}

void
Optimize_Init(void)
{
	static qboolean added;

	optimize_rank = Cvar_Get("optimize_rank", "rt resolution hdr upscale denoise presentation", CVAR_ARCHIVE);
	optimize_target = Cvar_Get("optimize_target", "60", CVAR_ARCHIVE);
	optimize_stat = Cvar_Get("optimize_stat", "p95", CVAR_ARCHIVE);
	optimize_rounds = Cvar_Get("optimize_rounds", "10", CVAR_ARCHIVE);
	optimize_seconds = Cvar_Get("optimize_seconds", "300", CVAR_ARCHIVE);
	optimize_warmup = Cvar_Get("optimize_warmup", "2", CVAR_ARCHIVE);
	optimize_demo = Cvar_Get("optimize_demo", "demo1.dm2", CVAR_ARCHIVE);
	/* the client's: the console may not set it */
	Cvar_Get("optimize_state", "0", CVAR_NOSET);
	Opt_SetState(OPT_STATE_NONE);
	opt_pending = 0;
	opt_have_saved = 0;

	if (!added)
	{
		Cmd_AddCommand("optimize", Optimize_f);
		added = true;
	}
}
