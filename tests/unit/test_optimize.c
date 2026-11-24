/*
 * The settings optimizer's command (TASK203, src/client/menu/optimize.c;
 * docs/plans/settings-optimizer.md, section 5) on the real cvar, command
 * and preset code, with the demo playback replaced: a run publishes each
 * candidate as the B side and writes no tuning cvar, saves the result as
 * a preset and makes it B; cancel and each failure put back the B side
 * and A/B state; the demo name is refused as a download path is.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "../../src/client/menu/header/vidrules.h"
#include "../../src/client/menu/header/optsearch.h"

static cvar_t *kept_vars;
static int plays;
static char played[64];
static int demo_exists = 1;
static int drop_in_play;       /* Play ends the old game first, as SV_InitGame's CL_Drop does */
static const char *fast_when = "r_rt_ao=0;";
static long long now_us = 1000000;
static char before[32][64];

static void
Play(const char *demo)
{
	plays++;
	snprintf(played, sizeof(played), "%s", demo);
	if (drop_in_play)
	{
		Optimize_DemoEnd();
	}
}

static long long
Clock(void)
{
	return now_us;
}

static int
Exists(const char *demo)
{
	(void)demo;
	return demo_exists;
}

/* a fresh table with every tuning row and the capabilities of a Mac with ray tracing and every scaler */
static void
Fresh(void)
{
	if (!kept_vars)
	{
		kept_vars = cvar_vars ? cvar_vars : (cvar_t *)-1;
	}
	cvar_vars = NULL;
	for (int i = 0; i < VidRules_PresetNum(); i++)
	{
		const char *def;
		const char *cv = VidRules_PresetCvar(i, &def);

		Cvar_Get(cv, def, CVAR_ARCHIVE);
	}
	Cvar_Get("r_rt_ab", "", 0);
	Cvar_Set("r_rt_ab", "");
	Cvar_Get("vid_renderer", "metal", 0);
	Cvar_Get("r_rt_available", "1", 0);
	Cvar_Get("r_metalfx_modes", "15", 0);
	Cvar_Get("r_hdr_headroom", "2", 0);
	Cvar_Get("timedemo", "0", 0);
	Cvar_Set("r_rt_ao", "1");
	Cvar_Set("r_metalfx_scale", "0.75");
	for (int i = 0; i < VidRules_PresetNum(); i++)
	{
		snprintf(before[i], sizeof(before[i]), "%s", Cvar_VariableString(VidRules_PresetCvar(i, NULL)));
	}
	RTPreset_SetDir(t_tmpdir());
	Optimize_SetHooks(Play, Exists);
	Optimize_SetClock(Clock);
	Optimize_Init();   /* the cvars at their defaults on the fresh table; the command once */
	plays = 0;
	demo_exists = 1;
	drop_in_play = 0;
	fast_when = "r_rt_ao=0;";
}

static void
Restore(void)
{
	const rtpreset_state_t a = { 0 };

	/* the tests after this file start with no run and on A */
	if (Optimize_Running())
	{
		Cmd_ExecuteString("optimize cancel");
	}
	RTPreset_SetState(&a);
	Optimize_SetHooks(NULL, NULL);
	Optimize_SetClock(NULL);
	/* the result a failure saved under the default name: the tests after this one start without it */
	{
		char path[MAX_OSPATH];

		snprintf(path, sizeof(path), "%s/presets/rt-optimized.cfg", t_tmpdir());
		remove(path);
	}
	RTPreset_SetDir(NULL);
	if (kept_vars)
	{
		cvar_vars = (kept_vars == (cvar_t *)-1) ? NULL : kept_vars;
		kept_vars = NULL;
	}
}

#define OPT_REQUIRE(COND) \
	do { if (!(COND)) { Restore(); T_REQUIRE(COND); } } while (0)

static void
CheckNoTuningCvarWritten(void)
{
	for (int i = 0; i < VidRules_PresetNum(); i++)
	{
		T_EQ_STR(Cvar_VariableString(VidRules_PresetCvar(i, NULL)), before[i]);
	}
}

/* n frames of a demo still playing, each client frame ticked as CL_Frame does; fast when B holds fast_when */
static void
Frames(int n)
{
	const int fast = strstr(Cvar_VariableString("r_rt_ab"), fast_when) != NULL;

	for (int i = 0; i < n; i++)
	{
		now_us += fast ? 10000 : 30000;
		Optimize_TickAt(now_us);
		Optimize_FrameAt(now_us);
		CheckNoTuningCvarWritten();
	}
}

