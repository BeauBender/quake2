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
 * The MetalFX scalers of ref_metal's 3D view (TASK111; decision 0011;
 * header/rm_metalfx.h). MetalFX is weak-linked: where its classes are
 * missing every mode reads as unsupported. The scaler runs at the seam
 * of the G-buffer contract (rmb_gbuf_view_end): after the 3D view, drawn
 * at the input size into the render-size colour target, and in place of
 * the bilinear stretch into the frame. Its output goes to a target of
 * the world view's rectangle, copied into the frame at that rectangle.
 * Nothing here is made while every mode is off.
 *
 * =======================================================================
 */

#import <Metal/Metal.h>
#include <TargetConditionals.h>

/* the iOS, tvOS and visionOS simulator SDKs have no MetalFX: there the back end is stubs (end of file) */
#if __has_include(<MetalFX/MetalFX.h>)
#define RMB_HAVE_METALFX 1
#import <MetalFX/MetalFX.h>
#endif

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#include "header/rmb.h"
#include "header/rm_metalfx.h"
#include "header/rmb_internal.h"

#if !__has_feature(objc_arc)
#error "rmb_metalfx.m is written for ARC: compile it with -fobjc-arc"
#endif

#ifdef RMB_HAVE_METALFX

extern id<MTLDevice> rmb_device;
extern id<MTLCommandQueue> rmb_queue;

/* the formats of the G-buffer contract: the render-size colour target, the frame, the depth and motion */
#define RMB_FX_COLOR MTLPixelFormatBGRA8Unorm
#define RMB_FX_DEPTH MTLPixelFormatDepth32Float
#define RMB_FX_MOTION MTLPixelFormatRG16Float

static id<MTLFXSpatialScaler> fx_spatial;
static id<MTLFXTemporalScaler> fx_temporal;
static id<MTLTexture> fx_out;       /* the output, the world view's rectangle */
static int fx_mode;                 /* the mode of the scaler made, 0 for none */
static rm_fx_size_t fx_size;
static int fx_ready;                /* prepared for the view being drawn */
static rm_fx_refusal_t fx_refusal;  /* the last scaler that could not be made */
static int fx_note;                 /* RM_FX_NOTE_* for the engine to print (rmb_fx_note) */
static int fx_note_mode;
static rm_fx_size_t fx_note_size;
static float fx_jx, fx_jy;
static int fx_reset;
static int fx_timing;
static _Atomic double fx_ms = -1.0;

unsigned
rmb_fx_support(void)
{
	unsigned has = RM_FX_HAS(RM_FX_OFF);

	if (!rmb_device)
	{
		return has;
	}

	if (@available(macOS 13.0, iOS 16.0, tvOS 27.1, *))
	{
		if ([MTLFXSpatialScalerDescriptor class] && [MTLFXSpatialScalerDescriptor supportsDevice:rmb_device])
		{
			has |= RM_FX_HAS(RM_FX_SPATIAL);
		}

		if ([MTLFXTemporalScalerDescriptor class] && [MTLFXTemporalScalerDescriptor supportsDevice:rmb_device])
		{
			has |= RM_FX_HAS(RM_FX_TEMPORAL);
		}
	}

	if (@available(macOS 26.0, iOS 26.0, tvOS 27.1, *))
	{
		if ([MTLFXTemporalDenoisedScalerDescriptor class] &&
			[MTLFXTemporalDenoisedScalerDescriptor supportsDevice:rmb_device])
		{
			has |= RM_FX_HAS(RM_FX_DENOISED);
		}

		if ([MTLFXFrameInterpolatorDescriptor class] && [MTLFXFrameInterpolatorDescriptor supportsDevice:rmb_device])
		{
			has |= RM_FX_HAS_INTERP;
		}
	}

	return has;
}

float
rmb_fx_max_ratio(int mode)
{
	if (!rmb_device)
	{
		return RM_FX_RATIO_MAX;
	}

	if (mode == RM_FX_TEMPORAL)
	{
		if (@available(macOS 14.0, iOS 17.0, tvOS 27.1, *))
		{
			if ([MTLFXTemporalScalerDescriptor class])
			{
				return [MTLFXTemporalScalerDescriptor supportedInputContentMaxScaleForDevice:rmb_device];
			}
		}
	}

	if (mode == RM_FX_DENOISED)
	{
		if (@available(macOS 26.0, iOS 26.0, tvOS 27.1, *))
		{
			if ([MTLFXTemporalDenoisedScalerDescriptor class])
			{
				return [MTLFXTemporalDenoisedScalerDescriptor supportedInputContentMaxScaleForDevice:rmb_device];
			}
		}
	}

	return RM_FX_RATIO_MAX;
}

