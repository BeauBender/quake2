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
	CHECK(rm_rtdir_show(4) == RM_RTDIR_SHOW_DLIGHTS && RM_RTDIR_SHOW_MAX == 4);
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

	/* TASK188 slice 3b: a model that carries the dynamic light passes its ray, up to the same layers */
	CHECK(mtl_rtdir_at(MTL_RTDIR_MAT_CARRIER, 0) == MTL_RTDIR_AT_PASS);
	CHECK(mtl_rtdir_at(MTL_RTDIR_MAT_CARRIER, MTL_RTDIR_LAYERS) == MTL_RTDIR_AT_SHADOW);

	for (m = 0; m < 256; m++)
	{
		if ((m != 3) && (m != 5) && (m != MTL_RTDIR_MAT_CARRIER))
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
	"\t\tconstant rm_rtdir_dlight *dl [[buffer(10)]],\n" \
	"\t\tdevice const uint *self [[buffer(11)]],\n" \
	"\t\tuint2 px [[thread_position_in_grid]])"

/* the traced light, the whole statement */
/* rm_rtdir_direct's parameter list, whole (TASK188 slice 2): no texture, no buffer but the scene's and the lights */
#define DIRECT_PARAMS \
	"(constant rm_rtdir_view &v, float3 p, float3 n, constant packed_float3 *styles,\n" \
	"\t\tinstance_acceleration_structure ias, device const uint *inst, device const uchar *tmat, uint ntris,\n" \
	"\t\tdevice const rm_rtdir_light *lights, constant rm_rtdir_dlight *dl, device const uint *self,\n" \
	"\t\tthread rm_rtdir_tally &t)"

#define KERNEL_RAW \
	"const float3 raw = world ? rm_rtdir_direct(v, rm_rtdir_position(v, px, d), normalize(nn.xyz * 2.0f - 1.0f), styles, ias,\n" \
	"\t\t\tinst, tmat, ntris, lights, dl, self, t) : float3(0.0f);"

/* the kernel body's reads, each once and no other: the material, depth and normal, and base on a pixel not traced */
static const char *const kernel_reads[] = { "mat.read(px)", "depth.read(px)", "normal.read(px)", "base.read(px)" };

/* the file's macros, each once and no other (the counts' slots) */
static const char *const kernel_defines[] = {
	"#define RM_RTDIR_C_WORLD 0\n", "#define RM_RTDIR_C_INRANGE 1\n", "#define RM_RTDIR_C_RAYS 2\n",
	"#define RM_RTDIR_C_LIT 3\n", "#define RM_RTDIR_C_FIT_LM 4\n", "#define RM_RTDIR_C_FIT_RAW 5\n",
	"#define RM_RTDIR_C_FIT_N 6\n", "#define RM_RTDIR_C_INRANGE_HI 7\n", "#define RM_RTDIR_C_RAYS_HI 8\n",
	"#define RM_RTDIR_C_LIT_HI 9\n", "#define RM_RTDIR_C_CAPPED 10\n",
};

/*
 * TASK188 slice 3: rm_rtdir_direct's walk over its candidates, whole: traced
 * strongest first under mtl_rtdir_walk's bound, one shadow ray each, and a
 * light counted toward the MTL_RTDIR_MAX_TRACED only once its ray reached it
 */
#define KERNEL_WALK \
	"\tfor (uint j = 0; mtl_rtdir_walk(j, k.n, seen); j++)\n" \
	"\t{\n" \
	"\t\tconst rm_rtdir_light l = lights[k.i[j]];\n" \
	"\n" \
	"\t\tif (v.show != 3u)\n" \
	"\t\t{\n" \
	"\t\t\tconst float3 to = l.pos.xyz - o;\n" \
	"\t\t\tconst float len = length(to);\n" \
	"\n" \
	"\t\t\tt.rays++;\n" \
	"\n" \
	"\t\t\tif ((len > 1.0f) && (rm_rtdir_seen(ray(o, to / len, 0.0f, len - 1.0f), ias, inst, tmat, ntris, 0u) < 0.5f))\n" \
	"\t\t\t{\n" \
	"\t\t\t\tcontinue;\n" \
	"\t\t\t}\n" \
	"\n" \
	"\t\t\tt.lit++;\n" \
	"\t\t}\n" \
	"\n" \
	"\t\tseen++;\n" \
	"\t\traw += k.s[j] * l.color.rgb * float3(styles[(uint)l.color.w & 255u]);\n" \
	"\t}\n"

/*
 * TASK188 slice 3b: the dynamic lights' term, whole: under its own bound
 * (mtl_rtdir_dlight_walk), gl1's radius rule (mtl_rtdir_dlight_add), one
 * shadow ray each that passes the model carrying the light, and its
 * colour; and the three terms' sum, the dynamic lights alone under view 4
 */
#define KERNEL_DYN \
	"\tfor (uint i = 0; mtl_rtdir_dlight_walk(i, v.ndlights); i++)\n" \
	"\t{\n" \
	"\t\tconst rm_rtdir_dlight d = dl[i];\n" \
	"\t\tconst float a = mtl_rtdir_dlight_add(mtl_rts_v3(p.x, p.y, p.z), mtl_rts_v3(n.x, n.y, n.z),\n" \
	"\t\t\t\tmtl_rts_v3(d.pos.x, d.pos.y, d.pos.z), d.pos.w);\n" \
	"\n" \
	"\t\tif (!(a > 0.0f))\n" \
	"\t\t{\n" \
	"\t\t\tcontinue;\n" \
	"\t\t}\n" \
	"\n" \
	"\t\tt.inrange++;\n" \
	"\n" \
	"\t\tif (v.show != 3u)\n" \
	"\t\t{\n" \
	"\t\t\tconst float3 to = d.pos.xyz - o;\n" \
	"\t\t\tconst float len = length(to);\n" \
	"\n" \
	"\t\t\tt.rays++;\n" \
	"\n" \
	"\t\t\tif ((len > 1.0f) &&\n" \
	"\t\t\t\t(rm_rtdir_seen(ray(o, to / len, 0.0f, len - 1.0f), ias, inst, tmat, ntris, mtl_rtdir_dlight_bit(i)) < 0.5f))\n" \
	"\t\t\t{\n" \
	"\t\t\t\tcontinue;\n" \
	"\t\t\t}\n" \
	"\n" \
	"\t\t\tt.lit++;\n" \
	"\t\t}\n" \
	"\n" \
	"\t\tsum += a * d.color.rgb;\n" \
	"\t}\n"

#define KERNEL_TERMS \
	"\t{\n" \
	"\t\tconst float3 dyn = rm_rtdir_dyn(v, p, n, o, ias, inst, tmat, ntris, dl, t);\n" \
	"\t\tconst float3 own = rm_rtdir_own(v, p, ias, inst, tmat, ntris, self) * float3(styles[0]);\n" \
	"\n" \
	"\t\treturn ((v.show == 4u) ? dyn : (raw + dyn + own)) * MTL_RTDIR_UNIT;\n" \
	"\t}\n"

/* the own light's lookup: the triangle's word, inside the table, through mtl_rtdir_self */
#define KERNEL_OWN \
	"\t\tif (mtl_rtdir_at(m, hop) != MTL_RTDIR_AT_PASS)\n" \
	"\t\t{\n" \
	"\t\t\tconst mtl_rgba_t c = mtl_rtdir_self((tri < v.nself) ? self[tri] : 0u);\n" \
	"\n" \
	"\t\t\treturn float3(c.r, c.g, c.b);\n" \
	"\t\t}\n"

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

	/*
	 * TASK188 slice 2: the point lights and the emissive faces come in one
	 * buffer, read twice (the loop's record and the kept one's), through the
	 * reach test and the dispatch, and nothing else enters the trace: its
	 * parameters are pinned whole.
	 */
	if ((r = pin_is(dir.params, dir.nparams, DIRECT_PARAMS)) != PIN_OK)
	{
		return PIN_WHY(r, "rm_rtdir_direct's parameters are not the view, the point, the styles, the scene and the lights");
	}

	r = pin_and(pin_count(dir.body, dir.nbody, "const rm_rtdir_light l = lights[i];", 1),
		pin_count(dir.body, dir.nbody, "const rm_rtdir_light l = lights[k.i[j]];", 1));
	r = pin_and(r, pin_count(dir.body, dir.nbody, "if (!mtl_rtdir_reach(l.pos.w, dist, l.spot.w))", 1));
	r = pin_and(r, pin_count(dir.body, dir.nbody,
		"s = mtl_rtdir_contrib(l.pos.w, dist, dot(n, dir), -dot(dir, l.spot.xyz), l.spot.w);", 1));

	if ((r != PIN_OK) || (count_in(dir.body, dir.nbody, "lights[") != 2) || (words_in(dir.body, dir.nbody, "lights") != 2))
	{
		return PIN_WHY((r != PIN_OK) ? r : PIN_BROKEN, "rm_rtdir_direct's lights are not the buffer's two reads, its reach and its dispatch");
	}

	/*
	 * TASK188 slice 3: the candidates kept once each in the first loop and
	 * ranked; the walk whole; the one shadow ray of the file inside it (no
	 * ray before the bound); the cap from the walk's count.
	 */
	r = pin_and(pin_count(dir.body, dir.nbody, KERNEL_WALK, 1),
		pin_count(dir.body, dir.nbody, "k = mtl_rtdir_keep(k, i, s);", 1));
	r = pin_and(r, pin_count(dir.body, dir.nbody, "k = mtl_rtdir_keep_rank(k);", 1));
	r = pin_and(r, pin_count(dir.body, dir.nbody, "t.capped = mtl_rtdir_capped(k, seen);", 1));

	if ((r != PIN_OK) || (count_in(text, strlen(text), "rm_rtdir_seen(") != 3) ||
		(count_in(dir.body, dir.nbody, "mtl_rtdir_keep(") != 1) ||
		(count_in(dir.body, dir.nbody, "mtl_rtdir_keep_rank(") != 1) || (words_in(dir.body, dir.nbody, "seen") != 4))
	{
		return PIN_WHY((r != PIN_OK) ? r : PIN_BROKEN,
			"rm_rtdir_direct's walk is not its candidates traced strongest first, one ray each, under the bound");
	}

	/*
	 * TASK188 slice 3b: the dynamic lights' term whole, its walk the one of
	 * the file, its ray the file's third; the own light's lookup whole; the
	 * three terms summed once, each function called once
	 */
	{
		fn_t dyn, own;

		if (!find_fn(text, "static float3\nrm_rtdir_dyn(", &dyn) || !find_fn(text, "static float3\nrm_rtdir_own(", &own))
		{
			return "rm_rtdir_dyn or rm_rtdir_own is not defined once";
		}

		r = pin_and(pin_count(dyn.body, dyn.nbody, KERNEL_DYN, 1), pin_count(dir.body, dir.nbody, KERNEL_TERMS, 1));

		if ((r != PIN_OK) || (count_in(text, strlen(text), "mtl_rtdir_dlight_walk(") != 1) ||
			(count_in(dyn.body, dyn.nbody, "rm_rtdir_seen(") != 1) || (count_in(text, strlen(text), "rm_rtdir_dyn(") != 2) ||
			(count_in(text, strlen(text), "dl[") != 1))
		{
			return PIN_WHY((r != PIN_OK) ? r : PIN_BROKEN,
				"rm_rtdir_dyn is not the dynamic lights by gl1's rule, one ray each, under their bound, summed in");
		}

		r = pin_count(own.body, own.nbody, KERNEL_OWN, 1);

		if ((r != PIN_OK) || (count_in(text, strlen(text), "rm_rtdir_own(") != 2) ||
			(count_in(text, strlen(text), "mtl_rtdir_self(") != 1) || (count_in(text, strlen(text), "self[") != 1))
		{
			return PIN_WHY((r != PIN_OK) ? r : PIN_BROKEN, "rm_rtdir_own is not the face's own light from its triangle's word");
		}
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
		{ "direct-reads-lm", { { "\t\tthread rm_rtdir_tally &t)\n{\n\tconst float3 o",
			"\t\tthread rm_rtdir_tally &t, texture2d<float, access::read> lm)\n{\n\tconst float3 o" },
			{ "\tfloat3 raw = float3(0.0f);\n", "\tfloat3 raw = lm.read(uint2(0)).rgb;\n" },
			{ "lights, dl, self, t) : float3(0.0f);", "lights, dl, self, t, lm) : float3(0.0f);" } } },
		{ "paste-macro", { { "kernel void\nrm_rtdir_trace(", "#define RM_CAT(a, b) a##b\nkernel void\nrm_rtdir_trace(" },
			{ "lights, dl, self, t) : float3(0.0f);", "lights, dl, self, t) + RM_CAT(l, m).read(px).rgb : float3(0.0f);" } } },
		{ "new-binding", { { "\t\ttexture2d<float, access::write> light_out [[texture(6)]],\n",
			"\t\ttexture2d<float, access::write> light_out [[texture(6)]],\n"
			"\t\ttexture2d<float, access::read> q [[texture(7)]],\n" },
			{ "lights, dl, self, t) : float3(0.0f);", "lights, dl, self, t) + q.read(px).rgb : float3(0.0f);" } } },
		{ "emit-buffer", { { "\t\tthread rm_rtdir_tally &t)\n{\n\tconst float3 o",
			"\t\tthread rm_rtdir_tally &t, device const float4 *lmc)\n{\n\tconst float3 o" },
			{ "\t\traw += k.s[j] * l.color.rgb * float3(styles[(uint)l.color.w & 255u]);\n",
				"\t\traw += k.s[j] * l.color.rgb * float3(styles[(uint)l.color.w & 255u]) + lmc[0].rgb;\n" },
			{ "lights, dl, self, t) : float3(0.0f);", "lights, dl, self, t, (device const float4 *)inst) : float3(0.0f);" } } },
		{ "emit-third-read", { { "\tfor (uint j = 0; mtl_rtdir_walk(j, k.n, seen); j++)\n",
			"\traw += lights[v.nlights].color.rgb;\n\tfor (uint j = 0; mtl_rtdir_walk(j, k.n, seen); j++)\n" } } },
		{ "emit-bypass", { { "if (!mtl_rtdir_reach(l.pos.w, dist, l.spot.w))", "if (!(dist > 0.0f))" } } },
		{ "trace-in-range", { { "\t\tt.inrange++;\n\t\tk = mtl_rtdir_keep(k, i, s);\n",
			"\t\tif (rm_rtdir_seen(ray(o, dir, 0.0f, dist - 1.0f), ias, inst, tmat, ntris, 0u) < 0.5f)\n"
			"\t\t{\n\t\t\tcontinue;\n\t\t}\n\n\t\tt.inrange++;\n\t\tk = mtl_rtdir_keep(k, i, s);\n" } } },
		{ "walk-unbounded", { { "mtl_rtdir_walk(j, k.n, seen)", "(j < k.n)" } } },
		{ "seen-before-ray", { { "\t\tseen++;\n\t\traw +=", "\t\traw +=" },
			{ "\t\tif (v.show != 3u)\n\t\t{\n\t\t\tconst float3 to = l.pos.xyz - o;\n",
				"\t\tseen++;\n\n\t\tif (v.show != 3u)\n\t\t{\n\t\t\tconst float3 to = l.pos.xyz - o;\n" } } },
		{ "vis-ignored", { { "ias, inst, tmat, ntris, 0u) < 0.5f))", "ias, inst, tmat, ntris, 0u) < -1.0f))" } } },
		/* TASK188 slice 3b: the dynamic lights' term and the emissive faces' own light */
		{ "dlight-noshadow", { { "\t\t\tif ((len > 1.0f) &&\n\t\t\t\t(rm_rtdir_seen(",
			"\t\t\tif ((len > 1.0f) && (v.nself == 12345u) &&\n\t\t\t\t(rm_rtdir_seen(" } } },
		{ "dlight-unbounded", { { "mtl_rtdir_dlight_walk(i, v.ndlights)", "(i < v.ndlights)" } } },
		{ "dlight-falloff", { { "\t\tconst float a = mtl_rtdir_dlight_add(mtl_rts_v3(p.x, p.y, p.z), mtl_rts_v3(n.x, n.y, n.z),\n"
			"\t\t\t\tmtl_rts_v3(d.pos.x, d.pos.y, d.pos.z), d.pos.w);\n",
			"\t\tconst float a = d.pos.w - distance(p, d.pos.xyz);\n" } } },
		{ "dlight-no-carrier", { { "ntris, mtl_rtdir_dlight_bit(i)) < 0.5f))", "ntris, 0u) < 0.5f))" } } },
		{ "dlight-dropped", { { "(raw + dyn + own)", "(raw + own)" } } },
		{ "self-ignored", { { "mtl_rtdir_self((tri < v.nself) ? self[tri] : 0u)", "mtl_rtdir_self(0u)" } } },
		{ "self-unbounded", { { "mtl_rtdir_self((tri < v.nself) ? self[tri] : 0u)", "mtl_rtdir_self(self[tri])" } } },
		{ "self-dropped", { { "(raw + dyn + own)", "(raw + dyn)" } } },
		{ "lmx", { { "\t\ttexture2d<float, access::read> base [[texture(4)]],\n",
			"\t\ttexture2d<float, access::read> base [[texture(4)]],\n"
			"\t\ttexture2d<float, access::read> lmx [[texture(7)]],\n" },
			{ "lights, dl, self, t) : float3(0.0f);", "lights, dl, self, t) + lmx.read(px).rgb : float3(0.0f);" } } },
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

	if (r != PIN_OK)
	{
		return PIN_WHY(r, "the direct pass binds a texture beyond its known seven");
	}

	/* TASK188 slice 2: the lights and the emissive faces in one buffer, bound once at the kernel's index 9 */
	r = pin_and(pin_count(tr.body, tr.nbody, "[ce setBuffer:lights offset:0 atIndex:9];", 1),
		pin_count(text, strlen(text), "(n > RM_RTDIR_MAX_UPLOAD)", 1));

	if ((r != PIN_OK) || (count_in(text, strlen(text), "atIndex:9]") != 1))
	{
		return PIN_WHY((r != PIN_OK) ? r : PIN_BROKEN, "the lights are not one buffer at index 9 of the upload's cap");
	}

	/*
	 * TASK188 slice 3b: the frame's dynamic lights, every slot of the array
	 * bound at index 10, their count held to MTL_RTDIR_MAX_DLIGHTS and handed
	 * to the view; the own-light table at index 11, its count the view's
	 */
	r = pin_and(pin_count(tr.body, tr.nbody, "[ce setBytes:dlights length:sizeof(dlights) atIndex:10];", 1),
		pin_count(tr.body, tr.nbody, "[ce setBuffer:(selftab ? selftab : counts) offset:0 atIndex:11];", 1));
	r = pin_and(r, pin_count(text, strlen(text), "n = (n > MTL_RTDIR_MAX_DLIGHTS) ? MTL_RTDIR_MAX_DLIGHTS : n;", 1));
	r = pin_and(r, pin_count(text, strlen(text), "static rm_rtdir_dlight_t dlights[MTL_RTDIR_MAX_DLIGHTS];", 1));
	r = pin_and(r, pin_count(text, strlen(text), "view.ndlights = ndlights;", 1));
	/* TASK188 slice 3c: the table's count taken at the encode, beside its binding, whatever the order of the calls before */
	r = pin_and(r, pin_count(tr.body, tr.nbody, "view.nself = selftab ? (unsigned)nself : 0u;", 1));

	if ((r != PIN_OK) || (count_in(text, strlen(text), "atIndex:10]") != 1) || (count_in(text, strlen(text), "atIndex:11]") != 1) ||
		(count_in(text, strlen(text), "view.ndlights") != 1) || (count_in(text, strlen(text), "view.nself") != 1))
	{
		return PIN_WHY((r != PIN_OK) ? r : PIN_BROKEN,
			"the dynamic lights are not the bounded array at index 10, or the own-light table not the one at index 11");
	}

	return NULL;
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
		{ "lights-cap", "(n > RM_RTDIR_MAX_UPLOAD)", "(n > RM_RTL_MAX_LIGHTS)" },
		{ "lights-twice", "\t\t[ce setBuffer:lights offset:0 atIndex:9];\n",
			"\t\t[ce setBuffer:lights offset:0 atIndex:9];\n\t\t[ce setBuffer:counts offset:0 atIndex:9];\n" },
		/* TASK188 slice 3b */
		{ "dlights-cap", "\tn = (n > MTL_RTDIR_MAX_DLIGHTS) ? MTL_RTDIR_MAX_DLIGHTS : n;\n", "" },
		{ "dlights-count", "\tview.ndlights = ndlights;\n", "\tview.ndlights = 4096;\n" },
		{ "self-count", "\tview.nself = selftab ? (unsigned)nself : 0u;\n", "\tview.nself = 0xffffffffu;\n" },
		/* TASK188 slice 3c: the table's count not taken at the encode */
		{ "self-count-early", "\t\tview.nself = selftab ? (unsigned)nself : 0u;\n", "" },
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
 * TASK188 slice 3c, the exposure fit at map load, in the engine's and the
 * back end's text (scene, back) with their comments taken out: the map's
 * fit is taken once, in RM_RTDir_Lights, after its reset to 1 there; the
 * view's exposure is rm_rtdir_exposure of r_rt_direct_exposure and that
 * fit, once, in RM_RTDir_View; and the back end neither fits nor writes
 * the view's exposure. NULL when it holds, else the rule broken.
 */
static const char *
fit_rule_broken(const char *scene, const char *back)
{
	static char text[65536], btext[65536], sq[65536];
	const char *reset, *taken;
	fn_t li, vi;

	strip_comments(scene, text, sizeof(text));
	strip_comments(back, btext, sizeof(btext));

	if (!find_fn(text, "static void\nRM_RTDir_Lights(", &li) || !find_fn(text, "void\nRM_RTDir_View(", &vi))
	{
		return "RM_RTDir_Lights or RM_RTDir_View is not defined once";
	}

	reset = strstr(li.body, "dir_fit = 1.0f;");
	taken = strstr(li.body, "dir_fit = rm_rtdir_fit_map(");

	if ((count_in(text, strlen(text), "dir_fit = ") != 2) || (count_in(li.body, li.nbody, "dir_fit = 1.0f;") != 1) ||
		(count_in(li.body, li.nbody, "dir_fit = rm_rtdir_fit_map(") != 1) || !reset || !taken || (reset > taken))
	{
		return "the map's fit is not taken once, in RM_RTDir_Lights, after its reset";
	}

	if ((count_in(text, strlen(text), "rm_rtdir_exposure(") != 1) ||
		(pin_count(vi.body, vi.nbody, "exposure = rm_rtdir_exposure(RM_Effect(r_rt_direct_exposure), dir_fit, &dir_fixed);", 1) !=
			PIN_OK))
	{
		return "the view's exposure is not r_rt_direct_exposure's, else the map's fit";
	}

	squeeze(btext, strlen(btext), sq, sizeof(sq));

	if ((count_in(sq, strlen(sq), "view.exposure=") != 0) || (count_in(sq, strlen(sq), "view.exposure+=") != 0) ||
		(count_in(sq, strlen(sq), "view.exposure*=") != 0) || (count_in(btext, strlen(btext), "rm_rtdir_fit") != 0))
	{
		return "the back end fits or writes the view's exposure";
	}

	return NULL;
}

/* the fit-at-load pin holds on the real files, and each mutant of it is refused */
static void
test_fit_binding(void)
{
	static char scene[65536], back[65536], m[65536 + 1024];
	static const struct
	{
		const char *name, *file, *from, *to;
	} mutants[] = {
		/* the fit computed and not used: every map at 1 */
		{ "fit-ignored", "scene", "r_rt_direct_exposure), dir_fit, &dir_fixed);", "r_rt_direct_exposure), 1.0f, &dir_fixed);" },
		/* the reset dropped: a map whose fit is not reached keeps the old map's */
		{ "reset-dropped", "scene", "\tdir_fit = 1.0f;\n", "" },
		/* a fit again each view */
		{ "fit-per-view", "scene", "\texposure = rm_rtdir_exposure(",
			"\tdir_fit = rm_rtdir_fit_map(dir_lights, dir_nlights, NULL, 0, NULL, NULL, NULL);\n\texposure = rm_rtdir_exposure(" },
		/* the back end overriding the view's exposure (the lagged fit's shape) */
		{ "back-end-exposure", "back", "\t\t[ce setBytes:&view length:sizeof(view) atIndex:0];",
			"\t\tview.exposure = 1.0f;\n\t\t[ce setBytes:&view length:sizeof(view) atIndex:0];" },
	};
	const char *why;
	int i;

	CHECK(read_file("src/client/refresh/metal/rm_rtdirscene.c", scene, sizeof(scene)));
	CHECK(read_file("src/client/refresh/metal/rmb_rtdir.m", back, sizeof(back)));
	why = fit_rule_broken(scene, back);

	if (why)
	{
		printf("the exposure fit: %s\n", why);
	}

	CHECK(why == NULL);

	for (i = 0; i < (int)(sizeof(mutants) / sizeof(mutants[0])); i++)
	{
		const int sc = !strcmp(mutants[i].file, "scene");
		const int applied = mutate(sc ? scene : back, mutants[i].from, mutants[i].to, m, sizeof(m));

		why = applied ? (sc ? fit_rule_broken(m, back) : fit_rule_broken(scene, m)) : NULL;

		if (!applied || !why)
		{
			printf("fit mutant %s: %s\n", mutants[i].name, applied ? "passes the fit check" : "does not apply");
		}

		CHECK(applied && (why != NULL));
	}
}

/* a test's line test: a line that ends at x past fit_wall[0] is blocked; each line's ends recorded */
static float fit_wall[1] = { 1e30f };
static float fit_a[3], fit_b[3];

static int
fit_clear(void *ctx, const float *a, const float *b)
{
	(void)ctx;
	memcpy(fit_a, a, sizeof(fit_a));
	memcpy(fit_b, b, sizeof(fit_b));
	return b[0] < fit_wall[0];
}

static void
fit_point_light(rm_rtdir_light_t *l, float x, float y, float z, float intensity)
{
	memset(l, 0, sizeof(*l));
	l->pos[0] = x;
	l->pos[1] = y;
	l->pos[2] = z;
	l->pos[3] = intensity;
	l->color[0] = l->color[1] = l->color[2] = 1.0f;
	l->spot[3] = MTL_RTDIR_NOSPOT;
}

static void
fit_point(rm_rtdir_fitpt_t *p, float x, float lm)
{
	memset(p, 0, sizeof(*p));
	p->pos[0] = x;
	p->normal[2] = 1.0f;
	p->lm[0] = p->lm[1] = p->lm[2] = lm;
}

/* TASK188 slice 3c: which samples enter: not near black, not clipped, a number */
static void
test_fit_enters(void)
{
	const float in[][3] = { { 20, 10, 5 }, { 16, 0, 0 }, { 0, 0, 16 }, { 254, 254, 254 }, { 100, 254.5f, 3 } };
	const float out[][3] = { { 15.9f, 15.9f, 15.9f }, { 0, 0, 0 }, { -50, -50, -50 }, { 255, 10, 10 }, { 10, 10, 300 },
		{ NAN, 50, 50 }, { 50, INFINITY, 50 } };
	int i;

	CHECK(RM_RTDIR_FIT_DARK == 16.0f && RM_RTDIR_FIT_CLIP == 255.0f);

	for (i = 0; i < (int)(sizeof(in) / sizeof(in[0])); i++)
	{
		CHECK(rm_rtdir_fit_enters(in[i]) == 1);
	}

	for (i = 0; i < (int)(sizeof(out) / sizeof(out[0])); i++)
	{
		CHECK(rm_rtdir_fit_enters(out[i]) == 0);
	}
}

/* the traced light at a sample: the kernel's rule on the host, with the line test as its shadow ray */
static void
test_fit_light(void)
{
	static rm_rtdir_light_t l[64];
	const float p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 1 };
	float out[3];
	unsigned long rays = 0;
	double want = 0;
	int i;

	/* one point light 100 above, intensity 300: (300 - 100) x 1, its line from 1 off the face to 1 short of the light */
	fit_point_light(&l[0], 0, 0, 100, 300);
	l[0].color[1] = 0.5f;
	fit_wall[0] = 1e30f;
	rm_rtdir_fit_light(l, 1, p, n, fit_clear, NULL, out, &rays);
	CHECK(near(out[0], 200, 1e-3) && near(out[1], 100, 1e-3) && near(out[2], 200, 1e-3) && (rays == 1));
	CHECK(near(fit_a[2], RM_RTDIR_FIT_BIAS, 1e-4) && near(fit_b[2], 99, 1e-3) && near(fit_a[0], 0, 1e-6));

	/* blocked: nothing; out of range or behind: no ray */
	fit_wall[0] = -1;
	rays = 0;
	rm_rtdir_fit_light(l, 1, p, n, fit_clear, NULL, out, &rays);
	CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0 && (rays == 1));
	fit_point_light(&l[0], 0, 0, -100, 300);
	fit_point_light(&l[1], 0, 0, 400, 300);
	rays = 0;
	rm_rtdir_fit_light(l, 2, p, n, fit_clear, NULL, out, &rays);
	CHECK(out[0] == 0 && (rays == 0));

	/* an emissive face 100 above, facing down: intensity / dist^2 */
	memset(&l[0], 0, sizeof(l[0]));
	l[0].pos[2] = 100;
	l[0].pos[3] = 1e6f;
	l[0].color[0] = l[0].color[1] = l[0].color[2] = 1.0f;
	l[0].spot[2] = -1;
	l[0].spot[3] = MTL_RTDIR_EMITTER;
	fit_wall[0] = 1e30f;
	rm_rtdir_fit_light(l, 1, p, n, fit_clear, NULL, out, NULL);
	CHECK(near(out[0], 100, 1e-3));

	/* the bound: 64 visible lights at one place sum the 16 strongest in 16 rays; behind a wall, 32 rays and nothing */
	for (i = 0; i < 64; i++)
	{
		fit_point_light(&l[i], 0, 0, 100, 300 + (float)i);
	}

	rays = 0;
	rm_rtdir_fit_light(l, 64, p, n, fit_clear, NULL, out, &rays);
	CHECK(rays == MTL_RTDIR_MAX_TRACED);

	/* each (300 + i - 100) x 1, the 16 strongest the last 16 */
	for (i = 48; i < 64; i++)
	{
		want += 200.0 + i;
	}

	CHECK(near(out[0], want, 1e-2));
	fit_wall[0] = -1;
	rays = 0;
	rm_rtdir_fit_light(l, 64, p, n, fit_clear, NULL, out, &rays);
	CHECK((rays == MTL_RTDIR_MAX_RAYS) && (out[0] == 0));
	fit_wall[0] = 1e30f;
}

