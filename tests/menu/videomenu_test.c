/*
 * Host test of the video menu's glue (src/client/menu/videomenu.c and
 * qmenu.c, with the rules of vidrules.c): the real files, linked against
 * stubs for the cvar system and the drawing, driven the way a player
 * drives them with key presses and "apply". No SDL, no engine, no game
 * data. Run by tests/check-video-menu.sh.
 *
 * What it covers:
 *  - the MetalFX scale item gets its real choices back when a scaler is
 *    turned on, and apply writes only a scale the user chose;
 *  - apply writes each item's cvar under the renderer the menu was built
 *    for, not the one just selected;
 *  - a greyed item cannot be changed by Left, Right or Enter;
 *  - the scale item's "custom" entry exists only while the cvar holds a
 *    value that is not a step, and stepping never lands on it.
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "src/client/header/client.h"
#include "src/client/menu/header/qmenu.h"
#include "src/client/menu/header/vidrules.h"

/* ---- stubs the two files link against ---- */

client_static_t cls;
viddef_t viddef;
qboolean keydown[K_LAST];
cvar_t *vid_fullscreen, *vid_renderer;

static cvar_t pool[64];
static int writes[64];
static int npool;

static int
FindIndex(const char *n)
{
	for (int i = 0; i < npool; i++)
	{
		if (!strcmp(pool[i].name, n))
		{
			return i;
		}
	}

	return -1;
}

cvar_t *
Cvar_Get(const char *n, const char *v, int f)
{
	int i = FindIndex(n);

	if (i >= 0)
	{
		return &pool[i];
	}

	if (npool == (int)(sizeof(pool) / sizeof(pool[0])))
	{
		fprintf(stderr, "videomenu_test: cvar pool full\n");
		exit(2);
	}

	pool[npool].name = strdup(n);
	pool[npool].string = strdup(v);
	pool[npool].value = strtod(v, NULL);
	pool[npool].flags = f;

	return &pool[npool++];
}

/* A value the test sets, not a write by the menu. */
static void
SetCv(const char *n, const char *v)
{
	cvar_t *c = Cvar_Get(n, v, 0);

	free(c->string);
	c->string = strdup(v);
	c->value = strtod(v, NULL);
	writes[c - pool] = 0;
}

cvar_t *
Cvar_Set(const char *n, const char *v)
{
	cvar_t *c = Cvar_Get(n, v, 0);

	free(c->string);
	c->string = strdup(v);
	c->value = strtod(v, NULL);
	c->modified = true;
	writes[c - pool]++;

	return c;
}

void
Cvar_SetValue(const char *n, float v)
{
	char b[64];

	if (v == (int)v)
	{
		snprintf(b, sizeof(b), "%d", (int)v);
	}
	else
	{
		snprintf(b, sizeof(b), "%f", v);
	}

	Cvar_Set(n, b);
}

float
Cvar_VariableValue(const char *n)
{
	int i = FindIndex(n);

	return i < 0 ? 0 : (float)strtod(pool[i].string, NULL);
}

const char *
Cvar_VariableString(const char *n)
{
	int i = FindIndex(n);

	return i < 0 ? "" : pool[i].string;
}

static const char *
Str(const char *n)
{
	return Cvar_VariableString(n);
}

static int
Writes(const char *n)
{
	int i = FindIndex(n);

	return i < 0 ? 0 : writes[i];
}

void Cbuf_AddText(const char *t) { (void)t; }
void Com_Printf(const char *f, ...) { (void)f; }

void
Com_sprintf(char *d, int s, const char *f, ...)
{
	va_list a;

	va_start(a, f);
	vsnprintf(d, s, f, a);
	va_end(a);
}

int Q_stricmp(const char *a, const char *b) { return strcasecmp(a, b); }

int
Q_strlcpy(char *d, const char *s, int n)
{
	int l = (int)strlen(s);

	if (n > 0)
	{
		int k = l < n - 1 ? l : n - 1;

		memcpy(d, s, k);
		d[k] = 0;
	}

	return l;
}

