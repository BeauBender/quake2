/*
 * The metal_colorspace probe (TASK164). rmb_cs_apply and rmb_cs_release
 * (src/client/refresh/metal/rmb_device.m) put the sRGB colour space on the
 * layer and take it off again; the layer must be left as it was found. No
 * host test covers rmb_*.m, and the 1 -> 0 bug of TASK121 lived here.
 *
 * The renderer's own code is under test, not a copy: this file includes
 * rmb_device.m whole (its two functions are static) and rmb_resource.m
 * (FIX133: rmb_fmt_pipe and rmb_fmt_bind, with its test seam fmt_refuse)
 * and rmb_metalfx.m (TASK196 HD4: the MetalFX scalers on r_hdr's float16
 * path, with its test seam fx_refuse), and defines the symbols those files
 * call elsewhere (rmb_encode_abandon, rmb_encode_release, rmb_encode_split,
 * rmb_frame_serial; rmb_frame_cut, and rmb_frame_compute, which hands out
 * one real command buffer and counts its calls) as stubs; rm_pure.c
 * supplies the ledger and rm_metalfx.c the MetalFX helpers (and both
 * are linked with what the other host tests link them with). A
 * standalone CAMetalLayer is set up as rmb_attach does (rmb_attach itself
 * runs on it), no window and no game data. With no Metal device the probe
 * prints one skip line and passes.
 *
 * Built and run by tests/check-ref-metal.sh on any Darwin (with the
 * script's sanitizers when the compiler has them; with no Metal device it
 * skips itself): clang -fobjc-arc rmb_cs_probe.m rm_pure.c rm_metalfx.c -framework Metal ...
 * With --measure it runs only HD4's measurements (fx_measure), by hand.
 */

#include <math.h>
#include <stdlib.h>
#include <unistd.h>

#include "rmb_device.m"
#include "rmb_resource.m"
#include "rmb_metalfx.m"

/* HD4: the frame's command buffer (rmb_encode.m's), one for each frame the probe commits */
static id<MTLCommandBuffer> probe_cmd;
static int probe_computes;

id<MTLCommandBuffer>
rmb_frame_compute(unsigned long *frame)
{
	(void)frame;
	probe_computes++;

	if (!probe_cmd)
	{
		probe_cmd = [rmb_queue commandBuffer];
	}

	return probe_cmd;
}

int rmb_frame_cut(void) { return 0; }

void rmb_encode_abandon(void) {}
void rmb_encode_release(void) {}
void rmb_encode_split(void) {}
unsigned long rmb_frame_serial;
void Com_Printf(const char *fmt, ...) { (void)fmt; }   /* shared.c's, for the files linked with it */

static int failures, checks;

#define CHECK(cond) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			failures++; \
			fprintf(stderr, "rmb_cs_probe: %s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
		} \
	} while (0)

/* a retain count that is wrong: stop at once. Any further release or retain acts on a reference
   the code under test has already given away (a use after release: the probe would die by a
   signal and leave a crash report); nothing is released, and the process leaves without unwinding */
#define CHECK_COUNT(cond) \
	do { \
		checks++; \
		if (!(cond)) \
		{ \
			fprintf(stderr, "rmb_cs_probe: %s:%d: FAILED: %s\n", __FILE__, __LINE__, #cond); \
			fprintf(stderr, "rmb_cs_probe: stopped at the first wrong retain count, %d checks\n", checks); \
			fflush(stderr); \
			_exit(1); \
		} \
	} while (0)

/* the layer's colour space is the one at attach: the same object, or none in both */
static int
same_as(CAMetalLayer *layer, CGColorSpaceRef want)
{
	@autoreleasepool
	{
		CGColorSpaceRef have = layer.colorspace;

		return (have == want) || (have && want && CFEqual(have, want));
	}
}

static int
is_srgb(CAMetalLayer *layer)
{
	int eq;

	@autoreleasepool
	{
		CGColorSpaceRef have = layer.colorspace;
		CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);

		eq = have && srgb && CFEqual(have, srgb);
		CGColorSpaceRelease(srgb);
	}

	return eq;
}

/*
 * The retain count of a colour space the probe made itself (rmb_cs_own is a
 * retained reference to the layer's: a missing CGColorSpaceRetain there is an
 * over-release, and the count after a cycle falls short of the one before).
 * A named colour space (Display P3, sRGB) is cached by the system, its count
 * says nothing, so the counted run uses a calibrated colour space made here.
 */
static long
count_of(CGColorSpaceRef cs)
{
	return cs ? (long)CFGetRetainCount(cs) : 0;
}

/*
 * FIX133: a command buffer and a render encoder that count what is asked of
 * them (presentDrawable:, setRenderPipelineState:), and the pipeline they
 * are given; no GPU work
 */
@interface probe_mock : NSObject
@property (nonatomic) int presents, binds;
@property (nonatomic, strong) id bound;
@end

