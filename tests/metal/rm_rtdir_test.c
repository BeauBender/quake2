/*
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The host test of the RT-primary presentation's direct light (TASK188,
 * PM6 slice 1): rm_rtdir.c and common/mtl_rtdir_math.h, with no SDL, no
 * Metal and no game data. The mode's switch (RT-primary only), qrad3's
 * falloff and cone, what a shadow ray does at a face, the light the
 * resolve reads and its clamps, the kernel's text for the no-lightmap rule
 * with seven mutants it must refuse (test_no_lightmap_kernel), the fit's
 * samples, the fit itself and the frame it is taken from, the fixed
 * exposure, the lights a pixel takes past the cap, the lights' packing
 * and its intensity clamp, the style table, the view and the lines. Run
 * by tests/check-ref-metal.sh from the repository's top directory.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtdir.h"
#include "../../src/client/refresh/metal/header/rm_rtgate.h"

/* src/common/shared/shared.c, linked for rm_pure.c, prints through the engine */
void
Com_Printf(const char *msg, ...)
{
	(void)msg;
}

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

static int
near(double a, double b, double eps)
{
	return fabs(a - b) <= eps;
}

/* the term runs under RT-primary only: stock, enhanced and retro never trace it, whatever r_rt_direct reads */
static void
test_on(void)
{
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 1, 0) == 1);
	CHECK(rm_rtdir_on(RM_PRES_STOCK, 1, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_ENHANCED, 1, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RETRO, 1, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 0, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, NAN, 1, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 0, 0) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 1, 1) == 0);
	CHECK(rm_rtdir_on(RM_PRES_RTPRIMARY, 1, 1, NAN) == 0);

	CHECK(rm_rtdir_show(0) == 0);
	CHECK(rm_rtdir_show(1) == RM_RTDIR_SHOW_DIRECT);
	CHECK(rm_rtdir_show(2.9f) == RM_RTDIR_SHOW_VIS);
	CHECK(rm_rtdir_show(3) == RM_RTDIR_SHOW_NOSHADOW);
	CHECK(rm_rtdir_show(99) == RM_RTDIR_SHOW_MAX);
	CHECK(rm_rtdir_show(-1) == 0);
	CHECK(rm_rtdir_show(NAN) == 0);
	CHECK(rm_rtdir_show(INFINITY) == 0);
}

/* qrad3's GatherSampleLight: (intensity - dist) x dot, nothing at N.L 0.001 or below, nothing outside the cone */
static void
test_scale(void)
{
	CHECK(near(mtl_rtdir_scale(300, 100, 1, 1, MTL_RTDIR_NOSPOT), 200, 1e-4));
	CHECK(near(mtl_rtdir_scale(300, 100, 0.5f, 1, MTL_RTDIR_NOSPOT), 100, 1e-4));
	CHECK(mtl_rtdir_scale(300, 300, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 400, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, MTL_RTDIR_MIN_DOT, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, 0.0011f, 1, MTL_RTDIR_NOSPOT) > 0);
	CHECK(mtl_rtdir_scale(300, 100, -1, 1, MTL_RTDIR_NOSPOT) == 0);
	/* a point light at any direction: -1 is still above NOSPOT */
	CHECK(mtl_rtdir_scale(300, 100, 1, -1, MTL_RTDIR_NOSPOT) > 0);
	/* a 10-degree cone: inside at 5 degrees, outside at 15 and on the edge */
	CHECK(mtl_rtdir_scale(300, 100, 1, (float)cos(5 * M_PI / 180), (float)cos(10 * M_PI / 180)) > 0);
	CHECK(mtl_rtdir_scale(300, 100, 1, (float)cos(15 * M_PI / 180), (float)cos(10 * M_PI / 180)) == 0);
	CHECK(mtl_rtdir_scale(300, 100, 1, 0.5f, 0.5f) == 0);
	CHECK(mtl_rtdir_scale(NAN, 100, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, NAN, 1, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, NAN, 1, MTL_RTDIR_NOSPOT) == 0);
	CHECK(mtl_rtdir_scale(300, 100, 1, NAN, MTL_RTDIR_NOSPOT) == 0);
}

/* a shadow ray passes water and translucent faces, the first MTL_RTDIR_LAYERS of them; anything else shadows */
static void
test_at(void)
{
	unsigned m;

	CHECK(mtl_rtdir_at(3, 0) == MTL_RTDIR_AT_PASS);
	CHECK(mtl_rtdir_at(5, 0) == MTL_RTDIR_AT_PASS);
	CHECK(mtl_rtdir_at(3, MTL_RTDIR_LAYERS - 1) == MTL_RTDIR_AT_PASS);
	CHECK(mtl_rtdir_at(3, MTL_RTDIR_LAYERS) == MTL_RTDIR_AT_SHADOW);
	CHECK(mtl_rtdir_at(5, MTL_RTDIR_LAYERS) == MTL_RTDIR_AT_SHADOW);

	for (m = 0; m < 256; m++)
	{
		if ((m != 3) && (m != 5))
		{
			CHECK(mtl_rtdir_at(m, 0) == MTL_RTDIR_AT_SHADOW);
		}
	}
}