void Q_strdel(char *s, size_t i, size_t n) { (void)s; (void)i; (void)n; }
size_t Q_strins(char *d, const char *s, size_t i, size_t n) { (void)d; (void)s; (void)i; (void)n; return 0; }
qboolean Q_strisnum(const char *s) { (void)s; return false; }
void Draw_CharScaled(int x, int y, int n, float s) { (void)x; (void)y; (void)n; (void)s; }
void Draw_Fill(int x, int y, int w, int h, int c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
void Draw_PicScaled(int x, int y, const char *p, float s) { (void)x; (void)y; (void)p; (void)s; }
void IN_GetClipboardText(char *o, size_t n) { (void)o; (void)n; }
int IN_SetClipboardText(const char *s) { (void)s; return 0; }
qboolean R_EndWorldRenderpass(void) { return true; }
static float test_scale = 1;
float SCR_GetMenuScale(void) { return test_scale; }
void Draw_GetPicSize(int *w, int *h, const char *n) { (void)n; *w = 232; *h = 32; }
int Sys_Milliseconds(void) { return 0; }
qboolean VID_HasRenderer(const char *r) { return strcmp(r, "vk") != 0; }

static menuframework_s *menu;

void M_PushMenu(menuframework_s *m) { menu = m; }
void M_ForceMenuOff(void) { }
const char *Default_MenuKey(menuframework_s *m, int k) { (void)m; (void)k; return NULL; }
void Default_MenuDraw(menuframework_s *m) { (void)m; }
void M_Menu_Video_f(void);

/* ---- helpers ---- */

static int failed, checks;

#define CHECK(cond, ...) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failed++; \
			printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
			printf(__VA_ARGS__); \
			printf("\n"); \
		} \
	} while (0)

/* The video menu and the page of the renderer's options, as opened. */
static menuframework_s *main_menu, *sub_menu;

/* Finds an item on either page and makes its page the current menu. */
static int
Find(const char *name)
{
	menuframework_s *pages[2] = { main_menu, sub_menu };

	for (int p = 0; p < 2; p++)
	{
		for (int i = 0; pages[p] && i < pages[p]->nitems; i++)
		{
			const menucommon_s *c = pages[p]->items[i];

			if (c->name && !strcmp(c->name, name))
			{
				menu = pages[p];

				return i;
			}
		}
	}

	printf("FAIL: no menu item named \"%s\"\n", name);
	failed++;
	exit(1);
}

static void
Cursor(const char *name)
{
	const int i = Find(name);

	menu->cursor = i;
}

static menucommon_s *
Item(const char *name)
{
	const int i = Find(name);

	return menu->items[i];
}

static menulist_s *
List(const char *name)
{
	return (menulist_s *)Item(name);
}

static int
Greyed(const char *name)
{
	return (Item(name)->flags & QMF_GRAYED) != 0;
}

static int
NumChoices(const menulist_s *l)
{
	int n = 0;

	while (l->itemnames[n])
	{
		n++;
	}

	return n;
}

static const char *
Shown(const char *name)
{
	menulist_s *l = List(name);

	return l->itemnames[l->curvalue];
}

static int
Slide(const char *name, int dir)
{
	Cursor(name);

	return Menu_SlideItem(menu, dir);
}

static void
Apply(void)
{
	Cursor("apply");
	Menu_SelectItem(menu);
}

/* Opens the menu with this renderer and Metal mode mask. */
static void
Open(const char *renderer, const char *modes)
{
	SetCv("vid_renderer", renderer);
	SetCv("r_metalfx_modes", modes);
	menu = main_menu = sub_menu = NULL;
	M_Menu_Video_f();
	main_menu = menu;

	/* into the renderer's page, as a player does with Enter */
	Cursor("renderer options...");
	if (!Menu_SelectItem(menu) || menu == main_menu)
	{
		printf("FAIL: \"renderer options...\" did not open its page\n");
		exit(1);
	}
	sub_menu = menu;

	/* the page has its own apply, the same action as the video menu's */
	{
		const menucommon_s *a = sub_menu->items[sub_menu->nitems - 1];
		const menucommon_s *b = main_menu->items[main_menu->nitems - 1];

		CHECK(a->name && b->name && !strcmp(a->name, "apply") && !strcmp(b->name, "apply") &&
			a->callback == b->callback, "the pages' apply items differ");
	}
}

/* Steps the renderer item to the one whose box text contains want. */
static int
PickRenderer(const char *want)
{
	menulist_s *l = List("renderer");

	for (int dir = -1; dir <= 1; dir += 2)
	{
		for (int guard = 0; guard < 16; guard++)
		{
			if (strstr(l->itemnames[l->curvalue], want))
			{
				return 1;
			}

			if (!Slide("renderer", dir))
			{
				break;
			}
		}
	}

	return strstr(l->itemnames[l->curvalue], want) != NULL;
}

static void
Reset(void)
{
	SetCv("r_metalfx", "0");
	SetCv("r_metalfx_scale", "0.5");
	SetCv("r_metalfx_interp", "0");
	SetCv("gl3_colorlight", "1");
	SetCv("gl4_colorlight", "1");
	SetCv("r_rt_ao", "0");
}

