/*
 * scripts/metalfx-report/ssim.c -- the structural measure of the MetalFX report (TASK114)
 *
 *   ssim A.tga B.tga
 *
 * Prints one line, "ssim=<mean> min=<lowest window> w=<n> h=<n>", the mean structural
 * similarity (Wang et al. 2004) of the luma (Rec. 601, 0..255) of two uncompressed
 * truecolor TGAs of the same size, over 8x8 windows every 4 pixels, uniform weights,
 * K1 = 0.01, K2 = 0.03. 1 means identical; less means structure differs. A frame
 * smaller than one window is compared as a single window.
 *
 * Exit 0 on success, 1 on a usage error, 3 when a file cannot be read, is not a
 * supported TGA, or the sizes differ: a failure to measure is never a score.
 * Portable C99, no dependencies: cc -O2 -o ssim ssim.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int w, h; double *y; } luma_t;

#define WIN 8
#define STEP 4

static int read_luma(const char *path, luma_t *l)
{
	unsigned char hdr[18], *px;
	int bpp, bottom_up, x, y;
	size_t n;
	FILE *fp = fopen(path, "rb");
	if (!fp) { fprintf(stderr, "ssim: cannot open %s\n", path); return 0; }
	if (fread(hdr, 1, 18, fp) != 18 || hdr[2] != 2) { fprintf(stderr, "ssim: %s is not an uncompressed truecolor TGA\n", path); fclose(fp); return 0; }
	l->w = hdr[12] | (hdr[13] << 8); l->h = hdr[14] | (hdr[15] << 8);
	bpp = hdr[16] / 8;
	bottom_up = !(hdr[17] & 0x20);
	if (l->w < 1 || l->h < 1 || (bpp != 3 && bpp != 4)) { fprintf(stderr, "ssim: %s: unsupported size or depth\n", path); fclose(fp); return 0; }
	if (fseek(fp, hdr[0], SEEK_CUR) != 0) { fprintf(stderr, "ssim: %s: bad header\n", path); fclose(fp); return 0; }
	n = (size_t)l->w * l->h * bpp;
	px = malloc(n);
	l->y = malloc(sizeof(double) * (size_t)l->w * l->h);
	if (!px || !l->y || fread(px, 1, n, fp) != n) { fprintf(stderr, "ssim: %s: short read\n", path); fclose(fp); free(px); return 0; }
	fclose(fp);
	for (y = 0; y < l->h; y++)
	{
		const int row = bottom_up ? l->h - 1 - y : y;
		for (x = 0; x < l->w; x++)
		{
			const unsigned char *p = px + ((size_t)y * l->w + x) * bpp; /* B G R (A) */
			l->y[(size_t)row * l->w + x] = 0.114 * p[0] + 0.587 * p[1] + 0.299 * p[2];
		}
	}
	free(px);
	return 1;
}

int main(int argc, char **argv)
{
	luma_t a = {0, 0, NULL}, b = {0, 0, NULL};
	const double c1 = (0.01 * 255) * (0.01 * 255), c2 = (0.03 * 255) * (0.03 * 255);
	double sum = 0, lowest = 1;
	long count = 0;
	int x0, y0, x, y, ww, wh;

	if (argc != 3) { fprintf(stderr, "usage: ssim A.tga B.tga\n"); return 1; }
	if (!read_luma(argv[1], &a) || !read_luma(argv[2], &b)) return 3;
	if (a.w != b.w || a.h != b.h) { fprintf(stderr, "ssim: size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h); return 3; }
	ww = a.w < WIN ? a.w : WIN;
	wh = a.h < WIN ? a.h : WIN;

	for (y0 = 0; y0 + wh <= a.h; y0 += STEP)
	{
		for (x0 = 0; x0 + ww <= a.w; x0 += STEP)
		{
			double ma = 0, mb = 0, va = 0, vb = 0, cov = 0, s;
			const double n = (double)ww * wh;
			for (y = y0; y < y0 + wh; y++)
				for (x = x0; x < x0 + ww; x++)
				{
					ma += a.y[(size_t)y * a.w + x];
					mb += b.y[(size_t)y * a.w + x];
				}
			ma /= n; mb /= n;
			for (y = y0; y < y0 + wh; y++)
				for (x = x0; x < x0 + ww; x++)
				{
					const double da = a.y[(size_t)y * a.w + x] - ma, db = b.y[(size_t)y * a.w + x] - mb;
					va += da * da; vb += db * db; cov += da * db;
				}
			va /= n; vb /= n; cov /= n;
			s = ((2 * ma * mb + c1) * (2 * cov + c2)) / ((ma * ma + mb * mb + c1) * (va + vb + c2));
			sum += s;
			if (s < lowest) lowest = s;
			count++;
			if (x0 + ww == a.w) break;
		}
		if (y0 + wh == a.h) break;
	}
	if (count < 1) { fprintf(stderr, "ssim: no window\n"); return 3; }
	printf("ssim=%.6f min=%.6f w=%d h=%d\n", sum / count, lowest, a.w, a.h);
	return 0;
}
