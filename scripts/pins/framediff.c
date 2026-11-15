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
 * With --mask M.tga (RT2, TASK54) only the pixels M marks count (any of
 * its three channels above 0): score is the mean over those pixels' channels
 * and bad the fraction of them, "masked=<n>" ends the line, and a mask
 * that marks nothing passes. M must have the frames' size (3 otherwise).
 *
 * Reads uncompressed 24/32-bit TGA (type 2), the format the engine's pin
 * capture writes; a frame with no pixels (width or height 0) is refused (3).
 * An image it writes is never written through a symbolic link or over
 * anything but a regular file, and is at most 65535 pixels each way. Portable C99, no dependencies: cc -O2 -o framediff framediff.c
 *
 *   framediff A.tga B.tga [--threshold T] [--bad-threshold B] [--mask M.tga]
 *                                             compare (default T = 0.02, B = 0.005)
 *   framediff --gen OUT.tga W H SEED          deterministic synthetic frame
 *   framediff --perturb IN.tga OUT.tga AMOUNT  shift every channel by AMOUNT
 *   framediff --block IN.tga OUT.tga X Y W H V set a W x H block to value V
 *
 * Footprint (TASK133): how much an effect changes the picture. The change of
 * a pixel is the mean over its three channels of (B - A), in levels (0..255),
 * negative when B is darker. A pixel is changed when |change| > --delta
 * (default 8 levels, the threshold of "bad"). With --mask only the pixels
 * M marks count, as above. A is the "off" frame and B the "on" one.
 *
 *   framediff --footprint A.tga B.tga [--delta D] [--mask M.tga] [--band METRIC=LO..HI]...
 *       prints one line, "footprint changed=<f> darker=<f> brighter=<f>
 *       max=<levels> maxdark=<levels> maxbright=<levels> chmax=<levels> mean=<levels>
 *       w=<n> h=<n> delta=<D> [masked=<n>] band=<none|ok|OUT:metric,...>":
 *       changed, darker and brighter are shares (0..1) of the counted
 *       pixels, max is the largest |change|, mean the mean |change| over
 *       all counted pixels. chmax is the largest change of any single
 *       channel (max and mean average the three channels, so a colour-only
 *       shift, red down and blue up, leaves them at 0 and shows in chmax).
 *       Exits 0, or 2 when a --band is violated
 *       (METRIC is changed, darker, brighter, max or mean; LO..HI inclusive;
 *       the option may repeat), 3 on a format or size mismatch, 1 on a
 *       usage error.
 *   framediff --highlight A.tga B.tga OUT.tga [--gain G] [--mask M.tga]
 *       writes a 24-bit TGA: A in grey, the pixels B made darker in red and
 *       brighter in green, the more saturated the larger |change| * G
 *       (default G = 8, so 32 levels is full colour)
 *   framediff --zoom A.tga B.tga OUT.tga [--window WxH] [--scale S] [--mask M.tga]
 *       writes a 24-bit TGA: the W x H window (default a quarter of the
 *       frame in each direction) holding the largest sum of |change|, from
 *       A on the left and B on the right, each scaled by S (default 3) with
 *       nearest-neighbour sampling; prints "window=x,y,w,h"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

typedef struct { int w, h, bpp, desc; unsigned char *px; } frame_t;

static int read_tga(const char *path, frame_t *f)
{
	unsigned char hdr[18];
	FILE *fp = fopen(path, "rb");
	if (!fp) { fprintf(stderr, "framediff: cannot open %s\n", path); return 0; }
	if (fread(hdr, 1, 18, fp) != 18 || hdr[2] != 2) { fprintf(stderr, "framediff: %s is not an uncompressed truecolor TGA\n", path); fclose(fp); return 0; }
	f->w = hdr[12] | (hdr[13] << 8); f->h = hdr[14] | (hdr[15] << 8); f->bpp = hdr[16] / 8; f->desc = hdr[17];
	if (f->w < 1 || f->h < 1) { fprintf(stderr, "framediff: %s has no pixels (%dx%d)\n", path, f->w, f->h); fclose(fp); return 0; }
	if (f->bpp != 3 && f->bpp != 4) { fprintf(stderr, "framediff: %s: unsupported depth %d\n", path, hdr[16]); fclose(fp); return 0; }
	fseek(fp, hdr[0], SEEK_CUR);
	size_t n = (size_t)f->w * f->h * f->bpp;
	f->px = malloc(n);
	if (!f->px || fread(f->px, 1, n, fp) != n) { fprintf(stderr, "framediff: %s: short read\n", path); fclose(fp); return 0; }
	fclose(fp);
	return 1;
}