/* ---- scenarios ---- */

/* The scaler item turned on: the scale item gets its choices back. */
static void
test_scale_item_follows_the_scaler(void)
{
	Reset();
	Open("metal", "15");

	CHECK(Greyed("MetalFX scale"), "greyed with the scaler off");
	CHECK(!strcmp(Shown("MetalFX scale"), "n/a"), "shows %s", Shown("MetalFX scale"));

	CHECK(Slide("MetalFX scaler", 1), "scaler Right");
	CHECK(!Greyed("MetalFX scale"), "enabled with a scaler on");
	CHECK(NumChoices(List("MetalFX scale")) >= 6, "choices %d", NumChoices(List("MetalFX scale")));
	CHECK(!strcmp(Shown("MetalFX scale"), "1/2"), "reads the cvar: shows %s", Shown("MetalFX scale"));

	CHECK(Slide("MetalFX scale", 1), "scale Right");
	CHECK(!strcmp(Shown("MetalFX scale"), "2/3"), "shows %s", Shown("MetalFX scale"));

	Apply();
	CHECK(!strcmp(Str("r_metalfx"), "1"), "r_metalfx %s", Str("r_metalfx"));
	CHECK(fabsf(Cvar_VariableValue("r_metalfx_scale") - 2.0f / 3.0f) < 0.001f,
		"r_metalfx_scale %s", Str("r_metalfx_scale"));

	/* no scale step: apply leaves the cvar alone */
	Reset();
	Open("metal", "15");
	CHECK(Slide("MetalFX scaler", 1), "scaler Right");
	Apply();
	CHECK(!strcmp(Str("r_metalfx"), "1"), "r_metalfx %s", Str("r_metalfx"));
	CHECK(Writes("r_metalfx_scale") == 0, "r_metalfx_scale written %d times, now %s",
		Writes("r_metalfx_scale"), Str("r_metalfx_scale"));
	CHECK(!strcmp(Str("r_metalfx_scale"), "0.5"), "r_metalfx_scale %s", Str("r_metalfx_scale"));

	/* the scaler off again greys the item, and apply writes nothing */
	Reset();
	SetCv("r_metalfx", "1");
	Open("metal", "15");
	CHECK(!Greyed("MetalFX scale"), "enabled at open with a scaler on");
	CHECK(Slide("MetalFX scaler", -1), "scaler Left");
	CHECK(Greyed("MetalFX scale"), "greyed with the scaler off again");
	CHECK(!strcmp(Shown("MetalFX scale"), "n/a"), "shows %s", Shown("MetalFX scale"));
	Apply();
	CHECK(Writes("r_metalfx_scale") == 0, "r_metalfx_scale written");
	CHECK(!strcmp(Str("r_metalfx"), "0"), "r_metalfx %s", Str("r_metalfx"));
}

/* Apply names every cvar under the renderer the menu was built for. */
static void
test_apply_uses_the_built_renderer(void)
{
	Reset();
	Open("gl3", "0");

	if (!PickRenderer("OpenGL 4.6"))
	{
		printf("skip: no gl4 in the renderer list\n");
		return;
	}

	/* the menu's choice: color light off, the cvar said on */
	CHECK(!Greyed("color light"), "color light is greyed under gl3");
	CHECK(Slide("color light", -1), "color light Left");
	Apply();

	CHECK(!strcmp(Str("vid_renderer"), "gl4"), "vid_renderer %s", Str("vid_renderer"));
	CHECK(!strcmp(Str("gl3_colorlight"), "0"), "gl3_colorlight %s", Str("gl3_colorlight"));
	CHECK(!strcmp(Str("gl4_colorlight"), "1"), "gl4_colorlight %s, the choice went to the wrong cvar",
		Str("gl4_colorlight"));
	CHECK(Writes("gl4_colorlight") == 0, "gl4_colorlight written");
}

/* Frame generation is greyed under Metal whatever bit 16 says (the renderer
   does not build it yet): it shows "n/a", apply writes nothing for it, also
   when the player leaves Metal, and its cvar is kept as it was. */
