/*
 * The host test of the world view's scaler decision and its internal
 * size (TASK109 slice 4): rm_metalfx.c with rm_rtgate.c (and rm_rtfx.c,
 * which the gate's table reads), the pure parts of ref_metal that decide,
 * from the device, the presentation and r_metalfx, whether a MetalFX
 * scaler is made and the size the 3D view is drawn at. Built and run by
 * tests/check-ref-metal.sh with the system compiler; no Metal, no SDL, no
 * game data.
 *
 * Two of #263's acceptance lines, as restated for r_metalfx and
 * r_metalfx_scale:
 * - every effect off and r_metalfx asking for none: no scaler is made,
 *   on any device (test_no_scaler);
 * - where the scaler is unsupported or the device has no ray tracing,
 *   the view's internal scale reads 1, with the report line that says why
 *   (test_no_temporal, test_unsupported, test_no_rt).
 * RM_FX_Begin (rm_fxscene.c) makes no scaler and draws the view at its
 * output's size for RM_FX_OFF; view_size below is that rule.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_metalfx.h"
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

#define UPTO(n) ((1u << (n)) - 1u)          /* Apple families 1 to n */
#define VM_HAS (RM_FX_HAS(0) | RM_FX_HAS(RM_FX_SPATIAL))   /* the VM: the spatial scaler only */
#define HAS_NONE RM_FX_HAS(0)               /* no MetalFX scaler at all */
#define HAS_COUNT (RM_FX_HAS_INTERP << 1)   /* every combination of the RM_FX_HAS bits */
#define RATIO 3.0f                          /* an input at most 3 times smaller than the output */

/*
 * RM_FX_Begin's size: for RM_FX_OFF no scaler is made and the view is
 * drawn at its output's size (returns 0); else the scaler's input,
 * rm_fx_sizes at rm_fx_scale (returns 1).
 */
static int
view_size(int mode, float scale_v, unsigned families, const rm_viewport_t *out, rm_fx_size_t *s)
{
	if (mode == RM_FX_OFF)
	{
		memset(s, 0, sizeof(*s));
		s->iw = s->ow = (int)out->w;
		s->ih = s->oh = (int)out->h;
		return 0;
	}

	return rm_fx_sizes(out, rm_fx_scale(scale_v, rm_fx_default_rows(families), (int)out->h, RATIO), s);
}

/* the view is drawn at its output's size: the internal scale reads 1 */
static int
reads_one(const rm_fx_size_t *s)
{
	return (s->iw == s->ow) && (s->ih == s->oh) && (s->ow > 0) && (s->oh > 0);
}

static const rm_viewport_t out1080 = { 0, 0, 1920, 1080, 0, 1 };

/* the user's r_metalfx values that ask for no scaler, and a scale that would draw below the output */
static const float offs[] = { 0.0f, -0.0f, 0.25f, 0.99f, -1.0f, -INFINITY, NAN };
static const float scales[] = { 0.0f, 0.5f, 0.75f };

