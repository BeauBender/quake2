/*
 * The host test of ref_metal's hardware gate for ray tracing (RT8,
 * TASK60): rm_rtgate.c, the pure part (header/rm_rtgate.h). Built and run
 * by tests/check-ref-metal.sh with the system compiler, ASAN and UBSAN
 * where they exist; no Metal, no SDL, no game data.
 *
 * With no argument it runs the cases: the path of every device class and
 * of each edge, the report lines, the table, and the per-frame step over a
 * sequence of calls, one assertion a case. With --names it checks the
 * cvar table against the names given, each "name=default" as a
 * Cvar_Get("r_rt_ of src/client/refresh/metal creates it: a name without
 * a row (unclassified), a row without a name (stale), and an effect
 * switch whose default is not "0" each fail.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtgate.h"
#include "../../src/client/refresh/metal/common/mtl_retro_math.h"

static int checks, failures;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

/* the families of apple1 to apple n */
#define UPTO(n) ((1u << (n)) - 1u)

static int
is(rm_rtg_t g, int path, int why, int family)
{
	return (g.path == path) && (g.why == why) && (g.family == family);
}

/* the device classes, and every edge with the path it takes */
static void
test_decide(void)
{
	/* this VM: apple1 to apple5, no ray tracing (the plan anchor's probe) */
	CHECK(is(rm_rtg_decide(UPTO(5), 0, 0), RM_RTG_NONE, RM_RTG_WHY_NORT, 5));
	/* M1 (apple7) and M2 (apple8): the compute path */
	CHECK(is(rm_rtg_decide(UPTO(7), 1, 0), RM_RTG_COMPUTE, RM_RTG_WHY_OK, 7));
	CHECK(is(rm_rtg_decide(UPTO(8), 1, 0), RM_RTG_COMPUTE, RM_RTG_WHY_OK, 8));
	/* M3 and later (apple9 with both): render-stage */
	CHECK(is(rm_rtg_decide(UPTO(9), 1, 1), RM_RTG_RENDER, RM_RTG_WHY_OK, 9));

	/* edge: apple5 with ray tracing reported: none, the family is below apple6 */
	CHECK(is(rm_rtg_decide(UPTO(5), 1, 0), RM_RTG_NONE, RM_RTG_WHY_FAMILY, 5));
	CHECK(is(rm_rtg_decide(UPTO(5), 1, 1), RM_RTG_NONE, RM_RTG_WHY_FAMILY, 5));
	/* edge: apple6 without ray tracing: none */
	CHECK(is(rm_rtg_decide(UPTO(6), 0, 0), RM_RTG_NONE, RM_RTG_WHY_NORT, 6));
	/* apple6 with it: compute, the first family that has it */
	CHECK(is(rm_rtg_decide(UPTO(6), 1, 0), RM_RTG_COMPUTE, RM_RTG_WHY_OK, 6));
	/* edge: apple9 without render-stage: compute */
	CHECK(is(rm_rtg_decide(UPTO(9), 1, 0), RM_RTG_COMPUTE, RM_RTG_WHY_OK, 9));
	/* edge: apple8 with render-stage reported: compute, render-stage is apple9's */
	CHECK(is(rm_rtg_decide(UPTO(8), 1, 1), RM_RTG_COMPUTE, RM_RTG_WHY_OK, 8));
	/* edge: apple9 with render-stage but no supportsRaytracing: none, nothing to trace */
	CHECK(is(rm_rtg_decide(UPTO(9), 0, 1), RM_RTG_NONE, RM_RTG_WHY_NORT, 9));
	/* edge: no family at all (the simulators): none, whatever the flags */
	CHECK(is(rm_rtg_decide(0, 1, 1), RM_RTG_NONE, RM_RTG_WHY_NOFAMILY, 0));
	CHECK(is(rm_rtg_decide(0, 0, 0), RM_RTG_NONE, RM_RTG_WHY_NOFAMILY, 0));
	/* edge: bits past apple9 are no family */
	CHECK(is(rm_rtg_decide((1u << 9) | (1u << 31), 1, 1), RM_RTG_NONE, RM_RTG_WHY_NOFAMILY, 0));
	/* the highest family counts, not the lowest: apple9 alone, apple1 and apple9 */
	CHECK(is(rm_rtg_decide(1u << 8, 1, 1), RM_RTG_RENDER, RM_RTG_WHY_OK, 9));
	CHECK(is(rm_rtg_decide(1u | (1u << 8), 1, 0), RM_RTG_COMPUTE, RM_RTG_WHY_OK, 9));
	CHECK(is(rm_rtg_decide(1u, 0, 0), RM_RTG_NONE, RM_RTG_WHY_NORT, 1));
	/* any value but 0 is yes */
	CHECK(is(rm_rtg_decide(UPTO(9), 2, -1), RM_RTG_RENDER, RM_RTG_WHY_OK, 9));
}

