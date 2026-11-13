/*
 * Copyright (C) 2026 Yamagi Quake II contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * src/common/shared/shared.c: the COM_Parse tokenizer (entity strings,
 * scripts), the Info_* userinfo/serverinfo key-value strings that cross
 * the network, and the Q_* string and math helpers.
 *
 * =======================================================================
 */

#include "test.h"

static void
pin_tokens(t_text_t *pin, const char *label, const char *input)
{
	char *copy = strdup(input);
	char *p = copy;
	int n = 0;

	t_text_printf(pin, "%s:\n", label);

	for (;;)
	{
		const char *tok = COM_Parse(&p);

		if (!p)
		{
			t_text_printf(pin, "  end after %d tokens (last \"%s\")\n", n, tok);
			break;
		}

		t_text_printf(pin, "  [");

		for (; *tok; tok++)
		{
			unsigned char c = *tok;

			if (c < 32 || c > 126)
			{
				t_text_printf(pin, "\\x%02x", c);
			}
			else
			{
				t_text_printf(pin, "%c", c);
			}
		}

		t_text_printf(pin, "]\n");
		n++;
	}

	free(copy);
}

TEST(com_parse_tokens)
{
	t_text_t pin = {0};

	pin_tokens(&pin, "entity block",
		"{\n\"classname\" \"worldspawn\"\n\"message\" \"The Edge\"\n}\n"
		"{ \"origin\" \"-8 16 24\" \"angle\" \"90\" }");
	pin_tokens(&pin, "comments and whitespace",
		"  // a comment line\n\tword1 // trailing\n\n  word2\r\n//last");
	/* Not pinned: a quoted string without its closing quote at the very
	   end of the input. COM_Parse then returns *data_p one byte past
	   the terminator and the next call reads beyond the string (found
	   under ASAN while writing this test, TASK29); extend this test
	   when that is fixed. */
	pin_tokens(&pin, "quotes",
		"\"\" \"a b\"c \"unterminated\nline\" end");
	pin_tokens(&pin, "separators are not special",
		"a=b;c{d}e/f");
	pin_tokens(&pin, "control characters split words",
		"one\x01two\x7fthree");
	pin_tokens(&pin, "empty", "");

	t_pin("com-parse-tokens", pin.data);
	t_text_free(&pin);
}

TEST(com_parse_null_and_long_tokens)
{
	char *p = NULL;
	char *buf;
	const char *tok;

	T_EQ_STR(COM_Parse(&p), "");
	T_CHECK(p == NULL);

	/* MAX_TOKEN_CHARS - 1 characters survive... */
	buf = malloc(MAX_TOKEN_CHARS + 16);
	memset(buf, 'x', MAX_TOKEN_CHARS - 1);
	strcpy(buf + MAX_TOKEN_CHARS - 1, " next");
	p = buf;
	tok = COM_Parse(&p);
	T_EQ_INT(strlen(tok), MAX_TOKEN_CHARS - 1);
	T_EQ_STR(COM_Parse(&p), "next");

	/* ...a token of MAX_TOKEN_CHARS or more becomes the empty string,
	   and parsing resumes after it. Same for a quoted one. */
	memset(buf, 'y', MAX_TOKEN_CHARS + 5);
	strcpy(buf + MAX_TOKEN_CHARS + 5, " next");
	p = buf;
	T_EQ_STR(COM_Parse(&p), "");
	T_CHECK(p != NULL);
	T_EQ_STR(COM_Parse(&p), "next");

	buf[0] = '"';
	memset(buf + 1, 'z', MAX_TOKEN_CHARS + 2);
	strcpy(buf + MAX_TOKEN_CHARS + 3, "\" next");
	p = buf;
	T_EQ_STR(COM_Parse(&p), "");
	T_EQ_STR(COM_Parse(&p), "next");

	free(buf);
}

