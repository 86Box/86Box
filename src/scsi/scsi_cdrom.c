/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the CD-ROM drive with SCSI(-like)
 *		commands, for both ATAPI and SCSI usage.
 *
 * Version:	@(#)scsi_cdrom.c	1.0.55	2018/10/25
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
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
#include "../timer.h"
#include "../device.h"
#include "../piix.h"
#include "../scsi/scsi_device.h"
#include "../nvr.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../sound/sound.h"
#include "../plat.h"
#include "../ui.h"
#include "../cdrom/cdrom.h"
#include "scsi_cdrom.h"


/* Bits of 'status' */
#define ERR_STAT		0x01
#define DRQ_STAT		0x08 /* Data request */
#define DSC_STAT		0x10
#define SERVICE_STAT		0x10
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits of 'error' */
#define ABRT_ERR		0x04 /* Command aborted */
#define MCR_ERR			0x08 /* Media change request */

#define cdbufferb	 dev->buffer


scsi_cdrom_t	*scsi_cdrom[CDROM_NUM] = { NULL, NULL, NULL, NULL };


#pragma pack(push,1)
typedef struct
{
	uint8_t opcode;
	uint8_t polled;
	uint8_t reserved2[2];
	uint8_t class;
	uint8_t reserved3[2];
	uint16_t len;
	uint8_t control;
} gesn_cdb_t;

typedef struct
{
	uint16_t len;
	uint8_t notification_class;
	uint8_t supported_events;
} gesn_event_header_t;
#pragma pack(pop)


/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
const uint8_t scsi_cdrom_command_flags[0x100] =
{
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x00 */
    IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,		/* 0x01 */
    0,								/* 0x02 */
    IMPLEMENTED | ALLOW_UA,					/* 0x03 */
    0, 0, 0, 0,							/* 0x04-0x07 */
    IMPLEMENTED | CHECK_READY,					/* 0x08 */
    0, 0,							/* 0x09-0x0A */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x0B */
    0, 0, 0, 0, 0, 0,						/* 0x0C-0x11 */
    IMPLEMENTED | ALLOW_UA,					/* 0x12 */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x13 */
    0,								/* 0x14 */
    IMPLEMENTED,						/* 0x15 */
    0, 0, 0, 0,							/* 0x16-0x19 */
    IMPLEMENTED,						/* 0x1A */
    IMPLEMENTED | CHECK_READY,					/* 0x1B */
    0, 0,							/* 0x1C-0x1D */
    IMPLEMENTED | CHECK_READY,					/* 0x1E */
    0, 0, 0, 0, 0, 0,						/* 0x1F-0x24 */
    IMPLEMENTED | CHECK_READY,					/* 0x25 */
    0, 0,							/* 0x26-0x27 */
    IMPLEMENTED | CHECK_READY,					/* 0x28 */
    0, 0,							/* 0x29-0x2A */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x2B */
    0, 0, 0,							/* 0x2C-0x2E */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x2F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 0x30-0x3F */
    0, 0,							/* 0x40-0x41 */
    IMPLEMENTED | CHECK_READY,					/* 0x42 */
    IMPLEMENTED | CHECK_READY,	/* 0x43 - Read TOC - can get through UNIT_ATTENTION, per VIDE-CDD.SYS
				   NOTE: The ATAPI reference says otherwise, but I think this is a question of
				   interpreting things right - the UNIT ATTENTION condition we have here
				   is a tradition from not ready to ready, by definition the drive
				   eventually becomes ready, make the condition go away. */
    IMPLEMENTED | CHECK_READY,					/* 0x44 */
    IMPLEMENTED | CHECK_READY,					/* 0x45 */
    IMPLEMENTED | ALLOW_UA,					/* 0x46 */
    IMPLEMENTED | CHECK_READY,					/* 0x47 */
    IMPLEMENTED | CHECK_READY,					/* 0x48 */
    0,								/* 0x49 */
    IMPLEMENTED | ALLOW_UA,					/* 0x4A */
    IMPLEMENTED | CHECK_READY,					/* 0x4B */
    0, 0,							/* 0x4C-0x4D */
    IMPLEMENTED | CHECK_READY,					/* 0x4E */
    0, 0,							/* 0x4F-0x50 */
    IMPLEMENTED | CHECK_READY,					/* 0x51 */
    IMPLEMENTED | CHECK_READY,					/* 0x52 */
    0, 0,							/* 0x53-0x54 */
    IMPLEMENTED,						/* 0x55 */
    0, 0, 0, 0,							/* 0x56-0x59 */
    IMPLEMENTED,						/* 0x5A */
    0, 0, 0, 0, 0,						/* 0x5B-0x5F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 0x60-0x6F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 0x70-0x7F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 0x80-0x8F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 0x90-0x9F */
    0, 0, 0, 0, 0,						/* 0xA0-0xA4 */
    IMPLEMENTED | CHECK_READY,					/* 0xA5 */
    0, 0,							/* 0xA6-0xA7 */
    IMPLEMENTED | CHECK_READY,					/* 0xA8 */
    0, 0, 0, 0,							/* 0xA9-0xAC */
    IMPLEMENTED | CHECK_READY,					/* 0xAD */
    0,								/* 0xAE */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0xAF */
    0, 0, 0, 0,							/* 0xB0-0xB3 */
    IMPLEMENTED | CHECK_READY | ATAPI_ONLY,			/* 0xB4 */
    0, 0, 0,							/* 0xB5-0xB7 */
    IMPLEMENTED | CHECK_READY | ATAPI_ONLY,			/* 0xB8 */
    IMPLEMENTED | CHECK_READY,					/* 0xB9 */
    IMPLEMENTED | CHECK_READY,					/* 0xBA */
    IMPLEMENTED,						/* 0xBB */
    IMPLEMENTED | CHECK_READY,					/* 0xBC */
    IMPLEMENTED,						/* 0xBD */
    IMPLEMENTED | CHECK_READY,					/* 0xBE */
    IMPLEMENTED | CHECK_READY,					/* 0xBF */
    0, 0,							/* 0xC0-0xC1 */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,			/* 0xC2 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,				/* 0xC3-0xCC */
    IMPLEMENTED | CHECK_READY | SCSI_ONLY,			/* 0xCD */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,				/* 0xCE-0xD9 */
    IMPLEMENTED | SCSI_ONLY,					/* 0xDA */
    0, 0, 0, 0, 0,						/* 0xDB-0xDF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,		/* 0xE0-0xEF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0		/* 0xF0-0xFF */
};

static uint64_t scsi_cdrom_mode_sense_page_flags = (GPMODEP_R_W_ERROR_PAGE |
						    GPMODEP_CDROM_PAGE |
						    GPMODEP_CDROM_AUDIO_PAGE |
						    GPMODEP_CAPABILITIES_PAGE |
						    GPMODEP_ALL_PAGES);

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_default =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0,  0 },
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
    {        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
    {                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1,  255, 2, 255, 0, 0, 0,    0 },
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
    { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0,  0, 2, 0xC2, 1,   0, 0, 0, 2, 0xC2, 0, 0, 0, 0 }
}   };

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_default_scsi =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0,  0 },
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
    {        GPMODE_CDROM_PAGE,    6, 0, 1, 0, 60, 0, 75 },
    {                     0x8E,  0xE, 5, 4, 0,128, 0, 75, 1,  255, 2, 255, 0, 0, 0,    0 },
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
    { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 1,  0, 0,  0, 2, 0xC2, 1,   0, 0, 0, 2, 0xC2, 0, 0, 0, 0 }
}   };

static const mode_sense_pages_t scsi_cdrom_mode_sense_pages_changeable =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE,    6, 0xFF, 0xFF, 0, 0, 0, 0 },
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
    {        GPMODE_CDROM_PAGE,    6, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    {                     0x8E,  0xE, 0xFF, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
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
    { GPMODE_CAPABILITIES_PAGE, 0x12, 0, 0, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
}   };

uint8_t scsi_cdrom_read_capacity_cdb[12] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static gesn_cdb_t *gesn_cdb;
static gesn_event_header_t *gesn_event_header;


static void	scsi_cdrom_command_complete(scsi_cdrom_t *dev);

static void	scsi_cdrom_mode_sense_load(scsi_cdrom_t *dev);

static void	scsi_cdrom_init(scsi_cdrom_t *dev);

static void	scsi_cdrom_callback(void *p);


#ifdef ENABLE_SCSI_CDROM_LOG
int scsi_cdrom_do_log = ENABLE_SCSI_CDROM_LOG;


static void
scsi_cdrom_log(const char *format, ...)
{
    va_list ap;

    if (scsi_cdrom_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
}
#else
#define scsi_cdrom_log(format, ...)
#endif


static void
scsi_cdrom_set_callback(scsi_cdrom_t *dev)
{
    if (dev && dev->drv && (dev->drv->bus_type != CDROM_BUS_SCSI))
	ide_set_callback(dev->drv->ide_channel >> 1, dev->callback);
}


static void
scsi_cdrom_set_signature(void *p)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    if (!dev)
	return;
    dev->phase = 1;
    dev->request_length = 0xEB14;
}


static void
scsi_cdrom_init(scsi_cdrom_t *dev)
{
    if (!dev)
	return;

    /* Tell the scsi_cdrom_t struct what cdrom element corresponds to it. */
    dev->drv = &(cdrom[dev->id]);

    /* Do a reset (which will also rezero it). */
    scsi_cdrom_reset(dev);

    /* Configure the drive. */
    dev->requested_blocks = 1;

    dev->drv->bus_mode = 0;
    if (dev->drv->bus_type >= CDROM_BUS_ATAPI)
	dev->drv->bus_mode |= 2;
    if (dev->drv->bus_type < CDROM_BUS_SCSI)
	dev->drv->bus_mode |= 1;
    scsi_cdrom_log("CD-ROM %i: Bus type %i, bus mode %i\n", dev->id, dev->drv->bus_type, dev->drv->bus_mode);

    dev->sense[0] = 0xf0;
    dev->sense[7] = 10;
    dev->status = READY_STAT | DSC_STAT;
    dev->pos = 0;
    dev->packet_status = 0xff;
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = dev->unit_attention = 0;
    dev->drv->cur_speed = dev->drv->speed;
    scsi_cdrom_mode_sense_load(dev);
}


/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
scsi_cdrom_current_mode(scsi_cdrom_t *dev)
{
    if (dev->drv->bus_type == CDROM_BUS_SCSI)
	return 2;
    else if (dev->drv->bus_type == CDROM_BUS_ATAPI) {
	scsi_cdrom_log("CD-ROM %i: ATAPI drive, setting to %s\n", dev->id,
		  (dev->features & 1) ? "DMA" : "PIO",
		  dev->id);
	return (dev->features & 1) ? 2 : 1;
    }

    return 0;
}


/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int
scsi_cdrom_err_stat_to_scsi(void *p)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    if (dev->status & ERR_STAT)
	return SCSI_STATUS_CHECK_CONDITION;
    else
	return SCSI_STATUS_OK;
}


/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int
scsi_cdrom_atapi_phase_to_scsi(scsi_cdrom_t *dev)
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


static uint32_t
scsi_cdrom_get_channel(void *p, int channel)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;
    if (!dev)
	return channel + 1;

    return dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 10 : 8];
}


static uint32_t
scsi_cdrom_get_volume(void *p, int channel)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;
    if (!dev)
	return 255;

    return dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 11 : 9];
}


