/*
 * VARCem	Virtual ARchaeological Computer EMulator.
 *		An emulator of (mostly) x86-based PC systems and devices,
 *		using the ISA,EISA,VLB,MCA  and PCI system buses, roughly
 *		spanning the era between 1981 and 1995.
 *
 *		This file is part of the VARCem Project.
 *
 *		Implementation of the PCjs JSON floppy image format.
 *
 * Version:	@(#)fdd_json.c	1.0.7	2019/12/05
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017-2019 Fred N. van Kempen.
 *
 *		Redistribution and  use  in source  and binary forms, with
 *		or  without modification, are permitted  provided that the
 *		following conditions are met:
 *
 *		1. Redistributions of  source  code must retain the entire
 *		   above notice, this list of conditions and the following
 *		   disclaimer.
 *
 *		2. Redistributions in binary form must reproduce the above
 *		   copyright  notice,  this list  of  conditions  and  the
 *		   following disclaimer in  the documentation and/or other
 *		   materials provided with the distribution.
 *
 *		3. Neither the  name of the copyright holder nor the names
 *		   of  its  contributors may be used to endorse or promote
 *		   products  derived from  this  software without specific
 *		   prior written permission.
 *
 * THIS SOFTWARE  IS  PROVIDED BY THE  COPYRIGHT  HOLDERS AND CONTRIBUTORS
 * "AS IS" AND  ANY EXPRESS  OR  IMPLIED  WARRANTIES,  INCLUDING, BUT  NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE  ARE  DISCLAIMED. IN  NO  EVENT  SHALL THE COPYRIGHT
 * HOLDER OR  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL,  EXEMPLARY,  OR  CONSEQUENTIAL  DAMAGES  (INCLUDING,  BUT  NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE  GOODS OR SERVICES;  LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED  AND ON  ANY
 * THEORY OF  LIABILITY, WHETHER IN  CONTRACT, STRICT  LIABILITY, OR  TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING  IN ANY  WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../plat.h"
#include "fdd.h"
#include "fdd_86f.h"
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


static json_t	*images[FDD_NUM];


#ifdef ENABLE_JSON_LOG
int json_do_log = ENABLE_JSON_LOG;


static void
json_log(const char *fmt, ...)
{
   va_list ap;

   if (json_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define json_log(fmt, ...)
#endif


static void
handle(json_t *dev, char *name, char *str)
{
    sector_t *sec = NULL;
    uint32_t l, pat;
    uint8_t *p;
    char *sp;
    int i, s;

    /* Point to the currently selected sector. */
    sec = &dev->sects[dev->track][dev->side][dev->dmf-1];

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
	sec->track = dev->track;
	sec->side = dev->side;

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
    json_log("JSON: Unexpected '%c' in state %d/%d.\n", c, state, level);

    return(-1);
}