/* the light the resolve reads: raw x exposure, at most 1 a channel, 0 for NaN, alpha 1 */
static void
test_light(void)
{
	mtl_rgba_t c = mtl_rtdir_light(0.25f, 0.5f, 0.125f, 2);

	CHECK(near(c.r, 0.5, 1e-6) && near(c.g, 1.0, 1e-6) && near(c.b, 0.25, 1e-6) && (c.a == 1.0f));
	c = mtl_rtdir_light(0.75f, 3, -1, 2);
	CHECK((c.r == 1.0f) && (c.g == 1.0f) && (c.b == 0.0f));
	c = mtl_rtdir_light(NAN, 0.5f, 0.5f, 1);
	CHECK((c.r == 0.0f) && near(c.g, 0.5, 1e-6));
	c = mtl_rtdir_light(0.5f, 0.5f, 0.5f, NAN);
	CHECK((c.r == 0.0f) && (c.g == 0.0f) && (c.b == 0.0f) && (c.a == 1.0f));
	c = mtl_rtdir_light(0, 0, 0, 16);
	CHECK((c.r == 0.0f) && (c.g == 0.0f) && (c.b == 0.0f));
}

/* the occurrences of needle in text[0, len) */
static int
count_in(const char *text, size_t len, const char *needle)
{
	const size_t nl = strlen(needle);
	size_t i;
	int n = 0;

	for (i = 0; nl && (i + nl <= len); i++)
	{
		if (!memcmp(text + i, needle, nl))
		{
			n++;
			i += nl - 1;
		}
	}

	return n;
}

static int
ident_char(char c)
{
	return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_');
}

/* the occurrences of the identifier word in text[0, len), as a whole word */
static int
words_in(const char *text, size_t len, const char *word)
{
	const size_t wl = strlen(word);
	size_t i;
	int n = 0;

	for (i = 0; wl && (i + wl <= len); i++)
	{
		if (!memcmp(text + i, word, wl) && ((i == 0) || !ident_char(text[i - 1])) &&
			((i + wl == len) || !ident_char(text[i + wl])))
		{
			n++;
			i += wl - 1;
		}
	}

	return n;
}

/* the text with its comments taken out (no string literal in the kernel's file holds a comment mark) */
static void
strip_comments(const char *in, char *out, size_t cap)
{
	size_t o = 0;

	while (*in && (o + 1 < cap))
	{
		if ((in[0] == '/') && (in[1] == '*'))
		{
			const char *e = strstr(in + 2, "*/");

			in = e ? e + 2 : in + strlen(in);
		}
		else if ((in[0] == '/') && (in[1] == '/'))
		{
			while (*in && (*in != '\n'))
			{
				in++;
			}
		}
		else
		{
			out[o++] = *in++;
		}
	}

	out[o] = 0;
}

/* a function's parameter list and body in text, found by the head of its definition */
typedef struct
{
	const char *params;
	size_t nparams;
	const char *body;
	size_t nbody;
} fn_t;

/* the span from the opening bracket at p to its match; 0 when it does not close */
static size_t
matched(const char *p, char open, char close)
{
	size_t i;
	int depth = 0;

	for (i = 0; p[i]; i++)
	{
		if (p[i] == open)
		{
			depth++;
		}
		else if ((p[i] == close) && (--depth == 0))
		{
			return i + 1;
		}
	}

	return 0;
}

/* 1 when the definition headed head (its return type, a newline, its name and "(") is in text exactly once */
static int
find_fn(const char *text, const char *head, fn_t *f)
{
	const char *p = strstr(text, head), *b;

	memset(f, 0, sizeof(*f));

	if (!p || strstr(p + 1, head))
	{
		return 0;
	}

	p += strlen(head) - 1;
	f->params = p;
	f->nparams = matched(p, '(', ')');
	b = f->nparams ? strchr(p + f->nparams, '{') : NULL;

	if (!b)
	{
		return 0;
	}

	f->body = b;
	f->nbody = matched(b, '{', '}');
	return f->nbody != 0;
}

#define KERNEL_WORLD_BRANCH \
	"\t\tif (world)\n" \
	"\t\t{\n" \
	"\t\t\tlight_out.write(rm_rtdir_out(raw, v.exposure), px);\n" \
	"\t\t\tvis_out.write(rm_rtdir_vis(v.show, t), px);\n" \
	"\t\t}\n" \
	"\t\telse\n" \
	"\t\t{\n" \
	"\t\t\tlight_out.write(base.read(px), px);\n" \
	"\t\t\tvis_out.write(float4(0.0f), px);\n" \
	"\t\t}\n"

