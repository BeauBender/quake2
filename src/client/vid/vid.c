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
 * API between the client and renderers.
 *
 * =======================================================================
 */

#include <assert.h>
#include <errno.h>

#include "../../client/header/client.h"
#include "../../client/header/keyboard.h"
#include "header/refrecord.h"

// --------

// Screenshots
// -----------

/*
 * Writes an uncompressed 24-bit TGA. data holds width*height pixels of
 * comp bytes (3 or 4, RGB or RGBA), row-wise, starting at the top left.
 * Returns true on success.
 */
static qboolean
VID_WriteTGA(const char *path, int width, int height, int comp, const unsigned char *data)
{
	unsigned char hdr[18];
	unsigned char *row;
	FILE *f;
	int x, y;
	qboolean ok = true;

	if ((comp != 3 && comp != 4) || width < 1 || height < 1 || width > 65535 || height > 65535)
	{
		return false;
	}

	row = malloc((size_t)width * 3);

	if (!row)
	{
		return false;
	}

	f = Q_fopen(path, "wb");

	if (!f)
	{
		free(row);
		return false;
	}

	memset(hdr, 0, sizeof(hdr));
	hdr[2] = 2;                 /* uncompressed truecolor */
	hdr[12] = width & 255;
	hdr[13] = (width >> 8) & 255;
	hdr[14] = height & 255;
	hdr[15] = (height >> 8) & 255;
	hdr[16] = 24;
	hdr[17] = 0x20;             /* rows stored top-down, as the renderer hands them over */

	if (fwrite(hdr, 1, sizeof(hdr), f) != sizeof(hdr))
	{
		ok = false;
	}

	for (y = 0; ok && y < height; y++)
	{
		const unsigned char *src = data + (size_t)y * width * comp;

		for (x = 0; x < width; x++)
		{
			row[x * 3 + 0] = src[x * comp + 2];
			row[x * 3 + 1] = src[x * comp + 1];
			row[x * 3 + 2] = src[x * comp + 0];
		}

		if (fwrite(row, 1, (size_t)width * 3, f) != (size_t)width * 3)
		{
			ok = false;
		}
	}

	free(row);

	if (fclose(f) != 0)
	{
		ok = false;
	}

	/*
	 * The file was opened (and so created or truncated) by this function:
	 * do not leave a truncated TGA behind. Failures before the open
	 * above remove nothing, as the path may be somebody else's file.
	 */
	if (!ok)
	{
		remove(path);
	}

	return ok;
}

/* Appends one line to <dir>/manifest.txt; silent when it cannot. */
static void
VID_PinManifest(const char *dir, const char *fmt, ...)
{
	char path[MAX_OSPATH];
	va_list ap;
	FILE *f;
	int len;

	len = snprintf(path, sizeof(path), "%s/manifest.txt", dir);
	if ((len < 0) || (len >= (int)sizeof(path)))
	{
		return;
	}

	f = Q_fopen(path, "ab");
	if (f)
	{
		va_start(ap, fmt);
		vfprintf(f, fmt, ap);
		va_end(ap);
		fclose(f);
	}
}

/*
 * Pin harness (INFORM8/INFORM9): when a pin store directory was given on
 * the command line (CL_PinsDir, latched by CL_PinsLatch; never a cvar), a
 * screenshot is written as an uncompressed 24-bit TGA straight into the
 * store, named by scenario and frame index, and scrnshot/ is never
 * touched. Uses the same writer as the screenshot command. Returns true
 * when it handled the frame.
 */
