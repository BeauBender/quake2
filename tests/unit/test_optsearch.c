/*
 * The settings optimizer's search (TASK203, src/client/menu/optsearch.c;
 * docs/plans/settings-optimizer.md, section 4) on recorded measurements:
 * the plan's worked trace gives its ten rows, meeting the target keeps
 * the most of the highest-ranked, missing gives up the lowest first and
 * the dearest step first by the hints, nothing meeting gives the closest
 * with its limit, the caps stop it with the best so far, a false
 * capability is never tried, and the same inputs give the same run.
 */

#include <stdio.h>
#include <string.h>

#include "test.h"
#include "../../src/client/menu/header/optsearch.h"

static void
DefaultInput(optinput_t *in)
{
	memset(in, 0, sizeof(*in));
	in->nrank = 0;
	in->rank[in->nrank++] = OPT_RT;
	in->rank[in->nrank++] = OPT_RESOLUTION;
	in->rank[in->nrank++] = OPT_HDR;
	in->rank[in->nrank++] = OPT_UPSCALE;
	in->rank[in->nrank++] = OPT_DENOISE;
	in->rank[in->nrank++] = OPT_PRESENTATION;
	in->rt_available = 1;
	in->metalfx_modes = 1 | 2 | 4 | 8;
	in->hdr_why = NULL;
	in->presentation = OPT_PRES_STOCK;
	in->player_metalfx = 0.0f;
	in->player_scale = 0.0f;
	in->target_ms = 16.7f;
	in->stat = 95;
	in->rounds = 10;
	in->seconds = 300.0f;
	in->probe_seconds = 10.0f;
}

static optmeasure_t
Meas(float stat)
{
	optmeasure_t m;

	m.stat_ms = stat;
	m.mean_ms = stat * 0.8f;
	m.p95_ms = stat;
	m.presented_fps = 1000.0f / (stat * 0.8f);
	m.seconds = 10.0f;
	return m;
}

static int
Has(const optsearch_t *s, const char *text)
{
	for (int i = 0; i < OptSearch_NumLines(s); i++)
	{
		if (strstr(OptSearch_Line(s, i), text))
		{
			return 1;
		}
	}
	return 0;
}

/* plays the search on a list of statistics, one per timed trial; the candidates in got */
static int
Play(optsearch_t *s, const optinput_t *in, const float *stats, int nstats, optcand_t *got, int max)
{
	optcand_t c;
	int n = 0;

	OptSearch_Init(s, in);
	while (n < max && OptSearch_Next(s, &c))
	{
		const optmeasure_t m = Meas((n < nstats) ? stats[n] : 99.0f);

		got[n] = c;
		OptSearch_Record(s, &m);
		n++;
	}
	/* one more Next ends the replay on the last measurement */
	OptSearch_Next(s, &c);
	return n;
}

static const float trace[] = { 30.0f, 26.0f, 24.0f, 22.0f, 18.0f, 12.0f, 15.0f, 17.0f, 16.0f, 16.5f };