/*
 * The map's exposure: a known luxel set and its ratio; an all-dark map
 * reads 1; a lit room beside a dark one that a very bright fixture
 * reaches keeps the lit room's ratio (the dark luxels and the clipped
 * one left out), where a fit over every luxel is crushed to the floor.
 */
static void
test_fit_map(void)
{
	static rm_rtdir_light_t l[2];
	static rm_rtdir_fitpt_t pts[32];
	rm_rtdir_mapfit_t f;
	int i;

	fit_wall[0] = 1e30f;

	/* the known set: a light of 300 at 100 above; lightmap 100 and 200 where the traced light is 200 */
	fit_point_light(&l[0], 0, 0, 100, 300);
	fit_point(&pts[0], 0, 100);
	fit_point(&pts[1], 0, 200);
	CHECK(mtl_rtdir_q(100 / 255.0f, 100 / 255.0f, 100 / 255.0f) == 25 && mtl_rtdir_q(200 / 255.0f, 200 / 255.0f, 200 / 255.0f) == 50);
	CHECK(near(rm_rtdir_fit_map(l, 1, pts, 2, fit_clear, NULL, &f), 0.75, 1e-6));
	CHECK((f.lm == 75) && (f.raw == 100) && (f.n == 2) && (f.dark == 0) && (f.clipped == 0) && (f.rays == 2) &&
		near(f.exposure, 0.75, 1e-6));

	/* the same with the light behind a wall: no traced light, so 1 */
	fit_wall[0] = -1;
	CHECK(near(rm_rtdir_fit_map(l, 1, pts, 2, fit_clear, NULL, &f), 1.0, 1e-6) && (f.n == 2) && (f.raw == 0));
	fit_wall[0] = 1e30f;

	/* an all-dark map: nothing enters, 1 */
	for (i = 0; i < 8; i++)
	{
		fit_point(&pts[i], (float)i, 5);
	}

	CHECK(near(rm_rtdir_fit_map(l, 1, pts, 8, fit_clear, NULL, &f), 1.0, 1e-6) && (f.n == 0) && (f.dark == 8) && (f.rays == 0));
	CHECK(near(rm_rtdir_fit_map(l, 1, NULL, 0, fit_clear, NULL, NULL), 1.0, 1e-6));
	CHECK(near(rm_rtdir_fit_map(NULL, 0, pts, 8, fit_clear, NULL, NULL), 1.0, 1e-6));

	/* one very bright fixture: 4 lit luxels (100 / 200 = 0.5), 20 dark ones (4) and 1 clipped one under it */
	fit_point_light(&l[0], 0, 0, 100, 300);
	memset(&l[1], 0, sizeof(l[1]));
	l[1].pos[0] = 10000;
	l[1].pos[2] = 50;
	l[1].pos[3] = 1e9f;
	l[1].color[0] = l[1].color[1] = l[1].color[2] = 1.0f;
	l[1].spot[2] = -1;
	l[1].spot[3] = MTL_RTDIR_EMITTER;

	for (i = 0; i < 4; i++)
	{
		fit_point(&pts[i], 0, 100);
	}

	for (i = 4; i < 24; i++)
	{
		fit_point(&pts[i], 10000, 4);
	}

	fit_point(&pts[24], 10000, 255);
	CHECK(near(rm_rtdir_fit_map(l, 2, pts, 25, fit_clear, NULL, &f), 0.5, 1e-6));
	CHECK((f.n == 4) && (f.dark == 20) && (f.clipped == 1) && (f.lm == 100) && (f.raw == 200));

	/* the same luxels all entered (each lightmap raised to the floor) take the fixture's light: crushed to the floor */
	for (i = 4; i < 25; i++)
	{
		fit_point(&pts[i], 10000, 16);
	}

	CHECK(near(rm_rtdir_fit_map(l, 2, pts, 25, fit_clear, NULL, &f), RM_RTDIR_FIT_MIN, 1e-6) && (f.n == 25));
}

