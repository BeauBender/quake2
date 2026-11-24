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

static cvar_t pool[128];
static int writes[128];
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

static int restarts;	/* the vid_restart commands queued */
static char cbuf_last[128];	/* TASK195: the last command queued */
void Cbuf_AddText(const char *t) { if (t && strstr(t, "vid_restart")) { restarts++; } if (t) { snprintf(cbuf_last, sizeof(cbuf_last), "%s", t); } }
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

/* ---- TASK194: the ray-tracing screen itself ---- */

static menuframework_s *rt_menu;

static menucommon_s *
RTItem(const char *name)
{
	for (int i = 0; i < rt_menu->nitems; i++)
	{
		const menucommon_s *c = rt_menu->items[i];

		if (c->name && !strcmp(c->name, name))
		{
			rt_menu->cursor = i;

			return rt_menu->items[i];
		}
	}

	printf("FAIL: no ray tracing item named \"%s\"\n", name);
	failed++;
	exit(1);
}

static int
RTGreyed(const char *name)
{
	return (RTItem(name)->flags & QMF_GRAYED) != 0;
}

/* Opens the video menu under metal and its ray tracing screen. */
static void
OpenRT(const char *avail)
{
	/* earlier cases leave values behind: a fresh start is every default */
	for (int i = 0; i < VidRules_RTNum(); i++)
	{
		SetCv(VidRules_RTControl(i)->cvar, VidRules_RTControl(i)->def);
	}
	SetCv("r_rt_available", avail);
	Open("metal", "7");
	menu = main_menu;
	Cursor("ray tracing...");
	Menu_SelectItem(menu);
	rt_menu = menu;
}

static void
test_ray_tracing_screen(void)
{
	static const char *const sw[] = { "shadows", "sun", "ambient occlusion", "water reflections", "gun shadow" };
	static const char *const sl[] = { "sun floor", "sun scale", "AO range", "AO minimum", "AO scale",
		"water reflect", "water ripple" };

	/* 1: the screen has its 13 items; the sliders wait for their effects */
	OpenRT("1");
	CHECK(rt_menu != main_menu && rt_menu->nitems == 14, "the screen has %d items", rt_menu->nitems);
	for (size_t i = 0; i < sizeof(sw) / sizeof(sw[0]); i++)
	{
		CHECK(!RTGreyed(sw[i]), "%s greyed", sw[i]);
	}
	for (size_t i = 0; i < sizeof(sl) / sizeof(sl[0]); i++)
	{
		CHECK(RTGreyed(sl[i]), "%s live with its effect off", sl[i]);
	}
	CHECK(RTItem("sun floor")->statusbar && !strcmp(RTItem("sun floor")->statusbar, "turn on sun first"),
		"sun floor says %s", RTItem("sun floor")->statusbar);
	CHECK(!RTGreyed("reset to defaults"), "reset to defaults greyed");

	/* 2: Right on sun writes 1 and frees sun floor */
	RTItem("sun");
	Menu_SlideItem(rt_menu, 1);
	CHECK(!strcmp(Str("r_rt_sun"), "1"), "r_rt_sun %s", Str("r_rt_sun"));
	CHECK(!RTGreyed("sun floor"), "sun floor still greyed after sun on");

	/* 3: Right on sun floor is one step, 0.5 to 0.55 */
	RTItem("sun floor");
	Menu_SlideItem(rt_menu, 1);
	CHECK(fabsf(Cvar_VariableValue("r_rt_sunfloor") - 0.55f) < 0.001f, "r_rt_sunfloor %s", Str("r_rt_sunfloor"));

	/* AO scale is a three-choice list: full, 1/2, 1/3 */
	RTItem("ambient occlusion");
	Menu_SlideItem(rt_menu, 1);
	CHECK(!RTGreyed("AO scale"), "AO scale greyed with AO on");
	{
		menulist_s *l = (menulist_s *)RTItem("AO scale");

		CHECK(NumChoices(l) == 3 && !strcmp(l->itemnames[l->curvalue], "1/2"), "AO scale shows %s",
			l->itemnames[l->curvalue]);
		Menu_SlideItem(rt_menu, -1);
		CHECK(Cvar_VariableValue("r_rt_ao_scale") == 1.0f, "r_rt_ao_scale %s", Str("r_rt_ao_scale"));
	}

	/* 4: reset to defaults, every row's cvar back */
	RTItem("reset to defaults");
	Menu_SelectItem(rt_menu);
	for (int i = 0; i < VidRules_RTNum(); i++)
	{
		const vidrt_control_t *c = VidRules_RTControl(i);

		CHECK(!strcmp(Str(c->cvar), c->def), "%s is %s after reset, not %s", c->cvar, Str(c->cvar), c->def);
	}
	CHECK(RTGreyed("sun floor"), "sun floor live after the reset turned sun off");

	/* 5: no ray tracing on the device, every item greyed with the reason */
	OpenRT("0");
	CHECK(rt_menu->nitems == 14, "unavailable screen has %d items", rt_menu->nitems);
	for (int i = 0; i < rt_menu->nitems; i++)
	{
		const menucommon_s *c = rt_menu->items[i];

		CHECK((c->flags & QMF_GRAYED) && c->statusbar &&
			!strcmp(c->statusbar, "ray tracing is not available on this device"),
			"item %d %s: greyed %d, %s", i, c->name ? c->name : "?", (c->flags & QMF_GRAYED) != 0,
			c->statusbar ? c->statusbar : "(none)");
	}
}

