/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Iomega ZIP drive with SCSI(-like)
 *		commands, for both ATAPI and SCSI usage.
 *
 * Version:	@(#)zip.c	1.0.29	2018/10/25
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../config.h"
#include "../timer.h"
#include "../device.h"
#include "../piix.h"
#include "../scsi/scsi_device.h"
#include "../nvr.h"
#include "../plat.h"
#include "../ui.h"
#include "hdc.h"
#include "hdc_ide.h"
#include "zip.h"


/* Bits of 'status' */
#define ERR_STAT		0x01
#define DRQ_STAT		0x08 /* Data request */
#define DSC_STAT                0x10
#define SERVICE_STAT            0x10
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits of 'error' */
#define ABRT_ERR		0x04 /* Command aborted */
#define MCR_ERR			0x08 /* Media change request */

#define zipbufferb		dev->buffer


zip_t		*zip[ZIP_NUM] = { NULL, NULL, NULL, NULL };
zip_drive_t	zip_drives[ZIP_NUM];


/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
const uint8_t zip_command_flags[0x100] =
{
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x00 */
    IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,		/* 0x01 */
    0,
    IMPLEMENTED | ALLOW_UA,					/* 0x03 */
    IMPLEMENTED | CHECK_READY | ALLOW_UA | NONDATA | SCSI_ONLY,	/* 0x04 */
    0,
    IMPLEMENTED,						/* 0x06 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x08 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x0A */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x0B */
    IMPLEMENTED,						/* 0x0C */
    IMPLEMENTED | ATAPI_ONLY,					/* 0x0D */
    0, 0, 0, 0,
    IMPLEMENTED | ALLOW_UA,					/* 0x12 */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x13 */
    0,
    IMPLEMENTED,						/* 0x15 */
    IMPLEMENTED | SCSI_ONLY,					/* 0x16 */
    IMPLEMENTED | SCSI_ONLY,					/* 0x17 */
    0, 0,
    IMPLEMENTED,						/* 0x1A */
    IMPLEMENTED | CHECK_READY,					/* 0x1B */
    0,
    IMPLEMENTED,						/* 0x1D */
    IMPLEMENTED | CHECK_READY,					/* 0x1E */
    0, 0, 0, 0,
    IMPLEMENTED | ATAPI_ONLY,					/* 0x23 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x25 */
    0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x28 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x2A */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x2B */
    0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x2E */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x2F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x41 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0,
    IMPLEMENTED,						/* 0x55 */
    0, 0, 0, 0,
    IMPLEMENTED,						/* 0x5A */
    0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0xA8 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0xAA */
    0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0xAE */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0xAF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED,						/* 0xBD */
    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static uint64_t zip_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE |
					     GPMODEP_DISCONNECT_PAGE |
					     GPMODEP_IOMEGA_PAGE |
					     GPMODEP_ALL_PAGES);
static uint64_t zip_250_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE |
						 GPMODEP_FLEXIBLE_DISK_PAGE |
						 GPMODEP_CACHING_PAGE |
						 GPMODEP_IOMEGA_PAGE |
						 GPMODEP_ALL_PAGES);


static const mode_sense_pages_t zip_mode_sense_pages_default =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x0a, 0xc8, 22, 0,  0, 0, 0, 90, 0, 0x50, 0x20 },
    {   GPMODE_DISCONNECT_PAGE, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {       GPMODE_IOMEGA_PAGE,    0x04, 0x5c, 0x0f, 0xff, 0x0f }
}   };

static const mode_sense_pages_t zip_250_mode_sense_pages_default =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x06, 0xc8, 0x64, 0,  0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {GPMODE_FLEXIBLE_DISK_PAGE, 0x1e, 0x80, 0, 0x40, 0x20, 2, 0, 0, 0xef, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0b, 0x7d, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {      GPMODE_CACHING_PAGE, 0x0a, 4, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0xff, 0xff },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },	{                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {       GPMODE_IOMEGA_PAGE,    0x04, 0x5c, 0x0f, 0x3c, 0x0f }
}   };

static const mode_sense_pages_t zip_mode_sense_pages_default_scsi =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x0a, 0xc8, 22, 0,  0, 0, 0, 90, 0, 0x50, 0x20 },
    {   GPMODE_DISCONNECT_PAGE, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {       GPMODE_IOMEGA_PAGE,    0x04, 0x5c, 0x0f, 0xff, 0x0f }
}   };

static const mode_sense_pages_t zip_250_mode_sense_pages_default_scsi =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x06, 0xc8, 0x64, 0,  0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {GPMODE_FLEXIBLE_DISK_PAGE, 0x1e, 0x80, 0, 0x40, 0x20, 2, 0, 0, 0xef, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0b, 0x7d, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {      GPMODE_CACHING_PAGE, 0x0a, 4, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0xff, 0xff },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {       GPMODE_IOMEGA_PAGE,    0x04, 0x5c, 0x0f, 0x3c, 0x0f }
}   };

static const mode_sense_pages_t zip_mode_sense_pages_changeable =
{   {
    {                        0,    0 },

    {    GPMODE_R_W_ERROR_PAGE, 0x0a, 0xFF, 0xFF, 0,  0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF },
    {   GPMODE_DISCONNECT_PAGE, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {       GPMODE_IOMEGA_PAGE,    0x04, 0xff, 0xff, 0xff, 0xff }
}   };

static const mode_sense_pages_t zip_250_mode_sense_pages_changeable =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x06, 0xFF, 0xFF, 0,  0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {GPMODE_FLEXIBLE_DISK_PAGE, 0x1e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {      GPMODE_CACHING_PAGE, 0x0a, 4, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0xff, 0xff },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {       GPMODE_IOMEGA_PAGE,    0x04, 0xff, 0xff, 0xff, 0xff }
}   };


static void	zip_command_complete(zip_t *dev);
static void	zip_init(zip_t *dev);

static void	zip_callback(void *p);


#ifdef ENABLE_ZIP_LOG
int zip_do_log = ENABLE_ZIP_LOG;