static void
scsi_cdrom_mode_sense_load(scsi_cdrom_t *dev)
{
    FILE *f;
    wchar_t file_name[512];
    int i;

    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
    for (i = 0; i < 0x3f; i++) {
	if (scsi_cdrom_mode_sense_pages_default.pages[i][1] != 0) {
		if (dev->drv->bus_type == CDROM_BUS_SCSI)
			memcpy(dev->ms_pages_saved.pages[i],
			       scsi_cdrom_mode_sense_pages_default_scsi.pages[i],
			       scsi_cdrom_mode_sense_pages_default_scsi.pages[i][1] + 2);
		else
			memcpy(dev->ms_pages_saved.pages[i],
			       scsi_cdrom_mode_sense_pages_default.pages[i],
			       scsi_cdrom_mode_sense_pages_default.pages[i][1] + 2);
	}
    }
    memset(file_name, 0, 512 * sizeof(wchar_t));
    if (dev->drv->bus_type == CDROM_BUS_SCSI)
	swprintf(file_name, 512, L"scsi_cdrom_%02i_mode_sense_bin", dev->id);
    else
	swprintf(file_name, 512, L"cdrom_%02i_mode_sense_bin", dev->id);
    f = plat_fopen(nvr_path(file_name), L"rb");
    if (f) {
	fread(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, f);
	fclose(f);
    }
}


static void
scsi_cdrom_mode_sense_save(scsi_cdrom_t *dev)
{
    FILE *f;
    wchar_t file_name[512];

    memset(file_name, 0, 512 * sizeof(wchar_t));
    if (dev->drv->bus_type == CDROM_BUS_SCSI)
	swprintf(file_name, 512, L"scsi_cdrom_%02i_mode_sense_bin", dev->id);
    else
	swprintf(file_name, 512, L"cdrom_%02i_mode_sense_bin", dev->id);
    f = plat_fopen(nvr_path(file_name), L"wb");
    if (f) {
	fwrite(dev->ms_pages_saved.pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, f);
	fclose(f);
    }
}


int
scsi_cdrom_read_capacity(void *p, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;
    int size = 0;

    if (dev->drv->ops && dev->drv->ops->size)
	size = dev->drv->ops->size(dev->drv) - 1;	/* IMPORTANT: What's returned is the last LBA block. */
    memset(buffer, 0, 8);
    buffer[0] = (size >> 24) & 0xff;
    buffer[1] = (size >> 16) & 0xff;
    buffer[2] = (size >> 8) & 0xff;
    buffer[3] = size & 0xff;
    buffer[6] = 8;					/* 2048 = 0x0800 */
    *len = 8;

    return 1;
}


/*SCSI Mode Sense 6/10*/
static uint8_t
scsi_cdrom_mode_sense_read(scsi_cdrom_t *dev, uint8_t page_control, uint8_t page, uint8_t pos)
{
    switch (page_control) {
	case 0:
	case 3:
		return dev->ms_pages_saved.pages[page][pos];
		break;
	case 1:
		return scsi_cdrom_mode_sense_pages_changeable.pages[page][pos];
		break;
	case 2:
		if (dev->drv->bus_type == CDROM_BUS_SCSI)
			return scsi_cdrom_mode_sense_pages_default_scsi.pages[page][pos];
		else
			return scsi_cdrom_mode_sense_pages_default.pages[page][pos];
		break;
    }

    return 0;
}


static uint32_t
scsi_cdrom_mode_sense(scsi_cdrom_t *dev, uint8_t *buf, uint32_t pos, uint8_t page, uint8_t block_descriptor_len)
{
    uint8_t page_control = (page >> 6) & 3;
    int i = 0, j = 0;

    uint8_t msplen;

    page &= 0x3f;

    if (block_descriptor_len) {
	buf[pos++] = 1;		/* Density code. */
	buf[pos++] = 0;		/* Number of blocks (0 = all). */
	buf[pos++] = 0;
	buf[pos++] = 0;
	buf[pos++] = 0;		/* Reserved. */
	buf[pos++] = 0;		/* Block length (0x800 = 2048 bytes). */
	buf[pos++] = 8;
	buf[pos++] = 0;
    }

    for (i = 0; i < 0x40; i++) {
        if ((page == GPMODE_ALL_PAGES) || (page == i)) {
		if (scsi_cdrom_mode_sense_page_flags & (1LL << ((uint64_t) (page & 0x3f)))) {
			buf[pos++] = scsi_cdrom_mode_sense_read(dev, page_control, i, 0);
			msplen = scsi_cdrom_mode_sense_read(dev, page_control, i, 1);
			buf[pos++] = msplen;
			scsi_cdrom_log("CD-ROM %i: MODE SENSE: Page [%02X] length %i\n", dev->id, i, msplen);
			for (j = 0; j < msplen; j++) {
				if ((i == GPMODE_CAPABILITIES_PAGE) && (j >= 6) && (j <= 7)) {
					if (j & 1)
						buf[pos++] = ((dev->drv->speed * 176) & 0xff);
					else
						buf[pos++] = ((dev->drv->speed * 176) >> 8);
				} else if ((i == GPMODE_CAPABILITIES_PAGE) && (j >= 12) && (j <= 13)) {
					if (j & 1)
						buf[pos++] = ((dev->drv->cur_speed * 176) & 0xff);
					else
						buf[pos++] = ((dev->drv->cur_speed * 176) >> 8);
				} else
					buf[pos++] = scsi_cdrom_mode_sense_read(dev, page_control, i, 2 + j);
			}
		}
	}
    }

    return pos;
}