/* a face's lightmap at the nearest luxel, its maps summed, the luxel held to the face */
static void
test_fit_luxel(void)
{
	/* two maps of 2 x 2 luxels, 3 bytes each */
	static const unsigned char s[2 * 4 * 3] = {
		1, 2, 3, 10, 20, 30, 4, 5, 6, 40, 50, 60,
		100, 100, 100, 101, 101, 101, 102, 102, 102, 103, 103, 103,
	};
	float out[3];

	rm_rtdir_fit_luxel(s, 1, 2, 2, 0.4f, 0.6f, out);
	CHECK(out[0] == 4 && out[1] == 5 && out[2] == 6);
	rm_rtdir_fit_luxel(s, 2, 2, 2, 0.4f, 0.6f, out);
	CHECK(out[0] == 106 && out[1] == 107 && out[2] == 108);
	rm_rtdir_fit_luxel(s, 2, 2, 2, 0.6f, 0.4f, out);
	CHECK(out[0] == 111 && out[1] == 121 && out[2] == 131);
	rm_rtdir_fit_luxel(s, 1, 2, 2, -3.0f, 9.0f, out);
	CHECK(out[0] == 4 && out[1] == 5 && out[2] == 6);
	rm_rtdir_fit_luxel(s, 1, 2, 2, NAN, NAN, out);
	CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3);
	rm_rtdir_fit_luxel(s, 0, 2, 2, 0, 0, out);
	CHECK(out[0] == 0 && out[1] == 0 && out[2] == 0);
	rm_rtdir_fit_luxel(NULL, 1, 2, 2, 0, 0, out);
	CHECK(out[0] == 0);
	rm_rtdir_fit_luxel(s, 1, 0, 2, 0, 0, out);
	CHECK(out[0] == 0);
}