#define KERNEL_FIT_SAMPLE \
	"{\n" \
	"\tconst float4 lms = lm.read(px);\n" \
	"\n" \
	"\treturn uint2(mtl_rtdir_q(lms.r, lms.g, lms.b), mtl_rtdir_q(raw.r, raw.g, raw.b));\n" \
	"}"

#define KERNEL_OUT \
	"{\n" \
	"\tconst mtl_rgba_t c = mtl_rtdir_light(raw.r, raw.g, raw.b, exposure);\n" \
	"\n" \
	"\treturn float4(c.r, c.g, c.b, c.a);\n" \
	"}"

/*
 * The plan's section 6, item 5, in the kernel's text (src) with its
 * comments taken out: no world pixel's light reads the lightmap. The
 * traced light comes from rm_rtdir_direct, which takes no texture; it is
 * held in a const raw that the kernel names three times (its definition,
 * the fit's sample, the world's write); the lightmap texture lm is read
 * once, in rm_rtdir_fit_sample, which takes raw by value and returns the
 * two quantized values the frame's sums take; the world branch writes
 * rm_rtdir_out of raw and the exposure, and only the other branch writes
 * base. NULL when it holds, else the first rule broken.
 */
static const char *
kernel_broken(const char *src)
{
	static char text[65536];
	fn_t k, dir, fit, out;

	strip_comments(src, text, sizeof(text));

	if (!find_fn(text, "kernel void\nrm_rtdir_trace(", &k))
	{
		return "the kernel rm_rtdir_trace is not defined once";
	}

	if (!find_fn(text, "static float3\nrm_rtdir_direct(", &dir))
	{
		return "rm_rtdir_direct is not defined once";
	}

	if (!find_fn(text, "static uint2\nrm_rtdir_fit_sample(", &fit))
	{
		return "rm_rtdir_fit_sample is not defined once";
	}

	if (!find_fn(text, "static float4\nrm_rtdir_out(", &out))
	{
		return "rm_rtdir_out is not defined once";
	}

	/* the traced light: no texture in reach */
	if (count_in(dir.params, dir.nparams, "texture") || count_in(dir.body, dir.nbody, ".read(") ||
		words_in(dir.body, dir.nbody, "lm") || words_in(dir.body, dir.nbody, "base") ||
		words_in(dir.body, dir.nbody, "fit"))
	{
		return "rm_rtdir_direct reaches a texture or the fit";
	}

	/* the lightmap: the kernel's parameter, its one use (the fit's call), the fit's parameter and its read */
	if ((words_in(text, strlen(text), "lm") != 4) || (count_in(text, strlen(text), "lm.read(") != 1) ||
		(words_in(k.params, k.nparams, "lm") != 1) || (words_in(k.body, k.nbody, "lm") != 1) ||
		(count_in(k.body, k.nbody, "rm_rtdir_fit_sample(lm, px, raw)") != 1))
	{
		return "the lightmap is named outside the fit's sample";
	}

	if (strncmp(fit.params, "(texture2d<float, access::read> lm, uint2 px, float3 raw)", fit.nparams) ||
		(fit.nbody != strlen(KERNEL_FIT_SAMPLE)) || strncmp(fit.body, KERNEL_FIT_SAMPLE, fit.nbody))
	{
		return "rm_rtdir_fit_sample is not the fit's sample alone";
	}

	if (strncmp(out.params, "(float3 raw, float exposure)", out.nparams) || (out.nbody != strlen(KERNEL_OUT)) ||
		strncmp(out.body, KERNEL_OUT, out.nbody))
	{
		return "rm_rtdir_out is not mtl_rtdir_light of the traced light";
	}

	/* raw: defined const from the trace, named by the fit's sample and the world's write, nowhere else */
	if ((count_in(k.body, k.nbody, "const float3 raw = world ? rm_rtdir_direct(") != 1) ||
		(words_in(k.body, k.nbody, "raw") != 3))
	{
		return "the kernel's raw is not the traced light alone";
	}

	/* the fit's values: defined once, into the frame's sums only */
	if ((count_in(k.body, k.nbody, "const uint2 fit = ") != 1) || (words_in(k.body, k.nbody, "fit") != 3) ||
		(count_in(k.body, k.nbody, "simd_sum(fit.x)") != 1) || (count_in(k.body, k.nbody, "simd_sum(fit.y)") != 1))
	{
		return "the fit's values go somewhere but the frame's sums";
	}

	/* the light out: the world branch from the traced light, base on every other pixel */
	if ((count_in(k.body, k.nbody, KERNEL_WORLD_BRANCH) != 1) || (count_in(text, strlen(text), "light_out.write(") != 2) ||
		(words_in(k.body, k.nbody, "base") != 1) || (words_in(text, strlen(text), "base") != 2))
	{
		return "the world branch is not the traced light's single write";
	}

	if ((count_in(text, strlen(text), "for (int layer = 0; layer <= MTL_RTDIR_LAYERS; layer++)") != 1) ||
		(count_in(text, strlen(text), "if (mtl_rtdir_at(m, layer) != MTL_RTDIR_AT_PASS)") != 1))
	{
		return "the shadow ray's layer walk changed";
	}

	return NULL;
}