/* plays one demo of n frames to its end */
static void
Demo(int n)
{
	Frames(n);
	Optimize_DemoEnd();
	Cbuf_Execute();
}

static int
FileExists(const char *name)
{
	char path[MAX_OSPATH];
	FILE *f;

	snprintf(path, sizeof(path), "%s/presets/rt-%s.cfg", t_tmpdir(), name);
	if (!(f = fopen(path, "r")))
	{
		return 0;
	}
	fclose(f);
	return 1;
}

TEST(optimize_run_writes_no_tuning_cvar)
{
	int guard = 0;

	Fresh();
	Cvar_Set("optimize_rounds", "6");
	Cvar_Set("optimize_warmup", "0");
	drop_in_play = 1;   /* a game on: the play ends it first */
	Cmd_ExecuteString("optimize run best");
	drop_in_play = 0;
	T_CONSOLE_HAS("ends the current game");
	OPT_REQUIRE(plays == 1);
	T_EQ_STR(played, "demo1.dm2");
	T_EQ_STR(Cvar_VariableString("optimize_state"), "2");   /* the probe */
	T_CHECK(strncmp(Cvar_VariableString("r_rt_ab"), "B: preset optimize|", 19) == 0);
	T_EQ_STR(Cvar_VariableString("timedemo"), "1");
	T_CHECK(FileExists("before-optimize"));
	CheckNoTuningCvarWritten();

	/* the disconnect inside the play was the game the benchmark ended */
	T_EQ_INT(Optimize_Running(), 1);

	while (Optimize_Running() && guard++ < 20)
	{
		Demo(100);
	}
	T_EQ_INT(Optimize_Running(), 0);
	T_CHECK(guard <= 8);   /* the probe and at most six trials */
	T_EQ_STR(Cvar_VariableString("optimize_state"), "0");
	T_EQ_STR(Cvar_VariableString("timedemo"), "0");
	T_CHECK(FileExists("best"));
	T_CHECK(strncmp(Cvar_VariableString("r_rt_ab"), "B: preset best|", 15) == 0);
	T_CHECK(strstr(Cvar_VariableString("r_rt_ab"), "r_rt_ao=0;") != NULL);
	T_CONSOLE_HAS("result:");
	CheckNoTuningCvarWritten();

	/* report prints it again; undo puts back the B side from before the run */
	Cmd_ExecuteString("optimize report");
	T_CONSOLE_HAS("result:");
	Cmd_ExecuteString("optimize undo");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	CheckNoTuningCvarWritten();
	Restore();
}

TEST(optimize_cancel_restores_b_side)
{
	Fresh();
	RTPreset_AB();   /* B: stock, before the run */
	OPT_REQUIRE(strncmp(Cvar_VariableString("r_rt_ab"), "B: stock|", 9) == 0);
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(Optimize_Running(), 1);
	Demo(100);   /* the probe */
	T_EQ_STR(Cvar_VariableString("optimize_state"), "1");
	Cmd_ExecuteString("optimize cancel");
	T_EQ_INT(Optimize_Running(), 0);
	T_EQ_STR(Cvar_VariableString("optimize_state"), "0");
	T_CHECK(strncmp(Cvar_VariableString("r_rt_ab"), "B: stock|", 9) == 0);
	T_EQ_STR(RTPreset_Side(), "B: stock");
	T_EQ_STR(Cvar_VariableString("timedemo"), "0");
	CheckNoTuningCvarWritten();
	RTPreset_AB();
	Restore();
}

TEST(optimize_failed_trial_restores_b_side)
{
	Fresh();
	Cmd_ExecuteString("optimize run");
	Demo(100);    /* the probe */
	Demo(10);     /* a disconnect before the demo's end */
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("before the demo's end");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	T_EQ_STR(Cvar_VariableString("optimize_state"), "0");
	CheckNoTuningCvarWritten();

	/* the renderer changed */
	Cmd_ExecuteString("optimize run");
	Demo(100);
	Cvar_Set("vid_renderer", "soft");
	Demo(100);
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("the renderer changed");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	CheckNoTuningCvarWritten();

	/* the time cap */
	Cvar_Set("vid_renderer", "metal");
	Cvar_Set("optimize_seconds", "10");
	Cmd_ExecuteString("optimize run");
	Demo(400);    /* 12 s of frames: past the cap */
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("the time cap");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	CheckNoTuningCvarWritten();
	Restore();
}

