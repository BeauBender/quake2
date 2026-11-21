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
 * The ray-traced presentation's data at map load (TASK187, PM5): the two
 * hooks of rm_model.c. Gathered only while r_rt_stats is not 0; nothing
 * draws it yet.
 *
 * =======================================================================
 */

#ifndef RM_RTDATA_H
#define RM_RTDATA_H

/* from Mod_LoadBrushModel, the map file still in memory and its lumps checked: the lights and the emitters */
void RM_RTD_BrushLoaded(const char *name, const byte *mod_base, const dheader_t *header);

/* from RI_EndRegistration, the sky set: the r_rt_stats 1 line, once a load */
void RM_RTD_MapLoaded(void);

#endif
