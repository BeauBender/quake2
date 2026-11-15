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
float SCR_GetMenuScale(void) { return 1; }
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

static int
Find(const char *name)
{
	for (int i = 0; i < menu->nitems; i++)
	{
		const menucommon_s *c = menu->items[i];

		if (c->name && !strcmp(c->name, name))
		{
			return i;
		}
	}

	printf("FAIL: no menu item named \"%s\"\n", name);
	failed++;
	exit(1);
}

static menulist_s *
List(const char *name)
{
	return (menulist_s *)menu->items[Find(name)];
}

static int
Greyed(const char *name)
{
	return (((menucommon_s *)menu->items[Find(name)])->flags & QMF_GRAYED) != 0;
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
	menu->cursor = Find(name);

	return Menu_SlideItem(menu, dir);
}

static void
Apply(void)
{
	menu->cursor = Find("apply");
	Menu_SelectItem(menu);
}

/* Opens the menu with this renderer and Metal mode mask. */
static void
Open(const char *renderer, const char *modes)
{
	SetCv("vid_renderer", renderer);
	SetCv("r_metalfx_modes", modes);
	menu = NULL;
	M_Menu_Video_f();
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

static int
HasRenderer(const char *want)
{
	Open("gl1", "0");

	return PickRenderer(want);
}

static void
Reset(void)
{
	SetCv("r_metalfx", "0");
	SetCv("r_metalfx_scale", "0.5");
	SetCv("r_metalfx_interp", "0");
	SetCv("gl3_colorlight", "1");
	SetCv("gl4_colorlight", "1");
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

/* Leaving Metal with frame generation set: it is written under Metal's
   name, the one it was shown under, so the choice is kept for when the
   player comes back. */
static void
test_leaving_metal_keeps_frame_generation(void)
{
	Reset();
	SetCv("r_metalfx_interp", "1");
	Open("metal", "31");

	if (!PickRenderer("OpenGL 1.4"))
	{
		printf("skip: no Metal in the renderer list\n");
		return;
	}

	CHECK(!Greyed("frame generation"), "greyed with the interpolator present");
	CHECK(!strcmp(Shown("frame generation"), "on"), "shows %s", Shown("frame generation"));
	CHECK(Slide("frame generation", -1), "frame generation Left");
	Apply();

	CHECK(!strcmp(Str("vid_renderer"), "gl1"), "vid_renderer %s", Str("vid_renderer"));
	CHECK(!strcmp(Str("r_metalfx_interp"), "0"), "r_metalfx_interp %s, dropped", Str("r_metalfx_interp"));
}

/* Enter, Left and Right on a greyed item change nothing and run nothing. */
static void
test_greyed_items_cannot_be_changed(void)
{
	static const char *const names[] = {
		"color intensity", "min. light level", "overbrights", "color light", "bloom",
		"dynamic light", "MetalFX scaler", "MetalFX scale", "frame generation", "denoiser"
	};
	static const char *const renderers[] = { "soft", "gl3", "gl1", "metal" };

	for (size_t r = 0; r < sizeof(renderers) / sizeof(renderers[0]); r++)
	{
		Reset();
		Open(renderers[r], "15");
		menu->statusbar = NULL;

		for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
		{
			menucommon_s *c = menu->items[Find(names[i])];
			int before = 0;

			if (!(c->flags & QMF_GRAYED))
			{
				continue;
			}

			if (c->type == MTYPE_SPINCONTROL)
			{
				before = ((menulist_s *)c)->curvalue;
			}

			menu->cursor = Find(names[i]);
			CHECK(!Menu_SlideItem(menu, 1) && !Menu_SlideItem(menu, -1),
				"%s: %s slid while greyed", renderers[r], names[i]);
			CHECK(!Menu_SelectItem(menu), "%s: %s selected while greyed", renderers[r], names[i]);
			CHECK(menu->statusbar == NULL, "%s: %s ran its callback while greyed (status \"%s\")",
				renderers[r], names[i], menu->statusbar);

			if (c->type == MTYPE_SPINCONTROL)
			{
				CHECK(((menulist_s *)c)->curvalue == before, "%s: %s changed", renderers[r], names[i]);
			}
		}
	}
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

int
main(void)
{
	vid_fullscreen = Cvar_Get("vid_fullscreen", "0", 0);
	vid_renderer = Cvar_Get("vid_renderer", "gl3", 0);
	Cvar_Get("r_mode", "4", 0);

	test_scale_item_follows_the_scaler();
	test_apply_uses_the_built_renderer();
	if (HasRenderer("Metal"))
	{
		test_leaving_metal_keeps_frame_generation();
	}
	else
	{
		printf("skip: no Metal in the renderer list\n");
	}
	test_greyed_items_cannot_be_changed();
	test_custom_scale_is_not_a_choice();

	printf("videomenu_test: %d checks, %d failed\n", checks, failed);

	return failed ? 1 : 0;
}