static int
load_image(json_t *dev)
{
    char buff[4096], name[32];
    int c, i, j, state, level;
    char *ptr;

    if (dev->f == NULL) {
	json_log("JSON: no file loaded!\n");
	return(0);
    }

    /* Initialize. */
    for (i=0; i<NTRACKS; i++) {
	for (j=0; j<NSIDES; j++)
		memset(dev->sects[i][j], 0x00, sizeof(sector_t));
    }
    dev->track = dev->side = dev->dmf = 0;    /* "dmf" is "sector#" */

    /* Now run the state machine. */
    ptr = NULL;
    level = state = 0;
    while (state >= 0) {
	/* Get a character from the input. */
	c = fgetc(dev->f);
	if ((c == EOF) || ferror(dev->f)) {
		state = -1;
		break;
	}

	/* Process it. */
	switch(state) {
		case 0:		/* read level header */
			dev->dmf = 1;
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
					handle(dev, name, buff);

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
			handle(dev, NULL, NULL);
			switch(c) {
				case ',':	/* next sector */
					dev->dmf++;
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
			dev->spt[dev->track][dev->side] = dev->dmf;
			dev->side++;
			break;

		case 9:		/* track done */
			switch(c) {
				case ',':	/* next track */
					dev->side = 0;
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
			dev->track++;
			break;
	}

    }

    /* Save derived values. */
    dev->tracks = dev->track;
    dev->sides = dev->side;

    return(1);
}


/* Seek the heads to a track, and prepare to read data from that track. */
static void
json_seek(int drive, int track)
{
    uint8_t id[4] = { 0,0,0,0 };
    json_t *dev = images[drive];
    int side, sector;
    int rate, gap2, gap3, pos;
    int ssize, rsec, asec;
    int interleave_type;

    if (dev->f == NULL) {
	json_log("JSON: seek: no file loaded!\n");
	return;
    }

    /* Allow for doublestepping tracks. */
    if (! dev->track_width && fdd_doublestep_40(drive)) track /= 2;

    /* Set the new track. */
    dev->track = track;
    d86f_set_cur_track(drive, track);

    /* Reset the 86F state machine. */
    d86f_reset_index_hole_pos(drive, 0);
    d86f_destroy_linked_lists(drive, 0);
    d86f_reset_index_hole_pos(drive, 1);
    d86f_destroy_linked_lists(drive, 1);

    interleave_type = 0;

    if (track > dev->tracks) {
	d86f_zero_track(drive);
	return;
    }

    for (side=0; side<dev->sides; side++) {
	/* Get transfer rate for this side. */
	rate = dev->track_flags & 0x07;
	if (!rate && (dev->track_flags & 0x20)) rate = 4;

	/* Get correct GAP3 value for this side. */
	gap3 = fdd_get_gap3_size(rate,
				    dev->sects[track][side][0].size,
				    dev->spt[track][side]);

	/* Get correct GAP2 value for this side. */
	gap2 = ((dev->track_flags & 0x07) >= 3) ? 41 : 22;

	pos = d86f_prepare_pretrack(drive, side, 0);

	for (sector=0; sector<dev->spt[track][side]; sector++) {
		if (interleave_type == 0) {
			rsec = dev->sects[track][side][sector].sector;
			asec = sector;
		} else {
			rsec = fdd_dmf_r[sector];
			asec = dev->interleave_ordered[rsec][side];
		}
		id[0] = track;
		id[1] = side;
		id[2] = rsec;
		if (dev->sects[track][side][asec].size > 255)
			perror("fdd_json.c: json_seek: sector size too big.");
		id[3] = dev->sects[track][side][asec].size & 0xff;
		ssize = fdd_sector_code_size(dev->sects[track][side][asec].size & 0xff);

		pos = d86f_prepare_sector(
				drive, side, pos, id,
				dev->sects[track][side][asec].data,
				ssize, gap2, gap3,
				0	/*flags*/
			);

		if (sector == 0)
		  d86f_initialize_last_sector_id(drive,id[0],id[1],id[2],id[3]);
	}
    }
}


static uint16_t
disk_flags(int drive)
{
    json_t *dev = images[drive];

    return(dev->disk_flags);
}


static uint16_t
track_flags(int drive)
{
    json_t *dev = images[drive];

    return(dev->track_flags);
}


static void
set_sector(int drive, int side, uint8_t c, uint8_t h, uint8_t r, uint8_t n)
{
    json_t *dev = images[drive];
    int i;

    dev->sector[side] = 0;

    /* Make sure we are on the desired track. */
    if (c != dev->track) return;

    /* Set the desired side. */
    dev->side = side;

    /* Now loop over all sector ID's on this side to find our sector. */
    for (i=0; i<dev->spt[c][side]; i++) {
	if ((dev->sects[dev->track][side][i].track == c) &&
	    (dev->sects[dev->track][side][i].side == h) &&
	    (dev->sects[dev->track][side][i].sector == r) &&
	    (dev->sects[dev->track][side][i].size == n)) {
		dev->sector[side] = i;
	}
    }
}


static uint8_t
poll_read_data(int drive, int side, uint16_t pos)
{
    json_t *dev = images[drive];
    uint8_t sec = dev->sector[side];

    return(dev->sects[dev->track][side][sec].data[pos]);
}


void
json_init(void)
{
    memset(images, 0x00, sizeof(images));
}


void
json_load(int drive, wchar_t *fn)
{
    double bit_rate;
    int temp_rate;
    sector_t *sec;
    json_t *dev;
    int i;

    /* Just in case- remove ourselves from 86F. */
    d86f_unregister(drive);

    /* Allocate a drive block. */
    dev = (json_t *)malloc(sizeof(json_t));
    memset(dev, 0x00, sizeof(json_t));

    /* Open the image file. */
    dev->f = plat_fopen(fn, L"rb");
    if (dev->f == NULL) {
	free(dev);
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    /* Our images are always RO. */
    writeprot[drive] = 1;

    /* Set up the drive unit. */
    images[drive] = dev;

    /* Load all sectors from the image file. */
    if (! load_image(dev)) {
	json_log("JSON: failed to initialize\n");
	(void)fclose(dev->f);
	free(dev);
	images[drive] = NULL;
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    json_log("JSON(%d): %ls (%i tracks, %i sides, %i sectors)\n",
	drive, fn, dev->tracks, dev->sides, dev->spt[0][0]);

    /*
     * If the image has more than 43 tracks, then
     * the tracks are thin (96 tpi).
     */
    dev->track_width = (dev->tracks > 43) ? 1 : 0;

    /* If the image has 2 sides, mark it as such. */
    dev->disk_flags = 0x00;
    if (dev->sides == 2)
	dev->disk_flags |= 0x08;

    /* JSON files are always assumed to be MFM-encoded. */
    dev->track_flags = 0x08;

    dev->interleave = 0;
#if 0
    dev->skew = 0;
#endif

    temp_rate = 0xff;
    sec = &dev->sects[0][0][0];
    for (i=0; i<6; i++) {
	if (dev->spt[0][0] > fdd_max_sectors[sec->size][i]) continue;

	bit_rate = fdd_bit_rates_300[i];
	temp_rate = fdd_rates[i];
	dev->disk_flags |= (fdd_holes[i] << 1);

	if ((bit_rate == 500.0) && (dev->spt[0][0] == 21) &&
	    (sec->size == 2) && (dev->tracks >= 80) &&
	    (dev->tracks <= 82) && (dev->sides == 2)) {
		/*
		 * This is a DMF floppy, set the flag so
		 * we know to interleave the sectors.
		 */
		dev->dmf = 1;
	} else {
		if ((bit_rate == 500.0) && (dev->spt[0][0] == 22) &&
		    (sec->size == 2) && (dev->tracks >= 80) &&
		    (dev->tracks <= 82) && (dev->sides == 2)) {
			/*
			 * This is marked specially because of the
			 * track flag (a RPM slow down is needed).
			 */
			dev->interleave = 2;
		}

		dev->dmf = 0;
	}

	break;
    }

    if (temp_rate == 0xff) {
	json_log("JSON: invalid image (temp_rate=0xff)\n");
	(void)fclose(dev->f);
	dev->f = NULL;
	free(dev);
	images[drive] = NULL;
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    if (dev->interleave == 2) {
	dev->interleave = 1;
	dev->disk_flags |= 0x60;
    }

    dev->gap2_len = (temp_rate == 3) ? 41 : 22;
    if (dev->dmf)
	dev->gap3_len = 8;
      else
	dev->gap3_len = fdd_get_gap3_size(temp_rate,sec->size,dev->spt[0][0]);

    if (! dev->gap3_len) {
	json_log("JSON: image of unknown format was inserted into drive %c:!\n",
								'C'+drive);
	(void)fclose(dev->f);
	dev->f = NULL;
	free(dev);
	images[drive] = NULL;
	memset(fn, 0x00, sizeof(wchar_t));
	return;
    }

    dev->track_flags |= (temp_rate & 0x03);	/* data rate */
    if (temp_rate & 0x04)
	dev->track_flags |= 0x20;		/* RPM */

    json_log("      disk_flags: 0x%02x, track_flags: 0x%02x, GAP3 length: %i\n",
	dev->disk_flags, dev->track_flags, dev->gap3_len);
    json_log("      bit rate 300: %.2f, temporary rate: %i, hole: %i, DMF: %i\n",
		bit_rate, temp_rate, (dev->disk_flags >> 1), dev->dmf);

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
    json_t *dev = images[drive];
    int t, h, s;

    if (dev == NULL) return;

    /* Unlink image from the system. */
    d86f_unregister(drive);

    /* Release all the sector buffers. */
    for (t=0; t<256; t++) {
	for (h=0; h<2; h++) {
		memset(dev->sects[t][h], 0x00, sizeof(sector_t));
		for (s=0; s<256; s++) {
			if (dev->sects[t][h][s].data != NULL)
				free(dev->sects[t][h][s].data);
			dev->sects[t][h][s].data = NULL;
		}
	}
    }

    if (dev->f != NULL)
	(void)fclose(dev->f);

    /* Release the memory. */
    free(dev);
    images[drive] = NULL;
}
