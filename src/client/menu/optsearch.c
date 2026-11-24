/* TASK203: the settings optimizer's search; a stub until the next commit */
#include <string.h>
#include "header/optsearch.h"

const char *OptSearch_Word(int pri) { (void)pri; return ""; }
int OptSearch_Priority(const char *word) { (void)word; return -1; }
const char *OptSearch_CvarName(int cv) { (void)cv; return ""; }
int OptSearch_ParseRank(const char *text, int rank[OPT_NPRI], int *n, char *err, size_t errlen) { (void)text; (void)rank; *n = 0; if (errlen) err[0] = 0; return 0; }
int OptSearch_ParseTarget(const char *text, float *ms) { (void)text; (void)ms; return 0; }
int OptSearch_ParseStat(const char *text, int *stat) { (void)text; (void)stat; return 0; }
int OptSearch_Stats(const long long *t_us, int n, float warmup, int stat, optmeasure_t *out) { (void)t_us; (void)n; (void)warmup; (void)stat; (void)out; return 0; }
void OptSearch_Init(optsearch_t *s, const optinput_t *in) { memset(s, 0, sizeof(*s)); s->in = *in; s->best = -1; }
int OptSearch_Next(optsearch_t *s, optcand_t *c) { (void)s; (void)c; return 0; }
void OptSearch_Record(optsearch_t *s, const optmeasure_t *m) { (void)s; (void)m; }
const optcand_t *OptSearch_Result(const optsearch_t *s, int *met) { (void)s; *met = 0; return NULL; }
int OptSearch_NumLines(const optsearch_t *s) { return s->nlines; }
const char *OptSearch_Line(const optsearch_t *s, int i) { return s->lines[i]; }
void OptSearch_Describe(const optsearch_t *s, const optcand_t *c, char *buf, size_t len) { (void)s; (void)c; if (len) buf[0] = 0; }
