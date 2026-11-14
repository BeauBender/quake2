/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
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
 * The image registry and uploads, from gl1_image.c
 * (docs/plans/ref-metal-native.md, section 5). gl1's upload decisions as
 * the shim takes them: it advertises non-power-of-two textures, so every
 * image keeps its size (R_Upload32Native), with R_LightScaleTexture, the
 * gamma and intensity tables and the skin flood fill. Not copied: the
 * power-of-two path (R_Upload32Soft, gl1_picmip, gl1_round_down), the
 * paletted path and r_scale8bittextures (no scenario sets it). The scrap
 * is rm_scrap.c's (N3). The shared loaders in files/ parse the files.
 *
 * =======================================================================
 */

#include "header/local.h"

image_t gltextures[MAX_GLTEXTURES];
int numgltextures;
unsigned d_8to24table[256];
byte gammatable[256];

image_t *r_notexture;    /* the fallback of a missing world texture (rm_misc.c makes it) */
float rm_sw_gamma = 1;   /* gl1's gl_state.sw_gamma: vid_gamma, at least 0.1, as R_InitImages read it */
float rm_inverse_intensity_value = 0.5f;   /* gl1's gl_state.inverse_intensity: the gamma over gl1_intensity, as R_InitImages computed it */
int registration_sequence;

static byte intensitytable[256];
static int image_max = 0;
static cvar_t *intensity;

/* gl_texturemode as R_TextureMode last set it */
static int filter_min = 3, filter_mag = 1;

/* from gl1_image.c, R_LoadPic: r_2D_unfiltered and r_nolerp_list */
static qboolean
R_NoLerp(const char *name, imagetype_t type)
{
	if ((r_2D_unfiltered->value != 0.0f) && (type == it_pic))
	{
		return (r_lerp_list->string == NULL) || (strstr(r_lerp_list->string, name) == NULL);
	}

	return (gl_nolerp_list->string != NULL) && (strstr(gl_nolerp_list->string, name) != NULL);
}

/* gl1's per-texture filter state as one of the back end's samplers, at load or after a mode change */
static int
R_ImageSampler(const image_t *image, int mode_change)
{
	return rm_image_sampler((image->type != it_pic) && (image->type != it_sky),
			R_NoLerp(image->name, image->type), mode_change);
}

/* from gl1_image.c; the samplers follow the mode, per image the class does */
void
R_TextureMode(const char *string)
{
	int i, min, mag;

	if (!rm_texture_mode(string, &min, &mag))
	{
		R_Printf(PRINT_ALL, "bad filter name '%s'\n", string);
		return;
	}

	filter_min = min;
	filter_mag = mag;

	/* clamp selected anisotropy: the shim's maximum is 16 */
	if (gl_anisotropic->value > 16)
	{
		ri.Cvar_SetValue("r_anisotropic", 16);
	}

	rmb_samplers_set(filter_min, filter_mag, (int)gl_anisotropic->value);

	/* r_2D_unfiltered and r_nolerp_list may have changed which images are unfiltered */
	for (i = 0; i < numgltextures; i++)
	{
		if (gltextures[i].texnum)
		{
			gltextures[i].sampler = R_ImageSampler(&gltextures[i], 1);
		}
	}
}

/* from gl1_image.c */
void
R_ImageList_f(void)
{
	int i, used, texels;
	image_t *image;

	R_Printf(PRINT_ALL, "------------------\n");
	texels = 0;
	used = 0;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		const char *in_use = "";
		char imageType = '?';

		if (!image->texnum)
		{
			continue;
		}

		if (image->registration_sequence == registration_sequence)
		{
			in_use = "*";
			used++;
		}

		texels += image->upload_width * image->upload_height;

		switch (image->type)
		{
			case it_skin:
				imageType = 'M';
				break;
			case it_sprite:
				imageType = 'S';
				break;
			case it_wall:
				imageType = 'W';
				break;
			case it_pic:
				imageType = 'P';
				break;
			case it_sky:
				imageType = 'Y';
				break;
			default:
				break;
		}

		R_Printf(PRINT_ALL, "%c%c %3i %3i RGB: %s (%dx%d) %s\n",
				image->scrap ? 'S' : ' ', imageType, image->upload_width, image->upload_height,
				image->name, image->width, image->height, in_use);
	}

	R_Printf(PRINT_ALL, "Total texel count (not counting mipmaps): %i\n", texels);
	R_Printf(PRINT_ALL, "Used %d of %d / %d images.\n", used, image_max, MAX_GLTEXTURES);
}