@implementation probe_mock
- (void)presentDrawable:(id)drawable
{
	(void)drawable;
	self.presents++;
}
- (void)setRenderPipelineState:(id)ps
{
	self.bound = ps;
	self.binds++;
}
@end

/*
 * FIX133: with the float16 path on, a pipeline whose RGBA16Float variant is
 * not built (fmt_refuse) is never handed back or bound (rmb_fmt_pipe nil,
 * rmb_fmt_bind 0 and no bind), that frame presents nothing, and HDR is off
 * at the next rmb_hdr_set until the next renderer start (rmb_resources_close,
 * then rmb_attach), which tries it again; an attach alone keeps it off. ps stands
 * for a stock pipeline: an object with the descriptor rmb_new_render_pipe
 * would have kept for it.
 */
static void
hdr_fail_case(CAMetalLayer *layer, int mode)
{
	char err[128];
	probe_mock *ps_obj = [probe_mock new], *enc = [probe_mock new], *cmd = [probe_mock new];
	id<MTLRenderPipelineState> ps = (id<MTLRenderPipelineState>)(id)ps_obj;
	id<MTLRenderCommandEncoder> e = (id<MTLRenderCommandEncoder>)(id)enc;
	id<MTLCommandBuffer> c = (id<MTLCommandBuffer>)(id)cmd;
	id<CAMetalDrawable> drawable = (id<CAMetalDrawable>)(id)[probe_mock new];
	MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];

	fprintf(stderr, "rmb_cs_probe: a float16 variant that is not built, metal_colorspace %d\n", mode);
	d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
	rmb_colorspace_set(mode);
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	CHECK(!rmb_hdr_failed());

	if (!fmt_descs)
	{
		fmt_descs = [NSMapTable weakToStrongObjectsMapTable];
	}

	[fmt_descs setObject:d forKey:ps_obj];
	CHECK(rmb_fmt_pipe(ps) == ps);                 /* the stock path: ps itself */
	CHECK(rmb_fmt_bind(e, ps) && (enc.bound == ps_obj));
	CHECK(rmb_present_written(c, drawable, rmb_present_region(1, 1, 0, 0)) && (cmd.presents == 1));

	rmb_hdr_set(1);
	CHECK(rmb_hdr_on());
	fmt_refuse = 1;
	enc.binds = 0;
	CHECK(rmb_fmt_pipe(ps) == nil);                /* gap 1: never the BGRA8Unorm ps */
	CHECK(rmb_hdr_failed() && rmb_hdr_on());       /* off from the next rmb_hdr_set, between frames */
	CHECK(!rmb_fmt_bind(e, ps) && (enc.binds == 0));   /* asked again in that frame: nothing bound */
	fmt_refuse = 0;

	/* gap 2: that frame and a float16 frame without its present pipeline present nothing */
	CHECK(rmb_present_region(1, 1, 1, 1) == 0);
	CHECK(!rmb_present_written(c, drawable, rmb_present_region(1, 1, 1, 1)) && (cmd.presents == 1));
	rmb_hdr_dead = 0;
	CHECK(rmb_present_region(1, 1, 1, 0) == 0);
	CHECK(!rmb_present_written(c, drawable, rmb_present_region(1, 1, 1, 0)) && (cmd.presents == 1));
	CHECK(rmb_present_region(1, 0, 0, 1) == 0);    /* the other format */
	CHECK(rmb_present_region(1, 1, 1, 1) == 1);
	rmb_hdr_fail();

	rmb_hdr_set(1);
	CHECK(!rmb_hdr_on());
	CHECK(!mode || is_srgb(layer));
	rmb_hdr_set(1);
	CHECK(!rmb_hdr_on());                          /* off until the next renderer start */

	/*
	 * gap 3: a detach and an attach without rmb_resources_close keep the failure with the caches
	 * that remember it (ps's NSNull): r_hdr 1 stays off, and no draw meets a nil pipeline while on
	 */
	rmb_detach();
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	CHECK(rmb_hdr_failed());
	rmb_hdr_set(1);
	CHECK(!rmb_hdr_on());
	CHECK((rmb_fmt_pipe(ps) != nil) || !rmb_hdr_on());
	CHECK(layer.pixelFormat == MTLPixelFormatBGRA8Unorm);

	/* the close and the next open (vid_restart): the flag and the caches reset together, the path is tried again */
	rmb_detach();
	rmb_resources_close();
	CHECK(!rmb_hdr_failed() && ([fmt_variants objectForKey:ps_obj] == nil));
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	rmb_hdr_set(1);
	CHECK(rmb_hdr_on());
	CHECK(layer.pixelFormat == MTLPixelFormatRGBA16Float);
	fmt_descs = [NSMapTable weakToStrongObjectsMapTable];
	[fmt_descs setObject:d forKey:ps_obj];
	fmt_refuse = 1;
	CHECK((rmb_fmt_pipe(ps) == nil) && rmb_hdr_failed());   /* built again, not the cached failure */
	fmt_refuse = 0;
	rmb_hdr_set(0);
	rmb_detach();
	rmb_resources_close();
	rmb_colorspace_set(0);
}

