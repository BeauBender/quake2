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
 * The ray-tracing scene of ref_metal, RT1 (TASK53;
 * docs/plans/metal-raytracing-design.md, section 1): the r_rt_* cvars,
 * the map's structures from the retained world buffers at map load, each
 * alias model's frames and triangle list on its first visible frame, and
 * per frame the instance list of the refdef with the lerp factors of its
 * alias instances, handed to the back end (rmb_rt.m) before the 3D pass.
 * The decisions are rm_rt.c's (host-tested); this file walks the
 * renderer's models and entities. Nothing reads the structures yet, and
 * nothing here runs while rm_rt_wanted says no.
 *
 * =======================================================================
 */

#include "header/local.h"
#include "header/rm_rt.h"
#include "header/rm_rtscene.h"

#define RT_MAX_AMODELS 512   /* rm_model.c's MAX_MOD_KNOWN */
#define RT_TIMES 8192        /* the frames whose GPU times the summary spreads */

_Static_assert(sizeof(dtriangle_t) == 6 * sizeof(short), "an MD2 triangle is 6 shorts, index_xyz first");
_Static_assert(offsetof(daliasframe_t, verts) == RM_MD2_FRAME_HEADER, "a frame's vertices follow its 40-byte header");

static cvar_t *r_rt_stats;
static cvar_t *r_rt_gunshadow;

static int rt_device;           /* rmb_caps_t.raytracing */
static int rt_opened;           /* rmb_rt_open succeeded */
static int rt_told;             /* the no-ray-tracing line was printed */
static int rt_refused;          /* rmb_rt_open failed in this context: not tried again */
static int rt_scene_ready;      /* this frame's instance structure was built (RT3 traces it) */

/* the map the structures were built from (or refused for: rt_map_ok 0) */
static const model_t *rt_world_mod;
static const rm_world_t *rt_world;
static int rt_map_ok;
static char rt_map[MAX_QPATH];
static unsigned char rt_inline_ok[MAX_MAP_MODELS];

/* the alias models the back end holds: mid, or -1 for a model refused */
typedef struct
{
	const model_t *mod;
	int mid;
} rt_amodel_t;

static rt_amodel_t rt_amodels[RT_MAX_AMODELS];

/* the frames since the map's structures were built, for the summary */
static double rt_alias_ms[RT_TIMES], rt_ias_ms[RT_TIMES];
static unsigned long rt_ntimes, rt_frames, rt_busy_aliases, rt_busy_tris;

void
RM_RT_Init(const rmb_caps_t *caps)
{
	r_rt_stats = ri.Cvar_Get("r_rt_stats", "0", 0);
	r_rt_gunshadow = ri.Cvar_Get("r_rt_gunshadow", "0", CVAR_ARCHIVE);
	rt_device = caps->raytracing != 0;
	rt_told = 0;
	rt_refused = 0;
	rt_opened = 0;
	rt_world_mod = NULL;
	rt_world = NULL;
	rt_map_ok = 0;
	memset(rt_amodels, 0, sizeof(rt_amodels));
	rt_ntimes = rt_frames = rt_busy_aliases = rt_busy_tris = 0;
	rt_scene_ready = 0;
	RM_RTS_Init(caps);   /* r_rt_shadows (RT3, TASK55) */
}