/* TASK197 (P8): under Retro the Ray tracing and Tuning screens grey every row the mode reads as off,
   "set by the presentation", and write no cvar; stock and Enhanced leave the scaler's row live */
static void
test_screens_under_retro(void)
{
	static const char *const sw[] = { "shadows", "sun", "ambient occlusion", "water reflections", "gun shadow" };
	static const char *const sl[] = { "sun floor", "sun scale", "AO range", "AO minimum", "AO scale",
		"water reflect", "water ripple" };
	static const char *const tu[] = { "scaler", "sun elevation", "sun azimuth", "AO rays", "water hits" };

	SetCv("r_presentation", "2");
	SetCv("r_metalfx", "2");
	OpenRT("1");
	SetCv("r_rt_ao", "1");
	for (size_t i = 0; i < sizeof(sw) / sizeof(sw[0]); i++)
	{
		const menucommon_s *c = RTItem(sw[i]);

		CHECK(RTGreyed(sw[i]) && c->statusbar && !strcmp(c->statusbar, "set by the presentation") &&
			((menulist_s *)c)->curvalue == 0, "%s under Retro: greyed %d, %s", sw[i], RTGreyed(sw[i]),
			c->statusbar ? c->statusbar : "(none)");
	}
	for (size_t i = 0; i < sizeof(sl) / sizeof(sl[0]); i++)
	{
		CHECK(RTGreyed(sl[i]) && !strcmp(RTItem(sl[i])->statusbar, "set by the presentation"),
			"%s under Retro says %s", sl[i], RTItem(sl[i])->statusbar);
	}
	CHECK(!strcmp(Str("r_rt_ao"), "1") && !strcmp(Str("r_metalfx"), "2"), "Retro wrote a cvar: ao %s, fx %s",
		Str("r_rt_ao"), Str("r_metalfx"));

	/* the tuning screen: the scaler and the effects' rows greyed, the others live */
	RTItem("tuning...");
	Menu_SelectItem(rt_menu);
	rt_menu = menu;
	for (size_t i = 0; i < sizeof(tu) / sizeof(tu[0]); i++)
	{
		CHECK(RTGreyed(tu[i]) && !strcmp(RTItem(tu[i])->statusbar, "set by the presentation"),
			"%s on the tuning screen under Retro: greyed %d", tu[i], RTGreyed(tu[i]));
	}
	CHECK(!RTGreyed("flat light"), "flat light greyed under Retro");
	CHECK(((menulist_s *)RTItem("scaler"))->curvalue == 0, "I1: scaler row under Retro shows %d, the renderer reads 0",
		((menulist_s *)RTItem("scaler"))->curvalue);

	/* N3: reset to defaults under Retro writes no row greyed "set by the presentation" */
	SetCv("r_presentation", "2");
	OpenRT("1");
	SetCv("r_rt_ao_range", "512");
	SetCv("r_rt_sunfloor", "0.9");
	RTItem("reset to defaults");
	Menu_SelectItem(rt_menu);
	CHECK(!strcmp(Str("r_rt_ao_range"), "512") && !strcmp(Str("r_rt_sunfloor"), "0.9"),
		"N3: reset under Retro wrote a greyed row: range %s, floor %s", Str("r_rt_ao_range"), Str("r_rt_sunfloor"));

	/* N2: the main menu's MetalFX items are greyed under Retro and Apply writes no r_metalfx */
	SetCv("r_metalfx", "2");
	Open("metal", "15");
	menu = main_menu;
	CHECK(Greyed("MetalFX scaler") && Item("MetalFX scaler")->statusbar &&
		!strcmp(Item("MetalFX scaler")->statusbar, "set by the presentation") && Greyed("MetalFX scale"),
		"N2: the MetalFX items under Retro: scaler greyed %d, scale greyed %d", Greyed("MetalFX scaler"),
		Greyed("MetalFX scale"));
	{
		const int w = Writes("r_metalfx");

		Apply();
		CHECK(Writes("r_metalfx") == w && !strcmp(Str("r_metalfx"), "2"), "N2: Apply wrote r_metalfx under Retro: %s",
			Str("r_metalfx"));
	}

	/* the mode before and after: stock leaves the scaler live */
	SetCv("r_presentation", "0");
	OpenRT("1");
	RTItem("tuning...");
	Menu_SelectItem(rt_menu);
	rt_menu = menu;
	CHECK(!RTGreyed("scaler"), "scaler greyed at mode 0");
	SetCv("r_metalfx", "0");
	SetCv("r_rt_ao", "0");
}