static void
zip_log(const char *fmt, ...)
{
    va_list ap;

    if (zip_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define zip_log(fmt, ...)
#endif


int
find_zip_for_channel(uint8_t channel)
{
    uint8_t i = 0;

    for (i = 0; i < ZIP_NUM; i++) {
	if ((zip_drives[i].bus_type == ZIP_BUS_ATAPI) && (zip_drives[i].ide_channel == channel))
		return i;
    }
    return 0xff;
}


int
zip_load(zip_t *dev, wchar_t *fn)
{
    int read_only = dev->drv->ui_writeprot;
    int size = 0;

    dev->drv->f = plat_fopen(fn, dev->drv->ui_writeprot ? L"rb" : L"rb+");
    if (!dev->drv->ui_writeprot && !dev->drv->f) {
	dev->drv->f = plat_fopen(fn, L"rb");
	read_only = 1;
    }
    if (dev->drv->f) {
	fseek(dev->drv->f, 0, SEEK_END);
	size = ftell(dev->drv->f);

	if ((size == ((ZIP_250_SECTORS << 9) + 0x1000)) || (size == ((ZIP_SECTORS << 9) + 0x1000))) {
		/* This is a ZDI image. */
		size -= 0x1000;
		dev->drv->base = 0x1000;
	} else
		dev->drv->base = 0;

	if (dev->drv->is_250) {
		if ((size != (ZIP_250_SECTORS << 9)) && (size != (ZIP_SECTORS << 9))) {
			zip_log("File is incorrect size for a ZIP image\nMust be exactly %i or %i bytes\n",
				ZIP_250_SECTORS << 9, ZIP_SECTORS << 9);
			fclose(dev->drv->f);
			dev->drv->f = NULL;
			dev->drv->medium_size = 0;
			zip_eject(dev->id);	/* Make sure the host OS knows we've rejected (and ejected) the image. */
			return 0;
		}
	} else {
		if (size != (ZIP_SECTORS << 9)) {
			zip_log("File is incorrect size for a ZIP image\nMust be exactly %i bytes\n",
				ZIP_SECTORS << 9);
			fclose(dev->drv->f);
			dev->drv->f = NULL;
			dev->drv->medium_size = 0;
			zip_eject(dev->id);	/* Make sure the host OS knows we've rejected (and ejected) the image. */
			return 0;
		}
	}

	dev->drv->medium_size = size >> 9;

	fseek(dev->drv->f, dev->drv->base, SEEK_SET);

	memcpy(dev->drv->image_path, fn, sizeof(dev->drv->image_path));

	dev->drv->read_only = read_only;

	return 1;
    }

    return 0;
}


void
zip_disk_reload(zip_t *dev)
{
    int ret = 0;

    if (wcslen(dev->drv->prev_image_path) == 0)
	return;
    else
	ret = zip_load(dev, dev->drv->prev_image_path);

    if (ret)
	dev->unit_attention = 1;
}


void
zip_disk_close(zip_t *dev)
{
    if (dev->drv->f) {
	fclose(dev->drv->f);
	dev->drv->f = NULL;

	memcpy(dev->drv->prev_image_path, dev->drv->image_path, sizeof(dev->drv->prev_image_path));
	memset(dev->drv->image_path, 0, sizeof(dev->drv->image_path));

	dev->drv->medium_size = 0;
    }
}


static void
zip_set_callback(zip_t *dev)
{
    if (dev->drv->bus_type != ZIP_BUS_SCSI)
	ide_set_callback(dev->drv->ide_channel >> 1, dev->callback);
}


static void
zip_set_signature(void *p)
{
    zip_t *dev = (zip_t *) p;

    if (dev->id >= ZIP_NUM)
	return;
    dev->phase = 1;
    dev->request_length = 0xEB14;
}


static void
zip_init(zip_t *dev)
{
    if (dev->id >= ZIP_NUM)
	return;

    dev->requested_blocks = 1;
    dev->sense[0] = 0xf0;
    dev->sense[7] = 10;
    dev->drv->bus_mode = 0;
    if (dev->drv->bus_type >= ZIP_BUS_ATAPI)
	dev->drv->bus_mode |= 2;
    if (dev->drv->bus_type < ZIP_BUS_SCSI)
	dev->drv->bus_mode |= 1;
    zip_log("ZIP %i: Bus type %i, bus mode %i\n", dev->id, dev->drv->bus_type, dev->drv->bus_mode);
    if (dev->drv->bus_type < ZIP_BUS_SCSI)
	zip_set_signature(dev);
    dev->status = READY_STAT | DSC_STAT;
    dev->pos = 0;
    dev->packet_status = 0xff;
    zip_sense_key = zip_asc = zip_ascq = dev->unit_attention = 0;
}


static int
zip_supports_pio(zip_t *dev)
{
    return (dev->drv->bus_mode & 1);
}


static int
zip_supports_dma(zip_t *dev)
{
    return (dev->drv->bus_mode & 2);
}


/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
zip_current_mode(zip_t *dev)
{
    if (!zip_supports_pio(dev) && !zip_supports_dma(dev))
	return 0;
    if (zip_supports_pio(dev) && !zip_supports_dma(dev)) {
	zip_log("ZIP %i: Drive does not support DMA, setting to PIO\n", dev->id);
	return 1;
    }
    if (!zip_supports_pio(dev) && zip_supports_dma(dev))
	return 2;
    if (zip_supports_pio(dev) && zip_supports_dma(dev)) {
	zip_log("ZIP %i: Drive supports both, setting to %s\n", dev->id, (dev->features & 1) ? "DMA" : "PIO");
	return (dev->features & 1) ? 2 : 1;
    }

    return 0;
}


/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
static int
zip_err_stat_to_scsi(void *p)
{
    zip_t *dev = (zip_t *) p;

    if (dev->status & ERR_STAT)
	return SCSI_STATUS_CHECK_CONDITION;
    else
	return SCSI_STATUS_OK;
}


/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int
zip_atapi_phase_to_scsi(zip_t *dev)
{
    if (dev->status & 8) {
	switch (dev->phase & 3) {
		case 0:
			return 0;
		case 1:
			return 2;
		case 2:
			return 1;
		case 3:
			return 7;
	}
    } else {
	if ((dev->phase & 3) == 3)
		return 3;
	else
		return 4;
    }

    return 0;
}


static void
zip_mode_sense_load(zip_t *dev)
{
    FILE *f;
    wchar_t file_name[512];
    int i;

    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
    for (i = 0; i < 0x3f; i++) {
	if (dev->drv->is_250) {
		if (zip_250_mode_sense_pages_default.pages[i][1] != 0) {
			if (zip_drives[dev->id].bus_type == ZIP_BUS_SCSI) {
				memcpy(dev->ms_pages_saved.pages[i],
				       zip_250_mode_sense_pages_default_scsi.pages[i],
				       zip_250_mode_sense_pages_default_scsi.pages[i][1] + 2);
			} else {
				memcpy(dev->ms_pages_saved.pages[i],
				       zip_250_mode_sense_pages_default.pages[i],
				       zip_250_mode_sense_pages_default.pages[i][1] + 2);
			}
		}
	} else {
		if (zip_mode_sense_pages_default.pages[i][1] != 0) {
			if (dev->drv->bus_type == ZIP_BUS_SCSI) {
				memcpy(dev->ms_pages_saved.pages[i],
				       zip_mode_sense_pages_default_scsi.pages[i],
				       zip_mode_sense_pages_default_scsi.pages[i][1] + 2);
			} else {
				memcpy(dev->ms_pages_saved.pages[i],
				       zip_mode_sense_pages_default.pages[i],
				       zip_mode_sense_pages_default.pages[i][1] + 2);
			}
		}
	}
    }
    memset(file_name, 0, 512 * sizeof(wchar_t));
    if (dev->drv->bus_type == ZIP_BUS_SCSI)
	swprintf(file_name, 512, L"scsi_zip_%02i_mode_sense_bin", dev->id);
    else
	swprintf(file_name, 512, L"zip_%02i_mode_sense_bin", dev->id);
    f = plat_fopen(nvr_path(file_name), L"rb");
    if (f)
	fclose(f);
}


static void
zip_mode_sense_save(zip_t *dev)
{
    FILE *f;
    wchar_t file_name[512];

    memset(file_name, 0, 512 * sizeof(wchar_t));
    if (dev->drv->bus_type == ZIP_BUS_SCSI)
	swprintf(file_name, 512, L"scsi_zip_%02i_mode_sense_bin", dev->id);
    else
	swprintf(file_name, 512, L"zip_%02i_mode_sense_bin", dev->id);
    f = plat_fopen(nvr_path(file_name), L"wb");
    if (f)
	fclose(f);
}


static int
zip_read_capacity(void *p, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
    zip_t *dev = (zip_t *) p;
    int size = 0;

    if (dev->drv->is_250)
	size = dev->drv->medium_size - 1;	/* IMPORTANT: What's returned is the last LBA block. */
    else
	size = ZIP_SECTORS - 1;			/* IMPORTANT: What's returned is the last LBA block. */
    memset(buffer, 0, 8);
    buffer[0] = (size >> 24) & 0xff;
    buffer[1] = (size >> 16) & 0xff;
    buffer[2] = (size >> 8) & 0xff;
    buffer[3] = size & 0xff;
    buffer[6] = 2;				/* 512 = 0x0200 */
    *len = 8;

    return 1;
}


/*SCSI Mode Sense 6/10*/
static uint8_t
zip_mode_sense_read(zip_t *dev, uint8_t page_control, uint8_t page, uint8_t pos)
{
    switch (page_control) {
	case 0:
	case 3:
		if (dev->drv->is_250 && (page == 5) && (pos == 9) && (dev->drv->medium_size == ZIP_SECTORS))
			return 0x60;
		return dev->ms_pages_saved.pages[page][pos];
		break;
	case 1:
		if (dev->drv->is_250)
			return zip_250_mode_sense_pages_changeable.pages[page][pos];
		else
			return zip_mode_sense_pages_changeable.pages[page][pos];
		break;
	case 2:
		if (dev->drv->is_250) {
			if ((page == 5) && (pos == 9) && (dev->drv->medium_size == ZIP_SECTORS))
				return 0x60;
			if (dev->drv->bus_type == ZIP_BUS_SCSI)
				return zip_250_mode_sense_pages_default_scsi.pages[page][pos];
			else
				return zip_250_mode_sense_pages_default.pages[page][pos];
		} else {
			if (dev->drv->bus_type == ZIP_BUS_SCSI)
				return zip_mode_sense_pages_default_scsi.pages[page][pos];
			else
				return zip_mode_sense_pages_default.pages[page][pos];
		}
		break;
    }

    return 0;
}


static uint32_t
zip_mode_sense(zip_t *dev, uint8_t *buf, uint32_t pos, uint8_t page, uint8_t block_descriptor_len)
{
    uint64_t pf;
    uint8_t page_control = (page >> 6) & 3;

    if (dev->drv->is_250)
	pf = zip_250_mode_sense_page_flags;
    else
	pf = zip_mode_sense_page_flags;

    int i = 0;
    int j = 0;

    uint8_t msplen;

    page &= 0x3f;

    if (block_descriptor_len) {
	if (dev->drv->is_250) {
		buf[pos++] = ((dev->drv->medium_size >> 24) & 0xff);
		buf[pos++] = ((dev->drv->medium_size >> 16) & 0xff);
		buf[pos++] = ((dev->drv->medium_size >>  8) & 0xff);
		buf[pos++] = ( dev->drv->medium_size        & 0xff);
	} else {
		buf[pos++] = ((ZIP_SECTORS >> 24) & 0xff);
		buf[pos++] = ((ZIP_SECTORS >> 16) & 0xff);
		buf[pos++] = ((ZIP_SECTORS >>  8) & 0xff);
		buf[pos++] = ( ZIP_SECTORS        & 0xff);
	}
	buf[pos++] = 0;		/* Reserved. */
	buf[pos++] = 0;		/* Block length (0x200 = 512 bytes). */
	buf[pos++] = 2;
	buf[pos++] = 0;
    }

    for (i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
		if (pf & (1LL << ((uint64_t) page))) {
			buf[pos++] = zip_mode_sense_read(dev, page_control, i, 0);
			msplen = zip_mode_sense_read(dev, page_control, i, 1);
			buf[pos++] = msplen;
			zip_log("ZIP %i: MODE SENSE: Page [%02X] length %i\n", dev->id, i, msplen);
			for (j = 0; j < msplen; j++)
				buf[pos++] = zip_mode_sense_read(dev, page_control, i, 2 + j);
		}
	}
    }

    return pos;
}


static void
zip_update_request_length(zip_t *dev, int len, int block_len)
{
    int bt, min_len = 0;

    dev->max_transfer_len = dev->request_length;

    /* For media access commands, make sure the requested DRQ length matches the block length. */
    switch (dev->current_cdb[0]) {
	case 0x08:
	case 0x28:
	case 0xa8:
		/* Make sure total length is not bigger than sum of the lengths of
		   all the requested blocks. */
		bt = (dev->requested_blocks * block_len);
		if (len > bt)
			len = bt;

		min_len = block_len;

		if (len <= block_len) {
			/* Total length is less or equal to block length. */
			if (dev->max_transfer_len < block_len) {
				/* Transfer a minimum of (block size) bytes. */
				dev->max_transfer_len = block_len;
				dev->packet_len = block_len;
				break;
			}
		}
	default:
		dev->packet_len = len;
		break;
    }
    /* If the DRQ length is odd, and the total remaining length is bigger, make sure it's even. */
    if ((dev->max_transfer_len & 1) && (dev->max_transfer_len < len))
	dev->max_transfer_len &= 0xfffe;
    /* If the DRQ length is smaller or equal in size to the total remaining length, set it to that. */
    if (!dev->max_transfer_len)
	dev->max_transfer_len = 65534;

    if ((len <= dev->max_transfer_len) && (len >= min_len))
	dev->request_length = dev->max_transfer_len = len;
    else if (len > dev->max_transfer_len)
	dev->request_length = dev->max_transfer_len;

    return;
}


static void
zip_command_bus(zip_t *dev)
{
    dev->status = BUSY_STAT;
    dev->phase = 1;
    dev->pos = 0;
    dev->callback = 1LL * ZIP_TIME;
    zip_set_callback(dev);
}


static void
zip_command_common(zip_t *dev)
{
    double bytes_per_second, period;
    double dusec;

    dev->status = BUSY_STAT;
    dev->phase = 1;
    dev->pos = 0;
    if (dev->packet_status == PHASE_COMPLETE) {
	zip_callback((void *) dev);
	dev->callback = 0LL;
    } else {
	if (dev->drv->bus_type == ZIP_BUS_SCSI) {
		dev->callback = -1LL;	/* Speed depends on SCSI controller */
		return;
	} else {
		if (zip_current_mode(dev) == 2)
			bytes_per_second = 66666666.666666666666666;	/* 66 MB/s MDMA-2 speed */
		else
			bytes_per_second =  8333333.333333333333333;	/* 8.3 MB/s PIO-2 speed */
	}

	period = 1000000.0 / bytes_per_second;
	dusec = (double) TIMER_USEC;
	dusec = dusec * period * (double) (dev->packet_len);
	dev->callback = ((int64_t) dusec);
    }

    zip_set_callback(dev);
}


static void
zip_command_complete(zip_t *dev)
{
    dev->packet_status = PHASE_COMPLETE;
    zip_command_common(dev);
}


static void
zip_command_read(zip_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    zip_command_common(dev);
}


static void
zip_command_read_dma(zip_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    zip_command_common(dev);
}


static void
zip_command_write(zip_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    zip_command_common(dev);
}


static void
zip_command_write_dma(zip_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    zip_command_common(dev);
}


/* id = Current ZIP device ID;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void
zip_data_command_finish(zip_t *dev, int len, int block_len, int alloc_len, int direction)
{
    zip_log("ZIP %i: Finishing command (%02X): %i, %i, %i, %i, %i\n",
	    dev->id, dev->current_cdb[0], len, block_len, alloc_len, direction, dev->request_length);
    dev->pos = 0;
    if (alloc_len >= 0) {
	if (alloc_len < len)
		len = alloc_len;
    }
    if ((len == 0) || (zip_current_mode(dev) == 0)) {
	if (dev->drv->bus_type != ZIP_BUS_SCSI)
		dev->packet_len = 0;

	zip_command_complete(dev);
    } else {
	if (zip_current_mode(dev) == 2) {
		if (dev->drv->bus_type != ZIP_BUS_SCSI)
			dev->packet_len = alloc_len;

		if (direction == 0)
			zip_command_read_dma(dev);
		else
			zip_command_write_dma(dev);
	} else {
		zip_update_request_length(dev, len, block_len);
		if (direction == 0)
			zip_command_read(dev);
		else
			zip_command_write(dev);
	}
    }

    zip_log("ZIP %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
	    dev->id, dev->packet_status, dev->request_length, dev->packet_len, dev->pos, dev->phase);
}


static void
zip_sense_clear(zip_t *dev, int command)
{
    zip_sense_key = zip_asc = zip_ascq = 0;
}


static void
zip_set_phase(zip_t *dev, uint8_t phase)
{
    uint8_t scsi_id = dev->drv->scsi_device_id;

    if (dev->drv->bus_type != ZIP_BUS_SCSI)
	return;

    scsi_devices[scsi_id].phase = phase;
}


static void
zip_cmd_error(zip_t *dev)
{
    zip_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = ((zip_sense_key & 0xf) << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = 0x80;
    dev->callback = 50LL * ZIP_TIME;
    zip_set_callback(dev);
    zip_log("ZIP %i: [%02X] ERROR: %02X/%02X/%02X\n", dev->id, dev->current_cdb[0], zip_sense_key, zip_asc, zip_ascq);
}


static void
zip_unit_attention(zip_t *dev)
{
    zip_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = 0x80;
    dev->callback = 50LL * ZIP_TIME;
    zip_set_callback(dev);
    zip_log("ZIP %i: UNIT ATTENTION\n", dev->id);
}


static void
zip_bus_master_error(zip_t *dev)
{
    zip_sense_key = zip_asc = zip_ascq = 0;
    zip_cmd_error(dev);
}


static void
zip_not_ready(zip_t *dev)
{
    zip_sense_key = SENSE_NOT_READY;
    zip_asc = ASC_MEDIUM_NOT_PRESENT;
    zip_ascq = 0;
    zip_cmd_error(dev);
}


static void
zip_write_protected(zip_t *dev)
{
    zip_sense_key = SENSE_UNIT_ATTENTION;
    zip_asc = ASC_WRITE_PROTECTED;
    zip_ascq = 0;
    zip_cmd_error(dev);
}


static void
zip_invalid_lun(zip_t *dev)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_INV_LUN;
    zip_ascq = 0;
    zip_cmd_error(dev);
}


static void
zip_illegal_opcode(zip_t *dev)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_ILLEGAL_OPCODE;
    zip_ascq = 0;
    zip_cmd_error(dev);
}


static void
zip_lba_out_of_range(zip_t *dev)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_LBA_OUT_OF_RANGE;
    zip_ascq = 0;
    zip_cmd_error(dev);
}


static void
zip_invalid_field(zip_t *dev)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_INV_FIELD_IN_CMD_PACKET;
    zip_ascq = 0;
    zip_cmd_error(dev);
    dev->status = 0x53;
}


static void
zip_invalid_field_pl(zip_t *dev)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
    zip_ascq = 0;
    zip_cmd_error(dev);
    dev->status = 0x53;
}


static void
zip_data_phase_error(zip_t *dev)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_DATA_PHASE_ERROR;
    zip_ascq = 0;
    zip_cmd_error(dev);
}


static int
zip_blocks(zip_t *dev, int32_t *len, int first_batch, int out)
{
    *len = 0;

    if (!dev->sector_len) {
	zip_command_complete(dev);
	return -1;
    }

    zip_log("%sing %i blocks starting from %i...\n", out ? "Writ" : "Read", dev->requested_blocks, dev->sector_pos);

    if (dev->sector_pos >= dev->drv->medium_size) {
	zip_log("ZIP %i: Trying to %s beyond the end of disk\n", dev->id, out ? "write" : "read");
	zip_lba_out_of_range(dev);
	return 0;
    }

    *len = dev->requested_blocks << 9;

    fseek(dev->drv->f, dev->drv->base + (dev->sector_pos << 9), SEEK_SET);
    if (out)
	fwrite(zipbufferb, 1, *len, dev->drv->f);
    else
	fread(zipbufferb, 1, *len, dev->drv->f);

    zip_log("%s %i bytes of blocks...\n", out ? "Written" : "Read", *len);

    dev->sector_pos += dev->requested_blocks;
    dev->sector_len -= dev->requested_blocks;

    return 1;
}


void
zip_insert(zip_t *dev)
{
    dev->unit_attention = 1;
}


/*SCSI Sense Initialization*/
void
zip_sense_code_ok(zip_t *dev)
{	
    zip_sense_key = SENSE_NONE;
    zip_asc = 0;
    zip_ascq = 0;
}


static int
zip_pre_execution_check(zip_t *dev, uint8_t *cdb)
{
    int ready = 0;

    if (dev->drv->bus_type == ZIP_BUS_SCSI) {
	if ((cdb[0] != GPCMD_REQUEST_SENSE) && (cdb[1] & 0xe0)) {
		zip_log("ZIP %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", dev->id, ((dev->request_length >> 5) & 7));
		zip_invalid_lun(dev);
		return 0;
	}
    }

    if (!(zip_command_flags[cdb[0]] & IMPLEMENTED)) {
	zip_log("ZIP %i: Attempting to execute unknown command %02X over %s\n", dev->id, cdb[0],
		(dev->drv->bus_type == ZIP_BUS_SCSI) ? "SCSI" : "ATAPI");

	zip_illegal_opcode(dev);
	return 0;
    }

    if ((dev->drv->bus_type < ZIP_BUS_SCSI) && (zip_command_flags[cdb[0]] & SCSI_ONLY)) {
	zip_log("ZIP %i: Attempting to execute SCSI-only command %02X over ATAPI\n", dev->id, cdb[0]);
	zip_illegal_opcode(dev);
	return 0;
    }

    if ((dev->drv->bus_type == ZIP_BUS_SCSI) && (zip_command_flags[cdb[0]] & ATAPI_ONLY)) {
	zip_log("ZIP %i: Attempting to execute ATAPI-only command %02X over SCSI\n", dev->id, cdb[0]);
	zip_illegal_opcode(dev);
	return 0;
    }

    ready = (dev->drv->f != NULL);

    /* If the drive is not ready, there is no reason to keep the
       UNIT ATTENTION condition present, as we only use it to mark
       disc changes. */
    if (!ready && dev->unit_attention)
	dev->unit_attention = 0;

    /* If the UNIT ATTENTION condition is set and the command does not allow
       execution under it, error out and report the condition. */
    if (dev->unit_attention == 1) {
	/* Only increment the unit attention phase if the command can not pass through it. */
	if (!(zip_command_flags[cdb[0]] & ALLOW_UA)) {
		/* zip_log("ZIP %i: Unit attention now 2\n", dev->id); */
		dev->unit_attention = 2;
		zip_log("ZIP %i: UNIT ATTENTION: Command %02X not allowed to pass through\n", dev->id, cdb[0]);
		zip_unit_attention(dev);
		return 0;
	}
    } else if (dev->unit_attention == 2) {
	if (cdb[0] != GPCMD_REQUEST_SENSE) {
		/* zip_log("ZIP %i: Unit attention now 0\n", dev->id); */
		dev->unit_attention = 0;
	}
    }

    /* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
       the UNIT ATTENTION condition if it's set. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
	zip_sense_clear(dev, cdb[0]);

    /* Next it's time for NOT READY. */
    if ((zip_command_flags[cdb[0]] & CHECK_READY) && !ready) {
	zip_log("ZIP %i: Not ready (%02X)\n", dev->id, cdb[0]);
	zip_not_ready(dev);
	return 0;
    }

    zip_log("ZIP %i: Continuing with command %02X\n", dev->id, cdb[0]);

    return 1;
}


static void
zip_seek(zip_t *dev, uint32_t pos)
{
    /* zip_log("ZIP %i: Seek %08X\n", dev->id, pos); */
    dev->sector_pos   = pos;
}


static void
zip_rezero(zip_t *dev)
{
    dev->sector_pos = dev->sector_len = 0;
    zip_seek(dev, 0);
}


void
zip_reset(void *p)
{
    zip_t *dev = (zip_t *) p;

    zip_rezero(dev);
    dev->status = 0;
    dev->callback = 0LL;
    zip_set_callback(dev);
    zip_set_signature(dev);
    dev->packet_status = 0xff;
    dev->unit_attention = 0;
}


static void
zip_request_sense(zip_t *dev, uint8_t *buffer, uint8_t alloc_length, int desc)
{				
    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
	memset(buffer, 0, alloc_length);
	if (!desc)
		memcpy(buffer, dev->sense, alloc_length);
	else {
		buffer[1] = zip_sense_key;
		buffer[2] = zip_asc;
		buffer[3] = zip_ascq;
	}
    }

    buffer[0] = desc ? 0x72 : 0x70;

    if (dev->unit_attention && (zip_sense_key == 0)) {
	buffer[desc ? 1 : 2] = SENSE_UNIT_ATTENTION;
	buffer[desc ? 2 : 12] = ASC_MEDIUM_MAY_HAVE_CHANGED;
	buffer[desc ? 3 : 13] = 0;
    }

    zip_log("ZIP %i: Reporting sense: %02X %02X %02X\n", dev->id, buffer[2], buffer[12], buffer[13]);

    if (buffer[desc ? 1 : 2] == SENSE_UNIT_ATTENTION) {
	/* If the last remaining sense is unit attention, clear
	   that condition. */
	dev->unit_attention = 0;
    }

    /* Clear the sense stuff as per the spec. */
    zip_sense_clear(dev, GPCMD_REQUEST_SENSE);
}


