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
 * machine-learning super-resolution scaler (a VTFrameProcessor session
 * with a VTSuperResolutionScalerConfiguration of the image input type).
 * macOS 26 and later; VideoToolbox is weak-linked and every call is
 * behind @available, so a build with a lower deployment target reads
 * the backend as unsupported on an older macOS and still runs (the
 * Makefile sets none: a default build's minimum is the build host's).
 *
 * The scaler needs a model the operating system downloads once. Nothing
 * here downloads it except CL_UpscaleVTFetchStart, which only the menu
 * item calls (a key press, which stuffed text cannot make), after the
 * menu has said so (UPSCALE_SAY_VT_FETCH); the download runs on its own
 * and the menu shows its state. A vt run with the model missing refuses.
 *
 * Objects are made only between CL_UpscaleVTBegin and CL_UpscaleVTEnd;
 * CL_UpscaleVTFacts makes one configuration to read the model's status
 * and keeps nothing, and CL_UpscaleVTAvailable and
 * CL_UpscaleVTFetchState, which the menu asks each frame, make none. The padding, the crop and the alpha are cl_upscale.c's
 * (CL_UpscaleRGBA), shared with metalfx.
 *
 * =======================================================================
 */

#import <Foundation/Foundation.h>
#import <CoreVideo/CoreVideo.h>
#import <CoreMedia/CoreMedia.h>
#import <VideoToolbox/VideoToolbox.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "header/upscale_vt.h"

/* the tile size the support checks make their configuration at */
#define VT_PROBE_SIDE 64
/* the configuration's largest image frame on macOS */
#define VT_MAX_SIDE 1920

/* the run's factor: 0 outside a run */
static int vt_factor;
/* the session of the last tile's size, kept for the next of the same */
static id vt_processor;
static int vt_iw, vt_ih;
static OSType vt_format;
static long long vt_frames;
#ifdef UPSCALE_VT_TEST
/* the host test's switch: the checks say isSupported is NO */
int upscale_vt_test_unsupported;
#endif

static void
CL_UpscaleVTSay(char *reason, size_t size, const char *text)
{
	if (reason && (size > 0))
	{
		snprintf(reason, size, "%s", text);
	}
}

static VTSuperResolutionScalerConfiguration *
CL_UpscaleVTConfig(int w, int h, int factor) API_AVAILABLE(macos(26.0));

static VTSuperResolutionScalerConfiguration *
CL_UpscaleVTConfig(int w, int h, int factor)
{
	return [[VTSuperResolutionScalerConfiguration alloc]
		initWithFrameWidth:w frameHeight:h scaleFactor:factor
		inputType:VTSuperResolutionScalerConfigurationInputTypeImage
		usePrecomputedFlow:NO
		qualityPrioritization:VTSuperResolutionScalerConfigurationQualityPrioritizationNormal
		revision:VTSuperResolutionScalerConfiguration.defaultRevision];
}

/* 1 when the class is there and isSupported says yes; no object is made */
static int
CL_UpscaleVTClass(int *os)
{
	*os = 0;

	if (@available(macOS 26.0, *))
	{
		/* weak-linked: nil when the framework or the class is missing */
		if ([VTSuperResolutionScalerConfiguration class] == nil)
		{
			return 0;
		}

		*os = 1;

#ifdef UPSCALE_VT_TEST
		if (upscale_vt_test_unsupported)
		{
			return 0;
		}
#endif

		return VTSuperResolutionScalerConfiguration.isSupported ? 1 : 0;
	}

	return 0;
}

void
CL_UpscaleVTFacts(int factor, upscale_vt_facts_t *facts)
{
	memset(facts, 0, sizeof(*facts));
	facts->built = 1;
	facts->model = -1;
	facts->supported = CL_UpscaleVTClass(&facts->os);

	if (!facts->supported)
	{
		return;
	}

	if (@available(macOS 26.0, *))
	{
		@autoreleasepool
		{
			VTSuperResolutionScalerConfiguration *c;

			facts->factor = [VTSuperResolutionScalerConfiguration.supportedScaleFactors
				containsObject:@(factor)] ? 1 : 0;

			if (!facts->factor)
			{
				return;
			}

			c = CL_UpscaleVTConfig(VT_PROBE_SIDE, VT_PROBE_SIDE, factor);
			facts->config = c ? 1 : 0;

			if (!c)
			{
				return;
			}

			switch (c.configurationModelStatus)
			{
				case VTSuperResolutionScalerConfigurationModelStatusDownloadRequired:
					facts->model = UPSCALE_VT_MODEL_DOWNLOAD;
					break;
				case VTSuperResolutionScalerConfigurationModelStatusDownloading:
					facts->model = UPSCALE_VT_MODEL_DOWNLOADING;
					break;
				case VTSuperResolutionScalerConfigurationModelStatusReady:
					facts->model = UPSCALE_VT_MODEL_READY;
					break;
				default:
					facts->model = 100 + (int)c.configurationModelStatus;
					break;
			}
		}
	}
}