/* the engine prints it: once for each mode and size, as the callers see to */
static void
rmb_fx_tell(int note, int mode, const rm_fx_size_t *s)
{
	fx_note = note;
	fx_note_mode = mode;
	fx_note_size = *s;
}

int
rmb_fx_note(int *mode, rm_fx_size_t *s)
{
	const int note = fx_note;

	*mode = fx_note_mode;
	*s = fx_note_size;
	fx_note = RM_FX_NOTE_NONE;
	return note;
}

/* frees the scaler and its output target; the refusal memo is kept */
static void
rmb_fx_free(void)
{
	@autoreleasepool
	{
		if (fx_spatial)
		{
			fx_spatial = nil;
			rmb_ledger_note(RM_OBJ_FX, 0);
		}

		if (fx_temporal)
		{
			fx_temporal = nil;
			rmb_ledger_note(RM_OBJ_FX, 0);
		}

		if (fx_out)
		{
			fx_out = nil;
			rmb_ledger_note(RM_OBJ_FX, 0);
		}
	}

	fx_mode = 0;
	fx_ready = 0;
	memset(&fx_size, 0, sizeof(fx_size));
}

void
rmb_fx_release(void)
{
	rmb_fx_free();
	rm_fx_refusal_clear(&fx_refusal);
	fx_note = RM_FX_NOTE_NONE;
}

/* the scaler of mode at s and its output target; 0, with nothing kept, when one cannot be made */
static int
rmb_fx_make(int mode, const rm_fx_size_t *s)
{
	MTLTextureUsage out_usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;

	if (mode == RM_FX_SPATIAL)
	{
		if (@available(macOS 13.0, iOS 16.0, tvOS 27.1, *))
		{
			MTLFXSpatialScalerDescriptor *d = [MTLFXSpatialScalerDescriptor new];

			d.colorTextureFormat = RMB_FX_COLOR;
			d.outputTextureFormat = RMB_FX_COLOR;
			d.inputWidth = (NSUInteger)s->iw;
			d.inputHeight = (NSUInteger)s->ih;
			d.outputWidth = (NSUInteger)s->ow;
			d.outputHeight = (NSUInteger)s->oh;
			d.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
			fx_spatial = [d newSpatialScalerWithDevice:rmb_device];

			if (fx_spatial)
			{
				rmb_ledger_note(RM_OBJ_FX, 1);
				out_usage |= fx_spatial.outputTextureUsage;
			}
		}
	}
	else if (mode == RM_FX_TEMPORAL)
	{
		if (@available(macOS 13.0, iOS 16.0, tvOS 27.1, *))
		{
			MTLFXTemporalScalerDescriptor *d = [MTLFXTemporalScalerDescriptor new];

			d.colorTextureFormat = RMB_FX_COLOR;
			d.depthTextureFormat = RMB_FX_DEPTH;
			d.motionTextureFormat = RMB_FX_MOTION;
			d.outputTextureFormat = RMB_FX_COLOR;
			d.inputWidth = (NSUInteger)s->iw;
			d.inputHeight = (NSUInteger)s->ih;
			d.outputWidth = (NSUInteger)s->ow;
			d.outputHeight = (NSUInteger)s->oh;
			d.autoExposureEnabled = NO;   /* the stock display-referred colour: fixed exposure (design section 2) */
			fx_temporal = [d newTemporalScalerWithDevice:rmb_device];

			if (fx_temporal)
			{
				rmb_ledger_note(RM_OBJ_FX, 1);
				out_usage |= fx_temporal.outputTextureUsage;
			}
		}
	}

	if (!fx_spatial && !fx_temporal)
	{
		return 0;
	}

	{
		MTLTextureDescriptor *t = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:RMB_FX_COLOR
				width:(NSUInteger)s->ow height:(NSUInteger)s->oh mipmapped:NO];

		t.storageMode = MTLStorageModePrivate;
		t.usage = out_usage;
		fx_out = [rmb_device newTextureWithDescriptor:t];
	}

	if (!fx_out)
	{
		rmb_fx_free();
		return 0;
	}

	rmb_ledger_note(RM_OBJ_FX, 1);
	fx_mode = mode;
	fx_size = *s;
	return 1;
}