TEST(info_value_for_key)
{
	const char *s = "\\name\\Player\\skin\\male/grunt\\rate\\25000\\empty\\\\hand\\2";

	T_EQ_STR(Info_ValueForKey(s, "name"), "Player");
	T_EQ_STR(Info_ValueForKey(s, "skin"), "male/grunt");
	T_EQ_STR(Info_ValueForKey(s, "hand"), "2");
	T_EQ_STR(Info_ValueForKey(s, "empty"), "");
	T_EQ_STR(Info_ValueForKey(s, "missing"), "");
	/* Prefixes and values are not keys. */
	T_EQ_STR(Info_ValueForKey(s, "nam"), "");
	T_EQ_STR(Info_ValueForKey(s, "Player"), "");
	/* Keys compare case-sensitively. */
	T_EQ_STR(Info_ValueForKey(s, "Name"), "");
	/* Without the leading backslash, too. */
	T_EQ_STR(Info_ValueForKey("a\\1\\b\\2", "b"), "2");
	/* A key with no value. */
	T_EQ_STR(Info_ValueForKey("\\lonely", "lonely"), "");

	/* Two results stay valid at the same time (two buffers). */
	{
		const char *a = Info_ValueForKey(s, "name");
		const char *b = Info_ValueForKey(s, "rate");

		T_EQ_STR(a, "Player");
		T_EQ_STR(b, "25000");
	}
}

TEST(info_value_is_cut_at_max_value)
{
	char s[MAX_INFO_STRING];
	char longv[MAX_INFO_VALUE + 20];

	memset(longv, 'v', sizeof(longv) - 1);
	longv[sizeof(longv) - 1] = '\0';
	Com_sprintf(s, sizeof(s), "\\k\\%s", longv);
	T_EQ_INT(strlen(Info_ValueForKey(s, "k")), MAX_INFO_VALUE - 1);
}

TEST(info_set_and_remove)
{
	char s[MAX_INFO_STRING] = "";
	char big[MAX_INFO_VALUE + 8];
	int i;

	Info_SetValueForKey(s, "name", "Player");
	Info_SetValueForKey(s, "skin", "female/athena");
	T_EQ_STR(s, "\\name\\Player\\skin\\female/athena");

	/* Setting again replaces, and moves the key to the end. */
	Info_SetValueForKey(s, "name", "Other");
	T_EQ_STR(s, "\\skin\\female/athena\\name\\Other");

	/* An empty or NULL value removes the key. */
	Info_SetValueForKey(s, "skin", "");
	T_EQ_STR(s, "\\name\\Other");
	Info_SetValueForKey(s, "name", NULL);
	T_EQ_STR(s, "");

	/* Refused keys and values leave the string alone. */
	Info_SetValueForKey(s, "a", "1");
	t_console_reset();
	Info_SetValueForKey(s, "b\\c", "1");
	Info_SetValueForKey(s, "quote\"", "1");
	Info_SetValueForKey(s, "semi;", "1");
	Info_SetValueForKey(s, "b", "x\\y");
	Info_SetValueForKey(s, "b", "x\"y");
	Info_SetValueForKey(s, NULL, "1");
	T_EQ_STR(s, "\\a\\1");
	T_CONSOLE_HAS("Can't use keys with a '\\'");
	T_CONSOLE_HAS("Can't use keys with a '\"'");
	T_CONSOLE_HAS("Can't use keys with a ';'");
	T_CONSOLE_HAS("Can't use values with a '\\'");

	/* Values may contain ';' (only Info_Validate rejects it). */
	Info_SetValueForKey(s, "b", "x;y");
	T_EQ_STR(Info_ValueForKey(s, "b"), "x;y");
	T_CHECK(!Info_Validate(s));
	T_CHECK(Info_Validate("\\a\\1"));
	T_CHECK(!Info_Validate("\\a\\\"1\""));

	/* Too long keys and values are refused. */
	memset(big, 'k', sizeof(big) - 1);
	big[sizeof(big) - 1] = '\0';
	t_console_reset();
	Info_SetValueForKey(s, "c", big);
	T_CONSOLE_HAS("Keys and values must be <");
	T_EQ_STR(Info_ValueForKey(s, "c"), "");
	big[MAX_INFO_KEY] = '\0';
	Info_SetValueForKey(s, big, "1");
	T_EQ_STR(Info_ValueForKey(s, big), "");

	/* High-bit and control characters are dropped from values. */
	s[0] = '\0';
	Info_SetValueForKey(s, "n", "a\x01" "b\xc1" "c\x7f");
	T_EQ_STR(s, "\\n\\abAc");

	/* When the whole string would exceed MAX_INFO_STRING the new pair
	   is dropped (the old one is already removed). */
	s[0] = '\0';

	for (i = 0; i < 20; i++)
	{
		char key[8];

		memset(big, 'a' + i, 50);
		big[50] = '\0';
		Com_sprintf(key, sizeof(key), "k%d", i);
		t_console_reset();
		Info_SetValueForKey(s, key, big);
	}

	T_CHECK(strlen(s) < MAX_INFO_STRING);
	T_CONSOLE_HAS("Info string length exceeded");
	T_EQ_STR(Info_ValueForKey(s, "k19"), "");
	T_CHECK(strlen(Info_ValueForKey(s, "k0")) == 50);
}