int
CL_UpscaleVTAvailable(int factor, char *reason, size_t size)
{
	int os, ok = 1;

	CL_UpscaleVTSay(reason, size, "");

	if (!CL_UpscaleVTClass(&os))
	{
		CL_UpscaleVTSay(reason, size, os ?
			"VideoToolbox's super-resolution scaler is not supported on this device" :
			"this OS has no VideoToolbox super-resolution scaler (macOS 26 or later)");
		return 0;
	}

	if (@available(macOS 26.0, *))
	{
		/* a class property: no object is made; the menu asks it each frame */
		@autoreleasepool
		{
			ok = [VTSuperResolutionScalerConfiguration.supportedScaleFactors
				containsObject:@(factor)] ? 1 : 0;
		}
	}

	if (!ok && reason && (size > 0))
	{
		snprintf(reason, size, "VideoToolbox's super-resolution scaler has no %dx",
			factor);
	}

	return ok;
}

/*
 * The model's download, which only the menu item starts. It runs on its
 * own: the main thread never waits for it. Its state is read and written
 * on vt_fetch_queue only; the completion handler posts there, and the
 * handler of a fetch the bound gave up on (an older generation) is
 * ignored.
 */
static dispatch_queue_t vt_fetch_queue;
static upscale_vt_fetch_t vt_fetch_state;
static unsigned vt_fetch_gen;
static uint64_t vt_fetch_start;
static char vt_fetch_why[160];
/* the configuration whose model is being fetched, kept until it is done */
static id vt_fetch_config;

static dispatch_queue_t
CL_UpscaleVTFetchQueue(void)
{
	static dispatch_once_t once;

	dispatch_once(&once, ^{
		vt_fetch_queue = dispatch_queue_create("yq2.upscale.vt.fetch",
			DISPATCH_QUEUE_SERIAL);
	});

	return vt_fetch_queue;
}

int
CL_UpscaleVTFetchStart(int factor, char *reason, size_t size)
{
	int os, ok = 0;

	CL_UpscaleVTSay(reason, size, "");

	if (!CL_UpscaleVTClass(&os))
	{
		CL_UpscaleVTSay(reason, size, "VideoToolbox's super-resolution scaler is not supported");
		return 0;
	}

	if (@available(macOS 26.0, *))
	{
		@autoreleasepool
		{
			dispatch_queue_t q = CL_UpscaleVTFetchQueue();
			VTSuperResolutionScalerConfiguration *c;
			__block int running = 0;
			__block unsigned gen = 0;
			unsigned mine;

			dispatch_sync(q, ^{
				running = (vt_fetch_state == UPSCALE_VT_FETCH_RUNNING);
			});

			if (running)
			{
				return 1;
			}

			c = CL_UpscaleVTConfig(VT_PROBE_SIDE, VT_PROBE_SIDE, factor);

			if (!c)
			{
				CL_UpscaleVTSay(reason, size,
					"VideoToolbox refused a super-resolution configuration");
				return 0;
			}

			dispatch_sync(q, ^{
				gen = ++vt_fetch_gen;
				vt_fetch_state = UPSCALE_VT_FETCH_RUNNING;
				vt_fetch_why[0] = '\0';
				vt_fetch_start = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
				vt_fetch_config = c;
			});
			mine = gen;

			[c downloadConfigurationModelWithCompletionHandler:^(NSError *e) {
				NSString *why = e ? [e.localizedDescription copy] : nil;

				dispatch_async(q, ^{
					if (mine != vt_fetch_gen)
					{
						return;
					}

					vt_fetch_config = nil;
					vt_fetch_state = why ? UPSCALE_VT_FETCH_FAILED :
						UPSCALE_VT_FETCH_DONE;

					if (why)
					{
						snprintf(vt_fetch_why, sizeof(vt_fetch_why),
							"the model's download failed: %s",
							why.UTF8String ? why.UTF8String : "?");
					}
				});
			}];

			ok = 1;
		}
	}

	return ok;
}