/* section 4.2's table, row by row */
TEST(optsearch_worked_trace)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[16];
	const optcand_t *r;
	int met = 0, n;
	t_text_t t = { 0 };

	DefaultInput(&in);
	n = Play(&s, &in, trace, 10, c, 16);
	T_REQUIRE(n == 10);

	T_EQ_FLT(c[0].val[OPT_CV_METALFX], 3.0f);
	T_EQ_FLT(c[0].val[OPT_CV_SCALE], 1.0f);
	T_EQ_FLT(c[0].val[OPT_CV_HDR], 1.0f);
	T_EQ_INT(c[0].pos[OPT_RT], 0);
	T_EQ_FLT(c[0].val[OPT_CV_AO_RAYS], 2.0f);
	T_EQ_FLT(c[0].val[OPT_CV_WATER_HITS], 1.0f);
	T_EQ_FLT(c[1].val[OPT_CV_METALFX], 2.0f);
	T_EQ_FLT(c[2].val[OPT_CV_METALFX], 0.0f);
	T_EQ_FLT(c[3].val[OPT_CV_HDR], 0.0f);
	T_EQ_FLT(c[4].val[OPT_CV_SCALE], 0.34f);
	T_EQ_FLT(c[4].val[OPT_CV_METALFX], 1.0f);   /* forced on by resolution */
	T_EQ_INT(c[4].pos[OPT_UPSCALE], 2);         /* upscale still stands at its 0 step */
	T_EQ_INT(c[5].pos[OPT_RT], 9);
	for (int cv = OPT_CV_SHADOWS; cv <= OPT_CV_GUNSHADOW; cv++)
	{
		T_EQ_FLT(c[5].val[cv], 0.0f);
	}
	T_EQ_INT(c[6].pos[OPT_RT], 4);
	T_EQ_FLT(c[6].val[OPT_CV_AO_SCALE], 0.33f);
	T_EQ_FLT(c[6].val[OPT_CV_AO], 1.0f);
	T_EQ_INT(c[7].pos[OPT_RT], 2);
	T_EQ_FLT(c[7].val[OPT_CV_AO_RAYS], 1.0f);
	T_EQ_FLT(c[7].val[OPT_CV_AO_SCALE], 1.0f);
	T_EQ_INT(c[8].pos[OPT_RT], 3);
	T_EQ_FLT(c[8].val[OPT_CV_AO_SCALE], 0.5f);
	T_EQ_INT(c[9].pos[OPT_RT], 3);
	T_EQ_FLT(c[9].val[OPT_CV_SCALE], 0.5f);
	T_EQ_FLT(c[9].val[OPT_CV_METALFX], 1.0f);

	r = OptSearch_Result(&s, &met);
	T_REQUIRE(r != NULL);
	T_EQ_INT(met, 1);
	T_EQ_MEM(r, &c[9], sizeof(*r));
	T_CHECK(Has(&s, "r_metalfx 1: forced on by resolution 0.5"));
	T_CHECK(Has(&s, "round cap"));

	for (int i = 0; i < OptSearch_NumLines(&s); i++)
	{
		t_text_printf(&t, "%s\n", OptSearch_Line(&s, i));
	}
	t_pin("optsearch_worked_trace", t.data);
	t_text_free(&t);
}

/* a trial passes when its statistic is at most the target; the first meeting it ends the run at one round */
TEST(optsearch_first_meets_ends_at_one)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[4];
	const float one[] = { 16.7f };
	int met = 0;

	DefaultInput(&in);
	T_EQ_INT(Play(&s, &in, one, 1, c, 4), 1);
	T_REQUIRE(OptSearch_Result(&s, &met) != NULL);
	T_EQ_INT(met, 1);
	T_CHECK(Has(&s, "nothing is given up"));
}

/* recorded measurements that meet the target: the result keeps the most of the highest-ranked */
TEST(optsearch_meets_keeps_highest_ranked)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[16];
	const optcand_t *r;
	const float stats[] = { 18.0f, 14.0f };
	int met = 0;

	DefaultInput(&in);
	in.target_ms = 15.0f;
	T_EQ_INT(Play(&s, &in, stats, 2, c, 16), 2);
	r = OptSearch_Result(&s, &met);
	T_REQUIRE(r != NULL);
	T_EQ_INT(met, 1);
	T_EQ_INT(r->pos[OPT_RT], 0);
	T_EQ_INT(r->pos[OPT_RESOLUTION], 0);
	T_EQ_INT(r->pos[OPT_HDR], 0);
	T_EQ_INT(r->pos[OPT_UPSCALE], 0);
	T_EQ_INT(r->pos[OPT_DENOISE], 1);   /* the lowest-ranked, and only it, given up */
	T_CHECK(Has(&s, "r_metalfx 2: denoise"));
}

