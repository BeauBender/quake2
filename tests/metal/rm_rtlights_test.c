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
 * The host test of the ray-traced presentation's data (TASK187, PM5):
 * rm_rtlights.c alone, with no SDL, no Metal and no game data. The
 * entity-lump parser on generated lumps (every key, the defaults, a
 * spotlight with its target and one whose target is missing, each
 * malformed class, a lump cut at every byte, a length of 0, more lights
 * than the cap), each lump copied into a heap block of exactly its length
 * with no NUL after it, so an over-read is the sanitizer's; the numbers;
 * the emissive faces, the .wal mean, the sky mean and the stats line. Run
 * by tests/check-ref-metal.sh from the repository's top directory.
 *
 * =======================================================================
 */

#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/client/refresh/metal/header/rm_rtlights.h"

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

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-4)

static rm_rtl_lights_t L;
static rm_rtl_emitters_t E;

/* parses the first len bytes of s from a heap block of exactly len bytes: no NUL follows them */
static int
parse_n(const char *s, size_t len)
{
	char *p = len ? malloc(len) : NULL;
	int r;

	if (len && !p)
	{
		printf("FAIL out of memory\n");
		exit(1);
	}

	if (len)
	{
		memcpy(p, s, len);
	}

	r = rm_rtl_parse(p, len, &L);
	free(p);
	return r;
}

static int
parse(const char *s)
{
	return parse_n(s, strlen(s));
}

static const char normal_map[] =
	"{\n\"classname\" \"worldspawn\"\n\"message\" \"a test\"\n\"sky\" \"unit1_\"\n}\n"
	"// a comment, as the game allows\n"
	"{\n\"classname\" \"light\"\n\"origin\" \"64 -32.5 128\"\n\"light\" \"250\"\n\"_color\" \"1 0.5 0.25\"\n"
	"\"style\" \"3\"\n\"target\" \"t1\"\n\"_cone\" \"20\"\n}\n"
	"{\n\"classname\" \"info_null\"\n\"origin\" \"64 -32.5 0\"\n\"targetname\" \"t1\"\n}\n"
	"{\n\"classname\" \"light\"\n\"origin\" \"0 0 0\"\n\"_light\" \"150\"\n\"_style\" \"10\"\n\"style\" \"2\"\n}\n"
	"{\n\"classname\" \"monster_soldier\"\n\"origin\" \"8 8 8\"\n\"angle\" \"90\"\n}\n";

static void
test_normal(void)
{
	const rm_rtl_light_t *a = &L.light[0], *b = &L.light[1];

	CHECK(parse(normal_map) == RM_RTL_OK);
	CHECK(L.entities == 5);
	CHECK(L.nlights == 2);
	CHECK(L.spots == 1);
	CHECK(L.styled == 2);
	CHECK(L.untargeted == 0);
	CHECK(L.skipped_long == 0 && L.skipped_number == 0 && L.over_cap == 0);

	/* every key */
	CHECK(NEAR(a->origin[0], 64) && NEAR(a->origin[1], -32.5) && NEAR(a->origin[2], 128));
	CHECK(NEAR(a->intensity, 250));
	CHECK(NEAR(a->color[0], 1) && NEAR(a->color[1], 0.5) && NEAR(a->color[2], 0.25));
	CHECK(a->style == 3);
	CHECK(a->spot == 1);
	CHECK(NEAR(a->cone, 20));
	CHECK(!strcmp(a->target, "t1"));
	CHECK(a->target_state == RM_RTL_TARGET_FOUND);
	CHECK(NEAR(a->dir[0], 0) && NEAR(a->dir[1], 0) && NEAR(a->dir[2], -1));

	/* _light when light is absent; _style over style */
	CHECK(NEAR(b->intensity, 150));
	CHECK(b->style == 10);
	CHECK(b->spot == 0 && b->target_state == RM_RTL_TARGET_NONE);
	CHECK(NEAR(b->dir[0], 0) && NEAR(b->dir[1], 0) && NEAR(b->dir[2], 0));
}