static void
zip_request_sense_for_scsi(void *p, uint8_t *buffer, uint8_t alloc_length)
{
    zip_t *dev = (zip_t *) p;
    int ready = 0;

    ready = (dev->drv->f != NULL);

    if (!ready && dev->unit_attention) {
	/* If the drive is not ready, there is no reason to keep the
	   UNIT ATTENTION condition present, as we only use it to mark
	   disc changes. */
	dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */

    zip_request_sense(dev, buffer, alloc_length, 0);
}


static void
zip_set_buf_len(zip_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus_type == ZIP_BUS_SCSI) {
	if (*BufLen == -1)
		*BufLen = *src_len;
	else {
		*BufLen = MIN(*src_len, *BufLen);
		*src_len = *BufLen;
	}
	zip_log("ZIP %i: Actual transfer length: %i\n", dev->id, *BufLen);
    }
}


static void
zip_buf_alloc(zip_t *dev, uint32_t len)
{
    zip_log("ZIP %i: Allocated buffer length: %i\n", dev->id, len);
    zipbufferb = (uint8_t *) malloc(len);
}


static void
zip_buf_free(zip_t *dev)
{
    if (zipbufferb) {
	zip_log("ZIP %i: Freeing buffer...\n", dev->id);
	free(zipbufferb);
	zipbufferb = NULL;
    }
}