/* src with the first occurrence of from replaced by to, in out; 0 when from is not in src */
static int
mutate(const char *src, const char *from, const char *to, char *out, size_t cap)
{
	const char *p = strstr(src, from);
	const size_t a = p ? (size_t)(p - src) : 0, fl = strlen(from), tl = strlen(to);

	if (!p || (strlen(src) - fl + tl + 1 > cap))
	{
		return 0;
	}

	memcpy(out, src, a);
	memcpy(out + a, to, tl);
	strcpy(out + a + tl, p + fl);
	return 1;
}

/*
 * The kernel's file holds the rule, and each mutant that gives a world
 * pixel the lightmap breaks it: the lightmap added to the write
 * (add-lm), the fit's sample reused in it (reuse-sample), the world
 * written from base (world-from-base), the two branches swapped
 * (branch-swap, the correctness review's), the fit's value added into
 * raw after the sample (fit-leak, the spec review's), the fit's sample
 * taking raw by reference and adding the lightmap to it (fit-sample-leak),
 * and the trace reading the lightmap (direct-reads-lm).
 */
static void
test_no_lightmap_kernel(void)
{
	static char src[65536], m[65536 + 1024], m2[65536 + 1024];
	FILE *f = fopen("src/client/refresh/metal/rm_rtdir.metal", "rb");
	const char *why;
	size_t len;
	int i;

	/* each mutant is up to three replacements, applied in order, so that it still compiles */
	static const struct
	{
		const char *name, *edit[3][2];
	} mutants[] = {
		{ "add-lm", { { "light_out.write(rm_rtdir_out(raw, v.exposure), px);",
			"light_out.write(rm_rtdir_out(raw, v.exposure) + lm.read(px), px);" } } },
		{ "reuse-sample", { { "light_out.write(rm_rtdir_out(raw, v.exposure), px);",
			"light_out.write(rm_rtdir_out(raw + float3(float(fit.x)) / 64.0f, v.exposure), px);" } } },
		{ "world-from-base", { { "light_out.write(rm_rtdir_out(raw, v.exposure), px);",
			"light_out.write(base.read(px), px);" } } },
		{ "branch-swap", { { KERNEL_WORLD_BRANCH,
			"\t\tif (world)\n\t\t{\n\t\t\tlight_out.write(base.read(px), px);\n\t\t\tvis_out.write(float4(0.0f), px);\n"
			"\t\t}\n\t\telse\n\t\t{\n\t\t\tlight_out.write(rm_rtdir_out(raw, v.exposure), px);\n"
			"\t\t\tvis_out.write(rm_rtdir_vis(v.show, t), px);\n\t\t}\n" } } },
		{ "fit-leak", { { "const float3 raw = world ? rm_rtdir_direct(", "float3 raw = world ? rm_rtdir_direct(" },
			{ "rm_rtdir_fit_sample(lm, px, raw) : uint2(0u);\n",
				"rm_rtdir_fit_sample(lm, px, raw) : uint2(0u);\n\traw += float3(float(fit.x)) * 0.001f;\n" } } },
		{ "fit-sample-leak", { { "const float3 raw = world ? rm_rtdir_direct(", "float3 raw = world ? rm_rtdir_direct(" },
			{ "rm_rtdir_fit_sample(texture2d<float, access::read> lm, uint2 px, float3 raw)",
				"rm_rtdir_fit_sample(texture2d<float, access::read> lm, uint2 px, thread float3 &raw)" },
			{ "\tconst float4 lms = lm.read(px);\n", "\tconst float4 lms = lm.read(px);\n\traw += lms.rgb;\n" } } },
		{ "direct-reads-lm", { { "device const rm_rtdir_light *lights, thread rm_rtdir_tally &t)",
			"device const rm_rtdir_light *lights, thread rm_rtdir_tally &t, texture2d<float, access::read> lm)" },
			{ "\tfloat3 raw = float3(0.0f);\n", "\tfloat3 raw = lm.read(uint2(0)).rgb;\n" },
			{ "lights, t) : float3(0.0f);", "lights, t, lm) : float3(0.0f);" } } },
	};

	CHECK(f != NULL);

	if (!f)
	{
		return;
	}

	len = fread(src, 1, sizeof(src) - 1, f);
	CHECK(!ferror(f) && (len < sizeof(src) - 1));
	fclose(f);
	src[len] = 0;

	why = kernel_broken(src);

	if (why)
	{
		printf("rm_rtdir.metal: %s\n", why);
	}

	CHECK(why == NULL);

	for (i = 0; i < (int)(sizeof(mutants) / sizeof(mutants[0])); i++)
	{
		int applied = 1, e;

		strcpy(m, src);

		for (e = 0; applied && (e < 3) && mutants[i].edit[e][0]; e++)
		{
			strcpy(m2, m);
			applied = mutate(m2, mutants[i].edit[e][0], mutants[i].edit[e][1], m, sizeof(m));
		}

		why = applied ? kernel_broken(m) : NULL;

		/* by hand: RM_RTDIR_MUTANTS=<dir> writes each mutant to <dir>/<name>.metal, to compile it */
		if (applied && getenv("RM_RTDIR_MUTANTS"))
		{
			char path[1024];
			FILE *o;

			snprintf(path, sizeof(path), "%s/%s.metal", getenv("RM_RTDIR_MUTANTS"), mutants[i].name);
			o = fopen(path, "wb");

			if (o)
			{
				fputs(m, o);
				fclose(o);
			}

			printf("mutant %s: %s\n", mutants[i].name, why ? why : "passes the kernel check");
		}

		if (!applied || !why)
		{
			printf("mutant %s: %s\n", mutants[i].name, applied ? "passes the kernel check" : "does not apply");
		}

		CHECK(applied && (why != NULL));
	}
}