static void
test_traces(void)
{
	rm_rtg_t g;

	g = rm_rtg_decide(UPTO(5), 0, 0);
	CHECK(rm_rtg_traces(&g) == 0);
	g = rm_rtg_decide(UPTO(7), 1, 0);
	CHECK(rm_rtg_traces(&g) == 1);
	g = rm_rtg_decide(UPTO(9), 1, 1);
	CHECK(rm_rtg_traces(&g) == 1);
}

static int
fmt_is(unsigned families, int rt, int rr, const char *want)
{
	char buf[256];
	rm_rtg_t g = rm_rtg_decide(families, rt, rr);

	return rm_rtg_format(&g, buf, sizeof(buf)) && !strcmp(buf, want);
}

static void
test_format(void)
{
	char buf[256];
	rm_rtg_t g;

	CHECK(fmt_is(UPTO(5), 0, 0, "ref_metal: ray tracing: none (apple5: no supportsRaytracing); the ray-tracing effects read 0"));
	CHECK(fmt_is(UPTO(5), 1, 0,
		"ref_metal: ray tracing: none (apple5: supportsRaytracing below apple6); the ray-tracing effects read 0"));
	CHECK(fmt_is(0, 1, 1, "ref_metal: ray tracing: none (no Apple GPU family reported); the ray-tracing effects read 0"));
	CHECK(fmt_is(UPTO(7), 1, 0, "ref_metal: ray tracing: compute path (apple7, supportsRaytracing)"));
	/* apple9 with both flags: the gate's answer is render-stage, but its code (TASK25) is not built,
	   so the line names the path that runs, compute, and that render-stage is available */
	CHECK(fmt_is(UPTO(9), 1, 1,
		"ref_metal: ray tracing: compute path (apple9, supportsRaytracing; render-stage available, not yet built)"));
	CHECK(fmt_is(UPTO(9), 1, 0, "ref_metal: ray tracing: compute path (apple9, supportsRaytracing)"));

	CHECK(!strcmp(rm_rtg_why_text(RM_RTG_WHY_OK), ""));
	CHECK(!strcmp(rm_rtg_why_text(99), ""));
	CHECK(!strcmp(rm_rtg_why_text(-1), ""));

	g = rm_rtg_decide(UPTO(5), 0, 0);
	CHECK(rm_rtg_reset_format("r_rt_ao", &g, buf, sizeof(buf)) &&
		!strcmp(buf, "ref_metal: r_rt_ao reads 0: no ray tracing on this device (no supportsRaytracing)"));
	g = rm_rtg_decide(0, 0, 0);
	CHECK(rm_rtg_reset_format("r_rt_water", &g, buf, sizeof(buf)) &&
		!strcmp(buf, "ref_metal: r_rt_water reads 0: no ray tracing on this device (no Apple GPU family reported)"));

	/* a line that does not fit: 0 and an empty buffer; no buffer: 0 */
	g = rm_rtg_decide(UPTO(5), 0, 0);
	memset(buf, 'x', sizeof(buf));
	CHECK(rm_rtg_format(&g, buf, 20) == 0 && buf[0] == '\0');
	memset(buf, 'x', sizeof(buf));
	CHECK(rm_rtg_reset_format("r_rt_ao", &g, buf, 20) == 0 && buf[0] == '\0');
	/* no room at all: 0, and nothing written */
	memset(buf, 'x', sizeof(buf));
	CHECK(rm_rtg_format(&g, buf, 0) == 0 && buf[0] == 'x');
	CHECK(rm_rtg_reset_format("r_rt_ao", &g, buf, 0) == 0 && buf[0] == 'x');
	g = rm_rtg_decide(UPTO(7), 1, 0);
	memset(buf, 'x', sizeof(buf));
	CHECK(rm_rtg_format(&g, buf, 20) == 0 && buf[0] == '\0');
	g = rm_rtg_decide(UPTO(9), 1, 1);
	memset(buf, 'x', sizeof(buf));
	CHECK(rm_rtg_format(&g, buf, 20) == 0 && buf[0] == '\0');
	/* the exact size: the line and its terminator fit, one less does not */
	g = rm_rtg_decide(UPTO(7), 1, 0);
	CHECK(rm_rtg_format(&g, buf, strlen("ref_metal: ray tracing: compute path (apple7, supportsRaytracing)") + 1) == 1);
	CHECK(rm_rtg_format(&g, buf, strlen("ref_metal: ray tracing: compute path (apple7, supportsRaytracing)")) == 0);
}

