/* stub: every function exists and answers wrongly (TASK53 red phase) */
#include <string.h>
#include "header/rm_rt.h"
unsigned rm_rt_material(int texflags) { (void)texflags; return 0; }
int rm_rt_wanted(float stats, int d) { (void)stats; (void)d; return 0; }
long rm_rt_materials(const rm_surfrec_t *recs, long nrecs, unsigned long nindices, unsigned char *mat, unsigned long cap) { (void)recs; (void)nrecs; (void)nindices; (void)mat; (void)cap; return -2; }
int rm_rt_range(unsigned long a, unsigned long b, unsigned long c, unsigned long *d, unsigned long *e) { (void)a; (void)b; (void)c; (void)d; (void)e; return -1; }
void rm_rt_count(const unsigned char *mat, unsigned long f, unsigned long n, rm_rt_tris_t *t) { (void)mat; (void)f; (void)n; (void)t; }
int rm_rt_frames_bytes(long a, long b, long c, long d, size_t cap, size_t *bytes) { (void)a; (void)b; (void)c; (void)d; (void)cap; (void)bytes; return -1; }
long rm_rt_frame_vertex(long f, long n, long fs, long v) { (void)f; (void)n; (void)fs; (void)v; return -2; }
long rm_rt_alias_indices(const short *t, long s, long n, long x, unsigned short *o, long c) { (void)t; (void)s; (void)n; (void)x; (void)o; (void)c; return -1; }
void rm_rt_transform(const float *o, const float *a, int b, float *x) { (void)o; (void)a; (void)b; memset(x, 0, 12 * sizeof(float)); }
int rm_rt_instances(const rm_rt_ent_t *e, long n, int w, int g, float h, rm_rt_inst_t *o, int c, rm_rt_excl_t *x) { (void)e; (void)n; (void)w; (void)g; (void)h; (void)o; (void)c; memset(x, 0x7f, sizeof(*x)); return -1; }
size_t rm_rt_instance_bytes(long n) { (void)n; return 1; }
size_t rm_rt_avert_bytes(long n) { (void)n; return 1; }
size_t rm_rt_aindex_bytes(long n) { (void)n; return 1; }
int rm_rt_spread(double *ms, unsigned long n, rm_rt_spread_t *o) { (void)ms; (void)n; memset(o, 0x7f, sizeof(*o)); return -1; }
int rm_rt_map_format(const char *m, const rm_rt_map_stats_t *s, char *b, size_t l) { (void)m; (void)s; if (l > 1) { b[0] = 'x'; b[1] = 0; } return -1; }
int rm_rt_frame_format(const rm_rt_frame_stats_t *s, char *b, size_t l) { (void)s; if (l > 1) { b[0] = 'x'; b[1] = 0; } return -1; }
int rm_rt_summary_format(const char *m, unsigned long f, const rm_rt_spread_t *a, const rm_rt_spread_t *i, unsigned long ba, unsigned long bt, char *b, size_t l) { (void)m; (void)f; (void)a; (void)i; (void)ba; (void)bt; if (l > 1) { b[0] = 'x'; b[1] = 0; } return -1; }