/* from gl1_image.c: scale up the pixel values to increase the lighting range */
void
R_LightScaleTexture(unsigned *in, int inwidth, int inheight, qboolean only_gamma)
{
	int i, c = inwidth * inheight;
	byte *p = (byte *)in;

	for (i = 0; i < c; i++, p += 4)
	{
		if (only_gamma)
		{
			p[0] = gammatable[p[0]];
			p[1] = gammatable[p[1]];
			p[2] = gammatable[p[2]];
		}
		else
		{
			p[0] = gammatable[intensitytable[p[0]]];
			p[1] = gammatable[intensitytable[p[1]]];
			p[2] = gammatable[intensitytable[p[2]]];
		}
	}
}

/*
 * from gl1_image.c, R_Upload32Native (gl1's R_Upload32 on the shim):
 * returns has_alpha; the texture keeps its size. The size check of
 * rmb_texture_upload comes before anything is scaled or written.
 */
static qboolean
R_Upload32(image_t *image, unsigned *data, int width, int height, qboolean mipmap)
{
	size_t bytes, i, c;
	qboolean has_alpha = false;

	if (!rm_upload_bytes(width, height, RMB_STAGING_BYTES, &bytes))
	{
		R_Printf(PRINT_DEVELOPER, "%s: %s is %dx%d, too large to upload\n", __func__,
				image->name, width, height);
		rmb_upload_refused();
		return false;
	}

	c = bytes / 4;
	R_LightScaleTexture(data, width, height, !mipmap);

	for (i = 0; i < c; i++)
	{
		if (((byte *)data)[i * 4 + 3] != 255)
		{
			has_alpha = true;
			break;
		}
	}

	image->texnum = rmb_texture_upload(image->texnum, (const unsigned char *)data, width, height,
			mipmap, !has_alpha);
	image->upload_width = width;
	image->upload_height = height;
	return has_alpha;
}

/* from gl1_image.c, without the paletted sky */
static qboolean
R_Upload8(image_t *image, byte *data, int width, int height, qboolean mipmap)
{
	unsigned *trans;
	qboolean ret;
	size_t bytes;

	/* refused before the conversion allocates for it */
	if (!rm_upload_bytes(width, height, RMB_STAGING_BYTES, &bytes))
	{
		rmb_upload_refused();
		return false;
	}

	trans = R_Convert8to32(data, width, height, d_8to24table);

	if (!trans)
	{
		return false;
	}

	ret = R_Upload32(image, trans, width, height, mipmap);
	free(trans);
	return ret;
}

/*
 * from gl1_image.c, R_LoadPic: little pics go into the scrap (rm_scrap.c).
 * 1 when the pic is in it; 0 when it is not, and the pic is loaded as an
 * image of its own.
 */
static qboolean
R_LoadScrap(image_t *image, byte *pic, int width, int height, int bits, qboolean nolerp)
{
	int texnum = -1, x = 0, y = 0;

	if ((width < 1) || (height < 1) || (width > 128) || (height > 128))
	{
		return false;
	}

	if (bits == 32)
	{
		texnum = Scrap_AllocBlock(width, height, &x, &y, (unsigned *)pic, nolerp ? 0 : RM_MAX_SCRAPS_NOLERP);
	}
	else if (bits == 8)
	{
		unsigned *trans = R_Convert8to32(pic, width, height, d_8to24table);

		if (trans)
		{
			texnum = Scrap_AllocBlock(width, height, &x, &y, trans, nolerp ? 0 : RM_MAX_SCRAPS_NOLERP);
			free(trans);
		}
	}

	if (texnum == -1)
	{
		return false;
	}

	if (nolerp && (texnum >= RM_MAX_SCRAPS_NOLERP))
	{
		R_Printf(PRINT_ALL, "%s: Nolerp image stored to lerp\n", image->name);
	}

	image->texnum = RM_TEXNUM_SCRAP;   /* not the atlas's id: a failed upload can free it */
	image->scrap = true;
	image->scrapnum = texnum;
	image->has_alpha = true;
	image->sl = (float)x / RM_SCRAP_WIDTH;
	image->sh = (float)(x + width) / RM_SCRAP_WIDTH;
	image->tl = (float)y / RM_SCRAP_HEIGHT;
	image->th = (float)(y + height) / RM_SCRAP_HEIGHT;
	image->upload_width = width;
	image->upload_height = height;
	return true;
}

