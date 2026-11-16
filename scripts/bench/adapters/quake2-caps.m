/*
 * scripts/bench/adapters/quake2-caps.m: the capability probe of the quake2
 * bench adapter (scripts/bench/adapters/quake2.sh).
 *
 * Asks the system what ref_metal asks it: the default Metal device, its
 * supportsRaytracing (rm_main.c's capability report), the supportsDevice
 * of MetalFX's spatial and temporal scalers (rmb_metalfx.m's), and the
 * main display's refresh rate. Prints key=value lines, which the adapter
 * reads as data:
 *
 *   gpu=yes|no
 *   raytracing=yes|no
 *   metalfx=yes|no            the spatial scaler (r_metalfx 1)
 *   metalfx-temporal=yes|no   the temporal scaler (r_metalfx 2)
 *   display-hz=<whole hertz>|unknown
 *
 * Built by the adapter's preflight into its temporary directory with the
 * host's own toolchain; never part of the engine.
 */

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>

int
main(void)
{
	@autoreleasepool
	{
		id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
		CGDisplayModeRef mode = CGDisplayCopyDisplayMode(CGMainDisplayID());
		double hz = mode ? CGDisplayModeGetRefreshRate(mode) : 0.0;

		printf("gpu=%s\n", dev ? "yes" : "no");
		printf("raytracing=%s\n", (dev && [dev supportsRaytracing]) ? "yes" : "no");
		printf("metalfx=%s\n", (dev && [MTLFXSpatialScalerDescriptor supportsDevice:dev]) ? "yes" : "no");
		printf("metalfx-temporal=%s\n", (dev && [MTLFXTemporalScalerDescriptor supportsDevice:dev]) ? "yes" : "no");

		if (hz >= 1.0)
		{
			printf("display-hz=%.0f\n", hz);
		}
		else
		{
			printf("display-hz=unknown\n");
		}

		if (mode)
		{
			CGDisplayModeRelease(mode);
		}
	}

	return 0;
}