/* HD4: a texture of the view, the depth or the motion, or the frame */
static id<MTLTexture>
fx_tex(MTLPixelFormat f, int w, int h, MTLStorageMode storage)
{
	MTLTextureDescriptor *t = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:f width:(NSUInteger)w
			height:(NSUInteger)h mipmapped:NO];

	t.storageMode = storage;
	t.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite | MTLTextureUsageRenderTarget;
	return [rmb_device newTextureWithDescriptor:t];
}

/* HD4: the frame's command buffer, run to its end; 1 when the GPU completed it */
static int
fx_commit(void)
{
	id<MTLCommandBuffer> c = probe_cmd;

	probe_cmd = nil;

	if (!c)
	{
		return 0;
	}

	[c commit];
	[c waitUntilCompleted];
	return c.status == MTLCommandBufferStatusCompleted;
}

/* HD4: what the back end has to report, and the mode it names (rmb_fx_note); the note is taken */
static int
fx_noted(int *mode)
{
	rm_fx_size_t ns;

	return rmb_fx_note(mode, &ns);
}

/*
 * TASK196 HD4: the MetalFX scalers follow the frame's format. At r_hdr 0
 * the spatial and the temporal scaler and their output are BGRA8Unorm,
 * the spatial one perceptual; at r_hdr 1 they are RGBA16Float and the
 * spatial one is hdr, and each runs on the GPU. A scaler of the other
 * format is never bound (its output is not the frame's): a toggle of
 * r_hdr remakes it. A scaler the GPU will not make (fx_refuse) leaves
 * MetalFX off with r_hdr's note at r_hdr 1 (the stock note at 0), told
 * once, nothing kept and nothing encoded.
 *
 * Two halves, each with its own line, run or skipped: the spatial half
 * (with the refusal case of the spatial scaler) runs on any GPU with the
 * spatial scaler, the temporal half only when it has the temporal one too
 * (the build VM's paravirtual GPU has the spatial scaler alone).
 * check-ref-metal.sh runs each fx mutant only when the half it needs ran, skips it
 * only on a skipped line, and fails a GPU whose "scalers:" line says both are there.
 */