/* every effect off (stock) and r_metalfx asking for none: no scaler on any device, at any scale */
static void
test_no_scaler(void)
{
	size_t i, k;
	unsigned has;
	int t, show, below, made = 0;
	rm_fx_size_t s;

	for (has = 0; has < HAS_COUNT; has++)
	{
		for (i = 0; i < sizeof(offs) / sizeof(offs[0]); i++)
		{
			for (t = 0; t < 8; t++)
			{
				const int temporal = t & 1;

				show = (t >> 1) & 1;
				below = (t >> 2) & 1;

				/* stock overlays no r_rt_* effect and leaves r_metalfx the user's */
				const float v = rm_pres_fx(RM_PRES_STOCK, offs[i], temporal, show, below);
				const int mode = rm_fx_view_mode(v, has | HAS_NONE);

				for (k = 0; k < sizeof(scales) / sizeof(scales[0]); k++)
				{
					made += view_size(mode, scales[k], UPTO(9), &out1080, &s);
					made += !reads_one(&s);
				}

				CHECK(mode == RM_FX_OFF);
			}
		}
	}

	CHECK(made == 0);

	/* and the rule is not "never": each mode the device has is made when asked for */
	CHECK(rm_fx_view_mode(1, VM_HAS) == RM_FX_SPATIAL);
	CHECK(rm_fx_view_mode(2, RM_FX_HAS_ALL) == RM_FX_TEMPORAL);
	CHECK(rm_fx_view_mode(2, VM_HAS) == RM_FX_SPATIAL);
	CHECK(rm_fx_view_mode(2, HAS_NONE) == RM_FX_OFF);
	/* the denoised scaler runs as the temporal one with no ray-traced effect counted */
	CHECK(rm_fx_view_mode(3, RM_FX_HAS_ALL) == RM_FX_TEMPORAL);
	CHECK(rm_fx_view_mode(9, RM_FX_HAS_ALL) == RM_FX_TEMPORAL);
	CHECK(view_size(rm_fx_view_mode(2, RM_FX_HAS_ALL), 0.5f, UPTO(9), &out1080, &s) == 1);
	CHECK((s.iw == 960) && (s.ih == 540) && (s.ow == 1920) && (s.oh == 1080));

	/* Retro reads r_metalfx 0 whatever the user's value (and RM_FX_Begin returns before it) */
	CHECK(rm_fx_view_mode(rm_pres_fx(RM_PRES_RETRO, 2, 1, 0, 1), RM_FX_HAS_ALL) == RM_FX_OFF);
}

/*
 * RT-primary on a device that traces rays but has no temporal scaler
 * (the spatial one only, or none): r_metalfx reads 0 with its start-up
 * line, and the view is drawn at its output's size at every scale, for
 * every user value (the spatial scaler is never run as RT-primary's).
 */
static void
test_no_temporal(void)
{
	static const unsigned hass[] = { VM_HAS, HAS_NONE, HAS_NONE | RM_FX_HAS_INTERP };
	static const float users[] = { 0, 1, 2, 3 };
	char buf[256];
	size_t h, u, k;
	int why = -1, below;
	const rm_rtg_t g = rm_rtg_decide(UPTO(7), 1, 0);
	rm_fx_size_t s;

	CHECK(rm_rtg_traces(&g));
	CHECK(rm_pres_decide(RM_PRES_RTPRIMARY, g.path, &why) == RM_PRES_RTPRIMARY);
	CHECK(why == RM_PRES_WHY_OK);

	for (h = 0; h < sizeof(hass) / sizeof(hass[0]); h++)
	{
		const int temporal = (hass[h] & RM_FX_HAS(RM_FX_TEMPORAL)) != 0;

		CHECK(!temporal);

		for (u = 0; u < sizeof(users) / sizeof(users[0]); u++)
		{
			for (below = 0; below < 2; below++)
			{
				const float v = rm_pres_fx(RM_PRES_RTPRIMARY, users[u], temporal, 0, below);
				const int mode = rm_fx_view_mode(v, hass[h]);

				CHECK(v == 0.0f);
				CHECK(mode == RM_FX_OFF);

				for (k = 0; k < sizeof(scales) / sizeof(scales[0]); k++)
				{
					CHECK(view_size(mode, scales[k], UPTO(7), &out1080, &s) == 0);
					CHECK(reads_one(&s));
				}
			}
		}
	}

	/* the report line, at start-up and at vid_restart */
	CHECK(rm_pres_fx_format(RM_PRES_RTPRIMARY, 0, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: presentation: ray traced: r_metalfx reads 0 (off): no MetalFX temporal scaler "
			"on this device"));

	/* with the temporal scaler, below the output, RT-primary's row is the temporal scaler (the rule is not "never") */
	CHECK(rm_fx_view_mode(rm_pres_fx(RM_PRES_RTPRIMARY, 0, 1, 0, 1), RM_FX_HAS_ALL) == RM_FX_TEMPORAL);
}

/*
 * The user's r_metalfx 2 or 3 on a device with no MetalFX scaler: it
 * reads 0 with its line (RM_FX_Read's), and the view is drawn at its
 * output's size at every scale.
 */
