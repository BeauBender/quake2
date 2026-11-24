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
 * with ten mutants it must refuse (test_no_lightmap_kernel) and the host's
 * bindings for it (test_host_binding), the fit's
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

/* text[0, len) with its spaces, tabs and newlines taken out, in out; its length */
static size_t
squeeze(const char *text, size_t len, char *out, size_t cap)
{
	size_t i, o = 0;

	for (i = 0; (i < len) && text[i] && (o + 1 < cap); i++)
	{
		if ((text[i] != ' ') && (text[i] != '\t') && (text[i] != '\n') && (text[i] != '\r'))
		{
			out[o++] = text[i];
		}
	}

	out[o] = 0;
	return o;
}

/* a pin's verdict: it holds, the text differs, or the text differs in its whitespace alone */
enum { PIN_OK, PIN_BROKEN, PIN_SPACE };

/* the rule's message, told that a whitespace-only mismatch is still one */
#define PIN_WHY(r, why) (((r) == PIN_SPACE) ? why " (reformatted? the pin is exact)" : why)

/* needle in text[0, len) exactly n times; PIN_SPACE when it is there n times once both lose their whitespace */
static int
pin_count(const char *text, size_t len, const char *needle, int n)
{
	static char st[65536], sn[4096];
	size_t lt, ln;

	if (count_in(text, len, needle) == n)
	{
		return PIN_OK;
	}

	lt = squeeze(text, len, st, sizeof(st));
	ln = squeeze(needle, strlen(needle), sn, sizeof(sn));
	return (ln && (count_in(st, lt, sn) == n)) ? PIN_SPACE : PIN_BROKEN;
}

/* text[0, len) is pin exactly; PIN_SPACE when the two differ in their whitespace alone */
static int
pin_is(const char *text, size_t len, const char *pin)
{
	static char st[65536], sp[65536];
	size_t lt, lp;

	if ((len == strlen(pin)) && !strncmp(text, pin, len))
	{
		return PIN_OK;
	}

	lt = squeeze(text, len, st, sizeof(st));
	lp = squeeze(pin, strlen(pin), sp, sizeof(sp));
	return ((lt == lp) && !memcmp(st, sp, lt)) ? PIN_SPACE : PIN_BROKEN;
}

/* the worse of two verdicts */
static int
pin_and(int a, int b)
{
	return (a == PIN_BROKEN) || (b == PIN_BROKEN) ? PIN_BROKEN : ((a == PIN_SPACE) || (b == PIN_SPACE) ? PIN_SPACE : PIN_OK);
}

/* the kernel's parameters, whole: seven textures, the six buffers it reads and its position */
#define KERNEL_PARAMS \
	"(depth2d<float, access::read> depth [[texture(0)]],\n" \
	"\t\ttexture2d<float, access::read> normal [[texture(1)]],\n" \
	"\t\ttexture2d<uint, access::read> mat [[texture(2)]],\n" \
	"\t\ttexture2d<float, access::read> lm [[texture(3)]],\n" \
	"\t\ttexture2d<float, access::read> base [[texture(4)]],\n" \
	"\t\ttexture2d<float, access::write> vis_out [[texture(5)]],\n" \
	"\t\ttexture2d<float, access::write> light_out [[texture(6)]],\n" \
	"\t\tconstant rm_rtdir_view &v [[buffer(0)]],\n" \
	"\t\tconstant packed_float3 *styles [[buffer(1)]],\n" \
	"\t\tinstance_acceleration_structure ias [[buffer(2)]],\n" \
	"\t\tdevice const uint *inst [[buffer(3)]],\n" \
	"\t\tdevice const uchar *tmat [[buffer(4)]],\n" \
	"\t\tconstant uint &ntris [[buffer(5)]],\n" \
	"\t\tdevice atomic_uint *counts [[buffer(6)]],\n" \
	"\t\tdevice const rm_rtdir_light *lights [[buffer(9)]],\n" \
	"\t\tuint2 px [[thread_position_in_grid]])"

/* the traced light, the whole statement */
#define KERNEL_RAW \
	"const float3 raw = world ? rm_rtdir_direct(v, rm_rtdir_position(v, px, d), normalize(nn.xyz * 2.0f - 1.0f), styles, ias,\n" \
	"\t\t\tinst, tmat, ntris, lights, t) : float3(0.0f);"