static void
fx_case(CAMetalLayer *layer, int mode)
{
	char err[128];
	const rm_fx_size_t s = { 320, 240, 640, 480, 0, 0 };
	id<MTLTexture> c8, f8, c16, f16, depth, motion;
	int noted_mode = -1, made, computes;
	const int has_s = (rmb_fx_support() & RM_FX_HAS(RM_FX_SPATIAL)) != 0;
	const int has_t = has_s && ((rmb_fx_support() & RM_FX_HAS(RM_FX_TEMPORAL)) != 0);

	fprintf(stderr, "rmb_cs_probe: scalers: spatial=%d temporal=%d\n", has_s, has_t);

	if (!has_s)
	{
		fprintf(stderr, "rmb_cs_probe: the MetalFX spatial half skipped: no spatial scaler on this GPU\n");
		fprintf(stderr, "rmb_cs_probe: the MetalFX temporal half skipped: no spatial scaler on this GPU\n");
		return;
	}

	fprintf(stderr, "rmb_cs_probe: the MetalFX spatial half under r_hdr 0 and 1, metal_colorspace %d: run\n", mode);
	if (has_t)
	{
		fprintf(stderr, "rmb_cs_probe: the MetalFX temporal half under r_hdr 0 and 1, metal_colorspace %d: run\n", mode);
	}
	else
	{
		fprintf(stderr, "rmb_cs_probe: the MetalFX temporal half skipped: no temporal scaler on this GPU\n");
	}

	rmb_colorspace_set(mode);
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	c8 = fx_tex(MTLPixelFormatBGRA8Unorm, s.iw, s.ih, MTLStorageModePrivate);
	f8 = fx_tex(MTLPixelFormatBGRA8Unorm, s.ow, s.oh, MTLStorageModePrivate);
	c16 = fx_tex(MTLPixelFormatRGBA16Float, s.iw, s.ih, MTLStorageModePrivate);
	f16 = fx_tex(MTLPixelFormatRGBA16Float, s.ow, s.oh, MTLStorageModePrivate);
	depth = fx_tex(MTLPixelFormatDepth32Float, s.iw, s.ih, MTLStorageModePrivate);
	motion = fx_tex(MTLPixelFormatRG16Float, s.iw, s.ih, MTLStorageModePrivate);
	CHECK(c8 && f8 && c16 && f16 && depth && motion);
	(void)fx_noted(&noted_mode);

	/* r_hdr 0: the stock formats, as before HD4 */
	CHECK(!rmb_hdr_on());
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 1));
	CHECK(fx_spatial && (fx_spatial.colorTextureFormat == MTLPixelFormatBGRA8Unorm) &&
		(fx_spatial.outputTextureFormat == MTLPixelFormatBGRA8Unorm));
	CHECK(fx_spatial.colorProcessingMode == MTLFXSpatialScalerColorProcessingModePerceptual);
	CHECK(fx_out.pixelFormat == MTLPixelFormatBGRA8Unorm);
	CHECK(rmb_fx_upscale(c8, depth, motion, f8) && fx_commit());
	if (has_t)
	{
		CHECK(rmb_fx_prepare(RM_FX_TEMPORAL, &s, 0.25f, -0.25f, 1));
		CHECK(fx_temporal && (fx_temporal.colorTextureFormat == MTLPixelFormatBGRA8Unorm) &&
			(fx_temporal.outputTextureFormat == MTLPixelFormatBGRA8Unorm));
	}
	else
	{
		CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 0));   /* a BGRA8 scaler prepared for the toggle below */
	}
	CHECK(fx_out.pixelFormat == MTLPixelFormatBGRA8Unorm);

	/* r_hdr 1 with the BGRA8 scaler still prepared: the float16 view and frame are not bound to it */
	rmb_hdr_set(1);
	CHECK(rmb_hdr_on());
	computes = probe_computes;
	CHECK(!rmb_fx_upscale(c16, depth, motion, f16) && (probe_computes == computes));
	probe_cmd = nil;

	/* r_hdr 1: float16 in and out, the spatial scaler's hdr mode */
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 1));
	CHECK(fx_spatial && (fx_spatial.colorTextureFormat == MTLPixelFormatRGBA16Float) &&
		(fx_spatial.outputTextureFormat == MTLPixelFormatRGBA16Float));
	CHECK(fx_spatial.colorProcessingMode == MTLFXSpatialScalerColorProcessingModeHDR);
	CHECK(fx_out.pixelFormat == MTLPixelFormatRGBA16Float);
	computes = probe_computes;
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 0));
	CHECK(!rmb_fx_upscale(c16, depth, motion, f8) && (probe_computes == computes));   /* an 8-bit frame */
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 0));
	CHECK(!rmb_fx_upscale(c8, depth, motion, f16) && (probe_computes == computes));   /* an 8-bit view */
	CHECK(fx_noted(&noted_mode) == RM_FX_NOTE_UNFIT);
	CHECK(rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0) == 0);   /* off: the refusal of the unfit view forgotten */
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 1));
	CHECK(rmb_fx_upscale(c16, depth, motion, f16) && fx_commit());

	made = 0;
	if (has_t)
	{
		made = rmb_fx_prepare(RM_FX_TEMPORAL, &s, 0.25f, -0.25f, 1);
		fprintf(stderr, "rmb_cs_probe: the temporal scaler in RGBA16Float colour and output: %s\n",
			made ? "made" : "refused (nil)");
		if (!made)
		{
			fprintf(stderr, "rmb_cs_probe: the temporal scaler in RGBA16Float skipped: refused in float16 on this GPU\n");
		}
	}

	if (has_t && made)
	{
		CHECK(fx_temporal && (fx_temporal.colorTextureFormat == MTLPixelFormatRGBA16Float) &&
			(fx_temporal.outputTextureFormat == MTLPixelFormatRGBA16Float));
		CHECK(fx_out.pixelFormat == MTLPixelFormatRGBA16Float);
		CHECK(rmb_fx_upscale(c16, depth, motion, f16) && fx_commit());
	}
	else if (has_t)
	{
		CHECK(!fx_temporal && !fx_out && (fx_noted(&noted_mode) == RM_FX_NOTE_HDR));
	}

	/* a scaler the GPU will not make in float16: off, with r_hdr's note, once; nothing bound (spatial: the spatial half) */
	for (int m = RM_FX_SPATIAL; m <= (has_t ? RM_FX_TEMPORAL : RM_FX_SPATIAL); m++)
	{
		CHECK(rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0) == 0);
		(void)fx_noted(&noted_mode);
		fx_refuse = 1;
		computes = probe_computes;
		CHECK(!rmb_fx_prepare(m, &s, 0, 0, 1));
		CHECK(!fx_spatial && !fx_temporal && !fx_out && !rmb_fx_ready());
		CHECK((fx_noted(&noted_mode) == RM_FX_NOTE_HDR) && (noted_mode == m));
		CHECK(!rmb_fx_upscale(c16, depth, motion, f16) && (probe_computes == computes));
		CHECK(!rmb_fx_prepare(m, &s, 0, 0, 0));
		CHECK(fx_noted(&noted_mode) == RM_FX_NOTE_NONE);   /* told once */
		fx_refuse = 0;
		CHECK(!rmb_fx_prepare(m, &s, 0, 0, 0));           /* refused at this mode, size and format */

		/* the toggle to r_hdr 0 tries it again, in 8 bits; refused there, the stock note */
		rmb_hdr_set(0);
		CHECK(!rmb_hdr_on());
		fx_refuse = 1;
		CHECK(!rmb_fx_prepare(m, &s, 0, 0, 1));
		CHECK((fx_noted(&noted_mode) == RM_FX_NOTE_UNMADE) && (noted_mode == m));
		fx_refuse = 0;
		rmb_hdr_set(1);
		CHECK(rmb_hdr_on());
	}

	/* the toggle back to r_hdr 0 remakes the spatial scaler in 8 bits; no float16 view or frame is bound to it */
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 1));
	CHECK(fx_out.pixelFormat == MTLPixelFormatRGBA16Float);
	rmb_hdr_set(0);
	CHECK(!rmb_hdr_on());
	computes = probe_computes;
	CHECK(!rmb_fx_upscale(c16, depth, motion, f16) && (probe_computes == computes));
	CHECK(!rmb_fx_ready());
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 0) && (fx_spatial.outputTextureFormat == MTLPixelFormatBGRA8Unorm));
	rmb_hdr_set(1);
	CHECK(!rmb_fx_upscale(c8, depth, motion, f8) && (probe_computes == computes));   /* nor the other way round */
	rmb_hdr_set(0);
	CHECK(rmb_fx_prepare(RM_FX_SPATIAL, &s, 0, 0, 1));
	CHECK(fx_spatial && (fx_spatial.colorTextureFormat == MTLPixelFormatBGRA8Unorm) &&
		(fx_spatial.colorProcessingMode == MTLFXSpatialScalerColorProcessingModePerceptual));
	CHECK(rmb_fx_upscale(c8, depth, motion, f8) && fx_commit());

	CHECK(rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0) == 0);
	CHECK(!fx_spatial && !fx_temporal && !fx_out);
	rmb_fx_release();
	rmb_detach();
	rmb_resources_close();
	rmb_colorspace_set(0);
}

