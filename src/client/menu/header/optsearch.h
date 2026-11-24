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
 * The settings optimizer's search (TASK203; docs/plans/settings-optimizer.md,
 * section 4). Pure: it reads no cvar, file, timer or renderer state. The
 * caller gives the ranked priorities, the capabilities, the target, the
 * caps and the cost hints (optinput_t), then asks for the next candidate
 * (OptSearch_Next), times it and hands back its measurement
 * (OptSearch_Record), until Next says the search has ended. The result
 * and the report lines are then OptSearch_Result and OptSearch_Line. The
 * same inputs and measurements give the same candidates and result.
 *
 * =======================================================================
 */

#ifndef CL_MENU_OPTSEARCH_H
#define CL_MENU_OPTSEARCH_H

#include <stddef.h>

/* the priorities, in the order of the plan's table; optimize_rank names them */
typedef enum
{
	OPT_RESOLUTION,
	OPT_RT,
	OPT_HDR,
	OPT_UPSCALE,
	OPT_DENOISE,
	OPT_PRESENTATION,
	OPT_NPRI
} optpri_t;

/* the cvars a candidate may set, in this order */
typedef enum
{
	OPT_CV_SHADOWS,
	OPT_CV_SUN,
	OPT_CV_AO,
	OPT_CV_WATER,
	OPT_CV_GUNSHADOW,
	OPT_CV_AO_RAYS,
	OPT_CV_AO_SCALE,
	OPT_CV_WATER_HITS,
	OPT_CV_HDR,
	OPT_CV_METALFX,
	OPT_CV_SCALE,
	OPT_NCV
} optcv_t;

/* the presentation in force (r_presentation), as the search reads its overlay */
#define OPT_PRES_STOCK 0
#define OPT_PRES_ENHANCED 1
#define OPT_PRES_RETRO 2
#define OPT_PRES_RTPRIMARY 3

#define OPT_MAXROUNDS 64      /* optimize_rounds is held to 1 .. this */
#define OPT_MAXSTEPS 12       /* positions on one ladder */
#define OPT_MAXLINES 128
#define OPT_LINELEN 320
#define OPT_STAT_MEAN 0       /* optimize_stat mean; 50 .. 99 is that percentile */

typedef struct
{
	int rank[OPT_NPRI];       /* the ranked priorities, highest first */
	int nrank;
	int rt_available;         /* r_rt_available */
	int metalfx_modes;        /* r_metalfx_modes: bit 2 spatial, 4 temporal, 8 denoised */
	const char *hdr_why;      /* NULL: "HDR" is enabled; else VidRules_Hdr's reason */
	int presentation;         /* OPT_PRES_* */
	float player_metalfx;     /* the player's r_metalfx and r_metalfx_scale (A), read only */
	float player_scale;
	float target_ms;          /* the statistic a trial must not exceed */
	int stat;                 /* OPT_STAT_MEAN or a percentile */
	int rounds;               /* the most timed trials */
	float seconds;            /* the most seconds, the probe included */
	float probe_seconds;      /* the probe's duration */
	int have_hints;           /* the probe gave metal_rt_cost (ref_metal, rm_main.c) */
	float hint_ms[4];         /* shadows, sun, ao, water: mean GPU ms per frame */
} optinput_t;

/* one candidate: each ranked priority's position, and the cvars it sets after the coupling */
typedef struct
{
	int pos[OPT_NPRI];
	int set[OPT_NCV];         /* 1 where the candidate sets the cvar; elsewhere the player's value stays */
	float val[OPT_NCV];
} optcand_t;

/* one timed trial */
typedef struct
{
	float stat_ms;            /* the statistic optimize_stat names */
	float mean_ms;
	float p95_ms;
	float presented_fps;      /* <= 0: no reading */
	float seconds;            /* the trial's duration */
} optmeasure_t;