/* the fit's samples: the channel mean, clamped to QMAX, in 1/64 steps; sums exact whatever the order */
static void
test_q(void)
{
	unsigned long s1 = 0, s2 = 0;
	int i;

	CHECK(mtl_rtdir_q(0, 0, 0) == 0);
	CHECK(mtl_rtdir_q(1, 1, 1) == 64);
	CHECK(mtl_rtdir_q(0.5f, 0, 1) == 32);
	CHECK(mtl_rtdir_q(100, 100, 100) == 1024);
	CHECK(mtl_rtdir_q(-1, -1, -1) == 0);
	CHECK(mtl_rtdir_q(NAN, 0, 0) == 0);
	CHECK(mtl_rtdir_q(INFINITY, 0, 0) == 1024);

	for (i = 0; i < 1000; i++)
	{
		s1 += mtl_rtdir_q(i / 997.0f, 0.3f, 0.1f);
		s2 += mtl_rtdir_q((999 - i) / 997.0f, 0.3f, 0.1f);
	}

	CHECK(s1 == s2);
	/* the 32-bit sums: an 8K view's samples at the most a sample holds */
	CHECK((unsigned long long)(7680 / MTL_RTDIR_FIT_STEP + 1) * (4320 / MTL_RTDIR_FIT_STEP + 1) * 1024 < 0xffffffffULL);
}

/* the stated rule: lightmap sum / raw sum, clamped; the exposure in use when nothing was sampled */
static void
test_fit(void)
{
	int fixed = -1;

	CHECK(near(rm_rtdir_fit(200, 100, 10, 1), 2.0, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 400, 10, 1), 0.25, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 4000, 10, 1), RM_RTDIR_FIT_MIN, 1e-6));
	CHECK(near(rm_rtdir_fit(100000, 1, 10, 1), RM_RTDIR_FIT_MAX, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 0, 10, 3), 3.0, 1e-6));
	CHECK(near(rm_rtdir_fit(100, 50, 0, 3), 3.0, 1e-6));
	CHECK(near(rm_rtdir_fit(0, 50, 10, 3), RM_RTDIR_FIT_MIN, 1e-6));

	CHECK(near(rm_rtdir_exposure(0, 2.5f, &fixed), 2.5, 1e-6) && (fixed == 0));
	CHECK(near(rm_rtdir_exposure(1.5f, 2.5f, &fixed), 1.5, 1e-6) && (fixed == 1));
	CHECK(near(rm_rtdir_exposure(1000, 2.5f, &fixed), RM_RTDIR_EXPOSURE_MAX, 1e-6) && (fixed == 1));
	CHECK(near(rm_rtdir_exposure(-1, 2.5f, &fixed), 2.5, 1e-6) && (fixed == 0));
	CHECK(near(rm_rtdir_exposure(NAN, 2.5f, &fixed), 2.5, 1e-6) && (fixed == 0));
	CHECK(near(rm_rtdir_exposure(INFINITY, 2.5f, NULL), 2.5, 1e-6));
}

