/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the HxC MFM image format.
 *
 * Version:	@(#)fdd_mfm.c	1.0.2	2019/12/05
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018,2019 Miran Grca.
 */
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../plat.h"
#include "fdd.h"
#include "fdd_86f.h"
#include "fdd_img.h"
#include "fdd_mfm.h"
#include "fdc.h"


#pragma pack(push,1)
typedef struct {
    uint8_t	hdr_name[7];

    uint16_t	tracks_no;
    uint8_t	sides_no;

    uint16_t	rpm;
    uint16_t	bit_rate;
    uint8_t	if_type;

    uint32_t	track_list_offset;
} mfm_header_t;

typedef struct {
    uint16_t	track_no;
    uint8_t	side_no;
    uint32_t	track_size;
    uint32_t	track_offset;
} mfm_track_t;

typedef struct {
    uint16_t	track_no;
    uint8_t	side_no;
    uint16_t	rpm;
    uint16_t	bit_rate;
    uint32_t	track_size;
    uint32_t	track_offset;
} mfm_adv_track_t;
#pragma pack(pop)

typedef struct {
    FILE	*f;

    mfm_header_t	hdr;
    mfm_track_t		*tracks;
    mfm_adv_track_t	*adv_tracks;

    uint16_t	disk_flags, pad;
    uint16_t	side_flags[2];

    int		br_rounded, rpm_rounded,
		total_tracks, cur_track;

    uint8_t	track_data[2][256*1024];
} mfm_t;


static mfm_t	*mfm[FDD_NUM];
static fdc_t	*mfm_fdc;


#ifdef ENABLE_MFM_LOG
int mfm_do_log = ENABLE_MFM_LOG;


static void
mfm_log(const char *fmt, ...)
{
   va_list ap;

   if (mfm_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define mfm_log(fmt, ...)
#endif


static int
get_track_index(int drive, int side, int track)
{
    mfm_t *dev = mfm[drive];
    int i, ret = -1;

    for (i = 0; i < dev->total_tracks; i++) {
	if ((dev->tracks[i].track_no == track) &&
	    (dev->tracks[i].side_no == side)) {
		ret = i;
		break;
	}
    }

    return ret;
}


static int
get_adv_track_index(int drive, int side, int track)
{
    mfm_t *dev = mfm[drive];
    int i, ret = -1;

    for (i = 0; i < dev->total_tracks; i++) {
	if ((dev->adv_tracks[i].track_no == track) &&
	    (dev->adv_tracks[i].side_no == side)) {
		ret = i;
		break;
	}
    }

    return ret;
}


static void
get_adv_track_bitrate(int drive, int side, int track, int *br, int *rpm)
{
    mfm_t *dev = mfm[drive];
    int track_index;
    double dbr;

    track_index = get_adv_track_index(drive, side, track);

    if (track_index == -1) {
	*br = 250;
	*rpm = 300;
    } else {
	dbr = round(((double) dev->adv_tracks[track_index].bit_rate) / 50.0) * 50.0;
	*br = ((int) dbr);
	dbr = round(((double) dev->adv_tracks[track_index].rpm) / 60.0) * 60.0;
	*rpm = ((int) dbr);
    }
}


static void
set_disk_flags(int drive)
{
    int br = 250, rpm = 300;
    mfm_t *dev = mfm[drive];
    uint16_t temp_disk_flags = 0x1080;	/* We ALWAYS claim to have extra bit cells, even if the actual amount is 0;
					   Bit 12 = 1, bits 6, 5 = 0 - extra bit cells field specifies the entire
					   amount of bit cells per track. */

    /* If this is the modified MFM format, get bit rate (and RPM) from track 0 instead. */
    if (dev->hdr.if_type & 0x80)
	get_adv_track_bitrate(drive, 0, 0, &br, &rpm);
    else {
	br = dev->br_rounded;
	rpm = dev->rpm_rounded;
    }

    switch (br) {
	case 500:
		temp_disk_flags |= 2;
		break;

	case 300:
	case 250:
	default:
		temp_disk_flags |= 0;
		break;

	case 1000:
		temp_disk_flags |= 4;
		break;
    }

    if (dev->hdr.sides_no == 2)
	temp_disk_flags |= 8;

    dev->disk_flags = temp_disk_flags;
}


static uint16_t
disk_flags(int drive)
{
    mfm_t *dev = mfm[drive];

    return dev->disk_flags;
}


static void
set_side_flags(int drive, int side)
{
    mfm_t *dev = mfm[drive];
    uint16_t temp_side_flags = 0;
    int br = 250, rpm = 300;

    if (dev->hdr.if_type & 0x80)
	get_adv_track_bitrate(drive, side, dev->cur_track, &br, &rpm);
    else {
	br = dev->br_rounded;
	rpm = dev->rpm_rounded;
    }

    /* 300 kbps @ 360 rpm = 250 kbps @ 200 rpm */
    if ((br == 300) && (rpm == 360)) {
	br = 250;
	rpm = 300;
    }

    switch (br) {
	case 500:
		temp_side_flags = 0;
		break;

	case 300:
		temp_side_flags = 1;
		break;

	case 250:
	default:
		temp_side_flags = 2;
		break;

	case 1000:
		temp_side_flags = 3;
		break;
    }

    if (rpm == 360)
	temp_side_flags |= 0x20;

    /*
     * Set the encoding value to match that provided by the FDC.
     * Then if it's wrong, it will sector not found anyway.
     */
    temp_side_flags |= 0x08;

    dev->side_flags[side] = temp_side_flags;
}


static uint16_t
side_flags(int drive)
{
    mfm_t *dev = mfm[drive];
    int side;

    side = fdd_get_head(drive);

    return dev->side_flags[side];
}


static uint32_t
get_raw_size(int drive, int side)
{
    mfm_t *dev = mfm[drive];
    int track_index, is_300_rpm;
    int br = 250, rpm = 300;

    if (dev->hdr.if_type & 0x80) {
	track_index = get_adv_track_index(drive, side, dev->cur_track);
	get_adv_track_bitrate(drive, 0, 0, &br, &rpm);
    } else {
	track_index = get_track_index(drive, side, dev->cur_track);
	br = dev->br_rounded;
	rpm = dev->rpm_rounded;
    }

    is_300_rpm = (rpm == 300);

    if (track_index == -1) {
	mfm_log("MFM: Unable to find track (%i, %i)\n", dev->cur_track, side);
	switch (br) {
		case 250:
		default:
			return is_300_rpm ? 100000 : 83333;
		case 300:
			return is_300_rpm ? 120000 : 100000;
		case 500:
			return is_300_rpm ? 200000 : 166666;
		case 1000:
			return is_300_rpm ? 400000 : 333333;
	}
    }

    /* Bit 7 on - my extension of the HxC MFM format to output exact bitcell counts
       for each track instead of rounded byte counts. */
    if (dev->hdr.if_type & 0x80)
	return dev->adv_tracks[track_index].track_size;
    else
	return dev->tracks[track_index].track_size * 8;
}


static int32_t
extra_bit_cells(int drive, int side)
{
    return (int32_t) get_raw_size(drive, side);
}


static uint16_t *
encoded_data(int drive, int side)
{
    mfm_t *dev = mfm[drive];

    return((uint16_t *)dev->track_data[side]);
}


void
mfm_read_side(int drive, int side)
{
    mfm_t *dev = mfm[drive];
    int track_index, track_size;
    int track_bytes;

    if (dev->hdr.if_type & 0x80)
	track_index = get_adv_track_index(drive, side, dev->cur_track);
    else
	track_index = get_track_index(drive, side, dev->cur_track);

    track_size = get_raw_size(drive, side);
    track_bytes = track_size >> 3;
    if (track_size & 0x07)
	track_bytes++;

    if (track_index == -1)
	memset(dev->track_data[side], 0x00, track_bytes);
    else {
	if (dev->hdr.if_type & 0x80)
		fseek(dev->f, dev->adv_tracks[track_index].track_offset, SEEK_SET);
	else
		fseek(dev->f, dev->tracks[track_index].track_offset, SEEK_SET);
	fread(dev->track_data[side], 1, track_bytes, dev->f);
    }

    mfm_log("drive = %i, side = %i, dev->cur_track = %i, track_index = %i, track_size = %i\n",
	    drive, side, dev->cur_track, track_index, track_size);
}


void
mfm_seek(int drive, int track)
{
    mfm_t *dev = mfm[drive];

    mfm_log("mfm_seek(%i, %i)\n", drive, track);

    if (fdd_doublestep_40(drive)) {
	if (dev->hdr.tracks_no <= 43)
		track /= 2;
    }

    dev->cur_track = track;
    d86f_set_cur_track(drive, track);

    if (dev->f == NULL)
	return;

    if (track < 0)
	track = 0;

    mfm_read_side(drive, 0);
    mfm_read_side(drive, 1);

    set_side_flags(drive, 0);
    set_side_flags(drive, 1);
}


void
mfm_load(int drive, wchar_t *fn)
{
    mfm_t *dev;
    double dbr;
    int i;

    writeprot[drive] = fwriteprot[drive] = 1;

    /* Allocate a drive block. */
    dev = (mfm_t *)malloc(sizeof(mfm_t));
    memset(dev, 0x00, sizeof(mfm_t));

    dev->f = plat_fopen(fn, L"rb");
    if (dev->f == NULL) {
	free(dev);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    }

    d86f_unregister(drive);

    /* Read the header. */
    fread(&dev->hdr, 1, sizeof(mfm_header_t), dev->f);

    /* Calculate tracks * sides, allocate the tracks array, and read it. */
    dev->total_tracks = dev->hdr.tracks_no * dev->hdr.sides_no;
    if (dev->hdr.if_type & 0x80) {
	dev->adv_tracks = (mfm_adv_track_t *) malloc(dev->total_tracks * sizeof(mfm_adv_track_t));
	fread(dev->adv_tracks, 1, dev->total_tracks * sizeof(mfm_adv_track_t), dev->f);
    } else {
	dev->tracks = (mfm_track_t *) malloc(dev->total_tracks * sizeof(mfm_track_t));
	fread(dev->tracks, 1, dev->total_tracks * sizeof(mfm_track_t), dev->f);
    }

    /* The chances of finding a HxC MFM image of a single-sided thin track
       disk are much smaller than the chances of finding a HxC MFM image
       incorrectly converted from a SCP image, erroneously indicating 1
       side and 80+ tracks instead of 2 sides and <= 43 tracks, so if we
       have detected such an image, convert the track numbers. */
    if ((dev->hdr.tracks_no > 43) && (dev->hdr.sides_no == 1)) {
	dev->hdr.tracks_no >>= 1;
	dev->hdr.sides_no <<= 1;

	for (i = 0; i < dev->total_tracks; i++) {
		if (dev->hdr.if_type & 0x80) {
			dev->adv_tracks[i].side_no <<= 1;
			dev->adv_tracks[i].side_no |= (dev->adv_tracks[i].track_no & 1);
			dev->adv_tracks[i].track_no >>= 1;
		} else {
			dev->tracks[i].side_no <<= 1;
			dev->tracks[i].side_no |= (dev->tracks[i].track_no & 1);
			dev->tracks[i].track_no >>= 1;
		}
	}
    }

    if (!(dev->hdr.if_type & 0x80)) {
	dbr = round(((double) dev->hdr.bit_rate) / 50.0) * 50.0;
	dev->br_rounded = (int) dbr;
	mfm_log("Rounded bit rate: %i kbps\n", dev->br_rounded);

	dbr = round(((double) dev->hdr.rpm) / 60.0) * 60.0;
	dev->rpm_rounded = (int) dbr;
	mfm_log("Rounded RPM: %i kbps\n", dev->rpm_rounded);
    }

    /* Set up the drive unit. */
    mfm[drive] = dev;

    set_disk_flags(drive);

    /* Attach this format to the D86F engine. */
    d86f_handler[drive].disk_flags = disk_flags;
    d86f_handler[drive].side_flags = side_flags;
    d86f_handler[drive].writeback = null_writeback;
    d86f_handler[drive].set_sector = null_set_sector;
    d86f_handler[drive].write_data = null_write_data;
    d86f_handler[drive].format_conditions = null_format_conditions;
    d86f_handler[drive].extra_bit_cells = extra_bit_cells;
    d86f_handler[drive].encoded_data = encoded_data;
    d86f_handler[drive].read_revolution = common_read_revolution;
    d86f_handler[drive].index_hole_pos = null_index_hole_pos;
    d86f_handler[drive].get_raw_size = get_raw_size;
    d86f_handler[drive].check_crc = 1;
    d86f_set_version(drive, D86FVER);

    d86f_common_handlers(drive);

    drives[drive].seek = mfm_seek;

    mfm_log("Loaded as MFM\n");
}


void
mfm_close(int drive)
{
    mfm_t *dev = mfm[drive];

    if (dev == NULL) return;

    d86f_unregister(drive);

    drives[drive].seek = NULL;

    if (dev->tracks)
	free(dev->tracks);

    if (dev->adv_tracks)
	free(dev->adv_tracks);

    if (dev->f)
	fclose(dev->f);

    /* Release the memory. */
    free(dev);
    mfm[drive] = NULL;
}


void
mfm_set_fdc(void *fdc)
{
    mfm_fdc = (fdc_t *)fdc;
}
