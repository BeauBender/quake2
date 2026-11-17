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
	UPSCALE_VT_FETCH	/* the model is to be fetched first */
} upscale_vt_state_t;

/*
 * What a vt run at factor does with facts, checked in the struct's
 * order: unsupported with a one-line reason in reason ("" otherwise),
 * ready, or fetch the model first (it is missing or being fetched).
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
 * 1 when the backend may be offered: it is built, the OS has the class,
 * isSupported says yes, and no run of this process has found it
 * unsupported. Else 0 and a one-line reason. Makes no object: the menu
 * asks it.
 */
int CL_UpscaleVTAvailable(char *reason, size_t size);

/* a run found the backend unsupported: the menu gives this reason */
void CL_UpscaleVTRefused(const char *reason);

/*
 * Asks the operating system to download the model of a configuration
 * at factor, and waits at most UPSCALE_VT_FETCH_SECONDS: 1 when the
 * model is ready after, else 0 and a one-line reason. Called only by a
 * vt run, after UPSCALE_SAY_VT_FETCH is printed.
 */
int CL_UpscaleVTFetch(int factor, char *reason, size_t size);

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

/* said once before the model is fetched; nothing is fetched without vt */
#define UPSCALE_SAY_VT_FETCH "upscale_textures: vt: the operating system " \
	"will now download VideoToolbox's super-resolution model (once, from " \
	"Apple)\n"

/* the longest wait for the model's download, in seconds */
#define UPSCALE_VT_FETCH_SECONDS 900

#endif /* CL_HEADER_UPSCALE_VT_H */
