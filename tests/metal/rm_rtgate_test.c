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
	CHECK(fmt_is(UPTO(9), 1, 1, "ref_metal: ray tracing: render-stage path (apple9, supportsRaytracingFromRender)"));

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

	/* the effect switches: the three passes that trace rays (rm_rtgate.c gives each row's reason) */
	effects(buf, sizeof(buf));
	CHECK(!strcmp(buf, "r_rt_ao r_rt_shadows r_rt_water"));

	/* the same switches one by one, as the glue takes them; none before the first or past the last */
	CHECK(rm_rtg_effect(0) && !strcmp(rm_rtg_effect(0), "r_rt_ao"));
	CHECK(rm_rtg_effect(1) && !strcmp(rm_rtg_effect(1), "r_rt_shadows"));
	CHECK(rm_rtg_effect(2) && !strcmp(rm_rtg_effect(2), "r_rt_water"));
	CHECK(rm_rtg_effect(3) == NULL);
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

int
main(int argc, char **argv)
{
	if ((argc > 1) && !strcmp(argv[1], "--names"))
	{
		return check_names(argc, argv);
	}

	test_decide();
	test_traces();
	test_format();
	test_table();
	test_step();

	printf("rm_rtgate_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