int
rmb_fx_prepare(int mode, const rm_fx_size_t *s, float jx, float jy, int reset)
{
	static int hdr_told;

	fx_ready = 0;

	/* r_hdr's float16 path (TASK196): the scalers take BGRA8Unorm, so none runs until HD4; told once */
	if (rm_hdr_fx_mode(rmb_hdr_on(), mode) != mode)
	{
		if (fx_mode)
		{
			rmb_fx_free();
		}

		if (!hdr_told)
		{
			hdr_told = 1;
			rmb_fx_tell(RM_FX_NOTE_HDR, mode, s);
		}

		return 0;
	}

	hdr_told = 0;

	if ((mode != RM_FX_SPATIAL) && (mode != RM_FX_TEMPORAL))
	{
		if (fx_mode)
		{
			rmb_fx_free();
		}

		/* a mode turned off: the next one is tried again */
		rm_fx_refusal_clear(&fx_refusal);
		return 0;
	}

	if ((mode != fx_mode) || !rm_fx_same_size(s, &fx_size))
	{
		/* a scaler refused before in this mode and at these sizes is not tried again */
		const int was_refused = rm_fx_refused(&fx_refusal, mode, s);

		rmb_fx_free();

		if (was_refused)
		{
			return 0;
		}

		@autoreleasepool
		{
			if (!rmb_fx_make(mode, s))
			{
				/* told once for each mode and size; the view is stretched as without a scaler */
				rm_fx_refuse(&fx_refusal, mode, s);
				rmb_fx_tell(RM_FX_NOTE_UNMADE, mode, s);
				return 0;
			}
		}

		/* a scaler made anew has no history */
		reset = 1;
	}

	fx_jx = jx;
	fx_jy = jy;
	fx_reset = reset != 0;
	fx_ready = 1;
	return 1;
}

int
rmb_fx_ready(void)
{
	return fx_ready;
}

void
rmb_fx_unready(void)
{
	fx_ready = 0;
}

void
rmb_fx_set_timing(int on)
{
	fx_timing = on != 0;
}

double
rmb_fx_gpu_ms(void)
{
	return atomic_load(&fx_ms);
}

/* the texture has the usage the scaler needs and is at least w x h of format f */
static int
rmb_fx_fits(id<MTLTexture> t, MTLTextureUsage need, MTLPixelFormat f, int w, int h)
{
	return t && ((t.usage & need) == need) && (t.pixelFormat == f) && (t.width >= (NSUInteger)w) &&
		(t.height >= (NSUInteger)h);
}