static qboolean
VID_WritePinFrame(int width, int height, int comp, const unsigned char *data)
{
	static int frame;
	const char *dir = CL_PinsDir();
	const char *scenario = CL_PinsScenario();
	char path[MAX_OSPATH];
	int len, idx;

	if (CL_PinsInvalid())
	{
		Com_Printf("pins: a pins_* argument was refused, nothing written\n");
		return true;
	}

	if (!dir[0])
	{
		return false;
	}

	if ((comp != 3) && (comp != 4))
	{
		Com_Printf("pins: unsupported pixel format (%d bytes), nothing written\n", comp);
		return true;
	}

	/* Belt and braces: CL_PinsLatch refused these already. */
	if (strstr(dir, "..") || strpbrk(scenario, "/\\:") || strstr(scenario, ".."))
	{
		Com_Printf("pins: pins_dir or pins_scenario holds a forbidden character, nothing written\n");
		return true;
	}

	/*
	 * The frame index always advances, so a failed write never shifts
	 * the names of the frames after it. A failure is recorded in
	 * manifest.txt as "FAILED <name>", which scripts/pins.sh record
	 * treats as an incomplete pin. If the manifest cannot be written
	 * either, its line count no longer matches the frames and record
	 * fails on that.
	 */
	idx = frame++;

	/* the trace marks where in the frame this pin frame was taken */
	if (RefRec_Recording())
	{
		const int32_t n = idx;

		RefRec_Draw(REC_CAPTURE, &n, 1, NULL, 0, NULL, NULL, 0);
	}

	/* Never open a truncated path. */
	len = snprintf(path, sizeof(path), "%s/%s-%06d.tga", dir, scenario, idx);
	if ((len < 0) || (len >= (int)sizeof(path)))
	{
		Com_Printf("pins: the frame path does not fit %d bytes, nothing written\n", (int)sizeof(path));
		VID_PinManifest(dir, "FAILED %s-%06d.tga\n", scenario, idx);
		return true;
	}

	if (!VID_WriteTGA(path, width, height, comp, data))
	{
		/* VID_WriteTGA removed what it created; nothing else is touched here. */
		Com_Printf("pins: cannot write %s\n", path);
		VID_PinManifest(dir, "FAILED %s-%06d.tga\n", scenario, idx);
		return true;   /* handled: scrnshot/ stays untouched while pins are active */
	}

	VID_PinManifest(dir, "%s-%06d.tga %s %dx%d\n", scenario, idx,
		vid_renderer ? vid_renderer->string : "?", width, height);
	return true;
}

/*
 * Writes a screenshot as an uncompressed TGA. This function is called
 * with raw image data of width*height pixels, each pixel has comp bytes.
 * Must be 3 or 4, for RGB or RGBA. The pixels must be given row-wise,
 * starting at the top left.
 */
void VID_WriteScreenshot(int width, int height, int comp, const void* data)
{
	char picname[80];
	char checkname[MAX_OSPATH];
	int i;
	int argc = Cmd_Argc();
	const char* gameDir = FS_Gamedir();

	if (VID_WritePinFrame(width, height, comp, (const unsigned char *)data))
	{
		return;
	}

	// FS_InitFilesystem() made sure the screenshots dir exists.

	if (argc > 1 && (argc > 2 || Q_stricmp(Cmd_Argv(1), "tga") != 0))
	{
		Com_Printf("screenshot only writes TGA files; the (optional) argument must be \"tga\"\n");
		return;
	}

	/* find a file name to save it to */
	for (i = 0; i <= 9999; i++)
	{
		FILE *f;
		Com_sprintf(checkname, sizeof(checkname), "%s/scrnshot/q2_%04d.tga", gameDir, i);
		f = Q_fopen(checkname, "rb");

		if (!f)
		{
			Com_sprintf(picname, sizeof(picname), "q2_%04d.tga", i);
			/* Also lands here for an existing file that cannot be opened, so the writer never removes a file it did not open itself. */
			break; /* file doesn't exist */
		}

		fclose(f);
	}

	if (i == 10000)
	{
		Com_Printf("SCR_ScreenShot_f: Couldn't create a file\n");
		return;
	}

	if (VID_WriteTGA(checkname, width, height, comp, (const unsigned char *)data))
	{
		Com_Printf("Wrote %s\n", picname);
	}
	else
	{
		Com_Printf("SCR_ScreenShot_f: Couldn't write %s\n", picname);
	}
}


// Video mode array
// ----------------

typedef struct vidmode_s
{
	const char *description;
	int width, height;
	int mode;
} vidmode_t;