static void
scsi_cdrom_update_request_length(scsi_cdrom_t *dev, int len, int block_len)
{
    int32_t bt, min_len = 0;

    dev->max_transfer_len = dev->request_length;

    /* For media access commands, make sure the requested DRQ length matches the block length. */
    switch (dev->current_cdb[0]) {
	case 0x08:
	case 0x28:
	case 0xa8:
	case 0xb9:
	case 0xbe:
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


static double
scsi_cdrom_bus_speed(scsi_cdrom_t *dev)
{
    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
	dev->callback = -1LL;	/* Speed depends on SCSI controller */
	return 0.0;
    } else {
	/* TODO: Get the actual selected speed from IDE. */
	if (scsi_cdrom_current_mode(dev) == 2)
		return 66666666.666666666666666;	/* 66 MB/s MDMA-2 speed */
	else
		return  8333333.333333333333333;	/* 8.3 MB/s PIO-2 speed */
    }
}


static void
scsi_cdrom_command_bus(scsi_cdrom_t *dev)
{
    dev->status = BUSY_STAT;
    dev->phase = 1;
    dev->pos = 0;
    dev->callback = 1LL * CDROM_TIME;
    scsi_cdrom_set_callback(dev);
}


static void
scsi_cdrom_command_common(scsi_cdrom_t *dev)
{
    double bytes_per_second, period;
    double dusec;

    dev->status = BUSY_STAT;
    dev->phase = 1;
    dev->pos = 0;
    dev->callback = 0LL;

    scsi_cdrom_log("CD-ROM %i: Current speed: %ix\n", dev->id, dev->drv->cur_speed);

    if (dev->packet_status == PHASE_COMPLETE) {
	scsi_cdrom_callback(dev);
	dev->callback = 0LL;
    } else {
	switch(dev->current_cdb[0]) {
		case GPCMD_REZERO_UNIT:
		case 0x0b:
		case 0x2b:
			/* Seek time is in us. */
			period = cdrom_seek_time(dev->drv);
			scsi_cdrom_log("CD-ROM %i: Seek period: %" PRIu64 " us\n",
				  dev->id, (int64_t) period);
			period = period * ((double) TIMER_USEC);
			dev->callback += ((int64_t) period);
			scsi_cdrom_set_callback(dev);
			return;
		case 0x08:
		case 0x28:
		case 0xa8:
			/* Seek time is in us. */
			period = cdrom_seek_time(dev->drv);
			scsi_cdrom_log("CD-ROM %i: Seek period: %" PRIu64 " us\n",
				  dev->id, (int64_t) period);
			period = period * ((double) TIMER_USEC);
			dev->callback += ((int64_t) period);
		case 0x25:
		case 0x42:
		case 0x43:
		case 0x44:
		case 0x51:
		case 0x52:
		case 0xad:
		case 0xb8:
		case 0xb9:
		case 0xbe:
			if (dev->current_cdb[0] == 0x42)
				dev->callback += 200LL * CDROM_TIME;
			/* Account for seek time. */
			bytes_per_second = 176.0 * 1024.0;
			bytes_per_second *= (double) dev->drv->cur_speed;
			break;
		default:
			bytes_per_second = scsi_cdrom_bus_speed(dev);
			if (bytes_per_second == 0.0) {
				dev->callback = -1LL;	/* Speed depends on SCSI controller */
				return;
			}
			break;
	}

	period = 1000000.0 / bytes_per_second;
	scsi_cdrom_log("CD-ROM %i: Byte transfer period: %" PRIu64 " us\n", dev->id, (int64_t) period);
	period = period * (double) (dev->packet_len);
	scsi_cdrom_log("CD-ROM %i: Sector transfer period: %" PRIu64 " us\n", dev->id, (int64_t) period);
	dusec = period * ((double) TIMER_USEC);
	dev->callback += ((int64_t) dusec);
    }
    scsi_cdrom_set_callback(dev);
}


static void
scsi_cdrom_command_complete(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_COMPLETE;
    scsi_cdrom_command_common(dev);
}


static void
scsi_cdrom_command_read(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_IN;
    scsi_cdrom_command_common(dev);
    dev->total_read = 0;
}


static void
scsi_cdrom_command_read_dma(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_IN_DMA;
    scsi_cdrom_command_common(dev);
    dev->total_read = 0;
}


static void
scsi_cdrom_command_write(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT;
    scsi_cdrom_command_common(dev);
}


static void scsi_cdrom_command_write_dma(scsi_cdrom_t *dev)
{
    dev->packet_status = PHASE_DATA_OUT_DMA;
    scsi_cdrom_command_common(dev);
}


/* id = Current CD-ROM device ID;
   len = Total transfer length;
   block_len = Length of a single block (it matters because media access commands on ATAPI);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void scsi_cdrom_data_command_finish(scsi_cdrom_t *dev, int len, int block_len, int alloc_len, int direction)
{
    scsi_cdrom_log("CD-ROM %i: Finishing command (%02X): %i, %i, %i, %i, %i\n",
	      dev->id, dev->current_cdb[0], len, block_len, alloc_len, direction, dev->request_length);
    dev->pos = 0;
    if (alloc_len >= 0) {
	if (alloc_len < len)
		len = alloc_len;
    }
    if ((len == 0) || (scsi_cdrom_current_mode(dev) == 0)) {
	if (dev->drv->bus_type != CDROM_BUS_SCSI)
		dev->packet_len = 0;

	scsi_cdrom_command_complete(dev);
    } else {
	if (scsi_cdrom_current_mode(dev) == 2) {
		if (dev->drv->bus_type != CDROM_BUS_SCSI)
			dev->packet_len = alloc_len;

		if (direction == 0)
			scsi_cdrom_command_read_dma(dev);
		else
			scsi_cdrom_command_write_dma(dev);
	} else {
		scsi_cdrom_update_request_length(dev, len, block_len);
		if (direction == 0)
			scsi_cdrom_command_read(dev);
		else
			scsi_cdrom_command_write(dev);
	}
    }

    scsi_cdrom_log("CD-ROM %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
	      dev->id, dev->packet_status, dev->request_length, dev->packet_len, dev->pos, dev->phase);
}


static void
scsi_cdrom_sense_clear(scsi_cdrom_t *dev, int command)
{
    dev->previous_command = command;
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = 0;
}


static void
scsi_cdrom_set_phase(scsi_cdrom_t *dev, uint8_t phase)
{
    uint8_t scsi_id = dev->drv->scsi_device_id;

    if (dev->drv->bus_type != CDROM_BUS_SCSI)
	return;

    scsi_devices[scsi_id].phase = phase;
}


static void
scsi_cdrom_cmd_error(scsi_cdrom_t *dev)
{
    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = ((scsi_cdrom_sense_key & 0xf) << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = 0x80;
    dev->callback = 50LL * CDROM_TIME;
    scsi_cdrom_set_callback(dev);
    scsi_cdrom_log("CD-ROM %i: ERROR: %02X/%02X/%02X\n", dev->id, scsi_cdrom_sense_key, scsi_cdrom_asc, scsi_cdrom_ascq);
}


static void
scsi_cdrom_unit_attention(scsi_cdrom_t *dev)
{
    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = 0x80;
    dev->callback = 50LL * CDROM_TIME;
    scsi_cdrom_set_callback(dev);
    scsi_cdrom_log("CD-ROM %i: UNIT ATTENTION\n", dev->id);
}


static void
scsi_cdrom_bus_master_error(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = scsi_cdrom_asc = scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}


static void
scsi_cdrom_not_ready(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_NOT_READY;
    scsi_cdrom_asc = ASC_MEDIUM_NOT_PRESENT;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}


static void
scsi_cdrom_invalid_lun(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_INV_LUN;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}


static void
scsi_cdrom_illegal_opcode(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_ILLEGAL_OPCODE;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}


static void
scsi_cdrom_lba_out_of_range(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_LBA_OUT_OF_RANGE;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}


static void
scsi_cdrom_invalid_field(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_INV_FIELD_IN_CMD_PACKET;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
    dev->status = 0x53;
}


static void
scsi_cdrom_invalid_field_pl(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
    dev->status = 0x53;
}


static void
scsi_cdrom_illegal_mode(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_ILLEGAL_MODE_FOR_THIS_TRACK;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}


static void
scsi_cdrom_incompatible_format(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_INCOMPATIBLE_FORMAT;
    scsi_cdrom_ascq = 2;
    scsi_cdrom_cmd_error(dev);
}


static void
scsi_cdrom_data_phase_error(scsi_cdrom_t *dev)
{
    scsi_cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_cdrom_asc = ASC_DATA_PHASE_ERROR;
    scsi_cdrom_ascq = 0;
    scsi_cdrom_cmd_error(dev);
}


void
scsi_cdrom_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks)
{
    int temp = 0;

    switch(cdb[0]) {
	case GPCMD_READ_6:
		cdb[1] = (lba_pos >> 16) & 0xff;
		cdb[2] = (lba_pos >> 8) & 0xff;
		cdb[3] = lba_pos & 0xff;
		break;

	case GPCMD_READ_10:
		cdb[2] = (lba_pos >> 24) & 0xff;
		cdb[3] = (lba_pos >> 16) & 0xff;
		cdb[4] = (lba_pos >> 8) & 0xff;
		cdb[5] = lba_pos & 0xff;
		cdb[7] = (number_of_blocks >> 8) & 0xff;
		cdb[8] = number_of_blocks & 0xff;
		break;

	case GPCMD_READ_12:
		cdb[2] = (lba_pos >> 24) & 0xff;
		cdb[3] = (lba_pos >> 16) & 0xff;
		cdb[4] = (lba_pos >> 8) & 0xff;
		cdb[5] = lba_pos & 0xff;
		cdb[6] = (number_of_blocks >> 24) & 0xff;
		cdb[7] = (number_of_blocks >> 16) & 0xff;
		cdb[8] = (number_of_blocks >> 8) & 0xff;
		cdb[9] = number_of_blocks & 0xff;
		break;

	case GPCMD_READ_CD_MSF:
		temp = cdrom_lba_to_msf_accurate(lba_pos);
		cdb[3] = (temp >> 16) & 0xff;
		cdb[4] = (temp >> 8) & 0xff;
		cdb[5] = temp & 0xff;

		temp = cdrom_lba_to_msf_accurate(lba_pos + number_of_blocks - 1);
		cdb[6] = (temp >> 16) & 0xff;
		cdb[7] = (temp >> 8) & 0xff;
		cdb[8] = temp & 0xff;
		break;			

	case GPCMD_READ_CD:
		cdb[2] = (lba_pos >> 24) & 0xff;
		cdb[3] = (lba_pos >> 16) & 0xff;
		cdb[4] = (lba_pos >> 8) & 0xff;
		cdb[5] = lba_pos & 0xff;
		cdb[6] = (number_of_blocks >> 16) & 0xff;
		cdb[7] = (number_of_blocks >> 8) & 0xff;
		cdb[8] = number_of_blocks & 0xff;
		break;
    }
}


static int
scsi_cdrom_read_data(scsi_cdrom_t *dev, int msf, int type, int flags, int32_t *len)
{
    int ret = 0;
    uint32_t cdsize = 0;

    int i = 0;
    int temp_len = 0;

    if (dev->drv->ops && dev->drv->ops->size)
	cdsize = dev->drv->ops->size(dev->drv);
    else {
	scsi_cdrom_not_ready(dev);
	return 0;
    }

    if (dev->sector_pos >= cdsize) {
	scsi_cdrom_log("CD-ROM %i: Trying to read from beyond the end of disc (%i >= %i)\n", dev->id,
		  dev->sector_pos, cdsize);
	scsi_cdrom_lba_out_of_range(dev);
	return 0;
    }

    if ((dev->sector_pos + dev->sector_len - 1) >= cdsize) {
	scsi_cdrom_log("CD-ROM %i: Trying to read to beyond the end of disc (%i >= %i)\n", dev->id,
		  (dev->sector_pos + dev->sector_len - 1), cdsize);
	scsi_cdrom_lba_out_of_range(dev);
	return 0;
    }

    dev->old_len = 0;
    *len = 0;

    for (i = 0; i < dev->requested_blocks; i++) {
	if (dev->drv->ops && dev->drv->ops->readsector_raw)
		ret = dev->drv->ops->readsector_raw(dev->drv, cdbufferb + dev->data_pos,
						    dev->sector_pos + i, msf, type, flags, &temp_len);
	else {
		scsi_cdrom_not_ready(dev);
		return 0;
	}

	dev->data_pos += temp_len;
	dev->old_len += temp_len;

	*len += temp_len;

	if (!ret) {
		scsi_cdrom_illegal_mode(dev);
		return 0;
	}
    }

    return 1;
}


static int
scsi_cdrom_read_blocks(scsi_cdrom_t *dev, int32_t *len, int first_batch)
{
    int ret = 0, msf = 0;
    int type = 0, flags = 0;

    if (dev->current_cdb[0] == GPCMD_READ_CD_MSF)
	msf = 1;

    if ((dev->current_cdb[0] == GPCMD_READ_CD_MSF) || (dev->current_cdb[0] == GPCMD_READ_CD)) {
	type = (dev->current_cdb[1] >> 2) & 7;
	flags = dev->current_cdb[9] | (((uint32_t) dev->current_cdb[10]) << 8);
    } else {
	type = 8;
	flags = 0x10;
    }

    dev->data_pos = 0;

    if (!dev->sector_len) {
	scsi_cdrom_command_complete(dev);
	return -1;
    }

    scsi_cdrom_log("Reading %i blocks starting from %i...\n", dev->requested_blocks, dev->sector_pos);

    scsi_cdrom_update_cdb(dev->current_cdb, dev->sector_pos, dev->requested_blocks);

    ret = scsi_cdrom_read_data(dev, msf, type, flags, len);

    scsi_cdrom_log("Read %i bytes of blocks...\n", *len);

    if (!ret || ((dev->old_len != *len) && !first_batch)) {
	if ((dev->old_len != *len) && !first_batch)
		scsi_cdrom_illegal_mode(dev);

	return 0;
    }

    dev->sector_pos += dev->requested_blocks;
    dev->sector_len -= dev->requested_blocks;

    return 1;
}


/*SCSI Read DVD Structure*/
static int
scsi_cdrom_read_dvd_structure(scsi_cdrom_t *dev, int format, const uint8_t *packet, uint8_t *buf)
{
    int layer = packet[6];
    uint64_t total_sectors = 0;

    switch (format) {
       	case 0x00:	/* Physical format information */
		if (dev->drv->ops && dev->drv->ops->size)
	    		total_sectors = (uint64_t) dev->drv->ops->size(dev->drv);
		else {
			scsi_cdrom_not_ready(dev);
			return 0;
		}

	        if (layer != 0) {
			scsi_cdrom_invalid_field(dev);
			return 0;
		}

               	total_sectors >>= 2;
		if (total_sectors == 0) {
			/* return -ASC_MEDIUM_NOT_PRESENT; */
			scsi_cdrom_not_ready(dev);
			return 0;
		}

		buf[4] = 1;	/* DVD-ROM, part version 1 */
		buf[5] = 0xf;	/* 120mm disc, minimum rate unspecified */
		buf[6] = 1;	/* one layer, read-only (per MMC-2 spec) */
		buf[7] = 0;	/* default densities */

		/* FIXME: 0x30000 per spec? */
		buf[8] = buf[9] = buf[10] = buf[11] = 0; /* start sector */
		buf[12] = (total_sectors >> 24) & 0xff; /* end sector */
		buf[13] = (total_sectors >> 16) & 0xff;
		buf[14] = (total_sectors >> 8) & 0xff;
		buf[15] = total_sectors & 0xff;

		buf[16] = (total_sectors >> 24) & 0xff; /* l0 end sector */
		buf[17] = (total_sectors >> 16) & 0xff;
		buf[18] = (total_sectors >> 8) & 0xff;
		buf[19] = total_sectors & 0xff;

		/* Size of buffer, not including 2 byte size field */				
		buf[0] = ((2048 +2 ) >> 8) & 0xff;
		buf[1] = (2048 + 2) & 0xff;

		/* 2k data + 4 byte header */
		return (2048 + 4);

	case 0x01:	/* DVD copyright information */
		buf[4] = 0; /* no copyright data */
		buf[5] = 0; /* no region restrictions */

		/* Size of buffer, not including 2 byte size field */
		buf[0] = ((4 + 2) >> 8) & 0xff;
		buf[1] = (4 + 2) & 0xff;			

		/* 4 byte header + 4 byte data */
		return (4 + 4);

	case 0x03:	/* BCA information - invalid field for no BCA info */
		scsi_cdrom_invalid_field(dev);
		return 0;

	case 0x04:	/* DVD disc manufacturing information */
			/* Size of buffer, not including 2 byte size field */
		buf[0] = ((2048 + 2) >> 8) & 0xff;
		buf[1] = (2048 + 2) & 0xff;

		/* 2k data + 4 byte header */
		return (2048 + 4);

	case 0xff:
		/*
		 * This lists all the command capabilities above.  Add new ones
		 * in order and update the length and buffer return values.
		 */

		buf[4] = 0x00; /* Physical format */
		buf[5] = 0x40; /* Not writable, is readable */
		buf[6] = ((2048 + 4) >> 8) & 0xff;
		buf[7] = (2048 + 4) & 0xff;

		buf[8] = 0x01; /* Copyright info */
		buf[9] = 0x40; /* Not writable, is readable */
		buf[10] = ((4 + 4) >> 8) & 0xff;
		buf[11] = (4 + 4) & 0xff;

		buf[12] = 0x03; /* BCA info */
		buf[13] = 0x40; /* Not writable, is readable */
		buf[14] = ((188 + 4) >> 8) & 0xff;
		buf[15] = (188 + 4) & 0xff;

		buf[16] = 0x04; /* Manufacturing info */
		buf[17] = 0x40; /* Not writable, is readable */
		buf[18] = ((2048 + 4) >> 8) & 0xff;
		buf[19] = (2048 + 4) & 0xff;

		/* Size of buffer, not including 2 byte size field */
		buf[6] = ((16 + 2) >> 8) & 0xff;
		buf[7] = (16 + 2) & 0xff;

		/* data written + 4 byte header */
		return (16 + 4);

	default: /* TODO: formats beyond DVD-ROM requires */
		scsi_cdrom_invalid_field(dev);
		return 0;
    }
}


static void
scsi_cdrom_insert(void *p)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    if (!dev)
	return;

    dev->unit_attention = 1;
    scsi_cdrom_log("CD-ROM %i: Media insert\n", dev->id);
}


