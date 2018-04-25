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
 * Version:	@(#)zip.c	1.0.20	2018/03/26
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
#include "../scsi/scsi.h"
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


zip_t		*zip[ZIP_NUM];
zip_drive_t	zip_drives[ZIP_NUM];
uint8_t atapi_zip_drives[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t scsi_zip_drives[16][8] =	{	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
						{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }	};


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
    0,
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
    0, 0, 0,
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

static uint64_t zip_mode_sense_page_flags = (1LL << GPMODE_R_W_ERROR_PAGE) |
					    (1LL << 0x02LL) | (1LL << 0x2FLL) |
					    (1LL << GPMODE_ALL_PAGES);
static uint64_t zip_250_mode_sense_page_flags = (1LL << GPMODE_R_W_ERROR_PAGE) |
					    (1LL << 0x05LL) | (1LL << 0x08LL) |
					    (1LL << 0x2FLL) |
					    (1LL << GPMODE_ALL_PAGES);


static const mode_sense_pages_t zip_mode_sense_pages_default =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x0a, 0xc8, 22, 0,  0, 0, 0, 90, 0, 0x50, 0x20 },
    {                     0x02, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
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
    {                     0x2f,    0x04, 0x5c, 0x0f, 0xff, 0x0f }
}   };

static const mode_sense_pages_t zip_250_mode_sense_pages_default =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x06, 0xc8, 0x64, 0,  0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                     0x05, 0x1e, 0x80, 0, 0x40, 0x20, 2, 0, 0, 0xef, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0b, 0x7d, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                     0x08, 0x0a, 4, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0xff, 0xff },
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
    {                     0x2f,    0x04, 0x5c, 0x0f, 0x3c, 0x0f }
}   };

static const mode_sense_pages_t zip_mode_sense_pages_default_scsi =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x0a, 0xc8, 22, 0,  0, 0, 0, 90, 0, 0x50, 0x20 },
    {                     0x02, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
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
    {                     0x2f,    0x04, 0x5c, 0x0f, 0xff, 0x0f }
}   };

static const mode_sense_pages_t zip_250_mode_sense_pages_default_scsi =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x06, 0xc8, 0x64, 0,  0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                     0x05, 0x1e, 0x80, 0, 0x40, 0x20, 2, 0, 0, 0xef, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0b, 0x7d, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                     0x08, 0x0a, 4, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0xff, 0xff },
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
    {                     0x2f,    0x04, 0x5c, 0x0f, 0x3c, 0x0f }
}   };

static const mode_sense_pages_t zip_mode_sense_pages_changeable =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x0a, 0xc8, 22, 0,  0, 0, 0, 90, 0, 0x50, 0x20 },
    {                     0x02, 0x0e, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
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
    {                     0x2f,    0x04, 0x5c, 0x0f, 0xff, 0x0f }
}   };

static const mode_sense_pages_t zip_250_mode_sense_pages_changeable =
{   {
    {                        0,    0 },
    {    GPMODE_R_W_ERROR_PAGE, 0x06, 0xc8, 0x64, 0,  0, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                        0,    0 },
    {                     0x05, 0x1e, 0x80, 0, 0x40, 0x20, 2, 0, 0, 0xef, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x0b, 0x7d, 0, 0 },
    {                        0,    0 },
    {                        0,    0 },
    {                     0x08, 0x0a, 4, 0, 0xff, 0xff, 0, 0, 0xff, 0xff, 0xff, 0xff },
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
    {                     0x2f,    0x04, 0x5c, 0x0f, 0x3c, 0x0f }
}   };

static mode_sense_pages_t zip_mode_sense_pages_saved[ZIP_NUM];


static void	zip_command_complete(uint8_t id);

void		zip_init(int id, int cdb_len_setting);
void		zip_phase_callback(uint8_t id);


#ifdef ENABLE_ZIP_LOG
int zip_do_log = ENABLE_ZIP_LOG;
#endif


static void
zip_log(const char *format, ...)
{
#ifdef ENABLE_ZIP_LOG
    va_list ap;

    if (zip_do_log) {
	va_start(ap, format);
	pclog_ex(format, ap);
	va_end(ap);
    }
#endif
}


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


void
zip_destroy_drives(void)
{
    int i;

    for (i = 0; i < ZIP_NUM; i++) {
	if (zip[i]) {
		free(zip[i]);
		zip[i] = NULL;
	}
    }
}


int
zip_load(uint8_t id, wchar_t *fn)
{
    int read_only = zip_drives[id].ui_writeprot;
    int size = 0;

    zip_drives[id].f = plat_fopen(fn, zip_drives[id].ui_writeprot ? L"rb" : L"rb+");
    if (!zip_drives[id].ui_writeprot && !zip_drives[id].f) {
	zip_drives[id].f = plat_fopen(fn, L"rb");
	read_only = 1;
    }
    if (zip_drives[id].f) {
	fseek(zip_drives[id].f, 0, SEEK_END);
	size = ftell(zip_drives[id].f);

	if ((size == ((ZIP_250_SECTORS << 9) + 0x1000)) || (size == ((ZIP_SECTORS << 9) + 0x1000))) {
		/* This is a ZDI image. */
		size -= 0x1000;
		zip_drives[id].base = 0x1000;
	} else
		zip_drives[id].base = 0;

	if (zip_drives[id].is_250) {
		if ((size != (ZIP_250_SECTORS << 9)) && (size != (ZIP_SECTORS << 9))) {
			zip_log("File is incorrect size for a ZIP image\nMust be exactly %i or %i bytes\n",
				ZIP_250_SECTORS << 9, ZIP_SECTORS << 9);
			fclose(zip_drives[id].f);
			zip_drives[id].f = NULL;
			zip_drives[id].medium_size = 0;
			zip_eject(id);	/* Make sure the host OS knows we've rejected (and ejected) the image. */
			return 0;
		}
	} else {
		if (size != (ZIP_SECTORS << 9)) {
			zip_log("File is incorrect size for a ZIP image\nMust be exactly %i bytes\n",
				ZIP_SECTORS << 9);
			fclose(zip_drives[id].f);
			zip_drives[id].f = NULL;
			zip_drives[id].medium_size = 0;
			zip_eject(id);	/* Make sure the host OS knows we've rejected (and ejected) the image. */
			return 0;
		}
	}

	zip_drives[id].medium_size = size >> 9;

	fseek(zip_drives[id].f, zip_drives[id].base, SEEK_SET);

	memcpy(zip_drives[id].image_path, fn, sizeof(zip_drives[id].image_path));

	zip_drives[id].read_only = read_only;

	return 1;
    }

    return 0;
}


void
zip_disk_reload(uint8_t id)
{
    zip_t *dev = zip[id];
    int ret = 0;

    if (wcslen(zip_drives[id].prev_image_path) == 0)
	return;
    else
	ret = zip_load(id, zip_drives[id].prev_image_path);

    if (ret)
	dev->unit_attention = 1;
}


void
zip_close(uint8_t id)
{
    if (zip_drives[id].f) {
	fclose(zip_drives[id].f);
	zip_drives[id].f = NULL;

	memcpy(zip_drives[id].prev_image_path, zip_drives[id].image_path, sizeof(zip_drives[id].prev_image_path));
	memset(zip_drives[id].image_path, 0, sizeof(zip_drives[id].image_path));

	zip_drives[id].medium_size = 0;
    }
}


void
build_atapi_zip_map()
{
    uint8_t i = 0;

    memset(atapi_zip_drives, 0xff, 8);

    for (i = 0; i < 8; i++) {
	atapi_zip_drives[i] = find_zip_for_channel(i);
	if (atapi_zip_drives[i] != 0xff)
		zip_init(atapi_zip_drives[i], 12);
    }
}


