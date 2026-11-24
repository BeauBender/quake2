/*
 * Copyright (C) 2026 Yamagi Quake II contributors
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
 * =======================================================================
 *
 * The ray-tracing tuning harness's commands (TASK195): rt_preset saves,
 * loads and lists the tuning cvars (vidrules.c's VidRules_PresetCvar) as
 * <writedir>/presets/rt-<name>.cfg, and rt_ab flips the Metal renderer
 * between the user's values (A) and a second set (B: the defaults, or a
 * preset). The B side is an overlay: rt_ab writes no tuning cvar, only
 * r_rt_ab, the description of the B side the renderer reads through
 * RM_Effect ("<label>|name=value;...", empty for A).
 *
 * =======================================================================
 */

#include <time.h>

#include "../../common/header/common.h"
#include "header/vidrules.h"

#define RTP_MAXCVARS RTPRESET_MAXROWS
#define RTP_MAXVALUE RTPRESET_MAXVALUE

static const char *rtp_dir;   /* NULL: FS_Gamedir(); the unit tests set their own */
static int rtp_side;    /* 0: A, the user's values; 1: B */
static char rtp_bname[VIDPRESET_MAXNAME + 1];   /* "" for stock */
static char rtp_bvals[RTP_MAXCVARS][RTP_MAXVALUE];

/* a value a preset or the B side may hold: printable, no quote, and none of r_rt_ab's separators */
static int
RTP_ValueOk(const char *v)
{
	if (strlen(v) >= RTP_MAXVALUE)
	{
		return 0;
	}
	for (; *v; v++)
	{
		if (*v < ' ' || *v > '~' || *v == '"' || *v == ';' || *v == '|' || *v == '=')
		{
			return 0;
		}
	}
	return 1;
}

/* the preset's file; 0, refused with a line, when the path does not fit in buf */
static int
RTP_Path(const char *cmd, const char *name, char *buf, size_t len)
{
	const int n = snprintf(buf, len, "%s/presets/rt-%s.cfg", rtp_dir ? rtp_dir : FS_Gamedir(), name);

	if ((n < 0) || ((size_t)n >= len))
	{
		Com_Printf("%s: refused: the path is too long\n", cmd);
		return 0;
	}
	return 1;
}

/* the name, refused with its reason when it may not name a file */
static int
RTP_NameOk(const char *cmd, const char *name)
{
	const char *why = VidRules_PresetNameError(name);

	if (why)
	{
		Com_Printf("%s: refused: %s\n", cmd, why);
		return 0;
	}
	return 1;
}

/* the text r_rt_ab carries for the B side */
static void
RTP_Publish(void)
{
	char spec[2048];
	size_t at;

	if (!rtp_side)
	{
		Cvar_Set("r_rt_ab", "");
		return;
	}

	at = (size_t)snprintf(spec, sizeof(spec), "B: %s%s|", rtp_bname[0] ? "preset " : "stock",
			rtp_bname);

	for (int i = 0; i < VidRules_PresetNum() && i < RTP_MAXCVARS && at < sizeof(spec); i++)
	{
		const char *def;
		const char *name = VidRules_PresetCvar(i, &def);

		at += (size_t)snprintf(spec + at, sizeof(spec) - at, "%s=%s;", name,
				rtp_bname[0] ? rtp_bvals[i] : def);
	}

	Cvar_Set("r_rt_ab", spec);
}

const char *
RTPreset_Side(void)
{
	static char text[VIDPRESET_MAXNAME + 16];

	if (!rtp_side)
	{
		return "A: your settings";
	}
	snprintf(text, sizeof(text), "B: %s%s", rtp_bname[0] ? "preset " : "stock", rtp_bname);
	return text;
}

/*
 * Reads preset name into vals: each tuning cvar's value, where the file
 * does not set it its current one, or the row's default when the current
 * one is unset or may not be held (RTP_ValueOk). set[i] is 1 where the
 * file sets row i; an empty value is no setting. 0 when the file cannot
 * be read.
 */