static int
scsi_cdrom_pre_execution_check(scsi_cdrom_t *dev, uint8_t *cdb)
{
    int ready = 0, status = 0;

    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
	if ((cdb[0] != GPCMD_REQUEST_SENSE) && (cdb[1] & 0xe0)) {
		scsi_cdrom_log("CD-ROM %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n",
			  dev->id, ((dev->request_length >> 5) & 7));
		scsi_cdrom_invalid_lun(dev);
		return 0;
	}
    }

    if (!(scsi_cdrom_command_flags[cdb[0]] & IMPLEMENTED)) {
	scsi_cdrom_log("CD-ROM %i: Attempting to execute unknown command %02X over %s\n", dev->id, cdb[0],
		  (dev->drv->bus_type == CDROM_BUS_SCSI) ? "SCSI" : "ATAPI");

	scsi_cdrom_illegal_opcode(dev);
	return 0;
    }

    if ((dev->drv->bus_type < CDROM_BUS_SCSI) && (scsi_cdrom_command_flags[cdb[0]] & SCSI_ONLY)) {
	scsi_cdrom_log("CD-ROM %i: Attempting to execute SCSI-only command %02X over ATAPI\n", dev->id, cdb[0]);
	scsi_cdrom_illegal_opcode(dev);
	return 0;
    }

    if ((dev->drv->bus_type == CDROM_BUS_SCSI) && (scsi_cdrom_command_flags[cdb[0]] & ATAPI_ONLY)) {
	scsi_cdrom_log("CD-ROM %i: Attempting to execute ATAPI-only command %02X over SCSI\n", dev->id, cdb[0]);
	scsi_cdrom_illegal_opcode(dev);
	return 0;
    }

    if (dev->drv->ops && dev->drv->ops->status)
	status = dev->drv->ops->status(dev->drv);
    else
	status = CD_STATUS_EMPTY;

    if ((status == CD_STATUS_PLAYING) || (status == CD_STATUS_PAUSED)) {
	ready = 1;
	goto skip_ready_check;
    }

    if (dev->drv->ops && dev->drv->ops->medium_changed) {
	if (dev->drv->ops->medium_changed(dev->drv))
		scsi_cdrom_insert((void *) dev);
    }

    if (dev->drv->ops && dev->drv->ops->ready)
	ready = dev->drv->ops->ready(dev->drv);

skip_ready_check:
    /* If the drive is not ready, there is no reason to keep the
       UNIT ATTENTION condition present, as we only use it to mark
       disc changes. */
    if (!ready && dev->unit_attention)
	dev->unit_attention = 0;

    /* If the UNIT ATTENTION condition is set and the command does not allow
       execution under it, error out and report the condition. */
    if (dev->unit_attention == 1) {
	/* Only increment the unit attention phase if the command can not pass through it. */
	if (!(scsi_cdrom_command_flags[cdb[0]] & ALLOW_UA)) {
		/* scsi_cdrom_log("CD-ROM %i: Unit attention now 2\n", dev->id); */
		dev->unit_attention++;
		scsi_cdrom_log("CD-ROM %i: UNIT ATTENTION: Command %02X not allowed to pass through\n",
			  dev->id, cdb[0]);
		scsi_cdrom_unit_attention(dev);
		return 0;
	}
    } else if (dev->unit_attention == 2) {
	if (cdb[0] != GPCMD_REQUEST_SENSE) {
		/* scsi_cdrom_log("CD-ROM %i: Unit attention now 0\n", dev->id); */
		dev->unit_attention = 0;
	}
    }

    /* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
       the UNIT ATTENTION condition if it's set. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
	scsi_cdrom_sense_clear(dev, cdb[0]);

    /* Next it's time for NOT READY. */
    if (!ready)
	dev->media_status = MEC_MEDIA_REMOVAL;
    else
	dev->media_status = (dev->unit_attention) ? MEC_NEW_MEDIA : MEC_NO_CHANGE;

    if ((scsi_cdrom_command_flags[cdb[0]] & CHECK_READY) && !ready) {
	scsi_cdrom_log("CD-ROM %i: Not ready (%02X)\n", dev->id, cdb[0]);
	scsi_cdrom_not_ready(dev);
	return 0;
    }

    scsi_cdrom_log("CD-ROM %i: Continuing with command %02X\n", dev->id, cdb[0]);

    return 1;
}


static void
scsi_cdrom_rezero(scsi_cdrom_t *dev)
{
    dev->sector_pos = dev->sector_len = 0;
    cdrom_seek(dev->drv, 0);
}


void
scsi_cdrom_reset(void *p)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    if (!dev)
	return;

    scsi_cdrom_rezero(dev);
    dev->status = 0;
    dev->callback = 0LL;
    scsi_cdrom_set_callback(dev);
    scsi_cdrom_set_signature(dev);
    dev->packet_status = 0xff;
    dev->unit_attention = 0xff;
}


static void
scsi_cdrom_request_sense(scsi_cdrom_t *dev, uint8_t *buffer, uint8_t alloc_length)
{
    int status = dev->drv->cd_status;

    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
	memset(buffer, 0, alloc_length);
	memcpy(buffer, dev->sense, alloc_length);
    }

    buffer[0] = 0x70;

    if ((scsi_cdrom_sense_key > 0) && ((status < CD_STATUS_PLAYING) ||
	(status == CD_STATUS_STOPPED)) && cdrom_playing_completed(dev->drv)) {
	buffer[2]=SENSE_ILLEGAL_REQUEST;
	buffer[12]=ASC_AUDIO_PLAY_OPERATION;
	buffer[13]=ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
    } else if ((scsi_cdrom_sense_key == 0) && (status >= CD_STATUS_PLAYING) &&
	       (status != CD_STATUS_STOPPED)) {
	buffer[2]=SENSE_ILLEGAL_REQUEST;
	buffer[12]=ASC_AUDIO_PLAY_OPERATION;
	buffer[13]=(status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
    } else {
	if (dev->unit_attention && (scsi_cdrom_sense_key == 0)) {
		buffer[2]=SENSE_UNIT_ATTENTION;
		buffer[12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
		buffer[13]=0;
	}
    }

    scsi_cdrom_log("CD-ROM %i: Reporting sense: %02X %02X %02X\n", dev->id, buffer[2], buffer[12], buffer[13]);

    if (buffer[2] == SENSE_UNIT_ATTENTION) {
	/* If the last remaining sense is unit attention, clear
	   that condition. */
	dev->unit_attention = 0;
    }

    /* Clear the sense stuff as per the spec. */
    scsi_cdrom_sense_clear(dev, GPCMD_REQUEST_SENSE);
}


void
scsi_cdrom_request_sense_for_scsi(void *p, uint8_t *buffer, uint8_t alloc_length)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;
    int ready = 0;

    if (dev->drv->ops && dev->drv->ops->medium_changed) {
	if (dev->drv->ops->medium_changed(dev->drv))
		scsi_cdrom_insert((void *) dev);
    }

    if (dev->drv->ops && dev->drv->ops->ready)
	ready = dev->drv->ops->ready(dev->drv);

    if (!ready && dev->unit_attention) {
	/* If the drive is not ready, there is no reason to keep the
	   UNIT ATTENTION condition present, as we only use it to mark
	   disc changes. */
	dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */
    scsi_cdrom_request_sense(dev, buffer, alloc_length);
}


static void
scsi_cdrom_set_buf_len(scsi_cdrom_t *dev, int32_t *BufLen, int32_t *src_len)
{
    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
	if (*BufLen == -1)
		*BufLen = *src_len;
	else {
		*BufLen = MIN(*src_len, *BufLen);
		*src_len = *BufLen;
	}
	scsi_cdrom_log("CD-ROM %i: Actual transfer length: %i\n", dev->id, *BufLen);
    }
}


static void
scsi_cdrom_buf_alloc(scsi_cdrom_t *dev, uint32_t len)
{
    scsi_cdrom_log("CD-ROM %i: Allocated buffer length: %i\n", dev->id, len);
    cdbufferb = (uint8_t *) malloc(len);
}


static void
scsi_cdrom_buf_free(scsi_cdrom_t *dev)
{
    if (cdbufferb) {
	scsi_cdrom_log("CD-ROM %i: Freeing buffer...\n", dev->id);
	free(cdbufferb);
	cdbufferb = NULL;
    }
}


