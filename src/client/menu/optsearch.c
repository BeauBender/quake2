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
 * section 4). Each ranked priority has a ladder from its dearest position
 * (0) to its cheapest. The first candidate is the top of every ladder the
 * device can run; while a candidate misses the target, the lowest-ranked
 * priority with a step moves to its cheapest, and the first that meets it
 * is bisected for its dearest passing position; the rounds left buy back
 * the lower priorities one step at a time, the highest-ranked first.
 *
 * OptSearch_Next replays the whole search from the start over the trials
 * recorded so far, looking each candidate up by the cvar values it sets,
 * and stops at the first candidate not yet measured. So the search keeps
 * no state between calls but the measurements, and the same inputs give
 * the same candidates. Every loop is bounded by the ladders' lengths and
 * the round cap.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "header/optsearch.h"

#define OPT_MAXFRAMES 8192
#define OPT_BAND 0.03f   /* the noise band, a fraction of the target (plan, section 4.6) */

static const char *const opt_words[OPT_NPRI] = {
	"resolution", "rt", "hdr", "upscale", "denoise", "presentation"
};

static const char *const opt_cvars[OPT_NCV] = {
	"r_rt_shadows", "r_rt_sun", "r_rt_ao", "r_rt_water", "r_rt_gunshadow",
	"r_rt_ao_rays", "r_rt_ao_scale", "r_rt_water_hits", "r_hdr", "r_metalfx", "r_metalfx_scale"
};

static const char *const opt_pres_names[] = { "Stock", "Enhanced", "Retro", "Ray traced" };

const char *
OptSearch_Word(int pri)
{
	return (pri >= 0 && pri < OPT_NPRI) ? opt_words[pri] : NULL;
}

int
OptSearch_Priority(const char *word)
{
	for (int i = 0; word && i < OPT_NPRI; i++)
	{
		if (!strcmp(word, opt_words[i]))
		{
			return i;
		}
	}
	return -1;
}

const char *
OptSearch_CvarName(int cv)
{
	return (cv >= 0 && cv < OPT_NCV) ? opt_cvars[cv] : NULL;
}

int
OptSearch_ParseRank(const char *text, int rank[OPT_NPRI], int *n, char *err, size_t errlen)
{
	int seen[OPT_NPRI] = { 0 };

	*n = 0;
	if (errlen)
	{
		err[0] = '\0';
	}
	while (text && *text)
	{
		char word[16];
		size_t len = 0;
		int p;

		while (*text == ' ')
		{
			text++;
		}
		if (!*text)
		{
			break;
		}
		while (text[len] && text[len] != ' ')
		{
			len++;
		}
		if (len >= sizeof(word) || (p = (memcpy(word, text, len), word[len] = '\0', OptSearch_Priority(word))) < 0)
		{
			snprintf(err, errlen, "a word is not a priority; the words: %s %s %s %s %s %s", opt_words[0],
				opt_words[1], opt_words[2], opt_words[3], opt_words[4], opt_words[5]);
			return 0;
		}
		if (seen[p])
		{
			snprintf(err, errlen, "%s is given twice", word);
			return 0;
		}
		seen[p] = 1;
		rank[(*n)++] = p;
		text += len;
	}
	return 1;
}

int
OptSearch_ParseTarget(const char *text, float *ms)
{
	char *end;
	double v;

	if (!text || !text[0] || text[0] == ' ')
	{
		return 0;
	}
	v = strtod(text, &end);
	if (end == text || !(v > 0.0) || v > 1000000.0)
	{
		return 0;
	}
	if (!strcmp(end, "ms"))
	{
		*ms = (float)v;
		return 1;
	}
	if (*end)
	{
		return 0;
	}
	*ms = (float)(1000.0 / v);
	return 1;
}

int
OptSearch_ParseStat(const char *text, int *stat)
{
	if (!text)
	{
		return 0;
	}
	if (!strcmp(text, "mean"))
	{
		*stat = OPT_STAT_MEAN;
		return 1;
	}
	if (text[0] == 'p' && text[1] >= '0' && text[1] <= '9' && text[2] >= '0' && text[2] <= '9' && !text[3])
	{
		const int v = (text[1] - '0') * 10 + (text[2] - '0');

		if (v >= 50 && v <= 99)
		{
			*stat = v;
			return 1;
		}
	}
	return 0;
}