int
find_zip_for_scsi_id(uint8_t scsi_id, uint8_t scsi_lun)
{
    uint8_t i = 0;

    for (i = 0; i < ZIP_NUM; i++) {
	if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) && (zip_drives[i].scsi_device_id == scsi_id) && (zip_drives[i].scsi_device_lun == scsi_lun))
		return i;
    }
    return 0xff;
}


void
build_scsi_zip_map()
{
    uint8_t i = 0;
    uint8_t j = 0;

    for (i = 0; i < 16; i++)
	memset(scsi_zip_drives[i], 0xff, 8);

    for (i = 0; i < 16; i++) {
	for (j = 0; j < 8; j++) {
		scsi_zip_drives[i][j] = find_zip_for_scsi_id(i, j);
		if (scsi_zip_drives[i][j] != 0xff)
			zip_init(scsi_zip_drives[i][j], 12);
	}
    }
}


static void
zip_set_callback(uint8_t id)
{
    zip_t *dev = zip[id];

    if (zip_drives[id].bus_type != ZIP_BUS_SCSI)
	ide_set_callback(zip_drives[id].ide_channel >> 1, dev->callback);
}


static void
zip_reset_cdb_len(int id)
{
    zip_t *dev = zip[id];

    dev->cdb_len = dev->cdb_len_setting ? 16 : 12;
}


void
zip_set_signature(int id)
{
    zip_t *dev = zip[id];

    if (id >= ZIP_NUM)
	return;
    dev->phase = 1;
    dev->request_length = 0xEB14;
}


void
zip_init(int id, int cdb_len_setting)
{
    zip_t *dev = zip[id];

    if (id >= ZIP_NUM)
	return;

    memset(dev, 0, sizeof(zip_t));
    dev->requested_blocks = 1;
    if (cdb_len_setting <= 1)
	dev->cdb_len_setting = cdb_len_setting;
    zip_reset_cdb_len(id);
    dev->sense[0] = 0xf0;
    dev->sense[7] = 10;
    zip_drives[id].bus_mode = 0;
    if (zip_drives[id].bus_type >= ZIP_BUS_ATAPI)
	zip_drives[id].bus_mode |= 2;
    if (zip_drives[id].bus_type < ZIP_BUS_SCSI)
	zip_drives[id].bus_mode |= 1;
    zip_log("ZIP %i: Bus type %i, bus mode %i\n", id, zip_drives[id].bus_type, zip_drives[id].bus_mode);
    if (zip_drives[id].bus_type < ZIP_BUS_SCSI)
	zip_set_signature(id);
    dev->status = READY_STAT | DSC_STAT;
    dev->pos = 0;
    dev->packet_status = 0xff;
    zip_sense_key = zip_asc = zip_ascq = dev->unit_attention = 0;
    dev->cdb_len_setting = 0;
    dev->cdb_len = 12;
}


static int
zip_supports_pio(int id)
{
    return (zip_drives[id].bus_mode & 1);
}


static int
zip_supports_dma(int id)
{
    return (zip_drives[id].bus_mode & 2);
}


/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
static int
zip_current_mode(int id)
{
    zip_t *dev = zip[id];

    if (!zip_supports_pio(id) && !zip_supports_dma(id))
	return 0;
    if (zip_supports_pio(id) && !zip_supports_dma(id)) {
	zip_log("ZIP %i: Drive does not support DMA, setting to PIO\n", id);
	return 1;
    }
    if (!zip_supports_pio(id) && zip_supports_dma(id))
	return 2;
    if (zip_supports_pio(id) && zip_supports_dma(id)) {
	zip_log("ZIP %i: Drive supports both, setting to %s\n", id, (dev->features & 1) ? "DMA" : "PIO", id);
	return (dev->features & 1) ? 2 : 1;
    }

    return 0;
}


/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int
zip_ZIP_PHASE_to_scsi(uint8_t id)
{
    zip_t *dev = zip[id];

    if (dev->status & ERR_STAT)
	return SCSI_STATUS_CHECK_CONDITION;
    else
	return SCSI_STATUS_OK;
}