/* the effect switches of the table, in its order, as one string */
static void
effects(char *buf, size_t len)
{
	int i, e;
	const char *n;

	buf[0] = '\0';

	for (i = 0; (n = rm_rtg_cvar(i, &e)) != NULL; i++)
	{
		if (e)
		{
			strncat(buf, buf[0] ? " " : "", len - strlen(buf) - 1);
			strncat(buf, n, len - strlen(buf) - 1);
		}
	}
}

static void
test_table(void)
{
	char buf[512];
	int i, e, sorted = 1, prefixed = 1, found = 1, neffects = 0;

	CHECK(rm_rtg_ncvars() > 0);
	CHECK(rm_rtg_cvar(-1, &e) == NULL);
	CHECK(rm_rtg_cvar(rm_rtg_ncvars(), &e) == NULL);
	CHECK(rm_rtg_cvar(0, NULL) != NULL);
	CHECK(rm_rtg_find(NULL) == -1);
	CHECK(rm_rtg_find("") == -1);
	CHECK(rm_rtg_find("r_rt_a") == -1);
	CHECK(rm_rtg_find("r_rt_aox") == -1);
	CHECK(rm_rtg_find("R_RT_AO") == -1);

	for (i = 0; i < rm_rtg_ncvars(); i++)
	{
		const char *n = rm_rtg_cvar(i, &e);

		prefixed &= (n != NULL) && !strncmp(n, "r_rt_", 5);
		found &= (rm_rtg_find(n) == i);
		sorted &= (i == 0) || (strcmp(rm_rtg_cvar(i - 1, NULL), n) < 0);
		neffects += (e != 0);
	}

	CHECK(prefixed);
	CHECK(found);
	CHECK(sorted);   /* sorted and so no name twice */
	CHECK(neffects <= RM_RTG_MAX_EFFECTS);

	/* the effect switches: the four passes that trace rays (rm_rtgate.c gives each row's reason) */
	effects(buf, sizeof(buf));
	CHECK(!strcmp(buf, "r_rt_ao r_rt_shadows r_rt_sun r_rt_water"));

	/* the same switches one by one, as the glue takes them; none before the first or past the last */
	CHECK(rm_rtg_effect(0) && !strcmp(rm_rtg_effect(0), "r_rt_ao"));
	CHECK(rm_rtg_effect(1) && !strcmp(rm_rtg_effect(1), "r_rt_shadows"));
	CHECK(rm_rtg_effect(2) && !strcmp(rm_rtg_effect(2), "r_rt_sun"));
	CHECK(rm_rtg_effect(3) && !strcmp(rm_rtg_effect(3), "r_rt_water"));
	CHECK(rm_rtg_effect(4) == NULL);
	CHECK(rm_rtg_effect(-1) == NULL);
	CHECK(rm_rtg_effect(1000) == NULL);

	/* settings, among them the raster paths every Metal GPU runs and the development views */
	rm_rtg_cvar(rm_rtg_find("r_rt_gbuffer"), &e);
	CHECK(e == 0);
	rm_rtg_cvar(rm_rtg_find("r_rt_motion"), &e);
	CHECK(e == 0);
	rm_rtg_cvar(rm_rtg_find("r_rt_stats"), &e);
	CHECK(e == 0);
	rm_rtg_cvar(rm_rtg_find("r_rt_ao_show"), &e);
	CHECK(e == 0);
	rm_rtg_cvar(rm_rtg_find("r_rt_shadows_show"), &e);
	CHECK(e == 0);
	rm_rtg_cvar(rm_rtg_find("r_rt_sun_show"), &e);
	CHECK(e == 0);
	rm_rtg_cvar(rm_rtg_find("r_rt_sun_angles"), &e);
	CHECK(e == 0);
}