static void
zip_command(void *p, uint8_t *cdb)
{
    zip_t *dev = (zip_t *) p;
    int pos = 0, block_desc = 0;
    int ret;
    int32_t len, max_len;
    int32_t alloc_length;
    uint32_t i = 0;
    int size_idx, idx = 0;
    unsigned preamble_len;
    int32_t blen = 0;
    int32_t *BufLen;

    if (dev->drv->bus_type == ZIP_BUS_SCSI) {
	BufLen = &scsi_devices[dev->drv->scsi_device_id].buffer_length;
	dev->status &= ~ERR_STAT;
    } else {
	BufLen = &blen;
	dev->error = 0;
    }

    dev->packet_len = 0;
    dev->request_pos = 0;

    memcpy(dev->current_cdb, cdb, 12);

    if (cdb[0] != 0) {
	zip_log("ZIP %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, Unit attention: %i\n",
		dev->id, cdb[0], zip_sense_key, zip_asc, zip_ascq, dev->unit_attention);
	zip_log("ZIP %i: Request length: %04X\n", dev->id, dev->request_length);

	zip_log("ZIP %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
		cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
		cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    zip_set_phase(dev, SCSI_PHASE_STATUS);

    /* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
    if (zip_pre_execution_check(dev, cdb) == 0)
	return;

    switch (cdb[0]) {
	case GPCMD_SEND_DIAGNOSTIC:
		if (!(cdb[1] & (1 << 2))) {
			zip_invalid_field(dev);
			return;
		}
	case GPCMD_SCSI_RESERVE:
	case GPCMD_SCSI_RELEASE:
	case GPCMD_TEST_UNIT_READY:
		zip_set_phase(dev, SCSI_PHASE_STATUS);
		zip_command_complete(dev);
		break;

	case GPCMD_FORMAT_UNIT:
		if ((dev->drv->bus_type == ZIP_BUS_SCSI) && dev->drv->read_only) {
			zip_write_protected(dev);
			return;
		}

		zip_set_phase(dev, SCSI_PHASE_STATUS);
		zip_command_complete(dev);
		break;

	case GPCMD_IOMEGA_SENSE:
		zip_set_phase(dev, SCSI_PHASE_DATA_IN);
		max_len = cdb[4];
		zip_buf_alloc(dev, 256);
		zip_set_buf_len(dev, BufLen, &max_len);
		memset(zipbufferb, 0, 256);
		if (cdb[2] == 1) {
			/* This page is related to disk health status - setting
			   this page to 0 makes disk health read as "marginal". */
			zipbufferb[0] = 0x58;
			zipbufferb[1] = 0x00;
			for (i = 0x00; i < 0x58; i++)
				zipbufferb[i + 0x02] = 0xff;
		} else if (cdb[2] == 2) {
			zipbufferb[0] = 0x3d;
			zipbufferb[1] = 0x00;
			for (i = 0x00; i < 0x13; i++)
				zipbufferb[i + 0x02] = 0x00;
			zipbufferb[0x15] = 0x00;
			if (dev->drv->read_only)
				zipbufferb[0x15] |= 0x02;
			for (i = 0x00; i < 0x27; i++)
				zipbufferb[i + 0x16] = 0x00;
		} else {
			zip_invalid_field(dev);
			zip_buf_free(dev);
			return;
		}
		zip_data_command_finish(dev, 18, 18, cdb[4], 0);
		break;

	case GPCMD_REZERO_UNIT:
		dev->sector_pos = dev->sector_len = 0;
		zip_seek(dev, 0);
		zip_set_phase(dev, SCSI_PHASE_STATUS);
		break;

	case GPCMD_REQUEST_SENSE:
		/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
		   should forget about the not ready, and report unit attention straight away. */
		zip_set_phase(dev, SCSI_PHASE_DATA_IN);
		max_len = cdb[4];

		if (!max_len) {
			zip_set_phase(dev, SCSI_PHASE_STATUS);
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20LL * ZIP_TIME;
			zip_set_callback(dev);
			break;
		}

		zip_buf_alloc(dev, 256);
		zip_set_buf_len(dev, BufLen, &max_len);
		len = (cdb[1] & 1) ? 8 : 18;
		zip_request_sense(dev, zipbufferb, max_len, cdb[1] & 1);
		zip_data_command_finish(dev, len, len, cdb[4], 0);
		break;

	case GPCMD_MECHANISM_STATUS:
		zip_set_phase(dev, SCSI_PHASE_DATA_IN);
		len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

		zip_buf_alloc(dev, 8);

		zip_set_buf_len(dev, BufLen, &len);

		memset(zipbufferb, 0, 8);
		zipbufferb[5] = 1;

		zip_data_command_finish(dev, 8, 8, len, 0);
		break;

	case GPCMD_READ_6:
	case GPCMD_READ_10:
	case GPCMD_READ_12:
		zip_set_phase(dev, SCSI_PHASE_DATA_IN);
		alloc_length = 512;

		switch(cdb[0]) {
			case GPCMD_READ_6:
				dev->sector_len = cdb[4];
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				zip_log("ZIP %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_READ_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				zip_log("ZIP %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_READ_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		if (!dev->sector_len) {
			zip_set_phase(dev, SCSI_PHASE_STATUS);
			/* zip_log("ZIP %i: All done - callback set\n", dev->id); */
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20LL * ZIP_TIME;
			zip_set_callback(dev);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're reading all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		zip_buf_alloc(dev, dev->packet_len);

		ret = zip_blocks(dev, &alloc_length, 1, 0);
		if (ret <= 0) {
			zip_buf_free(dev);
			return;
		}

		dev->requested_blocks = max_len;
		dev->packet_len = alloc_length;

		zip_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

		zip_data_command_finish(dev, alloc_length, 512, alloc_length, 0);

		if (dev->packet_status != PHASE_COMPLETE)
			ui_sb_update_icon(SB_ZIP | dev->id, 1);
		else
			ui_sb_update_icon(SB_ZIP | dev->id, 0);
		return;

	case GPCMD_VERIFY_6:
	case GPCMD_VERIFY_10:
	case GPCMD_VERIFY_12:
		if (!(cdb[1] & 2)) {
			zip_set_phase(dev, SCSI_PHASE_STATUS);
			zip_command_complete(dev);
			break;
		}
	case GPCMD_WRITE_6:
	case GPCMD_WRITE_10:
	case GPCMD_WRITE_AND_VERIFY_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_12:
		zip_set_phase(dev, SCSI_PHASE_DATA_OUT);
		alloc_length = 512;

		if ((dev->drv->bus_type == ZIP_BUS_SCSI) && dev->drv->read_only) {
			zip_write_protected(dev);
			return;
		}

		switch(cdb[0]) {
			case GPCMD_VERIFY_6:
			case GPCMD_WRITE_6:
				dev->sector_len = cdb[4];
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				break;
			case GPCMD_VERIFY_10:
			case GPCMD_WRITE_10:
			case GPCMD_WRITE_AND_VERIFY_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				zip_log("ZIP %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_VERIFY_12:
			case GPCMD_WRITE_12:
			case GPCMD_WRITE_AND_VERIFY_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		if (dev->drv->is_250) {
			if ((dev->sector_pos >= dev->drv->medium_size) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= dev->drv->medium_size)) {
				zip_lba_out_of_range(dev);
				return;
			}
		} else {
			if ((dev->sector_pos >= ZIP_SECTORS) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= ZIP_SECTORS)) {
				zip_lba_out_of_range(dev);
				return;
			}
		}

		if (!dev->sector_len) {
			zip_set_phase(dev, SCSI_PHASE_STATUS);
			/* zip_log("ZIP %i: All done - callback set\n", dev->id); */
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20LL * ZIP_TIME;
			zip_set_callback(dev);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're writing all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		zip_buf_alloc(dev, dev->packet_len);

		dev->requested_blocks = max_len;
		dev->packet_len = max_len << 9;

		zip_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

		zip_data_command_finish(dev, dev->packet_len, 512, dev->packet_len, 1);

		if (dev->packet_status != PHASE_COMPLETE)
			ui_sb_update_icon(SB_ZIP | dev->id, 1);
		else
			ui_sb_update_icon(SB_ZIP | dev->id, 0);
		return;

	case GPCMD_WRITE_SAME_10:
		zip_set_phase(dev, SCSI_PHASE_DATA_OUT);
		alloc_length = 512;

		if ((cdb[1] & 6) == 6) {
			zip_invalid_field(dev);
			return;
		}

		if ((dev->drv->bus_type == ZIP_BUS_SCSI) && dev->drv->read_only) {
			zip_write_protected(dev);
			return;
		}

		dev->sector_len = (cdb[7] << 8) | cdb[8];
		dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

		if (dev->drv->is_250) {
			if ((dev->sector_pos >= dev->drv->medium_size) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= dev->drv->medium_size)) {
				zip_lba_out_of_range(dev);
				return;
			}
		} else {
			if ((dev->sector_pos >= ZIP_SECTORS) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= ZIP_SECTORS)) {
				zip_lba_out_of_range(dev);
				return;
			}
		}

		if (!dev->sector_len) {
			zip_set_phase(dev, SCSI_PHASE_STATUS);
			/* zip_log("ZIP %i: All done - callback set\n", dev->id); */
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20LL * ZIP_TIME;
			zip_set_callback(dev);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're writing all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		zip_buf_alloc(dev, dev->packet_len);

		dev->requested_blocks = max_len;
		dev->packet_len = alloc_length;

		zip_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

		zip_data_command_finish(dev, dev->packet_len, 512, dev->packet_len, 1);

		if (dev->packet_status != PHASE_COMPLETE)
			ui_sb_update_icon(SB_ZIP | dev->id, 1);
		else
			ui_sb_update_icon(SB_ZIP | dev->id, 0);
		return;

	case GPCMD_MODE_SENSE_6:
	case GPCMD_MODE_SENSE_10:
		zip_set_phase(dev, SCSI_PHASE_DATA_IN);

		if (dev->drv->bus_type == ZIP_BUS_SCSI)
			block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
		else
			block_desc = 0;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = cdb[4];
			zip_buf_alloc(dev, 256);
		} else {
			len = (cdb[8] | (cdb[7] << 8));
			zip_buf_alloc(dev, 65536);
		}

		if (!(zip_mode_sense_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
			zip_invalid_field(dev);
			zip_buf_free(dev);
			return;
		}

		memset(zipbufferb, 0, len);
		alloc_length = len;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = zip_mode_sense(dev, zipbufferb, 4, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			zipbufferb[0] = len - 1;
			zipbufferb[1] = 0;
			if (block_desc)
				zipbufferb[3] = 8;
		} else {
			len = zip_mode_sense(dev, zipbufferb, 8, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			zipbufferb[0]=(len - 2) >> 8;
			zipbufferb[1]=(len - 2) & 255;
			zipbufferb[2] = 0;
			if (block_desc) {
				zipbufferb[6] = 0;
				zipbufferb[7] = 8;
			}
		}

		zip_set_buf_len(dev, BufLen, &len);

		zip_log("ZIP %i: Reading mode page: %02X...\n", dev->id, cdb[2]);

		zip_data_command_finish(dev, len, len, alloc_length, 0);
		return;

	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		zip_set_phase(dev, SCSI_PHASE_DATA_OUT);

		if (cdb[0] == GPCMD_MODE_SELECT_6) {
			len = cdb[4];
			zip_buf_alloc(dev, 256);
		} else {
			len = (cdb[7] << 8) | cdb[8];
			zip_buf_alloc(dev, 65536);
		}

		zip_set_buf_len(dev, BufLen, &len);

		dev->total_length = len;
		dev->do_page_save = cdb[1] & 1;

		zip_data_command_finish(dev, len, len, len, 1);
		return;

	case GPCMD_START_STOP_UNIT:
		zip_set_phase(dev, SCSI_PHASE_STATUS);

		switch(cdb[4] & 3) {
			case 0:		/* Stop the disc. */
				zip_eject(dev->id);	/* The Iomega Windows 9x drivers require this. */
				break;
			case 1:		/* Start the disc and read the TOC. */
				break;
			case 2:		/* Eject the disc if possible. */
				/* zip_eject(dev->id); */
				break;
			case 3:		/* Load the disc (close tray). */
				zip_reload(dev->id);
				break;
		}

		zip_command_complete(dev);
		break;

	case GPCMD_INQUIRY:
		zip_set_phase(dev, SCSI_PHASE_DATA_IN);

		max_len = cdb[3];
		max_len <<= 8;
		max_len |= cdb[4];

		zip_buf_alloc(dev, 65536);

		if (cdb[1] & 1) {
			preamble_len = 4;
			size_idx = 3;

			zipbufferb[idx++] = 05;
			zipbufferb[idx++] = cdb[2];
			zipbufferb[idx++] = 0;

			idx++;

			switch (cdb[2]) {
				case 0x00:
					zipbufferb[idx++] = 0x00;
					zipbufferb[idx++] = 0x83;
					break;
				case 0x83:
					if (idx + 24 > max_len) {
						zip_data_phase_error(dev);
						zip_buf_free(dev);
						return;
					}

					zipbufferb[idx++] = 0x02;
					zipbufferb[idx++] = 0x00;
					zipbufferb[idx++] = 0x00;
					zipbufferb[idx++] = 20;
					ide_padstr8(zipbufferb + idx, 20, "53R141");	/* Serial */
					idx += 20;

					if (idx + 72 > cdb[4])
						goto atapi_out;
					zipbufferb[idx++] = 0x02;
					zipbufferb[idx++] = 0x01;
					zipbufferb[idx++] = 0x00;
					zipbufferb[idx++] = 68;
					ide_padstr8(zipbufferb + idx, 8, "IOMEGA  "); /* Vendor */
					idx += 8;
					if (dev->drv->is_250)
						ide_padstr8(zipbufferb + idx, 40, "ZIP 250         "); /* Product */
					else
						ide_padstr8(zipbufferb + idx, 40, "ZIP 100         "); /* Product */
					idx += 40;
					ide_padstr8(zipbufferb + idx, 20, "53R141"); /* Product */
					idx += 20;
					break;
				default:
					zip_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
					zip_invalid_field(dev);
					zip_buf_free(dev);
					return;
			}
		} else {
			preamble_len = 5;
			size_idx = 4;

			memset(zipbufferb, 0, 8);
			if (cdb[1] & 0xe0)
				zipbufferb[0] = 0x60; /*No physical device on this LUN*/
			else
				zipbufferb[0] = 0x00; /*Hard disk*/
			zipbufferb[1] = 0x80; /*Removable*/
			zipbufferb[2] = (dev->drv->bus_type == ZIP_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
			zipbufferb[3] = (dev->drv->bus_type == ZIP_BUS_SCSI) ? 0x02 : 0x21;
			zipbufferb[4] = 31;
			if (dev->drv->bus_type == ZIP_BUS_SCSI) {
				zipbufferb[6] = 1;	/* 16-bit transfers supported */
				zipbufferb[7] = 0x20;	/* Wide bus supported */
			}

			ide_padstr8(zipbufferb + 8, 8, "IOMEGA  "); /* Vendor */
			if (dev->drv->is_250) {
				ide_padstr8(zipbufferb + 16, 16, "ZIP 250         "); /* Product */
				ide_padstr8(zipbufferb + 32, 4, "42.S"); /* Revision */
				if (max_len >= 44)
					ide_padstr8(zipbufferb + 36, 8, "08/08/01"); /* Date? */
				if (max_len >= 122)
					ide_padstr8(zipbufferb + 96, 26, "(c) Copyright IOMEGA 2000 "); /* Copyright string */
			} else {
				ide_padstr8(zipbufferb + 16, 16, "ZIP 100         "); /* Product */
				ide_padstr8(zipbufferb + 32, 4, "E.08"); /* Revision */
			}
			idx = 36;

			if (max_len == 96) {
				zipbufferb[4] = 91;
				idx = 96;
			} else if (max_len == 128) {
				zipbufferb[4] = 0x75;
				idx = 128;
			}
		}

atapi_out:
		zipbufferb[size_idx] = idx - preamble_len;
		len=idx;

		len = MIN(len, max_len);
		zip_set_buf_len(dev, BufLen, &len);

		zip_data_command_finish(dev, len, len, max_len, 0);
		break;

	case GPCMD_PREVENT_REMOVAL:
		zip_set_phase(dev, SCSI_PHASE_STATUS);
		zip_command_complete(dev);
		break;

	case GPCMD_SEEK_6:
	case GPCMD_SEEK_10:
		zip_set_phase(dev, SCSI_PHASE_STATUS);

		switch(cdb[0]) {
			case GPCMD_SEEK_6:
				pos = (cdb[2] << 8) | cdb[3];
				break;
			case GPCMD_SEEK_10:
				pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				break;
		}
		zip_seek(dev, pos);
		zip_command_complete(dev);
		break;

	case GPCMD_READ_CDROM_CAPACITY:
		zip_set_phase(dev, SCSI_PHASE_DATA_IN);

		zip_buf_alloc(dev, 8);

		if (zip_read_capacity(dev, dev->current_cdb, zipbufferb, (uint32_t *) &len) == 0) {
			zip_buf_free(dev);
			return;
		}

		zip_set_buf_len(dev, BufLen, &len);

		zip_data_command_finish(dev, len, len, len, 0);
		break;

	case GPCMD_IOMEGA_EJECT:
		zip_set_phase(dev, SCSI_PHASE_STATUS);
		zip_eject(dev->id);
		zip_command_complete(dev);
		break;

	case GPCMD_READ_FORMAT_CAPACITIES:
		len = (cdb[7] << 8) | cdb[8];

		zip_buf_alloc(dev, len);
		memset(zipbufferb, 0, len);

		pos = 0;

		/* List header */
		zipbufferb[pos++] = 0;
		zipbufferb[pos++] = 0;
		zipbufferb[pos++] = 0;
		if (dev->drv->f != NULL)
			zipbufferb[pos++] = 16;
		else
			zipbufferb[pos++] = 8;

		/* Current/Maximum capacity header */
		if (dev->drv->is_250) {
			if (dev->drv->f != NULL) {
				zipbufferb[pos++] = (dev->drv->medium_size >> 24) & 0xff;
				zipbufferb[pos++] = (dev->drv->medium_size >> 16) & 0xff;
				zipbufferb[pos++] = (dev->drv->medium_size >> 8)  & 0xff;
				zipbufferb[pos++] =  dev->drv->medium_size        & 0xff;
				zipbufferb[pos++] = 2;	/* Current medium capacity */
			} else {
				zipbufferb[pos++] = (ZIP_250_SECTORS >> 24) & 0xff;
				zipbufferb[pos++] = (ZIP_250_SECTORS >> 16) & 0xff;
				zipbufferb[pos++] = (ZIP_250_SECTORS >> 8)  & 0xff;
				zipbufferb[pos++] =  ZIP_250_SECTORS        & 0xff;
				zipbufferb[pos++] = 3;	/* Maximum medium capacity */
			}
		} else {
			zipbufferb[pos++] = (ZIP_SECTORS >> 24) & 0xff;
			zipbufferb[pos++] = (ZIP_SECTORS >> 16) & 0xff;
			zipbufferb[pos++] = (ZIP_SECTORS >> 8)  & 0xff;
			zipbufferb[pos++] =  ZIP_SECTORS        & 0xff;
			if (dev->drv->f != NULL)
				zipbufferb[pos++] = 2;
			else
				zipbufferb[pos++] = 3;
		}

		zipbufferb[pos++] = 512 >> 16;
		zipbufferb[pos++] = 512 >> 8;
		zipbufferb[pos++] = 512 & 0xff;

		if (dev->drv->f != NULL) {
			/* Formattable capacity descriptor */
			zipbufferb[pos++] = (dev->drv->medium_size >> 24) & 0xff;
			zipbufferb[pos++] = (dev->drv->medium_size >> 16) & 0xff;
			zipbufferb[pos++] = (dev->drv->medium_size >> 8)  & 0xff;
			zipbufferb[pos++] =  dev->drv->medium_size        & 0xff;
			zipbufferb[pos++] = 0;
			zipbufferb[pos++] = 512 >> 16;
			zipbufferb[pos++] = 512 >> 8;
			zipbufferb[pos++] = 512 & 0xff;
		}

		zip_set_buf_len(dev, BufLen, &len);

		zip_data_command_finish(dev, len, len, len, 0);
		break;

	default:
		zip_illegal_opcode(dev);
		break;
    }

    /* zip_log("ZIP %i: Phase: %02X, request length: %i\n", dev->id, dev->phase, dev->request_length); */

    if (zip_atapi_phase_to_scsi(dev) == SCSI_PHASE_STATUS)
	zip_buf_free(dev);
}


/* The command second phase function, needed for Mode Select. */
static uint8_t
zip_phase_data_out(zip_t *dev)
{
    uint16_t block_desc_len;
    uint16_t pos;

    uint8_t error = 0;
    uint8_t page, page_len;

    uint32_t i = 0;

    uint8_t hdr_len, val, old_val, ch;

    uint32_t last_to_write = 0;
    uint32_t c, h, s;

    int len = 0;

    switch(dev->current_cdb[0]) {
	case GPCMD_VERIFY_6:
	case GPCMD_VERIFY_10:
	case GPCMD_VERIFY_12:
		break;
	case GPCMD_WRITE_6:
	case GPCMD_WRITE_10:
	case GPCMD_WRITE_AND_VERIFY_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_12:
		if (dev->requested_blocks > 0)
			zip_blocks(dev, &len, 1, 1);
		break;
	case GPCMD_WRITE_SAME_10:
		if (!dev->current_cdb[7] && !dev->current_cdb[8]) {
			if (dev->drv->is_250)
				last_to_write = (dev->drv->medium_size - 1);
			else
				last_to_write = (ZIP_SECTORS - 1);
		} else
			last_to_write = dev->sector_pos + dev->sector_len - 1;

		for (i = dev->sector_pos; i <= last_to_write; i++) {
			if (dev->current_cdb[1] & 2) {
				zipbufferb[0] = (i >> 24) & 0xff;
				zipbufferb[1] = (i >> 16) & 0xff;
				zipbufferb[2] = (i >> 8) & 0xff;
				zipbufferb[3] = i & 0xff;
			} else if (dev->current_cdb[1] & 4) {
				/* CHS are 96,1,2048 (ZIP 100) and 239,1,2048 (ZIP 250) */
				s = (i % 2048);
				h = ((i - s) / 2048) % 1;
				c = ((i - s) / 2048) / 1;
				zipbufferb[0] = (c >> 16) & 0xff;
				zipbufferb[1] = (c >> 8) & 0xff;
				zipbufferb[2] = c & 0xff;
				zipbufferb[3] = h & 0xff;
				zipbufferb[4] = (s >> 24) & 0xff;
				zipbufferb[5] = (s >> 16) & 0xff;
				zipbufferb[6] = (s >> 8) & 0xff;
				zipbufferb[7] = s & 0xff;
			}
			fseek(dev->drv->f, dev->drv->base + (i << 9), SEEK_SET);
			fwrite(zipbufferb, 1, 512, dev->drv->f);
		}
		break;
	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10)
			hdr_len = 8;
		else
			hdr_len = 4;

		if (dev->drv->bus_type == ZIP_BUS_SCSI) {
			if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
				block_desc_len = zipbufferb[2];
				block_desc_len <<= 8;
				block_desc_len |= zipbufferb[3];
			} else {
				block_desc_len = zipbufferb[6];
				block_desc_len <<= 8;
				block_desc_len |= zipbufferb[7];
			}
		} else
			block_desc_len = 0;

		pos = hdr_len + block_desc_len;

		while(1) {
			page = zipbufferb[pos] & 0x3F;
			page_len = zipbufferb[pos + 1];

			pos += 2;

			if (!(zip_mode_sense_page_flags & (1LL << ((uint64_t) page))))
				error |= 1;
			else {
				for (i = 0; i < page_len; i++) {
					ch = zip_mode_sense_pages_changeable.pages[page][i + 2];
					val = zipbufferb[pos + i];
					old_val = dev->ms_pages_saved.pages[page][i + 2];
					if (val != old_val) {
						if (ch)
							dev->ms_pages_saved.pages[page][i + 2] = val;
						else
							error |= 1;
					}
				}
			}

			pos += page_len;

			if (dev->drv->bus_type == ZIP_BUS_SCSI)
				val = zip_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
			else
				val = zip_mode_sense_pages_default.pages[page][0] & 0x80;
			if (dev->do_page_save && val)
				zip_mode_sense_save(dev);

			if (pos >= dev->total_length)
				break;
		}

		if (error) {
			zip_invalid_field_pl(dev);
			return 0;
		}
		break;
    }

    return 1;
}


/* This is the general ATAPI PIO request function. */
static void
zip_pio_request(zip_t *dev, uint8_t out)
{
    int ret = 0;

    if (dev->drv->bus_type < ZIP_BUS_SCSI) {
	zip_log("ZIP %i: Lowering IDE IRQ\n", dev->id);
	ide_irq_lower(ide_drives[dev->drv->ide_channel]);
    }

    dev->status = BUSY_STAT;

    if (dev->pos >= dev->packet_len) {
	zip_log("ZIP %i: %i bytes %s, command done\n", dev->id, dev->pos, out ? "written" : "read");

	dev->pos = dev->request_pos = 0;
	if (out) {
		ret = zip_phase_data_out(dev);
		/* If ret = 0 (phase 1 error), then we do not do anything else other than
		   free the buffer, as the phase and callback have already been set by the
		   error function. */
		if (ret)
			zip_command_complete(dev);
	} else
		zip_command_complete(dev);
	zip_buf_free(dev);
    } else {
	zip_log("ZIP %i: %i bytes %s, %i bytes are still left\n", dev->id, dev->pos,
		out ? "written" : "read", dev->packet_len - dev->pos);

	/* If less than (packet length) bytes are remaining, update packet length
	   accordingly. */
	if ((dev->packet_len - dev->pos) < (dev->max_transfer_len))
		dev->max_transfer_len = dev->packet_len - dev->pos;
	zip_log("ZIP %i: Packet length %i, request length %i\n", dev->id, dev->packet_len,
		dev->max_transfer_len);

	dev->packet_status = out ? PHASE_DATA_OUT : PHASE_DATA_IN;

	dev->status = BUSY_STAT;
	dev->phase = 1;
	zip_callback((void *) dev);
	dev->callback = 0LL;
	zip_set_callback(dev);

	dev->request_pos = 0;
    }
}


static int
zip_read_from_ide_dma(zip_t *dev)
{
    int ret;

    if (!dev)
	return 0;

    if (ide_bus_master_write) {
	ret = ide_bus_master_write(dev->drv->ide_channel >> 1,
				   zipbufferb, dev->packet_len,
				   ide_bus_master_priv[dev->drv->ide_channel >> 1]);
	if (ret == 2)		/* DMA not enabled, wait for it to be enabled. */
		return 2;
	else if (ret == 1) {	/* DMA error. */		
		zip_bus_master_error(dev);
		return 0;
	} else
		return 1;
    } else
	return 0;
}


static int
zip_read_from_scsi_dma(uint8_t scsi_id)
{
    zip_t *dev = (zip_t *) scsi_devices[scsi_id].p;
    int32_t *BufLen = &scsi_devices[scsi_id].buffer_length;

    if (!dev)
	return 0;

    zip_log("Reading from SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
    memcpy(zipbufferb, scsi_devices[scsi_id].cmd_buffer, *BufLen);
    return 1;
}


static void
zip_irq_raise(zip_t *dev)
{
    if (dev->drv->bus_type < ZIP_BUS_SCSI)
	ide_irq_raise(ide_drives[dev->drv->ide_channel]);
}


static int
zip_read_from_dma(zip_t *dev)
{
#ifdef ENABLE_ZIP_LOG
    int32_t *BufLen = &scsi_devices[dev->drv->scsi_device_id].buffer_length;
#endif
    int ret = 0;

    if (dev->drv->bus_type == ZIP_BUS_SCSI)
	ret = zip_read_from_scsi_dma(dev->drv->scsi_device_id);
    else
	ret = zip_read_from_ide_dma(dev);

    if (ret != 1)
	return ret;

    if (dev->drv->bus_type == ZIP_BUS_SCSI)
	zip_log("ZIP %i: SCSI Input data length: %i\n", dev->id, *BufLen);
    else
	zip_log("ZIP %i: ATAPI Input data length: %i\n", dev->id, dev->packet_len);

    ret = zip_phase_data_out(dev);

    if (ret)
	return 1;
    else
	return 0;
}


static int
zip_write_to_ide_dma(zip_t *dev)
{
    int ret;

    if (!dev)
	return 0;

    if (ide_bus_master_read) {
	ret = ide_bus_master_read(dev->drv->ide_channel >> 1,
				  zipbufferb, dev->packet_len,
				  ide_bus_master_priv[dev->drv->ide_channel >> 1]);
	if (ret == 2)		/* DMA not enabled, wait for it to be enabled. */
		return 2;
	else if (ret == 1) {	/* DMA error. */		
		zip_bus_master_error(dev);
		return 0;
	} else
		return 1;
    } else
	return 0;
}


static int
zip_write_to_scsi_dma(uint8_t scsi_id)
{
    zip_t *dev = (zip_t *) scsi_devices[scsi_id].p;
    int32_t *BufLen = &scsi_devices[scsi_id].buffer_length;

    if (!dev)
	return 0;

    zip_log("Writing to SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
    memcpy(scsi_devices[scsi_id].cmd_buffer, zipbufferb, *BufLen);
    zip_log("ZIP %i: Data from CD buffer:  %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
	    zipbufferb[0], zipbufferb[1], zipbufferb[2], zipbufferb[3], zipbufferb[4], zipbufferb[5],
	    zipbufferb[6], zipbufferb[7]);
    return 1;
}


static int
zip_write_to_dma(zip_t *dev)
{
#ifdef ENABLE_ZIP_LOG
    int32_t *BufLen = &scsi_devices[dev->drv->scsi_device_id].buffer_length;
#endif
    int ret = 0;

    if (dev->drv->bus_type == ZIP_BUS_SCSI) {
	zip_log("Write to SCSI DMA: (ID %02X)\n", dev->drv->scsi_device_id);
	ret = zip_write_to_scsi_dma(dev->drv->scsi_device_id);
    } else
	ret = zip_write_to_ide_dma(dev);

    if (dev->drv->bus_type == ZIP_BUS_SCSI)
	zip_log("ZIP %i: SCSI Output data length: %i\n", dev->id, *BufLen);
    else
	zip_log("ZIP %i: ATAPI Output data length: %i\n", dev->id, dev->packet_len);

    return ret;
}


static void
zip_callback(void *p)
{
    zip_t *dev = (zip_t *) p;
    int ret;

    switch(dev->packet_status) {
	case PHASE_IDLE:
		zip_log("ZIP %i: PHASE_IDLE\n", dev->id);
		dev->pos = 0;
		dev->phase = 1;
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		return;
	case PHASE_COMMAND:
		zip_log("ZIP %i: PHASE_COMMAND\n", dev->id);
		dev->status = BUSY_STAT | (dev->status & ERR_STAT);
		memcpy(dev->atapi_cdb, zipbufferb, 12);
		zip_command(dev, dev->atapi_cdb);
		return;
	case PHASE_COMPLETE:
		zip_log("ZIP %i: PHASE_COMPLETE\n", dev->id);
		dev->status = READY_STAT;
		dev->phase = 3;
		dev->packet_status = 0xFF;
		ui_sb_update_icon(SB_ZIP | dev->id, 0);
		zip_irq_raise(dev);
		return;
	case PHASE_DATA_OUT:
		zip_log("ZIP %i: PHASE_DATA_OUT\n", dev->id);
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		dev->phase = 0;
		zip_irq_raise(dev);
		return;
	case PHASE_DATA_OUT_DMA:
		zip_log("ZIP %i: PHASE_DATA_OUT_DMA\n", dev->id);
		ret = zip_read_from_dma(dev);

		if ((ret == 1) || (dev->drv->bus_type == ZIP_BUS_SCSI)) {
			zip_log("ZIP %i: DMA data out phase done\n");
			zip_buf_free(dev);
			zip_command_complete(dev);
		} else if (ret == 2) {
			zip_log("ZIP %i: DMA out not enabled, wait\n");
			zip_command_bus(dev);
		} else {
			zip_log("ZIP %i: DMA data out phase failure\n");
			zip_buf_free(dev);
		}
		return;
	case PHASE_DATA_IN:
		zip_log("ZIP %i: PHASE_DATA_IN\n", dev->id);
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		dev->phase = 2;
		zip_irq_raise(dev);
		return;
	case PHASE_DATA_IN_DMA:
		zip_log("ZIP %i: PHASE_DATA_IN_DMA\n", dev->id);
		ret = zip_write_to_dma(dev);

		if ((ret == 1) || (dev->drv->bus_type == ZIP_BUS_SCSI)) {
			zip_log("ZIP %i: DMA data in phase done\n", dev->id);
			zip_buf_free(dev);
			zip_command_complete(dev);
		} else if (ret == 2) {
			zip_log("ZIP %i: DMA in not enabled, wait\n", dev->id);
			zip_command_bus(dev);
		} else {
			zip_log("ZIP %i: DMA data in phase failure\n", dev->id);
			zip_buf_free(dev);
		}
		return;
	case PHASE_ERROR:
		zip_log("ZIP %i: PHASE_ERROR\n", dev->id);
		dev->status = READY_STAT | ERR_STAT;
		dev->phase = 3;
		dev->packet_status = 0xFF;
		zip_irq_raise(dev);
		ui_sb_update_icon(SB_ZIP | dev->id, 0);
		return;
    }
}


static uint32_t
zip_packet_read(void *p, int length)
{
    zip_t *dev = (zip_t *) p;

    uint16_t *zipbufferw;
    uint32_t *zipbufferl;

    uint32_t temp = 0;

    if (!dev)
	return 0;

    zipbufferw = (uint16_t *) zipbufferb;
    zipbufferl = (uint32_t *) zipbufferb;

    if (!zipbufferb)
	return 0;

    /* Make sure we return a 0 and don't attempt to read from the buffer if we're transferring bytes beyond it,
       which can happen when issuing media access commands with an allocated length below minimum request length
       (which is 1 sector = 512 bytes). */
    switch(length) {
	case 1:
		temp = (dev->pos < dev->packet_len) ? zipbufferb[dev->pos] : 0;
		dev->pos++;
		dev->request_pos++;
		break;
	case 2:
		temp = (dev->pos < dev->packet_len) ? zipbufferw[dev->pos >> 1] : 0;
		dev->pos += 2;
		dev->request_pos += 2;
		break;
	case 4:
		temp = (dev->pos < dev->packet_len) ? zipbufferl[dev->pos >> 2] : 0;
		dev->pos += 4;
		dev->request_pos += 4;
		break;
	default:
		return 0;
    }

    if (dev->packet_status == PHASE_DATA_IN) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		zip_pio_request(dev, 0);
	}
	return temp;
    } else
	return 0;
}


static void
zip_packet_write(void *p, uint32_t val, int length)
{
    zip_t *dev = (zip_t *) p;

    uint16_t *zipbufferw;
    uint32_t *zipbufferl;

    if (!dev)
	return;

    if (dev->packet_status == PHASE_IDLE) {
	if (!zipbufferb)
		zip_buf_alloc(dev, 12);
    }

    zipbufferw = (uint16_t *) zipbufferb;
    zipbufferl = (uint32_t *) zipbufferb;

    if (!zipbufferb)
	return;

    switch(length) {
	case 1:
		zipbufferb[dev->pos] = val & 0xff;
		dev->pos++;
		dev->request_pos++;
		break;
	case 2:
		zipbufferw[dev->pos >> 1] = val & 0xffff;
		dev->pos += 2;
		dev->request_pos += 2;
		break;
	case 4:
		zipbufferl[dev->pos >> 2] = val;
		dev->pos += 4;
		dev->request_pos += 4;
		break;
	default:
		return;
    }

    if (dev->packet_status == PHASE_DATA_OUT) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		zip_pio_request(dev, 1);
	}
	return;
    } else if (dev->packet_status == PHASE_IDLE) {
	if (dev->pos >= 12) {
		dev->pos = 0;
		dev->status = BUSY_STAT;
		dev->packet_status = PHASE_COMMAND;
		timer_process();
		zip_callback((void *) dev);
		timer_update_outstanding();
	}
	return;
    }
}


/* Peform a master init on the entire module. */
void
zip_global_init(void)
{
    /* Clear the global data. */
    memset(zip, 0x00, sizeof(zip));
    memset(zip_drives, 0x00, sizeof(zip_drives));
}


static void
zip_100_identify(ide_t *ide)
{
    ide_padstr((char *) (ide->buffer + 23), "E.08", 8); /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "IOMEGA ZIP 100 ATAPI", 40); /* Model */
}


