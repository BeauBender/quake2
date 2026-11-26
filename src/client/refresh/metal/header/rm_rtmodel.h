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
 * RT-primary's light on alias models and the view weapon (TASK191, PM9;
 * docs/runbook/TASK191.md, "Design"): one traced probe a model, at the
 * point below it where R_LightPoint reads the lightmap, in place of that
 * sample. The pure rules here (rm_rtmodel.c, held by
 * tests/metal/rm_rtmodel_test.c); the probes are traced by
 * rm_rtmodel.metal in the direct light's encoder (rmb_rtdir.m) and read
 * back a fixed number of frames later.
 *
 * =======================================================================
 */

#ifndef RM_RTMODEL_H
#define RM_RTMODEL_H

#include "rmb.h"

#define RM_RTM_MAX 64            /* probes a frame */
#define RM_RTM_MATCH 64.0f       /* the farthest a model's origin is from the probe's it takes */
#define RM_RTM_SAMPLES 16        /* a probe's sky, sun and bounce rays (rm_rtmodel.metal) */

/* what the kernel reads for a probe: the point and the unit normal of the face R_LightPoint met */
typedef struct
{
	float spot[3];
	float normal[3];
} rm_rtm_probe_t;

/* what a probe's result is matched by: the model, whether it is the view weapon, its origin */
typedef struct
{
	const void *model;
	int weapon;
	float origin[3];
} rm_rtm_key_t;

/* a frame's probes, in the order they were asked for */
typedef struct
{
	int n;
	rm_rtm_key_t key[RM_RTM_MAX];
	rm_rtm_probe_t probe[RM_RTM_MAX];
} rm_rtm_frame_t;

/* the results read back: each probe's key and light (the light attachment's units, 0 .. 1) */
typedef struct
{
	int n;
	rm_rtm_key_t key[RM_RTM_MAX];
	float light[RM_RTM_MAX][3];
} rm_rtm_ready_t;

/* 1 when the models take the probes: the direct light traces this view and models (r_rt_direct_models) is not 0 or NaN */
int rm_rtm_on(int direct_traced, float models);

/*
 * 1 when a model of these RF_ flags, under these RDF_ flags, takes a
 * probe's light: one gl1 lights from the lightmap (rm_alias_lit), and not
 * translucent, red under the IR goggles, or depth-hacked unless it is the
 * view weapon. The others keep the stock light.
 */
int rm_rtm_traced(int flags, int rdflags);

/*
 * The probe of R_LightPoint's trace from origin: hit its result (1 or 0
 * when a face was met, -1 when none), spot the point on that face, normal
 * its plane's normal (NULL for none). 0 when no face was met or a value is
 * not finite or the normal is 0; else 1, the normal unit and turned
 * toward origin's side of the face.
 */
int rm_rtm_probe(const float *origin, const float *spot, const float *normal, int hit, rm_rtm_probe_t *out);

/* appends a probe to f: its index, the index of one with the same key already there, or -1 when f is full */
int rm_rtm_add(rm_rtm_frame_t *f, const rm_rtm_key_t *k, const rm_rtm_probe_t *p);

/*
 * The result a model takes: of the n keys, the one of the same model and
 * kind (weapon or not) whose origin is nearest to origin, within
 * RM_RTM_MATCH; the first on a tie. -1 for none.
 */
int rm_rtm_match(const rm_rtm_key_t *keys, int n, const void *model, int weapon, const float *origin);

/* a model's light from a probe's, as R_LightPoint's: the probe's (clamped to 0 .. 1, NaN 0) times r_modulate */
void rm_rtm_point(const float *light, float modulate, float *point);

/*
 * 1 when frame fno can read a slot's results: the slot was traced by the
 * frame RMB_FRAMES_IN_FLIGHT before fno, which the frame semaphore has seen
 * completed (traced 0: never)
 */
int rm_rtm_slot_ready(unsigned long traced, unsigned long fno);

/* r_rt_stats 2's line of the models: 0 when it does not fit */
int rm_rtm_stats_format(unsigned long frame, int probes, int matched, int stock, char *buf, size_t len);

#endif