TEST(info_remove_key)
{
	char s[MAX_INFO_STRING];

	Q_strlcpy(s, "\\a\\1\\bb\\2\\c\\3", sizeof(s));
	Info_RemoveKey(s, "b");
	T_EQ_STR(s, "\\a\\1\\bb\\2\\c\\3");
	Info_RemoveKey(s, "bb");
	T_EQ_STR(s, "\\a\\1\\c\\3");
	Info_RemoveKey(s, "a\\1");
	T_EQ_STR(s, "\\a\\1\\c\\3");
	Info_RemoveKey(s, "c");
	T_EQ_STR(s, "\\a\\1");
	Info_RemoveKey(s, "a");
	T_EQ_STR(s, "");
}

TEST(q_string_helpers)
{
	char buf[16];
	char big[64];

	/* Q_strlcpy / Q_strlcat return the length they tried to create. */
	T_EQ_INT(Q_strlcpy(buf, "0123456789abcdefXYZ", sizeof(buf)), 19);
	T_EQ_STR(buf, "0123456789abcde");
	T_EQ_INT(Q_strlcpy(buf, "abc", 0), 3);
	T_EQ_STR(buf, "0123456789abcde");
	Q_strlcpy(buf, "abc", sizeof(buf));
	T_EQ_INT(Q_strlcat(buf, "defghijklmnopqrstu", sizeof(buf)), 21);
	T_EQ_STR(buf, "abcdefghijklmno");

	T_EQ_INT(Q_strlcpy_ascii(buf, "a\tb\x80" "c", sizeof(buf)), 3);
	T_EQ_STR(buf, "abc");

	/* Com_sprintf truncates silently. */
	Com_sprintf(buf, 4, "%d", 123456);
	T_EQ_STR(buf, "123");

	T_EQ_INT(Q_stricmp("Quake", "qUAKE"), 0);
	T_CHECK(Q_stricmp("a", "b") < 0);
	T_EQ_INT(Q_strncasecmp("BASEQ2/pak0", "baseq2/PAK9", 10), 0);
	T_CHECK(Q_strncasecmp("BASEQ2/pak0", "baseq2/PAK9", 11) != 0);
	T_EQ_STR(Q_strcasestr("maps/Base1.bsp", "BASE1"), "Base1.bsp");
	T_CHECK(Q_strcasestr("maps/base1.bsp", "base2") == NULL);

	Q_strlcpy(big, "Mixed Case", sizeof(big));
	Q_strlwr(big);
	T_EQ_STR(big, "mixed case");
	Q_strupr(big);
	T_EQ_STR(big, "MIXED CASE");

	Q_strlcpy(big, "a\\b\\c", sizeof(big));
	Q_replacebackslash(big);
	T_EQ_STR(big, "a/b/c");

	Q_strlcpy(big, "hello world", sizeof(big));
	Q_strdel(big, 5, 6);
	T_EQ_STR(big, "hello");
	Q_strdel(big, 3, 10);
	T_EQ_STR(big, "hello");
	T_EQ_INT(Q_strins(big, ", you", 5, sizeof(big)), 5);
	T_EQ_STR(big, "hello, you");
	T_EQ_INT(Q_strins(big, "x", 11, sizeof(big)), 0);
	T_EQ_INT(Q_strins(big, "too long", 0, 12), 0);
	T_EQ_STR(big, "hello, you");

	T_CHECK(Q_strisnum("0123"));
	T_CHECK(Q_strisnum(""));
	T_CHECK(!Q_strisnum("12a"));
	T_EQ_STR(Q_strchrs("path/to.file", "./"), "/to.file");
	T_EQ_STR(Q_strrchrs("path/to.file", "./"), ".file");
	T_CHECK(Q_strchrs("abc", "xyz") == NULL);
	T_EQ_STR(Q_strchr0("abc", 'x'), "");
	T_EQ_STR(Q_strchr0("abc", 'b'), "bc");
	T_EQ_INT(Word_Count("  one two\tthree  "), 3);
}