static void
test_defaults(void)
{
	const rm_rtl_light_t *a = &L.light[0];

	CHECK(parse("{ \"classname\" \"light\" }") == RM_RTL_OK);
	CHECK(L.nlights == 1);
	CHECK(NEAR(a->origin[0], 0) && NEAR(a->origin[1], 0) && NEAR(a->origin[2], 0));
	CHECK(NEAR(a->intensity, RM_RTL_DEFAULT_LIGHT));
	CHECK(NEAR(a->color[0], 1) && NEAR(a->color[1], 1) && NEAR(a->color[2], 1));
	CHECK(a->style == 0 && !a->spot && NEAR(a->cone, 0));

	/* light 0 falls back as qrad3's does; a style outside the lightstyles reads 0; a colour scaled to its largest channel */
	CHECK(parse("{ classname light light 0 style 300 _color \"2 4 1\" }") == RM_RTL_OK);
	CHECK(L.nlights == 1);
	CHECK(NEAR(a->intensity, RM_RTL_DEFAULT_LIGHT));
	CHECK(a->style == 0 && L.styled == 0);
	CHECK(NEAR(a->color[0], 0.5) && NEAR(a->color[1], 1) && NEAR(a->color[2], 0.25));

	CHECK(parse("{ \"classname\" \"light\" \"style\" \"-1\" }") == RM_RTL_OK && L.light[0].style == 0);

	/* a light_spot with no target is aimed by its angle, with the default cone */
	CHECK(parse("{ \"classname\" \"light_spot\" \"angle\" \"90\" }") == RM_RTL_OK);
	CHECK(L.spots == 1 && NEAR(a->cone, RM_RTL_DEFAULT_CONE) && a->target_state == RM_RTL_TARGET_NONE);
	CHECK(NEAR(a->dir[0], 0) && NEAR(a->dir[1], 1) && NEAR(a->dir[2], 0));
	CHECK(parse("{ \"classname\" \"light_spot\" \"angle\" \"-1\" }") == RM_RTL_OK && NEAR(a->dir[2], 1));
	CHECK(parse("{ \"classname\" \"light_spot\" \"angle\" \"-2\" }") == RM_RTL_OK && NEAR(a->dir[2], -1));

	/* qrad3's prefix: light_mine1 is a light to it; lightning is too, a worldspawn is not */
	CHECK(parse("{ \"classname\" \"light_mine1\" } { \"classname\" \"worldspawn\" \"light\" \"5\" }") == RM_RTL_OK);
	CHECK(L.nlights == 1 && L.entities == 2);
}

static void
test_targets(void)
{
	const rm_rtl_light_t *a = &L.light[0];

	/* the target after the light, and the first of two of the same name */
	CHECK(parse("{ \"classname\" \"light\" \"origin\" \"0 0 0\" \"target\" \"t2\" }"
			"{ \"classname\" \"path_corner\" \"origin\" \"10 0 0\" \"targetname\" \"t2\" }"
			"{ \"classname\" \"path_corner\" \"origin\" \"0 10 0\" \"targetname\" \"t2\" }") == RM_RTL_OK);
	CHECK(L.nlights == 1 && L.spots == 1 && L.untargeted == 0 && L.ntargets == 2);
	CHECK(a->target_state == RM_RTL_TARGET_FOUND);
	CHECK(NEAR(a->dir[0], 1) && NEAR(a->dir[1], 0) && NEAR(a->dir[2], 0));

	/* a target at the light's own origin points down */
	CHECK(parse("{ \"classname\" \"light\" \"origin\" \"5 5 5\" \"target\" \"t3\" }"
			"{ \"targetname\" \"t3\" \"origin\" \"5 5 5\" }") == RM_RTL_OK);
	CHECK(a->target_state == RM_RTL_TARGET_FOUND && NEAR(a->dir[2], -1));

	/* a missing target */
	CHECK(parse("{ \"classname\" \"light\" \"target\" \"nowhere\" }"
			"{ \"classname\" \"info_null\" \"targetname\" \"elsewhere\" }") == RM_RTL_OK);
	CHECK(L.nlights == 1 && L.spots == 1 && L.untargeted == 1);
	CHECK(a->target_state == RM_RTL_TARGET_MISSING);
	CHECK(NEAR(a->dir[0], 0) && NEAR(a->dir[1], 0) && NEAR(a->dir[2], -1));

	/* a targetname of an earlier lump is not matched: only this lump's targets are looked up */
	CHECK(parse("{ \"targetname\" \"stale\" \"origin\" \"1 0 0\" }") == RM_RTL_OK && L.ntargets == 1);
	CHECK(parse("{ \"classname\" \"light\" \"target\" \"stale\" }") == RM_RTL_OK);
	CHECK(L.ntargets == 0 && L.untargeted == 1 && a->target_state == RM_RTL_TARGET_MISSING);

	/* a target name too long to keep matches nothing, even a targetname that is the same */
	{
		static char lump[1024];
		char name[RM_RTL_NAME + 8];

		memset(name, 'n', sizeof(name) - 1);
		name[sizeof(name) - 1] = '\0';
		snprintf(lump, sizeof(lump), "{ \"classname\" \"light\" \"target\" \"%s\" } { \"targetname\" \"%s\" }",
				name, name);
		CHECK(parse(lump) == RM_RTL_OK);
		CHECK(L.nlights == 1 && L.untargeted == 1 && L.ntargets == 0);
		CHECK(L.light[0].target_state == RM_RTL_TARGET_MISSING);
	}
}