/* a pixel's candidates: all of them up to MTL_RTDIR_MAX_RAYS; past it the strongest; ranked strongest first */
static void
test_keep(void)
{
	mtl_rtdir_keep_t k;
	unsigned i;
	int ranked = 1, strongest = 1;

	memset(&k, 0, sizeof(k));

	for (i = 0; i < MTL_RTDIR_MAX_RAYS; i++)
	{
		k = mtl_rtdir_keep(k, i, 1.0f + (float)i);
	}

	CHECK((k.n == MTL_RTDIR_MAX_RAYS) && (k.capped == 0));

	/* a weaker light past the cap is dropped, an equal one too (the first weakest stays) */
	k = mtl_rtdir_keep(k, 100, 0.5f);
	k = mtl_rtdir_keep(k, 101, 1.0f);
	CHECK((k.n == MTL_RTDIR_MAX_RAYS) && (k.capped == 1) && (k.i[0] == 0));

	/* a stronger one replaces the weakest; ranked, it is first, and the rest follow strongest first */
	k = mtl_rtdir_keep(k, 102, 50.0f);
	k = mtl_rtdir_keep_rank(k);
	CHECK((k.i[0] == 102) && (k.s[0] == 50.0f));

	for (i = 1; i < MTL_RTDIR_MAX_RAYS; i++)
	{
		ranked &= (k.i[i] == MTL_RTDIR_MAX_RAYS - i) && (k.s[i] == (float)(MTL_RTDIR_MAX_RAYS - i + 1));
	}

	CHECK(ranked);

	/* 2048 lights in range, the strongest at the end: the bound's strongest are kept, strongest first */
	memset(&k, 0, sizeof(k));

	for (i = 0; i < RM_RTL_MAX_LIGHTS; i++)
	{
		k = mtl_rtdir_keep(k, i, (float)i);
	}

	k = mtl_rtdir_keep_rank(k);

	for (i = 0; i < MTL_RTDIR_MAX_RAYS; i++)
	{
		strongest &= (k.i[i] == RM_RTL_MAX_LIGHTS - 1 - i);
	}

	CHECK((k.n == MTL_RTDIR_MAX_RAYS) && k.capped && strongest);

	/* equal contributions rank by the buffer's order */
	memset(&k, 0, sizeof(k));

	for (i = 0; i < 10; i++)
	{
		k = mtl_rtdir_keep(k, 9 - i, 2.0f);
	}

	k = mtl_rtdir_keep_rank(k);

	for (i = 0, ranked = 1; i < 10; i++)
	{
		ranked &= (k.i[i] == i);
	}

	CHECK(ranked);
}

