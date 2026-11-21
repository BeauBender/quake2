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
 * The pure part of the ray-traced presentation's data (TASK187, PM5):
 * the entity-lump parser, the emissive faces and their textures' average
 * colours, the sky's colour and the r_rt_stats 1 line. Neither SDL nor
 * Metal nor an engine header; host-tested by tests/metal/rm_rtlights_test.c.
 * The lump is untrusted: every read below is bounded by its length.
 *
 * =======================================================================
 */

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rtlights.h"

/* ---- the tokens ----------------------------------------------------------------- */

enum
{
	TOK_END,
	TOK_OPEN,
	TOK_CLOSE,
	TOK_STRING,
	TOK_LONG,          /* a string longer than the buffer: read past, not kept */
	TOK_UNTERMINATED   /* a quoted string the lump ends inside */
};

/* 1 for the end of the lump: its length, or a NUL byte (where the game's string ends) */
static int
rtl_end(const char *p, size_t len, size_t i)
{
	return (i >= len) || (p[i] == '\0');
}

/*
 * The next token from *pos, as the game's COM_Parse reads the lump:
 * blanks and // comments skipped, '{' and '}' alone, a quoted string, or a
 * word up to the next blank. The string goes to out (outsz bytes with its
 * NUL); a longer one is TOK_LONG.
 */
static int
rtl_token(const char *p, size_t len, size_t *pos, char *out, size_t outsz)
{
	size_t i = *pos, n = 0;
	int quoted;

	out[0] = '\0';

	for (;;)
	{
		while (!rtl_end(p, len, i) && ((unsigned char)p[i] <= ' '))
		{
			i++;
		}

		if (rtl_end(p, len, i))
		{
			*pos = i;
			return TOK_END;
		}

		if ((p[i] == '/') && !rtl_end(p, len, i + 1) && (p[i + 1] == '/'))
		{
			while (!rtl_end(p, len, i) && (p[i] != '\n'))
			{
				i++;
			}

			continue;
		}

		break;
	}

	if ((p[i] == '{') || (p[i] == '}'))
	{
		*pos = i + 1;
		return (p[i] == '{') ? TOK_OPEN : TOK_CLOSE;
	}

	quoted = (p[i] == '"');

	if (quoted)
	{
		i++;
	}

	while (!rtl_end(p, len, i) && (quoted ? (p[i] != '"') : ((unsigned char)p[i] > ' ')))
	{
		if (n + 1 < outsz)
		{
			out[n] = p[i];
		}

		n++;
		i++;
	}

	if (quoted)
	{
		if (rtl_end(p, len, i))
		{
			*pos = i;
			out[0] = '\0';
			return TOK_UNTERMINATED;
		}

		i++;   /* the closing quote */
	}

	*pos = i;

	if (n >= outsz)
	{
		out[0] = '\0';
		return TOK_LONG;
	}

	out[n] = '\0';
	return TOK_STRING;
}

/* ---- the numbers ---------------------------------------------------------------- */

static int
rtl_blank(char c)
{
	return (c == ' ') || (c == '\t') || (c == '\r') || (c == '\n');
}

static int
rtl_digit(char c)
{
	return (c >= '0') && (c <= '9');
}

int
rm_rtl_numbers(const char *s, float *out, int n)
{
	float v[4];
	size_t i = 0;
	int k;

	if (!s || !out || (n < 1) || (n > 4))
	{
		return 0;
	}

	for (k = 0; k < n; k++)
	{
		double d = 0, scale = 1;
		int neg = 0, digits = 0;

		while (rtl_blank(s[i]))
		{
			i++;
		}

		if ((s[i] == '-') || (s[i] == '+'))
		{
			neg = (s[i] == '-');
			i++;
		}

		while (rtl_digit(s[i]))
		{
			d = d * 10 + (s[i] - '0');
			digits++;
			i++;
		}

		if (s[i] == '.')
		{
			i++;

			while (rtl_digit(s[i]))
			{
				scale /= 10;
				d += (s[i] - '0') * scale;
				digits++;
				i++;
			}
		}

		/* a digit at least, finite as a float, and a blank or the end after it */
		if (!digits || !isfinite(d) || (d > FLT_MAX) || ((s[i] != '\0') && !rtl_blank(s[i])))
		{
			return 0;
		}

		v[k] = (float)(neg ? -d : d);
	}

	while (rtl_blank(s[i]))
	{
		i++;
	}

	if (s[i] != '\0')
	{
		return 0;
	}

	memcpy(out, v, (size_t)n * sizeof(float));
	return 1;
}