/* TASK183: the ray-tracing screen under a presentation: Enhanced runs the
   effects, so their switches are greyed "set by the presentation" showing on
   and their tuning rows stay live; r_rt_ao is never written. Mode 0 is as before. */
static void
test_rt_screen_under_presentation(void)
{
	/* Enhanced with the user's r_rt_ao 0 */
	SetCv("r_presentation", "1");
	OpenRT("1");
	{
		const char *const sw[] = { "shadows", "sun", "ambient occlusion", "water reflections" };
		menulist_s *l = (menulist_s *)RTItem("ambient occlusion");
		int w = Writes("r_rt_ao");

		CHECK(RTGreyed("ambient occlusion") && RTItem("ambient occlusion")->statusbar &&
			!strcmp(RTItem("ambient occlusion")->statusbar, "set by the presentation"),
			"AO switch under Enhanced: greyed %d, %s", RTGreyed("ambient occlusion"),
			RTItem("ambient occlusion")->statusbar ? RTItem("ambient occlusion")->statusbar : "(none)");
		CHECK(l->curvalue == 1, "AO switch shows %d under Enhanced", l->curvalue);
		for (int i = 0; i < 4; i++)
		{
			CHECK(RTGreyed(sw[i]) && ((menulist_s *)RTItem(sw[i]))->curvalue == 1, "%s under Enhanced", sw[i]);
		}
		CHECK(!RTGreyed("gun shadow"), "gun shadow greyed under Enhanced");
		CHECK(!RTGreyed("AO range") && !RTGreyed("AO minimum") && !RTGreyed("AO scale"),
			"AO tuning greyed under Enhanced");
		CHECK(!RTGreyed("sun floor") && !RTGreyed("water reflect"), "sun or water tuning greyed under Enhanced");
		RTItem("AO range");
		Menu_SlideItem(rt_menu, 1);
		CHECK(Cvar_VariableValue("r_rt_ao_range") > 128.0f, "r_rt_ao_range %s", Str("r_rt_ao_range"));
		CHECK(Writes("r_rt_ao") == w && !strcmp(Str("r_rt_ao"), "0"), "r_rt_ao written under Enhanced: %s", Str("r_rt_ao"));
	}

	/* the same at mode 0: the switch is live and off, its tuning rows wait for it */
	SetCv("r_presentation", "0");
	OpenRT("1");
	CHECK(!RTGreyed("ambient occlusion") && ((menulist_s *)RTItem("ambient occlusion"))->curvalue == 0,
		"AO switch at mode 0 not live and off");
	CHECK(RTGreyed("AO range") && !strcmp(RTItem("AO range")->statusbar, "turn on ambient occlusion first"),
		"AO range at mode 0 says %s", RTItem("AO range")->statusbar);

	/* FIX132: reset under Enhanced keeps the user's four switches, resets every tuning row */
	SetCv("r_presentation", "1");
	OpenRT("1");
	SetCv("r_rt_ao", "1");
	SetCv("r_rt_shadows", "1");
	SetCv("r_rt_ao_range", "512");
	SetCv("r_rt_sunfloor", "0.9");
	SetCv("r_rt_water_normal", "0.5");
	RTItem("reset to defaults");
	Menu_SelectItem(rt_menu);
	CHECK(!strcmp(Str("r_rt_ao"), "1") && !strcmp(Str("r_rt_shadows"), "1"),
		"reset under Enhanced wrote a switch: ao %s, shadows %s", Str("r_rt_ao"), Str("r_rt_shadows"));
	for (int i = 0; i < VidRules_RTNum(); i++)
	{
		const vidrt_control_t *c = VidRules_RTControl(i);

		if (c->effect >= 0)
		{
			CHECK(!strcmp(Str(c->cvar), c->def), "%s is %s after reset under Enhanced", c->cvar, Str(c->cvar));
		}
	}
	CHECK(RTItem("reset to defaults")->statusbar &&
		!strcmp(RTItem("reset to defaults")->statusbar, "the presentation's switches are kept"),
		"reset status says %s", RTItem("reset to defaults")->statusbar ? RTItem("reset to defaults")->statusbar : "(none)");
	SetCv("r_presentation", "0");
}