/* the kernel's choice (rm_rtdir_direct's two loops) on the host: s[i] each light's contribution, vis[i] its ray */
typedef struct
{
	double sum;
	unsigned rays, seen, capped;
	unsigned took[MTL_RTDIR_MAX_RAYS];
} choice_t;

static choice_t
choose(const float *s, const unsigned char *vis, unsigned n)
{
	mtl_rtdir_keep_t k;
	choice_t c;
	unsigned i, j;

	memset(&k, 0, sizeof(k));
	memset(&c, 0, sizeof(c));

	for (i = 0; i < n; i++)
	{
		if (s[i] > 0.0f)
		{
			k = mtl_rtdir_keep(k, i, s[i]);
		}
	}

	k = mtl_rtdir_keep_rank(k);

	for (j = 0; mtl_rtdir_walk(j, k.n, c.seen); j++)
	{
		c.rays++;

		if (!vis[k.i[j]])
		{
			continue;
		}

		c.took[c.seen++] = k.i[j];
		c.sum += k.s[j];
	}

	c.capped = mtl_rtdir_capped(k, c.seen);
	return c;
}

/*
 * TASK188 slice 3: the 16 lights a pixel sums are chosen among the visible
 * ones. The candidates are traced strongest first until 16 reach the
 * pixel, at most MTL_RTDIR_MAX_RAYS rays; an occluded light takes no
 * place. Past the bound (more lights in range than candidates, and fewer
 * than 16 of these visible) the weaker lights are not tried: the pixel is
 * capped.
 */