// This must be the same as VID_MenuInit()->resolutions[] in videomenu.c!
vidmode_t vid_modes[] = {
	{"Mode  0:  320x240", 320, 240, 0},
	{"Mode  1:  400x300", 400, 300, 1},
	{"Mode  2:  512x384", 512, 384, 2},
	{"Mode  3:  640x400", 640, 400, 3},
	{"Mode  4:  640x480", 640, 480, 4},
	{"Mode  5:  800x500", 800, 500, 5},
	{"Mode  6:  800x600", 800, 600, 6},
	{"Mode  7:  960x720", 960, 720, 7},
	{"Mode  8: 1024x480", 1024, 480, 8},
	{"Mode  9: 1024x640", 1024, 640, 9},
	{"Mode 10: 1024x768", 1024, 768, 10},
	{"Mode 11: 1152x768", 1152, 768, 11},
	{"Mode 12: 1152x864", 1152, 864, 12},
	{"Mode 13: 1280x800", 1280, 800, 13},
	{"Mode 14: 1280x720", 1280, 720, 14},
	{"Mode 15: 1280x960", 1280, 960, 15},
	{"Mode 16: 1280x1024", 1280, 1024, 16},
	{"Mode 17: 1366x768", 1366, 768, 17},
	{"Mode 18: 1440x900", 1440, 900, 18},
	{"Mode 19: 1600x1200", 1600, 1200, 19},
	{"Mode 20: 1680x1050", 1680, 1050, 20},
	{"Mode 21: 1920x1080", 1920, 1080, 21},
	{"Mode 22: 1920x1200", 1920, 1200, 22},
	{"Mode 23: 2048x1536", 2048, 1536, 23},
	{"Mode 24: 2560x1080", 2560, 1080, 24},
	{"Mode 25: 2560x1440", 2560, 1440, 25},
	{"Mode 26: 2560x1600", 2560, 1600, 26},
	{"Mode 27: 3440x1440", 3440, 1440, 27},
	{"Mode 28: 3840x1600", 3840, 1600, 28},
	{"Mode 29: 3840x2160", 3840, 2160, 29},
	{"Mode 30: 4096x2160", 4096, 2160, 30},
	{"Mode 31: 5120x2880", 5120, 2880, 31},
	{"Mode 32: 1600x900", 1600, 900, 32},
};

#define VID_NUM_MODES ARRLEN(vid_modes)

/*
 * Callback function for the 'vid_listmodes' cmd.
 */
static void
VID_ListModes_f(void)
{
	int i;

	Com_Printf("Supported video modes (r_mode):\n");

	for (i = 0; i < VID_NUM_MODES; ++i)
	{
		Com_Printf("  %s\n", vid_modes[i].description);
	}
	Com_Printf("  Mode -1: r_customwidth x r_customheight\n");
}

/*
 * Returns informations about the given mode.
 */
static qboolean
VID_GetModeInfo(int *width, int *height, int mode)
{
	if ((mode < 0) || (mode >= VID_NUM_MODES))
	{
		return false;
	}

	*width = vid_modes[mode].width;
	*height = vid_modes[mode].height;

	return true;
}

// --------

// Renderer load, restart and shutdown
// -----------------------------------

// Global console variables.
cvar_t *vid_gamma;
cvar_t *vid_fullscreen;
cvar_t *vid_renderer;

// Global video state, used throughout the client.
viddef_t viddef;

// Struct with the pointers exported by the renderer.
refexport_t	re;

// Handle / pointer the the loaded renderer DLL.
void *reflib_handle = NULL;

// Is a renderer loaded and active?
qboolean ref_active = false;

// Renderer restart type requested.
ref_restart_t restart_state = RESTART_UNDEF;

// Renderer lib extension.
#ifdef __APPLE__
const char* lib_ext = "dylib";
#elif defined(_WIN32)
const char* lib_ext = "dll";
#else
const char* lib_ext = "so";
#endif

/*
 * Returns platform specific path to a renderer lib.
 */
static void
VID_GetRendererLibPath(const char *renderer, char *path, size_t len)
{
	snprintf(path, len, "%sref_%s.%s", Sys_GetBinaryDir(), renderer, lib_ext);
}

/*
 * Checks if a renderer DLL is available.
 */
qboolean
VID_HasRenderer(const char *renderer)
{
	char reflib_path[MAX_OSPATH] = {0};
	VID_GetRendererLibPath(renderer, reflib_path, sizeof(reflib_path));

	if (Sys_IsFile(reflib_path))
	{
		return true;
	}

	return false;
}

/*
 * Called by the renderer to request a restart.
 */
static void
VID_RequestRestart(ref_restart_t rs)
{
	restart_state = rs;
}

/*
 * Restarts the renderer.
 */
static void
VID_Restart_f(void)
{
	if (restart_state == RESTART_UNDEF)
	{
		vid_fullscreen->modified = true;
	} else {
		restart_state = RESTART_FULL;
	}
}