/* TASK183: the "presentation" item: every mode shown, a mode that does not
   apply carrying its reason and never written; a mode writes no other cvar */
static void
test_presentation_item(void)
{
	static const char *const rt[] = { "r_rt_shadows", "r_rt_sun", "r_rt_ao", "r_rt_water" };
	menulist_s *l;
	int before[4];

	/* 1: Metal with ray tracing: Stock, Enhanced, and the later slices' modes with their reason */
	SetCv("r_presentation", "0");
	SetCv("r_rt_available", "1");
	Open("metal", "7");
	menu = main_menu;
	CHECK(!Greyed("presentation"), "presentation greyed under Metal");
	l = List("presentation");
	CHECK(NumChoices(l) == 4, "presentation has %d choices", NumChoices(l));
	CHECK(!strcmp(l->itemnames[0], "Stock") && !strcmp(l->itemnames[1], "Enhanced") &&
		!strcmp(l->itemnames[2], "Retro (awaiting review)") &&
		!strcmp(l->itemnames[3], "Ray traced (awaiting review)"), "presentation names %s, %s, %s, %s",
		l->itemnames[0], l->itemnames[1], l->itemnames[2], l->itemnames[3]);
	CHECK(l->curvalue == 0, "presentation shows %d at r_presentation 0", l->curvalue);

	/* 2: Enhanced, applied: r_presentation 1 and no r_rt_* written */
	for (int i = 0; i < 4; i++)
	{
		before[i] = Writes(rt[i]);
	}
	restarts = 0;
	Slide("presentation", 1);
	Apply();
	CHECK(restarts == 1, "Apply to Enhanced queued %d vid_restart", restarts);
	CHECK(!strcmp(Str("r_presentation"), "1"), "r_presentation %s after Enhanced", Str("r_presentation"));
	for (int i = 0; i < 4; i++)
	{
		CHECK(Writes(rt[i]) == before[i], "Enhanced wrote %s", rt[i]);
	}

	/* 3: the screen under Enhanced says it overrides the switches */
	SetCv("r_presentation", "1");
	OpenRT("1");
	CHECK(rt_menu->statusbar && strstr(rt_menu->statusbar, "overrides"), "the screen under Enhanced says %s",
		rt_menu->statusbar ? rt_menu->statusbar : "(none)");

	/* 4: Retro is shown, its reason on the status bar, and Apply writes nothing */
	SetCv("r_presentation", "0");
	Open("metal", "7");
	menu = main_menu;
	Slide("presentation", 1);
	Slide("presentation", 1);
	CHECK(Item("presentation")->statusbar && !strcmp(Item("presentation")->statusbar, "awaiting review"),
		"Retro's status %s", Item("presentation")->statusbar);
	Apply();
	CHECK(!strcmp(Str("r_presentation"), "0"), "Apply wrote r_presentation %s for Retro", Str("r_presentation"));

	/* 5: no ray tracing on the device: Enhanced carries its reason, and is not written */
	SetCv("r_rt_available", "0");
	Open("metal", "7");
	menu = main_menu;
	l = List("presentation");
	CHECK(!strcmp(l->itemnames[1], "Enhanced (needs ray tracing)"), "Enhanced without RT is %s", l->itemnames[1]);
	Slide("presentation", 1);
	Apply();
	CHECK(!strcmp(Str("r_presentation"), "0"), "Apply wrote r_presentation %s without RT", Str("r_presentation"));

	/* 6: off Metal the item is shown, greyed with its reason */
	Open("soft", "7");
	menu = main_menu;
	CHECK(Greyed("presentation"), "presentation live off Metal");
	CHECK(Item("presentation")->statusbar && !strcmp(Item("presentation")->statusbar, "ref_metal only"),
		"presentation off Metal says %s", Item("presentation")->statusbar);
	SetCv("r_rt_available", "1");
}