static void
test_visible_choice(void)
{
	static float s[RM_RTL_MAX_LIGHTS];
	static unsigned char vis[RM_RTL_MAX_LIGHTS];
	choice_t c, d;
	unsigned i;
	int ok;

	CHECK(MTL_RTDIR_MAX_RAYS == 32);
	CHECK(MTL_RTDIR_MAX_TRACED == 16);

	/* 20 lights, the 4 strongest occluded: the next 16 are summed, none of the occluded */
	for (i = 0; i < 20; i++)
	{
		s[i] = 100.0f - (float)i;
		vis[i] = i >= 4;
	}

	c = choose(s, vis, 20);
	CHECK((c.seen == MTL_RTDIR_MAX_TRACED) && (c.rays == 20) && (c.capped == 0));

	for (i = 0, ok = 1; i < c.seen; i++)
	{
		ok &= (c.took[i] == 4 + i);
	}

	CHECK(ok);

	/* every other light occluded, 40 in range: the first 16 visible, strongest first, in 31 rays */
	for (i = 0; i < 40; i++)
	{
		s[i] = 100.0f - (float)i;
		vis[i] = !(i & 1u);
	}

	c = choose(s, vis, 40);
	CHECK((c.seen == MTL_RTDIR_MAX_TRACED) && (c.rays == 31) && (c.capped == 0));

	for (i = 0, ok = 1; i < c.seen; i++)
	{
		ok &= (c.took[i] == 2 * i);
	}

	CHECK(ok);

	/* 40 in range and none visible: the bound, MTL_RTDIR_MAX_RAYS rays, and the pixel capped */
	memset(vis, 0, sizeof(vis));
	c = choose(s, vis, 40);
	CHECK((c.seen == 0) && (c.rays == MTL_RTDIR_MAX_RAYS) && (c.capped == 1) && (c.sum == 0));

	/* only the 33rd strongest visible: past the bound, not tried; the 32nd is the last tried */
	vis[32] = 1;
	c = choose(s, vis, 40);
	CHECK((c.seen == 0) && (c.rays == MTL_RTDIR_MAX_RAYS) && (c.capped == 1));
	vis[32] = 0;
	vis[31] = 1;
	c = choose(s, vis, 40);
	CHECK((c.seen == 1) && (c.took[0] == 31) && (c.rays == MTL_RTDIR_MAX_RAYS) && (c.capped == 1));

	/* 10 in range, 3 occluded: every one tried, 7 summed, not capped (nothing was left out) */
	for (i = 0; i < 10; i++)
	{
		vis[i] = ((i % 3) != 0) || (i == 9);
	}

	c = choose(s, vis, 10);
	CHECK((c.seen == 7) && (c.rays == 10) && (c.capped == 0));

	/* 2048 rising lights, all visible: the 16 strongest in 16 rays; all occluded: the bound */
	for (i = 0; i < RM_RTL_MAX_LIGHTS; i++)
	{
		s[i] = (float)i + 1.0f;
		vis[i] = 1;
	}

	c = choose(s, vis, RM_RTL_MAX_LIGHTS);
	CHECK((c.seen == MTL_RTDIR_MAX_TRACED) && (c.rays == MTL_RTDIR_MAX_TRACED) && (c.capped == 0) &&
		(c.took[0] == RM_RTL_MAX_LIGHTS - 1) &&
		(c.took[MTL_RTDIR_MAX_TRACED - 1] == RM_RTL_MAX_LIGHTS - MTL_RTDIR_MAX_TRACED));
	memset(vis, 0, sizeof(vis));
	c = choose(s, vis, RM_RTL_MAX_LIGHTS);
	CHECK((c.seen == 0) && (c.rays == MTL_RTDIR_MAX_RAYS) && (c.capped == 1));

	/* equal lights, all visible: the first 16 of the buffer, the same twice */
	for (i = 0; i < 40; i++)
	{
		s[i] = 5.0f;
		vis[i] = 1;
	}

	c = choose(s, vis, 40);
	d = choose(s, vis, 40);
	CHECK(!memcmp(&c, &d, sizeof(c)) && (c.seen == MTL_RTDIR_MAX_TRACED) && (c.took[0] == 0) &&
		(c.took[MTL_RTDIR_MAX_TRACED - 1] == MTL_RTDIR_MAX_TRACED - 1));
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

	CHECK(sizeof(rm_rtdir_view_t) == 128);
	CHECK(sizeof(rm_rtdir_dlight_t) == 32);
	CHECK(sizeof(rm_rtdir_light_t) == 48);
	CHECK(rm_rtdir_view(ident, ident, &vp, eye, 4, 2.0f, 7, 2, &v) == 1);
	CHECK(v.exposure == 2.0f && v.nlights == 7 && v.show == 2 && v.eye[0] == 1 && v.eye[2] == 3 && v.vp[2] == 640);
	/* the back end's counts, 0 until it sets them */
	CHECK(v.ndlights == 0 && v.nself == 0);
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
			"(4.00 a world pixel), 150 lit, 4 pixels past the bound (32 rays, 16 lights), fit 10/5 over 3 samples, "
			"exposure 2.0000 (fit), - ms"));
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
	/* a point a hair from the face is finite: the distance is taken as at least 1, as qrad3 does */
	CHECK(isfinite(mtl_rtdir_emit_scale(1e6f, 1e-20f, 1, 1)) && near(mtl_rtdir_emit_scale(1e6f, 1e-20f, 1, 1), 1e6, 1e-3));
	CHECK(isfinite(mtl_rtdir_emit_scale(16384.0f * 16384.0f, 1e-20f, 1, 1)));
	CHECK(near(mtl_rtdir_emit_scale(1e6f, 0.5f, 1, 1), 1e6, 1e-3));
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
 * The candidates a pixel traces are chosen deterministically: with point
 * lights and emitters in one buffer, the MTL_RTDIR_MAX_RAYS strongest of
 * those in range, the same set for the same input, ranked strongest
 * first. The selection mtl_rtdir_keep runs per light as the kernel's loop
 * does.
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

	a = mtl_rtdir_keep_rank(a);
	b = mtl_rtdir_keep_rank(b);
	CHECK(a.n == MTL_RTDIR_MAX_RAYS && a.capped);

	for (i = 0; i < a.n; i++)
	{
		same = same && (a.i[i] == b.i[i]) && (a.s[i] == b.s[i]) && ((i == 0) || (a.s[i - 1] >= a.s[i]));
	}

	CHECK(same);

	/* the points (400 - d: 300 down to 262) outshine the faces (4e5 / d^2: 154 down to 50): the 20 points, then the 12 nearest faces */
	for (i = 0, same = 1; i < a.n; i++)
	{
		same = same && (a.i[i] == ((i < 20) ? 2 * i : 2 * (i - 20) + 1));
	}

	CHECK(same);

	/* a face near the point beats them all and takes the weakest kept face's place */
	a = mtl_rtdir_keep(a, 41, mtl_rtdir_contrib(4e5f, 20, 1, 1, MTL_RTDIR_EMITTER));
	a = mtl_rtdir_keep_rank(a);
	CHECK(a.n == MTL_RTDIR_MAX_RAYS && a.i[0] == 41 && a.i[MTL_RTDIR_MAX_RAYS - 1] == 21);
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