int
rmb_fx_upscale(id<MTLTexture> color, id<MTLTexture> depth, id<MTLTexture> motion, id<MTLTexture> frame)
{
	unsigned long serial = 0;
	id<MTLCommandBuffer> cmd, fcmd;
	const rm_fx_size_t *s = &fx_size;
	int ok = 0;

	if (!fx_ready)
	{
		return 0;
	}

	fx_ready = 0;

	if (!frame || (frame.pixelFormat != RMB_FX_COLOR) || ((NSUInteger)(s->ox + s->ow) > frame.width) ||
		((NSUInteger)(s->oy + s->oh) > frame.height))
	{
		return 0;
	}

	@autoreleasepool
	{
		if (fx_spatial)
		{
			ok = rmb_fx_fits(color, fx_spatial.colorTextureUsage, RMB_FX_COLOR, s->iw, s->ih);
		}
		else if (fx_temporal)
		{
			ok = rmb_fx_fits(color, fx_temporal.colorTextureUsage, RMB_FX_COLOR, s->iw, s->ih) &&
				rmb_fx_fits(depth, fx_temporal.depthTextureUsage, RMB_FX_DEPTH, s->iw, s->ih) &&
				rmb_fx_fits(motion, fx_temporal.motionTextureUsage, RMB_FX_MOTION, s->iw, s->ih);
		}

		if (!ok)
		{
			/* told once for each mode and size: the scaler is dropped and refused at them */
			rmb_fx_tell(RM_FX_NOTE_UNFIT, fx_mode, s);
			rm_fx_refuse(&fx_refusal, fx_mode, s);
			rmb_fx_free();
			return 0;
		}

		/* timed (metal_stats): the scaler alone in a command buffer, between the frame's two parts */
		cmd = nil;

		if (fx_timing && rmb_frame_cut())
		{
			cmd = [rmb_queue commandBuffer];
		}

		fcmd = rmb_frame_compute(&serial);

		if (!fcmd)
		{
			return 0;
		}

		if (!cmd)
		{
			cmd = fcmd;
		}

		if (fx_spatial)
		{
			fx_spatial.colorTexture = color;
			fx_spatial.outputTexture = fx_out;
			fx_spatial.inputContentWidth = (NSUInteger)s->iw;
			fx_spatial.inputContentHeight = (NSUInteger)s->ih;
			[fx_spatial encodeToCommandBuffer:cmd];
		}
		else
		{
			fx_temporal.colorTexture = color;
			fx_temporal.depthTexture = depth;
			fx_temporal.motionTexture = motion;
			fx_temporal.outputTexture = fx_out;
			fx_temporal.inputContentWidth = (NSUInteger)s->iw;
			fx_temporal.inputContentHeight = (NSUInteger)s->ih;
			fx_temporal.jitterOffsetX = fx_jx;
			fx_temporal.jitterOffsetY = fx_jy;
			/* the G-buffer contract: render-size pixels, previous minus current, +y down; depth not reversed */
			fx_temporal.motionVectorScaleX = 1.0f;
			fx_temporal.motionVectorScaleY = 1.0f;
			fx_temporal.depthReversed = NO;
			fx_temporal.reset = fx_reset ? YES : NO;
			[fx_temporal encodeToCommandBuffer:cmd];
		}

		if (cmd != fcmd)
		{
			cmd.label = @"ref_metal metalfx";
			rmb_watch(cmd);
			[cmd addCompletedHandler:^(id<MTLCommandBuffer> b) {
				if (b.status == MTLCommandBufferStatusCompleted)
				{
					atomic_store(&fx_ms, (b.GPUEndTime - b.GPUStartTime) * 1000.0);
				}
			}];
			[cmd commit];
		}

		/* the output into the frame at the world view's rectangle */
		{
			id<MTLBlitCommandEncoder> blit = [fcmd blitCommandEncoder];

			if (!blit)
			{
				return 0;
			}

			[blit copyFromTexture:fx_out sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
					sourceSize:MTLSizeMake((NSUInteger)s->ow, (NSUInteger)s->oh, 1)
					toTexture:frame destinationSlice:0 destinationLevel:0
					destinationOrigin:MTLOriginMake((NSUInteger)s->ox, (NSUInteger)s->oy, 0)];
			[blit endEncoding];
		}
	}

	return 1;
}

#else /* no MetalFX in this SDK: every mode reads as unsupported, as on a Mac without MetalFX (decision 0011) */

unsigned
rmb_fx_support(void)
{
	return RM_FX_HAS(RM_FX_OFF);
}

float
rmb_fx_max_ratio(int mode)
{
	(void)mode;
	return RM_FX_RATIO_MAX;
}

int
rmb_fx_prepare(int mode, const rm_fx_size_t *s, float jx, float jy, int reset)
{
	(void)mode;
	(void)s;
	(void)jx;
	(void)jy;
	(void)reset;
	return 0;
}

int
rmb_fx_ready(void)
{
	return 0;
}

void
rmb_fx_unready(void)
{
}

int
rmb_fx_note(int *mode, rm_fx_size_t *s)
{
	*mode = RM_FX_OFF;
	memset(s, 0, sizeof(*s));
	return RM_FX_NOTE_NONE;
}

void
rmb_fx_set_timing(int on)
{
	(void)on;
}

double
rmb_fx_gpu_ms(void)
{
	return -1.0;
}

void
rmb_fx_release(void)
{
}

int
rmb_fx_upscale(id<MTLTexture> color, id<MTLTexture> depth, id<MTLTexture> motion, id<MTLTexture> frame)
{
	(void)color;
	(void)depth;
	(void)motion;
	(void)frame;
	return 0;
}

#endif /* RMB_HAVE_METALFX */