/* TASK195: the tuning screen: every row present and live or greyed with its reason, one debug view
   at a time, the A/B and preset actions queue their commands, apply (restart) only after the
   presentation changed */
static void
test_tuning_screen(void)
{
	static const char *const items[] = { "sun elevation", "sun azimuth", "AO rays", "water hits", "flat light",
		"scaler", "debug view", "presentation", "HDR", "HDR peak", "scale", "A/B: A: your settings", "B: stock", "preset name",
		"save preset", "load preset", "preset as B", "apply (restart)" };
	menuframework_s *tm;
	int before;

	for (int i = 0; i < VidRules_TuneNum(); i++)
	{
		const vidrt_control_t *c = VidRules_TuneControl(i);

		SetCv(c->cvar, (c->part == VIDTUNE_PART_DEBUG) ? "0" : c->def);
	}
	for (int v = 1; v < VidRules_NumDebugViews(); v++)
	{
		SetCv(VidRules_DebugView(v), "0");
	}
	SetCv("r_rt_ab", "");
	OpenRT("1");
	CHECK(!RTGreyed("tuning..."), "tuning greyed: %s", RTItem("tuning...")->statusbar);
	Menu_SelectItem(rt_menu);
	tm = menu;
	CHECK(tm != rt_menu && tm->keepview, "the tuning screen is not open, or dims the view");
	rt_menu = tm;

	/* 1: every item shown */
	CHECK(tm->nitems == (int)(sizeof(items) / sizeof(items[0])), "the tuning screen has %d items", tm->nitems);
	for (size_t i = 0; i < sizeof(items) / sizeof(items[0]); i++)
	{
		RTItem(items[i]);
	}

	/* 2: the greying and its reasons */
	CHECK(RTGreyed("sun elevation") && !strcmp(RTItem("sun elevation")->statusbar, "turn on sun first"),
		"sun elevation says %s", RTItem("sun elevation")->statusbar);
	CHECK(RTGreyed("AO rays") && !strcmp(RTItem("AO rays")->statusbar, "turn on ambient occlusion first"),
		"AO rays says %s", RTItem("AO rays")->statusbar);
	CHECK(RTGreyed("water hits"), "water hits live with water off");
	CHECK(!RTGreyed("scaler") && !RTGreyed("scale") && !RTGreyed("flat light") && !RTGreyed("debug view"), "a live row greyed");
	CHECK(RTGreyed("save preset") && !strcmp(RTItem("save preset")->statusbar, "the name is empty"),
		"save preset says %s", RTItem("save preset")->statusbar);
	CHECK(RTGreyed("apply (restart)") &&
		!strcmp(RTItem("apply (restart)")->statusbar, "nothing here waits for a restart"),
		"apply (restart) says %s", RTItem("apply (restart)")->statusbar);

	/* 3: a row of the pair writes its word; it goes live with its effect */
	SetCv("r_rt_sun", "1");
	tm->draw(tm);
	CHECK(!RTGreyed("sun elevation"), "sun elevation greyed with sun on");
	RTItem("sun elevation");
	Menu_SlideItem(tm, 1);
	CHECK(!strcmp(Str("r_rt_sun_angles"), "60 45"), "sun elevation wrote %s", Str("r_rt_sun_angles"));
	RTItem("sun azimuth");
	Menu_SlideItem(tm, -1);
	CHECK(!strcmp(Str("r_rt_sun_angles"), "60 40"), "sun azimuth wrote %s", Str("r_rt_sun_angles"));

	/* 4: the debug view, one at a time */
	CHECK(NumChoices((menulist_s *)RTItem("debug view")) == VidRules_NumDebugViews(), "debug views: %d",
		NumChoices((menulist_s *)RTItem("debug view")));
	Menu_SlideItem(tm, 1);
	CHECK(Cvar_VariableValue("r_rt_shadows_show") == 1, "debug view 1 is not the shadows");
	Menu_SlideItem(tm, 1);
	CHECK(Cvar_VariableValue("r_rt_shadows_show") == 0 && Cvar_VariableValue("r_rt_sun_show") == 1,
		"debug view 2: shadows %g, sun %g", Cvar_VariableValue("r_rt_shadows_show"),
		Cvar_VariableValue("r_rt_sun_show"));

	/* 5: A/B and the presets queue their commands; a bad name queues nothing */
	RTItem("A/B: A: your settings");
	Menu_SelectItem(tm);
	CHECK(!strcmp(cbuf_last, "rt_ab\n"), "A/B queued %s", cbuf_last);
	SetCv("r_rt_ab", "B: stock|r_rt_ao=0;");
	tm->draw(tm);
	RTItem("A/B: B: stock");
	snprintf(((menufield_s *)RTItem("preset name"))->buffer, 80, "%s", "x;quit");
	tm->draw(tm);
	cbuf_last[0] = '\0';
	RTItem("save preset");
	Menu_SelectItem(tm);
	CHECK(RTGreyed("save preset") && !cbuf_last[0], "a bad name queued %s", cbuf_last);
	snprintf(((menufield_s *)RTItem("preset name"))->buffer, 80, "%s", "mine");
	tm->draw(tm);
	RTItem("save preset");
	Menu_SelectItem(tm);
	CHECK(!strcmp(cbuf_last, "rt_preset save mine\n"), "save queued %s", cbuf_last);
	SetCv("r_rt_ab", "");

	/* 6: apply (restart) only after the presentation changed, and it restarts */
	RTItem("presentation");
	Menu_SlideItem(tm, 1);
	CHECK(Cvar_VariableValue("r_presentation") == 1, "presentation wrote %g", Cvar_VariableValue("r_presentation"));
	CHECK(!RTGreyed("apply (restart)"), "apply (restart) greyed after the presentation changed");
	before = restarts;
	RTItem("apply (restart)");
	Menu_SelectItem(tm);
	CHECK(restarts == before + 1, "apply (restart) queued %d", restarts - before);
	SetCv("r_presentation", "0");
	SetCv("r_rt_sun", "0");

	/* 7: off Metal (the screen drawn under a menu built for soft) every row is shown, greyed */
	Open("soft", "7");
	tm->draw(tm);
	CHECK(tm->nitems == (int)(sizeof(items) / sizeof(items[0])), "off Metal the screen has %d items", tm->nitems);
	for (int i = 0; i < VidRules_TuneNum(); i++)
	{
		const menucommon_s *c = tm->items[i];

		/* TASK196 (HD5): the HDR rows with their own reason */
		CHECK((c->flags & QMF_GRAYED) && !strcmp(c->statusbar, VidRules_HdrItem(VidRules_TuneControl(i)->cvar) >= 0 ?
				"HDR is a Metal renderer feature" : "needs the Metal renderer"),
			"%s off Metal: greyed %d, %s", c->name, (c->flags & QMF_GRAYED) != 0, c->statusbar);
	}
}