static void
zip_250_identify(ide_t *ide, int ide_has_dma)
{
    ide_padstr((char *) (ide->buffer + 23), "42.S", 8); /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "IOMEGA  ZIP 250       ATAPI", 40); /* Model */

    if (ide_has_dma) {
	ide->buffer[80] = 0x30; /*Supported ATA versions : ATA/ATAPI-4 ATA/ATAPI-5*/
	ide->buffer[81] = 0x15; /*Maximum ATA revision supported : ATA/ATAPI-5 T13 1321D revision 1*/
    }
}


static int
zip_get_max(int ide_has_dma, int type)
{
    int ret;

    switch(type) {
	case TYPE_PIO:
		ret = ide_has_dma ? 3 : 0;
		break;
	case TYPE_SDMA:
	default:
		ret = -1;
		break;
	case TYPE_MDMA:
		ret = ide_has_dma ? -1 : 1;
		break;
	case TYPE_UDMA:
		ret = ide_has_dma ? -1 : 2;
		break;
    }

    return ret;
}


static int
zip_get_timings(int ide_has_dma, int type)
{
    int ret;

    switch(type) {
	case TIMINGS_DMA:
		ret = ide_has_dma ? 0x96 : 0;
		break;
	case TIMINGS_PIO:
		ret = ide_has_dma ? 0xb4 : 0;
		break;
	case TIMINGS_PIO_FC:
		ret = ide_has_dma ? 0xb4 : 0;
		break;
	default:
		ret = 0;
		break;
    }

    return ret;
}


