/*
 * The ray-tracing tuning harness's presets and A/B toggle (TASK195,
 * src/client/menu/rtpreset.c with the rules of vidrules.c), on the real
 * cvar and command system: the name check refuses every form that could
 * leave the presets directory, a save then load brings every value back
 * on a fresh cvar table, load writes only the tuning cvars, and rt_ab
 * writes none of them, only r_rt_ab, whose B side holds the defaults or
 * the preset.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test.h"
#include "../../src/client/menu/header/vidrules.h"

static cvar_t *kept_vars;

/* a fresh table: this start; Restore puts the suite's table back for the tests after this file */
static void
FreshTable(int register_all)
{
	if (!kept_vars)
	{
		kept_vars = cvar_vars ? cvar_vars : (cvar_t *)-1;
	}
	cvar_vars = NULL;

	for (int i = 0; register_all && i < VidRules_PresetNum(); i++)
	{
		const char *def;
		const char *cv = VidRules_PresetCvar(i, &def);

		Cvar_Get(cv, def, CVAR_ARCHIVE);
	}
	RTPreset_SetDir(t_tmpdir());
}

static void
Restore(void)
{
	RTPreset_SetDir(NULL);
	if (kept_vars)
	{
		cvar_vars = (kept_vars == (cvar_t *)-1) ? NULL : kept_vars;
		kept_vars = NULL;
	}
}

/* T_REQUIRE leaves the test: the suite's table is put back first */
#define RTP_REQUIRE(COND) \
	do { if (!(COND)) { Restore(); T_REQUIRE(COND); } } while (0)

/* whether name is in the cvar table at all (Cvar_VariableString reads "" for both) */
static int
Registered(const char *name)
{
	for (const cvar_t *v = cvar_vars; v; v = v->next)
	{
		if (!strcmp(v->name, name))
		{
			return 1;
		}
	}
	return 0;
}

/* a value other than the default for row i, as a slider or a choice would set */
static const char *
Other(int i)
{
	const char *def;
	const char *cv = VidRules_PresetCvar(i, &def);
	static char buf[32];

	if (!strcmp(cv, "r_rt_sun_angles"))
	{
		return "30 120";
	}
	snprintf(buf, sizeof(buf), "%g", (double)strtof(def, NULL) + 1.0);
	return buf;
}

TEST(rtpreset_name_check)
{
	static const char *const bad[] = {
		"", "..", "a..b", "a/b", "/abs", "a\\b", ".hidden", "sp ace", "semi;colon", "quo\"te",
		"x123456789012345678901234567890123", "tab\t", "dot.cfg"
	};

	for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		if (!VidRules_PresetNameError(bad[i]))
		{
			t_fail(__FILE__, __LINE__, "the name \"%s\" is accepted", bad[i]);
		}
	}
	T_CHECK(VidRules_PresetNameError(NULL) != NULL);
	T_CHECK(VidRules_PresetNameError("owner-look_2") == NULL);
	T_CHECK(VidRules_PresetNameError("x1234567890123456789012345678901") == NULL);   /* 32 */

	/* the commands refuse too, and write nothing */
	FreshTable(1);
	T_EQ_INT(RTPreset_Save("../escape"), 0);
	T_CONSOLE_HAS("refused");
	T_EQ_INT(RTPreset_Load("a/b"), 0);
	T_EQ_INT(RTPreset_LoadAB(".x"), 0);
	Restore();
}

TEST(rtpreset_save_load_round_trip)
{
	const int n = VidRules_PresetNum();
	char want[32][64];

	T_REQUIRE(n > 12 && n <= 32);
	FreshTable(1);
	for (int i = 0; i < n; i++)
	{
		const char *cv = VidRules_PresetCvar(i, NULL);

		Cvar_Set(cv, Other(i));
		snprintf(want[i], sizeof(want[i]), "%s", Cvar_VariableString(cv));
	}
	T_EQ_INT(RTPreset_Save("round"), 1);

	/* a fresh start: every default, then the preset */
	FreshTable(1);
	T_CHECK(strcmp(Cvar_VariableString(VidRules_PresetCvar(0, NULL)), want[0]) != 0);
	T_EQ_INT(RTPreset_Load("round"), 1);
	for (int i = 0; i < n; i++)
	{
		T_EQ_STR(Cvar_VariableString(VidRules_PresetCvar(i, NULL)), want[i]);
	}
	T_CONSOLE_HAS("loaded round");
	Restore();
}