/* ---- the entities ---------------------------------------------------------------- */

enum
{
	K_CLASSNAME,
	K_ORIGIN,
	K_LIGHT,
	K__LIGHT,
	K__COLOR,
	K_STYLE,
	K__STYLE,
	K_TARGET,
	K_TARGETNAME,
	K__CONE,
	K_ANGLE,
	K_COUNT
};

static const char *const rtl_keys[K_COUNT] = {
	"classname", "origin", "light", "_light", "_color", "style", "_style", "target", "targetname", "_cone", "angle"
};

typedef struct
{
	char val[K_COUNT][RM_RTL_MAX_VALUE];
	int have[K_COUNT];
	int too_long;
} rtl_entity_t;

/* a key's single number, 0 when absent; 0 when it is present and does not parse */
static int
rtl_number(const rtl_entity_t *e, int k, float *out)
{
	*out = 0;
	return !e->have[k] || rm_rtl_numbers(e->val[k], out, 1);
}

static void
rtl_unit(float *v)
{
	double l = sqrt((double)v[0] * v[0] + (double)v[1] * v[1] + (double)v[2] * v[2]);

	if (!(l > 0) || !isfinite(l))
	{
		v[0] = 0;
		v[1] = 0;
		v[2] = -1;
		return;
	}

	v[0] = (float)(v[0] / l);
	v[1] = (float)(v[1] / l);
	v[2] = (float)(v[2] / l);
}

/* one entity read whole: a light, a target, both or neither */
static void
rtl_entity(rm_rtl_lights_t *out, const rtl_entity_t *e)
{
	float origin[3] = { 0, 0, 0 }, light, _light, color[3] = { 1, 1, 1 }, style, _style, cone, angle;
	int is_light, is_target;
	rm_rtl_light_t *l;

	out->entities++;

	if (e->too_long)
	{
		out->skipped_long++;
		return;
	}

	is_light = e->have[K_CLASSNAME] && !strncmp(e->val[K_CLASSNAME], "light", 5);
	is_target = e->have[K_TARGETNAME] && (e->val[K_TARGETNAME][0] != '\0');

	if (!is_light && !is_target)
	{
		return;
	}

	if ((e->have[K_ORIGIN] && !rm_rtl_numbers(e->val[K_ORIGIN], origin, 3)) ||
		(is_light && (!rtl_number(e, K_LIGHT, &light) || !rtl_number(e, K__LIGHT, &_light) ||
			(e->have[K__COLOR] && !rm_rtl_numbers(e->val[K__COLOR], color, 3)) ||
			!rtl_number(e, K_STYLE, &style) || !rtl_number(e, K__STYLE, &_style) ||
			!rtl_number(e, K__CONE, &cone) || !rtl_number(e, K_ANGLE, &angle))))
	{
		out->skipped_number++;
		return;
	}

	if (is_target && (strlen(e->val[K_TARGETNAME]) < RM_RTL_NAME) && (out->ntargets < RM_RTL_MAX_TARGETS))
	{
		rm_rtl_target_t *t = &out->target[out->ntargets++];

		memcpy(t->name, e->val[K_TARGETNAME], strlen(e->val[K_TARGETNAME]) + 1);
		memcpy(t->origin, origin, sizeof(origin));
	}

	if (!is_light)
	{
		return;
	}

	if (out->nlights >= RM_RTL_MAX_LIGHTS)
	{
		out->over_cap++;
		return;
	}

	l = &out->light[out->nlights++];
	memset(l, 0, sizeof(*l));
	memcpy(l->origin, origin, sizeof(origin));

	/* qrad3's CreateDirectLights: light, else _light, else 300 */
	l->intensity = light ? light : (_light ? _light : RM_RTL_DEFAULT_LIGHT);

	/* qrad3's ColorNormalize: divided by the largest channel, left as given when that is 0 */
	{
		float max = color[0];

		max = (color[1] > max) ? color[1] : max;
		max = (color[2] > max) ? color[2] : max;
		l->color[0] = max ? color[0] / max : color[0];
		l->color[1] = max ? color[1] / max : color[1];
		l->color[2] = max ? color[2] / max : color[2];
	}

	/* _style, else style; one outside the lightstyles reads 0 (checked before the conversion) */
	style = _style ? _style : style;
	l->style = ((style >= 0) && (style < RM_RTL_STYLES)) ? (int)style : 0;

	l->spot = !strcmp(e->val[K_CLASSNAME], "light_spot") || (e->have[K_TARGET] && (e->val[K_TARGET][0] != '\0'));

	if (l->spot)
	{
		l->cone = cone ? cone : RM_RTL_DEFAULT_CONE;

		if (e->have[K_TARGET] && (e->val[K_TARGET][0] != '\0'))
		{
			/* resolved once the whole lump is read; a name too long to keep matches nothing */
			if (strlen(e->val[K_TARGET]) < RM_RTL_NAME)
			{
				memcpy(l->target, e->val[K_TARGET], strlen(e->val[K_TARGET]) + 1);
			}
			else
			{
				l->target_state = RM_RTL_TARGET_MISSING;
			}
		}
		else if (angle == -1)
		{
			l->dir[2] = 1;
		}
		else if (angle == -2)
		{
			l->dir[2] = -1;
		}
		else
		{
			l->dir[0] = (float)cos(angle * M_PI / 180.0);
			l->dir[1] = (float)sin(angle * M_PI / 180.0);
		}
	}

	out->spots += l->spot;
	out->styled += (l->style != 0);
}

