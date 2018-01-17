/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the PCjs JSON floppy image format.
 *
 * Version:	@(#)fdd_json.c	1.0.10	2018/01/16
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "../86box.h"
#include "../plat.h"
#include "fdd.h"
#include "fdc.h"
#include "fdd_common.h"
#include "fdd_json.h"


#define NTRACKS			256
#define NSIDES			2
#define NSECTORS		256


typedef struct {
    uint8_t	track,			/* ID: track number */
		side,			/*     side number */
		sector;			/*     sector number 1.. */
    uint16_t	size;			/* encoded size of sector */
    uint8_t	*data;			/* allocated data for it */
} sector_t;

typedef struct {
    FILE	*f;

    /* Geometry. */
    uint8_t	tracks,			/* number of tracks */
		sides,			/* number of sides */
		sectors,		/* number of sectors per track */
		spt[NTRACKS][NSIDES];	/* number of sectors per track */

    uint8_t	track,			/* current track */
		side,			/* current side */
		sector[NSIDES];		/* current sector */

    uint8_t	dmf;			/* disk is DMF format */
    uint8_t	interleave;
#if 0
    uint8_t	skew;
#endif
    uint8_t	gap2_len;
    uint8_t	gap3_len;
    int		track_width;

    uint16_t	disk_flags,		/* flags for the entire disk */
		track_flags;		/* flags for the current track */

    uint8_t	interleave_ordered[NTRACKS][NSIDES];

    sector_t	sects[NTRACKS][NSIDES][NSECTORS];
} json_t;


static json_t	images[FDD_NUM];


static void
handle(json_t *img, char *name, char *str)
{
    sector_t *sec = NULL;
    uint32_t l, pat;
    uint8_t *p;
    char *sp;
    int i, s;

    /* Point to the currently selected sector. */
    sec = &img->sects[img->track][img->side][img->dmf-1];

    /* If no name given, assume sector is done. */
    if (name == NULL) {
	/* If no buffer, assume one with 00's. */
	if (sec->data == NULL) {
		sec->data = (uint8_t *)malloc(sec->size);
		memset(sec->data, 0x00, sec->size);
	}

	/* Encode the sector size. */
	sec->size = fdd_sector_size_code(sec->size);

	/* Set up the rest of the Sector ID. */
	sec->track = img->track;
	sec->side = img->side;

	return;
    }

    if (! strcmp(name, "sector")) {
	sec->sector = atoi(str);
	sec->size = 512;
    } else if (! strcmp(name, "length")) {
	sec->size = atoi(str);
    } else if (! strcmp(name, "pattern")) {
	pat = atol(str);

	if (sec->data == NULL)
		sec->data = (uint8_t *)malloc(sec->size);
	p = sec->data;
	s = (sec->size / sizeof(uint32_t));
	for (i=0; i<s; i++) {
		l = pat;
		*p++ = (l & 0x000000ff);
		l >>= 8;
		*p++ = (l & 0x000000ff);
		l >>= 8;
		*p++ = (l & 0x000000ff);
		l >>= 8;
		*p++ = (l & 0x000000ff);
	}
    } else if (! strcmp(name, "data")) {
	if (sec->data == NULL)
		sec->data = (uint8_t *)malloc(sec->size);
	p = sec->data;
	while (str && *str) {
		sp = strchr(str, ',');
		if (sp != NULL) *sp++ = '\0';
		l = atol(str);

		*p++ = (l & 0x000000ff);
		l >>= 8;
		*p++ = (l & 0x000000ff);
		l >>= 8;
		*p++ = (l & 0x000000ff);
		l >>= 8;
		*p++ = (l & 0x000000ff);

		str = sp;
	}
    }
}


static int
unexpect(int c, int state, int level)
{
    pclog("JSON: Unexpected '%c' in state %d/%d.\n", c, state, level);

    return(-1);
}


static int
load_image(json_t *img)
{
    char buff[4096], name[32];
    int c, i, j, state, level;
    char *ptr;

    if (img->f == NULL) {
	pclog("JSON: no file loaded!\n");
	return(0);
    }

    /* Initialize. */
    for (i=0; i<NTRACKS; i++) {
	for (j=0; j<NSIDES; j++)
		memset(img->sects[i][j], 0x00, sizeof(sector_t));
    }
    img->track = img->side = img->dmf = 0;    /* "dmf" is "sector#" */

    /* Now run the state machine. */
    ptr = NULL;
    level = state = 0;
    while (state >= 0) {
	/* Get a character from the input. */
	c = fgetc(img->f);
	if ((c == EOF) || ferror(img->f)) {
		state = -1;
		break;
	}

	/* Process it. */
	switch(state) {
		case 0:		/* read level header */
			img->dmf = 1;
			if (c != '[') {
				state = unexpect(c, state, level);
			} else {
				if (++level == 3)
					state++;
			}
			break;

		case 1:		/* read sector header */
			if (c != '{')
				state = unexpect(c, state, level);
			  else
				state++;
			break;

		case 2:		/* begin sector data name */
			if (c != '\"') {
				state = unexpect(c, state, level);
			} else {
				ptr = name;
				state++;
			}
			break;

		case 3:		/* read sector data name */
			if (c == '\"') {
				*ptr = '\0';
				state++;
			} else {
				*ptr++ = c;
			}
			break;

		case 4:		/* end of sector data name */
			if (c != ':') {
				state = unexpect(c, state, level);
			} else {
				ptr = buff;
				state++;
			}
			break;

		case 5:		/* read sector value data */
			switch(c) {
				case ',':
				case '}':
					*ptr = '\0';
					handle(img, name, buff);

					if (c == '}')
						state = 7; /* done */
					  else
						state = 2; /* word */
					break;

				case '[':
					state++;
					break;

				default:
					*ptr++ = c;
			}
			break;

		case 6:		/* read sector data complex */
			if (c != ']')
				*ptr++ = c;
			  else
				state = 5;
			break;

		case 7:		/* sector done */
			handle(img, NULL, NULL);
			switch(c) {
				case ',':	/* next sector */
					img->dmf++;
					state = 1;
					break;

				case ']':	/* all sectors done */
					if (--level == 0)
						state = -1;
					  else state++;
					break;

				default:
					state = unexpect(c, state, level);
			}
			break;

		case 8:		/* side done */
			switch(c) {
				case ',':	/* next side */
					state = 0;
					break;

				case ']':	/* all sides done */
					if (--level == 0)
						state = -1;
					  else state++;
					break;

				default:
					state = unexpect(c, state, level);
			}
			img->spt[img->track][img->side] = img->dmf;
			img->side++;
			break;

		case 9:		/* track done */
			switch(c) {
				case ',':	/* next track */
					img->side = 0;
					state = 0;
					break;

				case ']':	/* all tracks done */
					if (--level == 0)
						state = -1;
					  else state++;
					break;

				default:
					state = unexpect(c, state, level);
			}
			img->track++;
			break;
	}

    }

    /* Save derived values. */
    img->tracks = img->track;
    img->sides = img->side;

    return(1);
}


/* Seek the heads to a track, and prepare to read data from that track. */
static void
json_seek(int drive, int track)
{
    uint8_t id[4] = { 0,0,0,0 };
    json_t *img = &images[drive];
    int side, sector;
    int rate, gap2, gap3, pos;
    int ssize, rsec, asec;
    int interleave_type;

    if (img->f == NULL) {
	pclog("JSON: seek: no file loaded!\n");
	return;
    }

    /* Allow for doublestepping tracks. */
    if (! img->track_width && fdd_doublestep_40(drive)) track /= 2;

    /* Set the new track. */
    img->track = track;
    d86f_set_cur_track(drive, track);

    /* Reset the 86F state machine. */
    d86f_reset_index_hole_pos(drive, 0);
    d86f_reset_index_hole_pos(drive, 1);
    d86f_zero_bit_field(drive, 0);
    d86f_zero_bit_field(drive, 1);

    interleave_type = 0;

    if (track > img->tracks) {
	d86f_zero_track(drive);
	return;
    }

    for (side=0; side<img->sides; side++) {
	/* Get transfer rate for this side. */
	rate = img->track_flags & 0x07;
	if (!rate && (img->track_flags & 0x20)) rate = 4;

	/* Get correct GAP3 value for this side. */
	gap3 = fdd_get_gap3_size(rate,
				    img->sects[track][side][0].size,
				    img->spt[track][side]);

	/* Get correct GAP2 value for this side. */
	gap2 = ((img->track_flags & 0x07) >= 3) ? 41 : 22;

	pos = d86f_prepare_pretrack(drive, side, 0);

	for (sector=0; sector<img->spt[track][side]; sector++) {
		if (interleave_type == 0) {
			rsec = img->sects[track][side][sector].sector;
			asec = sector;
		} else {
			rsec = fdd_dmf_r[sector];
			asec = img->interleave_ordered[rsec][side];
		}
		id[0] = track;
		id[1] = side;
		id[2] = rsec;
		id[3] = img->sects[track][side][asec].size;
		ssize = fdd_sector_code_size(img->sects[track][side][asec].size);

		pos = d86f_prepare_sector(
				drive, side, pos, id,
				img->sects[track][side][asec].data,
				ssize, gap2, gap3,
				0,	/*deleted flag*/
				0	/*bad_crc flag*/
			);

		if (sector == 0)
		  d86f_initialize_last_sector_id(drive,id[0],id[1],id[2],id[3]);
	}
    }
}


static uint16_t
disk_flags(int drive)
{
    return(images[drive].disk_flags);
}


static uint16_t
track_flags(int drive)
{
    return(images[drive].track_flags);
}


static void
set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    json_t *img = &images[drive];
    int i;

    img->sector[side] = 0;

    /* Make sure we are on the desired track. */
    if (c != img->track) return;

    /* Set the desired side. */
    img->side = side;

    /* Now loop over all sector ID's on this side to find our sector. */
    for (i=0; i<img->spt[c][side]; i++) {
	if ((img->sects[img->track][side][i].track == c) &&
	    (img->sects[img->track][side][i].side == h) &&
	    (img->sects[img->track][side][i].sector == r) &&
	    (img->sects[img->track][side][i].size == n)) {
		img->sector[side] = i;
	}
    }
}