static int
RTP_Read(const char *cmd, const char *name, char vals[RTP_MAXCVARS][RTP_MAXVALUE], int set[RTP_MAXCVARS])
{
	char path[MAX_OSPATH], line[256];
	FILE *f;

	if (!RTP_Path(cmd, name, path, sizeof(path)))
	{
		return 0;
	}
	if (!(f = Q_fopen(path, "r")))
	{
		Com_Printf("rt_preset: no preset %s\n", name);
		return 0;
	}

	for (int i = 0; i < VidRules_PresetNum() && i < RTP_MAXCVARS; i++)
	{
		const char *def;
		const char *v = Cvar_VariableString(VidRules_PresetCvar(i, &def));

		Q_strlcpy(vals[i], (v[0] && RTP_ValueOk(v)) ? v : def, RTP_MAXVALUE);
		set[i] = 0;
	}

	while (fgets(line, sizeof(line), f))
	{
		char cv[64], val[RTP_MAXVALUE + 2];
		const char *p = line;
		size_t n = 0;

		if (strncmp(p, "set ", 4))
		{
			continue;   /* a comment, or a line that is no setting */
		}
		p += 4;
		while (*p && *p != ' ' && n < sizeof(cv) - 1)
		{
			cv[n++] = *p++;
		}
		cv[n] = '\0';
		while (*p == ' ')
		{
			p++;
		}
		n = 0;
		if (*p == '"')
		{
			p++;
		}
		while (*p && *p != '"' && *p != '\n' && *p != '\r' && n < sizeof(val) - 1)
		{
			val[n++] = *p++;
		}
		val[n] = '\0';

		for (int i = 0; i < VidRules_PresetNum() && i < RTP_MAXCVARS; i++)
		{
			if (!strcmp(VidRules_PresetCvar(i, NULL), cv))
			{
				if (!val[0])
				{
					continue;   /* empty: no setting, as an older save wrote for an unset cvar */
				}
				if (RTP_ValueOk(val))
				{
					Q_strlcpy(vals[i], val, RTP_MAXVALUE);
					set[i] = 1;
				}
				else
				{
					Com_Printf("rt_preset: %s: the value of %s is passed over\n", name, cv);
				}
			}
		}
	}

	fclose(f);
	return 1;
}

/* the row of cvar name, -1 when it is no row */
static int
RTP_Row(const char *name)
{
	for (int i = 0; name && i < VidRules_PresetNum() && i < RTP_MAXCVARS; i++)
	{
		if (!strcmp(VidRules_PresetCvar(i, NULL), name))
		{
			return i;
		}
	}
	return -1;
}

/* a given set (TASK203) as a value for each row, NULL where the set has none; 0, with a line, when refused */
static int
RTP_SetRows(const char *cmd, const char *const *names, const char *const *vals, int n,
	const char *over[RTP_MAXCVARS])
{
	for (int i = 0; i < RTP_MAXCVARS; i++)
	{
		over[i] = NULL;
	}
	for (int k = 0; k < n; k++)
	{
		const int row = RTP_Row(names[k]);

		if (row < 0 || !vals[k] || !RTP_ValueOk(vals[k]))
		{
			Com_Printf("%s: refused: %s is no preset row, or its value may not be held\n", cmd,
				(row < 0) ? "a cvar" : names[k]);
			return 0;
		}
		over[row] = vals[k];
	}
	return 1;
}

