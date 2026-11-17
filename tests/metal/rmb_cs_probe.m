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
 * Built and run by tests/check-ref-metal.sh on Darwin with the Metal
 * compiler: clang -fobjc-arc rmb_cs_probe.m rm_pure.c -framework Metal ...
 */

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

/* the layer's colour space is the one at attach: the same object, or none in both */
static int
same_as(CAMetalLayer *layer, CGColorSpaceRef want)
{
	CGColorSpaceRef have = layer.colorspace;

	return (have == want) || (have && want && CFEqual(have, want));
}

static int
is_srgb(CAMetalLayer *layer)
{
	CGColorSpaceRef have = layer.colorspace;
	CGColorSpaceRef srgb = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
	int eq = have && srgb && CFEqual(have, srgb);

	CGColorSpaceRelease(srgb);
	return eq;
}

/* one layer: <initial> is the colour space the layer holds at attach (NULL: as created) */
static void
run(const char *name, CGColorSpaceRef initial)
{
	char err[128];
	CAMetalLayer *layer = [CAMetalLayer layer];
	CGColorSpaceRef at_attach;

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

	rmb_colorspace_set(1);
	CHECK(is_srgb(layer));
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 1);

	rmb_colorspace_set(1);              /* asked again: nothing new is made */
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 1);

	rmb_colorspace_set(0);              /* 1 -> 0: the layer's own colour space is back */
	CHECK(same_as(layer, at_attach));
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 0);

	rmb_colorspace_set(1);              /* 0 -> 1 -> detach: the layer is left as found */
	CHECK(is_srgb(layer));
	rmb_detach();
	CHECK(same_as(layer, at_attach));
	CHECK(rm_ledger_kind_live(&rmb_ledger, RM_OBJ_STATE) == 0);

	rmb_colorspace_set(1);              /* mode 1 before the attach: attach applies it */
	CHECK(rmb_attach((__bridge void *)layer, 0, err, sizeof(err)));
	CHECK(is_srgb(layer));
	rmb_colorspace_set(0);
	CHECK(same_as(layer, at_attach));
	rmb_detach();
	CHECK(same_as(layer, at_attach));
	rmb_colorspace_set(0);
	CGColorSpaceRelease(at_attach);
}

int
main(void)
{
	@autoreleasepool
	{
		CGColorSpaceRef p3;

		rmb_device = MTLCreateSystemDefaultDevice();
		if (!rmb_device)
		{
			printf("rmb_cs_probe: skipped: no Metal device\n");
			return 0;
		}

		run("layer with its default colour space", NULL);

		p3 = CGColorSpaceCreateWithName(kCGColorSpaceDisplayP3);
		run("layer tagged Display P3", p3);
		CGColorSpaceRelease(p3);

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
