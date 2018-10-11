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
 * Version:	@(#)cdrom.c	1.0.1	2018/10/11
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
#include "cdrom.h"
#include "cdrom_image.h"
#include "cdrom_null.h"
#include "../sound/sound.h"


#define MIN_SEEK		2000
#define MAX_SEEK	      333333


cdrom_image_t	cdrom_image[CDROM_NUM];
cdrom_drive_t	cdrom_drives[CDROM_NUM];

#ifdef ENABLE_CDROM_LOG
int cdrom_do_log = ENABLE_CDROM_LOG;
#endif


static void
cdrom_log(const char *format, ...)
{
#ifdef ENABLE_CDROM_LOG
    va_list ap;

    if (cdrom_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
#endif
}


int
cdrom_lba_to_msf_accurate(int lba)
{
    int temp_pos;
    int m, s, f;

    temp_pos = lba + 150;
    f = temp_pos % 75;
    temp_pos -= f;
    temp_pos /= 75;
    s = temp_pos % 60;
    temp_pos -= s;
    temp_pos /= 60;
    m = temp_pos;

    return ((m << 16) | (s << 8) | f);
}


double
cdrom_get_short_seek(cdrom_drive_t *dev)
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


double
cdrom_get_long_seek(cdrom_drive_t *dev)
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
cdrom_seek_time(cdrom_drive_t *dev)
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
cdrom_seek(cdrom_drive_t *dev, uint32_t pos)
{
    /* cdrom_log("CD-ROM %i: Seek %08X\n", dev->id, pos); */
    if (!dev)
	return;

    dev->seek_pos   = pos;
    if (dev->handler && dev->handler->stop)
	dev->handler->stop(dev->id);
}


int
cdrom_playing_completed(cdrom_drive_t *dev)
{
    dev->prev_status = dev->cd_status;
    dev->cd_status = dev->handler->status(dev->id);
    if (((dev->prev_status == CD_STATUS_PLAYING) || (dev->prev_status == CD_STATUS_PAUSED)) &&
	((dev->cd_status != CD_STATUS_PLAYING) && (dev->cd_status != CD_STATUS_PAUSED)))
	return 1;
    else
	return 0;
}


/* Peform a master init on the entire module. */
void
cdrom_global_init(void)
{
    /* Clear the global data. */
    memset(cdrom_drives, 0x00, sizeof(cdrom_drives));
}


static void
cdrom_drive_reset(cdrom_drive_t *drv)
{
    drv->p = NULL;
    drv->insert = NULL;
    drv->get_volume = NULL;
    drv->get_channel = NULL;
    drv->close = NULL;
}


void
cdrom_hard_reset(void)
{
    int c;
    cdrom_drive_t *drv;

    for (c = 0; c < CDROM_NUM; c++) {
	if (cdrom_drives[c].bus_type) {
		cdrom_log("CDROM hard_reset drive=%d\n", c);

		drv = &cdrom_drives[c];
		drv->id = c;

		cdrom_drive_reset(drv);

		if ((drv->bus_type == CDROM_BUS_ATAPI) || (drv->bus_type == CDROM_BUS_SCSI))
			scsi_cdrom_drive_reset(c);

		if (drv->host_drive == 200) {
			image_open(c, cdrom_image[c].image_path);
			image_reset(c);
		} else
			cdrom_null_open(c);
	}
    }

    sound_cd_thread_reset();
}


void
cdrom_close_handler(uint8_t id)
{
    cdrom_drive_t *dev = &cdrom_drives[id];

    if (!dev)
	return;

    switch (dev->host_drive) {
	case 200:
		image_close(id);
		break;
	default:
		null_close(id);
		break;
    }

    dev->handler = NULL;
}


void
cdrom_close(void)
{
    int c;

    for (c = 0; c < CDROM_NUM; c++) {
	if (cdrom_drives[c].handler)
		cdrom_close_handler(c);

	if (cdrom_drives[c].close)
		cdrom_drives[c].close(cdrom_drives[c].p);

	cdrom_drive_reset(&cdrom_drives[c]);
    }
}