/* writes the tuning cvars, with over[i] in place of row i's value where set, as preset name */
static int
RTP_SaveRows(const char *cmd, const char *name, const char *const over[RTP_MAXCVARS])
{
	char path[MAX_OSPATH], date[32];
	time_t now = time(NULL);
	int saved = 0, existed = 0;
	FILE *f;

	if (!RTP_NameOk(cmd, name) || !RTP_Path(cmd, name, path, sizeof(path)))
	{
		return 0;
	}

	/* the optimizer says when it saves over a preset (plan, section 6) */
	if (over && (f = Q_fopen(path, "r")))
	{
		existed = 1;
		fclose(f);
	}

	FS_CreatePath(path);
	if (!(f = Q_fopen(path, "w")))
	{
		Com_Printf("%s: cannot write %s\n", cmd, path);
		return 0;
	}

	if (!strftime(date, sizeof(date), "%Y-%m-%d %H:%M", localtime(&now)))
	{
		date[0] = '\0';
	}
	fprintf(f, "// rt preset %s: Yamagi Quake II %s, %s\n", name, YQ2VERSION, date);

	for (int i = 0; i < VidRules_PresetNum(); i++)
	{
		const char *cv = VidRules_PresetCvar(i, NULL);
		const char *v = Cvar_VariableString(cv);

		if (over && i < RTP_MAXCVARS && over[i])
		{
			fprintf(f, "set %s \"%s\"\n", cv, over[i]);
			saved++;
		}
		/* unset (gl1 registers few of them), a value a server wrote (FIX168), or not a value a preset may hold: no line */
		else if (v[0] && !Cvar_ServerWrote(cv) && RTP_ValueOk(v))
		{
			fprintf(f, "set %s \"%s\"\n", cv, v);
			saved++;
		}
	}

	fclose(f);
	Com_Printf("rt_preset: %s %s (%d settings)\n", existed ? "replaced" : "saved", name, saved);
	return existed ? 2 : 1;
}

int
RTPreset_Save(const char *name)
{
	return RTP_SaveRows("rt_preset save", name, NULL);
}

int
RTPreset_SaveSet(const char *name, const char *const *names, const char *const *vals, int n)
{
	const char *over[RTP_MAXCVARS];

	if (!RTP_SetRows("optimize", names, vals, n, over))
	{
		return 0;
	}
	return RTP_SaveRows("optimize", name, over);
}

int
RTPreset_PublishSet(const char *label, const char *const *names, const char *const *vals, int n)
{
	const char *over[RTP_MAXCVARS];

	if (!RTP_NameOk("optimize", label) || !RTP_SetRows("optimize", names, vals, n, over))
	{
		return 0;
	}

	/* every other row as RTP_Read starts it: its value now, or the default where unset or not to be held */
	for (int i = 0; i < VidRules_PresetNum() && i < RTP_MAXCVARS; i++)
	{
		const char *def;
		const char *v = Cvar_VariableString(VidRules_PresetCvar(i, &def));

		Q_strlcpy(rtp_bvals[i], over[i] ? over[i] : ((v[0] && RTP_ValueOk(v)) ? v : def), RTP_MAXVALUE);
	}
	Q_strlcpy(rtp_bname, label, sizeof(rtp_bname));
	rtp_side = 1;
	RTP_Publish();
	return 1;
}

void
RTPreset_GetState(rtpreset_state_t *st)
{
	st->side = rtp_side;
	Q_strlcpy(st->bname, rtp_bname, sizeof(st->bname));
	memcpy(st->bvals, rtp_bvals, sizeof(st->bvals));
}

void
RTPreset_SetState(const rtpreset_state_t *st)
{
	rtp_side = st->side ? 1 : 0;
	Q_strlcpy(rtp_bname, st->bname, sizeof(rtp_bname));
	memcpy(rtp_bvals, st->bvals, sizeof(rtp_bvals));
	RTP_Publish();
}

int
RTPreset_Load(const char *name)
{
	char vals[RTP_MAXCVARS][RTP_MAXVALUE];
	int set[RTP_MAXCVARS];
	int changed = 0;

	if (!RTP_NameOk("rt_preset load", name) || !RTP_Read("rt_preset load", name, vals, set))
	{
		return 0;
	}

	for (int i = 0; i < VidRules_PresetNum() && i < RTP_MAXCVARS; i++)
	{
		const char *cv = VidRules_PresetCvar(i, NULL);

		if (set[i] && strcmp(Cvar_VariableString(cv), vals[i]))
		{
			Com_Printf("  %s: \"%s\" -> \"%s\"\n", cv, Cvar_VariableString(cv), vals[i]);
			Cvar_Set(cv, vals[i]);
			changed++;
		}
	}

	Com_Printf("rt_preset: loaded %s, %d changed\n", name, changed);
	return 1;
}