static void
zip_identify(void *p, int ide_has_dma)
{
    ide_t *ide = (ide_t *) p;
    zip_t *zip;

    zip = (zip_t *) ide->p;

    /* Using (2<<5) below makes the ASUS P/I-P54TP4XE misdentify the ZIP drive
       as a LS-120. */
    ide->buffer[0] = 0x8000 | (0<<8) | 0x80 | (1<<5); /* ATAPI device, direct-access device, removable media, interrupt DRQ */
    ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
    ide->buffer[49] = 0x200; /* LBA supported */
    ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */

    if (zip_drives[zip->id].is_250)
	zip_250_identify(ide, ide_has_dma);
    else
	zip_100_identify(ide);
}


static void
zip_drive_reset(int c)
{
    scsi_device_t *sd;
    ide_t *id;

    if (!zip[c]) {
	zip[c] = (zip_t *) malloc(sizeof(zip_t));
	memset(zip[c], 0, sizeof(zip_t));
    }

    zip[c]->id = c;

    if (zip_drives[c].bus_type == ZIP_BUS_SCSI) {
	/* SCSI ZIP, attach to the SCSI bus. */
	sd = &scsi_devices[zip_drives[c].scsi_device_id];

	sd->p = zip[c];
	sd->command = zip_command;
	sd->callback = zip_callback;
	sd->err_stat_to_scsi = zip_err_stat_to_scsi;
	sd->request_sense = zip_request_sense_for_scsi;
	sd->reset = zip_reset;
	sd->read_capacity = zip_read_capacity;
	sd->type = SCSI_REMOVABLE_DISK;
    } else if (zip_drives[c].bus_type == ZIP_BUS_ATAPI) {
	/* ATAPI CD-ROM, attach to the IDE bus. */
	id = ide_drives[zip_drives[c].ide_channel];
	/* If the IDE channel is initialized, we attach to it,
	   otherwise, we do nothing - it's going to be a drive
	   that's not attached to anything. */
	if (id) {
		id->p = zip[c];
		id->get_max = zip_get_max;
		id->get_timings = zip_get_timings;
		id->identify = zip_identify;
		id->set_signature = zip_set_signature;
		id->packet_write = zip_packet_write;
		id->packet_read = zip_packet_read;
		id->stop = NULL;
		id->packet_callback = zip_callback;
		id->device_reset = zip_reset;
		id->interrupt_drq = 1;

		ide_atapi_attach(id);
	}
    }
}