/* open an output file for writing, never through a symbolic link and never
 * over anything but a regular file: the path of an image may be planted */
static FILE *open_out(const char *path)
{
	struct stat st;
	int fd;
	FILE *fp;
	if (lstat(path, &st) == 0 && !S_ISREG(st.st_mode)) { fprintf(stderr, "framediff: %s exists and is not a regular file (a symbolic link is never written through)\n", path); return NULL; }
	fd = open(path, O_WRONLY | O_CREAT | O_NOFOLLOW | O_NONBLOCK, 0666);
	if (fd < 0) { fprintf(stderr, "framediff: cannot write %s: %s\n", path, strerror(errno)); return NULL; }
	if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || ftruncate(fd, 0) != 0) { fprintf(stderr, "framediff: %s is not a regular file\n", path); close(fd); return NULL; }
	fp = fdopen(fd, "wb");
	if (!fp) close(fd);
	return fp;
}

static int write_tga(const char *path, const frame_t *f)
{
	unsigned char hdr[18] = {0};
	FILE *fp;
	if (f->w < 1 || f->h < 1 || f->w > 65535 || f->h > 65535) { fprintf(stderr, "framediff: an image of %dx%d does not fit a TGA (1 to 65535 each way)\n", f->w, f->h); return 0; }
	hdr[2] = 2; hdr[12] = f->w & 255; hdr[13] = f->w >> 8; hdr[14] = f->h & 255; hdr[15] = f->h >> 8; hdr[16] = f->bpp * 8; hdr[17] = f->desc;
	fp = open_out(path);
	if (!fp) return 0;
	fwrite(hdr, 1, 18, fp); fwrite(f->px, 1, (size_t)f->w * f->h * f->bpp, fp); fclose(fp);
	return 1;
}

/* ---- footprint (TASK133) ---- */
static int read_pair(const char *pa, const char *pb, const char *pm, frame_t *a, frame_t *b, frame_t *m)
{
	m->px = NULL;
	if (!read_tga(pa, a) || !read_tga(pb, b)) return 0;
	if (a->w != b->w || a->h != b->h) { fprintf(stderr, "framediff: size mismatch %dx%d vs %dx%d\n", a->w, a->h, b->w, b->h); return 0; }
	if (pm) {
		if (!read_tga(pm, m)) return 0;
		if (m->w != a->w || m->h != a->h) { fprintf(stderr, "framediff: mask size mismatch %dx%d vs %dx%d\n", m->w, m->h, a->w, a->h); return 0; }
	}
	return 1;
}
static int marked_px(const frame_t *m, size_t i) { return !m->px || (m->px[i * m->bpp] | m->px[i * m->bpp + 1] | m->px[i * m->bpp + 2]); }
/* the change of pixel i, B - A summed over the three channels (-765..765) */
static int change_sum(const frame_t *a, const frame_t *b, size_t i)
{
	int s = 0;
	for (int c = 0; c < 3; c++) s += (int)b->px[i * b->bpp + c] - (int)a->px[i * a->bpp + c];
	return s;
}
static int parse_num(const char *s, double *v)
{
	char *end; *v = strtod(s, &end);
	return end != s && !*end && *v >= 0;
}