/*
 * Renderer-API recorder (docs/plans/refapi-record-replay.md). With
 * "+set pins_record <name>" on the command line (CL_PinsRecord, latched
 * by CL_PinsLatch with the pin directory; never a cvar) the first
 * renderer session of the run is recorded to <pins_dir>/<name>, beside
 * the pin frames. A later session (a renderer restart) is not recorded,
 * so the trace is never overwritten. Without pins_record nothing is
 * opened.
 */
static void
VID_StartRecord(void)
{
	static qboolean started;
	const char *dir = CL_PinsDir();
	const char *name = CL_PinsRecord();
	char path[MAX_OSPATH];
	int len;

	if (!name[0] || !dir[0] || CL_PinsInvalid())
	{
		return;
	}

	if (started)
	{
		Com_Printf("pins: only the first renderer session of a run is recorded\n");
		return;
	}

	started = true;

	/* Belt and braces: CL_PinsLatch refused these already. */
	if (strstr(dir, "..") || strstr(name, "..") || strpbrk(name, "/\\:"))
	{
		Com_Printf("pins: pins_dir or pins_record holds a forbidden character, not recording\n");
		return;
	}

	/* Never open a truncated path. */
	len = snprintf(path, sizeof(path), "%s/%s", dir, name);
	if ((len < 0) || (len >= (int)sizeof(path)))
	{
		Com_Printf("pins: the trace path does not fit %d bytes, not recording\n", (int)sizeof(path));
		return;
	}

	RefRec_SetPrint(Com_Printf);
	RefRec_StartRecord(path, viddef.width, viddef.height, CL_PinsCapture());
}

/*
 * The REC_CVARS snapshot at session open: every cvar with its flags;
 * RefRec_Cvars keeps those RefRec_CvarRecorded accepts (CVAR_ARCHIVE or
 * a renderer prefix, refrecord.h).
 */
static void
VID_RecordCvars(void)
{
	const char **names, **values;
	cvar_t *var;
	int *flags;
	int n = 0;

	for (var = cvar_vars; var; var = var->next)
	{
		n++;
	}

	names = malloc(n * sizeof(*names) + 1);
	values = malloc(n * sizeof(*values) + 1);
	flags = malloc(n * sizeof(*flags) + 1);
	n = 0;

	/* out of memory: an empty snapshot, the replay restores nothing */
	for (var = cvar_vars; var && names && values && flags; var = var->next)
	{
		names[n] = var->name;
		values[n] = var->string;
		flags[n] = var->flags;
		n++;
	}

	RefRec_Cvars(n, names, values, flags);
	free(names);
	free(values);
	free(flags);
}

/*
 * Replay target (pins_replay, CL_PinsReplay): the replayer hands every
 * record that passed its validation layer (Rep_Check), its values
 * bounded and its ids resolved, to the same wrappers
 * the client calls. Nothing is recorded while replaying (CL_PinsLatch
 * refuses pins_record beside pins_replay).
 */
static void *
VID_ReplayRegister(refrec_type_t type, const char *name)
{
	if (type == REC_MODEL)
	{
		return R_RegisterModel(name);
	}

	return (type == REC_SKIN) ? R_RegisterSkin(name) : Draw_FindPic(name);
}

static void
VID_ReplayCall(refrec_call_t *c)
{
	char screenshot[] = "screenshot tga";
	const int32_t *n = c->ints;
	const float *f = c->floats;
	vec3_t axis;

	switch (c->type)
	{
		case REC_BEGINREG: R_BeginRegistration(c->name); break;
		case REC_ENDREG: R_EndRegistration(); break;
		case REC_BEGINFRAME: R_BeginFrame(f[0]); break;
		case REC_ENDWORLD: R_EndWorldRenderpass(); break;
		case REC_ENDFRAME: R_EndFrame(); break;
		case REC_PICSIZE: Draw_GetPicSize(&c->ints[0], &c->ints[1], c->name); break;
		case REC_PICSCALED: Draw_PicScaled(n[0], n[1], c->name, f[0]); break;
		case REC_PICSCALEDCOL: Draw_PicScaledCol(n[0], n[1], c->name, f[0], &f[1]); break;
		case REC_STRETCHPIC: Draw_StretchPic(n[0], n[1], n[2], n[3], c->name); break;
		case REC_CHARSCALED: Draw_CharScaled(n[0], n[1], n[2], f[0]); break;
		case REC_TILECLEAR: Draw_TileClear(n[0], n[1], n[2], n[3], c->name); break;
		case REC_FILL: Draw_Fill(n[0], n[1], n[2], n[3], n[4]); break;
		case REC_FADESCREEN: Draw_FadeScreen(); break;
		case REC_STRETCHRAW: Draw_StretchRaw(n[0], n[1], n[2], n[3], n[4], n[5], c->data, n[6]); break;
		case REC_PALETTE: R_SetPalette(c->datalen ? c->data : NULL); break;

		case REC_SKY:
			VectorSet(axis, f[1], f[2], f[3]);
			R_SetSky(c->name, f[0], axis);
			break;

		case REC_CAPTURE:
			/* where the live run captured, before R_EndFrame (SCR_PinsCapture) */
			Cmd_ExecuteString(screenshot);
			break;

		default:
			break;
	}
}