/* the kernel body's reads, each once and no other: the material, depth and normal, and base on a pixel not traced */
static const char *const kernel_reads[] = { "mat.read(px)", "depth.read(px)", "normal.read(px)", "base.read(px)" };

/* the file's macros, each once and no other (the counts' slots) */
static const char *const kernel_defines[] = {
	"#define RM_RTDIR_C_WORLD 0\n", "#define RM_RTDIR_C_INRANGE 1\n", "#define RM_RTDIR_C_RAYS 2\n",
	"#define RM_RTDIR_C_LIT 3\n", "#define RM_RTDIR_C_FIT_LM 4\n", "#define RM_RTDIR_C_FIT_RAW 5\n",
	"#define RM_RTDIR_C_FIT_N 6\n", "#define RM_RTDIR_C_INRANGE_HI 7\n", "#define RM_RTDIR_C_RAYS_HI 8\n",
	"#define RM_RTDIR_C_LIT_HI 9\n", "#define RM_RTDIR_C_CAPPED 10\n",
};

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
	size_t i;
	int r;

	strip_comments(src, text, sizeof(text));

	/* no name is built out of others, and no macro but the counts' slots stands for anything */
	if (count_in(text, strlen(text), "##"))
	{
		return "the file pastes tokens (##)";
	}

	r = (count_in(text, strlen(text), "#define") == (int)(sizeof(kernel_defines) / sizeof(kernel_defines[0]))) ?
		PIN_OK : PIN_BROKEN;

	for (i = 0; i < sizeof(kernel_defines) / sizeof(kernel_defines[0]); i++)
	{
		r = pin_and(r, pin_count(text, strlen(text), kernel_defines[i], 1));
	}

	if (r != PIN_OK)
	{
		return PIN_WHY(r, "the file defines a macro beyond the counts' slots");
	}

	if (!find_fn(text, "kernel void\nrm_rtdir_trace(", &k))
	{
		return "the kernel rm_rtdir_trace is not defined once";
	}

	/* the kernel's inputs: the seven textures and the buffers it has, no other */
	if ((r = pin_is(k.params, k.nparams, KERNEL_PARAMS)) != PIN_OK)
	{
		return PIN_WHY(r, "the kernel's parameters are not its known seven textures and buffers");
	}

	/* what the kernel's body reads: the four known reads, once each, and no other */
	r = (count_in(k.body, k.nbody, ".read(") == (int)(sizeof(kernel_reads) / sizeof(kernel_reads[0]))) ? PIN_OK :
		PIN_BROKEN;

	for (i = 0; i < sizeof(kernel_reads) / sizeof(kernel_reads[0]); i++)
	{
		r = pin_and(r, pin_count(k.body, k.nbody, kernel_reads[i], 1));
	}

	if (r != PIN_OK)
	{
		return PIN_WHY(r, "the kernel's body reads beyond the material, depth, normal and base");
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

	r = pin_and(pin_is(fit.params, fit.nparams, "(texture2d<float, access::read> lm, uint2 px, float3 raw)"),
		pin_is(fit.body, fit.nbody, KERNEL_FIT_SAMPLE));

	if (r != PIN_OK)
	{
		return PIN_WHY(r, "rm_rtdir_fit_sample is not the fit's sample alone");
	}

	r = pin_and(pin_is(out.params, out.nparams, "(float3 raw, float exposure)"), pin_is(out.body, out.nbody, KERNEL_OUT));

	if (r != PIN_OK)
	{
		return PIN_WHY(r, "rm_rtdir_out is not mtl_rtdir_light of the traced light");
	}

	/* raw: defined const from the trace, the whole statement, named by the fit's sample and the world's write, nowhere else */
	if ((r = pin_count(k.body, k.nbody, KERNEL_RAW, 1)) != PIN_OK)
	{
		return PIN_WHY(r, "the kernel's raw is not the traced light alone");
	}

	if (words_in(k.body, k.nbody, "raw") != 3)
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
	if ((r = pin_count(k.body, k.nbody, KERNEL_WORLD_BRANCH, 1)) != PIN_OK)
	{
		return PIN_WHY(r, "the world branch is not the traced light's single write");
	}

	if ((count_in(text, strlen(text), "light_out.write(") != 2) ||
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
 * the trace reading the lightmap (direct-reads-lm), and three that add
 * a texture read after the trace's call: the lightmap's name pasted
 * together by a macro (paste-macro, the correctness review's), and a new
 * texture parameter, q (new-binding, whose host half test_host_binding
 * refuses) or lmx (the spec review's). The kernel's parameters, its
 * body's reads, its traced light's statement and the file's macros are
 * pinned whole for them.
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
		{ "paste-macro", { { "kernel void\nrm_rtdir_trace(", "#define RM_CAT(a, b) a##b\nkernel void\nrm_rtdir_trace(" },
			{ "lights, t) : float3(0.0f);", "lights, t) + RM_CAT(l, m).read(px).rgb : float3(0.0f);" } } },
		{ "new-binding", { { "\t\ttexture2d<float, access::write> light_out [[texture(6)]],\n",
			"\t\ttexture2d<float, access::write> light_out [[texture(6)]],\n"
			"\t\ttexture2d<float, access::read> q [[texture(7)]],\n" },
			{ "lights, t) : float3(0.0f);", "lights, t) + q.read(px).rgb : float3(0.0f);" } } },
		{ "lmx", { { "\t\ttexture2d<float, access::read> base [[texture(4)]],\n",
			"\t\ttexture2d<float, access::read> base [[texture(4)]],\n"
			"\t\ttexture2d<float, access::read> lmx [[texture(7)]],\n" },
			{ "lights, t) : float3(0.0f);", "lights, t) + lmx.read(px).rgb : float3(0.0f);" } } },
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

	/* the spec review's wrap of the world's write: refused, and the message says the pin is exact */
	CHECK(mutate(src, "light_out.write(rm_rtdir_out(raw, v.exposure), px);",
		"light_out.write(rm_rtdir_out(raw, v.exposure),\n\t\t\t\tpx);", m, sizeof(m)));
	why = kernel_broken(m);
	CHECK(why && strstr(why, "reformatted? the pin is exact"));
}

/* the direct pass's texture bindings in rmb_rtdir.m, (texture, index) as the kernel's parameters take them */
static const char *const host_bindings[] = {
	"[ce setTexture:depth atIndex:0];", "[ce setTexture:normal atIndex:1];", "[ce setTexture:material atIndex:2];",
	"[ce setTexture:lm atIndex:3];", "[ce setTexture:base atIndex:4];", "[ce setTexture:t_vis atIndex:5];",
	"[ce setTexture:t_light atIndex:6];",
};

/*
 * The host half of the rule, in rmb_rtdir.m's text (src) with its
 * comments taken out: the file binds a compute texture only in
 * rmb_rtdir_trace, the seven known (texture, index) pairs, each once, and
 * none else (setTexture or setTextures anywhere). NULL when it holds,
 * else the rule broken.
 */
static const char *
binding_broken(const char *src)
{
	static char text[65536];
	const size_t n = sizeof(host_bindings) / sizeof(host_bindings[0]);
	fn_t tr;
	size_t i;
	int r;

	strip_comments(src, text, sizeof(text));

	if (!find_fn(text, "static id<MTLTexture>\nrmb_rtdir_trace(", &tr))
	{
		return "rmb_rtdir_trace is not defined once";
	}

	r = ((count_in(text, strlen(text), "setTexture") == (int)n) && (count_in(tr.body, tr.nbody, "setTexture") == (int)n)) ?
		PIN_OK : PIN_BROKEN;

	for (i = 0; i < n; i++)
	{
		r = pin_and(r, pin_count(tr.body, tr.nbody, host_bindings[i], 1));
	}

	return (r == PIN_OK) ? NULL : PIN_WHY(r, "the direct pass binds a texture beyond its known seven");
}

/* a file of the tree, read whole into buf; 0 when it cannot be */
static int
read_file(const char *path, char *buf, size_t cap)
{
	FILE *f = fopen(path, "rb");
	size_t len;
	int ok;

	if (!f)
	{
		return 0;
	}

	len = fread(buf, 1, cap - 1, f);
	ok = !ferror(f) && (len < cap - 1);
	fclose(f);
	buf[len] = 0;
	return ok;
}

/*
 * rmb_rtdir.m binds the kernel's textures, and the host half of the
 * new-binding mutant (the lightmap bound again, at a new index, for a new
 * kernel parameter) breaks it, as does a binding through setTextures.
 * A binding re-indented is still refused, and says so.
 */
static void
test_host_binding(void)
{
	static char src[65536], m[65536 + 1024];
	static const struct
	{
		const char *name, *from, *to;
	} mutants[] = {
		{ "new-binding", "\t\t[ce setTexture:t_light atIndex:6];\n",
			"\t\t[ce setTexture:t_light atIndex:6];\n\t\t[ce setTexture:lm atIndex:7];\n" },
		{ "new-binding-range", "\t\t[ce setTexture:t_light atIndex:6];\n",
			"\t\t[ce setTexture:t_light atIndex:6];\n\t\t[ce setTextures:&lm withRange:NSMakeRange(7, 1)];\n" },
	};
	const char *why;
	int i;

	CHECK(read_file("src/client/refresh/metal/rmb_rtdir.m", src, sizeof(src)));
	why = binding_broken(src);

	if (why)
	{
		printf("rmb_rtdir.m: %s\n", why);
	}

	CHECK(why == NULL);

	for (i = 0; i < (int)(sizeof(mutants) / sizeof(mutants[0])); i++)
	{
		const int applied = mutate(src, mutants[i].from, mutants[i].to, m, sizeof(m));

		why = applied ? binding_broken(m) : NULL;

		if (!applied || !why)
		{
			printf("host mutant %s: %s\n", mutants[i].name, applied ? "passes the binding check" : "does not apply");
		}

		CHECK(applied && (why != NULL));
	}

	CHECK(mutate(src, "[ce setTexture:lm atIndex:3];", "[ce setTexture:lm  atIndex:3];", m, sizeof(m)));
	why = binding_broken(m);
	CHECK(why && strstr(why, "reformatted? the pin is exact"));
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

/*
 * TASK188 slice 2, the emissive faces: qrad3's emit_surface falloff
 * (intensity / dist^2 x both cosines), the range its cut gives, and the
 * dispatch the kernel calls for every light of the buffer.
 */
static void
test_emit_scale(void)
{
	/* 1e6 at 100 units, both cosines 1: 100; each cosine scales it */
	CHECK(near(mtl_rtdir_emit_scale(1e6f, 100, 1, 1), 100, 1e-3));
	CHECK(near(mtl_rtdir_emit_scale(1e6f, 100, 0.5f, 1), 50, 1e-3));
	CHECK(near(mtl_rtdir_emit_scale(1e6f, 100, 1, 0.5f), 50, 1e-3));
	CHECK(near(mtl_rtdir_emit_scale(1e6f, 200, 1, 1), 25, 1e-3));
	/* behind the face, or the face behind the point: nothing (qrad3's 0.001) */
	CHECK(mtl_rtdir_emit_scale(1e6f, 100, 1, MTL_RTDIR_MIN_DOT) == 0);
	CHECK(mtl_rtdir_emit_scale(1e6f, 100, 1, -1) == 0);
	CHECK(mtl_rtdir_emit_scale(1e6f, 100, MTL_RTDIR_MIN_DOT, 1) == 0);
	/* the range: sqrt(intensity / MTL_RTDIR_EMIT_CUT), 1000 for 1e6 */
	CHECK(mtl_rtdir_emit_scale(1e6f, 1000, 1, 1) > 0);
	CHECK(mtl_rtdir_emit_scale(1e6f, 1001, 1, 1) == 0);
	CHECK(mtl_rtdir_reach(1e6f, 1000, MTL_RTDIR_EMITTER) && !mtl_rtdir_reach(1e6f, 1001, MTL_RTDIR_EMITTER));
	/* a point light's reach is its intensity, as slice 1's cheap test */
	CHECK(mtl_rtdir_reach(300, 299, MTL_RTDIR_NOSPOT) && !mtl_rtdir_reach(300, 300, MTL_RTDIR_NOSPOT));
	CHECK(mtl_rtdir_reach(300, 299, 0.9f) && !mtl_rtdir_reach(300, 0, MTL_RTDIR_NOSPOT));
	CHECK(!mtl_rtdir_reach(NAN, 10, MTL_RTDIR_EMITTER) && !mtl_rtdir_reach(1e6f, NAN, MTL_RTDIR_EMITTER));
	/* not a number, or at the face's own point */
	CHECK(mtl_rtdir_emit_scale(NAN, 100, 1, 1) == 0 && mtl_rtdir_emit_scale(1e6f, NAN, 1, 1) == 0);
	CHECK(mtl_rtdir_emit_scale(1e6f, 100, NAN, 1) == 0 && mtl_rtdir_emit_scale(1e6f, 100, 1, NAN) == 0);
	CHECK(mtl_rtdir_emit_scale(1e6f, 0, 1, 1) == 0);
	/* the dispatch: the marker picks the face's falloff, any other cone the point's */
	CHECK(mtl_rtdir_contrib(1e6f, 100, 1, 1, MTL_RTDIR_EMITTER) == mtl_rtdir_emit_scale(1e6f, 100, 1, 1));
	CHECK(mtl_rtdir_contrib(300, 100, 1, 0, MTL_RTDIR_NOSPOT) == mtl_rtdir_scale(300, 100, 1, 0, MTL_RTDIR_NOSPOT));
	CHECK(mtl_rtdir_contrib(300, 100, 1, 0.5f, 0.9f) == 0);
	/* a face's direction word is its normal: a point behind it (facedot below 0) is not lit, whatever the range */
	CHECK(mtl_rtdir_contrib(1e6f, 100, 1, -0.5f, MTL_RTDIR_EMITTER) == 0);
}

/* the colour, the choice qrad3 makes (RM_RTDIR_EMIT_QRAD3_COLOUR, RM_RTDIR_EMIT_UNREAD) */
static void
test_reflectivity(void)
{
	const float bright[3] = { 0.8f, 0.6f, 0.2f }, dim[3] = { 0.4f, 0.2f, 0.1f }, bad[3] = { NAN, -1, 0.3f };
	float r[3];

	/* a largest channel of 0.5 or more: the mean as it is */
	rm_rtdir_reflectivity(bright, 1, r);
	CHECK(near(r[0], 0.8, 1e-6) && near(r[1], 0.6, 1e-6) && near(r[2], 0.2, 1e-6));
	/* under 0.5: doubled, as CalcTextureReflectivity scales the dim textures up */
	rm_rtdir_reflectivity(dim, 1, r);
	CHECK(near(r[0], 0.8, 1e-6) && near(r[1], 0.4, 1e-6) && near(r[2], 0.2, 1e-6));
	/* a texture qrad3 cannot read: 0, it lights nothing */
	rm_rtdir_reflectivity(bright, 0, r);
	CHECK((r[0] == RM_RTDIR_EMIT_UNREAD) && (r[1] == RM_RTDIR_EMIT_UNREAD) && (r[2] == RM_RTDIR_EMIT_UNREAD));
	CHECK(RM_RTDIR_EMIT_UNREAD == 0.0f);
	/* not a number or below 0: 0; the rest doubled by the largest left, 0.3 */
	rm_rtdir_reflectivity(bad, 1, r);
	CHECK((r[0] == 0) && (r[1] == 0) && near(r[2], 0.6, 1e-6));
}

/* a 64 x 32 face in the z = 100 plane facing down, its points 4 floats apart as the world's vertices */
static const float face_pts[4][4] = { { 0, 0, 100, 1 }, { 64, 0, 100, 1 }, { 64, 32, 100, 1 }, { 0, 32, 100, 1 } };

static void
face_init(rm_rtdir_face_t *f)
{
	f->pts = &face_pts[0][0];
	f->stride = 4;
	f->n = 4;
	f->normal[0] = 0;
	f->normal[1] = 0;
	f->normal[2] = -1;
}

/* one face as qrad3's emit_surface patch */
static void
test_emitter(void)
{
	const float white[3] = { 1, 1, 1 }, orange[3] = { 1, 0.5f, 0 }, faint[3] = { 0.125f, 0.125f, 0.125f };
	rm_rtdir_face_t f;
	rm_rtdir_light_t l;
	int cl = -1;

	face_init(&f);
	memset(&l, 0, sizeof(l));
	CHECK(rm_rtdir_emitter(&f, 300, white, &l, &cl) == RM_RTDIR_EMIT_KEPT && cl == 0);
	/* the points' mean a unit in front, the face's normal, the marker, style 0 */
	CHECK(near(l.pos[0], 32, 1e-4) && near(l.pos[1], 16, 1e-4) && near(l.pos[2], 99, 1e-4));
	CHECK((l.spot[0] == 0) && (l.spot[1] == 0) && (l.spot[2] == -1) && (l.spot[3] == MTL_RTDIR_EMITTER));
	CHECK(l.color[3] == 0);
	/* value x max(refl) x area x direct_scale: 300 x 1 x 2048 x 0.4 */
	CHECK(near(l.pos[3], 300.0 * 2048.0 * 0.4, 1e-1));
	CHECK(near(l.color[0], 1, 1e-6) && near(l.color[1], 1, 1e-6) && near(l.color[2], 1, 1e-6));

	/* a coloured texture: the light refl x refl / max, normalized, so orange's green is a quarter */
	CHECK(rm_rtdir_emitter(&f, 300, orange, &l, &cl) == RM_RTDIR_EMIT_KEPT);
	CHECK(near(l.color[0], 1, 1e-6) && near(l.color[1], 0.25, 1e-6) && (l.color[2] == 0));
	CHECK(near(l.pos[3], 300.0 * 2048.0 * 0.4, 1e-1));

	/* below qrad3's DIRECT_LIGHT (3) in every channel: no direct light; an unread texture (0) likewise */
	CHECK(rm_rtdir_emitter(&f, 23, faint, &l, &cl) == RM_RTDIR_EMIT_DIM);
	CHECK(rm_rtdir_emitter(&f, 24, faint, &l, &cl) == RM_RTDIR_EMIT_KEPT);
	{
		const float zero[3] = { 0, 0, 0 };

		CHECK(rm_rtdir_emitter(&f, 300, zero, &l, &cl) == RM_RTDIR_EMIT_DIM);
	}

	/* the intensity cap: a huge value is held to it and counted */
	CHECK(rm_rtdir_emitter(&f, 2000000000, white, &l, &cl) == RM_RTDIR_EMIT_KEPT && cl == 1);
	CHECK(l.pos[3] == RM_RTDIR_MAX_EMIT_INTENSITY && isfinite(l.pos[3]));

	/* shapeless: too few points, too many, a line, a bad normal, a value not above 0 */
	f.n = 2;
	CHECK(rm_rtdir_emitter(&f, 300, white, &l, &cl) == RM_RTDIR_EMIT_SHAPELESS);
	f.n = RM_RTDIR_EMIT_MAXPTS + 1;
	CHECK(rm_rtdir_emitter(&f, 300, white, &l, &cl) == RM_RTDIR_EMIT_SHAPELESS);
	{
		static const float line[3][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };
		rm_rtdir_face_t g = { &line[0][0], 3, 3, { 0, 0, 1 } };

		CHECK(rm_rtdir_emitter(&g, 300, white, &l, &cl) == RM_RTDIR_EMIT_SHAPELESS);
	}
	{
		static const float huge[3][3] = { { 0, 0, 0 }, { 3e38f, 0, 0 }, { 3e38f, 3e38f, 0 } };
		rm_rtdir_face_t g = { &huge[0][0], 3, 3, { 0, 0, 1 } };

		/* the points' mean overflows no float (the sums are doubles); the area is finite: held to the cap */
		CHECK(rm_rtdir_emitter(&g, 300, white, &l, &cl) == RM_RTDIR_EMIT_KEPT && cl == 1 && isfinite(l.pos[0]));
	}
	face_init(&f);
	f.normal[2] = -2;
	CHECK(rm_rtdir_emitter(&f, 300, white, &l, &cl) == RM_RTDIR_EMIT_SHAPELESS);
	f.normal[2] = NAN;
	CHECK(rm_rtdir_emitter(&f, 300, white, &l, &cl) == RM_RTDIR_EMIT_SHAPELESS);
	face_init(&f);
	CHECK(rm_rtdir_emitter(&f, 0, white, &l, &cl) == RM_RTDIR_EMIT_SHAPELESS);
	CHECK(rm_rtdir_emitter(NULL, 300, white, &l, &cl) == RM_RTDIR_EMIT_SHAPELESS);

	/* lit as the kernel lights it: a floor point 100 below the centre gets intensity / 99^2 */
	CHECK(rm_rtdir_emitter(&f, 300, white, &l, &cl) == RM_RTDIR_EMIT_KEPT);
	{
		const float s = mtl_rtdir_contrib(l.pos[3], 99, 1, -(0 * l.spot[0] + 0 * l.spot[1] + 1 * l.spot[2]), l.spot[3]);

		CHECK(near(s, 300.0 * 2048.0 * 0.4 / (99.0 * 99.0), 1e-2));
	}
}

/* the world's faces for test_pack_emitters: face 7 is a submodel's, the others the test face */
static int
face_of(void *ctx, long face, rm_rtdir_face_t *out)
{
	(void)ctx;

	if (face == 7)
	{
		return 0;
	}

	face_init(out);
	return 1;
}

static void
test_pack_emitters(void)
{
	static rm_rtl_emitters_t e;
	static rm_rtdir_light_t out[RM_RTDIR_MAX_EMITTERS + 8];
	rm_rtdir_emitpack_t why;
	char line[512];
	long n, i;

	memset(&e, 0, sizeof(e));
	e.ntex = 3;
	e.tex[0].read = 1;
	e.tex[0].mean[0] = e.tex[0].mean[1] = e.tex[0].mean[2] = 0.8f;
	e.tex[1].read = 0;        /* unread: qrad3's 0, so dim */
	e.tex[1].mean[0] = e.tex[1].mean[1] = e.tex[1].mean[2] = RM_RTL_UNREAD_GREY;
	e.tex[2].read = 1;        /* dim: 0.2 doubled to 0.4, value 5: 2 < 3 */
	e.tex[2].mean[0] = e.tex[2].mean[1] = e.tex[2].mean[2] = 0.2f;

	e.emit[0] = (rm_rtl_emit_t){ 0, 0, 300 };
	e.emit[1] = (rm_rtl_emit_t){ 1, 1, 300 };
	e.emit[2] = (rm_rtl_emit_t){ 2, 2, 5 };
	e.emit[3] = (rm_rtl_emit_t){ 7, 0, 300 };      /* a submodel's face: not the world's */
	e.emit[4] = (rm_rtl_emit_t){ 4, -1, 300 };     /* past the texture cap: unread */
	e.emit[5] = (rm_rtl_emit_t){ 5, 0, 200 };
	e.emit[6] = (rm_rtl_emit_t){ 6, 0, 2000000000 }; /* held to the intensity cap */
	e.n = 7;

	n = rm_rtdir_pack_emitters(&e, face_of, NULL, out, RM_RTDIR_MAX_EMITTERS, &why);
	CHECK(n == 3 && why.kept == 3);
	CHECK(why.bmodel == 1 && why.dim == 3 && why.unread == 2 && why.shapeless == 0 && why.over == 0 && why.clamped == 1);
	/* in e's order: 300, 200, the cap */
	CHECK(near(out[0].pos[3], 300.0 * 0.8 * 2048 * 0.4, 1e-1) && near(out[1].pos[3], 200.0 * 0.8 * 2048 * 0.4, 1e-1));
	CHECK(out[2].pos[3] == RM_RTDIR_MAX_EMIT_INTENSITY);

	/* the same input packs the same records */
	{
		static rm_rtdir_light_t again[RM_RTDIR_MAX_EMITTERS + 8];

		CHECK(rm_rtdir_pack_emitters(&e, face_of, NULL, again, RM_RTDIR_MAX_EMITTERS, NULL) == 3 &&
			!memcmp(out, again, 3 * sizeof(out[0])));
	}

	/* the cap: past it counted, the first ones kept */
	for (i = 0; i < RM_RTDIR_MAX_EMITTERS + 5; i++)
	{
		e.emit[i] = (rm_rtl_emit_t){ 0, 0, 300 };
	}

	e.n = RM_RTDIR_MAX_EMITTERS + 5;
	n = rm_rtdir_pack_emitters(&e, face_of, NULL, out, RM_RTDIR_MAX_EMITTERS, &why);
	CHECK(n == RM_RTDIR_MAX_EMITTERS && why.over == 5);
	CHECK(rm_rtdir_pack_emitters(NULL, face_of, NULL, out, RM_RTDIR_MAX_EMITTERS, &why) == 0 && why.kept == 0);
	CHECK(rm_rtdir_pack_emitters(&e, NULL, NULL, out, RM_RTDIR_MAX_EMITTERS, &why) == 0);

	/* the buffer holds both kinds */
	CHECK(RM_RTDIR_MAX_UPLOAD == RM_RTL_MAX_LIGHTS + RM_RTDIR_MAX_EMITTERS);

	memset(&why, 0, sizeof(why));
	why.kept = 214;
	why.dim = 2;
	CHECK(rm_rtdir_emit_format("maps/base1.bsp", &why, line, sizeof(line)) == 1);
	CHECK(!strcmp(line, "ref_metal: rt direct: maps/base1.bsp: 214 emissive faces uploaded (0 not the world's, "
			"0 shapeless, 2 below qrad3's direct light, 0 of an unread texture, 0 past the cap of 2048 left out, "
			"0 held to the intensity cap)"));
	CHECK(rm_rtdir_emit_format("maps/base1.bsp", &why, line, 10) == 0 && line[0] == 0);
}

/*
 * The lights a pixel samples are chosen deterministically: with point
 * lights and emitters in one buffer, the 16 strongest of those in range,
 * the same set for the same input, summed in the buffer's order. The
 * selection mtl_rtdir_keep runs per light as the kernel's loop does.
 */
static void
test_mixed_choice(void)
{
	mtl_rtdir_keep_t a, b;
	unsigned i;
	int same = 1;

	memset(&a, 0, sizeof(a));
	memset(&b, 0, sizeof(b));

	/* 40 lights: even ones points, odd ones faces, contributions by the kernel's dispatch */
	for (i = 0; i < 40; i++)
	{
		const float s = (i & 1u) ? mtl_rtdir_contrib(4e5f, 50.0f + (float)i, 1, 1, MTL_RTDIR_EMITTER) :
			mtl_rtdir_contrib(400, 100.0f + (float)i, 1, 0, MTL_RTDIR_NOSPOT);

		if (s > 0)
		{
			a = mtl_rtdir_keep(a, i, s);
			b = mtl_rtdir_keep(b, i, s);
		}
	}

	a = mtl_rtdir_keep_order(a);
	b = mtl_rtdir_keep_order(b);
	CHECK(a.n == MTL_RTDIR_MAX_TRACED && a.capped);

	for (i = 0; i < a.n; i++)
	{
		same = same && (a.i[i] == b.i[i]) && (a.s[i] == b.s[i]) && ((i == 0) || (a.i[i - 1] < a.i[i]));
	}

	CHECK(same);

	/* the points (400 - d: 300 down to 262) outshine the faces (4e5 / d^2: 154 down to 64): the 16 nearest points */
	for (i = 0, same = 1; i < a.n; i++)
	{
		same = same && (a.i[i] == 2 * i);
	}

	CHECK(same);

	/* a face near the point beats them all and takes the weakest point's place, the order kept */
	a = mtl_rtdir_keep(a, 41, mtl_rtdir_contrib(4e5f, 20, 1, 1, MTL_RTDIR_EMITTER));
	a = mtl_rtdir_keep_order(a);
	CHECK(a.n == MTL_RTDIR_MAX_TRACED && a.i[MTL_RTDIR_MAX_TRACED - 1] == 41 && a.i[MTL_RTDIR_MAX_TRACED - 2] == 28);
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
	test_host_binding();
	test_q();
	test_fit();
	test_fit_latest();
	test_keep();
	test_pack();
	test_styles();
	test_view_lines();
	test_emit_scale();
	test_reflectivity();
	test_emitter();
	test_pack_emitters();
	test_mixed_choice();
	test_mode();
	printf("rm_rtdir_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