/* a lump of one light whose key or value is n bytes long */
static void
long_case(int key, size_t n, int want_skipped)
{
	static char lump[8192], s[4096];

	memset(s, 'x', n);
	s[n] = '\0';

	if (key)
	{
		snprintf(lump, sizeof(lump), "{ \"classname\" \"light\" \"%s\" \"1\" } { \"classname\" \"light\" }", s);
	}
	else
	{
		snprintf(lump, sizeof(lump), "{ \"classname\" \"light\" \"message\" \"%s\" } { \"classname\" \"light\" }", s);
	}

	CHECK(parse(lump) == RM_RTL_OK);
	CHECK(L.entities == 2);
	CHECK(L.skipped_long == want_skipped);
	CHECK(L.nlights == 2 - want_skipped);
}

static void
test_malformed(void)
{
	/* an unterminated quote refuses the lump, the lights before it with it */
	CHECK(parse("{ \"classname\" \"light\" } { \"classname\" \"light") == RM_RTL_UNTERMINATED);
	CHECK(L.nlights == 0 && L.refused == RM_RTL_UNTERMINATED);
	CHECK(parse("{ \"classname\" \"light\" } { \"classn") == RM_RTL_UNTERMINATED);

	/* an over-long key or value skips its entity, not the lump */
	long_case(1, RM_RTL_MAX_KEY - 1, 0);
	long_case(1, RM_RTL_MAX_KEY, 1);
	long_case(0, RM_RTL_MAX_VALUE - 1, 0);
	long_case(0, RM_RTL_MAX_VALUE, 1);
	long_case(0, 3000, 1);

	/* an over-deep brace */
	CHECK(parse("{ \"classname\" \"light\" { \"origin\" \"0 0 0\" } }") == RM_RTL_TOO_DEEP);
	CHECK(L.nlights == 0);
	CHECK(parse("{ \"classname\" { } }") == RM_RTL_TOO_DEEP);

	/* unbalanced braces: a '}' or a key outside an entity, a key without its value, an entity left open */
	CHECK(parse("{ \"classname\" \"light\" } }") == RM_RTL_UNBALANCED);
	CHECK(L.nlights == 0);
	CHECK(parse("\"classname\" \"light\"") == RM_RTL_UNBALANCED);
	CHECK(parse("{ \"classname\" \"light\" \"origin\" }") == RM_RTL_UNBALANCED);
	CHECK(parse("{ \"classname\" \"light\"") == RM_RTL_UNBALANCED);
	CHECK(parse("{ \"classname\" \"light\" \"origin\"") == RM_RTL_UNBALANCED);

	/* a number that does not parse skips its entity */
	CHECK(parse("{ \"classname\" \"light\" \"origin\" \"1 2 x\" } { \"classname\" \"light\" }") == RM_RTL_OK);
	CHECK(L.skipped_number == 1 && L.nlights == 1);
	CHECK(parse("{ \"classname\" \"light\" \"light\" \"bright\" }") == RM_RTL_OK && L.skipped_number == 1);
	CHECK(parse("{ \"classname\" \"light\" \"_color\" \"1 1\" }") == RM_RTL_OK && L.skipped_number == 1);
	CHECK(parse("{ \"classname\" \"light\" \"_color\" \"1 1 1 1\" }") == RM_RTL_OK && L.skipped_number == 1);
	CHECK(parse("{ \"classname\" \"light\" \"light\" \"1e3\" }") == RM_RTL_OK && L.skipped_number == 1);
	CHECK(parse("{ \"classname\" \"light\" \"_cone\" \"\" }") == RM_RTL_OK && L.skipped_number == 1);
	CHECK(parse("{ \"classname\" \"light\" \"style\" \"0x10\" }") == RM_RTL_OK && L.skipped_number == 1);
	CHECK(parse("{ \"classname\" \"info_null\" \"targetname\" \"t\" \"origin\" \"a b c\" }") == RM_RTL_OK);
	CHECK(L.skipped_number == 1 && L.ntargets == 0);

	/* a number too large for a float */
	{
		static char lump[512], big[64];

		memset(big, '9', 60);
		big[60] = '\0';
		snprintf(lump, sizeof(lump), "{ \"classname\" \"light\" \"light\" \"%s\" }", big);
		CHECK(parse(lump) == RM_RTL_OK && L.skipped_number == 1 && L.nlights == 0);
	}

	/* the keys of an entity that is neither a light nor a target are not read as numbers */
	CHECK(parse("{ \"classname\" \"worldspawn\" \"origin\" \"what\" }") == RM_RTL_OK && L.skipped_number == 0);
}