/*
 * REC_CVARS restore: the replayer asks for the live cvar's flags and sets
 * only what RefRec_CvarRestorable allows for them; a name without a live
 * cvar is never created. A replay is a pin run, so CL_WriteConfiguration
 * never writes the restored values to config.cfg.
 */
static int
VID_ReplayCvarFlags(const char *name)
{
	cvar_t *var;

	for (var = cvar_vars; var; var = var->next)
	{
		if (strcmp(var->name, name) == 0)
		{
			return var->flags;
		}
	}

	return -1;
}

static void
VID_ReplaySetCvar(const char *name, const char *value)
{
	Cvar_Set(name, value);
}

/* Opens the trace for the loaded renderer at its size; false when refused. */
qboolean
VID_OpenReplay(const char *path)
{
	refrec_target_t t;
	refrec_header_t h;

	if (!ref_active || RefRec_Recording())
	{
		return false;
	}

	memset(&t, 0, sizeof(t));
	t.api_version = API_VERSION;
	t.width = viddef.width;
	t.height = viddef.height;
	t.Register = VID_ReplayRegister;
	t.RenderFrame = R_RenderFrame;
	t.Call = VID_ReplayCall;
	t.CvarFlags = VID_ReplayCvarFlags;
	t.SetCvar = VID_ReplaySetCvar;
	RefRec_SetPrint(Com_Printf);
	RefRec_SetReplayTarget(&t);

	if (!RefRec_OpenReplay(path, &h))
	{
		return false;
	}

	Com_Printf("pins: replaying %u frames of %ux%u, captured every %u live\n",
			h.frame_count, h.width, h.height, h.capture_every);
	return true;
}

/* One frame of the replay: 1 replayed, 0 after the last frame, -1 refused. */
int
VID_ReplayFrame(void)
{
	refrec_replay_stats_t st;

	if (RefRec_ReplayNextFrame())
	{
		return 1;
	}

	RefRec_ReplayStats(&st);
	return st.failed ? -1 : 0;
}

void
VID_CloseReplay(void)
{
	refrec_replay_stats_t st;

	RefRec_ReplayStats(&st);
	RefRec_CloseReplay();
	Com_Printf("pins: replayed %u frames, %u captures, %u values bounded, %u draws outside the frame skipped, "
			"%u warnings, %u pic size mismatches, %u cvars restored, %u not\n",
			st.frames, st.captures, st.clamped, st.dropped, st.warns, st.picsize_mismatch,
			st.cvars_set, st.cvars_skipped);
}

/*
 * Shuts the renderer down and unloads it.
 */
static void
VID_ShutdownRenderer(void)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_StopRecord();
		}

		/* Shut down the renderer */
		re.Shutdown();
		GLimp_ShutdownGraphics();
		Sys_FreeLibrary(reflib_handle);
		reflib_handle = NULL;
		memset(&re, 0, sizeof(re));

		CL_ClearTEntModels();
	}

	// Declare the refresher as inactive
	ref_active = false;
}

/*
 * Loads and initializes a renderer.
 */