void
zip_hard_reset(void)
{
    int c;

    for (c = 0; c < ZIP_NUM; c++) {
	if ((zip_drives[c].bus_type == ZIP_BUS_ATAPI) || (zip_drives[c].bus_type == ZIP_BUS_SCSI)) {
		zip_log("ZIP hard_reset drive=%d\n", c);

		/* Make sure to ignore any SCSI ZIP drive that has an out of range ID. */
		if ((zip_drives[c].bus_type == ZIP_BUS_SCSI) && (zip_drives[c].scsi_device_id > SCSI_ID_MAX))
			continue;

		/* Make sure to ignore any ATAPI ZIP drive that has an out of range IDE channel. */
		if ((zip_drives[c].bus_type == ZIP_BUS_ATAPI) && (zip_drives[c].ide_channel > 7))
			continue;

		zip_drive_reset(c);

		zip[c]->id = c;
		zip[c]->drv = &zip_drives[c];

		zip_init(zip[c]);

		if (wcslen(zip_drives[c].image_path))
			zip_load(zip[c], zip_drives[c].image_path);

		zip_mode_sense_load(zip[c]);

		if (zip_drives[c].bus_type == ZIP_BUS_SCSI)
			zip_log("SCSI ZIP drive %i attached to SCSI ID %i\n", c, zip_drives[c].scsi_device_id);
		else if (zip_drives[c].bus_type == ZIP_BUS_ATAPI)
			zip_log("ATAPI ZIP drive %i attached to IDE channel %i\n", c, zip_drives[c].ide_channel);
	}
    }
}


void
zip_close(void)
{
    zip_t *dev;
    int c;

    for (c = 0; c < ZIP_NUM; c++) {
	dev = zip[c];

	if (dev) {
		zip_disk_close(dev);

		free(zip[c]);
		zip[c] = NULL;
	}
    }
}