static void
test_frame_generation_is_greyed_and_not_written(void)
{
	Reset();
	SetCv("r_metalfx_interp", "1");
	Open("metal", "31");

	CHECK(Greyed("frame generation"), "enabled with the interpolator present");
	CHECK(!strcmp(Shown("frame generation"), "n/a"), "shows %s", Shown("frame generation"));
	CHECK(!Slide("frame generation", -1), "frame generation Left changed a greyed item");
	CHECK(!strcmp(Shown("frame generation"), "n/a"), "shows %s after Left", Shown("frame generation"));
	Apply();
	CHECK(!strcmp(Str("r_metalfx_interp"), "1"), "r_metalfx_interp %s, written", Str("r_metalfx_interp"));

	if (PickRenderer("OpenGL 1.4"))
	{
		Apply();
		CHECK(!strcmp(Str("vid_renderer"), "gl1"), "vid_renderer %s", Str("vid_renderer"));
		CHECK(!strcmp(Str("r_metalfx_interp"), "1"), "r_metalfx_interp %s after leaving Metal", Str("r_metalfx_interp"));
	}

	/* the denoiser is greyed in the same way */
	Open("metal", "31");
	CHECK(Greyed("denoiser"), "denoiser enabled");
	CHECK(!strcmp(Shown("denoiser"), "n/a"), "denoiser shows %s", Shown("denoiser"));
}

/* TASK158: the water reflections' hit shading. Greyed with its reason while
   r_rt_water is off and under another renderer (it shows "n/a", apply writes
   nothing); with the water on it shows r_rt_water_hits as its index, and apply
   writes the index only when the player moved it. */
static void
test_water_hits_item(void)
{
	Reset();
	SetCv("r_rt_water_hits", "1");
	Open("metal", "31");
	CHECK(Greyed("water reflections"), "water reflections enabled with r_rt_water unset");
	CHECK(!strcmp(Shown("water reflections"), "n/a"), "water reflections shows %s", Shown("water reflections"));
	CHECK(!strcmp(Item("water reflections")->statusbar, "needs r_rt_water 1"), "reason %s",
		Item("water reflections")->statusbar);
	CHECK(!Slide("water reflections", 1), "water reflections slid while greyed");
	Apply();
	CHECK(Writes("r_rt_water_hits") == 0, "r_rt_water_hits written while greyed");

	Reset();
	SetCv("r_rt_water", "1");
	SetCv("r_rt_water_hits", "0.5");
	Open("metal", "31");
	CHECK(!Greyed("water reflections"), "water reflections greyed with r_rt_water 1");
	CHECK(!strcmp(Shown("water reflections"), "screen space"), "0.5 shows %s", Shown("water reflections"));
	Apply();
	CHECK(Writes("r_rt_water_hits") == 0, "an unchanged 0.5 was written");
	CHECK(Slide("water reflections", 1), "water reflections Right");
	CHECK(!strcmp(Shown("water reflections"), "world space"), "Right shows %s", Shown("water reflections"));
	Apply();
	CHECK(!strcmp(Str("r_rt_water_hits"), "1"), "r_rt_water_hits %s", Str("r_rt_water_hits"));

	Reset();
	SetCv("r_rt_water", "1");
	Open("gl1", "31");
	CHECK(Greyed("water reflections"), "water reflections enabled under gl1");
	CHECK(!strcmp(Item("water reflections")->statusbar, "Metal renderer only"), "gl1 reason %s",
		Item("water reflections")->statusbar);
}

/* r_metalfx is a cvar anyone can set: apply reads it as an index, so a value
   that is not a number or is huge neither traps nor writes. */
static void
test_apply_reads_r_metalfx_as_an_index(void)
{
	static const char *values[] = { "nan", "1e30", "-1e30", "inf", "2.9" };

	for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++)
	{
		Reset();
		SetCv("r_metalfx", values[i]);
		Open("metal", "15");
		Apply();
		CHECK(Writes("r_metalfx") == 0 && !strcmp(Str("r_metalfx"), values[i]),
			"r_metalfx %s was written, now %s", values[i], Str("r_metalfx"));
	}

	Reset();
	SetCv("r_metalfx", "1e30");
	Open("metal", "15");
	CHECK(!strcmp(Shown("MetalFX scaler"), "temporal denoised"), "1e30 shows %s", Shown("MetalFX scaler"));
	Apply();
	CHECK(Writes("r_metalfx") == 0, "r_metalfx written for an unchanged 1e30");

	Reset();
	SetCv("r_metalfx", "nan");
	Open("metal", "15");
	CHECK(!strcmp(Shown("MetalFX scaler"), "off"), "nan shows %s", Shown("MetalFX scaler"));
	Apply();
	CHECK(Writes("r_metalfx") == 0, "r_metalfx written for an unchanged nan");
}

/* Every status line either page can show fits the smallest virtual width,
   320 units of 8 per character: 40 characters. */
#define STATUS_MAX 40