/*
 * TASK196 HD4's measurements, run only with --measure (docs/runbook/TASK196.md, HD4): what the
 * MetalFX scalers of this GPU cost in memory in each format, whether the denoiser takes
 * RGBA16Float, and how stable the temporal scaler is on a still view at r_hdr 0 and 1. They print
 * lines and check only that each frame ran; tests/check-ref-metal.sh does not run them.
 */
#define FX_MW 960
#define FX_MH 540
#define FX_MOW 1920
#define FX_MOH 1080
#define FX_FRAMES 60
#define FX_SETTLE 10

/* a still view: a smooth gradient, a checker of hard edges and a grid of small cores far above white */
static void
fx_scene(double u, double v, float *rgb)
{
	double c = 0.2 + 0.15 * sin(u * 9.0) * cos(v * 7.0);
	const double du = u * 32.0 - floor(u * 32.0) - 0.5, dv = v * 18.0 - floor(v * 18.0) - 0.5;

	if ((((int)floor(u * 24.0)) + ((int)floor(v * 13.5))) & 1)
	{
		c += 0.45;
	}

	if ((du * du + dv * dv) < 0.0036)
	{
		c += 7.0;   /* about 3 output pixels across: an additive core */
	}

	rgb[0] = (float)c;
	rgb[1] = (float)(c * 0.8);
	rgb[2] = (float)(c * 0.6);
}

static float
fx_unit(float v)
{
	return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}

/* the view sampled at the jitter: BGRA8Unorm clamps at white, RGBA16Float keeps what is above */
static void
fx_fill(id<MTLTexture> t, int hdr, float jx, float jy, int sgn, void *px)
{
	float rgb[3];

	for (int y = 0; y < FX_MH; y++)
	{
		for (int x = 0; x < FX_MW; x++)
		{
			fx_scene((x + 0.5 + sgn * jx) / FX_MW, (y + 0.5 + sgn * jy) / FX_MH, rgb);

			if (hdr)
			{
				__fp16 *p = (__fp16 *)px + ((size_t)y * FX_MW + x) * 4;

				p[0] = (__fp16)rgb[0];
				p[1] = (__fp16)rgb[1];
				p[2] = (__fp16)rgb[2];
				p[3] = (__fp16)1.0f;
			}
			else
			{
				unsigned char *p = (unsigned char *)px + ((size_t)y * FX_MW + x) * 4;

				p[0] = (unsigned char)lrintf(fx_unit(rgb[2]) * 255.0f);
				p[1] = (unsigned char)lrintf(fx_unit(rgb[1]) * 255.0f);
				p[2] = (unsigned char)lrintf(fx_unit(rgb[0]) * 255.0f);
				p[3] = 255;
			}
		}
	}

	[t replaceRegion:MTLRegionMake2D(0, 0, FX_MW, FX_MH) mipmapLevel:0 withBytes:px
		bytesPerRow:(NSUInteger)FX_MW * (hdr ? 8 : 4)];
}