/*
 * TASK196 (HD5): the HDR items on the renderer's page, right after the colour space: always shown,
 * greyed with each reason, select and a slide do nothing on a greyed one, a change is live (written at
 * once, no apply, no restart), and r_hdr 1 greys the colour space with "set by HDR".
 */
static void
HdrOpen(const char *renderer, const char *headroom, const char *hdr, const char *pres)
{
	Reset();
	SetCv("r_hdr_headroom", headroom);
	SetCv("r_hdr", hdr);
	SetCv("r_hdr_peak", "4");
	SetCv("r_presentation", pres);
	SetCv("metal_colorspace", "0");
	Open(renderer, "15");
}

static void
HdrGreyed(const char *what, const char *reason)
{
	static const char *const names[] = { "HDR", "HDR peak" };
	for (int i = 0; i < 2; i++)
	{
		const int w_hdr = Writes("r_hdr"), w_peak = Writes("r_hdr_peak");

		CHECK(Greyed(names[i]) && Item(names[i])->statusbar && !strcmp(Item(names[i])->statusbar, reason),
			"%s: %s greyed %d, says %s", what, names[i], Greyed(names[i]),
			Item(names[i])->statusbar ? Item(names[i])->statusbar : "(no reason)");
		Cursor(names[i]);
		Menu_SelectItem(menu);
		Slide(names[i], 1);
		Slide(names[i], -1);
		CHECK(Writes("r_hdr") == w_hdr && Writes("r_hdr_peak") == w_peak,
			"%s: the greyed %s wrote a cvar", what, names[i]);
	}
	Apply();
	CHECK(Writes("r_hdr") == 0 && Writes("r_hdr_peak") == 0, "%s: apply wrote the HDR cvars", what);
}