static uint8_t
poll_read_data(int drive, int side, uint16_t pos)
{
    json_t *img = &images[drive];
    uint8_t sec = img->sector[side];

    return(img->sects[img->track][side][sec].data[pos]);
}


void
json_init(void)
{
    memset(images, 0x00, sizeof(images));
}


void
json_load(int drive, wchar_t *fn)
{
    json_t *img = &images[drive];
    sector_t *sec;
    double bit_rate;
    int temp_rate;
    int i;

    /* Just in case- remove ourselves from 86F. */
    d86f_unregister(drive);

    /* Zap any old data. */
    memset(img, 0x00, sizeof(json_t));

    /* Open the image file. */
    img->f = plat_fopen(fn, L"rb");
    if (img->f == NULL) {
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    /* Our images are always RO. */
    writeprot[drive] = 1;

    /* Load all sectors from the image file. */
    if (! load_image(img)) {
	pclog("JSON: failed to initialize\n");
	(void)fclose(img->f);
	img->f = NULL;
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    pclog("JSON(%d): %ls (%i tracks, %i sides, %i sectors)\n",
	drive, fn, img->tracks, img->sides, img->spt[0][0]);

    /*
     * If the image has more than 43 tracks, then
     * the tracks are thin (96 tpi).
     */
    img->track_width = (img->tracks > 43) ? 1 : 0;

    /* If the image has 2 sides, mark it as such. */
    img->disk_flags = 0x00;
    if (img->sides == 2)
	img->disk_flags |= 0x08;

    /* JSON files are always assumed to be MFM-encoded. */
    img->track_flags = 0x08;

    img->interleave = 0;
#if 0
    img->skew = 0;
#endif

    temp_rate = 0xff;
    sec = &img->sects[0][0][0];
    for (i=0; i<6; i++) {
	if (img->spt[0][0] > fdd_max_sectors[sec->size][i]) continue;

	bit_rate = fdd_bit_rates_300[i];
	temp_rate = fdd_rates[i];
	img->disk_flags |= (fdd_holes[i] << 1);

	if ((bit_rate == 500.0) && (img->spt[0][0] == 21) &&
	    (sec->size == 2) && (img->tracks >= 80) &&
	    (img->tracks <= 82) && (img->sides == 2)) {
		/*
		 * This is a DMF floppy, set the flag so
		 * we know to interleave the sectors.
		 */
		img->dmf = 1;
	} else {
		if ((bit_rate == 500.0) && (img->spt[0][0] == 22) &&
		    (sec->size == 2) && (img->tracks >= 80) &&
		    (img->tracks <= 82) && (img->sides == 2)) {
			/*
			 * This is marked specially because of the
			 * track flag (a RPM slow down is needed).
			 */
			img->interleave = 2;
		}

		img->dmf = 0;
	}

	break;
    }

    if (temp_rate == 0xff) {
	pclog("JSON: invalid image (temp_rate=0xff)\n");
	(void)fclose(img->f);
	img->f = NULL;
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    if (img->interleave == 2) {
	img->interleave = 1;
	img->disk_flags |= 0x60;
    }

    img->gap2_len = (temp_rate == 3) ? 41 : 22;
    if (img->dmf) {
	img->gap3_len = 8;
    } else {
	img->gap3_len = fdd_get_gap3_size(temp_rate,sec->size,img->spt[0][0]);
    }

    if (! img->gap3_len) {
	pclog("JSON: image of unknown format was inserted into drive %c:!\n",
								'C'+drive);
	(void)fclose(img->f);
	img->f = NULL;
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    img->track_flags |= (temp_rate & 0x03);	/* data rate */
    if (temp_rate & 0x04)
	img->track_flags |= 0x20;		/* RPM */

    pclog("      disk_flags: 0x%02x, track_flags: 0x%02x, GAP3 length: %i\n",
	img->disk_flags, img->track_flags, img->gap3_len);
    pclog("      bit rate 300: %.2f, temporary rate: %i, hole: %i, DMF: %i\n",
		bit_rate, temp_rate, (img->disk_flags >> 1), img->dmf);

    /* Set up handlers for 86F layer. */
    d86f_handler[drive].disk_flags = disk_flags;
    d86f_handler[drive].side_flags = track_flags;
    d86f_handler[drive].writeback = null_writeback;
    d86f_handler[drive].set_sector = set_sector;
    d86f_handler[drive].read_data = poll_read_data;
    d86f_handler[drive].write_data = null_write_data;
    d86f_handler[drive].format_conditions = null_format_conditions;
    d86f_handler[drive].extra_bit_cells = null_extra_bit_cells;
    d86f_handler[drive].encoded_data = common_encoded_data;
    d86f_handler[drive].read_revolution = common_read_revolution;
    d86f_handler[drive].index_hole_pos = null_index_hole_pos;
    d86f_handler[drive].get_raw_size = common_get_raw_size;
    d86f_handler[drive].check_crc = 1;
    d86f_set_version(drive, 0x0063);

    d86f_common_handlers(drive);

    drives[drive].seek = json_seek;
}


/* Close the image. */
void
json_close(int drive)
{
    json_t *img = &images[drive];
    int t, h, s;

    /* Unlink image from the system. */
    d86f_unregister(drive);

    /* Release all the sector buffers. */
    for (t=0; t<256; t++) {
	for (h=0; h<2; h++) {
		memset(img->sects[t][h], 0x00, sizeof(sector_t));
		for (s=0; s<256; s++) {
			if (img->sects[t][h][s].data != NULL)
				free(img->sects[t][h][s].data);
			img->sects[t][h][s].data = NULL;
		}
	}
    }

    if (img->f != NULL) {
	(void)fclose(img->f);
	img->f = NULL;
    }
}