/* a lump cut at every byte: no read past the cut, and either a whole lump or a refusal */
static void
test_cut(void)
{
	size_t n, len = strlen(normal_map);
	int bad = 0;

	for (n = 0; n <= len; n++)
	{
		int r = parse_n(normal_map, n);

		if ((r != RM_RTL_OK) && (r != RM_RTL_UNTERMINATED) && (r != RM_RTL_UNBALANCED))
		{
			bad++;
		}

		if ((r != RM_RTL_OK) && (L.nlights != 0))
		{
			bad++;
		}
	}

	CHECK(bad == 0);

	/* mid-token: inside a quoted value, and right after a key */
	CHECK(parse_n(normal_map, strstr(normal_map, "64 -32.5 128") - normal_map + 3) == RM_RTL_UNTERMINATED);
	CHECK(parse_n(normal_map, strstr(normal_map, "\"_cone\"") - normal_map + 7) == RM_RTL_UNBALANCED);

	/* a NUL ends the lump, as it ends the game's string */
	{
		static const char nul[] = "{ \"classname\" \"light\" }\0{ \"classname\" \"light\" }";

		CHECK(parse_n(nul, sizeof(nul) - 1) == RM_RTL_OK && L.nlights == 1);
		CHECK(parse_n("{ \"classname\" \"li\0ght\" }", 24) == RM_RTL_UNTERMINATED);
	}
}

static void
test_empty(void)
{
	CHECK(parse_n("", 0) == RM_RTL_OK && L.nlights == 0 && L.entities == 0);
	CHECK(rm_rtl_parse(NULL, 0, &L) == RM_RTL_OK && L.nlights == 0);
	CHECK(rm_rtl_parse(NULL, 100, &L) == RM_RTL_OK && L.nlights == 0);
	CHECK(parse("   \n\t // only a comment") == RM_RTL_OK && L.entities == 0);
	CHECK(parse("{ }") == RM_RTL_OK && L.entities == 1 && L.nlights == 0);
}

static void
test_cap(void)
{
	static const char one[] = "{ \"classname\" \"light\" \"origin\" \"1 2 3\" }\n";
	size_t n = RM_RTL_MAX_LIGHTS + 37, i, sz = n * (sizeof(one) - 1);
	char *lump = malloc(sz);

	if (!lump)
	{
		printf("FAIL out of memory\n");
		exit(1);
	}

	for (i = 0; i < n; i++)
	{
		memcpy(lump + i * (sizeof(one) - 1), one, sizeof(one) - 1);
	}

	CHECK(parse_n(lump, sz) == RM_RTL_OK);
	CHECK(L.nlights == RM_RTL_MAX_LIGHTS);
	CHECK(L.over_cap == 37);
	CHECK(L.entities == (long)n);
	CHECK(NEAR(L.light[RM_RTL_MAX_LIGHTS - 1].origin[2], 3));
	free(lump);
}

static void
test_numbers(void)
{
	float v[4];

	CHECK(rm_rtl_numbers(" -12.5\t+3 .5 ", v, 3) && NEAR(v[0], -12.5) && NEAR(v[1], 3) && NEAR(v[2], 0.5));
	CHECK(rm_rtl_numbers("7.", v, 1) && NEAR(v[0], 7));
	CHECK(!rm_rtl_numbers("", v, 1));
	CHECK(!rm_rtl_numbers("-", v, 1));
	CHECK(!rm_rtl_numbers(".", v, 1));
	CHECK(!rm_rtl_numbers("1,5", v, 1));
	CHECK(!rm_rtl_numbers("1 2", v, 1));
	CHECK(!rm_rtl_numbers("1", v, 2));
	CHECK(!rm_rtl_numbers("1-2", v, 1));
	CHECK(!rm_rtl_numbers("nan", v, 1));
	CHECK(!rm_rtl_numbers("inf", v, 1));
	CHECK(!rm_rtl_numbers("1", v, 5));

	/* no locale: a decimal-comma locale, where there is one, changes nothing */
	if (setlocale(LC_NUMERIC, "de_DE.UTF-8") || setlocale(LC_NUMERIC, "fr_FR.UTF-8"))
	{
		CHECK(rm_rtl_numbers("1.5", v, 1) && NEAR(v[0], 1.5));
		CHECK(!rm_rtl_numbers("1,5", v, 1));
		setlocale(LC_NUMERIC, "C");
	}
}