upscale_vt_fetch_t
CL_UpscaleVTFetchState(char *reason, size_t size)
{
	__block upscale_vt_fetch_t state = UPSCALE_VT_FETCH_IDLE;

	CL_UpscaleVTSay(reason, size, "");

	if (@available(macOS 26.0, *))
	{
		dispatch_sync(CL_UpscaleVTFetchQueue(), ^{
			const uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);

			/* the bound: a fetch that has not finished is given up on */
			if ((vt_fetch_state == UPSCALE_VT_FETCH_RUNNING) &&
				(now - vt_fetch_start >
				 (uint64_t)UPSCALE_VT_FETCH_SECONDS * NSEC_PER_SEC))
			{
				vt_fetch_gen++;
				vt_fetch_config = nil;
				vt_fetch_state = UPSCALE_VT_FETCH_FAILED;
				snprintf(vt_fetch_why, sizeof(vt_fetch_why),
					"the model's download did not finish in %d s",
					UPSCALE_VT_FETCH_SECONDS);
			}

			state = vt_fetch_state;
			CL_UpscaleVTSay(reason, size, vt_fetch_why);
		});
	}

	return state;
}

void
CL_UpscaleVTEnd(void)
{
	if (@available(macOS 26.0, *))
	{
		if (vt_processor)
		{
			[(VTFrameProcessor *)vt_processor endSession];
		}
	}

	vt_processor = nil;
	vt_iw = vt_ih = 0;
	vt_format = 0;
	vt_factor = 0;
}

int
CL_UpscaleVTBegin(int factor, char *reason, size_t size)
{
	upscale_vt_facts_t facts;

	CL_UpscaleVTEnd();
	CL_UpscaleVTSay(reason, size, "");
	CL_UpscaleVTFacts(factor, &facts);

	if (!facts.supported || !facts.factor || !facts.config ||
		(facts.model != UPSCALE_VT_MODEL_READY))
	{
		CL_UpscaleVTSay(reason, size, "the super-resolution scaler is not ready");
		return 0;
	}

	vt_factor = factor;
	vt_frames = 0;

	return 1;
}

/* the session for an iw by ih tile at the run's factor: the last one when the same */
static VTFrameProcessor *
CL_UpscaleVTSession(int iw, int ih) API_AVAILABLE(macos(26.0));

static VTFrameProcessor *
CL_UpscaleVTSession(int iw, int ih)
{
	VTSuperResolutionScalerConfiguration *c;
	VTFrameProcessor *p;
	OSType format = 0;

	if (vt_processor && (iw == vt_iw) && (ih == vt_ih))
	{
		return vt_processor;
	}

	if (vt_processor)
	{
		[(VTFrameProcessor *)vt_processor endSession];
	}

	vt_processor = nil;
	vt_iw = vt_ih = 0;

	c = CL_UpscaleVTConfig(iw, ih, vt_factor);

	if (!c)
	{
		return nil;
	}

	/*
	 * 32-bit BGRA, else RGBA, else 64-bit half-float RGBA (the only
	 * format this OS's scaler lists); the copies below convert
	 */
	for (NSNumber *n in c.frameSupportedPixelFormats)
	{
		if (n.unsignedIntValue == kCVPixelFormatType_32BGRA)
		{
			format = kCVPixelFormatType_32BGRA;
			break;
		}

		if ((n.unsignedIntValue == kCVPixelFormatType_32RGBA) &&
			(format != kCVPixelFormatType_32BGRA))
		{
			format = kCVPixelFormatType_32RGBA;
		}

		if (!format && (n.unsignedIntValue == kCVPixelFormatType_64RGBAHalf))
		{
			format = kCVPixelFormatType_64RGBAHalf;
		}
	}

	if (!format)
	{
		return nil;
	}

	p = [[VTFrameProcessor alloc] init];

	if (!p || ![p startSessionWithConfiguration:c error:NULL])
	{
		return nil;
	}

	vt_processor = p;
	vt_iw = iw;
	vt_ih = ih;
	vt_format = format;

	return p;
}

/* a w by h buffer of the session's format, IOSurface-backed */
static CVPixelBufferRef
CL_UpscaleVTBuffer(int w, int h)
{
	NSDictionary *attrs = @{
		(__bridge NSString *)kCVPixelBufferIOSurfacePropertiesKey: @{},
		(__bridge NSString *)kCVPixelBufferMetalCompatibilityKey: @YES,
	};
	CVPixelBufferRef b = NULL;

	if (CVPixelBufferCreate(kCFAllocatorDefault, (size_t)w, (size_t)h,
		vt_format, (__bridge CFDictionaryRef)attrs, &b) != kCVReturnSuccess)
	{
		return NULL;
	}

	return b;
}

/*
 * A byte (0 to 255) as the IEEE half-float bit pattern of v / 255 (the
 * scaler's colours run from 0 to 1), and back. The compiler's _Float16
 * does the rounding.
 */
static unsigned short
CL_UpscaleVTHalf(unsigned char v)
{
	const _Float16 f = (_Float16)(v / 255.0f);
	unsigned short h;

	memcpy(&h, &f, sizeof(h));

	return h;
}