/* missing gives up the lowest-ranked first; within rt the switches go dearest first by the hints */
TEST(optsearch_gives_up_lowest_first_dearest_by_hints)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[4];
	const float miss[] = { 30.0f, 30.0f };

	DefaultInput(&in);
	in.nrank = 2;
	in.rank[0] = OPT_RT;
	in.rank[1] = OPT_HDR;
	T_EQ_INT(Play(&s, &in, miss, 2, c, 2), 2);
	T_EQ_INT(c[1].pos[OPT_HDR], 1);
	T_EQ_INT(c[1].pos[OPT_RT], 0);
	T_EQ_INT(c[0].set[OPT_CV_METALFX], 0);   /* not ranked: the player's value stays */
	T_EQ_INT(c[0].set[OPT_CV_SCALE], 0);

	/* no hints: the fixed order, ao, water, sun, shadows, gun shadow */
	T_EQ_INT(s.nsteps[OPT_RT], 10);
	T_EQ_FLT(s.steps[OPT_RT][5][OPT_CV_AO], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][5][OPT_CV_WATER], 1.0f);
	T_EQ_FLT(s.steps[OPT_RT][6][OPT_CV_WATER], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][7][OPT_CV_SUN], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][8][OPT_CV_SHADOWS], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][8][OPT_CV_GUNSHADOW], 1.0f);

	/* hints: sun the dearest, then water, ao, shadows; the gun shadow stays last */
	in.have_hints = 1;
	in.hint_ms[0] = 0.5f;
	in.hint_ms[1] = 3.0f;
	in.hint_ms[2] = 1.0f;
	in.hint_ms[3] = 2.0f;
	OptSearch_Init(&s, &in);
	T_EQ_FLT(s.steps[OPT_RT][5][OPT_CV_SUN], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][5][OPT_CV_WATER], 1.0f);
	T_EQ_FLT(s.steps[OPT_RT][6][OPT_CV_WATER], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][6][OPT_CV_AO], 1.0f);
	T_EQ_FLT(s.steps[OPT_RT][7][OPT_CV_AO], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][8][OPT_CV_SHADOWS], 0.0f);
	T_EQ_FLT(s.steps[OPT_RT][8][OPT_CV_GUNSHADOW], 1.0f);
	T_EQ_FLT(s.steps[OPT_RT][9][OPT_CV_GUNSHADOW], 0.0f);
}

/* nothing meets the target: the closest measured, with the line naming its limit */
TEST(optsearch_nothing_meets_gives_closest)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[16];
	const optcand_t *r;
	const float stats[] = { 40.0f, 39.0f, 38.0f, 37.0f, 36.0f, 21.0f, 25.0f, 30.0f, 33.0f, 34.0f };
	int met = 1, n;

	DefaultInput(&in);
	n = Play(&s, &in, stats, 10, c, 16);
	T_CHECK(n <= 10);
	r = OptSearch_Result(&s, &met);
	T_REQUIRE(r != NULL);
	T_EQ_INT(met, 0);
	T_EQ_MEM(r, &c[5], sizeof(*r));   /* 21 ms, every ranked setting at its cheapest */
	T_CHECK(Has(&s, "closest: 21.0 ms against 16.7 ms"));
	T_CHECK(Has(&s, "limit: every ranked setting is at its cheapest"));
}

/* the same inputs give the same candidates, result and report, run twice */
TEST(optsearch_deterministic)
{
	static optsearch_t a, b;
	optinput_t in;
	optcand_t ca[16], cb[16];
	int na, nb;

	DefaultInput(&in);
	na = Play(&a, &in, trace, 10, ca, 16);
	nb = Play(&b, &in, trace, 10, cb, 16);
	T_EQ_INT(na, nb);
	T_EQ_MEM(ca, cb, sizeof(ca[0]) * (size_t)na);
	T_EQ_INT(OptSearch_NumLines(&a), OptSearch_NumLines(&b));
	for (int i = 0; i < OptSearch_NumLines(&a); i++)
	{
		T_EQ_STR(OptSearch_Line(&a, i), OptSearch_Line(&b, i));
	}
}

/* the round cap and the time cap each stop the search with the best so far */
TEST(optsearch_caps_stop_with_best)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[16];
	int met = 1;

	DefaultInput(&in);
	in.rounds = 3;
	T_EQ_INT(Play(&s, &in, trace, 10, c, 16), 3);
	T_REQUIRE(OptSearch_Result(&s, &met) != NULL);
	T_EQ_INT(met, 0);
	T_EQ_MEM(OptSearch_Result(&s, &met), &c[2], sizeof(c[0]));   /* 24 ms, the closest so far */
	T_CHECK(Has(&s, "round cap"));

	DefaultInput(&in);
	in.rounds = 7;
	T_EQ_INT(Play(&s, &in, trace, 10, c, 16), 7);
	T_EQ_MEM(OptSearch_Result(&s, &met), &c[6], sizeof(c[0]));   /* rt position 4 met, the best so far */
	T_EQ_INT(met, 1);

	/* the probe's 10 s and four trials of 10 s reach 50 s */
	DefaultInput(&in);
	in.seconds = 50.0f;
	T_EQ_INT(Play(&s, &in, trace, 10, c, 16), 4);
	T_CHECK(Has(&s, "time cap"));
	T_REQUIRE(OptSearch_Result(&s, &met) != NULL);
	T_EQ_MEM(OptSearch_Result(&s, &met), &c[3], sizeof(c[0]));

	/* a cap past what the search may hold is held */
	DefaultInput(&in);
	in.rounds = 100000;
	OptSearch_Init(&s, &in);
	T_EQ_INT(s.in.rounds, OPT_MAXROUNDS);
}

