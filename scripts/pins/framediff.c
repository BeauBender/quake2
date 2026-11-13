/*
 * scripts/pins/framediff.c -- tolerant frame comparator for the pin harness
 * (TASK27). The default comparator. Another one with the same CLI contract
 * can replace it through PINS_DIFF:
 * "<diff> a.tga b.tga --threshold T --bad-threshold B" prints
 * "score=<0..1> ... bad=<0..1> ..." on stdout and exits 0 when
 * score <= T and bad <= B, 2 when either is above, 3 on a format or size
 * mismatch.
 *
 * score is the mean absolute difference over all channels (0..1). bad is
 * the fraction of pixels whose largest channel difference exceeds 8/255,
 * so a large localized defect fails even when the mean stays low.
 *
 * Reads uncompressed 24/32-bit TGA (type 2), the format the engine's pin
 * capture writes. Portable C99, no dependencies: cc -O2 -o framediff framediff.c
 *
 *   framediff A.tga B.tga [--threshold T] [--bad-threshold B]
 *                                             compare (default T = 0.02, B = 0.005)
 *   framediff --gen OUT.tga W H SEED          deterministic synthetic frame
 *   framediff --perturb IN.tga OUT.tga AMOUNT  shift every channel by AMOUNT
 *   framediff --block IN.tga OUT.tga X Y W H V set a W x H block to value V
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

typedef struct { int w, h, bpp; unsigned char *px; } frame_t;

static int read_tga(const char *path, frame_t *f)
{
	unsigned char hdr[18];
	FILE *fp = fopen(path, "rb");
	if (!fp) { fprintf(stderr, "framediff: cannot open %s\n", path); return 0; }
	if (fread(hdr, 1, 18, fp) != 18 || hdr[2] != 2) { fprintf(stderr, "framediff: %s is not an uncompressed truecolor TGA\n", path); fclose(fp); return 0; }
	f->w = hdr[12] | (hdr[13] << 8); f->h = hdr[14] | (hdr[15] << 8); f->bpp = hdr[16] / 8;
	if (f->bpp != 3 && f->bpp != 4) { fprintf(stderr, "framediff: %s: unsupported depth %d\n", path, hdr[16]); fclose(fp); return 0; }
	fseek(fp, hdr[0], SEEK_CUR);
	size_t n = (size_t)f->w * f->h * f->bpp;
	f->px = malloc(n);
	if (!f->px || fread(f->px, 1, n, fp) != n) { fprintf(stderr, "framediff: %s: short read\n", path); fclose(fp); return 0; }
	fclose(fp);
	return 1;
}

static int write_tga(const char *path, const frame_t *f)
{
	unsigned char hdr[18] = {0};
	hdr[2] = 2; hdr[12] = f->w & 255; hdr[13] = f->w >> 8; hdr[14] = f->h & 255; hdr[15] = f->h >> 8; hdr[16] = f->bpp * 8;
	FILE *fp = fopen(path, "wb");
	if (!fp) return 0;
	fwrite(hdr, 1, 18, fp); fwrite(f->px, 1, (size_t)f->w * f->h * f->bpp, fp); fclose(fp);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc >= 6 && !strcmp(argv[1], "--gen")) {
		frame_t f; f.w = atoi(argv[3]); f.h = atoi(argv[4]); f.bpp = 3;
		unsigned s = (unsigned)atoi(argv[5]);
		f.px = malloc((size_t)f.w * f.h * 3);
		for (int y = 0; y < f.h; y++) for (int x = 0; x < f.w; x++) {
			unsigned char *p = f.px + ((size_t)y * f.w + x) * 3;
			p[0] = (unsigned char)((x * 7 + s) & 255); p[1] = (unsigned char)((y * 5 + s * 3) & 255); p[2] = (unsigned char)(((x ^ y) + s) & 255);
		}
		return write_tga(argv[2], &f) ? 0 : 1;
	}
	if (argc >= 5 && !strcmp(argv[1], "--perturb")) {
		frame_t f; if (!read_tga(argv[2], &f)) return 3;
		int amt = atoi(argv[4]); size_t n = (size_t)f.w * f.h * f.bpp;
		for (size_t i = 0; i < n; i++) { int v = f.px[i] + amt; f.px[i] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v); }
		return write_tga(argv[3], &f) ? 0 : 1;
	}
	if (argc >= 9 && !strcmp(argv[1], "--block")) {
		frame_t f; if (!read_tga(argv[2], &f)) return 3;
		int bx = atoi(argv[4]), by = atoi(argv[5]), bw = atoi(argv[6]), bh = atoi(argv[7]), v = atoi(argv[8]);
		for (int y = by; y < by + bh && y < f.h; y++) for (int x = bx; x < bx + bw && x < f.w; x++)
			for (int c = 0; c < 3; c++) f.px[((size_t)y * f.w + x) * f.bpp + c] = (unsigned char)v;
		return write_tga(argv[3], &f) ? 0 : 1;
	}
	if (argc < 3) { fprintf(stderr, "usage: framediff A.tga B.tga [--threshold T] [--bad-threshold B] | --gen OUT W H SEED | --perturb IN OUT AMOUNT | --block IN OUT X Y W H V\n"); return 1; }
	double threshold = 0.02, bad_threshold = 0.005;
	for (int i = 3; i < argc; i++) {
		double *dst = !strcmp(argv[i], "--threshold") ? &threshold : !strcmp(argv[i], "--bad-threshold") ? &bad_threshold : NULL;
		char *end;
		if (!dst || i + 1 >= argc) { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
		*dst = strtod(argv[++i], &end);
		if (end == argv[i] || *end || *dst < 0) { fprintf(stderr, "framediff: %s needs a non-negative number\n", argv[i - 1]); return 1; }
	}
	frame_t a, b;
	if (!read_tga(argv[1], &a) || !read_tga(argv[2], &b)) return 3;
	if (a.w != b.w || a.h != b.h) { fprintf(stderr, "framediff: size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h); return 3; }
	double sum = 0; int maxd = 0; size_t bad = 0, npx = (size_t)a.w * a.h;
	for (size_t i = 0; i < npx; i++) {
		int worst = 0;
		for (int c = 0; c < 3; c++) {
			int d = abs((int)a.px[i * a.bpp + c] - (int)b.px[i * b.bpp + c]);
			sum += d; if (d > worst) worst = d;
		}
		if (worst > maxd) maxd = worst;
		if (worst > 8) bad++;
	}
	double score = sum / (npx * 3.0 * 255.0), badf = (double)bad / npx;
	printf("score=%.6f max=%d bad=%.6f w=%d h=%d threshold=%.6f bad_threshold=%.6f\n", score, maxd, badf, a.w, a.h, threshold, bad_threshold);
	return (score <= threshold && badf <= bad_threshold) ? 0 : 2;
}
