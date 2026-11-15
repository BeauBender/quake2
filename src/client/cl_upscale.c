/* TASK123 red stub: every function refuses or does nothing */
#include "header/client.h"
#include "header/upscale.h"

int CL_UpscaleNameOk(const char *name) { (void)name; return 0; }
int CL_UpscaleOutName(const char *name, char *out, size_t outsize) { (void)name; (void)out; (void)outsize; return 0; }
int CL_UpscaleInGitTree(const char *dir, int (*exists)(const char *path, void *ctx), void *ctx) { (void)dir; (void)exists; (void)ctx; return 0; }
int CL_UpscaleEntryExists(const char *path, void *ctx) { (void)path; (void)ctx; return 0; }
int CL_UpscaleDirInGitTree(const char *dir) { (void)dir; return 0; }
int CL_UpscaleSizeOk(long long w, long long h) { (void)w; (void)h; return 0; }
void CL_UpscaleWrapPad(const unsigned char *src, int w, int h, unsigned char *dst) { (void)src; (void)w; (void)h; (void)dst; }
void CL_UpscaleCrop(const unsigned char *src, int sw, int x, int y, int w, int h, unsigned char *dst) { (void)src; (void)sw; (void)x; (void)y; (void)w; (void)h; (void)dst; }
void CL_UpscaleScale2x(const unsigned char *src, unsigned char *dst, int w, int h) { (void)src; (void)dst; (void)w; (void)h; }
unsigned char *CL_UpscaleIndices(const unsigned char *src, int w, int h, int factor, int wrap) { (void)src; (void)w; (void)h; (void)factor; (void)wrap; return NULL; }
void CL_UpscaleFloodFillSkin(unsigned char *skin, int w, int h, const unsigned *table) { (void)skin; (void)w; (void)h; (void)table; }
void CL_UpscaleTable(const unsigned char *palette, unsigned *table) { (void)palette; memset(table, 0, 256 * sizeof(*table)); }
void CL_UpscaleToRGBA(const unsigned char *data, size_t w, size_t h, const unsigned *table, unsigned char *rgba) { (void)data; (void)table; memset(rgba, 0, w * h * 4); }
int CL_UpscaleWalPixels(const unsigned char *data, size_t len, int *w, int *h, size_t *offset) { (void)data; (void)len; (void)w; (void)h; (void)offset; return 0; }
int CL_UpscalePcxSize(const unsigned char *data, size_t len, int *w, int *h) { (void)data; (void)len; (void)w; (void)h; return 0; }