/* each spotlight's target: the first entity of that targetname, at most RM_RTL_MAX_TARGETS compared */
static void
rtl_resolve(rm_rtl_lights_t *out)
{
	long i, j;

	for (i = 0; i < out->nlights; i++)
	{
		rm_rtl_light_t *l = &out->light[i];

		if (!l->spot || ((l->target[0] == '\0') && (l->target_state != RM_RTL_TARGET_MISSING)))
		{
			continue;
		}

		for (j = 0; (l->target_state != RM_RTL_TARGET_MISSING) && (j < out->ntargets); j++)
		{
			if (!strcmp(out->target[j].name, l->target))
			{
				l->dir[0] = out->target[j].origin[0] - l->origin[0];
				l->dir[1] = out->target[j].origin[1] - l->origin[1];
				l->dir[2] = out->target[j].origin[2] - l->origin[2];
				rtl_unit(l->dir);
				l->target_state = RM_RTL_TARGET_FOUND;
				break;
			}
		}

		if (l->target_state != RM_RTL_TARGET_FOUND)
		{
			l->target_state = RM_RTL_TARGET_MISSING;
			l->dir[0] = 0;
			l->dir[1] = 0;
			l->dir[2] = -1;
			out->untargeted++;
		}
	}
}

static int
rtl_refuse(rm_rtl_lights_t *out, int why)
{
	out->refused = why;
	out->nlights = 0;
	out->ntargets = 0;
	out->spots = out->styled = out->untargeted = out->over_cap = 0;
	return why;
}