/* warm-up frames are not in a trial's statistic */
TEST(optsearch_warmup_left_out)
{
	long long t[40];
	optmeasure_t m;
	int n = 0;
	long long now = 1000000;

	/* 2 s of slow frames (100 ms), then 20 frames of 10 ms */
	t[n++] = now;
	for (int i = 0; i < 20; i++)
	{
		now += 100000;
		t[n++] = now;
	}
	for (int i = 0; i < 19; i++)
	{
		now += 10000;
		t[n++] = now;
	}
	T_EQ_INT(OptSearch_Stats(t, n, 2.0f, 95, &m), 1);
	T_EQ_FLT(m.p95_ms, 10.0f);
	T_EQ_FLT(m.mean_ms, 10.0f);
	T_EQ_FLT(m.stat_ms, 10.0f);
	T_EQ_INT(OptSearch_Stats(t, n, 0.0f, OPT_STAT_MEAN, &m), 1);
	T_CHECK(m.mean_ms > 50.0f);
	T_EQ_FLT(m.p95_ms, 100.0f);
	T_EQ_INT(OptSearch_Stats(t, n, 60.0f, 95, &m), 0);   /* nothing left after the warm-up */
	T_EQ_INT(OptSearch_Stats(t, 1, 0.0f, 95, &m), 0);
}

/* a priority whose capability is false is never tried, and the report says why */
TEST(optsearch_false_capability_never_tried)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[16];
	int n;

	DefaultInput(&in);
	in.rt_available = 0;
	in.metalfx_modes = 1;
	in.hdr_why = "this display reports no extended range";
	n = Play(&s, &in, trace, 10, c, 16);
	T_EQ_INT(n, 1);   /* nothing to turn: one trial */
	for (int cv = 0; cv < OPT_NCV; cv++)
	{
		T_EQ_INT(c[0].set[cv], 0);
	}
	T_CHECK(Has(&s, "rt: not tried: ray tracing is not available on this device"));
	T_CHECK(Has(&s, "resolution: not tried: no MetalFX scaler on this device"));
	T_CHECK(Has(&s, "upscale: not tried: no MetalFX scaler on this device"));
	T_CHECK(Has(&s, "denoise: not tried"));
	T_CHECK(Has(&s, "hdr: not tried: this display reports no extended range"));
}

/* under each presentation the rows of its table are left untried, "set by the presentation" */
TEST(optsearch_presentation_rows_untried)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[16];

	DefaultInput(&in);
	in.presentation = OPT_PRES_ENHANCED;
	Play(&s, &in, trace, 10, c, 16);
	T_EQ_INT(s.stepset[OPT_RT][OPT_CV_SHADOWS] | s.stepset[OPT_RT][OPT_CV_SUN] |
		s.stepset[OPT_RT][OPT_CV_AO] | s.stepset[OPT_RT][OPT_CV_WATER], 0);
	T_EQ_INT(s.stepset[OPT_RT][OPT_CV_GUNSHADOW], 1);
	T_CHECK(Has(&s, "r_rt_ao: set by the presentation"));

	DefaultInput(&in);
	in.presentation = OPT_PRES_RETRO;
	Play(&s, &in, trace, 10, c, 16);
	T_EQ_INT(s.nsteps[OPT_RT], 0);
	T_EQ_INT(s.nsteps[OPT_UPSCALE], 0);
	T_EQ_INT(s.nsteps[OPT_DENOISE], 0);
	T_EQ_INT(c[0].set[OPT_CV_METALFX], 0);
	T_CHECK(Has(&s, "rt: not tried: set by the presentation"));

	DefaultInput(&in);
	in.presentation = OPT_PRES_RTPRIMARY;
	Play(&s, &in, trace, 10, c, 16);
	T_EQ_INT(s.stepset[OPT_RT][OPT_CV_SHADOWS] | s.stepset[OPT_RT][OPT_CV_SUN] |
		s.stepset[OPT_RT][OPT_CV_AO] | s.stepset[OPT_RT][OPT_CV_WATER], 0);
	T_EQ_INT(s.stepset[OPT_RT][OPT_CV_AO_RAYS], 0);   /* ao is off under Ray traced: its cost is not a step */
	T_EQ_INT(s.stepset[OPT_RT][OPT_CV_WATER_HITS], 1);
	T_EQ_INT(s.stepset[OPT_RT][OPT_CV_GUNSHADOW], 1);
	T_CHECK(Has(&s, "r_rt_shadows: set by the presentation"));
	T_CHECK(Has(&s, "r_rt_water: set by the presentation"));
}

