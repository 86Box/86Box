/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the FDI floppy stream image format
 *		interface to the FDI2RAW module.
 *
 * Version:	@(#)fdd_fdi.c	1.0.4	2018/10/18
 *
 * Authors:	Sarah Walker, <tommowalker@tommowalker.co.uk>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2008-2018 Sarah Walker.
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2018 Fred N. van Kempen.
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
#include "fdd_img.h"
#include "fdd_fdi.h"
#include "fdc.h"
#include "fdi2raw.h"


typedef struct {
    FILE	*f;
    FDI		*h;

    int		lasttrack;
    int		sides;
    int		track;
    int		tracklen[2][4];
    int		trackindex[2][4];

    uint8_t	track_data[2][4][256*1024];
    uint8_t	track_timing[2][4][256*1024];
} fdi_t;


static fdi_t	*fdi[FDD_NUM];
static fdc_t	*fdi_fdc;


#ifdef ENABLE_FDI_LOG
int fdi_do_log = ENABLE_FDI_LOG;


static void
fdi_log(const char *fmt, ...)
{
   va_list ap;

   if (fdi_do_log)
   {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
   }
}
#else
#define fdi_log(fmt, ...)
#endif


static uint16_t
disk_flags(int drive)
{
    fdi_t *dev = fdi[drive];
    uint16_t temp_disk_flags = 0x80;	/* We ALWAYS claim to have extra bit cells, even if the actual amount is 0. */

    switch (fdi2raw_get_bit_rate(dev->h)) {
	case 500:
		temp_disk_flags |= 2;
		break;

	case 300:
	case 250:
		temp_disk_flags |= 0;
		break;

	case 1000:
		temp_disk_flags |= 4;
		break;

	default:
		temp_disk_flags |= 0;
    }

    if (dev->sides == 2)
	temp_disk_flags |= 8;

    /*
     * Tell the 86F handler that we will handle our
     * data to handle it in reverse byte endianness.
     */
    temp_disk_flags |= 0x800;

    return(temp_disk_flags);
}


static uint16_t
side_flags(int drive)
{
    fdi_t *dev = fdi[drive];
    uint16_t temp_side_flags = 0;

    switch (fdi2raw_get_bit_rate(dev->h)) {
	case 500:
		temp_side_flags = 0;
		break;

	case 300:
		temp_side_flags = 1;
		break;

	case 250:
		temp_side_flags = 2;
		break;

	case 1000:
		temp_side_flags = 3;
		break;

	default:
		temp_side_flags = 2;
    }

    if (fdi2raw_get_rotation(dev->h) == 360)
	temp_side_flags |= 0x20;

    /*
     * Set the encoding value to match that provided by the FDC.
     * Then if it's wrong, it will sector not found anyway.
     */
    temp_side_flags |= 0x08;

    return(temp_side_flags);
}


static int
fdi_density(void)
{
    if (! fdc_is_mfm(fdi_fdc)) return(0);

    switch (fdc_get_bit_rate(fdi_fdc)) {
	case 0:
		return(2);

	case 1:
		return(1);

	case 2:
		return(1);

	case 3:
	case 5:
		return(3);

	default:
		break;
    }

    return(1);
}


static int32_t
extra_bit_cells(int drive, int side)
{
    fdi_t *dev = fdi[drive];
    int density = 0;
    int raw_size = 0;
    int is_300_rpm = 0;

    density = fdi_density();

    is_300_rpm = (fdd_getrpm(drive) == 300);

    switch (fdc_get_bit_rate(fdi_fdc)) {
	case 0:
		raw_size = is_300_rpm ? 200000 : 166666;
		break;

	case 1:
		raw_size = is_300_rpm ? 120000 : 100000;
		break;

	case 2:
		raw_size = is_300_rpm ? 100000 : 83333;
		break;

	case 3:
	case 5:
		raw_size = is_300_rpm ? 400000 : 333333;
		break;

	default:
		raw_size = is_300_rpm ? 100000 : 83333;
    }

    return((dev->tracklen[side][density] - raw_size));
}


static void
read_revolution(int drive)
{
    fdi_t *dev = fdi[drive];
    int c, den, side;
    int track = dev->track;

    if (track > dev->lasttrack) {
	for (den = 0; den < 4; den++) {
		memset(dev->track_data[0][den], 0, 106096);
		memset(dev->track_data[1][den], 0, 106096);
		dev->tracklen[0][den] = dev->tracklen[1][den] = 100000;
	}
	return;
    }

    for (den = 0; den < 4; den++) {
	for (side = 0; side < dev->sides; side++) {
		c = fdi2raw_loadtrack(dev->h,
				      (uint16_t *)dev->track_data[side][den],
				      (uint16_t *)dev->track_timing[side][den],
				      (track * dev->sides) + side,
				      &dev->tracklen[side][den],
				      &dev->trackindex[side][den], NULL, den);
		if (! c)
			memset(dev->track_data[side][den], 0, dev->tracklen[side][den]);
	}

	if (dev->sides == 1) {
		memset(dev->track_data[1][den], 0, 106096);
		dev->tracklen[1][den] = 100000;
	}
    }
}


static uint32_t
index_hole_pos(int drive, int side)
{
    fdi_t *dev = fdi[drive];
    int density;

    density = fdi_density();

    return(dev->trackindex[side][density]);
}


static uint32_t
get_raw_size(int drive, int side)
{
    fdi_t *dev = fdi[drive];
    int density;

    density = fdi_density();

    return(dev->tracklen[side][density]);
}


static uint16_t *
encoded_data(int drive, int side)
{
    fdi_t *dev = fdi[drive];
    int density = 0;

    density = fdi_density();

    return((uint16_t *)dev->track_data[side][density]);
}


void
fdi_seek(int drive, int track)
{
    fdi_t *dev = fdi[drive];

    if (fdd_doublestep_40(drive)) {
	if (fdi2raw_get_tpi(dev->h) < 2)
		track /= 2;
    }

    d86f_set_cur_track(drive, track);

    if (dev->f == NULL) return;

    if (track < 0)
	track = 0;

#if 0
    if (track > dev->lasttrack)
	track = dev->lasttrack - 1;
#endif

    dev->track = track;

    read_revolution(drive);
}


void
fdi_load(int drive, wchar_t *fn)
{
    char header[26];
    fdi_t *dev;

    writeprot[drive] = fwriteprot[drive] = 1;

    /* Allocate a drive block. */
    dev = (fdi_t *)malloc(sizeof(fdi_t));
    memset(dev, 0x00, sizeof(fdi_t));

    dev->f = plat_fopen(fn, L"rb");
    if (dev == NULL) {
	free(dev);
	memset(floppyfns[drive], 0, sizeof(floppyfns[drive]));
	return;
    }

    d86f_unregister(drive);

    fread(header, 1, 25, dev->f);
    fseek(dev->f, 0, SEEK_SET);
    header[25] = 0;
    if (strcmp(header, "Formatted Disk Image file") != 0) {
	/* This is a Japanese FDI file. */
	fdi_log("fdi_load(): Japanese FDI file detected, redirecting to IMG loader\n");
	fclose(dev->f);
	free(dev);
	img_load(drive, fn);
	return;
    }

    /* Set up the drive unit. */
    fdi[drive] = dev;

    dev->h = fdi2raw_header(dev->f);
    dev->lasttrack = fdi2raw_get_last_track(dev->h);
    dev->sides = fdi2raw_get_last_head(dev->h) + 1;

    /* Attach this format to the D86F engine. */
    d86f_handler[drive].disk_flags = disk_flags;
    d86f_handler[drive].side_flags = side_flags;
    d86f_handler[drive].writeback = null_writeback;
    d86f_handler[drive].set_sector = null_set_sector;
    d86f_handler[drive].write_data = null_write_data;
    d86f_handler[drive].format_conditions = null_format_conditions;
    d86f_handler[drive].extra_bit_cells = extra_bit_cells;
    d86f_handler[drive].encoded_data = encoded_data;
    d86f_handler[drive].read_revolution = read_revolution;
    d86f_handler[drive].index_hole_pos = index_hole_pos;
    d86f_handler[drive].get_raw_size = get_raw_size;
    d86f_handler[drive].check_crc = 1;
    d86f_set_version(drive, D86FVER);

    d86f_common_handlers(drive);

    drives[drive].seek = fdi_seek;

    fdi_log("Loaded as FDI\n");
}


void
fdi_close(int drive)
{
    fdi_t *dev = fdi[drive];

    if (dev == NULL) return;

    d86f_unregister(drive);

    drives[drive].seek = NULL;

    if (dev->h)
	fdi2raw_header_free(dev->h);

    if (dev->f)
	fclose(dev->f);

    /* Release the memory. */
    free(dev);
    fdi[drive] = NULL;
}


void
fdi_set_fdc(void *fdc)
{
    fdi_fdc = (fdc_t *)fdc;
}