static unsigned char
CL_UpscaleVTUnhalf(unsigned short h)
{
	int e = (h >> 10) & 0x1f;
	float f;

	if (h & 0x8000)
	{
		return 0;
	}

	if (e == 0x1f)
	{
		return (h & 0x3ff) ? 0 : 255;
	}

	f = (e ? (1.0f + (h & 0x3ff) / 1024.0f) * ldexpf(1.0f, e - 15) :
		(h & 0x3ff) / 1024.0f * ldexpf(1.0f, -14)) * 255.0f + 0.5f;

	return (f >= 255.0f) ? 255 : (unsigned char)f;
}

/* RGBA8 rows to or from the buffer; BGRA swaps red and blue */
static int
CL_UpscaleVTCopy(CVPixelBufferRef b, unsigned char *rgba, int w, int h,
		int into)
{
	const int half = (vt_format == kCVPixelFormatType_64RGBAHalf);
	const int swap = (vt_format == kCVPixelFormatType_32BGRA);
	const size_t bpp = half ? 8 : 4;
	unsigned char *base;
	size_t stride;
	int x, y, c;

	if (CVPixelBufferLockBaseAddress(b, into ? 0 :
		kCVPixelBufferLock_ReadOnly) != kCVReturnSuccess)
	{
		return 0;
	}

	base = CVPixelBufferGetBaseAddress(b);
	stride = CVPixelBufferGetBytesPerRow(b);

	if (!base || (stride < (size_t)w * bpp) ||
		(CVPixelBufferGetWidth(b) != (size_t)w) ||
		(CVPixelBufferGetHeight(b) != (size_t)h))
	{
		CVPixelBufferUnlockBaseAddress(b, into ? 0 : kCVPixelBufferLock_ReadOnly);
		return 0;
	}

	for (y = 0; y < h; y++)
	{
		unsigned char *row = base + (size_t)y * stride;
		unsigned char *t = rgba + (size_t)y * w * 4;

		for (x = 0; x < w; x++, row += bpp, t += 4)
		{
			if (half)
			{
				unsigned short *r = (unsigned short *)row;

				for (c = 0; c < 4; c++)
				{
					if (into)
					{
						r[c] = CL_UpscaleVTHalf(c == 3 ? 255 : t[c]);
					}
					else
					{
						t[c] = (c == 3) ? 255 : CL_UpscaleVTUnhalf(r[c]);
					}
				}

				continue;
			}

			{
				unsigned char *to = into ? row : t;
				const unsigned char *from = into ? t : row;

				to[0] = from[swap ? 2 : 0];
				to[1] = from[1];
				to[2] = from[swap ? 0 : 2];
				to[3] = 255;
			}
		}
	}

	CVPixelBufferUnlockBaseAddress(b, into ? 0 : kCVPixelBufferLock_ReadOnly);

	return 1;
}

int
CL_UpscaleVTScale(void *ctx, const unsigned char *in, int iw, int ih,
		unsigned char *out, int ow, int oh)
{
	int ok = 0;

	(void)ctx;

	if (!vt_factor || !in || !out || (iw < 1) || (ih < 1) ||
		(iw > VT_MAX_SIDE) || (ih > VT_MAX_SIDE) ||
		(ow != iw * vt_factor) || (oh != ih * vt_factor))
	{
		return 0;
	}

	if (@available(macOS 26.0, *))
	{
		@autoreleasepool
		{
			VTFrameProcessor *p = CL_UpscaleVTSession(iw, ih);
			CVPixelBufferRef sb = NULL, db = NULL;
			VTFrameProcessorFrame *sf, *df;
			VTSuperResolutionScalerParameters *params;
			CMTime t;

			if (!p)
			{
				return 0;
			}

			sb = CL_UpscaleVTBuffer(iw, ih);
			db = CL_UpscaleVTBuffer(ow, oh);

			/* the copy in takes const-free rows; in is not written */
			if (sb && db && CL_UpscaleVTCopy(sb, (unsigned char *)in, iw, ih, 1))
			{
				t = CMTimeMake(vt_frames++, 30);
				sf = [[VTFrameProcessorFrame alloc] initWithBuffer:sb presentationTimeStamp:t];
				df = [[VTFrameProcessorFrame alloc] initWithBuffer:db presentationTimeStamp:t];
				params = (sf && df) ? [[VTSuperResolutionScalerParameters alloc]
					initWithSourceFrame:sf previousFrame:nil previousOutputFrame:nil
					opticalFlow:nil
					submissionMode:VTSuperResolutionScalerParametersSubmissionModeRandom
					destinationFrame:df] : nil;

				ok = params && [p processWithParameters:params error:NULL] &&
					CL_UpscaleVTCopy(db, out, ow, oh, 0);
			}

			if (sb)
			{
				CVPixelBufferRelease(sb);
			}

			if (db)
			{
				CVPixelBufferRelease(db);
			}
		}
	}

	return ok;
}