/* a candidate whose coupled values equal an earlier one is looked up, not timed */
TEST(optsearch_coupled_lookup)
{
	optsearch_t s;
	optinput_t in;
	optcand_t c[16];
	const float stats[] = { 30.0f, 25.0f, 10.0f, 12.0f, 20.0f };
	int n;

	DefaultInput(&in);
	in.nrank = 2;
	in.rank[0] = OPT_RESOLUTION;
	in.rank[1] = OPT_UPSCALE;
	in.target_ms = 15.0f;
	n = Play(&s, &in, stats, 5, c, 16);
	/* 1 first (1, 2); 2 upscale 0 (1, 0); 3 resolution 0.34 (0.34, forced 1); 4 bisect 0.75 (0.75, 1);
	   buy back upscale to 1: (0.75, 1) again, looked up; then to 2: (0.75, 2), timed */
	T_EQ_INT(n, 5);
	T_EQ_FLT(c[2].val[OPT_CV_METALFX], 1.0f);
	T_EQ_FLT(c[3].val[OPT_CV_SCALE], 0.75f);
	T_EQ_FLT(c[4].val[OPT_CV_SCALE], 0.75f);
	T_EQ_FLT(c[4].val[OPT_CV_METALFX], 2.0f);
	T_CHECK(Has(&s, "looked up"));
	for (int i = 0; i < n; i++)
	{
		for (int j = 0; j < i; j++)
		{
			T_CHECK(memcmp(c[i].val, c[j].val, sizeof(c[i].val)) != 0);
		}
	}
}

TEST(optsearch_parse_inputs)
{
	int rank[OPT_NPRI], n = 0, stat = 0;
	char err[256];
	float ms = 0;

	T_EQ_INT(OptSearch_ParseRank("rt resolution hdr upscale denoise presentation", rank, &n, err, sizeof(err)), 1);
	T_EQ_INT(n, 6);
	T_EQ_INT(rank[0], OPT_RT);
	T_EQ_INT(rank[5], OPT_PRESENTATION);
	T_EQ_INT(OptSearch_ParseRank("  hdr   rt ", rank, &n, err, sizeof(err)), 1);
	T_EQ_INT(n, 2);
	T_EQ_INT(OptSearch_ParseRank("rt rt", rank, &n, err, sizeof(err)), 0);
	T_CHECK(strstr(err, "twice") != NULL);
	T_EQ_INT(OptSearch_ParseRank("rt speed", rank, &n, err, sizeof(err)), 0);
	T_CHECK(strstr(err, "resolution rt hdr upscale denoise presentation") != NULL);
	T_EQ_INT(OptSearch_ParseRank("rt rt rt rt rt rt rt rt", rank, &n, err, sizeof(err)), 0);

	T_EQ_INT(OptSearch_ParseTarget("60", &ms), 1);
	T_EQ_FLT(ms, 1000.0f / 60.0f);
	T_EQ_INT(OptSearch_ParseTarget("12ms", &ms), 1);
	T_EQ_FLT(ms, 12.0f);
	T_EQ_INT(OptSearch_ParseTarget("0", &ms), 0);
	T_EQ_INT(OptSearch_ParseTarget("fast", &ms), 0);
	T_EQ_INT(OptSearch_ParseTarget("12 ms", &ms), 0);
	T_EQ_INT(OptSearch_ParseTarget("-5", &ms), 0);

	T_EQ_INT(OptSearch_ParseStat("p95", &stat), 1);
	T_EQ_INT(stat, 95);
	T_EQ_INT(OptSearch_ParseStat("mean", &stat), 1);
	T_EQ_INT(stat, OPT_STAT_MEAN);
	T_EQ_INT(OptSearch_ParseStat("p100", &stat), 0);
	T_EQ_INT(OptSearch_ParseStat("p49", &stat), 0);
	T_EQ_INT(OptSearch_ParseStat("p9x", &stat), 0);
}