/*
 * The exposure is the latest completed frame's fit for this map: of a
 * harvest in slot order, the largest frame wins; an entry at or before the
 * reset (the old map's frames in flight) is ignored.
 */
static void
test_fit_latest(void)
{
	rm_rtdir_stats_t s[3];
	unsigned long after = 0;

	memset(s, 0, sizeof(s));
	/* frame 6 in slot 0 and frame 5 in slot 2, read in that order */
	s[0].frame = 6;
	s[0].fit_lm = 300;
	s[0].fit_raw = 100;
	s[0].fit_n = 10;
	s[1].frame = 5;
	s[1].fit_lm = 100;
	s[1].fit_raw = 100;
	s[1].fit_n = 10;
	CHECK(near(rm_rtdir_fit_latest(s, 2, &after, 1), 3.0, 1e-6) && (after == 6));
	/* the same order the other way round */
	s[2] = s[0];
	s[0] = s[1];
	s[1] = s[2];
	after = 0;
	CHECK(near(rm_rtdir_fit_latest(s, 2, &after, 1), 3.0, 1e-6) && (after == 6));
	/* a frame already applied, or older, changes nothing */
	CHECK(near(rm_rtdir_fit_latest(s, 2, &after, 2.5f), 2.5, 1e-6) && (after == 6));

	/* the map changed after frame 10 was traced: its frames in flight do not overwrite the reset to 1 */
	after = 10;
	s[0].frame = 9;
	s[0].fit_lm = 800;
	s[0].fit_raw = 100;
	s[1].frame = 10;
	s[1].fit_lm = 800;
	s[1].fit_raw = 100;
	CHECK(near(rm_rtdir_fit_latest(s, 2, &after, 1), 1.0, 1e-6) && (after == 10));
	/* and the new map's first completed frame is applied */
	s[1].frame = 11;
	s[1].fit_lm = 200;
	CHECK(near(rm_rtdir_fit_latest(s, 2, &after, 1), 2.0, 1e-6) && (after == 11));

	/* the newest with no sample keeps the exposure in use, and is the frame applied */
	s[0].frame = 12;
	s[0].fit_n = 0;
	s[1].frame = 13;
	s[1].fit_n = 0;
	CHECK(near(rm_rtdir_fit_latest(s, 2, &after, 2), 2.0, 1e-6) && (after == 13));
	CHECK(near(rm_rtdir_fit_latest(s, 0, &after, 2), 2.0, 1e-6) && (after == 13));
	CHECK(near(rm_rtdir_fit_latest(NULL, 2, &after, 2), 2.0, 1e-6) && (after == 13));
}

/* the lights a pixel takes: all of them up to the cap, in the buffer's order; past it the strongest */
static void
test_keep(void)
{
	mtl_rtdir_keep_t k;
	unsigned i;
	int ordered = 1, strongest = 1;

	memset(&k, 0, sizeof(k));

	for (i = 0; i < MTL_RTDIR_MAX_TRACED; i++)
	{
		k = mtl_rtdir_keep(k, i, 1.0f + (float)i);
	}

	CHECK((k.n == MTL_RTDIR_MAX_TRACED) && (k.capped == 0));
	k = mtl_rtdir_keep_order(k);

	for (i = 0; i < MTL_RTDIR_MAX_TRACED; i++)
	{
		ordered &= (k.i[i] == i) && (k.s[i] == 1.0f + (float)i);
	}

	CHECK(ordered);

	/* a weaker light past the cap is dropped, an equal one too (the first weakest stays) */
	k = mtl_rtdir_keep(k, 100, 0.5f);
	k = mtl_rtdir_keep(k, 101, 1.0f);
	CHECK((k.n == MTL_RTDIR_MAX_TRACED) && (k.capped == 1) && (k.i[0] == 0));

	/* a stronger one replaces the weakest; in order again, it is last */
	k = mtl_rtdir_keep(k, 102, 50.0f);
	k = mtl_rtdir_keep_order(k);
	CHECK((k.i[0] == 1) && (k.i[MTL_RTDIR_MAX_TRACED - 1] == 102) && (k.s[MTL_RTDIR_MAX_TRACED - 1] == 50.0f));

	/* 2048 lights in range, the strongest at the end: the cap's strongest are kept, in order */
	memset(&k, 0, sizeof(k));

	for (i = 0; i < RM_RTL_MAX_LIGHTS; i++)
	{
		k = mtl_rtdir_keep(k, i, (float)i);
	}

	k = mtl_rtdir_keep_order(k);

	for (i = 0; i < MTL_RTDIR_MAX_TRACED; i++)
	{
		strongest &= (k.i[i] == RM_RTL_MAX_LIGHTS - MTL_RTDIR_MAX_TRACED + i);
	}

	CHECK((k.n == MTL_RTDIR_MAX_TRACED) && k.capped && strongest);
}