/* from gl1_image.c: also the entry point for the generated textures */
image_t *
R_LoadPic(const char *name, byte *pic, int width, int realwidth,
		int height, int realheight, size_t data_size, imagetype_t type, int bits)
{
	image_t *image;
	qboolean mipmap = (type != it_pic) && (type != it_sky);
	int i;

	(void)data_size;

	/* find a free image_t */
	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (!image->texnum)
		{
			break;
		}
	}

	if (i == numgltextures)
	{
		if (numgltextures == MAX_GLTEXTURES)
		{
			ri.Sys_Error(ERR_DROP, "%s: MAX_GLTEXTURES", __func__);
		}

		numgltextures++;
	}

	image = &gltextures[i];

	if (strlen(name) >= sizeof(image->name))
	{
		ri.Sys_Error(ERR_DROP, "%s: \"%s\" is too long", __func__, name);
		return NULL;
	}

	memset(image, 0, sizeof(*image));
	strcpy(image->name, name);
	image->registration_sequence = registration_sequence;
	image->width = width;
	image->height = height;
	image->type = type;

	if ((type == it_skin) && (bits == 8))
	{
		R_FloodFillSkin(pic, width, height, d_8to24table);
	}

	/* Normalize crosshair images to white so that color tinting via
	   modulation produces the correct hue regardless of the original
	   palette color used in the crosshair PCX. */
	if (bits == 8 && (strcmp(name, "pics/ch1.pcx") == 0 ||
			strcmp(name, "pics/ch2.pcx") == 0 ||
			strcmp(name, "pics/ch3.pcx") == 0))
	{
		int best = 0, j;
		float best_lum = 0;

		for (j = 0; j < 255; j++)
		{
			byte r = ((byte *)&d_8to24table[j])[0];
			byte g = ((byte *)&d_8to24table[j])[1];
			byte b = ((byte *)&d_8to24table[j])[2];
			float lum = 0.299f * r + 0.587f * g + 0.114f * b;

			if (lum > best_lum)
			{
				best_lum = lum;
				best = j;
			}
		}

		for (j = 0; j < width * height; j++)
		{
			if (pic[j] != 255)
			{
				pic[j] = (byte)best;
			}
		}
	}

	if ((type == it_pic) && R_LoadScrap(image, pic, width, height, bits, R_NoLerp(name, type)))
	{
		/* in the atlas: its coordinates are set */
	}
	else if (bits == 8)
	{
		image->has_alpha = R_Upload8(image, pic, width, height, mipmap);
	}
	else if (bits == 32)
	{
		image->has_alpha = R_Upload32(image, (unsigned *)pic, width, height, mipmap);
	}
	else
	{
		ri.Sys_Error(ERR_DROP, "%s: texture '%s' has %d bits per pixel, only 8 and 32 supported",
				__func__, name, bits);
	}

	if (!image->texnum)
	{
		/* refused by the size check or a failed allocation: the slot stays free */
		memset(image, 0, sizeof(*image));
		return NULL;
	}

	image->sampler = R_ImageSampler(image, 0);

	if (!image->scrap)
	{
		image->sl = 0;
		image->sh = 1;
		image->tl = 0;
		image->th = 1;
	}

	if (realwidth && realheight)
	{
		if ((realwidth <= image->width) && (realheight <= image->height))
		{
			image->width = realwidth;
			image->height = realheight;
		}
		else
		{
			R_Printf(PRINT_DEVELOPER,
					"Warning, image '%s' has hi-res replacement smaller than the original! (%d x %d) < (%d x %d)\n",
					name, image->width, image->height, realwidth, realheight);
		}
	}

	return image;
}