static void
CheckStatus(const char *what, const char *text)
{
	CHECK(!text || strlen(text) <= STATUS_MAX, "%s: status \"%s\" is %d characters", what,
		text, (int)(text ? strlen(text) : 0));
}

static void
test_status_text_fits_the_screen(void)
{
	static const char *renderers[] = { "gl1", "gl3", "gl4", "vk", "soft", "metal", "mylib" };
	static const int masks[] = { 1, 3, 15, 31 };
	static const char *mnames[] = { "1", "3", "15", "31" };

	for (size_t r = 0; r < sizeof(renderers) / sizeof(renderers[0]); r++)
	{
		for (size_t m = 0; m < sizeof(masks) / sizeof(masks[0]); m++)
		{
			for (int it = 0; it < VIDITEM_COUNT; it++)
			{
				for (int sc = 0; sc <= 3; sc++)
				{
					CheckStatus("rule", VidRules_Item((viditem_t)it, renderers[r], masks[m], sc).text);
				}
			}

			Reset();
			Open(renderers[r], mnames[m]);

			for (int p = 0; p < 2; p++)
			{
				const menuframework_s *page = p ? sub_menu : main_menu;

				for (int i = 0; i < page->nitems; i++)
				{
					CheckStatus(renderers[r], ((const menucommon_s *)page->items[i])->statusbar);
				}
			}
		}
	}
}

/* Enter, Left and Right on a greyed item change nothing and run nothing. */
static void
test_greyed_items_cannot_be_changed(void)
{
	static const char *const names[] = {
		"color intensity", "min. light level", "overbrights", "color light", "bloom",
		"dynamic light", "MetalFX scaler", "MetalFX scale", "frame generation", "denoiser",
		"water reflections",
		"AO trace size", "AO rays", "colour space"
	};
	static const char *const renderers[] = { "soft", "gl3", "gl1", "metal" };

	for (size_t r = 0; r < sizeof(renderers) / sizeof(renderers[0]); r++)
	{
		Reset();
		Open(renderers[r], "15");
		main_menu->statusbar = NULL;
		sub_menu->statusbar = NULL;

		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		{
			menucommon_s *c = Item(names[i]);
			int before = 0;

			if (!(c->flags & QMF_GRAYED))
			{
				continue;
			}

			if (c->type == MTYPE_SPINCONTROL)
			{
				before = ((menulist_s *)c)->curvalue;
			}

			Cursor(names[i]);
			CHECK(!Menu_SlideItem(menu, 1) && !Menu_SlideItem(menu, -1),
				"%s: %s slid while greyed", renderers[r], names[i]);
			CHECK(!Menu_SelectItem(menu), "%s: %s selected while greyed", renderers[r], names[i]);
			CHECK(main_menu->statusbar == NULL && sub_menu->statusbar == NULL,
				"%s: %s ran its callback while greyed", renderers[r], names[i]);

			if (c->type == MTYPE_SPINCONTROL)
			{
				CHECK(((menulist_s *)c)->curvalue == before, "%s: %s changed", renderers[r], names[i]);
			}
		}
	}
}

/* A scaler moved between two modes keeps the scale the player stepped to,
   and a greyed item's cvar is not written from its "n/a" value. */
static void
test_scale_survives_a_scaler_move_and_greyed_cvars_stay(void)
{
	Reset();
	Open("metal", "15");
	CHECK(Slide("MetalFX scaler", 1), "scaler Right");
	CHECK(Slide("MetalFX scale", 1), "scale Right");
	CHECK(Slide("MetalFX scaler", 1), "scaler Right again");
	CHECK(!strcmp(Shown("MetalFX scale"), "2/3"), "the step was lost: shows %s", Shown("MetalFX scale"));

	/* modes 15 has no interpolator: frame generation is greyed and its cvar kept */
	Reset();
	SetCv("r_metalfx_interp", "1");
	Open("metal", "15");
	CHECK(Greyed("frame generation"), "enabled without an interpolator");
	Apply();
	CHECK(!strcmp(Str("r_metalfx_interp"), "1"), "r_metalfx_interp %s", Str("r_metalfx_interp"));

	/* no item at the cursor: nothing runs */
	main_menu->cursor = -1;
	CHECK(!Menu_SelectItem(main_menu), "selected with the cursor off the list");
}

