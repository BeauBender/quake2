/*
 * The metal_colorspace probe (TASK164). rmb_cs_apply and rmb_cs_release
 * (src/client/refresh/metal/rmb_device.m) put the sRGB colour space on the
 * layer and take it off again; the layer must be left as it was found. No
 * host test covers rmb_*.m, and the 1 -> 0 bug of TASK121 lived here.
 *
 * The renderer's own code is under test, not a copy: this file includes
 * rmb_device.m whole (its two functions are static) and defines the three
 * symbols that file calls elsewhere (rmb_encode_abandon, rmb_encode_release,
 * rmb_resources_close) as empty stubs; rm_pure.c supplies the ledger (and
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

void rmb_encode_abandon(void) {}
void rmb_encode_release(void) {}
void rmb_resources_close(void) {}
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
