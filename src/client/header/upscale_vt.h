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
 * The vt backend of upscale_textures (TASK167): VideoToolbox's
 * super-resolution scaler (VTSuperResolutionScalerConfiguration, macOS
 * 26) in src/client/cl_upscale_vt.m on macOS. Elsewhere, iOS included,
 * src/client/cl_upscale_cmd.c has the same functions, which say the
 * backend is not built. Plain C, no client header, so the host test
 * (tests/metal/upscale_vt_test.m) builds the file alone. What a run does
 * with the checks is CL_UpscaleVTState's (cl_upscale.c, unit-tested).
 *
 * =======================================================================
 */

#ifndef CL_HEADER_UPSCALE_VT_H
#define CL_HEADER_UPSCALE_VT_H

#include <stddef.h>

/* the model's status, as configurationModelStatus gives it */
typedef enum
{
	UPSCALE_VT_MODEL_DOWNLOAD,	/* the operating system has to fetch it */
	UPSCALE_VT_MODEL_DOWNLOADING,	/* a fetch is under way */
	UPSCALE_VT_MODEL_READY		/* on this machine */
} upscale_vt_model_t;

/* the support checks of a vt run, in the order they are judged */
typedef struct
{
	int built;	/* cl_upscale_vt.m is built here (the macOS client) */
	int os;		/* the OS has VTSuperResolutionScalerConfiguration */
	int supported;	/* its isSupported */
	int factor;	/* its supportedScaleFactors has the run's factor */
	int config;	/* a configuration at the run's factor was made */
	int model;	/* that configuration's model status, upscale_vt_model_t */
} upscale_vt_facts_t;

typedef enum
{
	UPSCALE_VT_UNSUPPORTED,	/* scale2x, with the reason */
	UPSCALE_VT_READY,	/* the run may scale */
	UPSCALE_VT_FETCH	/* the model is missing: a run refuses, the menu may fetch it */
} upscale_vt_state_t;

/*
 * What a vt run at factor does with facts, checked in the struct's
 * order: unsupported with a one-line reason in reason ("" otherwise),
 * ready, or the model is missing or being fetched (the reason is then
 * UPSCALE_VT_NO_MODEL or UPSCALE_VT_FETCHING, the refusal a run prints).
 * In cl_upscale.c.
 */
upscale_vt_state_t CL_UpscaleVTState(const upscale_vt_facts_t *facts,
		int factor, char *reason, size_t size);

/*
 * The checks at factor into facts. Asks the class and one configuration
 * of a small tile; keeps nothing and fetches nothing.
 */
void CL_UpscaleVTFacts(int factor, upscale_vt_facts_t *facts);

/*
 * 1 when the backend may be offered at factor: it is built, the OS has
 * the class, isSupported says yes and supportedScaleFactors has factor.
 * Else 0 and a one-line reason. Nothing a run found is remembered, so a
 * refusal at one factor greys nothing at another. Makes no object: the
 * menu asks it each frame.
 */
int CL_UpscaleVTAvailable(int factor, char *reason, size_t size);

/* the model's download, which only the menu item starts */
typedef enum
{
	UPSCALE_VT_FETCH_IDLE,		/* none started by this process */
	UPSCALE_VT_FETCH_RUNNING,	/* the operating system is downloading it */
	UPSCALE_VT_FETCH_DONE,		/* the download finished */
	UPSCALE_VT_FETCH_FAILED		/* it failed or outlived its bound: the reason */
} upscale_vt_fetch_t;

/*
 * Asks the operating system to download the model of a configuration
 * at factor and returns at once: 1 when the download was started (or
 * is running already), else 0 and a one-line reason. Only the menu
 * item calls it (a key press, which stuffed text cannot make), after
 * UPSCALE_SAY_VT_FETCH is printed; tests/check-upscale-vt.sh keeps it
 * so. A run never fetches: it refuses with UPSCALE_VT_NO_MODEL.
 */
int CL_UpscaleVTFetchStart(int factor, char *reason, size_t size);

/*
 * The download's state, and for UPSCALE_VT_FETCH_FAILED its reason. A
 * download running longer than UPSCALE_VT_FETCH_SECONDS is given up on
 * here (FAILED; a late completion is ignored). Makes no object: the
 * menu asks it each frame. A failure is not remembered beyond the next
 * start: a press tries again.
 */
upscale_vt_fetch_t CL_UpscaleVTFetchState(char *reason, size_t size);

/*
 * What the menu's vt item shows: 1 when it may be pressed, else 0
 * (greyed); its status-bar line into status. available and why are
 * CL_UpscaleVTAvailable's at the selected factor, fetch and fetchwhy
 * CL_UpscaleVTFetchState's. In cl_upscale.c.
 */
int CL_UpscaleVTItem(int available, const char *why, upscale_vt_fetch_t fetch,
		const char *fetchwhy, char *status, size_t size);

/* Starts a run at factor (2 or 4): 1, or 0 and a one-line reason. */
int CL_UpscaleVTBegin(int factor, char *reason, size_t size);

/*
 * Scales the RGBA8 in (iw by ih) into out (ow by oh, the run's factor
 * times the input) with a VTFrameProcessor session, kept for the next
 * tile of the same size. 1 on success; 0 outside a run, for another
 * factor, or when VideoToolbox fails. ctx is not used (upscale_scaler_t).
 * The alpha out is 255; CL_UpscaleRGBA sets it from the indices.
 */
int CL_UpscaleVTScale(void *ctx, const unsigned char *in, int iw, int ih,
		unsigned char *out, int ow, int oh);

/* Ends the run: every object CL_UpscaleVTBegin and Scale made is let go. */
void CL_UpscaleVTEnd(void);

/* a vt run's refusal when the model is missing, or being downloaded */
#define UPSCALE_VT_NO_MODEL "VideoToolbox's super-resolution model is not on " \
	"this machine: fetch it from the menu (Options, upscale textures, " \
	"VideoToolbox ML)"
#define UPSCALE_VT_FETCHING "VideoToolbox's super-resolution model is being " \
	"downloaded: run again when the menu says it is ready"

/* said once, by the menu item, before the model is fetched */
#define UPSCALE_SAY_VT_FETCH "upscale_textures: vt: the operating system " \
	"will now download VideoToolbox's super-resolution model (once, from " \
	"Apple)\n"

/* the longest wait for the model's download, in seconds */
#define UPSCALE_VT_FETCH_SECONDS 900

#endif /* CL_HEADER_UPSCALE_VT_H */