/* "custom" is a choice only while the cvar holds a value that is not a step. */
static void
test_custom_scale_is_not_a_choice(void)
{
	Reset();
	SetCv("r_metalfx", "1");
	Open("metal", "15");

	CHECK(NumChoices(List("MetalFX scale")) == 6, "a step shows %d choices", NumChoices(List("MetalFX scale")));

	for (int i = 0; i < 12; i++)
	{
		Slide("MetalFX scale", 1);
		CHECK(strcmp(Shown("MetalFX scale"), "custom") != 0, "stepping Right landed on custom");
	}

	for (int i = 0; i < 12; i++)
	{
		Slide("MetalFX scale", -1);
		CHECK(strcmp(Shown("MetalFX scale"), "custom") != 0, "stepping Left landed on custom");
	}

	/* a value that is not a step: custom shows until the player steps off it */
	Reset();
	SetCv("r_metalfx", "1");
	SetCv("r_metalfx_scale", "0.4");
	Open("metal", "15");
	CHECK(!strcmp(Shown("MetalFX scale"), "custom"), "shows %s", Shown("MetalFX scale"));
	Apply();
	CHECK(Writes("r_metalfx_scale") == 0 && !strcmp(Str("r_metalfx_scale"), "0.4"),
		"custom value rewritten: %s", Str("r_metalfx_scale"));

	Reset();
	SetCv("r_metalfx", "1");
	SetCv("r_metalfx_scale", "0.4");
	Open("metal", "15");
	CHECK(Slide("MetalFX scale", -1), "Left off custom");
	CHECK(!strcmp(Shown("MetalFX scale"), "full"), "shows %s", Shown("MetalFX scale"));
	for (int i = 0; i < 4; i++)
	{
		Slide("MetalFX scale", 1);
		CHECK(strcmp(Shown("MetalFX scale"), "custom") != 0, "stepping landed on custom again");
	}
	Apply();
	CHECK(!strcmp(Str("r_metalfx_scale"), "1"), "r_metalfx_scale %s", Str("r_metalfx_scale"));
}

/* The AO items with r_rt_ao on: the values shown, the writes on apply, and
   no write for an item left as it was. */
static void
test_ao_items_apply(void)
{
	Reset();
	SetCv("r_rt_ao", "1");
	SetCv("r_rt_ao_scale", "0.5");
	SetCv("r_rt_ao_rays", "2");
	Open("metal", "31");
	CHECK(!Greyed("AO trace size"), "AO trace size greyed with r_rt_ao 1");
	CHECK(!Greyed("AO rays"), "AO rays greyed with r_rt_ao 1");
	CHECK(!strcmp(Shown("AO trace size"), "1/2"), "scale shows %s", Shown("AO trace size"));
	CHECK(!strcmp(Shown("AO rays"), "2"), "rays shows %s", Shown("AO rays"));
	CHECK(Slide("AO trace size", 1), "scale Right");
	CHECK(Slide("AO rays", -1), "rays Left");
	Apply();
	CHECK(!strcmp(Str("r_rt_ao_scale"), "0.333333"), "r_rt_ao_scale %s", Str("r_rt_ao_scale"));
	CHECK(!strcmp(Str("r_rt_ao_rays"), "1"), "r_rt_ao_rays %s", Str("r_rt_ao_rays"));

	/* the other way: scale to full, rays to 2 */
	Reset();
	SetCv("r_rt_ao", "1");
	SetCv("r_rt_ao_scale", "0.5");
	SetCv("r_rt_ao_rays", "1");
	Open("metal", "31");
	CHECK(Slide("AO trace size", -1), "scale Left");
	CHECK(Slide("AO rays", 1), "rays Right");
	Apply();
	CHECK(!strcmp(Str("r_rt_ao_scale"), "1"), "r_rt_ao_scale %s", Str("r_rt_ao_scale"));
	CHECK(!strcmp(Str("r_rt_ao_rays"), "2"), "r_rt_ao_rays %s", Str("r_rt_ao_rays"));

	/* unchanged items are not written */
	Reset();
	SetCv("r_rt_ao", "1");
	SetCv("r_rt_ao_scale", "0.5");
	SetCv("r_rt_ao_rays", "2");
	Open("metal", "31");
	Apply();
	CHECK(Writes("r_rt_ao_scale") == 0, "r_rt_ao_scale written without a change");
	CHECK(Writes("r_rt_ao_rays") == 0, "r_rt_ao_rays written without a change");

	/* r_rt_ao off: greyed, nothing written */
	Reset();
	SetCv("r_rt_ao_scale", "0.5");
	SetCv("r_rt_ao_rays", "2");
	Open("metal", "31");
	CHECK(Greyed("AO trace size") && Greyed("AO rays"), "AO items enabled with r_rt_ao 0");
	Apply();
	CHECK(Writes("r_rt_ao_scale") == 0 && Writes("r_rt_ao_rays") == 0, "AO cvars written while greyed");
}