static int
FloatCmp(const void *a, const void *b)
{
	const float x = *(const float *)a, y = *(const float *)b;

	return (x > y) - (x < y);
}

int
OptSearch_Stats(const long long *t_us, int n, float warmup, int stat, optmeasure_t *out)
{
	static float ft[OPT_MAXFRAMES];
	const long long skip = (long long)(warmup * 1000000.0f);
	double sum = 0.0;
	int k = 0, idx;

	memset(out, 0, sizeof(*out));
	if (!t_us || n < 2)
	{
		return 0;
	}
	for (int i = 1; i < n && k < OPT_MAXFRAMES; i++)
	{
		if (t_us[i] - t_us[0] > skip)
		{
			ft[k] = (float)(t_us[i] - t_us[i - 1]) / 1000.0f;
			sum += ft[k];
			k++;
		}
	}
	if (!k)
	{
		return 0;
	}
	qsort(ft, (size_t)k, sizeof(ft[0]), FloatCmp);
	out->mean_ms = (float)(sum / k);
	/* the nearest rank */
	idx = (int)ceil(0.95 * k) - 1;
	out->p95_ms = ft[(idx < 0) ? 0 : idx];
	if (stat == OPT_STAT_MEAN)
	{
		out->stat_ms = out->mean_ms;
	}
	else
	{
		idx = (int)ceil(stat / 100.0 * k) - 1;
		out->stat_ms = ft[(idx < 0) ? 0 : idx];
	}
	out->seconds = (float)(t_us[n - 1] - t_us[0]) / 1000000.0f;
	return 1;
}