/* the summary of the frames since the map's structures were built, when there were any; the counts start again */
static void
RM_RT_Summary(void)
{
	char line[RM_STATS_LINE];
	rm_rt_spread_t sa, si;

	if (rt_world_mod && rt_frames)
	{
		rm_rt_spread(rt_alias_ms, rt_ntimes, &sa);
		rm_rt_spread(rt_ias_ms, rt_ntimes, &si);

		if (rm_rt_summary_format(rt_map, rt_frames, &sa, &si, rt_busy_aliases, rt_busy_tris, line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}

	rt_ntimes = rt_frames = rt_busy_aliases = rt_busy_tris = 0;
}

/* releases every ray-tracing object; the ledger counts them destroyed */
static void
RM_RT_FreeAll(void)
{
	RM_RT_Summary();
	RM_RTS_Shutdown();   /* the shadows' objects first: they use the structures */

	if (rt_opened)
	{
		rmb_rt_close();
	}

	rt_opened = 0;
	rt_scene_ready = 0;
	rt_world_mod = NULL;
	rt_world = NULL;
	rt_map_ok = 0;
	memset(rt_amodels, 0, sizeof(rt_amodels));
	memset(rt_inline_ok, 0, sizeof(rt_inline_ok));
}

void
RM_RT_Shutdown(void)
{
	if (r_rt_stats)
	{
		RM_RT_FreeAll();
	}
}

void
RM_RT_ModelFree(const model_t *mod)
{
	int i;

	if (!mod)
	{
		return;
	}

	if (mod == rt_world_mod)
	{
		RM_RT_Summary();

		if (rt_opened)
		{
			rmb_rt_free_map();
		}

		rt_world_mod = NULL;
		rt_world = NULL;
		rt_map_ok = 0;
		memset(rt_inline_ok, 0, sizeof(rt_inline_ok));
	}

	for (i = 0; i < RT_MAX_AMODELS; i++)
	{
		if (rt_amodels[i].mod == mod)
		{
			if ((rt_amodels[i].mid > 0) && rt_opened)
			{
				rmb_rt_model_free(rt_amodels[i].mid);
			}

			rt_amodels[i].mod = NULL;
			rt_amodels[i].mid = 0;
		}
	}
}

/*
 * The device check: 1 when structures are to be built. Without ray tracing
 * on the device (the build VM) it is 0 whatever the cvars say, and every
 * hook returns before it builds or allocates anything.
 */
static int
RM_RT_Wanted(void)
{
	return (r_rt_stats && rm_rt_wanted(r_rt_stats->value, rt_device)) || RM_RTS_Wanted();
}

int
RM_RT_SceneReady(void)
{
	return rt_scene_ready;
}

static int
RM_RT_Open(void)
{
	char err[256];

	if (rt_opened)
	{
		return 1;
	}

	if (rt_refused || !rmb_rt_open(err, sizeof(err)))
	{
		if (!rt_refused)
		{
			R_Printf(PRINT_ALL, "ref_metal: rt: %s; no acceleration structure is built\n", err);
		}

		rt_refused = 1;
		return 0;
	}

	rt_opened = 1;
	return 1;
}

/* the world's and the inline models' structures, from the world's retained buffers */
static void
RM_RT_BuildMap(void)
{
	static rmb_rt_range_t ranges[MAX_MAP_MODELS];
	const model_t *wm = r_worldmodel;
	const rm_world_t *w = wm->world;
	unsigned long ntris_all = w->size.indices / 3;
	rm_rt_map_stats_t s;
	unsigned char *mat;
	int i, nsub = wm->numsubmodels;

	if (rt_world_mod)
	{
		RM_RT_ModelFree(rt_world_mod);
	}

	rt_world_mod = wm;
	rt_world = w;
	rt_map_ok = 0;
	Q_strlcpy(rt_map, wm->name, sizeof(rt_map));
	memset(rt_inline_ok, 0, sizeof(rt_inline_ok));
	memset(&s, 0, sizeof(s));

	if ((nsub < 1) || (nsub > MAX_MAP_MODELS) || (ntris_all == 0) || !(mat = malloc(ntris_all)))
	{
		R_Printf(PRINT_ALL, "ref_metal: rt: %s: no structure is built (%d models, %lu triangles)\n", rt_map, nsub,
				ntris_all);
		return;
	}

	if (rm_rt_materials(w->recs, (long)w->size.surfs, w->size.indices, mat, ntris_all) != (long)ntris_all)
	{
		R_Printf(PRINT_ALL, "ref_metal: rt: %s: the surface records do not cover the index buffer; no structure is built\n",
				rt_map);
		free(mat);
		return;
	}

	for (i = 0; i < nsub; i++)
	{
		if (!rm_rt_range(wm->submodels[i].firstindex, wm->submodels[i].numindices, w->size.indices,
				&ranges[i].first_tri, &ranges[i].ntris))
		{
			ranges[i].first_tri = ranges[i].ntris = 0;
		}
	}

	if (!rmb_rt_build_map(w->vbuf, w->ibuf, ranges, nsub, mat, ntris_all, &s))
	{
		R_Printf(PRINT_ALL, "ref_metal: rt: %s: the structures could not be built\n", rt_map);
		free(mat);
		return;
	}

	rm_rt_count(mat, ranges[0].first_tri, ranges[0].ntris, &s.world);

	for (i = 1; i < nsub; i++)
	{
		rm_rt_tris_t t;
		int k;

		if (!ranges[i].ntris)
		{
			continue;
		}

		rt_inline_ok[i] = 1;
		rm_rt_count(mat, ranges[i].first_tri, ranges[i].ntris, &t);
		s.inline_models++;
		s.inlines.total += t.total;

		for (k = 0; k < RM_RT_MATS; k++)
		{
			s.inlines.tris[k] += t.tris[k];
		}
	}

	free(mat);
	rt_map_ok = 1;
	RM_RTS_MapBuilt(wm);   /* the last map's shadow tables go; this map's are built when the shadows are on (RT3) */
	rt_ntimes = rt_frames = rt_busy_aliases = rt_busy_tris = 0;

	{
		char line[RM_STATS_LINE];

		if (rm_rt_map_format(rt_map, &s, line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}
}

void
RM_RT_MapLoaded(void)
{
	if (!RM_RT_Wanted() || !r_worldmodel || !r_worldmodel->world || (rt_world == r_worldmodel->world))
	{
		return;
	}

	if (RM_RT_Open())
	{
		RM_RT_BuildMap();
	}
}

/* the back end's id of an alias model's frames and triangles, made on the model's first visible frame; 0 when refused */
static int
RM_RT_AliasModel(const model_t *mod)
{
	static unsigned short idx[3 * RM_RT_MD2_MAX_TRIS];
	const dmdl_t *h = (const dmdl_t *)mod->extradata;
	int i, slot = -1, mid = 0;
	size_t fb = 0;

	for (i = 0; i < RT_MAX_AMODELS; i++)
	{
		if (rt_amodels[i].mod == mod)
		{
			return (rt_amodels[i].mid > 0) ? rt_amodels[i].mid : 0;
		}

		if (!rt_amodels[i].mod && (slot < 0))
		{
			slot = i;
		}
	}

	if (slot < 0)
	{
		return 0;
	}

	/* the model rm_md2_check accepted (rm_model.c), as R_DrawAliasModel takes it; its triangles checked here */
	if (h && (mod->md2.verts > 0) && (h->num_xyz == mod->md2.num_xyz) && (h->num_frames > 0) &&
		rm_rt_frames_bytes(h->num_frames, h->framesize, RM_MD2_FRAME_HEADER, h->num_xyz, RMB_STATIC_MAX_BYTES, &fb) &&
		(rm_rt_alias_indices((const short *)((const byte *)h + h->ofs_tris), (long)(sizeof(dtriangle_t) / sizeof(short)),
			h->num_tris, h->num_xyz, idx, 3 * RM_RT_MD2_MAX_TRIS) == h->num_tris))
	{
		mid = rmb_rt_model((const byte *)h + h->ofs_frames, fb, h->num_xyz, idx, h->num_tris);
	}

	rt_amodels[slot].mod = mod;
	rt_amodels[slot].mid = mid ? mid : -1;
	return mid;
}

/* gl1's R_DrawAliasModel and R_DrawAliasFrameLerp up to the lerp: the frames as the draw bounds them, and the lerp's factors */
static void
RM_RT_Lerp(const entity_t *e, int mid, rmb_rt_lerp_t *l)
{
	const dmdl_t *h = (const dmdl_t *)e->model->extradata;
	const daliasframe_t *frame, *oldframe;
	vec3_t delta, vectors[3];
	int f = e->frame, of = e->oldframe;
	float backlerp = r_lerpmodels->value ? e->backlerp : 0;

	if (!(e->flags & RF_WEAPONMODEL))
	{
		rm_alias_frames(&f, &of, h->num_frames, 1);
	}

	rm_alias_frames(&f, &of, h->num_frames, 0);
	frame = (const daliasframe_t *)((const byte *)h + h->ofs_frames + (size_t)f * h->framesize);
	oldframe = (const daliasframe_t *)((const byte *)h + h->ofs_frames + (size_t)of * h->framesize);

	VectorSubtract(e->oldorigin, e->origin, delta);
	AngleVectors(e->angles, vectors[0], vectors[1], vectors[2]);
	rm_alias_lerp_setup(frame->scale, frame->translate, oldframe->scale, oldframe->translate, backlerp, delta,
			vectors[0], vectors[1], vectors[2], l->move, l->frontv, l->backv);
	l->mid = mid;
	l->frame = rm_rt_frame_vertex(f, h->num_frames, h->framesize, RM_MD2_FRAME_HEADER);
	l->oldframe = rm_rt_frame_vertex(of, h->num_frames, h->framesize, RM_MD2_FRAME_HEADER);
}

void
RM_RT_Frame(const refdef_t *fd)
{
	static rm_rt_ent_t ents[RM_MAX_ENTITIES];
	static rm_rt_inst_t inst[RM_RT_MAX_INSTANCES];
	static rmb_rt_lerp_t lerps[RM_RT_MAX_INSTANCES];
	rm_rt_frame_stats_t fs;
	rm_rt_excl_t excl;
	double ta[RMB_FRAMES_IN_FLIGHT], ti[RMB_FRAMES_IN_FLIGHT];
	int i, n, ne, nl = 0, k, gun;

	rt_scene_ready = 0;

	/* the device check (and the off state): nothing below runs without ray tracing or with every setting at its default */
	if (!RM_RT_Wanted())
	{
		if (rt_opened)
		{
			RM_RT_FreeAll();
		}

		if (r_rt_stats && r_rt_stats->value && !rt_device && !rt_told)
		{
			rt_told = 1;
			R_Printf(PRINT_ALL, "ref_metal: rt: this device has no ray tracing; no acceleration structure is built\n");
		}

		return;
	}

	/* a view without the world (the player setup menu's model) takes no ray tracing (design section 2) */
	if (!fd || (fd->rdflags & RDF_NOWORLDMODEL) || !r_worldmodel || !r_worldmodel->world || !RM_RT_Open())
	{
		return;
	}

	if (rt_world != r_worldmodel->world)
	{
		RM_RT_BuildMap();
	}

	if (!rt_map_ok)
	{
		return;
	}

	gun = r_rt_gunshadow->value != 0;
	ne = r_drawentities->value ? rm_entity_count(fd->num_entities) : 0;

	for (i = 0; i < ne; i++)
	{
		const entity_t *e = &fd->entities[i];
		const model_t *m = e->model;
		rm_rt_ent_t *r = &ents[i];

		memset(r, 0, sizeof(*r));
		r->flags = e->flags;
		VectorCopy(e->origin, r->origin);
		VectorCopy(e->angles, r->angles);
		VectorCopy(e->oldorigin, r->oldorigin);
		r->backlerp = e->backlerp;
		r->alpha = e->alpha;

		if (!m)
		{
			r->kind = RM_RT_ENT_NULL;
		}
		else if (m->type == mod_alias)
		{
			r->kind = RM_RT_ENT_ALIAS;

			/* the model's buffers only for an entity rm_rt_instances can keep */
			if (!(e->flags & (RF_BEAM | RM_RF_SHELLS)) && (!(e->flags & RF_WEAPONMODEL) || gun))
			{
				r->model = RM_RT_AliasModel(m);
			}
		}
		else if (m->type == mod_brush)
		{
			const long sub = (long)(m - r_worldmodel->submodels);

			r->kind = RM_RT_ENT_BRUSH;
			r->model = ((m >= r_worldmodel->submodels) && (sub > 0) && (sub < r_worldmodel->numsubmodels) &&
				rt_inline_ok[sub]) ? (int)sub : 0;
		}
		else
		{
			r->kind = RM_RT_ENT_SPRITE;
		}
	}

	n = rm_rt_instances(ents, ne, 1, gun, gl_lefthand->value, inst, RM_RT_MAX_INSTANCES, &excl);

	for (i = 0; i < n; i++)
	{
		if (inst[i].kind == RM_RT_INST_ALIAS)
		{
			RM_RT_Lerp(&fd->entities[inst[i].ent], inst[i].model, &lerps[nl++]);
		}
	}

	memset(&fs, 0, sizeof(fs));
	fs.excl = excl;
	fs.alias_ms = fs.ias_ms = -1;

	if (!rmb_rt_frame(inst, n, lerps, nl, RM_RTS_Wanted(), &fs))
	{
		return;
	}

	rt_scene_ready = 1;

	k = rmb_rt_times(ta, ti, RMB_FRAMES_IN_FLIGHT);

	for (i = 0; i < k; i++)
	{
		if (rt_ntimes < RT_TIMES)
		{
			rt_alias_ms[rt_ntimes] = ta[i];
			rt_ias_ms[rt_ntimes] = ti[i];
			rt_ntimes++;
		}
	}

	if (k)
	{
		fs.alias_ms = ta[k - 1];
		fs.ias_ms = ti[k - 1];
	}

	rt_frames++;

	if ((fs.aliases > rt_busy_aliases) || ((fs.aliases == rt_busy_aliases) && (fs.alias_tris > rt_busy_tris)))
	{
		rt_busy_aliases = fs.aliases;
		rt_busy_tris = fs.alias_tris;
	}

	if (r_rt_stats->value >= 2)
	{
		char line[RM_STATS_LINE];

		if (rm_rt_frame_format(&fs, line, sizeof(line)))
		{
			R_Printf(PRINT_ALL, "%s\n", line);
		}
	}
}