/* a demo that brings no frame (its map not installed, or not a demo): the run fails, it does not wait */
TEST(optimize_demo_without_frames_fails)
{
	Fresh();
	Cmd_ExecuteString("optimize run");
	OPT_REQUIRE(Optimize_Running());
	Optimize_DemoEnd();   /* the demo failed before its first frame */
	Optimize_DemoEnd();
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("no frame drawn");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	T_EQ_STR(Cvar_VariableString("timedemo"), "0");
	CheckNoTuningCvarWritten();

	/* a trial's demo that draws none fails the same way, with the best so far reported */
	t_console_reset();
	Cmd_ExecuteString("optimize run");
	Demo(100);   /* the probe */
	Demo(100);   /* trial 1 */
	OPT_REQUIRE(Optimize_Running());
	Optimize_DemoEnd();
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("no frame drawn");
	T_CONSOLE_HAS("closest:");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	Restore();
}

/* a failure after a trial reports the best so far: its line, its limit and what it gave up */
TEST(optimize_failure_reports_best_so_far)
{
	Fresh();
	Cvar_Set("optimize_warmup", "0");
	Cmd_ExecuteString("optimize run");
	Demo(100);   /* the probe */
	Demo(100);   /* trial 1, slow: misses */
	Demo(100);   /* trial 2 */
	Demo(100);   /* trial 3: none has met the target */
	OPT_REQUIRE(Optimize_Running());
	Demo(10);    /* a disconnect before the demo's end */
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("before the demo's end");
	T_CONSOLE_HAS("closest: 30.0 ms against 16.7 ms (trial ");
	T_CONSOLE_HAS("limit: the run stopped before the search ended: a disconnect");
	T_CONSOLE_HAS("stopped before the search ended: a disconnect came before the demo's end");
	t_console_reset();
	Cmd_ExecuteString("optimize report");
	T_CONSOLE_HAS("closest:");
	T_CONSOLE_HAS("stopped before the search ended");
	CheckNoTuningCvarWritten();
	Restore();
}

/* the time cap counts from the command and is checked on every client frame, with no frame drawn */
TEST(optimize_time_cap_without_frames)
{
	Fresh();
	Cvar_Set("optimize_seconds", "10");
	Cmd_ExecuteString("optimize run");
	OPT_REQUIRE(Optimize_Running());
	Optimize_TickAt(now_us + 9000000);
	T_EQ_INT(Optimize_Running(), 1);
	Optimize_TickAt(now_us + 11000000);
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("the time cap");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	T_EQ_STR(Cvar_VariableString("timedemo"), "0");
	T_EQ_STR(Cvar_VariableString("optimize_state"), "0");
	CheckNoTuningCvarWritten();
	Restore();
}

/* timedemo reset while a run is on (CL_FixCvarCheats in multiplayer, or the demo's text): the run fails */
TEST(optimize_timedemo_reset_fails)
{
	Fresh();
	Cmd_ExecuteString("optimize run");
	Cvar_Set("timedemo", "0");   /* before the probe's first frame */
	Optimize_TickAt(now_us);
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("timedemo is not 1");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");

	t_console_reset();
	Cmd_ExecuteString("optimize run");
	Demo(100);   /* the probe */
	Demo(100);   /* trial 1 */
	Frames(40);  /* trial 2, playing */
	OPT_REQUIRE(Optimize_Running());
	Cvar_Set("timedemo", "0");
	Frames(1);
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("timedemo is not 1");
	T_CONSOLE_HAS("stopped before the search ended: timedemo is not 1");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	T_EQ_STR(Cvar_VariableString("timedemo"), "0");
	CheckNoTuningCvarWritten();
	Restore();
}

/* a vid_restart mid-run: the renderer's presented count goes back, and the trial fails */
TEST(optimize_vid_restart_fails)
{
	Fresh();
	Cvar_Get("metal_presented", "0", 0);
	Cvar_Set("metal_presented", "5000");   /* a stale count from a renderer before this run */
	Cmd_ExecuteString("optimize run");
	Cvar_Set("metal_presented", "300");    /* the renderer's real count: not a restart */
	Demo(100);
	OPT_REQUIRE(Optimize_Running());
	Cvar_Set("metal_presented", "420");
	Frames(20);  /* trial 1, playing */
	T_EQ_INT(Optimize_Running(), 1);
	Cvar_Set("metal_presented", "2");      /* the renderer started again */
	Frames(1);
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("the renderer changed");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	CheckNoTuningCvarWritten();
	Restore();
}