static void
test_pack(void)
{
	static rm_rtl_lights_t l;
	static rm_rtdir_light_t out[RM_RTL_MAX_LIGHTS];
	rm_rtdir_pack_t why;
	long n, i;

	memset(&l, 0, sizeof(l));
	l.nlights = 5;
	/* a point light */
	l.light[0].origin[0] = 1;
	l.light[0].origin[1] = 2;
	l.light[0].origin[2] = 3;
	l.light[0].intensity = 300;
	l.light[0].color[0] = 1;
	l.light[0].color[1] = 0.5f;
	l.light[0].color[2] = 0.25f;
	l.light[0].style = 5;
	/* a spotlight with its target */
	l.light[1].intensity = 200;
	l.light[1].spot = 1;
	l.light[1].cone = 60;
	l.light[1].target_state = RM_RTL_TARGET_FOUND;
	l.light[1].dir[2] = -1;
	l.light[1].style = 300;
	/* a spotlight without its target, a dark light and a NaN one: left out */
	l.light[2].intensity = 300;
	l.light[2].spot = 1;
	l.light[2].target_state = RM_RTL_TARGET_MISSING;
	l.light[3].intensity = -50;
	l.light[4].intensity = NAN;

	n = rm_rtdir_pack(&l, out, RM_RTL_MAX_LIGHTS, &why);
	CHECK(n == 2 && why.kept == 2 && why.untargeted == 1 && why.dark == 2 && why.over == 0);
	CHECK(out[0].pos[0] == 1 && out[0].pos[1] == 2 && out[0].pos[2] == 3 && out[0].pos[3] == 300);
	CHECK(out[0].color[0] == 1 && out[0].color[1] == 0.5f && out[0].color[2] == 0.25f && out[0].color[3] == 5);
	CHECK(out[0].spot[3] == MTL_RTDIR_NOSPOT && out[0].spot[0] == 0 && out[0].spot[2] == 0);
	CHECK(out[1].spot[2] == -1 && near(out[1].spot[3], 0.5, 1e-6));
	/* a style outside 0 to 255 reads 0 */
	CHECK(out[1].color[3] == 0);

	/* the cap */
	n = rm_rtdir_pack(&l, out, 1, &why);
	CHECK(n == 1 && why.over == 1);
	n = rm_rtdir_pack(NULL, out, 1, NULL);
	CHECK(n == 0);

	/* every light of a full lump */
	memset(&l, 0, sizeof(l));
	l.nlights = RM_RTL_MAX_LIGHTS;

	for (i = 0; i < RM_RTL_MAX_LIGHTS; i++)
	{
		l.light[i].intensity = 100;
	}

	n = rm_rtdir_pack(&l, out, RM_RTL_MAX_LIGHTS, &why);
	CHECK(n == RM_RTL_MAX_LIGHTS && why.over == 0 && why.clamped == 0);

	/* the intensity held to RM_RTDIR_MAX_INTENSITY: a map's light of 1e30 or FLT_MAX packs as it */
	l.light[0].intensity = 1e30f;
	l.light[1].intensity = 3.4e38f;
	l.light[2].intensity = RM_RTDIR_MAX_INTENSITY;
	n = rm_rtdir_pack(&l, out, RM_RTL_MAX_LIGHTS, &why);
	CHECK(n == RM_RTL_MAX_LIGHTS && why.clamped == 2);
	CHECK(out[0].pos[3] == RM_RTDIR_MAX_INTENSITY && out[1].pos[3] == RM_RTDIR_MAX_INTENSITY);
	CHECK(out[2].pos[3] == RM_RTDIR_MAX_INTENSITY && out[3].pos[3] == 100);
	CHECK(RM_RTDIR_MAX_INTENSITY > 8192.0 * sqrt(3.0));
	/* a count past the array is held to it */
	l.nlights = RM_RTL_MAX_LIGHTS + 10;
	n = rm_rtdir_pack(&l, out, RM_RTL_MAX_LIGHTS, &why);
	CHECK(n == RM_RTL_MAX_LIGHTS);
}

static void
test_styles(void)
{
	static float out[3 * RM_RTL_STYLES];
	float st[2][3] = { { 1, 1, 1 }, { 0.5f, NAN, 2 } };

	rm_rtdir_styles((const float (*)[3])st, 2, 1.5f, out);
	CHECK(out[0] == 1.5f && out[1] == 1.5f && out[2] == 1.5f);
	CHECK(out[3] == 0.75f && out[4] == 0.0f && out[5] == 3.0f);
	CHECK(out[6] == 1.5f && out[3 * RM_RTL_STYLES - 1] == 1.5f);
	rm_rtdir_styles(NULL, 0, 1, out);
	CHECK(out[0] == 1 && out[3 * RM_RTL_STYLES - 1] == 1);
}