void
scsi_cdrom_command(void *p, uint8_t *cdb)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;
    int len, max_len, used_len, alloc_length, msf;
    int pos = 0, i= 0, size_idx, idx = 0;
    uint32_t feature;
    unsigned preamble_len;
    int toc_format, block_desc = 0;
    int ret, format = 0;
    int real_pos, track = 0;
    char device_identify[9] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', 0 };
    char device_identify_ex[15] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', ' ', 'v', '1', '.', '0', '0', 0 };
    int32_t blen = 0, *BufLen;
    uint8_t *b;
    uint32_t profiles[2] = { MMC_PROFILE_CD_ROM, MMC_PROFILE_DVD_ROM };

    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
	BufLen = &scsi_devices[dev->drv->scsi_device_id].buffer_length;
	dev->status &= ~ERR_STAT;
    } else {
	BufLen = &blen;
	dev->error = 0;
    }

    dev->packet_len = 0;
    dev->request_pos = 0;

    device_identify[7] = dev->id + 0x30;

    device_identify_ex[7] = dev->id + 0x30;
    device_identify_ex[10] = EMU_VERSION[0];
    device_identify_ex[12] = EMU_VERSION[2];
    device_identify_ex[13] = EMU_VERSION[3];

    dev->data_pos = 0;

    memcpy(dev->current_cdb, cdb, 12);

    if (dev->drv->ops && dev->drv->ops->status)
	dev->drv->cd_status = dev->drv->ops->status(dev->drv);
    else
	dev->drv->cd_status = CD_STATUS_EMPTY;

    if (cdb[0] != 0) {
	scsi_cdrom_log("CD-ROM %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, Unit attention: %i\n",
		  dev->id, cdb[0], scsi_cdrom_sense_key, scsi_cdrom_asc, scsi_cdrom_ascq, dev->unit_attention);
	scsi_cdrom_log("CD-ROM %i: Request length: %04X\n", dev->id, dev->request_length);

	scsi_cdrom_log("CD-ROM %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
		  cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
		  cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    msf = cdb[1] & 2;
    dev->sector_len = 0;

    scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

    /* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
    if (scsi_cdrom_pre_execution_check(dev, cdb) == 0)
	return;

    switch (cdb[0]) {
	case GPCMD_TEST_UNIT_READY:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
		scsi_cdrom_command_complete(dev);
		break;

	case GPCMD_REZERO_UNIT:
		if (dev->drv->ops->stop)
			dev->drv->ops->stop(dev->drv);
		dev->sector_pos = dev->sector_len = 0;
		dev->drv->seek_diff = dev->drv->seek_pos;
		cdrom_seek(dev->drv, 0);
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
		break;

	case GPCMD_REQUEST_SENSE:
		/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
		   should forget about the not ready, and report unit attention straight away. */
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
		max_len = cdb[4];

		if (!max_len) {
			scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20LL * CDROM_TIME;
			scsi_cdrom_set_callback(dev);
			break;
		}

		scsi_cdrom_buf_alloc(dev, 256);
		scsi_cdrom_set_buf_len(dev, BufLen, &max_len);
		scsi_cdrom_request_sense(dev, cdbufferb, max_len);
		scsi_cdrom_data_command_finish(dev, 18, 18, cdb[4], 0);
		break;

	case GPCMD_SET_SPEED:
	case GPCMD_SET_SPEED_ALT:
		dev->drv->cur_speed = (cdb[3] | (cdb[2] << 8)) / 176;
		if (dev->drv->cur_speed < 1)
			dev->drv->cur_speed = 1;
		else if (dev->drv->cur_speed > dev->drv->speed)
			dev->drv->cur_speed = dev->drv->speed;
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
		scsi_cdrom_command_complete(dev);
		break;

	case GPCMD_MECHANISM_STATUS:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
		len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

		scsi_cdrom_buf_alloc(dev, 8);

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		memset(cdbufferb, 0, 8);
		cdbufferb[5] = 1;

		scsi_cdrom_data_command_finish(dev, 8, 8, len, 0);
		break;

	case GPCMD_READ_TOC_PMA_ATIP:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		max_len = cdb[7];
		max_len <<= 8;
		max_len |= cdb[8];

		scsi_cdrom_buf_alloc(dev, 65536);

		toc_format = cdb[2] & 0xf;

		if (toc_format == 0)
			toc_format = (cdb[9] >> 6) & 3;

		if (!dev->drv->ops) {
			scsi_cdrom_not_ready(dev);
			return;
		}

		switch (toc_format) {
			case 0: /*Normal*/
				if (!dev->drv->ops->readtoc) {
					scsi_cdrom_not_ready(dev);
					return;
				}
				len = dev->drv->ops->readtoc(dev->drv, cdbufferb, cdb[6], msf, max_len,
							    0);
				break;
			case 1: /*Multi session*/
				if (!dev->drv->ops->readtoc_session) {
					scsi_cdrom_not_ready(dev);
					return;
				}
				len = dev->drv->ops->readtoc_session(dev->drv, cdbufferb, msf, max_len);
				cdbufferb[0] = 0; cdbufferb[1] = 0xA;
				break;
			case 2: /*Raw*/
				if (!dev->drv->ops->readtoc_raw) {
					scsi_cdrom_not_ready(dev);
					return;
				}
				len = dev->drv->ops->readtoc_raw(dev->drv, cdbufferb, max_len);
				break;
			default:
				scsi_cdrom_invalid_field(dev);
				scsi_cdrom_buf_free(dev);
				return;
		}

		if (len > max_len) {
			len = max_len;

			cdbufferb[0] = ((len - 2) >> 8) & 0xff;
			cdbufferb[1] = (len - 2) & 0xff;
		}

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_data_command_finish(dev, len, len, len, 0);
		/* scsi_cdrom_log("CD-ROM %i: READ_TOC_PMA_ATIP format %02X, length %i (%i)\n", dev->id,
			     toc_format, ide->cylinder, cdbufferb[1]); */
		return;

	case GPCMD_READ_CD_OLD:
		/* IMPORTANT: Convert the command to new read CD
			      for pass through purposes. */
		dev->current_cdb[0] = 0xbe;
	case GPCMD_READ_6:
	case GPCMD_READ_10:
	case GPCMD_READ_12:
	case GPCMD_READ_CD:
	case GPCMD_READ_CD_MSF:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
		alloc_length = 2048;

		switch(cdb[0]) {
			case GPCMD_READ_6:
				dev->sector_len = cdb[4];
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				msf = 0;
				break;
			case GPCMD_READ_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				scsi_cdrom_log("CD-ROM %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len,
					  dev->sector_pos);
				msf = 0;
				break;
			case GPCMD_READ_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				scsi_cdrom_log("CD-ROM %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len,
					  dev->sector_pos);
				msf = 0;
				break;
			case GPCMD_READ_CD_MSF:
				alloc_length = 2856;
				dev->sector_len = MSFtoLBA(cdb[6], cdb[7], cdb[8]);
				dev->sector_pos = MSFtoLBA(cdb[3], cdb[4], cdb[5]);

				dev->sector_len -= dev->sector_pos;
				dev->sector_len++;
				msf = 1;
				break;
			case GPCMD_READ_CD_OLD:
			case GPCMD_READ_CD:
				alloc_length = 2856;
				dev->sector_len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

				msf = 0;
				break;
		}

		dev->drv->seek_diff = ABS((int) (pos - dev->drv->seek_pos));
		dev->drv->seek_pos = dev->sector_pos;

		if (!dev->sector_len) {
			scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
			/* scsi_cdrom_log("CD-ROM %i: All done - callback set\n", dev->id); */
			dev->packet_status = PHASE_COMPLETE;
			dev->callback = 20LL * CDROM_TIME;
			scsi_cdrom_set_callback(dev);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're reading all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		scsi_cdrom_buf_alloc(dev, dev->packet_len);

		ret = scsi_cdrom_read_blocks(dev, &alloc_length, 1);
		if (ret <= 0) {
			scsi_cdrom_buf_free(dev);
			return;
		}

		dev->requested_blocks = max_len;
		dev->packet_len = alloc_length;

		scsi_cdrom_set_buf_len(dev, BufLen, (int32_t *) &dev->packet_len);

		scsi_cdrom_data_command_finish(dev, alloc_length, alloc_length / dev->requested_blocks,
					       alloc_length, 0);

		if (dev->packet_status != PHASE_COMPLETE)
			ui_sb_update_icon(SB_CDROM | dev->id, 1);
		else
			ui_sb_update_icon(SB_CDROM | dev->id, 0);
		return;

	case GPCMD_READ_HEADER:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		alloc_length = ((cdb[7] << 8) | cdb[8]);
		scsi_cdrom_buf_alloc(dev, 8);

		dev->sector_len = 1;
		dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4]<<8) | cdb[5];
		if (msf)
			real_pos = cdrom_lba_to_msf_accurate(dev->sector_pos);
		else
			real_pos = dev->sector_pos;
		cdbufferb[0] = 1; /*2048 bytes user data*/
		cdbufferb[1] = cdbufferb[2] = cdbufferb[3] = 0;
		cdbufferb[4] = (real_pos >> 24);
		cdbufferb[5] = ((real_pos >> 16) & 0xff);
		cdbufferb[6] = ((real_pos >> 8) & 0xff);
		cdbufferb[7] = real_pos & 0xff;

		len = 8;
		len = MIN(len, alloc_length);

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_data_command_finish(dev, len, len, len, 0);
		return;

	case GPCMD_MODE_SENSE_6:
	case GPCMD_MODE_SENSE_10:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		if (dev->drv->bus_type == CDROM_BUS_SCSI)
			block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
		else
			block_desc = 0;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = cdb[4];
			scsi_cdrom_buf_alloc(dev, 256);
		} else {
			len = (cdb[8] | (cdb[7] << 8));
			scsi_cdrom_buf_alloc(dev, 65536);
		}

		if (!(scsi_cdrom_mode_sense_page_flags & (1LL << (uint64_t) (cdb[2] & 0x3f)))) {
			scsi_cdrom_invalid_field(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		memset(cdbufferb, 0, len);
		alloc_length = len;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = scsi_cdrom_mode_sense(dev, cdbufferb, 4, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			cdbufferb[0] = len - 1;
			if (dev->drv->ops && dev->drv->ops->media_type_id)
				cdbufferb[1] = dev->drv->ops->media_type_id(dev->drv);
			else
				cdbufferb[1] = 0x70;
			if (block_desc)
				cdbufferb[3] = 8;
		} else {
			len = scsi_cdrom_mode_sense(dev, cdbufferb, 8, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			cdbufferb[0]=(len - 2) >> 8;
			cdbufferb[1]=(len - 2) & 255;
			if (dev->drv->ops && dev->drv->ops->media_type_id)
				cdbufferb[2] = dev->drv->ops->media_type_id(dev->drv);
			else
				cdbufferb[2] = 0x70;
			if (block_desc) {
				cdbufferb[6] = 0;
				cdbufferb[7] = 8;
			}
		}

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_log("CD-ROM %i: Reading mode page: %02X...\n", dev->id, cdb[2]);

		scsi_cdrom_data_command_finish(dev, len, len, alloc_length, 0);
		return;

	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_OUT);

		if (cdb[0] == GPCMD_MODE_SELECT_6) {
			len = cdb[4];
			scsi_cdrom_buf_alloc(dev, 256);
		} else {
			len = (cdb[7] << 8) | cdb[8];
			scsi_cdrom_buf_alloc(dev, 65536);
		}

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		dev->total_length = len;
		dev->do_page_save = cdb[1] & 1;

		scsi_cdrom_data_command_finish(dev, len, len, len, 1);
		return;

	case GPCMD_GET_CONFIGURATION:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		/* XXX: could result in alignment problems in some architectures */
		feature = (cdb[2] << 8) | cdb[3];
		max_len = (cdb[7] << 8) | cdb[8];

		/* only feature 0 is supported */
		if ((cdb[2] != 0) || (cdb[3] > 2)) {
			scsi_cdrom_invalid_field(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		scsi_cdrom_buf_alloc(dev, 65536);
		memset(cdbufferb, 0, max_len);

		alloc_length = 0;
		b = cdbufferb;

		/*
		 * the number of sectors from the media tells us which profile
		 * to use as current.  0 means there is no media
		 */
		if (dev->drv->ops && dev->drv->ops->ready &&
		    dev->drv->ops->ready(dev->drv)) {
			len = dev->drv->ops->size(dev->drv);
			if (len > CD_MAX_SECTORS) {
				b[6] = (MMC_PROFILE_DVD_ROM >> 8) & 0xff;
				b[7] = MMC_PROFILE_DVD_ROM & 0xff;
				ret = 1;
			} else {
				b[6] = (MMC_PROFILE_CD_ROM >> 8) & 0xff;
				b[7] = MMC_PROFILE_CD_ROM & 0xff;
				ret = 0;
			}
		} else
			ret = 2;

		alloc_length = 8;
		b += 8;

		if ((feature == 0) || ((cdb[1] & 3) < 2)) {
			b[2] = (0 << 2) | 0x02 | 0x01; /* persistent and current */
			b[3] = 8;

			alloc_length += 4;
			b += 4;

			for (i = 0; i < 2; i++) {
				b[0] = (profiles[i] >> 8) & 0xff;
				b[1] = profiles[i] & 0xff;

				if (ret == i)
					b[2] |= 1;

				alloc_length += 4;
				b += 4;
			}
		}
		if ((feature == 1) || ((cdb[1] & 3) < 2)) {
			b[1] = 1;
			b[2] = (2 << 2) | 0x02 | 0x01; /* persistent and current */
			b[3] = 8;

			if (dev->drv->bus_type == CDROM_BUS_SCSI)
				b[7] = 1;
			else
				b[7] = 2;
			b[8] = 1;

			alloc_length += 12;
			b += 12;
		}
		if ((feature == 2) || ((cdb[1] & 3) < 2)) {
			b[1] = 2;
			b[2] = (1 << 2) | 0x02 | 0x01; /* persistent and current */
			b[3] = 4;

			b[4] = 2;

			alloc_length += 8;
			b += 8;
		}

		cdbufferb[0] = ((alloc_length - 4) >> 24) & 0xff;
		cdbufferb[1] = ((alloc_length - 4) >> 16) & 0xff;
		cdbufferb[2] = ((alloc_length - 4) >> 8) & 0xff;
		cdbufferb[3] = (alloc_length - 4) & 0xff;

		alloc_length = MIN(alloc_length, max_len);

		scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);

		scsi_cdrom_data_command_finish(dev, alloc_length, alloc_length, alloc_length, 0);
		break;

	case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		scsi_cdrom_buf_alloc(dev, 8 + sizeof(gesn_event_header_t));

		gesn_cdb = (void *) cdb;
		gesn_event_header = (void *) cdbufferb;

		/* It is fine by the MMC spec to not support async mode operations. */
		if (!(gesn_cdb->polled & 0x01)) {
			/* asynchronous mode */
			/* Only polling is supported, asynchronous mode is not. */
			scsi_cdrom_invalid_field(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		/*
		 * These are the supported events.
		 *
		 * We currently only support requests of the 'media' type.
		 * Notification class requests and supported event classes are bitmasks,
		 * but they are built from the same values as the "notification class"
		 * field.
		 */
		gesn_event_header->supported_events = 1 << GESN_MEDIA;

		/*
		 * We use |= below to set the class field; other bits in this byte
		 * are reserved now but this is useful to do if we have to use the
		 * reserved fields later.
		 */
		gesn_event_header->notification_class = 0;

		/*
		 * Responses to requests are to be based on request priority.  The
		 * notification_class_request_type enum above specifies the
		 * priority: upper elements are higher prio than lower ones.
		 */
		if (gesn_cdb->class & (1 << GESN_MEDIA)) {
			gesn_event_header->notification_class |= GESN_MEDIA;

			cdbufferb[4] = dev->media_status;	/* Bits 7-4 = Reserved, Bits 4-1 = Media Status */
			cdbufferb[5] = 1;			/* Power Status (1 = Active) */
			cdbufferb[6] = 0;
			cdbufferb[7] = 0;
			used_len = 8;
		} else {
			gesn_event_header->notification_class = 0x80; /* No event available */
			used_len = sizeof(*gesn_event_header);
		}
		gesn_event_header->len = used_len - sizeof(*gesn_event_header);

		memcpy(cdbufferb, gesn_event_header, 4);

		scsi_cdrom_set_buf_len(dev, BufLen, &used_len);

		scsi_cdrom_data_command_finish(dev, used_len, used_len, used_len, 0);
		break;

	case GPCMD_READ_DISC_INFORMATION:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);
		
		max_len = cdb[7];
		max_len <<= 8;
		max_len |= cdb[8];

		scsi_cdrom_buf_alloc(dev, 65536);

		memset(cdbufferb, 0, 34);
		memset(cdbufferb, 1, 9);
		cdbufferb[0] = 0;
		cdbufferb[1] = 32;
		cdbufferb[2] = 0xe; /* last session complete, disc finalized */
		cdbufferb[7] = 0x20; /* unrestricted use */
		cdbufferb[8] = 0x00; /* CD-ROM */

		len=34;
		len = MIN(len, max_len);

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_data_command_finish(dev, len, len, len, 0);
		break;

	case GPCMD_READ_TRACK_INFORMATION:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		max_len = cdb[7];
		max_len <<= 8;
		max_len |= cdb[8];

		scsi_cdrom_buf_alloc(dev, 65536);

		track = ((uint32_t) cdb[2]) << 24;
		track |= ((uint32_t) cdb[3]) << 16;
		track |= ((uint32_t) cdb[4]) << 8;
		track |= (uint32_t) cdb[5];

		if (((cdb[1] & 0x03) != 1) || (track != 1)) {
			scsi_cdrom_invalid_field(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		len = 36;

		memset(cdbufferb, 0, 36);
		cdbufferb[0] = 0;
		cdbufferb[1] = 34;
		cdbufferb[2] = 1; /* track number (LSB) */
		cdbufferb[3] = 1; /* session number (LSB) */
		cdbufferb[5] = (0 << 5) | (0 << 4) | (4 << 0); /* not damaged, primary copy, data track */
		cdbufferb[6] = (0 << 7) | (0 << 6) | (0 << 5) | (0 << 6) | (1 << 0); /* not reserved track, not blank, not packet writing, not fixed packet, data mode 1 */
		cdbufferb[7] = (0 << 1) | (0 << 0); /* last recorded address not valid, next recordable address not valid */
		if (dev->drv->ops && dev->drv->ops->size) {
			cdbufferb[24] = (dev->drv->ops->size(dev->drv) >> 24) & 0xff; /* track size */
			cdbufferb[25] = (dev->drv->ops->size(dev->drv) >> 16) & 0xff; /* track size */
			cdbufferb[26] = (dev->drv->ops->size(dev->drv) >> 8) & 0xff; /* track size */
			cdbufferb[27] = dev->drv->ops->size(dev->drv) & 0xff; /* track size */
		} else {
			scsi_cdrom_not_ready(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		if (len > max_len) {
			len = max_len;
			cdbufferb[0] = ((max_len - 2) >> 8) & 0xff;
			cdbufferb[1] = (max_len - 2) & 0xff;
		}

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_data_command_finish(dev, len, len, max_len, 0);
		break;

	case GPCMD_PLAY_AUDIO_10:
	case GPCMD_PLAY_AUDIO_12:
	case GPCMD_PLAY_AUDIO_MSF:
	case GPCMD_PLAY_AUDIO_TRACK_INDEX:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

		switch(cdb[0]) {
			case GPCMD_PLAY_AUDIO_10:
				msf = 0;
				pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				len = (cdb[7] << 8) | cdb[8];
				break;
			case GPCMD_PLAY_AUDIO_12:
				msf = 0;
				pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				len = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
				break;
			case GPCMD_PLAY_AUDIO_MSF:
				/* This is apparently deprecated in the ATAPI spec, and apparently
				   has been since 1995 (!). Hence I'm having to guess most of it. */
				msf = 1;
				pos = (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
				break;
			case GPCMD_PLAY_AUDIO_TRACK_INDEX:
				msf = 2;
				pos = (cdb[4] << 8) | cdb[5];
				len = (cdb[7] << 8) | cdb[8];
				break;
		}

		if ((dev->drv->host_drive < 1) || (dev->drv->cd_status <= CD_STATUS_DATA_ONLY)) {
			scsi_cdrom_illegal_mode(dev);
			break;
		}

		if (dev->drv->ops && dev->drv->ops->playaudio)
			ret = dev->drv->ops->playaudio(dev->drv, pos, len, msf);
		else
			ret = 0;

		if (ret)
			scsi_cdrom_command_complete(dev);
		else
			scsi_cdrom_illegal_mode(dev);
		break;

	case GPCMD_READ_SUBCHANNEL:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		max_len = cdb[7];
		max_len <<= 8;
		max_len |= cdb[8];
		msf = (cdb[1] >> 1) & 1;

		scsi_cdrom_buf_alloc(dev, 32);

		scsi_cdrom_log("CD-ROM %i: Getting page %i (%s)\n", dev->id, cdb[3], msf ? "MSF" : "LBA");

		if (cdb[3] > 3) {
			/* scsi_cdrom_log("CD-ROM %i: Read subchannel check condition %02X\n", dev->id,
				     cdb[3]); */
			scsi_cdrom_invalid_field(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		switch(cdb[3]) {
			case 0:
				alloc_length = 4;
				break;
			case 1:
				alloc_length = 16;
				break;
			default:
				alloc_length = 24;
				break;
		}

		memset(cdbufferb, 0, 24);
		pos = 0;
		cdbufferb[pos++] = 0;
		cdbufferb[pos++] = 0; /*Audio status*/
		cdbufferb[pos++] = 0; cdbufferb[pos++] = 0; /*Subchannel length*/
		cdbufferb[pos++] = cdb[3] & 3; /*Format code*/
		if (cdb[3] == 1) {
			if (dev->drv->ops && dev->drv->ops->getcurrentsubchannel)
				cdbufferb[1] = dev->drv->ops->getcurrentsubchannel(dev->drv, &cdbufferb[5], msf);
			else {
				scsi_cdrom_not_ready(dev);
				scsi_cdrom_buf_free(dev);
				return;
			}
			switch(dev->drv->cd_status) {
				case CD_STATUS_PLAYING:
					cdbufferb[1] = 0x11;
					break;
				case CD_STATUS_PAUSED:
					cdbufferb[1] = 0x12;
					break;
				case CD_STATUS_DATA_ONLY:
					cdbufferb[1] = 0x15;
					break;
				default:
					cdbufferb[1] = 0x13;
					break;
			}
		}

		if (!(cdb[2] & 0x40) || (cdb[3] == 0))
			len = 4;
		else
			len = alloc_length;

		len = MIN(len, max_len);
		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_data_command_finish(dev, len, len, len, 0);
		break;

	case GPCMD_READ_DVD_STRUCTURE:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		alloc_length = (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);

		scsi_cdrom_buf_alloc(dev, alloc_length);

		if (dev->drv->ops && dev->drv->ops->size)
			len = dev->drv->ops->size(dev->drv);
		else {
			scsi_cdrom_not_ready(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		if ((cdb[7] < 0xc0) && (len <= CD_MAX_SECTORS)) {
			scsi_cdrom_incompatible_format(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}

		memset(cdbufferb, 0, alloc_length);

		if ((cdb[7] <= 0x7f) || (cdb[7] == 0xff)) {
			if (cdb[1] == 0) {
				ret = scsi_cdrom_read_dvd_structure(dev, format, cdb, cdbufferb);
					if (ret) {
					scsi_cdrom_set_buf_len(dev, BufLen, &alloc_length);
					scsi_cdrom_data_command_finish(dev, alloc_length, alloc_length,
								       len, 0);
				} else
					scsi_cdrom_buf_free(dev);
				return;
			}
		} else {
			scsi_cdrom_invalid_field(dev);
			scsi_cdrom_buf_free(dev);
			return;
		}
		break;

	case GPCMD_START_STOP_UNIT:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

		switch(cdb[4] & 3) {
			case 0:		/* Stop the disc. */
				if (dev->drv->ops && dev->drv->ops->stop)
					dev->drv->ops->stop(dev->drv);
				break;
			case 1:		/* Start the disc and read the TOC. */
				if (dev->drv->ops && dev->drv->ops->medium_changed)
					dev->drv->ops->medium_changed(dev->drv);	/* This causes a TOC reload. */
				break;
			case 2:		/* Eject the disc if possible. */
				if (dev->drv->ops && dev->drv->ops->stop)
					dev->drv->ops->stop(dev->drv);
				cdrom_eject(dev->id);
				break;
			case 3:		/* Load the disc (close tray). */
				cdrom_reload(dev->id);
				break;
		}

		scsi_cdrom_command_complete(dev);
		break;

	case GPCMD_INQUIRY:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		max_len = cdb[3];
		max_len <<= 8;
		max_len |= cdb[4];

		scsi_cdrom_buf_alloc(dev, 65536);

		if (cdb[1] & 1) {
			preamble_len = 4;
			size_idx = 3;

			cdbufferb[idx++] = 05;
			cdbufferb[idx++] = cdb[2];
			cdbufferb[idx++] = 0;

			idx++;

			switch (cdb[2]) {
				case 0x00:
					cdbufferb[idx++] = 0x00;
					cdbufferb[idx++] = 0x83;
					break;
				case 0x83:
					if (idx + 24 > max_len) {
						scsi_cdrom_data_phase_error(dev);
						scsi_cdrom_buf_free(dev);
						return;
					}

					cdbufferb[idx++] = 0x02;
					cdbufferb[idx++] = 0x00;
					cdbufferb[idx++] = 0x00;
					cdbufferb[idx++] = 20;
					ide_padstr8(cdbufferb + idx, 20, "53R141");	/* Serial */
					idx += 20;

					if (idx + 72 > cdb[4])
						goto atapi_out;
					cdbufferb[idx++] = 0x02;
					cdbufferb[idx++] = 0x01;
					cdbufferb[idx++] = 0x00;
					cdbufferb[idx++] = 68;
					ide_padstr8(cdbufferb + idx, 8, EMU_NAME); /* Vendor */
					idx += 8;
					ide_padstr8(cdbufferb + idx, 40, device_identify_ex); /* Product */
					idx += 40;
					ide_padstr8(cdbufferb + idx, 20, "53R141"); /* Product */
					idx += 20;
					break;
				default:
					scsi_cdrom_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
					scsi_cdrom_invalid_field(dev);
					scsi_cdrom_buf_free(dev);
					return;
			}
		} else {
			preamble_len = 5;
			size_idx = 4;

			memset(cdbufferb, 0, 8);
			cdbufferb[0] = 5; /*CD-ROM*/
			cdbufferb[1] = 0x80; /*Removable*/
			cdbufferb[2] = (dev->drv->bus_type == CDROM_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
			cdbufferb[3] = (dev->drv->bus_type == CDROM_BUS_SCSI) ? 0x12 : 0x21;
			cdbufferb[4] = 31;
			if (dev->drv->bus_type == CDROM_BUS_SCSI) {
				cdbufferb[6] = 1;	/* 16-bit transfers supported */
				cdbufferb[7] = 0x20;	/* Wide bus supported */
			}

			ide_padstr8(cdbufferb + 8, 8, EMU_NAME); /* Vendor */
			ide_padstr8(cdbufferb + 16, 16, device_identify); /* Product */
			ide_padstr8(cdbufferb + 32, 4, EMU_VERSION); /* Revision */
			idx = 36;

			if (max_len == 96) {
				cdbufferb[4] = 91;
				idx = 96;
			}
		}

atapi_out:
		cdbufferb[size_idx] = idx - preamble_len;
		len=idx;

		len = MIN(len, max_len);
		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_data_command_finish(dev, len, len, max_len, 0);
		break;

	case GPCMD_PREVENT_REMOVAL:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);
		scsi_cdrom_command_complete(dev);
		break;

	case GPCMD_PAUSE_RESUME_ALT:
	case GPCMD_PAUSE_RESUME:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

		if (cdb[8] & 1) {
			if (dev->drv->ops && dev->drv->ops->resume)
				dev->drv->ops->resume(dev->drv);
			else {
				scsi_cdrom_illegal_mode(dev);
				break;
			}
		} else {
			if (dev->drv->ops && dev->drv->ops->pause)
				dev->drv->ops->pause(dev->drv);
			else {
				scsi_cdrom_illegal_mode(dev);
				break;
			}
		}
		scsi_cdrom_command_complete(dev);
		break;

	case GPCMD_SEEK_6:
	case GPCMD_SEEK_10:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

		switch(cdb[0]) {
			case GPCMD_SEEK_6:
				pos = (cdb[2] << 8) | cdb[3];
				break;
			case GPCMD_SEEK_10:
				pos = (cdb[2] << 24) | (cdb[3]<<16) | (cdb[4]<<8) | cdb[5];
				break;
		}
		dev->drv->seek_diff = ABS((int) (pos - dev->drv->seek_pos));
		cdrom_seek(dev->drv, pos);
		scsi_cdrom_command_complete(dev);
		break;

	case GPCMD_READ_CDROM_CAPACITY:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_DATA_IN);

		scsi_cdrom_buf_alloc(dev, 8);

		if (scsi_cdrom_read_capacity(dev, dev->current_cdb, cdbufferb, (uint32_t *) &len) == 0) {
			scsi_cdrom_buf_free(dev);
			return;
		}

		scsi_cdrom_set_buf_len(dev, BufLen, &len);

		scsi_cdrom_data_command_finish(dev, len, len, len, 0);
		break;

	case GPCMD_STOP_PLAY_SCAN:
		scsi_cdrom_set_phase(dev, SCSI_PHASE_STATUS);

		if (dev->drv->ops && dev->drv->ops->stop)
			dev->drv->ops->stop(dev->drv);
		else {
			scsi_cdrom_illegal_mode(dev);
			break;
		}
		scsi_cdrom_command_complete(dev);
		break;

	default:
		scsi_cdrom_illegal_opcode(dev);
		break;
    }

    /* scsi_cdrom_log("CD-ROM %i: Phase: %02X, request length: %i\n", dev->phase, dev->request_length); */

    if (scsi_cdrom_atapi_phase_to_scsi(dev) == SCSI_PHASE_STATUS)
	scsi_cdrom_buf_free(dev);
}


/* The command second phase function, needed for Mode Select. */
static uint8_t
scsi_cdrom_phase_data_out(scsi_cdrom_t *dev)
{
    uint16_t block_desc_len, pos;
    uint16_t i = 0;

    uint8_t error = 0;
    uint8_t page, page_len, hdr_len, val, old_val, ch;

    switch(dev->current_cdb[0]) {
	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10)
			hdr_len = 8;
		else
			hdr_len = 4;

		if (dev->drv->bus_type == CDROM_BUS_SCSI) {
			if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
				block_desc_len = cdbufferb[2];
				block_desc_len <<= 8;
				block_desc_len |= cdbufferb[3];
			} else {
				block_desc_len = cdbufferb[6];
				block_desc_len <<= 8;
				block_desc_len |= cdbufferb[7];
			}
		} else
			block_desc_len = 0;

		pos = hdr_len + block_desc_len;

		while(1) {
			page = cdbufferb[pos] & 0x3F;
			page_len = cdbufferb[pos + 1];

			pos += 2;

			if (!(scsi_cdrom_mode_sense_page_flags & (1LL << ((uint64_t) page)))) {
				scsi_cdrom_log("Unimplemented page %02X\n", page);
				error |= 1;
			} else {
				for (i = 0; i < page_len; i++) {
					ch = scsi_cdrom_mode_sense_pages_changeable.pages[page][i + 2];
					val = cdbufferb[pos + i];
					old_val = dev->ms_pages_saved.pages[page][i + 2];
					if (val != old_val) {
						if (ch)
							dev->ms_pages_saved.pages[page][i + 2] = val;
						else {
							scsi_cdrom_log("Unchangeable value on position %02X on page %02X\n", i + 2, page);
							error |= 1;
						}
					}
				}
			}

			pos += page_len;

			if (dev->drv->bus_type == CDROM_BUS_SCSI)
				val = scsi_cdrom_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
			else
				val = scsi_cdrom_mode_sense_pages_default.pages[page][0] & 0x80;

			if (dev->do_page_save && val)
				scsi_cdrom_mode_sense_save(dev);

			if (pos >= dev->total_length)
				break;
		}

		if (error) {
			scsi_cdrom_invalid_field_pl(dev);
			return 0;
		}
		break;
    }

    return 1;
}


/* This is the general ATAPI PIO request function. */
static void
scsi_cdrom_pio_request(scsi_cdrom_t *dev, uint8_t out)
{
    int ret = 0;

    if (dev->drv->bus_type < CDROM_BUS_SCSI) {
	scsi_cdrom_log("CD-ROM %i: Lowering IDE IRQ\n", dev->id);
	ide_irq_lower(ide_drives[dev->drv->ide_channel]);
    }

    dev->status = BUSY_STAT;

    if (dev->pos >= dev->packet_len) {
	scsi_cdrom_log("CD-ROM %i: %i bytes %s, command done\n", dev->id, dev->pos, out ? "written" : "read");

	dev->pos = dev->request_pos = 0;
	if (out) {
		ret = scsi_cdrom_phase_data_out(dev);
		/* If ret = 0 (phase 1 error), then we do not do anything else other than
		   free the buffer, as the phase and callback have already been set by the
		   error function. */
		if (ret)
			scsi_cdrom_command_complete(dev);
	} else
		scsi_cdrom_command_complete(dev);
	scsi_cdrom_buf_free(dev);
    } else {
	scsi_cdrom_log("CD-ROM %i: %i bytes %s, %i bytes are still left\n", dev->id, dev->pos,
		  out ? "written" : "read", dev->packet_len - dev->pos);

	/* If less than (packet length) bytes are remaining, update packet length
	   accordingly. */
	if ((dev->packet_len - dev->pos) < (dev->max_transfer_len))
		dev->max_transfer_len = dev->packet_len - dev->pos;
	scsi_cdrom_log("CD-ROM %i: Packet length %i, request length %i\n", dev->id, dev->packet_len,
		  dev->max_transfer_len);

	dev->packet_status = out ? PHASE_DATA_OUT : PHASE_DATA_IN;

	dev->status = BUSY_STAT;
	dev->phase = 1;
	scsi_cdrom_callback(dev);
	dev->callback = 0LL;
	scsi_cdrom_set_callback(dev);

	dev->request_pos = 0;
    }
}


static int
scsi_cdrom_read_from_ide_dma(scsi_cdrom_t *dev)
{
    int ret;

    if (!dev)
	return 0;

    if (ide_bus_master_write) {
	ret = ide_bus_master_write(dev->drv->ide_channel >> 1,
				   cdbufferb, dev->packet_len,
				   ide_bus_master_priv[dev->drv->ide_channel >> 1]);
	if (ret == 2)		/* DMA not enabled, wait for it to be enabled. */
		return 2;
	else if (ret == 1) {	/* DMA error. */		
		scsi_cdrom_bus_master_error(dev);
		return 0;
	} else
		return 1;
    } else
	return 0;

    return 0;
}


static int
scsi_cdrom_read_from_scsi_dma(uint8_t scsi_id)
{
    scsi_cdrom_t *dev = scsi_devices[scsi_id].p;
    int32_t *BufLen = &scsi_devices[scsi_id].buffer_length;

    if (dev)
	return 0;

    scsi_cdrom_log("Reading from SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
    memcpy(cdbufferb, scsi_devices[scsi_id].cmd_buffer, *BufLen);
    return 1;
}


static void
scsi_cdrom_irq_raise(scsi_cdrom_t *dev)
{
    if (dev->drv->bus_type < CDROM_BUS_SCSI)
	ide_irq_raise(ide_drives[dev->drv->ide_channel]);
}


static int
scsi_cdrom_read_from_dma(scsi_cdrom_t *dev)
{
#ifdef ENABLE_SCSI_CDROM_LOG
    int32_t *BufLen = &scsi_devices[dev->drv->scsi_device_id].buffer_length;
#endif
    int ret = 0;

    if (dev->drv->bus_type == CDROM_BUS_SCSI)
	ret = scsi_cdrom_read_from_scsi_dma(dev->drv->scsi_device_id);
    else
	ret = scsi_cdrom_read_from_ide_dma(dev);

    if (ret != 1)
	return ret;

    if (dev->drv->bus_type == CDROM_BUS_SCSI)
	scsi_cdrom_log("CD-ROM %i: SCSI Input data length: %i\n", dev->id, *BufLen);
    else
	scsi_cdrom_log("CD-ROM %i: ATAPI Input data length: %i\n", dev->id, dev->packet_len);

    ret = scsi_cdrom_phase_data_out(dev);

    if (ret)
	return 1;
    else
	return 0;
}


static int
scsi_cdrom_write_to_ide_dma(scsi_cdrom_t *dev)
{
    int ret;

    if (!dev)
	return 0;

    if (ide_bus_master_read) {
	ret = ide_bus_master_read(dev->drv->ide_channel >> 1,
				  cdbufferb, dev->packet_len,
				  ide_bus_master_priv[dev->drv->ide_channel >> 1]);
	if (ret == 2)		/* DMA not enabled, wait for it to be enabled. */
		return 2;
	else if (ret == 1) {	/* DMA error. */		
		scsi_cdrom_bus_master_error(dev);
		return 0;
	} else
		return 1;
    } else
	return 0;
}


static int
scsi_cdrom_write_to_scsi_dma(uint8_t scsi_id)
{
    scsi_cdrom_t *dev = scsi_devices[scsi_id].p;
    int32_t *BufLen = &scsi_devices[scsi_id].buffer_length;

    if (!dev)
	return 0;

    scsi_cdrom_log("Writing to SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
    memcpy(scsi_devices[scsi_id].cmd_buffer, cdbufferb, *BufLen);
    scsi_cdrom_log("CD-ROM %i: Data from CD buffer:  %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
	      cdbufferb[0], cdbufferb[1], cdbufferb[2], cdbufferb[3], cdbufferb[4], cdbufferb[5],
	      cdbufferb[6], cdbufferb[7]);
    return 1;
}


static int
scsi_cdrom_write_to_dma(scsi_cdrom_t *dev)
{
#ifdef ENABLE_SCSI_CDROM_LOG
    int32_t *BufLen = &scsi_devices[dev->drv->scsi_device_id].buffer_length;
#endif
    int ret = 0;

    if (dev->drv->bus_type == CDROM_BUS_SCSI) {
	scsi_cdrom_log("Write to SCSI DMA: (ID %02X)\n", dev->drv->scsi_device_id);
	ret = scsi_cdrom_write_to_scsi_dma(dev->drv->scsi_device_id);
    } else
	ret = scsi_cdrom_write_to_ide_dma(dev);

    if (dev->drv->bus_type == CDROM_BUS_SCSI)
	scsi_cdrom_log("CD-ROM %i: SCSI Output data length: %i\n", dev->id, *BufLen);
    else
	scsi_cdrom_log("CD-ROM %i: ATAPI Output data length: %i\n", dev->id, dev->packet_len);

    return ret;
}


static void
scsi_cdrom_callback(void *p)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;
    int ret;

    switch(dev->packet_status) {
	case PHASE_IDLE:
		scsi_cdrom_log("CD-ROM %i: PHASE_IDLE\n", dev->id);
		dev->pos = 0;
		dev->phase = 1;
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		return;
	case PHASE_COMMAND:
		scsi_cdrom_log("CD-ROM %i: PHASE_COMMAND\n", dev->id);
		dev->status = BUSY_STAT | (dev->status & ERR_STAT);
		memcpy(dev->atapi_cdb, cdbufferb, 12);
		scsi_cdrom_command(dev, dev->atapi_cdb);
		return;
	case PHASE_COMPLETE:
		scsi_cdrom_log("CD-ROM %i: PHASE_COMPLETE\n", dev->id);
		dev->status = READY_STAT;
		dev->phase = 3;
		dev->packet_status = 0xFF;
		ui_sb_update_icon(SB_CDROM | dev->id, 0);
		scsi_cdrom_irq_raise(dev);
		return;
	case PHASE_DATA_OUT:
		scsi_cdrom_log("CD-ROM %i: PHASE_DATA_OUT\n", dev->id);
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		dev->phase = 0;
		scsi_cdrom_irq_raise(dev);
		return;
	case PHASE_DATA_OUT_DMA:
		scsi_cdrom_log("CD-ROM %i: PHASE_DATA_OUT_DMA\n", dev->id);
		ret = scsi_cdrom_read_from_dma(dev);

		if ((ret == 1) || (dev->drv->bus_type == CDROM_BUS_SCSI)) {
			scsi_cdrom_log("CD-ROM %i: DMA data out phase done\n");
			scsi_cdrom_buf_free(dev);
			scsi_cdrom_command_complete(dev);
		} else if (ret == 2) {
			scsi_cdrom_log("CD-ROM %i: DMA out not enabled, wait\n");
			scsi_cdrom_command_bus(dev);
		} else {
			scsi_cdrom_log("CD-ROM %i: DMA data out phase failure\n");
			scsi_cdrom_buf_free(dev);
		}
		return;
	case PHASE_DATA_IN:
		scsi_cdrom_log("CD-ROM %i: PHASE_DATA_IN\n", dev->id);
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		dev->phase = 2;
		scsi_cdrom_irq_raise(dev);
		return;
	case PHASE_DATA_IN_DMA:
		scsi_cdrom_log("CD-ROM %i: PHASE_DATA_IN_DMA\n", dev->id);
		ret = scsi_cdrom_write_to_dma(dev);

		if ((ret == 1) || (dev->drv->bus_type == CDROM_BUS_SCSI)) {
			scsi_cdrom_log("CD-ROM %i: DMA data in phase done\n", dev->id);
			scsi_cdrom_buf_free(dev);
			scsi_cdrom_command_complete(dev);
		} else if (ret == 2) {
			scsi_cdrom_log("CD-ROM %i: DMA in not enabled, wait\n", dev->id);
			scsi_cdrom_command_bus(dev);
		} else {
			scsi_cdrom_log("CD-ROM %i: DMA data in phase failure\n", dev->id);
			scsi_cdrom_buf_free(dev);
		}
		return;
	case PHASE_ERROR:
		scsi_cdrom_log("CD-ROM %i: PHASE_ERROR\n", dev->id);
		dev->status = READY_STAT | ERR_STAT;
		dev->phase = 3;
		scsi_cdrom_irq_raise(dev);
		ui_sb_update_icon(SB_CDROM | dev->id, 0);
		return;
    }
}


static uint32_t
scsi_cdrom_packet_read(void *p, int length)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    uint16_t *cdbufferw;
    uint32_t *cdbufferl;

    uint32_t temp = 0;

    if (!dev)
	return 0;

    cdbufferw = (uint16_t *) cdbufferb;
    cdbufferl = (uint32_t *) cdbufferb;

    if (!cdbufferb)
	return 0;

    /* Make sure we return a 0 and don't attempt to read from the buffer if we're transferring bytes beyond it,
       which can happen when issuing media access commands with an allocated length below minimum request length
       (which is 1 sector = 2048 bytes). */
    switch(length) {
	case 1:
		temp = (dev->pos < dev->packet_len) ? cdbufferb[dev->pos] : 0;
		dev->pos++;
		dev->request_pos++;
		break;
	case 2:
		temp = (dev->pos < dev->packet_len) ? cdbufferw[dev->pos >> 1] : 0;
		dev->pos += 2;
		dev->request_pos += 2;
		break;
	case 4:
		temp = (dev->pos < dev->packet_len) ? cdbufferl[dev->pos >> 2] : 0;
		dev->pos += 4;
		dev->request_pos += 4;
		break;
	default:
		return 0;
    }

    if (dev->packet_status == PHASE_DATA_IN) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		scsi_cdrom_pio_request(dev, 0);
	}
	return temp;
    } else
	return 0;
}


static void
scsi_cdrom_packet_write(void *p, uint32_t val, int length)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    uint16_t *cdbufferw;
    uint32_t *cdbufferl;

    if (!dev)
	return;

    if ((dev->packet_status == PHASE_IDLE) && !cdbufferb)
	scsi_cdrom_buf_alloc(dev, 12);

    cdbufferw = (uint16_t *) cdbufferb;
    cdbufferl = (uint32_t *) cdbufferb;

    if (!cdbufferb)
	return;

    switch(length) {
	case 1:
		cdbufferb[dev->pos] = val & 0xff;
		dev->pos++;
		dev->request_pos++;
		break;
	case 2:
		cdbufferw[dev->pos >> 1] = val & 0xffff;
		dev->pos += 2;
		dev->request_pos += 2;
		break;
	case 4:
		cdbufferl[dev->pos >> 2] = val;
		dev->pos += 4;
		dev->request_pos += 4;
		break;
	default:
		return;
    }

    if (dev->packet_status == PHASE_DATA_OUT) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		scsi_cdrom_pio_request(dev, 1);
	}
	return;
    } else if (dev->packet_status == PHASE_IDLE) {
	if (dev->pos >= 12) {
		dev->pos = 0;
		dev->status = BUSY_STAT;
		dev->packet_status = PHASE_COMMAND;
		timer_process();
		scsi_cdrom_callback(dev);
		timer_update_outstanding();
	}
	return;
    }
}


static void
scsi_cdrom_close(void *p)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    if (dev) {
	free(scsi_cdrom[dev->id]);
	scsi_cdrom[dev->id] = NULL;
    }
}


static void
scsi_cdrom_stop(void *p)
{
    scsi_cdrom_t *dev = (scsi_cdrom_t *) p;

    if (dev->drv->ops && dev->drv->ops->stop)
	dev->drv->ops->stop(dev->drv);
}


static int
scsi_cdrom_get_max(int ide_has_dma, int type)
{
    int ret;

    switch(type) {
	case TYPE_PIO:
		ret = ide_has_dma ? 4 : 0;
		break;
	case TYPE_SDMA:
		ret = ide_has_dma ? -1 : 2;
		break;
	case TYPE_MDMA:
		ret = ide_has_dma ? -1 : 2;
		break;
	case TYPE_UDMA:
		ret = ide_has_dma ? -1 : 2;
		break;
	default:
		ret = -1;
		break;
    }

    return ret;
}


static int
scsi_cdrom_get_timings(int ide_has_dma, int type)
{
    int ret;

    switch(type) {
	case TIMINGS_DMA:
		ret = ide_has_dma ? 120 : 0;
		break;
	case TIMINGS_PIO:
		ret = ide_has_dma ? 120 : 0;
		break;
	case TIMINGS_PIO_FC:
		ret = 0;
		break;
	default:
		ret = 0;
		break;
    }

    return ret;
}


/**
 * Fill in ide->buffer with the output of the "IDENTIFY PACKET DEVICE" command
 */
static void
scsi_cdrom_identify(void *p, int ide_has_dma)
{
    ide_t *ide = (ide_t *) p;
#if 0
    scsi_cdrom_t *dev;
    char device_identify[9] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', 0 };

    dev = (scsi_cdrom_t *) ide->p;

    device_identify[7] = dev->id + 0x30;
    scsi_cdrom_log("ATAPI Identify: %s\n", device_identify);
#endif

    ide->buffer[0] = 0x8000 | (5<<8) | 0x80 | (2<<5); /* ATAPI device, CD-ROM drive, removable media, accelerated DRQ */
    ide_padstr((char *) (ide->buffer + 10), "", 20); /* Serial Number */
#if 0
    ide_padstr((char *) (ide->buffer + 23), EMU_VERSION, 8); /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), device_identify, 40); /* Model */
#else
    ide_padstr((char *) (ide->buffer + 23), "4.20    ", 8); /* Firmware */
    ide_padstr((char *) (ide->buffer + 27), "NEC                 CD-ROM DRIVE:273    ", 40); /* Model */
#endif
    ide->buffer[49] = 0x200; /* LBA supported */
    ide->buffer[126] = 0xfffe; /* Interpret zero byte count limit as maximum length */

    if (ide_has_dma) {
	ide->buffer[71] = 30;
	ide->buffer[72] = 30;
    }
}


void
scsi_cdrom_drive_reset(int c)
{
    cdrom_t *drv = &cdrom[c];
    scsi_device_t *sd;
    ide_t *id;

    /* Make sure to ignore any SCSI CD-ROM drive that has an out of range ID. */
    if ((drv->bus_type == CDROM_BUS_SCSI) && (drv->scsi_device_id > SCSI_ID_MAX))
	return;

    /* Make sure to ignore any ATAPI CD-ROM drive that has an out of range IDE channel. */
    if ((drv->bus_type == CDROM_BUS_ATAPI) && (drv->ide_channel > 7))
	return;

    if (!scsi_cdrom[c]) {
	scsi_cdrom[c] = (scsi_cdrom_t *) malloc(sizeof(scsi_cdrom_t));
	memset(scsi_cdrom[c], 0, sizeof(scsi_cdrom_t));
    }

    scsi_cdrom[c]->id = c;
    scsi_cdrom[c]->drv = drv;
    drv->p = scsi_cdrom[c];
    drv->insert = scsi_cdrom_insert;
    drv->get_volume = scsi_cdrom_get_volume;
    drv->get_channel = scsi_cdrom_get_channel;
    drv->close = scsi_cdrom_close;

    scsi_cdrom_init(scsi_cdrom[c]);

    if (drv->bus_type == CDROM_BUS_SCSI) {
	/* SCSI CD-ROM, attach to the SCSI bus. */
	sd = &scsi_devices[drv->scsi_device_id];

	sd->p = scsi_cdrom[c];
	sd->command = scsi_cdrom_command;
	sd->callback = scsi_cdrom_callback;
	sd->err_stat_to_scsi = scsi_cdrom_err_stat_to_scsi;
	sd->request_sense = scsi_cdrom_request_sense_for_scsi;
	sd->reset = scsi_cdrom_reset;
	sd->read_capacity = scsi_cdrom_read_capacity;
	sd->type = SCSI_REMOVABLE_CDROM;

	scsi_cdrom_log("SCSI CD-ROM drive %i attached to SCSI ID %i\n", c, cdrom[c].scsi_device_id);
    } else if (drv->bus_type == CDROM_BUS_ATAPI) {
	/* ATAPI CD-ROM, attach to the IDE bus. */
	id = ide_drives[drv->ide_channel];
	/* If the IDE channel is initialized, we attach to it,
	   otherwise, we do nothing - it's going to be a drive
	   that's not attached to anything. */
	if (id) {
		id->p = scsi_cdrom[c];
		id->get_max = scsi_cdrom_get_max;
		id->get_timings = scsi_cdrom_get_timings;
		id->identify = scsi_cdrom_identify;
		id->set_signature = scsi_cdrom_set_signature;
		id->packet_write = scsi_cdrom_packet_write;
		id->packet_read = scsi_cdrom_packet_read;
		id->stop = scsi_cdrom_stop;
		id->packet_callback = scsi_cdrom_callback;
		id->device_reset = scsi_cdrom_reset;
		id->interrupt_drq = 0;

		ide_atapi_attach(id);
	}

	scsi_cdrom_log("ATAPI CD-ROM drive %i attached to IDE channel %i\n", c, cdrom[c].ide_channel);
    }
}