TEST(rtpreset_load_writes_only_tuning_cvars)
{
	char path[MAX_OSPATH];
	FILE *f;

	FreshTable(1);
	Cvar_Get("r_mode", "4", 0);
	Cvar_Get("r_rt_ao_seed", "0", 0);
	snprintf(path, sizeof(path), "%s/presets/rt-other.cfg", t_tmpdir());
	FS_CreatePath(path);
	f = fopen(path, "w");
	RTP_REQUIRE(f != NULL);
	fprintf(f, "// rt preset other\nset r_mode \"9\"\nset r_rt_ao_seed \"7\"\nset r_rt_ao \"1\"\n"
		"set r_rt_sun \"1;quit\"\nalias x quit\n");
	fclose(f);

	T_EQ_INT(RTPreset_Load("other"), 1);
	T_EQ_STR(Cvar_VariableString("r_mode"), "4");
	T_EQ_STR(Cvar_VariableString("r_rt_ao_seed"), "0");
	T_EQ_STR(Cvar_VariableString("r_rt_ao"), "1");
	T_EQ_STR(Cvar_VariableString("r_rt_sun"), "0");   /* a value with a separator is passed over */
	Restore();
}

TEST(rtpreset_ab_writes_no_tuning_cvar)
{
	const int n = VidRules_PresetNum();
	char before[32][64];
	const char *spec;

	FreshTable(1);
	Cvar_Get("r_rt_ab", "", 0);
	Cvar_Set("r_rt_ao", "1");
	Cvar_Set("r_rt_ao_range", "512");
	for (int i = 0; i < n; i++)
	{
		snprintf(before[i], sizeof(before[i]), "%s", Cvar_VariableString(VidRules_PresetCvar(i, NULL)));
	}

	/* B: stock, every default */
	RTPreset_AB();
	T_EQ_STR(RTPreset_Side(), "B: stock");
	spec = Cvar_VariableString("r_rt_ab");
	T_CHECK(strncmp(spec, "B: stock|", 9) == 0);
	T_CHECK(strstr(spec, "r_rt_ao=0;") != NULL);
	T_CHECK(strstr(spec, "r_rt_ao_range=128;") != NULL);
	T_CHECK(strstr(spec, "r_rt_sun_angles=55 45;") != NULL);
	for (int i = 0; i < n; i++)
	{
		T_EQ_STR(Cvar_VariableString(VidRules_PresetCvar(i, NULL)), before[i]);
	}

	/* A again: the overlay is empty */
	RTPreset_AB();
	T_EQ_STR(RTPreset_Side(), "A: your settings");
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");

	/* B: a preset, loaded as the B side only */
	Cvar_Set("r_rt_ao_range", "256");
	T_EQ_INT(RTPreset_Save("bside"), 1);
	Cvar_Set("r_rt_ao_range", "512");
	T_EQ_INT(RTPreset_LoadAB("bside"), 1);
	T_EQ_STR(Cvar_VariableString("r_rt_ab"), "");   /* still A until flipped */
	RTPreset_AB();
	T_EQ_STR(RTPreset_Side(), "B: preset bside");
	T_CHECK(strstr(Cvar_VariableString("r_rt_ab"), "r_rt_ao_range=256;") != NULL);
	T_EQ_STR(Cvar_VariableString("r_rt_ao_range"), "512");
	for (int i = 0; i < n; i++)
	{
		T_EQ_STR(Cvar_VariableString(VidRules_PresetCvar(i, NULL)), before[i]);
	}
	RTPreset_AB();
	Restore();
}