static void
test_unsupported(void)
{
	char buf[256];
	int asked, k;
	rm_fx_size_t s;

	for (asked = RM_FX_SPATIAL; asked <= RM_FX_MAX; asked++)
	{
		const int got = rm_fx_supported(asked, HAS_NONE);
		const int mode = rm_fx_view_mode((float)asked, HAS_NONE);

		CHECK(got == RM_FX_OFF);
		CHECK(mode == RM_FX_OFF);

		for (k = 0; k < (int)(sizeof(scales) / sizeof(scales[0])); k++)
		{
			CHECK(view_size(mode, scales[k], UPTO(9), &out1080, &s) == 0);
			CHECK(reads_one(&s));
		}
	}

	/* RM_FX_Read says it where got differs from asked */
	CHECK(rm_fx_supported(2, HAS_NONE) != 2);
	CHECK(rm_fx_fallback_format("r_metalfx", 2, rm_fx_supported(2, HAS_NONE), "is not supported on this device, it reads",
			buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: r_metalfx 2 (temporal) is not supported on this device, it reads 0 (off)"));
}

/*
 * A device with no ray tracing (the VM: no Apple family reported, the
 * spatial scaler only): Enhanced and RT-primary read stock with their
 * line, the gate's lines say the effects and their scales do nothing,
 * and with r_metalfx 0 (the restated acceptance) no scaler is made and
 * the view is drawn at its output's size.
 */
static void
test_no_rt(void)
{
	static const float asked[] = { RM_PRES_ENHANCED, RM_PRES_RTPRIMARY };
	static const char *const lines[] = {
		"ref_metal: presentation: stock; r_presentation 1 (enhanced) reads 0: no ray tracing on this device",
		"ref_metal: presentation: stock; r_presentation 3 (ray traced) reads 0: no ray tracing on this device",
	};
	char buf[256];
	size_t a, k;
	int why, below;
	const rm_rtg_t g = rm_rtg_decide(0, 0, 0);
	rm_fx_size_t s;

	CHECK(!rm_rtg_traces(&g));
	CHECK(rm_rtg_format(&g, buf, sizeof(buf)) == 1);
	CHECK(strstr(buf, "ref_metal: ray tracing: none (") == buf);
	CHECK(strstr(buf, "); the ray-tracing effects read 0") != NULL);
	CHECK(rm_rtg_scales_format(&g, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: ray tracing: none on this device: the effect scales do nothing"));
	CHECK(rm_rtg_reset_format("r_rt_ao", &g, buf, sizeof(buf)) == 1);
	CHECK(!strcmp(buf, "ref_metal: r_rt_ao reads 0: no ray tracing on this device (no Apple GPU family reported)"));

	for (a = 0; a < sizeof(asked) / sizeof(asked[0]); a++)
	{
		why = -1;
		const int mode = rm_pres_decide(asked[a], g.path, &why);

		CHECK(mode == RM_PRES_STOCK);
		CHECK(why == RM_PRES_WHY_NORT);
		CHECK(rm_pres_format(asked[a], mode, why, buf, sizeof(buf)) == 1);
		CHECK(!strcmp(buf, lines[a]));
		/* stock has no RT-primary scaler line */
		CHECK(rm_pres_fx_format(mode, 0, buf, sizeof(buf)) == 0);

		for (below = 0; below < 2; below++)
		{
			const float v = rm_pres_fx(mode, 0.0f, 0, 0, below);
			const int fx = rm_fx_view_mode(v, VM_HAS);

			CHECK(v == 0.0f);
			CHECK(fx == RM_FX_OFF);

			for (k = 0; k < sizeof(scales) / sizeof(scales[0]); k++)
			{
				CHECK(view_size(fx, scales[k], 0, &out1080, &s) == 0);
				CHECK(reads_one(&s));
			}
		}
	}
}

int
main(void)
{
	test_no_scaler();
	test_no_temporal();
	test_unsupported();
	test_no_rt();

	printf("rm_fxgate_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