TEST(com_path_helpers)
{
	char out[MAX_OSPATH];
	char path[MAX_OSPATH];

	T_EQ_STR(COM_SkipPath("maps/sub/base1.bsp"), "base1.bsp");
	T_EQ_STR(COM_SkipPath("nodir"), "nodir");
	COM_StripExtension("maps/base1.bsp", out);
	T_EQ_STR(out, "maps/base1");
	COM_StripExtension("dir.d/noext", out);
	T_EQ_STR(out, "dir");
	T_EQ_STR(COM_FileExtension("maps/base1.bsp"), "bsp");
	T_EQ_STR(COM_FileExtension("noext"), "");
	COM_FileBase("models/weapons/v_shotg/tris.md2", out);
	T_EQ_STR(out, "tris");
	COM_FilePath("models/weapons/tris.md2", out);
	T_EQ_STR(out, "models/weapons");
	Q_strlcpy(path, "config", sizeof(path));
	COM_DefaultExtension(path, ".cfg");
	T_EQ_STR(path, "config.cfg");
	COM_DefaultExtension(path, ".txt");
	T_EQ_STR(path, "config.cfg");
}

static void
set_signbits(cplane_t *p)
{
	int j;

	p->signbits = 0;

	for (j = 0; j < 3; j++)
	{
		if (p->normal[j] < 0)
		{
			p->signbits |= 1 << j;
		}
	}
}

/*
 * Rounds to 4 decimals and folds -0 into 0, so that the pin does not
 * depend on the last bits of float arithmetic (optimisation level,
 * fused multiply-add) on different compilers and CPUs.
 */
static double
r4(double x)
{
	double y = (double)(long long)(x * 10000.0 + (x < 0 ? -0.5 : 0.5)) / 10000.0;

	return (y == 0) ? 0 : y;
}