TEST(rtpreset_cvars_are_the_look)
{
	/* each cvar once; neither a debug view nor the presentation (a restart) */
	for (int i = 0; i < VidRules_PresetNum(); i++)
	{
		const char *cv = VidRules_PresetCvar(i, NULL);

		T_CHECK(!strstr(cv, "_show") && strcmp(cv, "r_presentation"));
		for (int j = 0; j < i; j++)
		{
			T_CHECK(strcmp(VidRules_PresetCvar(j, NULL), cv) != 0);
		}
	}
	T_CHECK(VidRules_IsPresetCvar("r_rt_sun_angles") && VidRules_IsPresetCvar("r_metalfx"));
	T_CHECK(!VidRules_IsPresetCvar("r_mode"));
}

/*
 * Under gl1 most tuning cvars are not registered: a save writes no line
 * for them (nor for a value a preset may not hold), a load creates none,
 * an empty value in a file is no setting, and the B side reads the row's
 * default where the current value is unset or may not reach r_rt_ab.
 */
TEST(rtpreset_unset_cvars_stay_unset)
{
	char path[MAX_OSPATH], text[4096];
	const char *spec;
	size_t got;
	FILE *f;

	FreshTable(0);
	Cvar_Get("r_rt_ab", "", 0);
	Cvar_Get("r_rt_ao", "1", 0);
	Cvar_Get("r_rt_ao_range", "1;2", 0);   /* a value with r_rt_ab's separator */

	T_EQ_INT(RTPreset_Save("sparse"), 1);
	snprintf(path, sizeof(path), "%s/presets/rt-sparse.cfg", t_tmpdir());
	f = fopen(path, "r");
	RTP_REQUIRE(f != NULL);
	got = fread(text, 1, sizeof(text) - 1, f);
	fclose(f);
	text[got] = '\0';
	T_CHECK(strstr(text, "set r_rt_ao \"1\"") != NULL);
	T_CHECK(strstr(text, "\"\"") == NULL);
	T_CHECK(strstr(text, "r_rt_ao_range") == NULL);
	T_CHECK(strstr(text, "r_rt_sun_angles") == NULL);

	T_EQ_INT(RTPreset_Load("sparse"), 1);
	T_CONSOLE_HAS("loaded sparse, 0 changed");
	for (int i = 0; i < VidRules_PresetNum(); i++)
	{
		const char *cv = VidRules_PresetCvar(i, NULL);

		if (strcmp(cv, "r_rt_ao") && strcmp(cv, "r_rt_ao_range") && Registered(cv))
		{
			t_fail(__FILE__, __LINE__, "load registered %s, which the save did not set", cv);
		}
	}
	T_EQ_STR(Cvar_VariableString("r_rt_ao_range"), "1;2");

	/* an empty value in a file is no setting */
	f = fopen(path, "w");
	RTP_REQUIRE(f != NULL);
	fprintf(f, "set r_rt_ao \"\"\nset r_rt_sun \"\"\n");
	fclose(f);
	T_EQ_INT(RTPreset_Load("sparse"), 1);
	T_EQ_STR(Cvar_VariableString("r_rt_ao"), "1");
	T_CHECK(!Registered("r_rt_sun"));

	/* the B side: the row's default where the current value is unset or may not reach r_rt_ab */
	T_EQ_INT(RTPreset_LoadAB("sparse"), 1);
	RTPreset_AB();
	spec = Cvar_VariableString("r_rt_ab");
	T_CHECK(strstr(spec, "r_rt_ao=1;") != NULL);
	T_CHECK(strstr(spec, "r_rt_ao_range=128;") != NULL);
	T_CHECK(strstr(spec, "1;2") == NULL);
	T_CHECK(strstr(spec, "r_rt_sun_angles=55 45;") != NULL);
	T_CHECK(strstr(spec, "=;") == NULL);
	RTPreset_AB();
	Restore();
}