/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int
zip_atapi_phase_to_scsi(uint8_t id)
{
    zip_t *dev = zip[id];

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
zip_mode_sense_load(uint8_t id)
{
    FILE *f;
    wchar_t file_name[512];
    int i;

    memset(&zip_mode_sense_pages_saved[id], 0, sizeof(mode_sense_pages_t));
    for (i = 0; i < 0x3f; i++) {
	if (zip_drives[id].is_250) {
		if (zip_250_mode_sense_pages_default.pages[i][1] != 0) {
			if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
				memcpy(zip_mode_sense_pages_saved[id].pages[i], zip_250_mode_sense_pages_default_scsi.pages[i], zip_250_mode_sense_pages_default_scsi.pages[i][1] + 2);
			else
				memcpy(zip_mode_sense_pages_saved[id].pages[i], zip_250_mode_sense_pages_default.pages[i], zip_250_mode_sense_pages_default.pages[i][1] + 2);
		}
	} else {
		if (zip_mode_sense_pages_default.pages[i][1] != 0) {
			if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
				memcpy(zip_mode_sense_pages_saved[id].pages[i], zip_mode_sense_pages_default_scsi.pages[i], zip_mode_sense_pages_default_scsi.pages[i][1] + 2);
			else
				memcpy(zip_mode_sense_pages_saved[id].pages[i], zip_mode_sense_pages_default.pages[i], zip_mode_sense_pages_default.pages[i][1] + 2);
		}
	}
    }
    memset(file_name, 0, 512 * sizeof(wchar_t));
    if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
	swprintf(file_name, 512, L"scsi_zip_%02i_mode_sense_bin", id);
    else
	swprintf(file_name, 512, L"zip_%02i_mode_sense_bin", id);
    f = plat_fopen(nvr_path(file_name), L"rb");
    if (f)
	fclose(f);
}


static void
zip_mode_sense_save(uint8_t id)
{
    FILE *f;
    wchar_t file_name[512];

    memset(file_name, 0, 512 * sizeof(wchar_t));
    if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
	swprintf(file_name, 512, L"scsi_zip_%02i_mode_sense_bin", id);
    else
	swprintf(file_name, 512, L"zip_%02i_mode_sense_bin", id);
    f = plat_fopen(nvr_path(file_name), L"wb");
    if (f)
	fclose(f);
}


int
zip_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
    int size = 0;

    if (zip_drives[id].is_250)
	size = zip_drives[id].medium_size - 1;	/* IMPORTANT: What's returned is the last LBA block. */
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
zip_mode_sense_read(uint8_t id, uint8_t page_control, uint8_t page, uint8_t pos)
{
    switch (page_control) {
	case 0:
	case 3:
		if (zip_drives[id].is_250 && (page == 5) && (pos == 9) && (zip_drives[id].medium_size == ZIP_SECTORS))
			return 0x60;
		return zip_mode_sense_pages_saved[id].pages[page][pos];
		break;
	case 1:
		if (zip_drives[id].is_250)
			return zip_250_mode_sense_pages_changeable.pages[page][pos];
		else
			return zip_mode_sense_pages_changeable.pages[page][pos];
		break;
	case 2:
		if (zip_drives[id].is_250) {
			if ((page == 5) && (pos == 9) && (zip_drives[id].medium_size == ZIP_SECTORS))
				return 0x60;
			if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
				return zip_250_mode_sense_pages_default_scsi.pages[page][pos];
			else
				return zip_250_mode_sense_pages_default.pages[page][pos];
		} else {
			if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
				return zip_mode_sense_pages_default_scsi.pages[page][pos];
			else
				return zip_mode_sense_pages_default.pages[page][pos];
		}
		break;
    }

    return 0;
}


static uint32_t
zip_mode_sense(uint8_t id, uint8_t *buf, uint32_t pos, uint8_t type, uint8_t block_descriptor_len)
{
    zip_t *dev = zip[id];

    uint64_t page_flags;
    uint8_t page_control = (type >> 6) & 3;

    if (zip_drives[id].is_250)
	page_flags = zip_250_mode_sense_page_flags;
    else
	page_flags = zip_mode_sense_page_flags;

    int i = 0;
    int j = 0;

    uint8_t msplen;

    type &= 0x3f;

    if (block_descriptor_len) {
	if (zip_drives[id].is_250) {
		buf[pos++] = ((zip_drives[id].medium_size >> 24) & 0xff);
		buf[pos++] = ((zip_drives[id].medium_size >> 16) & 0xff);
		buf[pos++] = ((zip_drives[id].medium_size >>  8) & 0xff);
		buf[pos++] = ( zip_drives[id].medium_size        & 0xff);
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
        if ((type == GPMODE_ALL_PAGES) || (type == i)) {
		if (page_flags & (1LL << dev->current_page_code)) {
			buf[pos++] = zip_mode_sense_read(id, page_control, i, 0);
			msplen = zip_mode_sense_read(id, page_control, i, 1);
			buf[pos++] = msplen;
			zip_log("ZIP %i: MODE SENSE: Page [%02X] length %i\n", id, i, msplen);
			for (j = 0; j < msplen; j++)
				buf[pos++] = zip_mode_sense_read(id, page_control, i, 2 + j);
		}
	}
    }

    return pos;
}


static void
zip_update_request_length(uint8_t id, int len, int block_len)
{
    zip_t *dev = zip[id];
    uint32_t bt, min_len = 0;

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

    return;
}


static void
zip_command_bus(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->status = BUSY_STAT;
    dev->phase = 1;
    dev->pos = 0;
    dev->callback = 1LL * ZIP_TIME;
    zip_set_callback(id);
}


static void
zip_command_common(uint8_t id)
{
    zip_t *dev = zip[id];

    double bytes_per_second, period;
    double dusec;

    dev->status = BUSY_STAT;
    dev->phase = 1;
    dev->pos = 0;
    if (dev->packet_status == ZIP_PHASE_COMPLETE) {
	zip_phase_callback(id);
	dev->callback = 0LL;
    } else {
	if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
		dev->callback = -1LL;	/* Speed depends on SCSI controller */
		return;
	} else {
		if (zip_current_mode(id) == 2)
			bytes_per_second = 66666666.666666666666666;	/* 66 MB/s MDMA-2 speed */
		else
			bytes_per_second =  8333333.333333333333333;	/* 8.3 MB/s PIO-2 speed */
	}

	period = 1000000.0 / bytes_per_second;
	dusec = (double) TIMER_USEC;
	dusec = dusec * period * (double) (dev->packet_len);
	dev->callback = ((int64_t) dusec);
    }

    zip_set_callback(id);
}


static void
zip_command_complete(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->packet_status = ZIP_PHASE_COMPLETE;
    zip_command_common(id);
}


static void
zip_command_read(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->packet_status = ZIP_PHASE_DATA_IN;
    zip_command_common(id);
    dev->total_read = 0;
}


static void
zip_command_read_dma(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->packet_status = ZIP_PHASE_DATA_IN_DMA;
    zip_command_common(id);
    dev->total_read = 0;
}

static void
zip_command_write(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->packet_status = ZIP_PHASE_DATA_OUT;
    zip_command_common(id);
}


static void
zip_command_write_dma(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->packet_status = ZIP_PHASE_DATA_OUT_DMA;
    zip_command_common(id);
}


/* id = Current ZIP device ID;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void
zip_data_command_finish(uint8_t id, int len, int block_len, int alloc_len, int direction)
{
    zip_t *dev = zip[id];

    zip_log("ZIP %i: Finishing command (%02X): %i, %i, %i, %i, %i\n", id, dev->current_cdb[0], len, block_len, alloc_len, direction, dev->request_length);
    dev->pos=0;
    if (alloc_len >= 0) {
	if (alloc_len < len)
		len = alloc_len;
    }
    if ((len == 0) || (zip_current_mode(id) == 0)) {
	if (zip_drives[id].bus_type != ZIP_BUS_SCSI)
		dev->packet_len = 0;

	zip_command_complete(id);
    } else {
	if (zip_current_mode(id) == 2) {
		if (zip_drives[id].bus_type != ZIP_BUS_SCSI)
			dev->packet_len = alloc_len;

		if (direction == 0)
			zip_command_read_dma(id);
		else
			zip_command_write_dma(id);
	} else {
		zip_update_request_length(id, len, block_len);
		if (direction == 0)
			zip_command_read(id);
		else
			zip_command_write(id);
	}
    }

    zip_log("ZIP %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n",
	    id, dev->packet_status, dev->request_length, dev->packet_len, dev->pos, dev->phase);
}


static void
zip_sense_clear(int id, int command)
{
    zip_t *dev = zip[id];

    dev->previous_command = command;
    zip_sense_key = zip_asc = zip_ascq = 0;
}


static void
zip_set_phase(uint8_t id, uint8_t phase)
{
    uint8_t scsi_id = zip_drives[id].scsi_device_id;
    uint8_t scsi_lun = zip_drives[id].scsi_device_lun;

    if (zip_drives[id].bus_type != ZIP_BUS_SCSI)
	return;

    SCSIDevices[scsi_id][scsi_lun].Phase = phase;
}


static void
zip_cmd_error(uint8_t id)
{
    zip_t *dev = zip[id];

    zip_set_phase(id, SCSI_PHASE_STATUS);
    dev->error = ((zip_sense_key & 0xf) << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = 0x80;
    dev->callback = 50LL * ZIP_TIME;
    zip_set_callback(id);
    zip_log("ZIP %i: [%02X] ERROR: %02X/%02X/%02X\n", id, dev->current_cdb[0], zip_sense_key, zip_asc, zip_ascq);
}


static void
zip_unit_attention(uint8_t id)
{
    zip_t *dev = zip[id];

    zip_set_phase(id, SCSI_PHASE_STATUS);
    dev->error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
    if (dev->unit_attention)
	dev->error |= MCR_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->pos = 0;
    dev->packet_status = 0x80;
    dev->callback = 50LL * ZIP_TIME;
    zip_set_callback(id);
    zip_log("ZIP %i: UNIT ATTENTION\n", id);
}


static void
zip_bus_master_error(uint8_t id)
{
    zip_sense_key = zip_asc = zip_ascq = 0;
    zip_cmd_error(id);
}


static void
zip_not_ready(uint8_t id)
{
    zip_sense_key = SENSE_NOT_READY;
    zip_asc = ASC_MEDIUM_NOT_PRESENT;
    zip_ascq = 0;
    zip_cmd_error(id);
}


static void
zip_write_protected(uint8_t id)
{
    zip_sense_key = SENSE_UNIT_ATTENTION;
    zip_asc = ASC_WRITE_PROTECTED;
    zip_ascq = 0;
    zip_cmd_error(id);
}


static void
zip_invalid_lun(uint8_t id)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_INV_LUN;
    zip_ascq = 0;
    zip_cmd_error(id);
}


static void
zip_illegal_opcode(uint8_t id)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_ILLEGAL_OPCODE;
    zip_ascq = 0;
    zip_cmd_error(id);
}


static void
zip_lba_out_of_range(uint8_t id)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_LBA_OUT_OF_RANGE;
    zip_ascq = 0;
    zip_cmd_error(id);
}


static void
zip_invalid_field(uint8_t id)
{
    zip_t *dev = zip[id];

    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_INV_FIELD_IN_CMD_PACKET;
    zip_ascq = 0;
    zip_cmd_error(id);
    dev->status = 0x53;
}


static void
zip_invalid_field_pl(uint8_t id)
{
    zip_t *dev = zip[id];

    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
    zip_ascq = 0;
    zip_cmd_error(id);
    dev->status = 0x53;
}


static void
zip_data_phase_error(uint8_t id)
{
    zip_sense_key = SENSE_ILLEGAL_REQUEST;
    zip_asc = ASC_DATA_PHASE_ERROR;
    zip_ascq = 0;
    zip_cmd_error(id);
}


static int
zip_blocks(uint8_t id, uint32_t *len, int first_batch, int out)
{
    zip_t *dev = zip[id];

    dev->data_pos = 0;

    *len = 0;

    if (!dev->sector_len) {
	zip_command_complete(id);
	return -1;
    }

    zip_log("%sing %i blocks starting from %i...\n", out ? "Writ" : "Read", dev->requested_blocks, dev->sector_pos);

    if (dev->sector_pos >= zip_drives[id].medium_size) {
	zip_log("ZIP %i: Trying to %s beyond the end of disk\n", id, out ? "write" : "read");
	zip_lba_out_of_range(id);
	return 0;
    }

    *len = dev->requested_blocks << 9;

    fseek(zip_drives[id].f, zip_drives[id].base + (dev->sector_pos << 9), SEEK_SET);
    if (out)
	fwrite(zipbufferb, 1, *len, zip_drives[id].f);
    else
	fread(zipbufferb, 1, *len, zip_drives[id].f);

    zip_log("%s %i bytes of blocks...\n", out ? "Written" : "Read", *len);

    dev->sector_pos += dev->requested_blocks;
    dev->sector_len -= dev->requested_blocks;

    return 1;
}


void
zip_insert(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->unit_attention = 1;
}


/*SCSI Sense Initialization*/
void
zip_sense_code_ok(uint8_t id)
{	
    zip_sense_key = SENSE_NONE;
    zip_asc = 0;
    zip_ascq = 0;
}


static int
zip_pre_execution_check(uint8_t id, uint8_t *cdb)
{
    zip_t *dev = zip[id];
    int ready = 0;

    if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
	if (((dev->request_length >> 5) & 7) != zip_drives[id].scsi_device_lun) {
		zip_log("ZIP %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", id, ((dev->request_length >> 5) & 7));
		zip_invalid_lun(id);
		return 0;
	}
    }

    if (!(zip_command_flags[cdb[0]] & IMPLEMENTED)) {
	zip_log("ZIP %i: Attempting to execute unknown command %02X over %s\n", id, cdb[0],
		(zip_drives[id].bus_type == ZIP_BUS_SCSI) ? "SCSI" : "ATAPI");

	zip_illegal_opcode(id);
	return 0;
    }

    if ((zip_drives[id].bus_type < ZIP_BUS_SCSI) && (zip_command_flags[cdb[0]] & SCSI_ONLY)) {
	zip_log("ZIP %i: Attempting to execute SCSI-only command %02X over ATAPI\n", id, cdb[0]);
	zip_illegal_opcode(id);
	return 0;
    }

    if ((zip_drives[id].bus_type == ZIP_BUS_SCSI) && (zip_command_flags[cdb[0]] & ATAPI_ONLY)) {
	zip_log("ZIP %i: Attempting to execute ATAPI-only command %02X over SCSI\n", id, cdb[0]);
	zip_illegal_opcode(id);
	return 0;
    }

    ready = (zip_drives[id].f != NULL);

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
		/* zip_log("ZIP %i: Unit attention now 2\n", id); */
		dev->unit_attention = 2;
		zip_log("ZIP %i: UNIT ATTENTION: Command %02X not allowed to pass through\n", id, cdb[0]);
		zip_unit_attention(id);
		return 0;
	}
    } else if (dev->unit_attention == 2) {
	if (cdb[0] != GPCMD_REQUEST_SENSE) {
		/* zip_log("ZIP %i: Unit attention now 0\n", id); */
		dev->unit_attention = 0;
	}
    }

    /* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
       the UNIT ATTENTION condition if it's set. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
	zip_sense_clear(id, cdb[0]);

    /* Next it's time for NOT READY. */
    if (!ready)
	dev->media_status = MEC_MEDIA_REMOVAL;
    else
	dev->media_status = (dev->unit_attention) ? MEC_NEW_MEDIA : MEC_NO_CHANGE;

    if ((zip_command_flags[cdb[0]] & CHECK_READY) && !ready) {
	zip_log("ZIP %i: Not ready (%02X)\n", id, cdb[0]);
	zip_not_ready(id);
	return 0;
    }

    zip_log("ZIP %i: Continuing with command %02X\n", id, cdb[0]);

    return 1;
}