static void
test_view_lines(void)
{
	static const float ident[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
	static const float zero[16];
	const rm_viewport_t vp = { 0, 0, 640, 480, 0, 1 };
	const float eye[3] = { 1, 2, 3 };
	rm_rtdir_view_t v;
	rm_rtdir_stats_t s;
	rm_rtdir_pack_t p;
	char line[512];

	CHECK(sizeof(rm_rtdir_view_t) == 112);
	CHECK(sizeof(rm_rtdir_light_t) == 48);
	CHECK(rm_rtdir_view(ident, ident, &vp, eye, 4, 2.0f, 7, 2, &v) == 1);
	CHECK(v.exposure == 2.0f && v.nlights == 7 && v.show == 2 && v.eye[0] == 1 && v.eye[2] == 3 && v.vp[2] == 640);
	CHECK(rm_rtdir_view(zero, ident, &vp, eye, 4, 2.0f, 7, 2, &v) == 0);
	CHECK(rm_rtdir_view(ident, ident, &vp, eye, 0, 2.0f, 7, 2, &v) == 0);

	memset(&s, 0, sizeof(s));
	s.frame = 9;
	s.pixels = 100;
	s.world = 50;
	s.inrange = 200;
	s.rays = 200;
	s.lit = 150;
	s.capped = 4;
	s.fit_lm = 10;
	s.fit_raw = 5;
	s.fit_n = 3;
	s.exposure = 2;
	s.ms = -1;
	CHECK(rm_rtdir_stats_format(&s, 256, 0, line, sizeof(line)) == 1);
	CHECK(!strcmp(line, "ref_metal: rt direct: frame 9: 100 pixels, 50 world, 256 lights, 200 in range, 200 shadow rays "
			"(4.00 a world pixel), 150 lit, 4 pixels past 16 lights, fit 10/5 over 3 samples, exposure 2.0000 (fit), - ms"));
	/* the GPU's 64-bit counts: past 32 bits they print whole */
	s.inrange = 5000000000ULL;
	CHECK(rm_rtdir_stats_format(&s, 256, 0, line, sizeof(line)) == 1 && strstr(line, " 5000000000 in range,"));
	s.inrange = 200;
	s.ms = 1.5;
	CHECK(rm_rtdir_stats_format(&s, 256, 1, line, sizeof(line)) == 1 && strstr(line, "(r_rt_direct_exposure), 1.500 ms"));
	CHECK(rm_rtdir_stats_format(&s, 256, 1, line, 20) == 0 && line[0] == 0);

	memset(&p, 0, sizeof(p));
	p.kept = 250;
	p.dark = 6;
	CHECK(rm_rtdir_map_format("maps/base1.bsp", &p, line, sizeof(line)) == 1);
	CHECK(!strcmp(line, "ref_metal: rt direct: maps/base1.bsp: 250 lights uploaded (6 dark, 0 spotlights without a "
			"target, 0 past the cap left out, 0 brighter than 16384 held to it)"));
	CHECK(rm_rtdir_map_format("maps/base1.bsp", &p, line, 10) == 0 && line[0] == 0);
}

/* the mode itself: RT-primary is chosen where the device traces rays, and reads 0 where it does not */
static void
test_mode(void)
{
	int why = -1;

	CHECK(rm_pres_decide(3, RM_RTG_COMPUTE, &why) == RM_PRES_RTPRIMARY && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(3, RM_RTG_RENDER, &why) == RM_PRES_RTPRIMARY && why == RM_PRES_WHY_OK);
	CHECK(rm_pres_decide(3, RM_RTG_NONE, &why) == RM_PRES_STOCK && why == RM_PRES_WHY_NORT);
	CHECK(rm_pres_value(RM_PRES_RTPRIMARY, "r_rt_direct", 0) == 1);
	CHECK(rm_pres_value(RM_PRES_RTPRIMARY, "r_rt_ao", 1) == 0);
	CHECK(rm_pres_value(RM_PRES_STOCK, "r_rt_direct", 0) == 0);
	CHECK(rm_pres_value(RM_PRES_ENHANCED, "r_rt_direct", 0) == 0);
	CHECK(rm_pres_value(RM_PRES_RETRO, "r_rt_direct", 0) == 0);
}

int
main(void)
{
	test_on();
	test_scale();
	test_at();
	test_light();
	test_no_lightmap_kernel();
	test_q();
	test_fit();
	test_fit_latest();
	test_keep();
	test_pack();
	test_styles();
	test_view_lines();
	test_mode();
	printf("rm_rtdir_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