/*
 * One step, judged in one assertion: the switches reset and said as bit
 * masks, each list in order with every index below n, and the return the
 * number reset.
 */
static int
step_is(int path, const float *v, int n, rm_rtg_state_t *s, unsigned want_reset, unsigned want_say)
{
	rm_rtg_out_t o;
	unsigned rm = 0, sm = 0;
	int k, bound = (n < 0) ? 0 : ((n > RM_RTG_MAX_EFFECTS) ? RM_RTG_MAX_EFFECTS : n);
	const int r = rm_rtg_step(path, v, n, s, &o);

	if ((r != o.nreset) || (o.nreset < 0) || (o.nreset > bound) || (o.nsay < 0) || (o.nsay > o.nreset))
	{
		return 0;
	}

	for (k = 0; k < o.nreset; k++)
	{
		if ((o.reset[k] < 0) || (o.reset[k] >= bound) || ((k > 0) && (o.reset[k] <= o.reset[k - 1])))
		{
			return 0;
		}

		rm |= 1u << o.reset[k];
	}

	for (k = 0; k < o.nsay; k++)
	{
		if ((o.say[k] < 0) || (o.say[k] >= bound) || ((k > 0) && (o.say[k] <= o.say[k - 1])))
		{
			return 0;
		}

		sm |= 1u << o.say[k];
	}

	return (rm == want_reset) && (sm == want_say);
}

/* the per-frame step over a sequence of calls, one assertion a case */
static void
test_step(void)
{
	rm_rtg_state_t s;
	float v[3] = { 0, 0, 0 };
	float big[RM_RTG_MAX_EFFECTS + 5];
	int i;

	rm_rtg_state_reset(&s);

	/* start-up, every switch at 0: nothing */
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 0, 0));
	/* the config or the command line set the second one: reset, with its line */
	v[1] = 1;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 2, 2));
	/* the reset took: nothing */
	v[1] = 0;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 0, 0));
	/* set at the console again: reset, with a line again */
	v[1] = 1;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 2, 2));
	/* a reset that did not take: reset again, no second line */
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 2, 0));
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 2, 0));
	/* another value while it stays: a change, so a line */
	v[1] = 2;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 2, 2));
	/* two in one frame, in order; the one back at 0 is not listed */
	v[0] = 1;
	v[1] = 0;
	v[2] = 1;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 1 | 4, 1 | 4));
	v[0] = v[2] = 0;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 0, 0));
	/* a value that is not a finite number is not 0: reset, one line while it stays */
	v[0] = NAN;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 1, 1));
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 1, 0));
	v[0] = INFINITY;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 1, 1));
	v[0] = -1;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 1, 1));
	v[0] = 0.001f;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 1, 1));
	/* -0 is 0 */
	v[0] = -0.0f;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 0, 0));

	/* with ray tracing nothing is listed, whatever the values */
	v[0] = v[1] = v[2] = 1;
	CHECK(step_is(RM_RTG_COMPUTE, v, 3, &s, 0, 0));
	CHECK(step_is(RM_RTG_RENDER, v, 3, &s, 0, 0));

	/* a fresh state after vid_restart: the switch still set is said again */
	rm_rtg_state_reset(&s);
	v[0] = v[2] = 0;
	CHECK(step_is(RM_RTG_NONE, v, 3, &s, 2, 2));

	/* n is clamped: none below 0, at most RM_RTG_MAX_EFFECTS */
	for (i = 0; i < RM_RTG_MAX_EFFECTS + 5; i++)
	{
		big[i] = 1;
	}

	rm_rtg_state_reset(&s);
	CHECK(step_is(RM_RTG_NONE, big, -1, &s, 0, 0));
	CHECK(step_is(RM_RTG_NONE, big, 0, &s, 0, 0));
	CHECK(step_is(RM_RTG_NONE, big, RM_RTG_MAX_EFFECTS + 5, &s, 0xffffu, 0xffffu));
}