static qboolean
VID_LoadRenderer(void)
{
	refimport_t	ri;
	GetRefAPI_t	GetRefAPI;

	char reflib_name[64] = {0};
	char reflib_path[MAX_OSPATH] = {0};

	// If the refresher is already active we need
	// to shut it down before loading a new one
	VID_ShutdownRenderer();

	// Log what we're doing.
	Com_Printf("----- refresher initialization -----\n");

	snprintf(reflib_name, sizeof(reflib_name), "ref_%s.%s", vid_renderer->string, lib_ext);
	VID_GetRendererLibPath(vid_renderer->string, reflib_path, sizeof(reflib_path));
	Com_Printf("Loading library: %s\n", reflib_name);

	// Check if the renderer libs exists.
	if (!VID_HasRenderer(vid_renderer->string))
	{
        Com_Printf("Library %s cannot be found!\n", reflib_name);

		return false;
	}

	// Mkay, let's load the requested renderer.
	GetRefAPI = (GetRefAPI_t)Sys_LoadLibrary(reflib_path, "GetRefAPI", &reflib_handle);

	// Okay, we couldn't load it. It's up to the
	// caller to recover from this.
	if (GetRefAPI == NULL)
	{
		Com_Printf("Loading %s as renderer lib failed!\n", reflib_name);

		return false;
	}

	// Fill in the struct exported to the renderer.
	// FIXME: Do we really need all these?
	ri.Cmd_AddCommand = Cmd_AddCommand;
	ri.Cmd_Argc = Cmd_Argc;
	ri.Cmd_Argv = Cmd_Argv;
	ri.Cmd_ExecuteText = Cbuf_ExecuteText;
	ri.Cmd_RemoveCommand = Cmd_RemoveCommand;
	ri.Com_VPrintf = Com_VPrintf;
	ri.Cvar_Get = Cvar_Get;
	ri.Cvar_Set = Cvar_Set;
	ri.Cvar_SetValue = Cvar_SetValue;
	ri.FS_FreeFile = FS_FreeFile;
	ri.FS_Gamedir = FS_Gamedir;
	ri.FS_LoadFile = FS_LoadFile;
	ri.GLimp_InitGraphics = GLimp_InitGraphics;
	ri.GLimp_GetDesktopMode = GLimp_GetDesktopMode;
	ri.Sys_Error = Com_Error;
	ri.Vid_GetModeInfo = VID_GetModeInfo;
	ri.Vid_MenuInit = VID_MenuInit;
	ri.Vid_WriteScreenshot = VID_WriteScreenshot;
	ri.Vid_RequestRestart = VID_RequestRestart;

	// Exchange our export struct with the renderers import struct.
	re = GetRefAPI(ri);

	// Declare the refresher as active.
	ref_active = true;

	// Let's check if we've got a compatible renderer.
	if (re.api_version != API_VERSION)
	{
		Com_Printf("%s has incompatible api_version %d!\n", reflib_name, re.api_version);

		VID_ShutdownRenderer();

		return false;
	}
	else if (re.framework_version != GLimp_GetFrameworkVersion())
	{
		Com_Printf("%s has incompatible sdl_version %d!\n", reflib_name, re.framework_version);

		VID_ShutdownRenderer();

		return false;
	}

	// Everything seems okay, initialize it.
	if (!re.Init())
	{
		VID_ShutdownRenderer();

		Com_Printf("ERROR: Loading %s as rendering backend failed.\n", reflib_name);
		Com_Printf("------------------------------------\n\n");

		return false;
	}

	/* session open: pins_record starts the recorder, unguarded, since
	   recording is off until it has run */
	VID_StartRecord();

	/* the renderer cvars a replay restores */
	if (RefRec_Recording())
	{
		VID_RecordCvars();
	}

	/* Ensure that all key states are cleared */
	Key_MarkAllUp();

	Com_Printf("Successfully loaded %s as rendering backend.\n", reflib_name);
	Com_Printf("------------------------------------\n\n");

	return true;
}

/*
 * Checks if a renderer changes was requested and executes it.
 * Inclusive fallback through all renderers. :)
 */
