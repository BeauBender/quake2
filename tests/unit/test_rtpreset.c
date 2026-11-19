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

static void
FreshTable(void)
{
	cvar_vars = NULL;   /* a fresh table: this start */

	for (int i = 0; i < VidRules_PresetNum(); i++)
	{
		const char *def;
		const char *cv = VidRules_PresetCvar(i, &def);

		Cvar_Get(cv, def, CVAR_ARCHIVE);
	}
	RTPreset_SetDir(t_tmpdir());
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
	FreshTable();
	T_EQ_INT(RTPreset_Save("../escape"), 0);
	T_CONSOLE_HAS("refused");
	T_EQ_INT(RTPreset_Load("a/b"), 0);
	T_EQ_INT(RTPreset_LoadAB(".x"), 0);
	RTPreset_SetDir(NULL);
}

TEST(rtpreset_save_load_round_trip)
{
	const int n = VidRules_PresetNum();
	char want[32][64];

	T_REQUIRE(n > 12 && n <= 32);
	FreshTable();
	for (int i = 0; i < n; i++)
	{
		const char *cv = VidRules_PresetCvar(i, NULL);

		Cvar_Set(cv, Other(i));
		snprintf(want[i], sizeof(want[i]), "%s", Cvar_VariableString(cv));
	}
	T_EQ_INT(RTPreset_Save("round"), 1);

	/* a fresh start: every default, then the preset */
	FreshTable();
	T_CHECK(strcmp(Cvar_VariableString(VidRules_PresetCvar(0, NULL)), want[0]) != 0);
	T_EQ_INT(RTPreset_Load("round"), 1);
	for (int i = 0; i < n; i++)
	{
		T_EQ_STR(Cvar_VariableString(VidRules_PresetCvar(i, NULL)), want[i]);
	}
	T_CONSOLE_HAS("loaded round");
	RTPreset_SetDir(NULL);
}

TEST(rtpreset_load_writes_only_tuning_cvars)
{
	char path[MAX_OSPATH];
	FILE *f;

	FreshTable();
	Cvar_Get("r_mode", "4", 0);
	Cvar_Get("r_rt_ao_seed", "0", 0);
	snprintf(path, sizeof(path), "%s/presets/rt-other.cfg", t_tmpdir());
	FS_CreatePath(path);
	f = fopen(path, "w");
	T_REQUIRE(f != NULL);
	fprintf(f, "// rt preset other\nset r_mode \"9\"\nset r_rt_ao_seed \"7\"\nset r_rt_ao \"1\"\n"
		"set r_rt_sun \"1;quit\"\nalias x quit\n");
	fclose(f);

	T_EQ_INT(RTPreset_Load("other"), 1);
	T_EQ_STR(Cvar_VariableString("r_mode"), "4");
	T_EQ_STR(Cvar_VariableString("r_rt_ao_seed"), "0");
	T_EQ_STR(Cvar_VariableString("r_rt_ao"), "1");
	T_EQ_STR(Cvar_VariableString("r_rt_sun"), "0");   /* a value with a separator is passed over */
	RTPreset_SetDir(NULL);
}

TEST(rtpreset_ab_writes_no_tuning_cvar)
{
	const int n = VidRules_PresetNum();
	char before[32][64];
	const char *spec;

	FreshTable();
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
	RTPreset_SetDir(NULL);
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