/*
 * TASK188 slice 3b: the dynamic light's falloff (mtl_rtdir_dlight_add, the
 * kernel's) against the stock rule, gl1's R_AddDynamicLights as the
 * lightmap takes it (rm_lm_dlight), at the one luxel of a 1 x 1 surface
 * on the plane z = 0 whose texture axes are the compiler's base axes at
 * scale 1: the light's height, its offset in the plane and its intensity
 * at a few radii, the cut at DLIGHT_CUTOFF on both sides, and the values.
 */
static void
test_dlight(void)
{
	static const float normal[3] = { 0, 0, 1 }, vecs0[4] = { 1, 0, 0, 0 }, vecs1[4] = { 0, 1, 0, 0 };
	static const float color[3] = { 1.0f, 0.5f, 0.25f };
	/* x, y, height, intensity, the add gl1 gives */
	static const float cases[][5] = {
		{ 0, 0, 10, 300, 290 },             /* straight above */
		{ 100, 0, 10, 300, 190 },           /* 100 out in the plane */
		{ 100, 60, 50, 300, 120 },          /* frad 250, fdist 100 + 60 / 2 */
		{ -150, 40, 50, 300, 80 },          /* frad 250, fdist 150 + 20 */
		{ 185, 0, 50, 300, 65 },            /* just inside the radius: 185 < 250 - 64 */
		{ 186, 0, 50, 300, 0 },             /* at it: nothing */
		{ 250, 0, 50, 300, 0 },             /* past it */
		{ 0, 0, 200, 300, 100 },            /* frad 100 */
		{ 0, 0, 236, 300, 0 },              /* frad 64: fdist 0 is not below 0 */
		{ 0, 0, 240, 300, 0 },              /* frad 60, under the cutoff */
		{ 33.7f, -12.2f, 20.5f, 200, 179.5f - 39.0f },  /* fractional: truncated offsets 33 and 12, 33 + 6 */
		{ 500, 300, 100, 1000, 900 - 650 }, /* a big light: 500 + 300 / 2 */
	};
	size_t i;

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
	{
		float bl[3] = { 0, 0, 0 };
		const float o[3] = { cases[i][0], cases[i][1], cases[i][2] };
		const float a = mtl_rtdir_dlight_add(mtl_rts_v3(0, 0, 0), mtl_rts_v3(0, 0, 1), mtl_rts_v3(o[0], o[1], o[2]),
				cases[i][3]);
		int k;

		CHECK(rm_lm_dlight(bl, 1, 1, normal, 0, vecs0, vecs1, 0, 0, o, cases[i][3], color) == 1);
		CHECK(near(a, cases[i][4], 1e-3));

		for (k = 0; k < 3; k++)
		{
			CHECK(near(bl[k], a * color[k], 1e-3));
		}
	}

	/* behind the plane: gl1 marks the front only, so the kernel adds nothing there */
	CHECK(mtl_rtdir_dlight_add(mtl_rts_v3(0, 0, 0), mtl_rts_v3(0, 0, 1), mtl_rts_v3(0, 0, -10), 300) == 0.0f);
	/* the same light on a wall (normal +x), the in-plane offsets on y and z */
	CHECK(near(mtl_rtdir_dlight_add(mtl_rts_v3(0, 0, 0), mtl_rts_v3(1, 0, 0), mtl_rts_v3(50, 100, 60), 300), 250 - 130, 1e-3));
	/* a value that is not a number adds nothing */
	CHECK(!(mtl_rtdir_dlight_add(mtl_rts_v3(0, 0, 0), mtl_rts_v3(0, 0, 1), mtl_rts_v3(0, 0, 10), NAN) > 0.0f));

	/* the record: origin and intensity, the colour; one rm_dlight_ok refuses is not taken */
	{
		const float org[3] = { 1, 2, 3 }, bad[3] = { NAN, 0, 0 }, far[3] = { 2e9f, 0, 0 };
		rm_rtdir_dlight_t d;

		CHECK(rm_rtdir_dlight(org, color, 200, &d) == 1);
		CHECK(d.pos[0] == 1 && d.pos[2] == 3 && d.pos[3] == 200 && d.color[1] == 0.5f && d.color[3] == 0);
		CHECK(rm_rtdir_dlight(bad, color, 200, &d) == 0);
		CHECK(rm_rtdir_dlight(far, color, 200, &d) == 0);
		CHECK(rm_rtdir_dlight(org, color, INFINITY, &d) == 0);
		CHECK(rm_rtdir_dlight(NULL, color, 200, &d) == 0);
	}
}