void
VID_CheckChanges(void)
{
	static const char *r_order[] = {
		"gl3",
		"gl1",
		"soft"
	};
	static const int r_order_len = ARRLEN(r_order);

	// Hack around renderers that still abuse vid_fullscreen
	// to communicate restart requests to the client.
	ref_restart_t rs;

	if (restart_state == RESTART_UNDEF)
	{
		if (vid_fullscreen->modified)
		{
			rs = RESTART_FULL;
			vid_fullscreen->modified = false;
		} else {
			rs = RESTART_NO;
		}
	} else {
		rs = restart_state;
		restart_state = RESTART_NO;
	}

	if (rs == RESTART_FULL)
	{
		// Stop sound, because the clients blocks while
		// we're reloading the renderer. The sound system
		// would screw up its internal timings.
		S_StopAllSounds();

		// Reset the client side of the renderer state.
		cl.refresh_prepped = false;
		cl.cinematicpalette_active = false;

		// More or less blocks the client.
		cls.disable_screen = true;

		// Mkay, let's try our luck.
		while (!VID_LoadRenderer())
		{
			int next_test = 0;

			/* Pin harness: frames filed under one renderer's name must
			   come from that renderer, so never fall back while capturing. */
			if (CL_PinsRun())
			{
				Com_Error(ERR_FATAL, "pins: renderer %s failed to load, no fallback while capturing\n",
						vid_renderer->string);
				return;
			}

			for (int i = 0; i < r_order_len; i++)
			{
				if (strcmp(vid_renderer->string, r_order[i]) == 0)
				{
					next_test = i + 1;
					break;
				}
			}

			if (next_test == r_order_len)
			{
				Com_Error(ERR_FATAL, "No usable renderer found!\n");
				return;
			}

			Com_Printf("Retrying with %s...\n", r_order[next_test]);
			Cvar_Set("vid_renderer", r_order[next_test]);
		}

		// Unblock the client.
		cls.disable_screen = false;
	}

	if (rs == RESTART_PARTIAL)
	{
		cl.refresh_prepped = false;
	}
}

/*
 * Initializes the video stuff.
 */
void
VID_Init(void)
{
	// Console variables
	vid_gamma = Cvar_Get("vid_gamma", "1.0", CVAR_ARCHIVE);
	vid_fullscreen = Cvar_Get("vid_fullscreen", "0", CVAR_ARCHIVE);
	vid_renderer = Cvar_Get("vid_renderer", "gl3", CVAR_ARCHIVE);

	// Commands
	Cmd_AddCommand("vid_restart", VID_Restart_f);
	Cmd_AddCommand("vid_listmodes", VID_ListModes_f);
	Cmd_AddCommand("r_listmodes", VID_ListModes_f); // more consistent with r_mode

	// Initializes the video backend. This is NOT the renderer
	// itself, just the client side support stuff!
	if (!GLimp_Init())
	{
		Com_Error(ERR_FATAL, "Couldn't initialize the graphics subsystem!\n");
	}

	// Load the renderer and get things going.
	VID_CheckChanges();
}

/*
 * Shuts the video stuff down.
 */
void
VID_Shutdown(void)
{
	VID_ShutdownRenderer();
	GLimp_Shutdown();
}

// ----

// Wrappers for the functions provided by the renderer libs.
// =========================================================

void
R_BeginRegistration(const char *map)
{
	unsigned int seed;

	/* pins_seed, latched with pins_record or pins_replay only: the same
	   random numbers from here in the live record and in its replay */
	if (CL_PinsSeed(&seed))
	{
		randk_seed_fixed(seed);
	}

	if (ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_Registration(REC_BEGINREG, map, NULL);
		}

		re.BeginRegistration(map);
	}
}

struct model_s*
R_RegisterModel(const char *name)
{
	if (ref_active)
	{
		struct model_s *mod = re.RegisterModel(name);

		if (RefRec_Recording())
		{
			RefRec_Registration(REC_MODEL, name, mod);
		}

		return mod;
	}

	return NULL;
}

struct image_s*
R_RegisterSkin(const char *name)
{
	if (ref_active)
	{
		struct image_s *skin = re.RegisterSkin(name);

		if (RefRec_Recording())
		{
			RefRec_Registration(REC_SKIN, name, skin);
		}

		return skin;
	}

	return NULL;
}

void
R_SetSky(const char *name, float rotate, vec3_t axis)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			const float f[4] = { rotate, axis[0], axis[1], axis[2] };

			RefRec_Draw(REC_SKY, NULL, 0, f, 4, name, NULL, 0);
		}

		re.SetSky(name, rotate, axis);
	}
}

void
R_EndRegistration(void)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_Registration(REC_ENDREG, NULL, NULL);
		}

		re.EndRegistration();
	}
}

void
R_RenderFrame(refdef_t *fd)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_RenderFrame(fd);
		}

		re.RenderFrame(fd);
	}
}

struct image_s*
Draw_FindPic(const char *name)
{
	if (ref_active)
	{
		struct image_s *pic = re.DrawFindPic(name);

		if (RefRec_Recording())
		{
			RefRec_Registration(REC_PIC, name, pic);
		}

		return pic;
	}

	return NULL;
}


