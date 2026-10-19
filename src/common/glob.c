/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
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
 * Global string matching
 *
 * =======================================================================
 */

#include <stdio.h>
#include "header/glob.h"

/*
 * Matches one non-star pattern element at *PP against the text
 * character TC (which must not be NUL) and advances *PP past the
 * element. A backslash at the very end of the pattern is a literal
 * backslash. Returns 1 on match, 0 on mismatch and -1 if the pattern
 * is malformed (unterminated set); the pattern pointer never moves
 * past the terminating NUL.
 */
static int
glob_match_element(char **pp, char tc)
{
	char *p = *pp;
	char c = *p++;

	switch (c)
	{
		case '?':
			*pp = p;
			return 1;

		case '\\':

			if (*p != '\0')
			{
				c = *p++;
			}

			*pp = p;
			return c == tc;

		case '[':
		{
			int invert, matched = 0;

			invert = ((*p == '!') || (*p == '^'));

			if (invert)
			{
				p++;
			}

			/* The first character is always a set member, even ']'. */
			c = *p;

			while (1)
			{
				char cstart, cend;

				if (c == '\0')
				{
					*pp = p;
					return -1;
				}

				p++;
				cstart = cend = c;

				if (c == '\\')
				{
					if (*p == '\0')
					{
						*pp = p;
						return -1;
					}

					cstart = cend = *p++;
				}

				c = *p;

				if ((c == '-') && (p[1] != ']') && (p[1] != '\0'))
				{
					p++;
					cend = *p++;

					if (cend == '\\')
					{
						if (*p == '\0')
						{
							*pp = p;
							return -1;
						}

						cend = *p++;
					}

					c = *p;
				}

				if ((tc >= cstart) && (tc <= cend))
				{
					matched = 1;
				}

				if (c == ']')
				{
					p++;
					break;
				}
			}

			*pp = p;
			return matched != invert;
		}

		default:
			*pp = p;
			return c == tc;
	}
}

/*
 * Iterative matcher with a single backtrack point (the most recent
 * star), O(strlen(pattern) * strlen(text)) in the worst case. If
 * STAR is set the pattern is treated as if it followed a star.
 */
static int
glob_run(char *pattern, char *text, int star)
{
	char *p = pattern, *t = text;
	char *star_p = NULL, *star_t = NULL;

	if (star)
	{
		star_p = p;
		star_t = t;
	}

	while (1)
	{
		if (*p == '*')
		{
			while (*p == '*')
			{
				p++;
			}

			if (*p == '\0')
			{
				return 1;
			}

			star_p = p;
			star_t = t;
			continue;
		}

		if (*p == '\0')
		{
			if (*t == '\0')
			{
				return 1;
			}
		}
		else if (*t != '\0')
		{
			int r = glob_match_element(&p, *t);

			if (r < 0)
			{
				return 0;
			}

			if (r)
			{
				t++;
				continue;
			}
		}

		/* Mismatch: let the last star swallow one more character. */
		if ((star_p == NULL) || (*star_t == '\0'))
		{
			return 0;
		}

		star_t++;
		t = star_t;
		p = star_p;
	}
}

/*
 * Like glob_match, but match PATTERN against any final segment of TEXT.
 */
static int
glob_match_after_star(char *pattern, char *text)
{
	return glob_run(pattern, text, 1);
}

/* Match the pattern PATTERN against the string TEXT;
 * return 1 if it matches, 0 otherwise.
 *
 * A match means the entire string TEXT is used up in matching.
 *
 * In the pattern string, `*' matches any sequence of characters,
 * `?' matches any character, [SET] matches any character in the specified set,
 * [!SET] matches any character not in the specified set.
 *
 * A set is composed of characters or ranges; a range looks like
 * character hyphen character (as in 0-9 or A-Z).
 * [0-9a-zA-Z_] is the set of characters allowed in C identifiers.
 * Any other character in the pattern must be matched exactly.
 *
 * To suppress the special syntactic significance of any of `[]*?!-\',
 * and match the character exactly, precede it with a `\'.
 */
int
glob_match(char *pattern, char *text)
{
	if (*pattern == '*')
	{
		return glob_match_after_star(pattern + 1, text);
	}

	return glob_run(pattern, text, 0);
}
