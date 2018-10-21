/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Generic CD-ROM drive core.
 *
 * Version:	@(#)cdrom.c	1.0.3	2018/10/21
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Miran Grca.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../config.h"
#include "cdrom.h"
#include "cdrom_image.h"
#include "../plat.h"
#include "../sound/sound.h"


#define MIN_SEEK		2000
#define MAX_SEEK	      333333


cdrom_t	cdrom[CDROM_NUM];


#ifdef ENABLE_CDROM_LOG
int		cdrom_do_log = ENABLE_CDROM_LOG;


void
cdrom_log(const char *fmt, ...)
{
    va_list ap;

    if (cdrom_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define cdrom_log(fmt, ...)
#endif


int
cdrom_lba_to_msf_accurate(int lba)
{
    int pos;
    int m, s, f;

    pos = lba + 150;
    f = pos % 75;
    pos -= f;
    pos /= 75;
    s = pos % 60;
    pos -= s;
    pos /= 60;
    m = pos;

    return ((m << 16) | (s << 8) | f);
}


static double
cdrom_get_short_seek(cdrom_t *dev)
{
    switch(dev->cur_speed) {
	case 0:
		fatal("CD-ROM %i: 0x speed\n", dev->id);
		return 0.0;
	case 1:
		return 240.0;
	case 2:
		return 160.0;
	case 3:
		return 150.0;
	case 4: case 5: case 6: case 7: case 8:
	case 9: case 10: case 11:
		return 112.0;
	case 12: case 13: case 14: case 15:
		return 75.0;
	case 16: case 17: case 18: case 19:
		return 58.0;
	case 20: case 21: case 22: case 23:
	case 40: case 41: case 42: case 43:
	case 44: case 45: case 46: case 47:
	case 48:
		return 50.0;
	default:
		/* 24-32, 52+ */
		return 45.0;
    }
}


static double
cdrom_get_long_seek(cdrom_t *dev)
{
    switch(dev->cur_speed) {
	case 0:
		fatal("CD-ROM %i: 0x speed\n", dev->id);
		return 0.0;
	case 1:
		return 1446.0;
	case 2:
		return 1000.0;
	case 3:
		return 900.0;
	case 4: case 5: case 6: case 7: case 8:
	case 9: case 10: case 11:
		return 675.0;
	case 12: case 13: case 14: case 15:
		return 400.0;
	case 16: case 17: case 18: case 19:
		return 350.0;
	case 20: case 21: case 22: case 23:
	case 40: case 41: case 42: case 43:
	case 44: case 45: case 46: case 47:
	case 48:
		return 300.0;
	default:
		/* 24-32, 52+ */
	return 270.0;
    }
}


double
cdrom_seek_time(cdrom_t *dev)
{
    uint32_t diff = dev->seek_diff;
    double sd = (double) (MAX_SEEK - MIN_SEEK);

    if (diff < MIN_SEEK)
	return 0.0;
    if (diff > MAX_SEEK)
	diff = MAX_SEEK;

    diff -= MIN_SEEK;

    return cdrom_get_short_seek(dev) + ((cdrom_get_long_seek(dev) * ((double) diff)) / sd);
}


void
cdrom_seek(cdrom_t *dev, uint32_t pos)
{
    /* cdrom_log("CD-ROM %i: Seek %08X\n", dev->id, pos); */
    if (!dev)
	return;

    dev->seek_pos   = pos;
    if (dev->ops && dev->ops->stop)
	dev->ops->stop(dev);
}


int
cdrom_playing_completed(cdrom_t *dev)
{
    dev->prev_status = dev->cd_status;

    if (dev->ops && dev->ops->status)
	dev->cd_status = dev->ops->status(dev);
    else {
	dev->cd_status = CD_STATUS_EMPTY;
	return 0;
    }

    if (((dev->prev_status == CD_STATUS_PLAYING) || (dev->prev_status == CD_STATUS_PAUSED)) &&
	((dev->cd_status != CD_STATUS_PLAYING) && (dev->cd_status != CD_STATUS_PAUSED)))
	return 1;

    return 0;
}


/* Peform a master init on the entire module. */
void
cdrom_global_init(void)
{
    /* Clear the global data. */
    memset(cdrom, 0x00, sizeof(cdrom));
}


static void
cdrom_drive_reset(cdrom_t *dev)
{
    dev->p = NULL;
    dev->insert = NULL;
    dev->close = NULL;
    dev->get_volume = NULL;
    dev->get_channel = NULL;
}


void
cdrom_hard_reset(void)
{
    cdrom_t *dev;
    int i;

    for (i = 0; i < CDROM_NUM; i++) {
	dev = &cdrom[i];
	if (dev->bus_type) {
		cdrom_log("CDROM %i: hard_reset\n", i);

		dev->id = i;

		cdrom_drive_reset(dev);

		switch(dev->bus_type) {
			case CDROM_BUS_ATAPI:
			case CDROM_BUS_SCSI:
				scsi_cdrom_drive_reset(i);
				break;

			default:
				break;
		}


		if (dev->host_drive == 200) {
			cdrom_image_open(dev, dev->image_path);
			cdrom_image_reset(dev);
		}
	}
    }

    sound_cd_thread_reset();
}


void
cdrom_close(void)
{
    cdrom_t *dev;
    int i;

    for (i = 0; i < CDROM_NUM; i++) {
	dev = &cdrom[i];

	if (dev->ops && dev->ops->exit)
		dev->ops->exit(dev);

	dev->ops = NULL;

	if (dev->close)
		dev->close(dev->p);

	cdrom_drive_reset(dev);
    }
}


/* Signal disc change to the emulated machine. */
void
cdrom_insert(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    if (dev->bus_type) {
	if (dev->insert)
		dev->insert(dev->p);
    }
}


/* The mechanics of ejecting a CD-ROM from a drive. */
void
cdrom_eject(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    /* This entire block should be in cdrom.c/cdrom_eject(dev*) ... */
    if (dev->host_drive == 0) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    if (dev->prev_image_path) {
	free(dev->prev_image_path);
	dev->prev_image_path = NULL;
    }

    if (dev->host_drive == 200) {
	dev->prev_image_path = (wchar_t *) malloc(1024 * sizeof(wchar_t));
	wcscpy(dev->prev_image_path, dev->image_path);
    }

    dev->prev_host_drive = dev->host_drive;
    dev->host_drive = 0;

    dev->ops->exit(dev);
    dev->ops = NULL;
    memset(dev->image_path, 0, sizeof(dev->image_path));

    cdrom_insert(id);

    plat_cdrom_ui_update(id, 0);

    config_save();
}


/* The mechanics of re-loading a CD-ROM drive. */
void
cdrom_reload(uint8_t id)
{
    cdrom_t *dev = &cdrom[id];

    if ((dev->host_drive == dev->prev_host_drive) ||
	(dev->prev_host_drive == 0) || (dev->host_drive != 0)) {
	/* Switch from empty to empty. Do nothing. */
	return;
    }

    if (dev->ops && dev->ops->exit)
	dev->ops->exit(dev);
    dev->ops = NULL;
    memset(dev->image_path, 0, sizeof(dev->image_path));

    if (dev->prev_host_drive == 200) {
	/* Reload a previous image. */
	wcscpy(dev->image_path, dev->prev_image_path);
	free(dev->prev_image_path);
	dev->prev_image_path = NULL;
	cdrom_image_open(dev, dev->image_path);

	cdrom_insert(id);

	if (wcslen(dev->image_path) == 0)
		dev->host_drive = 0;
	  else
		dev->host_drive = 200;
    }

    plat_cdrom_ui_update(id, 1);

    config_save();
}