int
RTPreset_LoadAB(const char *name)
{
	char vals[RTP_MAXCVARS][RTP_MAXVALUE];
	int set[RTP_MAXCVARS];

	if (!RTP_NameOk("rt_preset ab", name) || !RTP_Read("rt_preset ab", name, vals, set))
	{
		return 0;
	}

	memcpy(rtp_bvals, vals, sizeof(rtp_bvals));
	Q_strlcpy(rtp_bname, name, sizeof(rtp_bname));
	RTP_Publish();
	Com_Printf("rt_preset: B is preset %s; rt_ab flips; now %s\n", name, RTPreset_Side());
	return 1;
}

void
RTPreset_AB(void)
{
	rtp_side = !rtp_side;
	RTP_Publish();
	Com_Printf("rt_ab: %s\n", RTPreset_Side());
}

static void
RTPreset_List(void)
{
	char pattern[MAX_OSPATH];
	const char *f;
	int n = 0;

	n = snprintf(pattern, sizeof(pattern), "%s/presets/rt-*.cfg", rtp_dir ? rtp_dir : FS_Gamedir());
	if ((n < 0) || ((size_t)n >= sizeof(pattern)))
	{
		Com_Printf("rt_preset list: refused: the path is too long\n");
		return;
	}
	n = 0;

	for (f = Sys_FindFirst(pattern, 0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM); f;
		f = Sys_FindNext(0, SFF_SUBDIR | SFF_HIDDEN | SFF_SYSTEM))
	{
		const char *base = strrchr(f, '/');
		char name[VIDPRESET_MAXNAME + 8];
		size_t len;

		base = base ? base + 4 : f + 3;   /* past "/rt-" */
		Q_strlcpy(name, base, sizeof(name));
		len = strlen(name);
		if (len > 4)
		{
			name[len - 4] = '\0';   /* ".cfg" */
		}
		Com_Printf("  %s\n", name);
		n++;
	}
	Sys_FindClose();
	Com_Printf("rt_preset: %d preset%s\n", n, (n == 1) ? "" : "s");
}

static void
RTPreset_f(void)
{
	const char *verb = Cmd_Argv(1);

	if (Cmd_Argc() == 2 && !strcmp(verb, "list"))
	{
		RTPreset_List();
	}
	else if (Cmd_Argc() == 3 && !strcmp(verb, "save"))
	{
		RTPreset_Save(Cmd_Argv(2));
	}
	else if (Cmd_Argc() == 3 && !strcmp(verb, "load"))
	{
		RTPreset_Load(Cmd_Argv(2));
	}
	else if (Cmd_Argc() == 3 && !strcmp(verb, "ab"))
	{
		RTPreset_LoadAB(Cmd_Argv(2));
	}
	else
	{
		Com_Printf("usage: rt_preset save <name> | load <name> | ab <name> | list\n");
	}
}

static void
RTAB_f(void)
{
	if (Cmd_Argc() == 2 && !strcmp(Cmd_Argv(1), "stock"))
	{
		rtp_bname[0] = '\0';
		RTP_Publish();
		Com_Printf("rt_ab: B is stock; now %s\n", RTPreset_Side());
		return;
	}
	RTPreset_AB();
}

void
RTPreset_Init(void)
{
	/* not archived: every start shows A */
	Cvar_Get("r_rt_ab", "", 0);
	Cvar_Set("r_rt_ab", "");
	rtp_side = 0;
	rtp_bname[0] = '\0';

	Cmd_AddCommand("rt_ab", RTAB_f);
	Cmd_AddCommand("rt_preset", RTPreset_f);
}

/* the directory the presets live in, for the unit tests; NULL is <writedir> (FS_Gamedir) */
void
RTPreset_SetDir(const char *dir)
{
	rtp_dir = dir;
}