/*
 * The ray bound with N dynamic lights, a host model of the kernel's walks:
 * with every map light and every dynamic light in range and seen, a pixel
 * traces its map lights' walk (at most MTL_RTDIR_MAX_RAYS) and one ray a
 * dynamic light up to MTL_RTDIR_MAX_DLIGHTS, whatever N the view says; the
 * carried bit of each is its own.
 */
static void
test_dlight_bound(void)
{
	static const unsigned ns[] = { 0, 1, 4, 31, 32, 33, 100, 4096, 0xffffffffu };
	size_t a;
	unsigned i, bits = 0;

	for (a = 0; a < sizeof(ns) / sizeof(ns[0]); a++)
	{
		unsigned rays = 0;

		for (i = 0; mtl_rtdir_dlight_walk(i, ns[a]); i++)
		{
			rays++;
		}

		CHECK(rays == ((ns[a] < MTL_RTDIR_MAX_DLIGHTS) ? ns[a] : MTL_RTDIR_MAX_DLIGHTS));
	}

	/* the pixel's whole bound: the map's candidates and the dynamic lights */
	{
		mtl_rtdir_keep_t k;
		unsigned j, seen = 0, rays = 0;

		k.n = 0;
		k.capped = 0;

		for (i = 0; i < 4096; i++)
		{
			k = mtl_rtdir_keep(k, i, 1.0f + (float)(i % 7));
		}

		k = mtl_rtdir_keep_rank(k);

		/* every map light occluded: the walk tries all its candidates */
		for (j = 0; mtl_rtdir_walk(j, k.n, seen); j++)
		{
			rays++;
		}

		for (i = 0; mtl_rtdir_dlight_walk(i, 4096); i++)
		{
			rays++;
		}

		CHECK(rays == MTL_RTDIR_MAX_RAYS + MTL_RTDIR_MAX_DLIGHTS && rays == 64);
	}

	for (i = 0; i < MTL_RTDIR_MAX_DLIGHTS; i++)
	{
		CHECK(mtl_rtdir_dlight_bit(i) == (1u << i));
		CHECK(!(bits & mtl_rtdir_dlight_bit(i)));
		bits |= mtl_rtdir_dlight_bit(i);
	}

	CHECK(bits == 0xffffffffu && mtl_rtdir_dlight_bit(32) == 0 && mtl_rtdir_dlight_bit(0xffffffffu) == 0);
	/* the refdef's own bound */
	CHECK(MTL_RTDIR_MAX_DLIGHTS == RM_MAX_DLIGHTS);
}

/* the world's faces for test_self: face 3 is a submodel's, the others a test face */
static int
self_face(void *ctx, long face, rm_rtdir_face_t *out)
{
	(void)ctx;

	if (face == 3)
	{
		return 0;
	}

	face_init(out);
	return 1;
}

/*
 * The emissive faces' own light: qrad3's patch light, its word (rounded,
 * held to MTL_RTDIR_SELF_MAX, none without MTL_RTDIR_SELF_ON) and the
 * per-triangle table the kernel looks it up in, over the map's surface
 * records: an emitter's triangles marked, a dim face's, a submodel's, a
 * face past the records and one whose record leaves the buffer not.
 */
static void
test_self(void)
{
	static rm_rtl_emitters_t e;
	static const rm_surfrec_t recs[6] = {
		{ .first_index = 0, .num_indices = 6 }, { .first_index = 6, .num_indices = 9 },
		{ .first_index = 15, .num_indices = 3 }, { .first_index = 18, .num_indices = 6 },
		{ .first_index = 24, .num_indices = 3 }, { .first_index = 27, .num_indices = 300 },
	};
	const float refl[3] = { 0.8f, 0.4f, 0.2f };
	unsigned table[10], w;
	float rgb[3];
	mtl_rgba_t c;
	int i;

	/* the patch light: 300 x refl x refl / 0.8 */
	CHECK(rm_rtdir_patch_light(300, refl, rgb) == 1);
	CHECK(near(rgb[0], 240, 1e-3) && near(rgb[1], 60, 1e-3) && near(rgb[2], 15, 1e-3));
	CHECK(rm_rtdir_patch_light(3, refl, rgb) == 0 && rgb[0] == 0 && rgb[2] == 0);   /* 2.4: under DIRECT_LIGHT */
	CHECK(rm_rtdir_patch_light(0, refl, rgb) == 0);

	/* the word: rounded, held, none without the flag */
	w = mtl_rtdir_self_pack(240, 60.4f, 15.6f);
	c = mtl_rtdir_self(w);
	CHECK((w & MTL_RTDIR_SELF_ON) && c.r == 240 && c.g == 60 && c.b == 16 && c.a == 1);
	c = mtl_rtdir_self(mtl_rtdir_self_pack(5000, NAN, -3));
	CHECK(c.r == MTL_RTDIR_SELF_MAX && c.g == 0 && c.b == 0 && c.a == 1);
	c = mtl_rtdir_self(0);
	CHECK(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0);
	c = mtl_rtdir_self(~MTL_RTDIR_SELF_ON);
	CHECK(c.r == 0 && c.g == 0 && c.b == 0 && c.a == 0);

	memset(&e, 0, sizeof(e));
	e.ntex = 1;
	e.tex[0].read = 1;
	e.tex[0].mean[0] = 0.8f;
	e.tex[0].mean[1] = 0.4f;
	e.tex[0].mean[2] = 0.2f;
	e.emit[0] = (rm_rtl_emit_t){ 1, 0, 300 };     /* triangles 2 to 4 */
	e.emit[1] = (rm_rtl_emit_t){ 2, 0, 3 };       /* dim */
	e.emit[2] = (rm_rtl_emit_t){ 3, 0, 300 };     /* a submodel's */
	e.emit[3] = (rm_rtl_emit_t){ 4, 0, 600 };     /* triangle 8 */
	e.emit[4] = (rm_rtl_emit_t){ 5, 0, 300 };     /* its record leaves the buffer */
	e.emit[5] = (rm_rtl_emit_t){ 9, 0, 300 };     /* past the records */
	e.n = 6;

	memset(table, 0xff, sizeof(table));
	CHECK(rm_rtdir_self_table(&e, self_face, NULL, recs, 6, table, 10) == 2);

	for (i = 0; i < 10; i++)
	{
		c = mtl_rtdir_self(table[i]);

		if ((i >= 2) && (i <= 4))
		{
			CHECK(c.a == 1 && c.r == 240 && c.g == 60 && c.b == 15);
		}
		else if (i == 8)
		{
			CHECK(c.a == 1 && c.r == 480 && c.g == 120 && c.b == 30);
		}
		else
		{
			CHECK(table[i] == 0);
		}
	}

	/* no table, and no emitters: every triangle 0 */
	CHECK(rm_rtdir_self_table(&e, self_face, NULL, recs, 6, NULL, 10) == -1);
	CHECK(rm_rtdir_self_table(&e, self_face, NULL, recs, 6, table, 0) == -1);
	memset(table, 0xff, sizeof(table));
	CHECK(rm_rtdir_self_table(NULL, self_face, NULL, recs, 6, table, 10) == 0 && table[0] == 0 && table[9] == 0);
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
	test_fit_binding();
	test_q();
	test_fit();
	test_fit_enters();
	test_fit_light();
	test_fit_map();
	test_fit_luxel();
	test_keep();
	test_visible_choice();
	test_pack();
	test_styles();
	test_view_lines();
	test_emit_scale();
	test_reflectivity();
	test_emitter();
	test_pack_emitters();
	test_mixed_choice();
	test_dlight();
	test_dlight_bound();
	test_self();
	test_mode();
	printf("rm_rtdir_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