/* the frame read back as display values: each channel clamped to [0, 1], as r_hdr_peak 1 shows it */
static void
fx_read(id<MTLTexture> f, int hdr, void *px, float *out)
{
	[f getBytes:px bytesPerRow:(NSUInteger)FX_MOW * (hdr ? 8 : 4)
		fromRegion:MTLRegionMake2D(0, 0, FX_MOW, FX_MOH) mipmapLevel:0];

	for (size_t i = 0; i < (size_t)FX_MOW * FX_MOH; i++)
	{
		if (hdr)
		{
			const __fp16 *p = (const __fp16 *)px + i * 4;

			out[i * 3] = fx_unit((float)p[0]);
			out[i * 3 + 1] = fx_unit((float)p[1]);
			out[i * 3 + 2] = fx_unit((float)p[2]);
		}
		else
		{
			const unsigned char *p = (const unsigned char *)px + i * 4;

			out[i * 3] = p[2] / 255.0f;
			out[i * 3 + 1] = p[1] / 255.0f;
			out[i * 3 + 2] = p[0] / 255.0f;
		}
	}
}

/* the depth and motion of a still view: depth 0.5, no motion */
static void
fx_still(id<MTLTexture> depth, id<MTLTexture> motion)
{
	MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
	id<MTLCommandBuffer> c = [rmb_queue commandBuffer];
	id<MTLRenderCommandEncoder> e;

	rp.colorAttachments[0].texture = motion;
	rp.colorAttachments[0].loadAction = MTLLoadActionClear;
	rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
	rp.colorAttachments[0].storeAction = MTLStoreActionStore;
	rp.depthAttachment.texture = depth;
	rp.depthAttachment.loadAction = MTLLoadActionClear;
	rp.depthAttachment.clearDepth = 0.5;
	rp.depthAttachment.storeAction = MTLStoreActionStore;
	e = [c renderCommandEncoderWithDescriptor:rp];
	[e endEncoding];
	[c commit];
	[c waitUntilCompleted];
}

/* the frame-to-frame change of the temporal scaler's output on a still view, after FX_SETTLE frames */
static double
fx_stability(int hdr, int sgn, int print)
{
	const rm_fx_size_t s = { FX_MW, FX_MH, FX_MOW, FX_MOH, 0, 0 };
	const MTLPixelFormat f = hdr ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
	id<MTLTexture> color = fx_tex(f, FX_MW, FX_MH, MTLStorageModeShared);
	id<MTLTexture> frame = fx_tex(f, FX_MOW, FX_MOH, MTLStorageModeShared);
	id<MTLTexture> depth = fx_tex(MTLPixelFormatDepth32Float, FX_MW, FX_MH, MTLStorageModePrivate);
	id<MTLTexture> motion = fx_tex(MTLPixelFormatRG16Float, FX_MW, FX_MH, MTLStorageModePrivate);
	void *in = malloc((size_t)FX_MW * FX_MH * 8), *raw = malloc((size_t)FX_MOW * FX_MOH * 8);
	float *prev = malloc((size_t)FX_MOW * FX_MOH * 3 * sizeof(float));
	float *cur = malloc((size_t)FX_MOW * FX_MOH * 3 * sizeof(float));
	double sum = 0.0, maxd = 0.0;
	unsigned long n_px = 0, flicker = 0;
	int ran = 0;

	rmb_hdr_set(hdr);
	CHECK(rmb_hdr_on() == hdr);
	fx_still(depth, motion);

	for (int n = 0; n < FX_FRAMES; n++)
	{
		float jx, jy, *t;

		rm_fx_jitter(RM_FX_TEMPORAL, (unsigned)n, &jx, &jy);
		fx_fill(color, hdr, jx, jy, sgn, in);

		if (!rmb_fx_prepare(RM_FX_TEMPORAL, &s, jx, jy, n == 0) || !rmb_fx_upscale(color, depth, motion, frame) ||
			!fx_commit())
		{
			break;
		}

		ran++;
		fx_read(frame, hdr, raw, cur);

		if (n >= FX_SETTLE)
		{
			for (size_t i = 0; i < (size_t)FX_MOW * FX_MOH; i++)
			{
				double pm = 0.0;

				for (int k = 0; k < 3; k++)
				{
					const double d = fabs((double)cur[i * 3 + k] - (double)prev[i * 3 + k]);

					sum += d;
					pm = (d > pm) ? d : pm;
				}

				maxd = (pm > maxd) ? pm : maxd;
				flicker += pm > 0.1;
				n_px++;
			}
		}

		t = prev;
		prev = cur;
		cur = t;
	}

	CHECK(ran == FX_FRAMES);

	if (print)
	{
		printf("rmb_cs_probe: measure: temporal %dx%d -> %dx%d, %s, %d frames, jitter sign %+d: the change of a "
			"pixel from frame to frame over frames %d to %d, in display units: mean %.6f, max %.4f, "
			"pixels changing by more than 0.1: %lu of %lu\n", FX_MW, FX_MH, FX_MOW, FX_MOH,
			hdr ? "r_hdr 1 (RGBA16Float)" : "r_hdr 0 (BGRA8Unorm)", ran, sgn, FX_SETTLE, FX_FRAMES - 1,
			n_px ? sum / (3.0 * n_px) : -1.0, maxd, flicker, n_px);
	}

	rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0);
	free(in);
	free(raw);
	free(prev);
	free(cur);
	return n_px ? sum / (3.0 * n_px) : 1e9;
}

