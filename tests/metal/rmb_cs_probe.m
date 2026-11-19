/*
 * The metal_colorspace probe (TASK164). rmb_cs_apply and rmb_cs_release
 * (src/client/refresh/metal/rmb_device.m) put the sRGB colour space on the
 * layer and take it off again; the layer must be left as it was found. No
 * host test covers rmb_*.m, and the 1 -> 0 bug of TASK121 lived here.
 *
 * The renderer's own code is under test, not a copy: this file includes
 * rmb_device.m whole (its two functions are static) and rmb_resource.m
 * (FIX133: rmb_fmt_pipe and rmb_fmt_bind, with its test seam fmt_refuse),
 * and defines the symbols those files call elsewhere (rmb_encode_abandon,
 * rmb_encode_release, rmb_encode_split, rmb_frame_serial) as empty stubs;
 * rm_pure.c supplies the ledger (and
 * is linked with what the other host tests link it with). A
 * standalone CAMetalLayer is set up as rmb_attach does (rmb_attach itself
 * runs on it), no window and no game data. With no Metal device the probe
 * prints one skip line and passes.
 *
 * Built and run by tests/check-ref-metal.sh on any Darwin (with the
 * script's sanitizers when the compiler has them; with no Metal device it
 * skips itself): clang -fobjc-arc rmb_cs_probe.m rm_pure.c -framework Metal ...
 */

#include <unistd.h>

#include "rmb_device.m"
#include "rmb_resource.m"

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
 * at the next rmb_hdr_set until rmb_attach, which tries it again. ps stands
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
	CHECK(!rmb_hdr_on());                          /* off until the next attach */

	/* gap 3: a detach and an attach (vid_restart) try the float16 path again */
	rmb_detach();
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	CHECK(!rmb_hdr_failed());
	rmb_hdr_set(1);
	CHECK(rmb_hdr_on());
	CHECK(layer.pixelFormat == MTLPixelFormatRGBA16Float);
	rmb_hdr_set(0);
	rmb_detach();
	rmb_colorspace_set(0);
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

		/* FIX133: the failure is off until the next renderer start, which tries the float16 path again */
		CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
		CHECK(!rmb_hdr_failed());
		rmb_hdr_set(1);
		CHECK(rmb_hdr_on());
		rmb_hdr_set(0);
		rmb_detach();
		CHECK(same_as(layer, at_attach));

		hdr_fail_case(layer, mode);
		CHECK(same_as(layer, at_attach));
	}

	CGColorSpaceRelease(at_attach);
}

int
main(void)
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

		run("layer with its default colour space", NULL, 0);

		p3 = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
		run("layer tagged Display P3", p3, 0);
		CGColorSpaceRelease(p3);

		own = CGColorSpaceCreateCalibratedRGB(white, NULL, gamma, matrix);
		CHECK(own != NULL);
		run("layer tagged with a colour space of the probe's own, retain counts checked", own, 1);
		CGColorSpaceRelease(own);

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