static void
Line(optsearch_t *s, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static void
Line(optsearch_t *s, const char *fmt, ...)
{
	va_list ap;

	if (s->nlines >= OPT_MAXLINES)
	{
		return;
	}
	va_start(ap, fmt);
	vsnprintf(s->lines[s->nlines++], OPT_LINELEN, fmt, ap);
	va_end(ap);
}

static int
Ranked(const optsearch_t *s, int p)
{
	for (int i = 0; i < s->in.nrank; i++)
	{
		if (s->in.rank[i] == p)
		{
			return 1;
		}
	}
	return 0;
}

static int
RankOf(const optsearch_t *s, int p)
{
	for (int i = 0; i < s->in.nrank; i++)
	{
		if (s->in.rank[i] == p)
		{
			return i;
		}
	}
	return OPT_NPRI;
}

/* appends one step to ladder p: the previous position's values with cv at v */
static void
Step(optsearch_t *s, int p, int cv, float v)
{
	const int k = s->nsteps[p];

	if (k >= OPT_MAXSTEPS)
	{
		return;
	}
	if (k > 0)
	{
		memcpy(s->steps[p][k], s->steps[p][k - 1], sizeof(s->steps[p][k]));
	}
	s->steps[p][k][cv] = v;
	s->stepset[p][cv] = 1;
	snprintf(s->stepname[p][k], sizeof(s->stepname[p][k]), "%s %g", opt_cvars[cv], (double)v);
	s->nsteps[p]++;
}

/* the rt ladder: the top, the cost settings dearest first, then the switches the presentation leaves, dearest first */
static void
BuildRT(optsearch_t *s)
{
	/* what the presentation reads each switch as: -1 for the player's (the search may turn it) */
	float fixed[5] = { -1, -1, -1, -1, -1 };
	int order[4] = { OPT_CV_AO, OPT_CV_WATER, OPT_CV_SUN, OPT_CV_SHADOWS };
	const int hint_of[4] = { 2, 3, 1, 0 };   /* hint_ms index of order[i] at start */
	float cost[4];

	if (s->in.presentation == OPT_PRES_ENHANCED)
	{
		fixed[OPT_CV_SHADOWS] = fixed[OPT_CV_SUN] = fixed[OPT_CV_AO] = fixed[OPT_CV_WATER] = 1;
	}
	else if (s->in.presentation == OPT_PRES_RTPRIMARY)
	{
		fixed[OPT_CV_SHADOWS] = fixed[OPT_CV_SUN] = fixed[OPT_CV_AO] = 0;
		fixed[OPT_CV_WATER] = 1;
	}

	/* the top: position 0 */
	s->nsteps[OPT_RT] = 1;
	for (int cv = OPT_CV_SHADOWS; cv <= OPT_CV_GUNSHADOW; cv++)
	{
		if (fixed[cv] < 0)
		{
			s->steps[OPT_RT][0][cv] = 1;
			s->stepset[OPT_RT][cv] = 1;
		}
	}
	if (fixed[OPT_CV_WATER] != 0)
	{
		s->steps[OPT_RT][0][OPT_CV_WATER_HITS] = 1;
		s->stepset[OPT_RT][OPT_CV_WATER_HITS] = 1;
	}
	if (fixed[OPT_CV_AO] != 0)
	{
		s->steps[OPT_RT][0][OPT_CV_AO_RAYS] = 2;
		s->steps[OPT_RT][0][OPT_CV_AO_SCALE] = 1;
		s->stepset[OPT_RT][OPT_CV_AO_RAYS] = s->stepset[OPT_RT][OPT_CV_AO_SCALE] = 1;
	}
	snprintf(s->stepname[OPT_RT][0], sizeof(s->stepname[OPT_RT][0]), "rt at its top");

	if (s->stepset[OPT_RT][OPT_CV_WATER_HITS])
	{
		Step(s, OPT_RT, OPT_CV_WATER_HITS, 0);
	}
	if (s->stepset[OPT_RT][OPT_CV_AO_RAYS])
	{
		Step(s, OPT_RT, OPT_CV_AO_RAYS, 1);
		Step(s, OPT_RT, OPT_CV_AO_SCALE, 0.5f);
		Step(s, OPT_RT, OPT_CV_AO_SCALE, 0.33f);
	}

	/* the switches: the probe's hints, dearest first, ties in the fixed order; with none, the fixed order */
	for (int i = 0; i < 4; i++)
	{
		cost[i] = s->in.have_hints ? s->in.hint_ms[hint_of[i]] : 0.0f;
	}
	for (int i = 1; i < 4; i++)
	{
		for (int j = i; j > 0 && cost[j] > cost[j - 1]; j--)
		{
			const float c = cost[j];
			const int o = order[j];

			cost[j] = cost[j - 1];
			order[j] = order[j - 1];
			cost[j - 1] = c;
			order[j - 1] = o;
		}
	}
	for (int i = 0; i < 4; i++)
	{
		if (fixed[order[i]] < 0)
		{
			Step(s, OPT_RT, order[i], 0);
		}
	}
	/* no r_rt_stats line of its own, so no hint can place it: last */
	Step(s, OPT_RT, OPT_CV_GUNSHADOW, 0);

	for (int cv = OPT_CV_SHADOWS; cv <= OPT_CV_WATER; cv++)
	{
		if (fixed[cv] >= 0)
		{
			snprintf(s->why[OPT_PRESENTATION] + strlen(s->why[OPT_PRESENTATION]),
				sizeof(s->why[OPT_PRESENTATION]) - strlen(s->why[OPT_PRESENTATION]), "%s%s",
				s->why[OPT_PRESENTATION][0] ? " " : "", opt_cvars[cv]);
		}
	}
}

void
OptSearch_Init(optsearch_t *s, const optinput_t *in)
{
	const int scalers = in->metalfx_modes & (2 | 4 | 8);
	const int retro = (in->presentation == OPT_PRES_RETRO);
	int up_ok, dn_ok, res_tried;

	memset(s, 0, sizeof(*s));
	s->in = *in;
	if (s->in.nrank < 0 || s->in.nrank > OPT_NPRI)
	{
		s->in.nrank = 0;
	}
	if (s->in.rounds < 1)
	{
		s->in.rounds = 1;
	}
	if (s->in.rounds > OPT_MAXROUNDS)
	{
		s->in.rounds = OPT_MAXROUNDS;
	}
	if (!(s->in.seconds > 0.0f))
	{
		s->in.seconds = 1.0f;
	}
	if (s->in.presentation < OPT_PRES_STOCK || s->in.presentation > OPT_PRES_RTPRIMARY)
	{
		s->in.presentation = OPT_PRES_STOCK;
	}
	s->best = -1;

	/* rt */
	if (!in->rt_available)
	{
		snprintf(s->why[OPT_RT], sizeof(s->why[OPT_RT]), "ray tracing is not available on this device");
	}
	else if (retro)
	{
		snprintf(s->why[OPT_RT], sizeof(s->why[OPT_RT]), "set by the presentation");
	}
	else if (Ranked(s, OPT_RT))
	{
		BuildRT(s);
	}

	/* hdr */
	if (in->hdr_why)
	{
		snprintf(s->why[OPT_HDR], sizeof(s->why[OPT_HDR]), "%s", in->hdr_why);
	}
	else if (Ranked(s, OPT_HDR))
	{
		Step(s, OPT_HDR, OPT_CV_HDR, 1);
		Step(s, OPT_HDR, OPT_CV_HDR, 0);
	}

	/* upscale and denoise: one cvar, r_metalfx */
	up_ok = Ranked(s, OPT_UPSCALE) && (in->metalfx_modes & (2 | 4)) && !retro;
	dn_ok = Ranked(s, OPT_DENOISE) && (in->metalfx_modes & 8) && in->rt_available && !retro;
	if (!(in->metalfx_modes & (2 | 4)))
	{
		snprintf(s->why[OPT_UPSCALE], sizeof(s->why[OPT_UPSCALE]), "no MetalFX scaler on this device");
	}
	else if (retro)
	{
		snprintf(s->why[OPT_UPSCALE], sizeof(s->why[OPT_UPSCALE]), "set by the presentation");
	}
	if (!(in->metalfx_modes & 8))
	{
		snprintf(s->why[OPT_DENOISE], sizeof(s->why[OPT_DENOISE]), "%s",
			scalers ? "no denoised scaler on this device" : "no MetalFX scaler on this device");
	}
	else if (!in->rt_available)
	{
		snprintf(s->why[OPT_DENOISE], sizeof(s->why[OPT_DENOISE]), "ray tracing is not available on this device");
	}
	else if (retro)
	{
		snprintf(s->why[OPT_DENOISE], sizeof(s->why[OPT_DENOISE]), "set by the presentation");
	}

	/* resolution: a scaler on the device, and one chosen (rule 3 of section 4.1) */
	res_tried = 0;
	if (!scalers)
	{
		snprintf(s->why[OPT_RESOLUTION], sizeof(s->why[OPT_RESOLUTION]), "no MetalFX scaler on this device");
	}
	else if (retro)
	{
		snprintf(s->why[OPT_RESOLUTION], sizeof(s->why[OPT_RESOLUTION]), "set by the presentation");
	}
	else if (Ranked(s, OPT_RESOLUTION))
	{
		Step(s, OPT_RESOLUTION, OPT_CV_SCALE, 1);
		res_tried = 1;
		if (up_ok || dn_ok || in->player_metalfx >= 1.0f)
		{
			Step(s, OPT_RESOLUTION, OPT_CV_SCALE, 0.75f);
			Step(s, OPT_RESOLUTION, OPT_CV_SCALE, 0.5f);
			Step(s, OPT_RESOLUTION, OPT_CV_SCALE, 0.34f);
		}
		else
		{
			snprintf(s->why[OPT_RESOLUTION], sizeof(s->why[OPT_RESOLUTION]),
				"the view is drawn at full size because no scaler is chosen");
		}
	}

	if (up_ok)
	{
		if (in->metalfx_modes & 4)
		{
			Step(s, OPT_UPSCALE, OPT_CV_METALFX, 2);
		}
		if (in->metalfx_modes & 2)
		{
			Step(s, OPT_UPSCALE, OPT_CV_METALFX, 1);
		}
		/* no scaler is the cheapest step only while the view is drawn at full size (rule 2) */
		if (res_tried || (!Ranked(s, OPT_RESOLUTION) && in->player_scale == 1.0f))
		{
			Step(s, OPT_UPSCALE, OPT_CV_METALFX, 0);
		}
	}
	if (dn_ok)
	{
		Step(s, OPT_DENOISE, OPT_CV_METALFX, 3);
		Step(s, OPT_DENOISE, OPT_CV_METALFX, (in->metalfx_modes & 4) ? 2.0f : (in->metalfx_modes & 2) ? 1.0f : 0.0f);
	}
}

/* the candidate of the positions pos, after the coupling; *forced is whether resolution raised r_metalfx */
static void
Build(const optsearch_t *s, const int pos[OPT_NPRI], optcand_t *c, int *forced)
{
	memset(c, 0, sizeof(*c));
	memcpy(c->pos, pos, sizeof(c->pos));
	*forced = 0;

	for (int p = 0; p < OPT_NPRI; p++)
	{
		if (!s->nsteps[p] || p == OPT_UPSCALE || p == OPT_DENOISE)
		{
			continue;
		}
		for (int cv = 0; cv < OPT_NCV; cv++)
		{
			if (s->stepset[p][cv])
			{
				c->set[cv] = 1;
				c->val[cv] = s->steps[p][pos[p]][cv];
			}
		}
	}

	if (s->nsteps[OPT_DENOISE] && pos[OPT_DENOISE] == 0)
	{
		c->set[OPT_CV_METALFX] = 1;
		c->val[OPT_CV_METALFX] = 3;
	}
	else if (s->nsteps[OPT_UPSCALE])
	{
		c->set[OPT_CV_METALFX] = 1;
		c->val[OPT_CV_METALFX] = s->steps[OPT_UPSCALE][pos[OPT_UPSCALE]][OPT_CV_METALFX];
	}
	else if (s->nsteps[OPT_DENOISE])
	{
		c->set[OPT_CV_METALFX] = 1;
		c->val[OPT_CV_METALFX] = s->steps[OPT_DENOISE][pos[OPT_DENOISE]][OPT_CV_METALFX];
	}

	/* drawing smaller always has a scaler (rule 1) */
	if (c->set[OPT_CV_SCALE] && c->val[OPT_CV_SCALE] < 1.0f)
	{
		if (c->set[OPT_CV_METALFX] && c->val[OPT_CV_METALFX] < 1.0f)
		{
			c->val[OPT_CV_METALFX] = 1;
			*forced = 1;
		}
		else if (!c->set[OPT_CV_METALFX] && s->in.player_metalfx < 1.0f)
		{
			c->set[OPT_CV_METALFX] = 1;
			c->val[OPT_CV_METALFX] = 1;
			*forced = 1;
		}
	}
}

static int
Same(const optcand_t *a, const optcand_t *b)
{
	for (int cv = 0; cv < OPT_NCV; cv++)
	{
		if (a->set[cv] != b->set[cv] || (a->set[cv] && a->val[cv] != b->val[cv]))
		{
			return 0;
		}
	}
	return 1;
}

void
OptSearch_Describe(const optsearch_t *s, const optcand_t *c, char *buf, size_t len)
{
	size_t at = 0;

	if (!len)
	{
		return;
	}
	buf[0] = '\0';
	for (int cv = 0; cv < OPT_NCV && at < len; cv++)
	{
		if (c->set[cv] && (!s->ntrials || !s->trial[0].set[cv] || s->trial[0].val[cv] != c->val[cv]))
		{
			at += (size_t)snprintf(buf + at, len - at, "%s%s %g", at ? ", " : "", opt_cvars[cv], (double)c->val[cv]);
		}
	}
	if (!buf[0])
	{
		snprintf(buf, len, "the first candidate");
	}
}

/* a's positions hold more of the higher-ranked priorities than b's, or the same and a is faster */
static int
Better(const optsearch_t *s, int a, int b)
{
	const float d = s->meas[a].stat_ms - s->meas[b].stat_ms;

	for (int i = 0; i < s->in.nrank; i++)
	{
		const int p = s->in.rank[i];

		if (s->trial[a].pos[p] != s->trial[b].pos[p])
		{
			return s->trial[a].pos[p] < s->trial[b].pos[p];
		}
	}
	return (fabsf(d) >= OPT_BAND * s->in.target_ms) && (d < 0);
}

static int
Pass(const optsearch_t *s, int i)
{
	return s->meas[i].stat_ms <= s->in.target_ms;
}

static void
Consider(optsearch_t *s, int i)
{
	if (Pass(s, i))
	{
		if (s->best < 0 || !s->met || Better(s, i, s->best))
		{
			s->best = i;
			s->met = 1;
		}
	}
	else if (!s->met && (s->best < 0 || s->meas[i].stat_ms < s->meas[s->best].stat_ms))
	{
		s->best = i;
	}
}

/* the replay's view of one measurement */
typedef struct
{
	optcand_t next;
	int seen[OPT_MAXROUNDS];
	const char *cap;
} optreplay_t;

#define OPT_NEED (-1)
#define OPT_CAPPED (-2)

/* the trial of positions pos: its index, OPT_NEED with r->next set, or OPT_CAPPED */
static int
Measure(optsearch_t *s, optreplay_t *r, const int pos[OPT_NPRI])
{
	char desc[OPT_LINELEN];
	optcand_t c;
	float elapsed = s->in.probe_seconds;
	int forced;

	Build(s, pos, &c, &forced);
	for (int i = 0; i < s->ntrials; i++)
	{
		if (!Same(&s->trial[i], &c))
		{
			continue;
		}
		OptSearch_Describe(s, &c, desc, sizeof(desc));
		if (r->seen[i])
		{
			Line(s, "  %s equals trial %d: looked up, not timed", desc, i + 1);
		}
		else
		{
			r->seen[i] = 1;
			Line(s, "trial %d: %s: stat %.1f ms, mean %.1f ms, p95 %.1f ms, presented %.1f fps: %s", i + 1, desc,
				(double)s->meas[i].stat_ms, (double)s->meas[i].mean_ms,
				(double)s->meas[i].p95_ms, (double)s->meas[i].presented_fps, Pass(s, i) ? "meets" : "misses");
		}
		Consider(s, i);
		return i;
	}

	for (int i = 0; i < s->ntrials; i++)
	{
		elapsed += s->meas[i].seconds;
	}
	if (s->ntrials >= s->in.rounds)
	{
		r->cap = "round cap";
		return OPT_CAPPED;
	}
	if (elapsed >= s->in.seconds)
	{
		r->cap = "time cap";
		return OPT_CAPPED;
	}
	r->next = c;
	return OPT_NEED;
}

/* whether priority p may move to a cheaper position from pos */
static int
HasStep(const optsearch_t *s, int p, const int pos[OPT_NPRI])
{
	if (s->nsteps[p] < 2 || pos[p] >= s->nsteps[p] - 1)
	{
		return 0;
	}
	/* rule 4: while denoise holds r_metalfx 3 from above, upscale has no step of its own */
	if (p == OPT_UPSCALE && s->nsteps[OPT_DENOISE] && pos[OPT_DENOISE] == 0 &&
		RankOf(s, OPT_DENOISE) < RankOf(s, OPT_UPSCALE))
	{
		return 0;
	}
	return 1;
}

static void
Header(optsearch_t *s)
{
	const char *stat = (s->in.stat == OPT_STAT_MEAN) ? "mean" : "p";

	Line(s, "target: %s%.0d <= %.1f ms; %d rounds, %.0f s", stat, s->in.stat, (double)s->in.target_ms,
		s->in.rounds, (double)s->in.seconds);
	for (int i = 0; i < s->in.nrank; i++)
	{
		const int p = s->in.rank[i];

		if (p == OPT_PRESENTATION)
		{
			Line(s, "presentation: not turned (%s; r_presentation needs a vid_restart)",
				opt_pres_names[s->in.presentation]);
		}
		else if (!s->nsteps[p])
		{
			Line(s, "%s: not tried: %s", opt_words[p], s->why[p][0] ? s->why[p] : "nothing to turn");
		}
		else if (s->why[p][0])
		{
			Line(s, "%s: %s", opt_words[p], s->why[p]);
		}
	}
	if (Ranked(s, OPT_RT) && s->why[OPT_PRESENTATION][0])
	{
		for (int cv = OPT_CV_SHADOWS; cv <= OPT_CV_WATER; cv++)
		{
			if (strstr(s->why[OPT_PRESENTATION], opt_cvars[cv]))
			{
				Line(s, "%s: set by the presentation (%s); Stock would let the search try it", opt_cvars[cv],
					opt_pres_names[s->in.presentation]);
			}
		}
	}
	if (s->nsteps[OPT_UPSCALE] && s->nsteps[OPT_DENOISE] && RankOf(s, OPT_DENOISE) < RankOf(s, OPT_UPSCALE))
	{
		Line(s, "upscale: no step of its own while denoise holds r_metalfx 3");
	}
}

static void
Limit(optsearch_t *s, const optreplay_t *r)
{
	const optcand_t *c = &s->trial[s->best];
	const optmeasure_t *m = &s->meas[s->best];
	int any = 0, cheapest = 1;

	for (int i = 0; i < s->in.nrank; i++)
	{
		const int p = s->in.rank[i];

		if (s->nsteps[p] > 1)
		{
			any = 1;
			cheapest &= (c->pos[p] == s->nsteps[p] - 1);
		}
	}
	if (!any)
	{
		Line(s, "limit: nothing could be turned");
		return;
	}
	if (cheapest)
	{
		Line(s, "limit: every ranked setting is at its cheapest; the cost left is the stock raster at this output "
			"size, which the optimizer does not change (a vid_restart setting)");
		return;
	}
	if (m->presented_fps > 0 && m->mean_ms > 0 && m->presented_fps < (1000.0f / m->mean_ms) * (1.0f - OPT_BAND))
	{
		Line(s, "limit: the display's present rate (%.1f presented, %.1f rendered fps)", (double)m->presented_fps,
			(double)(1000.0f / m->mean_ms));
		return;
	}
	for (int i = 0; i < s->in.nrank; i++)
	{
		const int p = s->in.rank[i];

		if (p != OPT_PRESENTATION && !s->nsteps[p])
		{
			Line(s, "limit: %s not tried: %s", opt_words[p], s->why[p]);
			return;
		}
	}
	if (Ranked(s, OPT_RT) && s->why[OPT_PRESENTATION][0])
	{
		Line(s, "limit: %s set by the presentation", s->why[OPT_PRESENTATION]);
		return;
	}
	Line(s, "limit: the %s ended the search", r->cap ? r->cap : "search");
}

static void
End(optsearch_t *s, const optreplay_t *r)
{
	char desc[OPT_LINELEN];
	const optcand_t *c;
	int forced;
	optcand_t again;

	s->done = 1;
	if (r->cap)
	{
		Line(s, "the %s ended the search", r->cap);
	}
	if (s->best < 0)
	{
		Line(s, "no trial was measured");
		return;
	}
	c = &s->trial[s->best];
	OptSearch_Describe(s, c, desc, sizeof(desc));
	if (!s->met)
	{
		Line(s, "closest: %.1f ms against %.1f ms (trial %d: %s)", (double)s->meas[s->best].stat_ms,
			(double)s->in.target_ms, s->best + 1, desc);
		Limit(s, r);
		return;
	}
	Line(s, "result: trial %d, %.1f ms meets %.1f ms: %s", s->best + 1, (double)s->meas[s->best].stat_ms,
		(double)s->in.target_ms, desc);
	for (int i = 0; i < s->in.nrank; i++)
	{
		const int p = s->in.rank[i];

		for (int k = 1; k <= c->pos[p] && k < s->nsteps[p]; k++)
		{
			Line(s, "%s: %s gave up step %d of %d to meet %.1f ms", s->stepname[p][k], opt_words[p], k,
				s->nsteps[p] - 1, (double)s->in.target_ms);
		}
	}
	Build(s, c->pos, &again, &forced);
	if (forced)
	{
		Line(s, "r_metalfx %g: forced on by resolution %g", (double)c->val[OPT_CV_METALFX],
			(double)c->val[OPT_CV_SCALE]);
	}
}

/* the search from the start over the recorded trials: 1 with r->next the next to time, 0 when it ended */
static int
Search(optsearch_t *s, optreplay_t *r)
{
	int pos[OPT_NPRI] = { 0 };
	int settled = -1, t;

	s->nlines = 0;
	s->best = -1;
	s->met = 0;
	s->done = 0;
	Header(s);

#define OPT_MEASURE(T) do { if (((T) = Measure(s, r, pos)) == OPT_NEED) { return 1; } \
		if ((T) == OPT_CAPPED) { End(s, r); return 0; } } while (0)

	OPT_MEASURE(t);
	if (Pass(s, t))
	{
		Line(s, "trial 1 meets the target: nothing is given up");
		End(s, r);
		return 0;
	}

	/* give up, the lowest-ranked first */
	for (int i = s->in.nrank - 1; i >= 0 && settled < 0; i--)
	{
		const int p = s->in.rank[i];
		int lo = pos[p], hi = s->nsteps[p] - 1;

		if (!HasStep(s, p, pos))
		{
			continue;
		}
		pos[p] = hi;
		OPT_MEASURE(t);
		if (!Pass(s, t))
		{
			Line(s, "  %s at its cheapest still misses: it stays there", opt_words[p]);
			continue;
		}
		/* the dearest position that still meets the target */
		settled = i;
		while (hi - lo > 1)
		{
			const int mid = (lo + hi) / 2;

			pos[p] = mid;
			OPT_MEASURE(t);
			if (Pass(s, t))
			{
				hi = mid;
			}
			else
			{
				lo = mid;
			}
		}
		pos[p] = hi;
		Line(s, "  %s settles at position %d of %d", opt_words[p], hi, s->nsteps[p] - 1);
	}
	if (settled < 0)
	{
		End(s, r);
		return 0;
	}

	/* buy back the priorities below, the highest-ranked first, a step at a time */
	for (int i = settled + 1; i < s->in.nrank; i++)
	{
		const int p = s->in.rank[i];

		while (pos[p] > 0 && s->nsteps[p] > 1)
		{
			pos[p]--;
			OPT_MEASURE(t);
			if (!Pass(s, t))
			{
				pos[p]++;
				Line(s, "  buying back %s missed: undone", opt_words[p]);
				break;
			}
			Line(s, "  %s bought back to position %d", opt_words[p], pos[p]);
		}
	}
#undef OPT_MEASURE

	End(s, r);
	return 0;
}

int
OptSearch_Next(optsearch_t *s, optcand_t *c)
{
	static optreplay_t r;

	memset(&r, 0, sizeof(r));
	if (Search(s, &r))
	{
		*c = r.next;
		return 1;
	}
	return 0;
}

void
OptSearch_Record(optsearch_t *s, const optmeasure_t *m)
{
	optreplay_t r;

	if (s->ntrials >= OPT_MAXROUNDS)
	{
		return;
	}
	/* the candidate is the one the replay stops at */
	memset(&r, 0, sizeof(r));
	if (!Search(s, &r))
	{
		return;
	}
	s->trial[s->ntrials] = r.next;
	s->meas[s->ntrials] = *m;
	s->ntrials++;
}

const optcand_t *
OptSearch_Result(const optsearch_t *s, int *met)
{
	*met = s->met;
	return (s->best >= 0) ? &s->trial[s->best] : NULL;
}

int
OptSearch_NumLines(const optsearch_t *s)
{
	return s->nlines;
}

const char *
OptSearch_Line(const optsearch_t *s, int i)
{
	return (i >= 0 && i < s->nlines) ? s->lines[i] : "";
}