/* what a scaler and its output target take on the device, from its currentAllocatedSize */
static void
fx_memory(int hdr)
{
	const rm_fx_size_t s = { FX_MW, FX_MH, FX_MOW, FX_MOH, 0, 0 };

	rmb_hdr_set(hdr);
	CHECK(rmb_hdr_on() == hdr);

	for (int m = RM_FX_SPATIAL; m <= RM_FX_TEMPORAL; m++)
	{
		NSUInteger before, after;

		rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0);
		before = rmb_device.currentAllocatedSize;
		CHECK(rmb_fx_prepare(m, &s, 0, 0, 1));
		after = rmb_device.currentAllocatedSize;
		printf("rmb_cs_probe: measure: %s scaler %dx%d -> %dx%d in %s with its output target: %.1f MB on "
			"the device (the target alone %.1f MB)\n", rm_fx_name(m), FX_MW, FX_MH, FX_MOW, FX_MOH,
			hdr ? "RGBA16Float" : "BGRA8Unorm", ((double)after - (double)before) / 1e6,
			(double)FX_MOW * FX_MOH * (hdr ? 8 : 4) / 1e6);
	}

	rmb_fx_prepare(RM_FX_OFF, NULL, 0, 0, 0);
}

/* the denoiser (not run by the renderer before TASK59): made in each colour and output format? */
static void
fx_denoiser(void)
{
	if (@available(macOS 26.0, *))
	{
		for (int hdr = 0; hdr < 2; hdr++)
		{
			const MTLPixelFormat f = hdr ? MTLPixelFormatRGBA16Float : MTLPixelFormatBGRA8Unorm;
			MTLFXTemporalDenoisedScalerDescriptor *d = [MTLFXTemporalDenoisedScalerDescriptor new];
			id<MTLFXTemporalDenoisedScaler> dn = nil;
			NSUInteger before = rmb_device.currentAllocatedSize, after;
			const char *why = "";

			d.colorTextureFormat = f;
			d.outputTextureFormat = f;
			d.depthTextureFormat = MTLPixelFormatDepth32Float;
			d.motionTextureFormat = MTLPixelFormatRG16Float;
			d.diffuseAlbedoTextureFormat = MTLPixelFormatRGBA8Unorm;
			d.specularAlbedoTextureFormat = MTLPixelFormatRGBA8Unorm;
			d.normalTextureFormat = MTLPixelFormatRGBA16Float;
			d.roughnessTextureFormat = MTLPixelFormatR16Float;
			d.inputWidth = FX_MW;
			d.inputHeight = FX_MH;
			d.outputWidth = FX_MOW;
			d.outputHeight = FX_MOH;
			d.autoExposureEnabled = NO;

			@try
			{
				dn = [d newTemporalDenoisedScalerWithDevice:rmb_device];
			}
			@catch (NSException *x)
			{
				why = x.reason ? x.reason.UTF8String : "an exception";
			}

			after = rmb_device.currentAllocatedSize;
			printf("rmb_cs_probe: measure: denoised scaler %dx%d -> %dx%d, colour and output %s: %s%s%s "
				"(%.1f MB on the device)\n", FX_MW, FX_MH, FX_MOW, FX_MOH, hdr ? "RGBA16Float" : "BGRA8Unorm",
				dn ? "made" : "nil", *why ? ": " : "", why, ((double)after - (double)before) / 1e6);
			dn = nil;
		}
	}
	else
	{
		printf("rmb_cs_probe: measure: no denoised scaler before macOS 26\n");
	}
}

static void
fx_measure(void)
{
	char err[128];
	CAMetalLayer *layer = [CAMetalLayer layer];
	double plus, minus;
	int sgn;

	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	fx_memory(0);
	fx_memory(1);
	fx_denoiser();

	/* the sign of the jitter that matches this scaler's convention is the steadier one at r_hdr 0 */
	plus = fx_stability(0, 1, 1);
	minus = fx_stability(0, -1, 1);
	sgn = (plus <= minus) ? 1 : -1;
	fx_stability(1, sgn, 1);
	rmb_hdr_set(0);
	rmb_fx_release();
	rmb_detach();
	rmb_resources_close();
}

/* one layer: <initial> is the colour space the layer holds at attach (NULL: as created);
   <counted>: initial is the probe's own, and its retain count is checked around each cycle */