/* --names name=default ...: the table against the cvars the sources create */
static int
check_names(int argc, char **argv)
{
	int seen[64], i, unclassified = 0, stale = 0, effect_default = 0;
	const int n = rm_rtg_ncvars();

	if (n > (int)(sizeof(seen) / sizeof(seen[0])))
	{
		printf("rm_rtgate_test: the table has more rows than the check holds\n");
		return 1;
	}

	memset(seen, 0, sizeof(seen));

	for (i = 2; i < argc; i++)
	{
		char name[128];
		const char *eq = strchr(argv[i], '=');
		size_t len = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
		int row, e = 0;

		if (len >= sizeof(name))
		{
			printf("rm_rtgate_test: a name too long: %s\n", argv[i]);
			unclassified++;
			continue;
		}

		memcpy(name, argv[i], len);
		name[len] = '\0';
		row = rm_rtg_find(name);

		if (row < 0)
		{
			printf("rm_rtgate_test: %s is not classified in rm_rtgate.c's table (an effect switch or a setting)\n",
					name);
			unclassified++;
			continue;
		}

		seen[row] = 1;
		rm_rtg_cvar(row, &e);

		if (e && (!eq || strcmp(eq + 1, "0")))
		{
			printf("rm_rtgate_test: the effect switch %s defaults to %s, not 0\n", name, eq ? eq + 1 : "(none)");
			effect_default++;
		}
	}

	for (i = 0; i < n; i++)
	{
		if (!seen[i])
		{
			printf("rm_rtgate_test: the table's row %s names no Cvar_Get of ref_metal\n", rm_rtg_cvar(i, NULL));
			stale++;
		}
	}

	printf("rm_rtgate_test: %d names, %d unclassified, %d stale, %d effect defaults not 0\n", argc - 2, unclassified,
			stale, effect_default);
	return (unclassified || stale || effect_default) ? 1 : 0;
}