static int footprint_main(int argc, char **argv)
{
	double delta = 8;
	const char *mask_path = NULL, *bm[16]; double blo[16], bhi[16]; int nb = 0;
	if (argc < 4) { fprintf(stderr, "usage: framediff --footprint A.tga B.tga [--delta D] [--mask M.tga] [--band METRIC=LO..HI]...\n"); return 1; }
	for (int i = 4; i < argc; i++) {
		if (i + 1 >= argc) { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
		if (!strcmp(argv[i], "--delta")) {
			if (!parse_num(argv[++i], &delta)) { fprintf(stderr, "framediff: --delta needs a non-negative number\n"); return 1; }
		} else if (!strcmp(argv[i], "--mask")) mask_path = argv[++i];
		else if (!strcmp(argv[i], "--band")) {
			char *eq = strchr(argv[i + 1], '='), *dots = eq ? strstr(eq, "..") : NULL, *lo = eq ? eq + 1 : NULL;
			char metric[16], lob[64], hib[64]; size_t ml = eq ? (size_t)(eq - argv[i + 1]) : 0;
			if (!eq || !dots || nb >= 16 || ml == 0 || ml >= sizeof metric || (size_t)(dots - lo) >= sizeof lob || strlen(dots + 2) >= sizeof hib) { fprintf(stderr, "framediff: --band needs METRIC=LO..HI, got %s\n", argv[i + 1]); return 1; }
			memcpy(metric, argv[i + 1], ml); metric[ml] = 0;
			memcpy(lob, lo, (size_t)(dots - lo)); lob[dots - lo] = 0; strcpy(hib, dots + 2);
			if (strcmp(metric, "changed") && strcmp(metric, "darker") && strcmp(metric, "brighter") && strcmp(metric, "max") && strcmp(metric, "mean")) { fprintf(stderr, "framediff: --band metric must be changed, darker, brighter, max or mean, got %s\n", metric); return 1; }
			if (!parse_num(lob, &blo[nb]) || !parse_num(hib, &bhi[nb]) || blo[nb] > bhi[nb]) { fprintf(stderr, "framediff: --band range needs 0 <= LO <= HI, got %s\n", argv[i + 1]); return 1; }
			bm[nb] = strdup(metric); i++; nb++;
		} else { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
	}
	frame_t a, b, m;
	if (!read_pair(argv[2], argv[3], mask_path, &a, &b, &m)) return 3;
	size_t npx = (size_t)a.w * a.h, n = 0, nchg = 0, ndark = 0, nbright = 0;
	double maxdark = 0, maxbright = 0, sumabs = 0, chmax = 0;
	for (size_t i = 0; i < npx; i++) {
		if (!marked_px(&m, i)) continue;
		n++;
		int s = change_sum(&a, &b, i);
		double d = s / 3.0, ad = d < 0 ? -d : d;
		sumabs += ad;
		for (int c = 0; c < 3; c++) {
			double dc = (double)b.px[i * b.bpp + c] - (double)a.px[i * a.bpp + c];
			if (dc < 0) dc = -dc;
			if (dc > chmax) chmax = dc;
		}
		if (s < 0 && ad > maxdark) maxdark = ad;
		if (s > 0 && ad > maxbright) maxbright = ad;
		if (ad > delta) { nchg++; if (s < 0) ndark++; else nbright++; }
	}
	double vals[5], maxv = maxdark > maxbright ? maxdark : maxbright;
	vals[0] = n ? (double)nchg / n : 0; vals[1] = n ? (double)ndark / n : 0; vals[2] = n ? (double)nbright / n : 0; vals[3] = maxv; vals[4] = n ? sumabs / n : 0;
	static const char *names[5] = { "changed", "darker", "brighter", "max", "mean" };
	printf("footprint changed=%.6f darker=%.6f brighter=%.6f max=%.2f maxdark=%.2f maxbright=%.2f chmax=%.2f mean=%.4f w=%d h=%d delta=%g",
		vals[0], vals[1], vals[2], maxv, maxdark, maxbright, chmax, vals[4], a.w, a.h, delta);
	if (m.px) printf(" masked=%zu", n);
	char viol[128] = ""; int out = 0;
	for (int k = 0; k < nb; k++) for (int j = 0; j < 5; j++) if (!strcmp(bm[k], names[j])) {
		if (vals[j] < blo[k] || vals[j] > bhi[k]) {
			if (strlen(viol) + strlen(names[j]) + 2 < sizeof viol) { if (out) strcat(viol, ","); strcat(viol, names[j]); }
			out = 1;
		}
	}
	printf(" band=%s", !nb ? "none" : out ? "OUT:" : "ok");
	if (out) printf("%s", viol);
	printf("\n");
	return out ? 2 : 0;
}

static int highlight_main(int argc, char **argv)
{
	double gain = 8; const char *mask_path = NULL;
	if (argc < 5) { fprintf(stderr, "usage: framediff --highlight A.tga B.tga OUT.tga [--gain G] [--mask M.tga]\n"); return 1; }
	for (int i = 5; i < argc; i++) {
		if (i + 1 >= argc) { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
		if (!strcmp(argv[i], "--gain")) { if (!parse_num(argv[++i], &gain)) { fprintf(stderr, "framediff: --gain needs a non-negative number\n"); return 1; } }
		else if (!strcmp(argv[i], "--mask")) mask_path = argv[++i];
		else { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
	}
	frame_t a, b, m, o;
	if (!read_pair(argv[2], argv[3], mask_path, &a, &b, &m)) return 3;
	o.w = a.w; o.h = a.h; o.bpp = 3; o.desc = a.desc;
	size_t npx = (size_t)a.w * a.h;
	o.px = malloc(npx * 3);
	if (!o.px) return 1;
	for (size_t i = 0; i < npx; i++) {
		const unsigned char *pa = a.px + i * a.bpp;
		double grey = 0.3 * pa[2] + 0.59 * pa[1] + 0.11 * pa[0], t = 0, col[3] = { grey, grey, grey }; /* B G R */
		int s = marked_px(&m, i) ? change_sum(&a, &b, i) : 0;
		if (s) {
			double tgt[3];
			t = (s < 0 ? -s : s) / 3.0 * gain / 255.0; if (t > 1) t = 1;
			tgt[0] = 0; tgt[1] = s > 0 ? 255 : 0; tgt[2] = s < 0 ? 255 : 0;
			for (int c = 0; c < 3; c++) col[c] = grey * (1 - t) + tgt[c] * t;
		}
		for (int c = 0; c < 3; c++) o.px[i * 3 + c] = (unsigned char)(col[c] + 0.5);
	}
	return write_tga(argv[4], &o) ? 0 : 1;
}

static int zoom_main(int argc, char **argv)
{
	int ww = 0, wh = 0, scale = 3; const char *mask_path = NULL;
	if (argc < 5) { fprintf(stderr, "usage: framediff --zoom A.tga B.tga OUT.tga [--window WxH] [--scale S] [--mask M.tga]\n"); return 1; }
	for (int i = 5; i < argc; i++) {
		if (i + 1 >= argc) { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
		if (!strcmp(argv[i], "--window")) {
			if (sscanf(argv[++i], "%dx%d", &ww, &wh) != 2 || ww < 1 || wh < 1) { fprintf(stderr, "framediff: --window needs WxH\n"); return 1; }
		} else if (!strcmp(argv[i], "--scale")) {
			scale = atoi(argv[++i]);
			if (scale < 1 || scale > 16) { fprintf(stderr, "framediff: --scale needs 1 to 16\n"); return 1; }
		} else if (!strcmp(argv[i], "--mask")) mask_path = argv[++i];
		else { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
	}
	frame_t a, b, m, o;
	if (!read_pair(argv[2], argv[3], mask_path, &a, &b, &m)) return 3;
	if (!ww) { ww = a.w / 4; wh = a.h / 4; }
	if (ww > a.w) ww = a.w;
	if (wh > a.h) wh = a.h;
	if (ww < 1) ww = 1;
	if (wh < 1) wh = 1;
	/* summed-area table of |change|, so each window costs four reads */
	size_t sw = (size_t)a.w + 1;
	double *sat = calloc(sw * ((size_t)a.h + 1), sizeof *sat);
	if (!sat) return 1;
	for (int y = 0; y < a.h; y++) for (int x = 0; x < a.w; x++) {
		size_t i = (size_t)y * a.w + x;
		int s = marked_px(&m, i) ? change_sum(&a, &b, i) : 0;
		sat[(size_t)(y + 1) * sw + x + 1] = (s < 0 ? -s : s) + sat[(size_t)y * sw + x + 1] + sat[(size_t)(y + 1) * sw + x] - sat[(size_t)y * sw + x];
	}
	int bx = 0, by = 0; double best = -1;
	for (int y = 0; y + wh <= a.h; y++) for (int x = 0; x + ww <= a.w; x++) {
		double v = sat[(size_t)(y + wh) * sw + x + ww] - sat[(size_t)y * sw + x + ww] - sat[(size_t)(y + wh) * sw + x] + sat[(size_t)y * sw + x];
		if (v > best) { best = v; bx = x; by = y; }
	}
	free(sat);
	if ((long)2 * ww * scale > 65535 || (long)wh * scale > 65535) { fprintf(stderr, "framediff: the zoom image would be %ldx%ld, over the 65535 a TGA holds; use a smaller --window or --scale\n", (long)2 * ww * scale, (long)wh * scale); return 1; }
	o.w = 2 * ww * scale; o.h = wh * scale; o.bpp = 3; o.desc = a.desc;
	o.px = malloc((size_t)o.w * o.h * 3);
	if (!o.px) return 1;
	for (int y = 0; y < o.h; y++) for (int x = 0; x < o.w; x++) {
		const frame_t *src = x < ww * scale ? &a : &b;
		int sx = bx + (x % (ww * scale)) / scale, sy = by + y / scale;
		for (int c = 0; c < 3; c++) o.px[((size_t)y * o.w + x) * 3 + c] = src->px[((size_t)sy * src->w + sx) * src->bpp + c];
	}
	printf("window=%d,%d,%d,%d\n", bx, by, ww, wh);
	return write_tga(argv[4], &o) ? 0 : 1;
}

int main(int argc, char **argv)
{
	if (argc >= 2 && !strcmp(argv[1], "--footprint")) return footprint_main(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "--highlight")) return highlight_main(argc, argv);
	if (argc >= 2 && !strcmp(argv[1], "--zoom")) return zoom_main(argc, argv);
	if (argc >= 6 && !strcmp(argv[1], "--gen")) {
		frame_t f; f.w = atoi(argv[3]); f.h = atoi(argv[4]); f.bpp = 3; f.desc = 0;
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
	if (argc < 3) { fprintf(stderr, "usage: framediff A.tga B.tga [--threshold T] [--bad-threshold B] [--mask M.tga] | --gen OUT W H SEED | --perturb IN OUT AMOUNT | --block IN OUT X Y W H V | --footprint A B [--delta D] [--mask M] [--band M=LO..HI] | --highlight A B OUT | --zoom A B OUT\n"); return 1; }
	double threshold = 0.02, bad_threshold = 0.005;
	const char *mask_path = NULL;
	for (int i = 3; i < argc; i++) {
		if (!strcmp(argv[i], "--mask")) {
			if (i + 1 >= argc) { fprintf(stderr, "framediff: --mask needs a file\n"); return 1; }
			mask_path = argv[++i];
			continue;
		}
		double *dst = !strcmp(argv[i], "--threshold") ? &threshold : !strcmp(argv[i], "--bad-threshold") ? &bad_threshold : NULL;
		char *end;
		if (!dst || i + 1 >= argc) { fprintf(stderr, "framediff: unknown or incomplete option %s\n", argv[i]); return 1; }
		*dst = strtod(argv[++i], &end);
		if (end == argv[i] || *end || *dst < 0) { fprintf(stderr, "framediff: %s needs a non-negative number\n", argv[i - 1]); return 1; }
	}
	frame_t a, b;
	if (!read_tga(argv[1], &a) || !read_tga(argv[2], &b)) return 3;
	if (a.w != b.w || a.h != b.h) { fprintf(stderr, "framediff: size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h); return 3; }
	frame_t m = {0, 0, 0, 0, NULL};
	if (mask_path) {
		if (!read_tga(mask_path, &m)) return 3;
		if (m.w != a.w || m.h != a.h) { fprintf(stderr, "framediff: mask size mismatch %dx%d vs %dx%d\n", m.w, m.h, a.w, a.h); return 3; }
	}
	double sum = 0; int maxd = 0; size_t bad = 0, npx = (size_t)a.w * a.h, marked = 0;
	for (size_t i = 0; i < npx; i++) {
		int worst = 0;
		if (m.px && !(m.px[i * m.bpp] | m.px[i * m.bpp + 1] | m.px[i * m.bpp + 2])) continue;
		marked++;
		for (int c = 0; c < 3; c++) {
			int d = abs((int)a.px[i * a.bpp + c] - (int)b.px[i * b.bpp + c]);
			sum += d; if (d > worst) worst = d;
		}
		if (worst > maxd) maxd = worst;
		if (worst > 8) bad++;
	}
	double score = marked ? sum / (marked * 3.0 * 255.0) : 0, badf = marked ? (double)bad / marked : 0;
	printf("score=%.6f max=%d bad=%.6f w=%d h=%d threshold=%.6f bad_threshold=%.6f", score, maxd, badf, a.w, a.h, threshold, bad_threshold);
	if (m.px) printf(" masked=%zu", marked);
	printf("\n");
	return (score <= threshold && badf <= bad_threshold) ? 0 : 2;
}