static void
run(const char *name, CGColorSpaceRef initial, int counted)
{
	char err[128];
	CAMetalLayer *layer = [CAMetalLayer layer];
	CGColorSpaceRef at_attach;
	long base = 0;

	if (initial)
	{
		layer.colorspace = initial;
	}

	fprintf(stderr, "rmb_cs_probe: %s\n", name);

	rmb_colorspace_set(0);
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	at_attach = CGColorSpaceRetain(layer.colorspace);   /* the layer as attach left it, mode 0 */
	CHECK(!initial || same_as(layer, initial));        /* attach left a tagged layer alone */
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 0);
	base = count_of(initial);

	rmb_colorspace_set(1);
	CHECK(is_srgb(layer));
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 1);

	rmb_colorspace_set(1);              /* asked again: nothing new is made */
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 1);

	rmb_colorspace_set(0);              /* 1 -> 0: the layer's own colour space is back */
	CHECK(same_as(layer, at_attach));
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 0);
	CHECK_COUNT(!counted || count_of(initial) == base);   /* set and unset: no reference gained or lost */

	rmb_colorspace_set(1);              /* 0 -> 1 -> detach: the layer is left as found */
	CHECK(is_srgb(layer));
	rmb_detach();
	CHECK(same_as(layer, at_attach));
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 0);
	CHECK_COUNT(!counted || count_of(initial) == base);   /* set and detach */

	rmb_colorspace_set(1);              /* mode 1 before the attach: attach applies it */
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	CHECK(is_srgb(layer));
	rmb_colorspace_set(0);
	CHECK(same_as(layer, at_attach));
	CHECK_COUNT(!counted || count_of(initial) == base);   /* attach with it on, then off */
	rmb_detach();
	CHECK(same_as(layer, at_attach));
	CHECK_COUNT(!counted || count_of(initial) == base);   /* and detach */
	rmb_colorspace_set(0);

	/* r_hdr (TASK196) 0 -> 1 -> 0 with metal_colorspace 0 and 1: the colour space is back as it was */
	for (int mode = 0; mode < 2; mode++)
	{
		rmb_colorspace_set(mode);
		CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
		CHECK(!rmb_hdr_on());
		rmb_hdr_set(1);
		CHECK(rmb_hdr_on());
		CHECK(layer.pixelFormat == MTLPixelFormatRGBA16Float);
		rmb_hdr_set(0);
		CHECK(!rmb_hdr_on());
		CHECK(layer.pixelFormat == MTLPixelFormatBGRA8Unorm);
		CHECK(mode ? is_srgb(layer) : same_as(layer, at_attach));
		/* the failure path (rmb_hdr_fail) switches off at the next rmb_hdr_set, as a live r_hdr 0 does, and stays off */
		rmb_hdr_set(1);
		rmb_hdr_fail();
		CHECK(rmb_hdr_failed() && rmb_hdr_on());
		rmb_hdr_set(1);
		CHECK(!rmb_hdr_on());
		CHECK(mode ? is_srgb(layer) : same_as(layer, at_attach));
		rmb_hdr_set(1);
		CHECK(!rmb_hdr_on());

		rmb_detach();
		CHECK(same_as(layer, at_attach));
		rmb_colorspace_set(0);

		/* FIX133: the failure outlasts an attach; it is off until the next renderer start (rmb_resources_close) */
		CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
		CHECK(rmb_hdr_failed());
		rmb_hdr_set(1);
		CHECK(!rmb_hdr_on());
		rmb_detach();
		rmb_resources_close();
		CHECK(!rmb_hdr_failed());
		CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
		rmb_hdr_set(1);
		CHECK(rmb_hdr_on());
		rmb_hdr_set(0);
		rmb_detach();
		CHECK(same_as(layer, at_attach));

		hdr_fail_case(layer, mode);
		CHECK(same_as(layer, at_attach));
		fx_case(layer, mode);
		CHECK(same_as(layer, at_attach));
	}

	CGColorSpaceRelease(at_attach);
}

int
main(int argc, char **argv)
{
	@autoreleasepool
	{
		CGColorSpaceRef p3, own;
		CGFloat white[3] = { 0.9505f, 1.0f, 1.089f }, gamma[3] = { 2.2f, 2.2f, 2.2f };
		CGFloat matrix[9] = { 0.4124f, 0.2126f, 0.0193f, 0.3576f, 0.7152f, 0.1192f, 0.1805f, 0.0722f, 0.9505f };

		rmb_device = MTLCreateSystemDefaultDevice();
		if (!rmb_device)
		{
			printf("rmb_cs_probe: skipped: no Metal device\n");
			return 0;
		}
		rmb_queue = [rmb_device newCommandQueue];

		if ((argc > 1) && !strcmp(argv[1], "--measure"))
		{
			fx_measure();
			printf("rmb_cs_probe: measure: %d checks, %d failed\n", checks, failures);
			return failures != 0;
		}

		run("layer with its default colour space", NULL, 0);

		p3 = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
		run("layer tagged Display P3", p3, 0);
		CGColorSpaceRelease(p3);

		own = CGColorSpaceCreateCalibratedRGB(white, NULL, gamma, matrix);
		CHECK(own != NULL);
		run("layer tagged with a colour space of the probe's own, retain counts checked", own, 1);
		CGColorSpaceRelease(own);

		probe_cmd = nil;
		rmb_queue = nil;
		rmb_device = nil;
	}

	if (failures)
	{
		printf("rmb_cs_probe: %d checks, %d failed\n", checks, failures);
		return 1;
	}

	printf("rmb_cs_probe: %d checks, 0 failed\n", checks);
	return 0;
}
