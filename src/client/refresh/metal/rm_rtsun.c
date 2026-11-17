/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * The pure part of ref_metal's ray-traced sun and sky shadow (RT4,
 * TASK56; header/rm_rtsun.h). WIP: stubs, so the host test runs red.
 *
 * =======================================================================
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "header/rm_rtsun.h"

int rm_rtsun_on(float sun, int device_rt, float fullbright) { (void)sun; (void)device_rt; (void)fullbright; return 0; }
float rm_rtsun_scale(float v) { (void)v; return 0; }
float rm_rtsun_floor(float v) { (void)v; return 0; }
int rm_rtsun_parse_angles(const char *s, float *pitch, float *yaw) { (void)s; (void)pitch; (void)yaw; return 0; }
void rm_rtsun_dir(float pitch, float yaw, float *out) { (void)pitch; (void)yaw; out[0] = out[1] = out[2] = 0; }
int rm_rtsun_table_parse(const char *text, rm_rtsun_entry_t *out, int cap, int *nbad, int *first_bad)
{ (void)text; (void)out; (void)cap; *nbad = 0; *first_bad = 0; return 0; }
const rm_rtsun_entry_t *rm_rtsun_table_find(const rm_rtsun_entry_t *t, int n, const char *map)
{ (void)t; (void)n; (void)map; return NULL; }
int rm_rtsun_map_key(const char *model_name, char *out, size_t len) { (void)model_name; if (len) out[0] = 0; return 0; }
int rm_rtsun_sky_area(const rm_surfrec_t *recs, long nrecs, const rm_wvert_t *verts, unsigned long nverts,
		const unsigned *indices, unsigned long nindices, unsigned long first_index, unsigned long num_indices,
		double *sky, double *total)
{ (void)recs; (void)nrecs; (void)verts; (void)nverts; (void)indices; (void)nindices; (void)first_index;
	(void)num_indices; *sky = *total = 0; return 0; }
int rm_rtsun_sky_enough(double sky, double total) { (void)sky; (void)total; return 0; }
void rm_rtsun_mean(const unsigned char *rgba, size_t npix, float *out) { (void)rgba; (void)npix; out[0] = out[1] = out[2] = 0; }
void rm_rtsun_sky_color(const float (*means)[3], const int *have, float *out) { (void)means; (void)have; out[0] = out[1] = out[2] = 0; }
int rm_rtsun_show(float v) { (void)v; return 0; }
int rm_rtsun_view(const float *proj, const float *world, const rm_viewport_t *vp, const float *eye, float znear,
		const float *dir, const float *sky, float scale, float floor_, unsigned show, rm_rtsun_view_t *out)
{ (void)proj; (void)world; (void)vp; (void)eye; (void)znear; (void)dir; (void)sky; (void)scale; (void)floor_;
	(void)show; memset(out, 0, sizeof(*out)); return 0; }
int rm_rtsun_stats_format(const rm_rtsun_stats_t *s, char *buf, size_t len) { (void)s; if (len) buf[0] = 0; return 0; }
int rm_rtsun_map_format(const char *map, double sky, double total, int from_table, float pitch, float yaw,
		char *buf, size_t len)
{ (void)map; (void)sky; (void)total; (void)from_table; (void)pitch; (void)yaw; if (len) buf[0] = 0; return 0; }