static void
zip_seek(uint8_t id, uint32_t pos)
{
    zip_t *dev = zip[id];

    /* zip_log("ZIP %i: Seek %08X\n", id, pos); */
    dev->sector_pos   = pos;
}


static void
zip_rezero(uint8_t id)
{
    zip_t *dev = zip[id];

    dev->sector_pos = dev->sector_len = 0;
    zip_seek(id, 0);
}


void
zip_reset(uint8_t id)
{
    zip_t *dev = zip[id];

    zip_rezero(id);
    dev->status = 0;
    dev->callback = 0LL;
    zip_set_callback(id);
    dev->packet_status = 0xff;
    dev->unit_attention = 0;
}


static void
zip_request_sense(uint8_t id, uint8_t *buffer, uint8_t alloc_length, int desc)
{				
    zip_t *dev = zip[id];

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
	buffer[desc ? 1 : 2]=SENSE_UNIT_ATTENTION;
	buffer[desc ? 2 : 12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
	buffer[desc ? 3 : 13]=0;
    }

    zip_log("ZIP %i: Reporting sense: %02X %02X %02X\n", id, buffer[2], buffer[12], buffer[13]);

    if (buffer[desc ? 1 : 2] == SENSE_UNIT_ATTENTION) {
	/* If the last remaining sense is unit attention, clear
	   that condition. */
	dev->unit_attention = 0;
    }

    /* Clear the sense stuff as per the spec. */
    zip_sense_clear(id, GPCMD_REQUEST_SENSE);
}


void
zip_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{
    zip_t *dev = zip[id];
    int ready = 0;

    ready = (zip_drives[id].f != NULL);

    if (!ready && dev->unit_attention) {
	/* If the drive is not ready, there is no reason to keep the
	   UNIT ATTENTION condition present, as we only use it to mark
	   disc changes. */
	dev->unit_attention = 0;
    }

    /* Do *NOT* advance the unit attention phase. */

    zip_request_sense(id, buffer, alloc_length, 0);
}


static void
zip_set_buf_len(uint8_t id, int32_t *BufLen, uint32_t *src_len)
{
    if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
	if (*BufLen == -1)
		*BufLen = *src_len;
	else {
		*BufLen = MIN(*src_len, *BufLen);
		*src_len = *BufLen;
	}
	zip_log("ZIP %i: Actual transfer length: %i\n", id, *BufLen);
    }
}


static void
zip_buf_alloc(uint8_t id, uint32_t len)
{
    zip_t *dev = zip[id];

    zip_log("ZIP %i: Allocated buffer length: %i\n", id, len);
    zipbufferb = (uint8_t *) malloc(len);
}


static void
zip_buf_free(uint8_t id)
{
    zip_t *dev = zip[id];

    if (zipbufferb) {
	zip_log("ZIP %i: Freeing buffer...\n", id);
	free(zipbufferb);
	zipbufferb = NULL;
    }
}