/* TASK183: the presentation modes' choice of mode on a device */
static void
test_pres_decide(void)
{
	int why = -1;

	CHECK(rm_pres_decide(0, RM_RTG_NONE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(0, RM_RTG_COMPUTE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(1, RM_RTG_COMPUTE, &why) == RM_PRES_ENHANCED && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(1, RM_RTG_RENDER, &why) == RM_PRES_ENHANCED && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(1.75f, RM_RTG_COMPUTE, &why) == RM_PRES_ENHANCED && why == RM_PRES_WHY_OK);
	/* enhanced traces rays: stock on a device without ray tracing (the VM) */
	CHECK(rm_pres_decide(1, RM_RTG_NONE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NORT);
	/* retro and RT-primary are later slices: stock here, whatever the device */
	/* TASK184: Retro needs no ray tracing, so every path runs it */
	CHECK(rm_pres_decide(2, RM_RTG_NONE, &why) == RM_PRES_RETRO && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(2, RM_RTG_COMPUTE, &why) == RM_PRES_RETRO && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(2, RM_RTG_RENDER, &why) == RM_PRES_RETRO && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(2.5f, RM_RTG_NONE, &why) == RM_PRES_RETRO && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(2, RM_RTG_NONE, NULL) == RM_PRES_RETRO);
	CHECK(rm_pres_decide(3, RM_RTG_NONE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NOTBUILT);
	CHECK(rm_pres_decide(3, RM_RTG_RENDER, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NOTBUILT);
	CHECK(rm_pres_decide(-1, RM_RTG_COMPUTE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NOTBUILT);
	CHECK(rm_pres_decide(1e30f, RM_RTG_COMPUTE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NOTBUILT);
	CHECK(rm_pres_decide(NAN, RM_RTG_COMPUTE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NOTBUILT);
	CHECK(rm_pres_decide(1, RM_RTG_COMPUTE, NULL) == RM_PRES_ENHANCED);
}

/*
 * The overlay: stock gives the user's values, enhanced its own for the
 * four effect switches and the user's for every other setting, and going
 * back to stock gives the user's values again. The user's values are held
 * in the test's own array, which the overlay cannot write: nothing it
 * returns is stored anywhere but in the result.
 */
static void
test_pres_overlay(void)
{
	static const char *const decided[] = { "r_rt_ao", "r_rt_shadows", "r_rt_sun", "r_rt_water" };
	const float users[] = { 0.0f, 0.5f, 2.0f, -1.0f };
	int i, j, k, mode, effect;

	for (j = 0; j < (int)(sizeof(users) / sizeof(users[0])); j++)
	{
		for (i = 0; i < rm_rtg_ncvars(); i++)
		{
			const char *name = rm_rtg_cvar(i, &effect);
			int decides = 0;

			for (k = 0; k < (int)(sizeof(decided) / sizeof(decided[0])); k++)
			{
				decides |= !strcmp(name, decided[k]);
			}

			/* stock, enhanced, then stock again: the switch and its way back */
			for (mode = 0; mode < 3; mode++)
			{
				const int m = (mode == 1) ? RM_PRES_ENHANCED : RM_PRES_STOCK;
				const float want = (m == RM_PRES_ENHANCED && decides) ? 1.0f : users[j];

				CHECK(rm_pres_value(m, name, users[j]) == want);
			}

			/* the modes of later slices decide nothing in this one */
			CHECK(rm_pres_value(RM_PRES_RETRO, name, users[j]) == users[j]);
			CHECK(rm_pres_value(RM_PRES_RTPRIMARY, name, users[j]) == users[j]);
		}
	}

	/* every switch enhanced decides is one of the gate's effect switches */
	for (k = 0; k < (int)(sizeof(decided) / sizeof(decided[0])); k++)
	{
		i = rm_rtg_find(decided[k]);
		CHECK(i >= 0 && rm_rtg_cvar(i, &effect) && effect == 1);
	}

	CHECK(rm_pres_value(RM_PRES_ENHANCED, "vid_gamma", 1.2f) == 1.2f);
	CHECK(rm_pres_value(RM_PRES_ENHANCED, NULL, 3.0f) == 3.0f);
	CHECK(rm_pres_value(-1, "r_rt_ao", 0.0f) == 0.0f);
}

static int
pres_is(float requested, int mode, int why, const char *want)
{
	char buf[256];

	return rm_pres_format(requested, mode, why, buf, sizeof(buf)) && !strcmp(buf, want);
}

static void
test_pres_format(void)
{
	char small[8];

	CHECK(!strcmp(rm_pres_name(RM_PRES_STOCK), "stock"));
	CHECK(!strcmp(rm_pres_name(RM_PRES_ENHANCED), "enhanced"));
	CHECK(!strcmp(rm_pres_name(RM_PRES_RETRO), "retro"));
	CHECK(!strcmp(rm_pres_name(RM_PRES_RTPRIMARY), "ray traced"));
	CHECK(!strcmp(rm_pres_name(4), "") && !strcmp(rm_pres_name(-1), ""));
	CHECK(pres_is(0, RM_PRES_STOCK, RM_PRES_WHY_OK, "ref_metal: presentation: stock (r_presentation 0)"));
	CHECK(pres_is(1, RM_PRES_ENHANCED, RM_PRES_WHY_OK,
			"ref_metal: presentation: enhanced (r_presentation 1): r_rt_shadows, r_rt_sun, r_rt_ao and r_rt_water read 1, the user's values kept"));
	CHECK(pres_is(1, RM_PRES_STOCK, RM_PRES_WHY_NORT,
			"ref_metal: presentation: stock; r_presentation 1 (enhanced) reads 0: no ray tracing on this device"));
	CHECK(pres_is(2, RM_PRES_STOCK, RM_PRES_WHY_NOTBUILT,
			"ref_metal: presentation: stock; r_presentation 2 (retro) reads 0: not in this build"));
	CHECK(pres_is(2, RM_PRES_RETRO, RM_PRES_WHY_OK,
			"ref_metal: presentation: retro (r_presentation 2): the 3D view in RGB565 with the 4 x 4 dither, r_retro_filter and r_retro_gamma read each frame"));
	CHECK(pres_is(7, RM_PRES_STOCK, RM_PRES_WHY_NOTBUILT,
			"ref_metal: presentation: stock; r_presentation 7 reads 0: not in this build"));
	CHECK(!rm_pres_format(1, RM_PRES_ENHANCED, RM_PRES_WHY_OK, small, sizeof(small)) && small[0] == '\0');
	CHECK(!rm_pres_format(1, RM_PRES_ENHANCED, RM_PRES_WHY_OK, small, 0));
}

/* TASK195: the A/B overlay gives the B value on B, the user's (0) on A, and never a near name */
static void
test_ab_overlay(void)
{
	const char *b = "B: stock|r_rt_ao=0;r_rt_ao_range=128;r_rt_sun_angles=55 45;";
	char v[16], line[128];

	CHECK(!rm_ab_value("", "r_rt_ao", v, sizeof(v)));
	CHECK(!rm_ab_value(NULL, "r_rt_ao", v, sizeof(v)));
	CHECK(rm_ab_value(b, "r_rt_ao", v, sizeof(v)) && !strcmp(v, "0"));
	CHECK(rm_ab_value(b, "r_rt_ao_range", v, sizeof(v)) && !strcmp(v, "128"));
	CHECK(rm_ab_value(b, "r_rt_sun_angles", v, sizeof(v)) && !strcmp(v, "55 45"));
	CHECK(!rm_ab_value(b, "r_rt_ao_ran", v, sizeof(v)));
	CHECK(!rm_ab_value(b, "r_rt_sun", v, sizeof(v)));
	CHECK(!rm_ab_value(b, "r_rt_sun_angles", v, 4));   /* does not fit: the user's stands */
	CHECK(!rm_ab_value("B: x|broken", "broken", v, sizeof(v)));

	CHECK(rm_ab_format("", line, sizeof(line)) && !strcmp(line, "ref_metal: A/B: A: your settings"));
	CHECK(rm_ab_format(b, line, sizeof(line)) &&
		!strcmp(line, "ref_metal: A/B: B: stock (3 settings overlaid, no cvar written)"));
}

/* TASK195: a value is the user's own unless the presentation decides it or the B side holds it */
static void
test_effect_overlaid(void)
{
	const char *b = "B: stock|r_metalfx=3;r_rt_ao_range=128;";

	CHECK(!rm_effect_overlaid(RM_PRES_STOCK, "", "r_metalfx"));
	CHECK(!rm_effect_overlaid(RM_PRES_STOCK, NULL, "r_metalfx"));
	CHECK(rm_effect_overlaid(RM_PRES_STOCK, b, "r_metalfx"));
	CHECK(!rm_effect_overlaid(RM_PRES_STOCK, b, "r_metalfx_interp"));
	CHECK(!rm_effect_overlaid(RM_PRES_ENHANCED, "", "r_metalfx"));   /* not a switch the mode decides */
	CHECK(rm_effect_overlaid(RM_PRES_ENHANCED, "", "r_rt_ao"));
	CHECK(!rm_effect_overlaid(RM_PRES_STOCK, "", "r_rt_ao"));
	CHECK(!rm_effect_overlaid(RM_PRES_STOCK, b, NULL));
}

/*
 * TASK184: the Retro post-process's arithmetic (common/mtl_retro_math.h),
 * the CPU reference of rm_fragment_retro on known pixels.
 */
static void
test_retro_math(void)
{
	static const unsigned bayer[4][4] = {
		{ 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 }
	};
	unsigned x, y, c, r, bits;
	int seen[16] = { 0 }, all = 1;

	/* the 4 x 4 Bayer matrix, each rank once, repeating every 4 pixels */
	for (y = 0; y < 8; y++)
	{
		for (x = 0; x < 8; x++)
		{
			all &= mtl_retro_bayer(x, y) == bayer[y & 3][x & 3];
			all &= mtl_retro_bayer(x + 1024, y + 4096) == bayer[y & 3][x & 3];
			seen[mtl_retro_bayer(x, y)] = 1;
		}
	}

	CHECK(all);

	for (r = 0; r < 16; r++)
	{
		CHECK(seen[r]);
	}

	/* known pixels: black and white stay at every rank; mid grey splits between two levels */
	for (r = 0; r < 16; r++)
	{
		CHECK(mtl_retro_quant(0, 5, r) == 0 && mtl_retro_quant(0, 6, r) == 0);
		CHECK(mtl_retro_quant(255, 5, r) == 255 && mtl_retro_quant(255, 6, r) == 255);
	}

	CHECK(mtl_retro_quant(128, 5, 0) == 123);    /* level 15 of 31 */
	CHECK(mtl_retro_quant(128, 5, 15) == 132);   /* level 16 */
	CHECK(mtl_retro_quant(128, 6, 0) == 125);    /* level 31 of 63 */
	CHECK(mtl_retro_quant(128, 6, 15) == 130);   /* level 32 */
	CHECK(mtl_retro_quant(300, 5, 0) == 255);    /* a value above a byte is clamped */

	/*
	 * Every byte at every rank is one of the levels' bytes, the result is
	 * monotonic in the byte and in the rank, and the 16 ranks average to
	 * the byte within 1: the dither keeps the mean.
	 */
	for (bits = 5; bits <= 6; bits++)
	{
		unsigned l = (1u << bits) - 1u;
		int levels = 1, mono = 1, mean = 1;

		for (c = 0; c < 256; c++)
		{
			unsigned sum = 0;

			for (r = 0; r < 16; r++)
			{
				unsigned q = mtl_retro_quant(c, bits, r);
				unsigned k = (q * l + 127u) / 255u;

				levels &= ((k * 255u + l / 2u) / l) == q;
				mono &= (c == 0) || (q >= mtl_retro_quant(c - 1, bits, r));
				mono &= (r == 0) || (q >= mtl_retro_quant(c, bits, r - 1));
				sum += q;
			}

			mean &= fabs(sum / 16.0 - (double)c) <= 1.0;
		}

		CHECK(levels);
		CHECK(mono);
		CHECK(mean);
	}

	/* the box filter and the curves */
	CHECK(mtl_retro_box(0, 0, 0, 255) == 64);
	CHECK(mtl_retro_box(123, 132, 132, 123) == 128);
	CHECK(mtl_retro_box(255, 255, 255, 255) == 255);
	CHECK(mtl_retro_gamma(0) == 1.0f);
	CHECK(fabsf(mtl_retro_gamma(1) - 1.0f / 1.15f) < 1e-6f);
	CHECK(fabsf(mtl_retro_gamma(2) - 1.0f / 1.3f) < 1e-6f);
	CHECK(mtl_retro_gamma(3) == 1.0f && mtl_retro_gamma(-1) == 1.0f);
	CHECK(MTL_RETRO_CURVES == 3);
}

int
main(int argc, char **argv)
{
	if ((argc > 1) && !strcmp(argv[1], "--names"))
	{
		return check_names(argc, argv);
	}

	test_pres_decide();
	test_pres_overlay();
	test_pres_format();
	test_ab_overlay();
	test_effect_overlaid();
	test_retro_math();

	test_decide();
	test_traces();
	test_format();
	test_table();
	test_step();

	printf("rm_rtgate_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