static void
put32(unsigned char *p, unsigned v)
{
	p[0] = v & 0xff;
	p[1] = (v >> 8) & 0xff;
	p[2] = (v >> 16) & 0xff;
	p[3] = (v >> 24) & 0xff;
}

static void
texinfo(unsigned char *ti, int i, int flags, int value, const char *name)
{
	unsigned char *p = ti + i * RM_RTL_TEXINFO_SIZE;

	memset(p, 0, RM_RTL_TEXINFO_SIZE);
	put32(p + RM_RTL_TEXINFO_FLAGS, (unsigned)flags);
	put32(p + RM_RTL_TEXINFO_VALUE, (unsigned)value);
	memcpy(p + RM_RTL_TEXINFO_NAME, name, strlen(name) < RM_RTL_TEXNAME ? strlen(name) : RM_RTL_TEXNAME);
}

static void
face(unsigned char *f, int i, int ti)
{
	unsigned char *p = f + i * RM_RTL_FACE_SIZE;

	memset(p, 0, RM_RTL_FACE_SIZE);
	p[RM_RTL_FACE_TEXINFO] = ti & 0xff;
	p[RM_RTL_FACE_TEXINFO + 1] = (ti >> 8) & 0xff;
}

static void
test_emitters(void)
{
	unsigned char ti[5 * RM_RTL_TEXINFO_SIZE], f[9 * RM_RTL_FACE_SIZE];
	char name32[RM_RTL_TEXNAME + 1];

	memset(name32, 'w', RM_RTL_TEXNAME);
	name32[RM_RTL_TEXNAME] = '\0';

	texinfo(ti, 0, 0, 0, "e1u1/floor");
	texinfo(ti, 1, RM_RTL_SURF_LIGHT, 300, "e1u1/light1");
	texinfo(ti, 2, RM_RTL_SURF_LIGHT | RM_RTL_SURF_SKY, 200, "e1u1/sky");
	texinfo(ti, 3, RM_RTL_SURF_LIGHT, 0, "e1u1/light2");
	texinfo(ti, 4, RM_RTL_SURF_LIGHT, 50, name32);   /* 32 bytes, no NUL in the lump */

	face(f, 0, 0);
	face(f, 1, 1);
	face(f, 2, 2);
	face(f, 3, 3);
	face(f, 4, 1);
	face(f, 5, 4);
	face(f, 6, 5);    /* past the texinfo lump */
	face(f, 7, -1);
	face(f, 8, 4);

	/* the lump's trailing partial record is ignored */
	rm_rtl_emitters(ti, sizeof(ti) + 0, f, sizeof(f) - 1, &E);
	CHECK(E.n == 3 && E.ntex == 2 && E.bad_texinfo == 2);
	rm_rtl_emitters(ti, sizeof(ti), f, sizeof(f), &E);
	CHECK(E.n == 4);
	CHECK(E.ntex == 2);
	CHECK(E.bad_texinfo == 2);
	CHECK(E.emit[0].face == 1 && E.emit[0].tex == 0 && E.emit[0].value == 300);
	CHECK(E.emit[1].face == 4 && E.emit[1].tex == 0);
	CHECK(E.emit[2].face == 5 && E.emit[2].tex == 1 && E.emit[2].value == 50);
	CHECK(E.emit[3].face == 8 && E.emit[3].tex == 1);
	CHECK(!strcmp(E.tex[0].name, "e1u1/light1") && !strcmp(E.tex[1].name, name32));
	CHECK(!E.tex[0].read && NEAR(E.tex[0].mean[0], RM_RTL_UNREAD_GREY));

	rm_rtl_emitters(NULL, 0, NULL, 0, &E);
	CHECK(E.n == 0 && E.ntex == 0);
}