int
rm_rtl_parse(const char *lump, size_t len, rm_rtl_lights_t *out)
{
	static rtl_entity_t e;   /* 11 KiB: off the stack */
	char key[RM_RTL_MAX_KEY];
	size_t pos = 0;
	int tok, k;

	out->nlights = out->entities = out->spots = out->styled = out->untargeted = 0;
	out->skipped_long = out->skipped_number = out->over_cap = out->ntargets = 0;
	out->refused = RM_RTL_OK;

	if (!lump)
	{
		len = 0;
	}

	for (;;)
	{
		tok = rtl_token(lump, len, &pos, key, sizeof(key));

		if (tok == TOK_END)
		{
			break;
		}
		else if (tok == TOK_UNTERMINATED)
		{
			return rtl_refuse(out, RM_RTL_UNTERMINATED);
		}
		else if (tok != TOK_OPEN)
		{
			return rtl_refuse(out, RM_RTL_UNBALANCED);
		}

		memset(e.have, 0, sizeof(e.have));
		e.too_long = 0;

		for (;;)
		{
			int ktok, vtok;
			char *val;
			static char skip[RM_RTL_MAX_VALUE];

			ktok = rtl_token(lump, len, &pos, key, sizeof(key));

			if (ktok == TOK_CLOSE)
			{
				break;
			}
			else if (ktok == TOK_UNTERMINATED)
			{
				return rtl_refuse(out, RM_RTL_UNTERMINATED);
			}
			else if (ktok == TOK_OPEN)
			{
				return rtl_refuse(out, RM_RTL_TOO_DEEP);
			}
			else if (ktok == TOK_END)
			{
				return rtl_refuse(out, RM_RTL_UNBALANCED);
			}

			for (k = 0; (ktok == TOK_STRING) && (k < K_COUNT) && strcmp(key, rtl_keys[k]); k++)
			{
			}

			val = ((ktok == TOK_STRING) && (k < K_COUNT)) ? e.val[k] : skip;
			vtok = rtl_token(lump, len, &pos, val, RM_RTL_MAX_VALUE);

			if (vtok == TOK_UNTERMINATED)
			{
				return rtl_refuse(out, RM_RTL_UNTERMINATED);
			}
			else if (vtok == TOK_OPEN)
			{
				return rtl_refuse(out, RM_RTL_TOO_DEEP);
			}
			else if ((vtok == TOK_CLOSE) || (vtok == TOK_END))
			{
				return rtl_refuse(out, RM_RTL_UNBALANCED);
			}

			if ((ktok == TOK_LONG) || (vtok == TOK_LONG))
			{
				e.too_long = 1;
			}
			else if (val != skip)
			{
				e.have[k] = 1;
			}
		}

		rtl_entity(out, &e);
	}

	rtl_resolve(out);
	return RM_RTL_OK;
}

/* ---- the emissive faces ----------------------------------------------------------- */

static int32_t
rtl_le32(const unsigned char *p)
{
	uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
	int32_t v;

	memcpy(&v, &u, sizeof(v));
	return v;
}

void
rm_rtl_emitters(const unsigned char *texinfo, size_t tilen, const unsigned char *faces, size_t flen,
		rm_rtl_emitters_t *out)
{
	size_t nti = texinfo ? tilen / RM_RTL_TEXINFO_SIZE : 0, nf = faces ? flen / RM_RTL_FACE_SIZE : 0, f;
	int last = -1;

	out->n = 0;
	out->ntex = 0;
	out->tex_over = 0;
	out->bad_texinfo = 0;

	for (f = 0; (f < nf) && (out->n < RM_RTL_MAX_EMITTERS); f++)
	{
		const unsigned char *fp = faces + f * RM_RTL_FACE_SIZE, *tp;
		int t = (int16_t)(uint16_t)(fp[RM_RTL_FACE_TEXINFO] | (fp[RM_RTL_FACE_TEXINFO + 1] << 8));
		char name[RM_RTL_TEXNAME + 1];
		int32_t flags, value;
		int slot;

		if ((t < 0) || ((size_t)t >= nti))
		{
			out->bad_texinfo++;
			continue;
		}

		tp = texinfo + (size_t)t * RM_RTL_TEXINFO_SIZE;
		flags = rtl_le32(tp + RM_RTL_TEXINFO_FLAGS);
		value = rtl_le32(tp + RM_RTL_TEXINFO_VALUE);

		if (!(flags & RM_RTL_SURF_LIGHT) || (flags & RM_RTL_SURF_SKY) || (value <= 0))
		{
			continue;
		}

		memcpy(name, tp + RM_RTL_TEXINFO_NAME, RM_RTL_TEXNAME);
		name[RM_RTL_TEXNAME] = '\0';

		/* the per-texture cache: the previous face's slot first, then each slot */
		if ((last >= 0) && !strcmp(out->tex[last].name, name))
		{
			slot = last;
		}
		else
		{
			for (slot = 0; (slot < out->ntex) && strcmp(out->tex[slot].name, name); slot++)
			{
			}

			if (slot == out->ntex)
			{
				if (out->ntex < RM_RTL_MAX_TEXTURES)
				{
					rm_rtl_tex_t *x = &out->tex[out->ntex++];

					memcpy(x->name, name, sizeof(name));
					x->mean[0] = x->mean[1] = x->mean[2] = RM_RTL_UNREAD_GREY;
					x->read = 0;
				}
				else
				{
					slot = -1;
					out->tex_over++;
				}
			}
		}

		last = slot;
		out->emit[out->n].face = (long)f;
		out->emit[out->n].tex = slot;
		out->emit[out->n].value = value;
		out->n++;
	}
}