/* TASK121: the colour space item: always shown, greyed with its reason off Metal, written when it changed */
static void
test_colorspace_item(void)
{
	static const char *const others[] = { "soft", "gl3", "gl1" };

	for (size_t r = 0; r < sizeof(others) / sizeof(others[0]); r++)
	{
		Reset();
		Open(others[r], "15");
		CHECK(Greyed("colour space"), "%s: colour space enabled", others[r]);
		CHECK(!strcmp(Item("colour space")->statusbar, "Metal renderer only"),
			"%s: reason %s", others[r], Item("colour space")->statusbar);
		Apply();
		CHECK(Writes("metal_colorspace") == 0, "%s: metal_colorspace written while greyed", others[r]);
	}

	/* a value of 1 with a greyed item: apply must not write it (the guard is the only thing that stops it) */
	Reset();
	SetCv("metal_colorspace", "1");
	Open("gl1", "15");
	CHECK(Greyed("colour space"), "gl1 with 1: colour space enabled");
	Apply();
	CHECK(Writes("metal_colorspace") == 0, "gl1 with 1: metal_colorspace written while greyed");
	SetCv("metal_colorspace", "0");

	Reset();
	Open("metal", "15");
	CHECK(!Greyed("colour space"), "colour space greyed on Metal");
	CHECK(!strcmp(Shown("colour space"), "untagged"), "shows %s", Shown("colour space"));
	Apply();
	CHECK(Writes("metal_colorspace") == 0, "written without a change");
	CHECK(Slide("colour space", 1), "Right");
	CHECK(!strcmp(Shown("colour space"), "sRGB"), "shows %s", Shown("colour space"));
	Apply();
	CHECK(!strcmp(Str("metal_colorspace"), "1"), "metal_colorspace %s", Str("metal_colorspace"));

	Reset();
	SetCv("metal_colorspace", "1");
	Open("metal", "15");
	CHECK(!strcmp(Shown("colour space"), "sRGB"), "shows %s", Shown("colour space"));
	CHECK(Slide("colour space", -1), "Left");
	Apply();
	CHECK(!strcmp(Str("metal_colorspace"), "0"), "metal_colorspace %s", Str("metal_colorspace"));
}

/*
 * No item is drawn over the banner. Both edges are computed the way the
 * menu draws them, in the menu's own units (pixels / scale): the banner
 * (M_Banner) from its picture's height, the first item from the menu's
 * y (Menu_Center) plus the item's y (Menu_Draw); and the last item,
 * the apply action, ends inside the screen.
 */
static void
test_the_banner_is_clear_of_the_items(void)
{
	static const struct { int w, h; float scale; } screens[] = {
		{ 640, 480, 1 },	/* r_menuscale auto at 640x480 */
		{ 640, 480, 2 },	/* the smallest virtual height, 240 */
		{ 1280, 960, 4 },
	};

	for (size_t i = 0; i < sizeof(screens) / sizeof(screens[0]); i++)
	{
		int bw, bh;

		viddef.width = screens[i].w;
		viddef.height = screens[i].h;
		test_scale = screens[i].scale;
		Reset();
		Open("metal", "15");

		Draw_GetPicSize(&bw, &bh, "m_banner_video");

		for (int p = 0; p < 2; p++)
		{
			const menuframework_s *page = p ? sub_menu : main_menu;
			const float vh = viddef.height / test_scale;
			const float banner_bottom = vh / 2 - 110 + bh;
			const menucommon_s *first = page->items[0];
			const menucommon_s *last = page->items[page->nitems - 1];
			const float first_top = page->y + first->y;
			const float last_bottom = page->y + last->y + 10;

			printf("%dx%d scale %g, %s page: banner bottom %g, first item %g, last item bottom %g of %g\n",
				screens[i].w, screens[i].h, test_scale, p ? "renderer" : "video", banner_bottom,
				first_top, last_bottom, vh);
			CHECK(first_top >= banner_bottom, "%dx%d scale %g page %d: the first item (%g) is over the banner (bottom %g)",
				screens[i].w, screens[i].h, test_scale, p, first_top, banner_bottom);
			CHECK(last_bottom <= vh, "%dx%d scale %g page %d: the last item ends at %g below the screen %g",
				screens[i].w, screens[i].h, test_scale, p, last_bottom, vh);
		}
	}

	viddef.width = 640;
	viddef.height = 480;
	test_scale = 1;
}