/* the demo removed between trials: the next play is refused and the run fails */
TEST(optimize_demo_removed_between_trials)
{
	Fresh();
	Cmd_ExecuteString("optimize run");
	OPT_REQUIRE(plays == 1);
	demo_exists = 0;
	Demo(100);   /* the probe ends and queues the first trial */
	T_EQ_INT(plays, 1);
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("demos/demo1.dm2 is missing");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	Restore();
}

/* a timedemo draws about the probe's count: a few frames fewer is not a disconnect (N4) */
TEST(optimize_frame_count_tolerance)
{
	Fresh();
	Cmd_ExecuteString("optimize run");
	Demo(100);   /* the probe */
	Demo(95);
	T_EQ_INT(Optimize_Running(), 1);
	Demo(85);
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("before the demo's end");
	Restore();
}

/* the probe's cost hints as ref_metal writes them (rm_rt_cost_format): the switches go dearest first */
TEST(optimize_reads_cost_hints)
{
	Fresh();
	Cvar_Set("optimize_rank", "rt");
	Cvar_Set("optimize_warmup", "0");
	fast_when = "r_rt_sun=0;";
	Cmd_ExecuteString("optimize run");
	Cvar_Get("metal_rt_cost", "", 0);
	Cvar_Set("metal_rt_cost", "shadows=0.100 sun=5.000 ao=0.200 water=0.300");
	for (int guard = 0; Optimize_Running() && guard < 20; guard++)
	{
		Demo(100);
	}
	T_EQ_INT(Optimize_Running(), 0);
	T_CONSOLE_HAS("result:");
	/* sun, the dearest by the hints, is given up first; without them ao goes first */
	T_CHECK(strstr(Cvar_VariableString("r_rt_ab"), "r_rt_sun=0;") != NULL);
	T_CHECK(strstr(Cvar_VariableString("r_rt_ab"), "r_rt_ao=1;") != NULL);
	CheckNoTuningCvarWritten();
	Restore();
}

TEST(optimize_refuses)
{
	static const char *const bad[] = {
		"../demo1.dm2", "/abs.dm2", ".hidden.dm2", "c:demo.dm2", "a\\b.dm2", "demo1.dm2;quit",
		"demo1.dm2\nquit", "demo 1.dm2", "demo1.dm2\"", "demo1.so", "", "demo1"
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		if (!Optimize_DemoError(bad[i]))
		{
			t_fail(__FILE__, __LINE__, "the demo \"%s\" is accepted", bad[i]);
		}
	}
	T_CHECK(Optimize_DemoError("demo1.dm2") == NULL);
	T_CHECK(Optimize_DemoError("sub/demo2.dm2") == NULL);

	Fresh();
	Cvar_Set("optimize_demo", "../x.dm2");
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	T_CONSOLE_HAS("refused");
	Cvar_Set("optimize_demo", "demo1.dm2");

	demo_exists = 0;
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	T_CONSOLE_HAS("missing");
	demo_exists = 1;

	Cvar_Set("optimize_rank", "rt rt");
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	Cvar_Set("optimize_rank", "rt");
	Cvar_Set("optimize_target", "fast");
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	Cvar_Set("optimize_target", "60");
	Cmd_ExecuteString("optimize run ../escape");
	T_EQ_INT(plays, 0);

	/* the undo copy's name is not a result's (N3) */
	t_console_reset();
	Cmd_ExecuteString("optimize run before-optimize");
	T_EQ_INT(plays, 0);
	T_CONSOLE_HAS("optimize undo puts back");
	Cmd_ExecuteString("optimize run Before-Optimize");
	T_EQ_INT(plays, 0);

	/* not a number: refused before the clamp (N4) */
	Cvar_Set("optimize_rounds", "nan");
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	Cvar_Set("optimize_rounds", "10");
	Cvar_Set("optimize_seconds", "inf");
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	Cvar_Set("optimize_seconds", "300");
	Cvar_Set("optimize_warmup", "nan");
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	Cvar_Set("optimize_warmup", "2");

	Cvar_Set("vid_renderer", "soft");
	Cmd_ExecuteString("optimize run");
	T_EQ_INT(plays, 0);
	T_CONSOLE_HAS("the optimizer turns the Metal renderer's settings");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");
	T_EQ_INT(Optimize_Running(), 0);

	/* optimize_state is the client's: the console may not set it */
	Cmd_ExecuteString("set optimize_state 1");
	T_EQ_STR(Cvar_VariableString("optimize_state"), "0");
	CheckNoTuningCvarWritten();
	Restore();
}