int
rm_rtl_wal_mean(const unsigned char *wal, size_t len, const unsigned *palette, float *out)
{
	uint32_t w, h, ofs;
	uint64_t npix, i;
	double c[3] = { 0, 0, 0 };
	int k;

	/* miptex_t: name[32], width, height, offsets[4], animname[32], flags, contents, value */
	if (!wal || !palette || !out || (len < 100))
	{
		return 0;
	}

	w = (uint32_t)rtl_le32(wal + 32);
	h = (uint32_t)rtl_le32(wal + 36);
	ofs = (uint32_t)rtl_le32(wal + 40);
	npix = (uint64_t)w * h;

	if (!w || !h || (w > 8192) || (h > 8192) || (ofs > len) || (npix > (uint64_t)(len - ofs)))
	{
		return 0;
	}

	for (i = 0; i < npix; i++)
	{
		const unsigned char *rgba = (const unsigned char *)&palette[wal[ofs + i]];

		for (k = 0; k < 3; k++)
		{
			c[k] += rgba[k];
		}
	}

	for (k = 0; k < 3; k++)
	{
		out[k] = (float)(c[k] / (255.0 * (double)npix));
	}

	return 1;
}

int
rm_rtl_sky_mean(const float (*means)[3], const int *have, int n, float *out)
{
	double c[3] = { 0, 0, 0 };
	int i, k, m = 0;

	for (i = 0; means && have && (i < n); i++)
	{
		if (have[i])
		{
			for (k = 0; k < 3; k++)
			{
				c[k] += means[i][k];
			}

			m++;
		}
	}

	for (k = 0; k < 3; k++)
	{
		out[k] = m ? (float)(c[k] / m) : 0.0f;
	}

	return m;
}

/* ---- the line ------------------------------------------------------------------- */

const char *
rm_rtl_lump_name(int refused)
{
	switch (refused)
	{
		case RM_RTL_OK: return "none";
		case RM_RTL_UNTERMINATED: return "an unterminated quote";
		case RM_RTL_TOO_DEEP: return "a brace too deep";
		case RM_RTL_UNBALANCED: return "unbalanced braces";
		default: return "unknown";
	}
}

static int
rtl_byte(float v)
{
	return (v > 0) ? ((v < 1) ? (int)(v * 255.0f + 0.5f) : 255) : 0;
}

int
rm_rtl_format(const char *map, const rm_rtl_lights_t *l, const rm_rtl_emitters_t *e, const float *sky,
		char *buf, size_t len)
{
	char skys[32], refs[96];
	long unread = 0;
	int i, n;

	if (!buf || (len == 0))
	{
		return 0;
	}

	buf[0] = '\0';

	for (i = 0; i < e->ntex; i++)
	{
		unread += !e->tex[i].read;
	}

	if (sky)
	{
		snprintf(skys, sizeof(skys), "%d,%d,%d", rtl_byte(sky[0]), rtl_byte(sky[1]), rtl_byte(sky[2]));
	}
	else
	{
		snprintf(skys, sizeof(skys), "none");
	}

	if (l->refused)
	{
		snprintf(refs, sizeof(refs), "the lump (%s)", rm_rtl_lump_name(l->refused));
	}
	else
	{
		snprintf(refs, sizeof(refs), "%ld (past the cap %ld)", l->skipped_long + l->skipped_number, l->over_cap);
	}

	n = snprintf(buf, len, "ref_metal: rt-data: %s: lights %ld (spot %ld, styled %ld, untargeted %ld) "
			"emitters %ld (textures %d, unread %ld) sky %s refused %s", map ? map : "?", l->nlights, l->spots,
			l->styled, l->untargeted, e->n, e->ntex, unread, skys, refs);

	if ((n < 0) || ((size_t)n >= len))
	{
		buf[0] = '\0';
		return 0;
	}

	return 1;
}