static void
test_hdr_items(void)
{
	int before;

	/* after the colour space, on the renderer's page */
	HdrOpen("metal", "-1", "0", "0");
	CHECK(Find("HDR") == Find("colour space") + 1 && menu == sub_menu, "HDR is not after the colour space");
	CHECK(Find("HDR peak") == Find("HDR") + 1, "HDR peak is not after HDR");

	HdrOpen("soft", "16", "1", "0");
	HdrGreyed("soft", "HDR is a Metal renderer feature");
	HdrOpen("gl3", "16", "0", "0");
	HdrGreyed("gl3", "HDR is a Metal renderer feature");
	HdrOpen("metal", "-1", "0", "0");
	HdrGreyed("no reading", "the display's headroom is unavailable");
	HdrOpen("metal", "", "1", "0");
	HdrGreyed("no cvar", "the display's headroom is unavailable");
	HdrOpen("metal", "1", "1", "0");
	HdrGreyed("headroom 1", "this display reports no extended range");
	HdrOpen("metal", "16", "1", "2");
	HdrGreyed("Retro", "Retro is a 16-bit SDR look");
	CHECK(!Greyed("colour space"), "the colour space greyed under Retro, which holds HDR off");

	/* with headroom: the switch live, the peak greyed until it is on */
	HdrOpen("metal", "3.2", "0", "0");
	CHECK(!Greyed("HDR") && !strcmp(Shown("HDR"), "off"), "HDR greyed %d, shows %s", Greyed("HDR"), Shown("HDR"));
	CHECK(Greyed("HDR peak") && !strcmp(Item("HDR peak")->statusbar, "HDR is off"),
		"HDR peak says %s", Item("HDR peak")->statusbar);
	CHECK(!Greyed("colour space"), "the colour space greyed with HDR off");
	before = restarts;
	CHECK(Slide("HDR", 1), "HDR Right");
	CHECK(!strcmp(Str("r_hdr"), "1") && restarts == before, "r_hdr %s after Right, %d restarts: not live",
		Str("r_hdr"), restarts - before);
	CHECK(!Greyed("HDR peak"), "HDR peak greyed with HDR on: %s", Item("HDR peak")->statusbar);
	CHECK(strstr(Item("HDR peak")->statusbar, "3.2") != NULL, "HDR peak does not show the headroom: %s",
		Item("HDR peak")->statusbar);
	CHECK(Greyed("colour space") && !strcmp(Item("colour space")->statusbar, "set by HDR"),
		"colour space with HDR on: greyed %d, %s", Greyed("colour space"), Item("colour space")->statusbar);
	CheckStatus("HDR peak at 3.2", Item("HDR peak")->statusbar);
	HdrOpen("metal", "16", "1", "0");
	CHECK(!Greyed("HDR peak"), "HDR peak greyed at headroom 16");
	CheckStatus("HDR peak at 16", Item("HDR peak")->statusbar);
	HdrOpen("metal", "3.2", "0", "0");
	Slide("HDR", 1);
	Slide("HDR peak", 1);
	CHECK(Cvar_VariableValue("r_hdr_peak") == 4.5f, "r_hdr_peak %s after Right", Str("r_hdr_peak"));
	Apply();
	CHECK(Writes("r_hdr") == 1 && !strcmp(Str("r_hdr"), "1"), "apply wrote r_hdr again (%d writes)", Writes("r_hdr"));
	CHECK(Writes("metal_colorspace") == 0, "apply wrote the colour space while HDR sets it");

	/* opened with r_hdr 1: shows on; Left turns it off at once, and the peak greys again */
	HdrOpen("metal", "3.2", "1", "0");
	CHECK(!strcmp(Shown("HDR"), "on"), "HDR shows %s with r_hdr 1", Shown("HDR"));
	CHECK(Slide("HDR", -1) && !strcmp(Str("r_hdr"), "0"), "r_hdr %s after Left", Str("r_hdr"));
	CHECK(Greyed("HDR peak") && !strcmp(Item("HDR peak")->statusbar, "HDR is off"),
		"HDR peak after Left says %s", Item("HDR peak")->statusbar);

	SetCv("r_hdr", "0");
	SetCv("r_hdr_headroom", "-1");
	SetCv("r_presentation", "0");
}