void
Draw_GetPicSize(int *w, int *h, const char *name)
{
	if (ref_active)
	{
		re.DrawGetPicSize(w, h, name);

		if (RefRec_Recording())
		{
			const int32_t a[2] = { w ? *w : 0, h ? *h : 0 };

			RefRec_Draw(REC_PICSIZE, a, 2, NULL, 0, name, NULL, 0);
		}
	}
}

void
Draw_StretchPic(int x, int y, int w, int h, const char *name)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			const int32_t a[4] = { x, y, w, h };

			RefRec_Draw(REC_STRETCHPIC, a, 4, NULL, 0, name, NULL, 0);
		}

		re.DrawStretchPic(x, y, w, h, name);
	}
}

void
Draw_PicScaled(int x, int y, const char *pic, float factor)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			const int32_t a[2] = { x, y };

			RefRec_Draw(REC_PICSCALED, a, 2, &factor, 1, pic, NULL, 0);
		}

		re.DrawPicScaled(x, y, pic, factor);
	}
}

void
Draw_PicScaledCol(int x, int y, const char *pic, float factor, const float color[3])
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			const int32_t a[2] = { x, y };
			/* without a color only the factor is passed; the recorder warns */
			const float f[4] = { factor, color ? color[0] : 1.0f,
				color ? color[1] : 1.0f, color ? color[2] : 1.0f };

			RefRec_Draw(REC_PICSCALEDCOL, a, 2, f, color ? 4 : 1, pic, NULL, 0);
		}

		if (re.DrawPicScaledCol)
		{
			re.DrawPicScaledCol(x, y, pic, factor, color);
		}
		else
		{
			re.DrawPicScaled(x, y, pic, factor);
		}
	}
}

void
Draw_CharScaled(int x, int y, int num, float scale)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			const int32_t a[3] = { x, y, num };

			RefRec_Draw(REC_CHARSCALED, a, 3, &scale, 1, NULL, NULL, 0);
		}

		re.DrawCharScaled(x, y, num, scale);
	}
}

void
Draw_TileClear(int x, int y, int w, int h, const char *name)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			const int32_t a[4] = { x, y, w, h };

			RefRec_Draw(REC_TILECLEAR, a, 4, NULL, 0, name, NULL, 0);
		}

		re.DrawTileClear(x, y, w, h, name);
	}
}

void
Draw_Fill(int x, int y, int w, int h, int c)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			const int32_t a[5] = { x, y, w, h, c };

			RefRec_Draw(REC_FILL, a, 5, NULL, 0, NULL, NULL, 0);
		}

		re.DrawFill(x, y, w, h, c);
	}
}

void
Draw_FadeScreen(void)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_Draw(REC_FADESCREEN, NULL, 0, NULL, 0, NULL, NULL, 0);
		}

		re.DrawFadeScreen();
	}
}

void
Draw_StretchRaw(int x, int y, int w, int h, int cols, int rows, const byte *data, int bits)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			/* cl_cin.c allocates cin.pic as cols * rows * bits / 8 bytes;
			   the recorder refuses sizes it cannot carry with a REC_WARN */
			const int32_t a[7] = { x, y, w, h, cols, rows, bits };

			RefRec_Draw(REC_STRETCHRAW, a, 7, NULL, 0, NULL, data,
				(uint32_t)cols * (uint32_t)rows * (uint32_t)(bits / 8));
		}

		re.DrawStretchRaw(x, y, w, h, cols, rows, data, bits);
	}
}

void
R_SetPalette(const unsigned char *palette)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			/* every palette passed is 768 bytes: cl.cinematicpalette[768], cl_cin.c's Z_Malloc(768) */
			RefRec_Draw(REC_PALETTE, NULL, 0, NULL, 0, NULL, palette, palette ? REFREC_PALETTE_SIZE : 0);
		}

		re.SetPalette(palette);
	}
}

void
R_BeginFrame(float camera_separation)
{
	if (ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_Control(REC_BEGINFRAME, camera_separation);
		}

		re.BeginFrame(camera_separation);
	}
}

qboolean
R_EndWorldRenderpass(void)
{
	if(ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_Control(REC_ENDWORLD, 0);
		}

		return re.EndWorldRenderpass();
	}
	return false;
}

void
R_EndFrame(void)
{
	if(ref_active)
	{
		if (RefRec_Recording())
		{
			RefRec_Control(REC_ENDFRAME, 0);
		}

		re.EndFrame();
	}
}

qboolean
R_IsVSyncActive(void)
{
	if (ref_active)
	{
		return re.IsVSyncActive();
	}

	return false;
}