static void
test_wal(void)
{
	static unsigned palette[256];
	unsigned char wal[100 + 4];
	float m[3] = { -1, -1, -1 };
	int i;

	for (i = 0; i < 256; i++)
	{
		unsigned char *c = (unsigned char *)&palette[i];

		c[0] = (unsigned char)i;
		c[1] = 0;
		c[2] = 255;
		c[3] = 255;
	}

	memset(wal, 0, sizeof(wal));
	put32(wal + 32, 2);
	put32(wal + 36, 2);
	put32(wal + 40, 100);
	wal[100] = 0;
	wal[101] = 255;
	wal[102] = 0;
	wal[103] = 255;

	CHECK(rm_rtl_wal_mean(wal, sizeof(wal), palette, m) == 1);
	CHECK(NEAR(m[0], 0.5) && NEAR(m[1], 0) && NEAR(m[2], 1));

	/* a body one byte short, a header short, an offset past the end, a size of 0 or too large */
	m[0] = -1;
	CHECK(rm_rtl_wal_mean(wal, sizeof(wal) - 1, palette, m) == 0 && m[0] == -1);
	CHECK(rm_rtl_wal_mean(wal, 99, palette, m) == 0);
	put32(wal + 40, 0xffffffffu);
	CHECK(rm_rtl_wal_mean(wal, sizeof(wal), palette, m) == 0);
	put32(wal + 40, 100);
	put32(wal + 32, 0);
	CHECK(rm_rtl_wal_mean(wal, sizeof(wal), palette, m) == 0);
	put32(wal + 32, 0x10000u);
	put32(wal + 36, 0x10000u);
	CHECK(rm_rtl_wal_mean(wal, sizeof(wal), palette, m) == 0);
	CHECK(rm_rtl_wal_mean(NULL, 0, palette, m) == 0);

	/* a block shorter than the 100-byte header is refused, even when its body would fit */
	{
		unsigned char *s = malloc(99);

		if (!s)
		{
			printf("FAIL out of memory\n");
			exit(1);
		}

		memset(s, 0, 99);
		put32(s + 32, 1);
		put32(s + 36, 1);
		put32(s + 40, 98);
		CHECK(rm_rtl_wal_mean(s, 99, palette, m) == 0);
		free(s);
	}
}

static void
test_sky_and_line(void)
{
	const float means[6][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 1 }, { 1, 1, 1 } };
	const int have[6] = { 1, 1, 0, 0, 1, 0 }, none[6] = { 0 };
	float sky[3];
	char line[512], tiny[16];

	CHECK(rm_rtl_sky_mean(means, have, 6, sky) == 3);
	CHECK(NEAR(sky[0], 1.0 / 3) && NEAR(sky[1], 1.0 / 3) && NEAR(sky[2], 1.0 / 3));
	CHECK(rm_rtl_sky_mean(means, none, 6, sky) == 0 && sky[0] == 0);

	CHECK(parse(normal_map) == RM_RTL_OK);
	memset(&E, 0, sizeof(E));
	E.n = 5;
	E.ntex = 2;
	E.tex[0].read = 1;
	sky[0] = 0.5f;
	sky[1] = 1.5f;
	sky[2] = -1;
	CHECK(rm_rtl_format("maps/test.bsp", &L, &E, sky, line, sizeof(line)) == 1);
	CHECK(!strcmp(line, "ref_metal: rt-data: maps/test.bsp: lights 2 (spot 1, styled 2, untargeted 0) "
			"emitters 5 (textures 2, unread 1) sky 128,255,0 refused 0 (past the cap 0)"));
	CHECK(rm_rtl_format("maps/test.bsp", &L, &E, NULL, line, sizeof(line)) == 1 && strstr(line, " sky none "));

	CHECK(parse("{ \"a\" \"b") == RM_RTL_UNTERMINATED);
	CHECK(rm_rtl_format("m", &L, &E, NULL, line, sizeof(line)) == 1);
	CHECK(strstr(line, "refused the lump (an unterminated quote)") != NULL);
	CHECK(rm_rtl_format("m", &L, &E, NULL, tiny, sizeof(tiny)) == 0 && tiny[0] == '\0');
	CHECK(rm_rtl_format("m", &L, &E, NULL, tiny, 0) == 0);
}

int
main(void)
{
	test_normal();
	test_defaults();
	test_targets();
	test_malformed();
	test_cut();
	test_empty();
	test_cap();
	test_numbers();
	test_emitters();
	test_wal();
	test_sky_and_line();

	printf("rm_rtlights_test: %d checks, %d failed\n", checks, failures);
	return failures ? 1 : 0;
}