void
zip_command(uint8_t id, uint8_t *cdb)
{
    int pos = 0, block_desc = 0;
    int ret;
    uint32_t len, max_len;
    uint32_t alloc_length, i = 0;
    unsigned size_idx, idx = 0;
    unsigned preamble_len;
    int32_t blen = 0;
    int32_t *BufLen;

    zip_t *dev = zip[id];

    if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
	BufLen = &SCSIDevices[zip_drives[id].scsi_device_id][zip_drives[id].scsi_device_lun].BufferLength;
	dev->status &= ~ERR_STAT;
    } else {
	BufLen = &blen;
	dev->error = 0;
    }

    dev->packet_len = 0;
    dev->request_pos = 0;

    dev->data_pos = 0;

    memcpy(dev->current_cdb, cdb, dev->cdb_len);

    if (cdb[0] != 0) {
	zip_log("ZIP %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, Unit attention: %i\n", id, cdb[0], zip_sense_key, zip_asc, zip_ascq, dev->unit_attention);
	zip_log("ZIP %i: Request length: %04X\n", id, dev->request_length);

	zip_log("ZIP %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", id,
		cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
		cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    zip_set_phase(id, SCSI_PHASE_STATUS);

    /* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
    if (zip_pre_execution_check(id, cdb) == 0)
	return;

    switch (cdb[0]) {
	case GPCMD_SEND_DIAGNOSTIC:
		if (!(cdb[1] & (1 << 2))) {
			zip_invalid_field(id);
			return;
		}
	case GPCMD_SCSI_RESERVE:
	case GPCMD_SCSI_RELEASE:
	case GPCMD_TEST_UNIT_READY:
		zip_set_phase(id, SCSI_PHASE_STATUS);
		zip_command_complete(id);
		break;

	case GPCMD_FORMAT_UNIT:
		if ((zip_drives[id].bus_type == ZIP_BUS_SCSI) && zip_drives[id].read_only)
		{
			zip_write_protected(id);
			return;
		}

		zip_set_phase(id, SCSI_PHASE_STATUS);
		zip_command_complete(id);
		break;

	case GPCMD_IOMEGA_SENSE:
		zip_set_phase(id, SCSI_PHASE_DATA_IN);
		max_len = cdb[4];
		zip_buf_alloc(id, 256);
		zip_set_buf_len(id, BufLen, &max_len);
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
			if (zip_drives[id].read_only)
				zipbufferb[0x15] |= 0x02;
			for (i = 0x00; i < 0x27; i++)
				zipbufferb[i + 0x16] = 0x00;
		} else {
			zip_invalid_field(id);
			zip_buf_free(id);
			return;
		}
		zip_data_command_finish(id, 18, 18, cdb[4], 0);
		break;

	case GPCMD_REZERO_UNIT:
		dev->sector_pos = dev->sector_len = 0;
		zip_seek(id, 0);
		zip_set_phase(id, SCSI_PHASE_STATUS);
		break;

	case GPCMD_REQUEST_SENSE:
		/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
		   should forget about the not ready, and report unit attention straight away. */
		zip_set_phase(id, SCSI_PHASE_DATA_IN);
		max_len = cdb[4];
		zip_buf_alloc(id, 256);
		zip_set_buf_len(id, BufLen, &max_len);
		len = (cdb[1] & 1) ? 8 : 18;
		zip_request_sense(id, zipbufferb, max_len, cdb[1] & 1);
		zip_data_command_finish(id, len, len, cdb[4], 0);
		break;

	case GPCMD_MECHANISM_STATUS:
		zip_set_phase(id, SCSI_PHASE_DATA_IN);
		len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

		zip_buf_alloc(id, 8);

		zip_set_buf_len(id, BufLen, &len);

		memset(zipbufferb, 0, 8);
		zipbufferb[5] = 1;

		zip_data_command_finish(id, 8, 8, len, 0);
		break;

	case GPCMD_READ_6:
	case GPCMD_READ_10:
	case GPCMD_READ_12:
		zip_set_phase(id, SCSI_PHASE_DATA_IN);
		alloc_length = 512;

		switch(cdb[0]) {
			case GPCMD_READ_6:
				dev->sector_len = cdb[4];
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				zip_log("ZIP %i: Length: %i, LBA: %i\n", id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_READ_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				zip_log("ZIP %i: Length: %i, LBA: %i\n", id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_READ_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		if (!dev->sector_len) {
			zip_set_phase(id, SCSI_PHASE_STATUS);
			/* zip_log("ZIP %i: All done - callback set\n", id); */
			dev->packet_status = ZIP_PHASE_COMPLETE;
			dev->callback = 20LL * ZIP_TIME;
			zip_set_callback(id);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're reading all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		zip_buf_alloc(id, dev->packet_len);

		ret = zip_blocks(id, &alloc_length, 1, 0);
		if (ret <= 0) {
			zip_buf_free(id);
			return;
		}

		dev->requested_blocks = max_len;
		dev->packet_len = alloc_length;

		zip_set_buf_len(id, BufLen, &dev->packet_len);

		zip_data_command_finish(id, alloc_length, 512, alloc_length, 0);

		dev->all_blocks_total = dev->block_total;
		if (dev->packet_status != ZIP_PHASE_COMPLETE)
			ui_sb_update_icon(SB_ZIP | id, 1);
		else
			ui_sb_update_icon(SB_ZIP | id, 0);
		return;

	case GPCMD_VERIFY_6:
	case GPCMD_VERIFY_10:
	case GPCMD_VERIFY_12:
		if (!(cdb[1] & 2)) {
			zip_set_phase(id, SCSI_PHASE_STATUS);
			zip_command_complete(id);
			break;
		}
	case GPCMD_WRITE_6:
	case GPCMD_WRITE_10:
	case GPCMD_WRITE_AND_VERIFY_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_12:
		zip_set_phase(id, SCSI_PHASE_DATA_OUT);
		alloc_length = 512;

		if ((zip_drives[id].bus_type == ZIP_BUS_SCSI) && zip_drives[id].read_only) {
			zip_write_protected(id);
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
				zip_log("ZIP %i: Length: %i, LBA: %i\n", id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_VERIFY_12:
			case GPCMD_WRITE_12:
			case GPCMD_WRITE_AND_VERIFY_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		if (zip_drives[id].is_250) {
			if ((dev->sector_pos >= zip_drives[id].medium_size) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= zip_drives[id].medium_size)) {
				zip_lba_out_of_range(id);
				return;
			}
		} else {
			if ((dev->sector_pos >= ZIP_SECTORS) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= ZIP_SECTORS)) {
				zip_lba_out_of_range(id);
				return;
			}
		}

		if (!dev->sector_len) {
			zip_set_phase(id, SCSI_PHASE_STATUS);
			/* zip_log("ZIP %i: All done - callback set\n", id); */
			dev->packet_status = ZIP_PHASE_COMPLETE;
			dev->callback = 20LL * ZIP_TIME;
			zip_set_callback(id);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're writing all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		zip_buf_alloc(id, dev->packet_len);

		dev->requested_blocks = max_len;
		dev->packet_len = max_len << 9;

		zip_set_buf_len(id, BufLen, &dev->packet_len);

		zip_data_command_finish(id, dev->packet_len, 512, dev->packet_len, 1);

		dev->all_blocks_total = dev->block_total;
		if (dev->packet_status != ZIP_PHASE_COMPLETE)
			ui_sb_update_icon(SB_ZIP | id, 1);
		else
			ui_sb_update_icon(SB_ZIP | id, 0);
		return;

	case GPCMD_WRITE_SAME_10:
		zip_set_phase(id, SCSI_PHASE_DATA_OUT);
		alloc_length = 512;

		if ((cdb[1] & 6) == 6) {
			zip_invalid_field(id);
			return;
		}

		if ((zip_drives[id].bus_type == ZIP_BUS_SCSI) && zip_drives[id].read_only) {
			zip_write_protected(id);
			return;
		}

		dev->sector_len = (cdb[7] << 8) | cdb[8];
		dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

		if (zip_drives[id].is_250) {
			if ((dev->sector_pos >= zip_drives[id].medium_size) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= zip_drives[id].medium_size)) {
				zip_lba_out_of_range(id);
				return;
			}
		} else {
			if ((dev->sector_pos >= ZIP_SECTORS) ||
			    ((dev->sector_pos + dev->sector_len - 1) >= ZIP_SECTORS)) {
				zip_lba_out_of_range(id);
				return;
			}
		}

		if (!dev->sector_len) {
			zip_set_phase(id, SCSI_PHASE_STATUS);
			/* zip_log("ZIP %i: All done - callback set\n", id); */
			dev->packet_status = ZIP_PHASE_COMPLETE;
			dev->callback = 20LL * ZIP_TIME;
			zip_set_callback(id);
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;	/* If we're writing all blocks in one go for DMA, why not also for PIO, it should NOT
							   matter anyway, this step should be identical and only the way the read dat is
							   transferred to the host should be different. */

		dev->packet_len = max_len * alloc_length;
		zip_buf_alloc(id, dev->packet_len);

		dev->requested_blocks = max_len;
		dev->packet_len = alloc_length;

		zip_set_buf_len(id, BufLen, &dev->packet_len);

		zip_data_command_finish(id, dev->packet_len, 512, dev->packet_len, 1);

		dev->all_blocks_total = dev->block_total;
		if (dev->packet_status != ZIP_PHASE_COMPLETE)
			ui_sb_update_icon(SB_ZIP | id, 1);
		else
			ui_sb_update_icon(SB_ZIP | id, 0);
		return;

	case GPCMD_MODE_SENSE_6:
	case GPCMD_MODE_SENSE_10:
		zip_set_phase(id, SCSI_PHASE_DATA_IN);

		if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
			block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
		else
			block_desc = 0;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = cdb[4];
			zip_buf_alloc(id, 256);
		} else {
			len = (cdb[8] | (cdb[7] << 8));
			zip_buf_alloc(id, 65536);
		}

		dev->current_page_code = cdb[2] & 0x3F;
		zip_log("Mode sense page: %02X\n", dev->current_page_code);

		if (!(zip_mode_sense_page_flags & (1LL << dev->current_page_code))) {
			zip_invalid_field(id);
			zip_buf_free(id);
			return;
		}

		memset(zipbufferb, 0, len);
		alloc_length = len;

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = zip_mode_sense(id, zipbufferb, 4, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			zipbufferb[0] = len - 1;
			zipbufferb[1] = 0;
			if (block_desc)
				zipbufferb[3] = 8;
		} else {
			len = zip_mode_sense(id, zipbufferb, 8, cdb[2], block_desc);
			len = MIN(len, alloc_length);
			zipbufferb[0]=(len - 2) >> 8;
			zipbufferb[1]=(len - 2) & 255;
			zipbufferb[2] = 0;
			if (block_desc) {
				zipbufferb[6] = 0;
				zipbufferb[7] = 8;
			}
		}

		zip_set_buf_len(id, BufLen, &len);

		zip_log("ZIP %i: Reading mode page: %02X...\n", id, cdb[2]);

		zip_data_command_finish(id, len, len, alloc_length, 0);
		return;

	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		zip_set_phase(id, SCSI_PHASE_DATA_OUT);

		if (cdb[0] == GPCMD_MODE_SELECT_6) {
			len = cdb[4];
			zip_buf_alloc(id, 256);
		} else {
			len = (cdb[7] << 8) | cdb[8];
			zip_buf_alloc(id, 65536);
		}

		zip_set_buf_len(id, BufLen, &len);

		dev->total_length = len;
		dev->do_page_save = cdb[1] & 1;

		dev->current_page_pos = 0;

		zip_data_command_finish(id, len, len, len, 1);
		return;

	case GPCMD_START_STOP_UNIT:
		zip_set_phase(id, SCSI_PHASE_STATUS);

		switch(cdb[4] & 3) {
			case 0:		/* Stop the disc. */
				zip_eject(id);	/* The Iomega Windows 9x drivers require this. */
				break;
			case 1:		/* Start the disc and read the TOC. */
				break;
			case 2:		/* Eject the disc if possible. */
				/* zip_eject(id); */
				break;
			case 3:		/* Load the disc (close tray). */
				zip_reload(id);
				break;
		}

		zip_command_complete(id);
		break;

	case GPCMD_INQUIRY:
		zip_set_phase(id, SCSI_PHASE_DATA_IN);

		max_len = cdb[3];
		max_len <<= 8;
		max_len |= cdb[4];

		zip_buf_alloc(id, 65536);

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
						zip_data_phase_error(id);
						zip_buf_free(id);
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
					if (zip_drives[id].is_250)
						ide_padstr8(zipbufferb + idx, 40, "ZIP 250         "); /* Product */
					else
						ide_padstr8(zipbufferb + idx, 40, "ZIP 100         "); /* Product */
					idx += 40;
					ide_padstr8(zipbufferb + idx, 20, "53R141"); /* Product */
					idx += 20;
					break;
				default:
					zip_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
					zip_invalid_field(id);
					zip_buf_free(id);
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
			if (zip_drives[id].is_250) {
				zipbufferb[2] = (zip_drives[id].bus_type == ZIP_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
				zipbufferb[3] = (zip_drives[id].bus_type == ZIP_BUS_SCSI) ? 0x02 : 0x21;
			} else {
				zipbufferb[2] = (zip_drives[id].bus_type == ZIP_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
				zipbufferb[3] = (zip_drives[id].bus_type == ZIP_BUS_SCSI) ? 0x02 : 0x21;
			}
			zipbufferb[4] = 31;
			if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
				zipbufferb[6] = 1;	/* 16-bit transfers supported */
				zipbufferb[7] = 0x20;	/* Wide bus supported */
			}

			ide_padstr8(zipbufferb + 8, 8, "IOMEGA  "); /* Vendor */
			if (zip_drives[id].is_250) {
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
		zip_set_buf_len(id, BufLen, &len);

		zip_data_command_finish(id, len, len, max_len, 0);
		break;

	case GPCMD_PREVENT_REMOVAL:
		zip_set_phase(id, SCSI_PHASE_STATUS);
		zip_command_complete(id);
		break;

	case GPCMD_SEEK_6:
	case GPCMD_SEEK_10:
		zip_set_phase(id, SCSI_PHASE_STATUS);

		switch(cdb[0]) {
			case GPCMD_SEEK_6:
				pos = (cdb[2] << 8) | cdb[3];
				break;
			case GPCMD_SEEK_10:
				pos = (cdb[2] << 24) | (cdb[3]<<16) | (cdb[4]<<8) | cdb[5];
				break;
		}
		zip_seek(id, pos);
		zip_command_complete(id);
		break;

	case GPCMD_READ_CDROM_CAPACITY:
		zip_set_phase(id, SCSI_PHASE_DATA_IN);

		zip_buf_alloc(id, 8);

		if (zip_read_capacity(id, dev->current_cdb, zipbufferb, &len) == 0) {
			zip_buf_free(id);
			return;
		}

		zip_set_buf_len(id, BufLen, &len);

		zip_data_command_finish(id, len, len, len, 0);
		break;

	case GPCMD_IOMEGA_EJECT:
		zip_set_phase(id, SCSI_PHASE_STATUS);
		zip_eject(id);
		zip_command_complete(id);
		break;

	case GPCMD_READ_FORMAT_CAPACITIES:
		len = (cdb[7] << 8) | cdb[8];

		zip_buf_alloc(id, len);
		memset(zipbufferb, 0, len);

		pos = 0;

		/* List header */
		zipbufferb[pos++] = 0;
		zipbufferb[pos++] = 0;
		zipbufferb[pos++] = 0;
		if (zip_drives[id].f != NULL)
			zipbufferb[pos++] = 16;
		else
			zipbufferb[pos++] = 8;

		/* Current/Maximum capacity header */
		if (zip_drives[id].is_250) {
			if (zip_drives[id].f != NULL) {
				zipbufferb[pos++] = (zip_drives[id].medium_size >> 24) & 0xff;
				zipbufferb[pos++] = (zip_drives[id].medium_size >> 16) & 0xff;
				zipbufferb[pos++] = (zip_drives[id].medium_size >> 8)  & 0xff;
				zipbufferb[pos++] =  zip_drives[id].medium_size        & 0xff;
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
			if (zip_drives[id].f != NULL)
				zipbufferb[pos++] = 2;
			else
				zipbufferb[pos++] = 3;
		}

		zipbufferb[pos++] = 512 >> 16;
		zipbufferb[pos++] = 512 >> 8;
		zipbufferb[pos++] = 512 & 0xff;

		if (zip_drives[id].f != NULL) {
			/* Formattable capacity descriptor */
			zipbufferb[pos++] = (zip_drives[id].medium_size >> 24) & 0xff;
			zipbufferb[pos++] = (zip_drives[id].medium_size >> 16) & 0xff;
			zipbufferb[pos++] = (zip_drives[id].medium_size >> 8)  & 0xff;
			zipbufferb[pos++] =  zip_drives[id].medium_size        & 0xff;
			zipbufferb[pos++] = 0;
			zipbufferb[pos++] = 512 >> 16;
			zipbufferb[pos++] = 512 >> 8;
			zipbufferb[pos++] = 512 & 0xff;
		}

		zip_set_buf_len(id, BufLen, &len);

		zip_data_command_finish(id, len, len, len, 0);
		break;

	default:
		zip_illegal_opcode(id);
		break;
    }

    /* zip_log("ZIP %i: Phase: %02X, request length: %i\n", dev->phase, dev->request_length); */

    if (zip_atapi_phase_to_scsi(id) == SCSI_PHASE_STATUS)
	zip_buf_free(id);
}


/* The command second phase function, needed for Mode Select. */
static uint8_t
zip_phase_data_out(uint8_t id)
{
    zip_t *dev = zip[id];

    uint16_t block_desc_len;
    uint16_t pos;

    uint8_t error = 0;
    uint8_t page, page_len;

    uint16_t i = 0;

    uint8_t hdr_len, val, old_val, ch;

    uint32_t last_to_write = 0, len = 0;
    uint32_t c, h, s;

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
			zip_blocks(id, &len, 1, 1);
		break;
	case GPCMD_WRITE_SAME_10:
		if (!dev->current_cdb[7] && !dev->current_cdb[8]) {
			if (zip_drives[id].is_250)
				last_to_write = (zip_drives[id].medium_size - 1);
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
			fseek(zip_drives[id].f, zip_drives[id].base + (i << 9), SEEK_SET);
			fwrite(zipbufferb, 1, 512, zip_drives[id].f);
		}
		break;
	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10)
			hdr_len = 8;
		else
			hdr_len = 4;

		if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
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
					old_val = zip_mode_sense_pages_saved[id].pages[page][i + 2];
					if (val != old_val) {
						if (ch)
							zip_mode_sense_pages_saved[id].pages[page][i + 2] = val;
						else
							error |= 1;
					}
				}
			}

			pos += page_len;

			if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
				val = zip_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
			else
				val = zip_mode_sense_pages_default.pages[page][0] & 0x80;
			if (dev->do_page_save && val)
				zip_mode_sense_save(id);

			if (pos >= dev->total_length)
				break;
		}

		if (error) {
			zip_invalid_field_pl(id);
			return 0;
		}
		break;
    }

    return 1;
}


/* This is the general ATAPI PIO request function. */
static void
zip_pio_request(uint8_t id, uint8_t out)
{
    zip_t *dev = zip[id];

    int old_pos = 0;
    int ret = 0;

    if (zip_drives[id].bus_type < ZIP_BUS_SCSI) {
	zip_log("ZIP %i: Lowering IDE IRQ\n", id);
	ide_irq_lower(ide_drives[zip_drives[id].ide_channel]);
    }

    dev->status = BUSY_STAT;

    if (dev->pos >= dev->packet_len) {
	zip_log("ZIP %i: %i bytes %s, command done\n", id, dev->pos, out ? "written" : "read");

	dev->pos = dev->request_pos = 0;
	if (out) {
		ret = zip_phase_data_out(id);
		/* If ret = 0 (phase 1 error), then we do not do anything else other than
		   free the buffer, as the phase and callback have already been set by the
		   error function. */
		if (ret)
			zip_command_complete(id);
	} else
		zip_command_complete(id);
	zip_buf_free(id);
    } else {
	zip_log("ZIP %i: %i bytes %s, %i bytes are still left\n", id, dev->pos, out ? "written" : "read", dev->packet_len - dev->pos);

	/* If less than (packet length) bytes are remaining, update packet length
	   accordingly. */
	if ((dev->packet_len - dev->pos) < (dev->max_transfer_len))
		dev->max_transfer_len = dev->packet_len - dev->pos;
	zip_log("ZIP %i: Packet length %i, request length %i\n", id, dev->packet_len, dev->max_transfer_len);

	old_pos = dev->pos;
	dev->packet_status = out ? ZIP_PHASE_DATA_OUT : ZIP_PHASE_DATA_IN;
	zip_command_common(id);
	dev->pos = old_pos;

	dev->request_pos = 0;
    }
}


static int
zip_read_from_ide_dma(uint8_t channel)
{
    zip_t *dev;

    uint8_t id = atapi_zip_drives[channel];
    int ret;

    if (id > ZIP_NUM)
	return 0;

    dev = zip[id];

    if (ide_bus_master_write) {
	ret = ide_bus_master_write(channel >> 1,
				   zipbufferb, dev->packet_len,
				   ide_bus_master_priv[channel >> 1]);
	if (ret == 2)		/* DMA not enabled, wait for it to be enabled. */
		return 2;
	else if (ret == 1) {	/* DMA error. */		
		zip_bus_master_error(id);
		return 0;
	} else
		return 1;
    } else
	return 0;
}


static int
zip_read_from_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
    zip_t *dev;

    uint8_t id = scsi_zip_drives[scsi_id][scsi_lun];
    int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

    if (id > ZIP_NUM)
	return 0;

    dev = zip[id];

    zip_log("Reading from SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
    memcpy(zipbufferb, SCSIDevices[scsi_id][scsi_lun].CmdBuffer, *BufLen);
    return 1;
}


static void
zip_irq_raise(uint8_t id)
{
    if (zip_drives[id].bus_type < ZIP_BUS_SCSI)
	ide_irq_raise(ide_drives[zip_drives[id].ide_channel]);
}


static int
zip_read_from_dma(uint8_t id)
{
    zip_t *dev = zip[id];

    int32_t *BufLen = &SCSIDevices[zip_drives[id].scsi_device_id][zip_drives[id].scsi_device_lun].BufferLength;
    int ret = 0;

    int in_data_length = 0;

    if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
	ret = zip_read_from_scsi_dma(zip_drives[id].scsi_device_id, zip_drives[id].scsi_device_lun);
    else
	ret = zip_read_from_ide_dma(zip_drives[id].ide_channel);

    if (ret != 1)
	return ret;

    if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
	in_data_length = *BufLen;
	zip_log("ZIP %i: SCSI Input data length: %i\n", id, in_data_length);
    } else {
	in_data_length = dev->max_transfer_len;
	zip_log("ZIP %i: ATAPI Input data length: %i\n", id, in_data_length);
    }

    ret = zip_phase_data_out(id);

    if (ret)
	return 1;
    else
	return 0;
}


static int
zip_write_to_ide_dma(uint8_t channel)
{
    zip_t *dev;

    uint8_t id = atapi_zip_drives[channel];
    int ret;

    if (id > ZIP_NUM) {
	zip_log("ZIP %i: Drive not found\n", id);
	return 0;
    }

    dev = zip[id];

    if (ide_bus_master_read) {
	ret = ide_bus_master_read(channel >> 1,
				  zipbufferb, dev->packet_len,
				  ide_bus_master_priv[channel >> 1]);
	if (ret == 2)		/* DMA not enabled, wait for it to be enabled. */
		return 2;
	else if (ret == 1) {	/* DMA error. */		
		zip_bus_master_error(id);
		return 0;
	} else
		return 1;
    } else
	return 0;
}


static int
zip_write_to_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
    zip_t *dev;

    uint8_t id = scsi_zip_drives[scsi_id][scsi_lun];
    int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

    if (id > ZIP_NUM)
	return 0;

    dev = zip[id];

    zip_log("Writing to SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
    memcpy(SCSIDevices[scsi_id][scsi_lun].CmdBuffer, zipbufferb, *BufLen);
    zip_log("ZIP %i: Data from CD buffer:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, zipbufferb[0], zipbufferb[1], zipbufferb[2], zipbufferb[3], zipbufferb[4], zipbufferb[5], zipbufferb[6], zipbufferb[7]);
    zip_log("ZIP %i: Data from SCSI DMA :  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, SCSIDevices[scsi_id][scsi_lun].CmdBuffer[0], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[1], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[2], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[3], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[4], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[5], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[6], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[7]);
    return 1;
}


static int
zip_write_to_dma(uint8_t id)
{
    zip_t *dev = zip[id];

    int32_t *BufLen = &SCSIDevices[zip_drives[id].scsi_device_id][zip_drives[id].scsi_device_lun].BufferLength;
    int ret = 0;

    if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
	zip_log("Write to SCSI DMA: (%02X:%02X)\n", zip_drives[id].scsi_device_id, zip_drives[id].scsi_device_lun);
	ret = zip_write_to_scsi_dma(zip_drives[id].scsi_device_id, zip_drives[id].scsi_device_lun);
    } else
	ret = zip_write_to_ide_dma(zip_drives[id].ide_channel);

    if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
	zip_log("ZIP %i: SCSI Output data length: %i\n", id, *BufLen);
    else
	zip_log("ZIP %i: ATAPI Output data length: %i\n", id, dev->packet_len);

    return ret;
}


/* If the result is 1, issue an IRQ, otherwise not. */
void
zip_phase_callback(uint8_t id)
{
    zip_t *dev = zip[id];
    int ret;

    switch(dev->packet_status) {
	case ZIP_PHASE_IDLE:
		zip_log("ZIP %i: ZIP_PHASE_IDLE\n", id);
		dev->pos=0;
		dev->phase = 1;
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		return;
	case ZIP_PHASE_COMMAND:
		zip_log("ZIP %i: ZIP_PHASE_COMMAND\n", id);
		dev->status = BUSY_STAT | (dev->status &ERR_STAT);
		memcpy(dev->atapi_cdb, zipbufferb, dev->cdb_len);
		zip_command(id, dev->atapi_cdb);
		return;
	case ZIP_PHASE_COMPLETE:
		zip_log("ZIP %i: ZIP_PHASE_COMPLETE\n", id);
		dev->status = READY_STAT;
		dev->phase = 3;
		dev->packet_status = 0xFF;
		ui_sb_update_icon(SB_ZIP | id, 0);
		zip_irq_raise(id);
		return;
	case ZIP_PHASE_DATA_OUT:
		zip_log("ZIP %i: ZIP_PHASE_DATA_OUT\n", id);
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		dev->phase = 0;
		zip_irq_raise(id);
		return;
	case ZIP_PHASE_DATA_OUT_DMA:
		zip_log("ZIP %i: ZIP_PHASE_DATA_OUT_DMA\n", id);
		ret = zip_read_from_dma(id);

		if ((ret == 1) || (zip_drives[id].bus_type == ZIP_BUS_SCSI)) {
			zip_log("ZIP %i: DMA data out phase done\n");
			zip_buf_free(id);
			zip_command_complete(id);
		} else if (ret == 2) {
			zip_log("ZIP %i: DMA out not enabled, wait\n");
			zip_command_bus(id);
		} else {
			zip_log("ZIP %i: DMA data out phase failure\n");
			zip_buf_free(id);
		}
		return;
	case ZIP_PHASE_DATA_IN:
		zip_log("ZIP %i: ZIP_PHASE_DATA_IN\n", id);
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		dev->phase = 2;
		zip_irq_raise(id);
		return;
	case ZIP_PHASE_DATA_IN_DMA:
		zip_log("ZIP %i: ZIP_PHASE_DATA_IN_DMA\n", id);
		ret = zip_write_to_dma(id);

		if ((ret == 1) || (zip_drives[id].bus_type == ZIP_BUS_SCSI)) {
			zip_log("ZIP %i: DMA data in phase done\n");
			zip_buf_free(id);
			zip_command_complete(id);
		} else if (ret == 2) {
			zip_log("ZIP %i: DMA in not enabled, wait\n");
			zip_command_bus(id);
		} else {
			zip_log("ZIP %i: DMA data in phase failure\n");
			zip_buf_free(id);
		}
		return;
	case ZIP_PHASE_ERROR:
		zip_log("ZIP %i: ZIP_PHASE_ERROR\n", id);
		dev->status = READY_STAT | ERR_STAT;
		dev->phase = 3;
		dev->packet_status = 0xFF;
		zip_irq_raise(id);
		ui_sb_update_icon(SB_ZIP | id, 0);
		return;
    }
}


uint32_t
zip_read(uint8_t channel, int length)
{
    zip_t *dev;

    uint16_t *zipbufferw;
    uint32_t *zipbufferl;

    uint8_t id = atapi_zip_drives[channel];

    uint32_t temp = 0;

    if (id > ZIP_NUM)
	return 0;

    dev = zip[id];

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

    if (dev->packet_status == ZIP_PHASE_DATA_IN) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		zip_log("ZIP %i: Issuing read callback\n", id);
		zip_pio_request(id, 0);
	}
	zip_log("ZIP %i: Returning: %02X (buffer position: %i, request position: %i)\n", id, temp, dev->pos, dev->request_pos);
	return temp;
    } else {
	zip_log("ZIP %i: Returning zero (buffer position: %i, request position: %i)\n", id, dev->pos, dev->request_pos);
	return 0;
    }
}