/* from gl1_image.c: finds or loads the given image, or NULL */
image_t *
R_FindImage(const char *originname, imagetype_t type)
{
	char namewe[256], name[256] = {0};
	const char *ext;
	image_t *image;
	size_t len;
	int i;

	if (!originname)
	{
		return NULL;
	}

	Q_strlcpy(name, originname, sizeof(name));
	Q_replacebackslash(name);

	ext = COM_FileExtension(name);

	if (!ext[0])
	{
		return NULL;
	}

	len = (ext - name) - 1;

	if ((len < 1) || (len > sizeof(namewe) - 1))
	{
		R_Printf(PRINT_DEVELOPER, "%s: Bad filename %s\n", __func__, name);
		return NULL;
	}

	memcpy(namewe, name, len);
	namewe[len] = 0;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (image->texnum && !strcmp(name, image->name))
		{
			image->registration_sequence = registration_sequence;
			return image;
		}
	}

	image = (image_t *)R_LoadImage(name, namewe, ext, type,
			r_retexturing->value, (loadimage_t)R_LoadPic);

	if (!image && (r_validation->value > 0))
	{
		R_Printf(PRINT_ALL, "%s: can't load %s\n", __func__, name);
	}

	return image;
}

void
R_BeginImageRegistration(void)
{
	registration_sequence++;
}

/* from gl1_image.c: frees every image not touched in this registration sequence, but pics */
void
R_FreeUnusedImages(void)
{
	image_t *image;
	int i, used = 0;

	/* never free r_notexture (gl1 keeps the particle texture too: N9) */
	if (r_notexture)
	{
		r_notexture->registration_sequence = registration_sequence;
	}

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (image->registration_sequence == registration_sequence)
		{
			used++;
			continue;
		}

		if (!image->registration_sequence || (image->type == it_pic))
		{
			continue;
		}

		rmb_texture_free(image->texnum);
		memset(image, 0, sizeof(*image));
	}

	if (image_max < used)
	{
		image_max = used;
	}
}

/* from gl1_image.c */
qboolean
R_ImageHasFreeSpace(void)
{
	int i, used = 0;
	image_t *image;

	for (i = 0, image = gltextures; i < numgltextures; i++, image++)
	{
		if (!image->name[0])
		{
			continue;
		}

		if (image->registration_sequence == registration_sequence)
		{
			used++;
		}
	}

	if (image_max < used)
	{
		image_max = used;
	}

	/* should same size of free slots as currently used */
	return (numgltextures + used) < MAX_GLTEXTURES;
}

/* from gl1_image.c, with GL1_GAMMATABLE (gl1's header/local.h defines it) */
void
R_InitImages(void)
{
	byte *colormap = NULL;
	float g = vid_gamma->value;
	int i;

	g = Q_max(g, 0.1f);
	rm_sw_gamma = g;
	registration_sequence = 1;
	image_max = 0;

	intensity = ri.Cvar_Get("gl1_intensity", "2", CVAR_ARCHIVE);

	if (intensity->value <= 1)
	{
		ri.Cvar_Set("gl1_intensity", "1");
	}

	rm_inverse_intensity_value = rm_inverse_intensity(g, intensity->value);

	Scrap_Init();
	GetPCXPalette(&colormap, d_8to24table);
	free(colormap);

	if (g == 1)
	{
		for (i = 0; i < 256; i++)
		{
			gammatable[i] = i;
		}
	}
	else
	{
		g = 1.0f / g;

		for (i = 0; i < 256; i++)
		{
			float inf;

			inf = pow((float)(i + 0.5f) / 255.5f, g) * 255.0f + 0.5f;
			inf = Q_clamp(inf, 0, 255);
			gammatable[i] = inf;
		}
	}

	for (i = 0; i < 256; i++)
	{
		int j = i * intensity->value;

		intensitytable[i] = Q_min(j, 255);
	}

	R_TextureMode(gl_texturemode->string);
}

void
R_ShutdownImages(void)
{
	int i;

	for (i = 0; i < numgltextures; i++)
	{
		/* a scrap image has no texture of its own: the atlases are the scrap's, freed there */
		if (gltextures[i].texnum && !gltextures[i].scrap)
		{
			rmb_texture_free(gltextures[i].texnum);
		}

		memset(&gltextures[i], 0, sizeof(gltextures[i]));
	}

	numgltextures = 0;
	r_notexture = NULL;
}