/* TASK196 (HD5): a colour space choice not yet applied survives re-opening the page and an HDR toggle */
static void
test_colorspace_choice_is_pending(void)
{
	HdrOpen("metal", "3.2", "0", "0");
	CHECK(Slide("colour space", 1) && !strcmp(Shown("colour space"), "sRGB"), "Right");

	/* close and re-open the renderer options page */
	menu = main_menu;
	Cursor("renderer options...");
	Menu_SelectItem(menu);
	menu = sub_menu;
	CHECK(!strcmp(Shown("colour space"), "sRGB"), "re-opened page lost the choice: %s", Shown("colour space"));

	/* HDR on greys it, HDR off enables it again: the choice is back */
	Slide("HDR", 1);
	CHECK(Greyed("colour space"), "colour space not greyed with HDR on");
	Slide("HDR", -1);
	CHECK(!Greyed("colour space") && !strcmp(Shown("colour space"), "sRGB"),
		"HDR toggle lost the choice: %s", Shown("colour space"));
	Apply();
	CHECK(!strcmp(Str("metal_colorspace"), "1"), "apply wrote metal_colorspace %s", Str("metal_colorspace"));

	SetCv("metal_colorspace", "0");
	SetCv("r_hdr", "0");
}

/* TASK196 (HD5): the tuning screen's HDR rows, greyed with the display's reason, live with headroom */
static void
test_hdr_tuning_rows(void)
{
	menuframework_s *tm;

	SetCv("r_hdr", "0");
	SetCv("r_hdr_peak", "4");
	SetCv("r_presentation", "0");
	SetCv("r_rt_ab", "");
	SetCv("r_hdr_headroom", "1");
	OpenRT("1");
	RTItem("tuning...");
	Menu_SelectItem(rt_menu);
	tm = rt_menu = menu;
	CHECK(RTGreyed("HDR") && !strcmp(RTItem("HDR")->statusbar, "this display reports no extended range"),
		"tuning HDR says %s", RTItem("HDR")->statusbar);
	CHECK(RTGreyed("HDR peak") && !strcmp(RTItem("HDR peak")->statusbar, "this display reports no extended range"),
		"tuning HDR peak says %s", RTItem("HDR peak")->statusbar);
	RTItem("HDR");
	Menu_SlideItem(tm, 1);
	CHECK(!strcmp(Str("r_hdr"), "0"), "the greyed tuning HDR wrote r_hdr %s", Str("r_hdr"));

	SetCv("r_hdr_headroom", "4");
	tm->draw(tm);
	CHECK(!RTGreyed("HDR"), "tuning HDR greyed with headroom: %s", RTItem("HDR")->statusbar);
	CHECK(RTGreyed("HDR peak") && !strcmp(RTItem("HDR peak")->statusbar, "HDR is off"),
		"tuning HDR peak says %s", RTItem("HDR peak")->statusbar);
	RTItem("HDR");
	Menu_SlideItem(tm, 1);
	CHECK(!strcmp(Str("r_hdr"), "1"), "tuning HDR wrote r_hdr %s", Str("r_hdr"));
	CHECK(!RTGreyed("HDR peak"), "tuning HDR peak greyed with HDR on: %s", RTItem("HDR peak")->statusbar);
	CheckStatus("tuning HDR peak at 4", RTItem("HDR peak")->statusbar);
	SetCv("r_hdr_headroom", "16");
	tm->draw(tm);
	CheckStatus("tuning HDR peak at 16", RTItem("HDR peak")->statusbar);
	SetCv("r_hdr_headroom", "4");
	tm->draw(tm);
	RTItem("HDR peak");
	Menu_SlideItem(tm, -1);
	CHECK(Cvar_VariableValue("r_hdr_peak") == 3.5f, "tuning HDR peak wrote %s", Str("r_hdr_peak"));

	SetCv("r_presentation", "2");
	tm->draw(tm);
	CHECK(RTGreyed("HDR") && !strcmp(RTItem("HDR")->statusbar, "Retro is a 16-bit SDR look"),
		"tuning HDR under Retro says %s", RTItem("HDR")->statusbar);

	SetCv("r_hdr", "0");
	SetCv("r_hdr_peak", "4");
	SetCv("r_hdr_headroom", "-1");
	SetCv("r_presentation", "0");
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
	test_ray_tracing_screen();
	test_presentation_item();
	test_rt_screen_under_presentation();
	test_screens_under_retro();
	test_tuning_screen();
	test_hdr_items();
	test_colorspace_choice_is_pending();
	test_hdr_tuning_rows();

	printf("videomenu_test: %d checks, %d failed\n", checks, failed);

	return failed ? 1 : 0;
}