/*
 * TASK147: the screen's own items. Built for the desktop (the default)
 * nothing is greyed and there is no display row; built with
 * -DTEST_IOS=1 -DVID_PLATFORM=VIDPLAT_IOS (check-video-menu.sh builds
 * both) the mode, fullscreen, display, vertical sync and renderer are
 * greyed with their reasons, never hidden, cannot be changed, and apply
 * writes none of their cvars even where the cvars say otherwise.
 */
static void
test_screen_items(void)
{
	static const char *const names[] = { "video mode", "fullscreen", "vertical sync", "renderer" };
	static const char *const reasons[] = { "the screen's own size", "always the whole screen", "always on", "metal is linked in" };
	static const char *const cvars[] = { "r_mode", "vid_fullscreen", "r_vsync", "vid_renderer" };

	Reset();
	Cvar_Get("r_vsync", "1", 0);
	SetCv("r_mode", "3");
	SetCv("vid_fullscreen", "1");
	SetCv("r_vsync", "1");
	Open("metal", "15");

	for (int i = 0; i < 4; i++)
	{
#ifdef TEST_IOS
		CHECK(Greyed(names[i]), "%s is not greyed on iOS", names[i]);
		CHECK(Item(names[i])->statusbar && !strcmp(Item(names[i])->statusbar, reasons[i]),
			"%s: status \"%s\"", names[i], Item(names[i])->statusbar ? Item(names[i])->statusbar : "(none)");
		CHECK(!Slide(names[i], 1) && !Slide(names[i], -1), "%s changed on iOS", names[i]);
#else
		CHECK(!Greyed(names[i]), "%s is greyed on the desktop", names[i]);
		(void)reasons;
#endif
	}

#ifdef TEST_IOS
	CHECK(Greyed("display"), "display is not greyed on iOS");
	CHECK(Item("display")->statusbar && !strcmp(Item("display")->statusbar, "one screen"), "display status");
	CHECK(!Slide("display", 1) && !Slide("display", -1), "display changed on iOS");
#else
	{
		int found = 0;

		for (int i = 0; i < main_menu->nitems; i++)
		{
			const menucommon_s *c = main_menu->items[i];

			found |= (c->name && !strcmp(c->name, "display"));
		}

		CHECK(!found, "the desktop menu has a display row");
	}
#endif

	Apply();

	for (int i = 0; i < 4; i++)
	{
#ifdef TEST_IOS
		CHECK(Writes(cvars[i]) == 0, "apply wrote %s on iOS", cvars[i]);
#else
		(void)cvars;
#endif
	}

#ifdef TEST_IOS
	CHECK(!strcmp(Str("r_mode"), "3") && !strcmp(Str("vid_fullscreen"), "1"), "r_mode %s, vid_fullscreen %s",
		Str("r_mode"), Str("vid_fullscreen"));
#else
	/* the desktop writes them, as before */
	CHECK(Writes("r_mode") > 0, "apply wrote no r_mode on the desktop");
#endif
}

/* TASK194: the "ray tracing..." entry is in the video menu under every
   renderer, never hidden: greyed with its reason off Metal, live on Metal. */
static void
test_ray_tracing_entry_is_always_present(void)
{
	Open("metal", "7");
	CHECK(main_menu != NULL, "the video menu did not open");
	menu = main_menu;
	CHECK(!Greyed("ray tracing..."), "the ray tracing entry is greyed under Metal");

	Open("soft", "7");
	menu = main_menu;
	CHECK(Greyed("ray tracing..."), "the ray tracing entry is not greyed off Metal");
	CHECK(Item("ray tracing...")->statusbar &&
		strstr(Item("ray tracing...")->statusbar, "needs the Metal renderer"),
		"the ray tracing entry has no reason off Metal");
}

int
main(void)
{
	vid_fullscreen = Cvar_Get("vid_fullscreen", "0", 0);
	vid_renderer = Cvar_Get("vid_renderer", "gl3", 0);
	Cvar_Get("r_mode", "4", 0);

	test_screen_items();
	test_scale_item_follows_the_scaler();
	test_apply_uses_the_built_renderer();
	test_frame_generation_is_greyed_and_not_written();
	test_water_hits_item();
	test_apply_reads_r_metalfx_as_an_index();
	test_status_text_fits_the_screen();
	test_greyed_items_cannot_be_changed();
	test_scale_survives_a_scaler_move_and_greyed_cvars_stay();
	test_custom_scale_is_not_a_choice();
	test_ao_items_apply();
	test_colorspace_item();
	test_the_banner_is_clear_of_the_items();
	test_ray_tracing_entry_is_always_present();

	printf("videomenu_test: %d checks, %d failed\n", checks, failed);

	return failed ? 1 : 0;
}