typedef struct
{
	optinput_t in;
	int nsteps[OPT_NPRI];     /* 0: not tried */
	float steps[OPT_NPRI][OPT_MAXSTEPS][OPT_NCV];   /* the values each position sets */
	int stepset[OPT_NPRI][OPT_NCV];
	char stepname[OPT_NPRI][OPT_MAXSTEPS][48];
	char why[OPT_NPRI][96];   /* why a priority is not tried, or has no step */
	optcand_t trial[OPT_MAXROUNDS];
	optmeasure_t meas[OPT_MAXROUNDS];
	int ntrials;
	/* the last replay */
	int done;
	int best;                 /* index into trial, -1 for none */
	int met;                  /* whether best meets the target */
	const char *stopped;      /* during OptSearch_Stop's replay: why the run stopped */
	char lines[OPT_MAXLINES][OPT_LINELEN];
	int nlines;
} optsearch_t;

/* the word of a priority, and the priority of a word (-1 for none) */
const char *OptSearch_Word(int pri);
int OptSearch_Priority(const char *word);
const char *OptSearch_CvarName(int cv);

/* optimize_rank: words, highest first; 0 with the reason in err for a word not known or given twice */
int OptSearch_ParseRank(const char *text, int rank[OPT_NPRI], int *n, char *err, size_t errlen);
/* optimize_target: "60" (a rate, at least 1) or "12ms" (above 0); 0 when it does not parse or is out of range */
int OptSearch_ParseTarget(const char *text, float *ms);
/* optimize_stat: "mean" or "p50" .. "p99"; 0 when it does not parse */
int OptSearch_ParseStat(const char *text, int *stat);

/*
 * A trial's statistics from its frames' times (microseconds, increasing),
 * the frames ending in the first warmup seconds after the first left out.
 * 0 when no frame time is left.
 */
int OptSearch_Stats(const long long *t_us, int n, float warmup, int stat, optmeasure_t *out);

void OptSearch_Init(optsearch_t *s, const optinput_t *in);
/* 1: *c is the next candidate to time; 0: the search has ended */
int OptSearch_Next(optsearch_t *s, optcand_t *c);
/* the measurement of the candidate Next gave last */
void OptSearch_Record(optsearch_t *s, const optmeasure_t *m);
/*
 * The run stopped before the search ended (why: a reason, which need not
 * outlive the call): the report lines are replayed to that point and end
 * with the best so far, its limit and what it gave up, as at a cap.
 */
void OptSearch_Stop(optsearch_t *s, const char *why);
/* the result, NULL when no trial was measured; *met is whether it meets the target */
const optcand_t *OptSearch_Result(const optsearch_t *s, int *met);
int OptSearch_NumLines(const optsearch_t *s);
const char *OptSearch_Line(const optsearch_t *s, int i);
/* a candidate as "name=value" changes from the first candidate, in buf */
void OptSearch_Describe(const optsearch_t *s, const optcand_t *c, char *buf, size_t len);

/*
 * optimize.c (TASK203): the optimize command and the optimize_* cvars.
 * Optimize_Frame is called for each timedemo frame (cl_view.c),
 * Optimize_Tick on every client frame in every connection state
 * (cl_main.c: the time cap, timedemo and the renderer are checked there)
 * and Optimize_DemoEnd at a disconnect (cl_network.c); each returns at
 * once when no run is on. A demo name for optimize_demo: NULL when it may be
 * played, else why not (as CL_DownloadFilter refuses: "..", a leading
 * '.' or '/', ':', '\', and any character a console line could split on).
 */
void Optimize_Init(void);
void Optimize_Frame(void);
void Optimize_Tick(void);
void Optimize_DemoEnd(void);
int Optimize_Running(void);
const char *Optimize_DemoError(const char *demo);
/* the tests: the time of a frame, and what plays a demo and checks that it exists (NULL: the engine's) */
void Optimize_FrameAt(long long us);
void Optimize_TickAt(long long us);
void Optimize_SetHooks(void (*play)(const char *demo), int (*exists)(const char *demo));
/* the tests: the clock the run's start and Optimize_Frame and Optimize_Tick read (NULL: Sys_Microseconds) */
void Optimize_SetClock(long long (*now)(void));

#endif