TEST(math_helpers)
{
	t_text_t pin = {0};
	vec3_t f, r, u, v, mins, maxs;
	cplane_t plane;
	static const float angles[][3] = {
		{ 0, 0, 0 }, { 0, 90, 0 }, { 45, 0, 0 }, { 30, 60, 90 }, { -90, 180, 45 }
	};
	static const float mods[] = { 0, 360, 361, -1, 720.5f, -359 };
	int i;

	for (i = 0; i < (int)(sizeof(angles) / sizeof(angles[0])); i++)
	{
		AngleVectors(angles[i], f, r, u);
		t_text_printf(&pin, "AngleVectors(%g %g %g): f=(%.4f %.4f %.4f) r=(%.4f %.4f %.4f) u=(%.4f %.4f %.4f)\n",
			angles[i][0], angles[i][1], angles[i][2],
			r4(f[0]), r4(f[1]), r4(f[2]), r4(r[0]), r4(r[1]), r4(r[2]),
			r4(u[0]), r4(u[1]), r4(u[2]));
	}

	for (i = 0; i < (int)(sizeof(mods) / sizeof(mods[0])); i++)
	{
		t_text_printf(&pin, "anglemod(%g) = %.4f\n", mods[i], r4(anglemod(mods[i])));
	}

	t_text_printf(&pin, "LerpAngle(350, 10, 0.5) = %.4f\n", r4(LerpAngle(350, 10, 0.5f)));
	t_text_printf(&pin, "LerpAngle(10, 350, 0.25) = %.4f\n", r4(LerpAngle(10, 350, 0.25f)));
	t_text_printf(&pin, "Q_log2: %d %d %d %d\n", Q_log2(1), Q_log2(2), Q_log2(255), Q_log2(256));
	t_text_printf(&pin, "NextPow2: %u %u %u %u\n", NextPow2(0), NextPow2(1), NextPow2(17), NextPow2(64));
	t_text_printf(&pin, "NextPow2gt: %u %u\n", NextPow2gt(16), NextPow2gt(17));

	VectorSet(v, 3, 4, 12);
	t_text_printf(&pin, "VectorLength(3 4 12) = %.4f\n", r4(VectorLength(v)));
	{
		/* Argument evaluation order is unspecified: normalise first. */
		double len = VectorNormalize(v);

		t_text_printf(&pin, "VectorNormalize(3 4 12) = %.4f -> (%.4f %.4f %.4f)\n",
			r4(len), r4(v[0]), r4(v[1]), r4(v[2]));
	}
	VectorClear(v);
	t_text_printf(&pin, "VectorNormalize(0 0 0) = %g\n", VectorNormalize(v));

	ClearBounds(mins, maxs);
	VectorSet(v, -1, 5, 2);
	AddPointToBounds(v, mins, maxs);
	VectorSet(v, 4, -3, 2);
	AddPointToBounds(v, mins, maxs);
	t_text_printf(&pin, "bounds: (%g %g %g) (%g %g %g)\n",
		mins[0], mins[1], mins[2], maxs[0], maxs[1], maxs[2]);

	/* BoxOnPlaneSide: 1 front, 2 back, 3 crossing. */
	memset(&plane, 0, sizeof(plane));
	VectorSet(plane.normal, 1, 0, 0);
	plane.type = PLANE_X;
	plane.dist = 0;
	set_signbits(&plane);
	VectorSet(mins, 1, -1, -1);
	VectorSet(maxs, 2, 1, 1);
	t_text_printf(&pin, "BoxOnPlaneSide axial: front=%d", BoxOnPlaneSide(mins, maxs, &plane));
	VectorSet(mins, -2, -1, -1);
	VectorSet(maxs, -1, 1, 1);
	t_text_printf(&pin, " back=%d", BoxOnPlaneSide(mins, maxs, &plane));
	VectorSet(maxs, 1, 1, 1);
	t_text_printf(&pin, " cross=%d\n", BoxOnPlaneSide(mins, maxs, &plane));
	VectorSet(plane.normal, 0.6f, 0.8f, 0);
	plane.type = PLANE_ANYZ;
	plane.dist = 1;
	set_signbits(&plane);
	VectorSet(mins, 2, 2, 0);
	VectorSet(maxs, 3, 3, 1);
	t_text_printf(&pin, "BoxOnPlaneSide non-axial: front=%d", BoxOnPlaneSide(mins, maxs, &plane));
	VectorSet(mins, -1, -1, 0);
	VectorSet(maxs, 1, 1, 1);
	t_text_printf(&pin, " cross=%d\n", BoxOnPlaneSide(mins, maxs, &plane));

	t_text_printf(&pin, "LittleShort(0x1234)=%04x LittleLong(0x12345678)=%08x BigShort(0x1234)=%04x BigLong(0x12345678)=%08x\n",
		(unsigned short)LittleShort(0x1234), (unsigned)LittleLong(0x12345678),
		(unsigned short)BigShort(0x1234), (unsigned)BigLong(0x12345678));
	t_text_printf(&pin, "LittleFloat(1.5)=%g BigFloat(BigFloat(2.25))=%g\n",
		LittleFloat(1.5f), BigFloat(BigFloat(2.25f)));

	t_pin("math-helpers", pin.data);
	t_text_free(&pin);
}