void
zip_write(uint8_t channel, uint32_t val, int length)
{
    zip_t *dev;

    uint16_t *zipbufferw;
    uint32_t *zipbufferl;

    uint8_t id = atapi_zip_drives[channel];

    if (id > ZIP_NUM)
	return;

    dev = zip[id];

    if (dev->packet_status == ZIP_PHASE_IDLE) {
	if (!zipbufferb)
		zip_buf_alloc(id, dev->cdb_len);
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

    if (dev->packet_status == ZIP_PHASE_DATA_OUT) {
	if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
		/* Time for a DRQ. */
		zip_pio_request(id, 1);
	}
	return;
    } else if (dev->packet_status == ZIP_PHASE_IDLE) {
	if (dev->pos >= dev->cdb_len) {
		dev->pos=0;
		dev->status = BUSY_STAT;
		dev->packet_status = ZIP_PHASE_COMMAND;
		timer_process();
		zip_phase_callback(id);
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


void
zip_hard_reset(void)
{
    int c;

    zip_destroy_drives();

    for (c=0; c<ZIP_NUM; c++) {
	if (zip_drives[c].bus_type) {
		zip_log("ZIP hard_reset drive=%d\n", c);

		if (!zip[c])
			zip[c] = (zip_t *) malloc(sizeof(zip_t));

		SCSIReset(zip_drives[c].scsi_device_id, zip_drives[c].scsi_device_lun);

		if (wcslen(zip_drives[c].image_path))
			zip_load(c, zip_drives[c].image_path);

		zip_mode_sense_load(c);
	}
    }

    build_atapi_zip_map();
}
