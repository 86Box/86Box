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
 * Version:	@(#)zip.c	1.0.0	2018/01/22
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
#include "86box.h"
#include "config.h"
#include "timer.h"
#include "device.h"
#include "piix.h"
#include "scsi/scsi.h"
#include "nvr.h"
#include "disk/hdc.h"
#include "disk/hdc_ide.h"
#include "plat.h"
#include "ui.h"
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

zip_t		zip[ZIP_NUM];
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
uint8_t zip_command_flags[0x100] =
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
    IMPLEMENTED,
    IMPLEMENTED | CHECK_READY,					/* 0x1B */
    0,
    IMPLEMENTED,						/* 0x1D */
    IMPLEMENTED | CHECK_READY,					/* 0x1E */
    0, 0, 0, 0, 0, 0,
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

uint64_t zip_mode_sense_page_flags = (1LL << GPMODE_R_W_ERROR_PAGE) | (1LL << 0x02LL) | (1LL << 0x2FLL) | (1LL << GPMODE_ALL_PAGES);
uint64_t zip_250_mode_sense_page_flags = (1LL << GPMODE_R_W_ERROR_PAGE) | (1LL << 0x05LL) | (1LL << 0x08LL) | (1LL << 0x2FLL) | (1LL << GPMODE_ALL_PAGES);


static const mode_sense_pages_t zip_mode_sense_pages_default =
{	{
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
}	};

static const mode_sense_pages_t zip_250_mode_sense_pages_default =
{	{
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
}	};

static const mode_sense_pages_t zip_mode_sense_pages_default_scsi =
{	{
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
}	};

static const mode_sense_pages_t zip_250_mode_sense_pages_default_scsi =
{	{
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
}	};

static const mode_sense_pages_t zip_mode_sense_pages_changeable =
{	{
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
}	};

static const mode_sense_pages_t zip_250_mode_sense_pages_changeable =
{	{
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
}	};

static mode_sense_pages_t zip_mode_sense_pages_saved[ZIP_NUM];


#ifdef ENABLE_ZIP_LOG
int zip_do_log = ENABLE_ZIP_LOG;
#endif


static void
zip_log(const char *format, ...)
{
#ifdef ENABLE_ZIP_LOG
	va_list ap;

	if (zip_do_log)
	{
		va_start(ap, format);
		pclog_ex(format, ap);
		va_end(ap);
	}
#endif
}


int find_zip_for_channel(uint8_t channel)
{
	uint8_t i = 0;

	for (i = 0; i < ZIP_NUM; i++) {
		if (((zip_drives[i].bus_type == ZIP_BUS_ATAPI_PIO_ONLY) || (zip_drives[i].bus_type == ZIP_BUS_ATAPI_PIO_AND_DMA)) && (zip_drives[i].ide_channel == channel))
			return i;
	}
	return 0xff;
}

void zip_init(int id, int cdb_len_setting);

int zip_load(uint8_t id, wchar_t *fn)
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

		if (zip_drives[id].is_250) {
			if ((size != (ZIP_250_SECTORS << 9)) && (size != (ZIP_SECTORS << 9))) {
				zip_log("File is incorrect size for a ZIP image\nMust be exactly %i or %i bytes\n", ZIP_250_SECTORS << 9, ZIP_SECTORS << 9);
				fclose(zip_drives[id].f);
				zip_drives[id].f = NULL;
				zip_drives[id].medium_size = 0;
				return 0;
			}
		} else {
			if (size != (ZIP_SECTORS << 9)) {
				zip_log("File is incorrect size for a ZIP image\nMust be exactly %i bytes\n", ZIP_SECTORS << 9);
				fclose(zip_drives[id].f);
				zip_drives[id].f = NULL;
				zip_drives[id].medium_size = 0;
				return 0;
			}
		}

		zip_drives[id].medium_size = size >> 9;

		fseek(zip_drives[id].f, 0, SEEK_SET);

		memcpy(zip_drives[id].image_path, fn, sizeof(zip_drives[id].image_path));

		zip_drives[id].read_only = read_only;

		return 1;
	}

	return 0;
}

void zip_disk_reload(uint8_t id)
{
	int ret = 0;

	if (wcslen(zip_drives[id].prev_image_path) == 0)
		return;
	else
		ret = zip_load(id, zip_drives[id].prev_image_path);

	if (ret)
		zip[id].unit_attention = 1;
}

void zip_close(uint8_t id)
{
	if (zip_drives[id].f) {
		fclose(zip_drives[id].f);
		zip_drives[id].f = NULL;

		memcpy(zip_drives[id].prev_image_path, zip_drives[id].image_path, sizeof(zip_drives[id].prev_image_path));
		memset(zip_drives[id].image_path, 0, sizeof(zip_drives[id].image_path));

		zip_drives[id].medium_size = 0;
	}
}

void build_atapi_zip_map()
{
	uint8_t i = 0;

	memset(atapi_zip_drives, 0xff, 8);

	for (i = 0; i < 8; i++) {
		atapi_zip_drives[i] = find_zip_for_channel(i);
		if (atapi_zip_drives[i] != 0xff)
			zip_init(atapi_zip_drives[i], 12);
	}
}

int find_zip_for_scsi_id(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t i = 0;

	for (i = 0; i < ZIP_NUM; i++) {
		if ((zip_drives[i].bus_type == ZIP_BUS_SCSI) && (zip_drives[i].scsi_device_id == scsi_id) && (zip_drives[i].scsi_device_lun == scsi_lun))
			return i;
	}
	return 0xff;
}

void build_scsi_zip_map()
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

void zip_set_callback(uint8_t id)
{
	if (zip_drives[id].bus_type != ZIP_BUS_SCSI)
		ide_set_callback(zip_drives[id].ide_channel, zip[id].callback);
}

void zip_set_cdb_len(int id, int cdb_len)
{
	zip[id].cdb_len = cdb_len;
}

void zip_reset_cdb_len(int id)
{
	zip[id].cdb_len = zip[id].cdb_len_setting ? 16 : 12;
}

void zip_set_signature(int id)
{
	if (id >= ZIP_NUM)
		return;
	zip[id].phase = 1;
	zip[id].request_length = 0xEB14;
}

void zip_init(int id, int cdb_len_setting)
{
	if (id >= ZIP_NUM)
		return;
	memset(&(zip[id]), 0, sizeof(zip_t));
	zip[id].requested_blocks = 1;
	if (cdb_len_setting <= 1)
		zip[id].cdb_len_setting = cdb_len_setting;
	zip_reset_cdb_len(id);
	zip[id].sense[0] = 0xf0;
	zip[id].sense[7] = 10;
	zip_drives[id].bus_mode = 0;
	if (zip_drives[id].bus_type >= ZIP_BUS_ATAPI_PIO_AND_DMA)
		zip_drives[id].bus_mode |= 2;
	if (zip_drives[id].bus_type < ZIP_BUS_SCSI)
		zip_drives[id].bus_mode |= 1;
	zip_log("ZIP %i: Bus type %i, bus mode %i\n", id, zip_drives[id].bus_type, zip_drives[id].bus_mode);
	if (zip_drives[id].bus_type < ZIP_BUS_SCSI)
		zip_set_signature(id);
	zip_drives[id].max_blocks_at_once = 85;
	zip[id].status = READY_STAT | DSC_STAT;
	zip[id].pos = 0;
	zip[id].packet_status = 0xff;
	zip_sense_key = zip_asc = zip_ascq = zip[id].unit_attention = 0;
	zip[id].cdb_len_setting = 0;
	zip[id].cdb_len = 12;
}

int zip_supports_pio(int id)
{
	return (zip_drives[id].bus_mode & 1);
}

int zip_supports_dma(int id)
{
	return (zip_drives[id].bus_mode & 2);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
int zip_current_mode(int id)
{
	if (!zip_supports_pio(id) && !zip_supports_dma(id))
		return 0;
	if (zip_supports_pio(id) && !zip_supports_dma(id)) {
		zip_log("ZIP %i: Drive does not support DMA, setting to PIO\n", id);
		return 1;
	}
	if (!zip_supports_pio(id) && zip_supports_dma(id))
		return 2;
	if (zip_supports_pio(id) && zip_supports_dma(id)) {
		zip_log("ZIP %i: Drive supports both, setting to %s\n", id, (zip[id].features & 1) ? "DMA" : "PIO", id);
		return (zip[id].features & 1) ? 2 : 1;
	}

	return 0;
}

/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int zip_ZIP_PHASE_to_scsi(uint8_t id)
{
	if (zip[id].status & ERR_STAT)
		return SCSI_STATUS_CHECK_CONDITION;
	else
		return SCSI_STATUS_OK;
}

/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int zip_atapi_phase_to_scsi(uint8_t id)
{
	if (zip[id].status & 8) {
		switch (zip[id].phase & 3) {
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
		if ((zip[id].phase & 3) == 3)
			return 3;
		else
			return 4;
	}

	return 0;
}

int zip_lba_to_msf_accurate(int lba)
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

void zip_mode_sense_load(uint8_t id)
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

void zip_mode_sense_save(uint8_t id)
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

static void zip_command_complete(uint8_t id);

uint8_t zip_read_capacity_cdb[12] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

int zip_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
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
uint8_t zip_mode_sense_read(uint8_t id, uint8_t page_control, uint8_t page, uint8_t pos)
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

uint32_t zip_mode_sense(uint8_t id, uint8_t *buf, uint32_t pos, uint8_t type, uint8_t block_descriptor_len)
{
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
			if (page_flags & (1LL << zip[id].current_page_code)) {
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

void zip_update_request_length(uint8_t id, int len, int block_len)
{
	uint32_t bt;

	/* For media access commands, make sure the requested DRQ length matches the block length. */
	switch (zip[id].current_cdb[0]) {
		case 0x08:
		case 0x28:
		case 0xa8:
			if (zip[id].request_length < block_len)
				zip[id].request_length = block_len;
			bt = (zip[id].requested_blocks * block_len);
			if (len > bt)
				len = bt;
		default:
			zip[id].packet_len = len;
			break;
	}
	/* If the DRQ length is odd, and the total remaining length is bigger, make sure it's even. */
	if ((zip[id].request_length & 1) && (zip[id].request_length < len))
		zip[id].request_length &= 0xfffe;
	/* If the DRQ length is smaller or equal in size to the total remaining length, set it to that. */
	if (len <= zip[id].request_length)
		zip[id].request_length = len;
	return;
}

static void zip_command_common(uint8_t id)
{
	zip[id].status = BUSY_STAT;
	zip[id].phase = 1;
	zip[id].pos = 0;
	if (zip[id].packet_status == ZIP_PHASE_COMPLETE) {
		zip[id].callback = 20LL * ZIP_TIME;
		zip_set_callback(id);
	} else if (zip[id].packet_status == ZIP_PHASE_DATA_IN) {
		if (zip[id].current_cdb[0] == 0x42) {
			zip_log("ZIP %i: READ SUBCHANNEL\n");
			zip[id].callback = 1000LL * ZIP_TIME;
			zip_set_callback(id);
		} else {
			zip[id].callback = 60LL * ZIP_TIME;
			zip_set_callback(id);
		}
	} else {
		zip[id].callback = 60LL * ZIP_TIME;
		zip_set_callback(id);
	}
}

static void zip_command_complete(uint8_t id)
{
	zip[id].packet_status = ZIP_PHASE_COMPLETE;
	zip_command_common(id);
}

static void zip_command_read(uint8_t id)
{
	zip[id].packet_status = ZIP_PHASE_DATA_IN;
	zip_command_common(id);
	zip[id].total_read = 0;
}

static void zip_command_read_dma(uint8_t id)
{
	zip[id].packet_status = ZIP_PHASE_DATA_IN_DMA;
	zip_command_common(id);
	zip[id].total_read = 0;
}

static void zip_command_write(uint8_t id)
{
	zip[id].packet_status = ZIP_PHASE_DATA_OUT;
	zip_command_common(id);
}

static void zip_command_write_dma(uint8_t id)
{
	zip[id].packet_status = ZIP_PHASE_DATA_OUT_DMA;
	zip_command_common(id);
}

static int zip_request_length_is_zero(uint8_t id)
{
	if ((zip[id].request_length == 0) && (zip_drives[id].bus_type < ZIP_BUS_SCSI))
		return 1;
	return 0;
}

/* id = Current ZIP device ID;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void zip_data_command_finish(uint8_t id, int len, int block_len, int alloc_len, int direction)
{
	zip_log("ZIP %i: Finishing command (%02X): %i, %i, %i, %i, %i\n", id, zip[id].current_cdb[0], len, block_len, alloc_len, direction, zip[id].request_length);
	zip[id].pos=0;
	if (alloc_len >= 0) {
		if (alloc_len < len) {
			len = alloc_len;
		}
	}
	if (zip_request_length_is_zero(id) || (len == 0) || (zip_current_mode(id) == 0)) {
		if (zip_drives[id].bus_type != ZIP_BUS_SCSI) {
			zip[id].packet_len = 0;
		}
		zip_command_complete(id);
	}
	else {
		if (zip_current_mode(id) == 2) {
			if (zip_drives[id].bus_type != ZIP_BUS_SCSI) {
				zip[id].packet_len = alloc_len;
			}

			if (direction == 0)
				zip_command_read_dma(id);
			else
				zip_command_write_dma(id);
		}
		else {
			zip_update_request_length(id, len, block_len);
			if (direction == 0)
				zip_command_read(id);
			else
				zip_command_write(id);
		}
	}
	
	zip_log("ZIP %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n", id, zip[id].packet_status, zip[id].request_length, zip[id].packet_len, zip[id].pos, zip[id].phase);
}

static void zip_sense_clear(int id, int command)
{
	zip[id].previous_command = command;
	zip_sense_key = zip_asc = zip_ascq = 0;
}

static void zip_set_phase(uint8_t id, uint8_t phase)
{
	uint8_t scsi_id = zip_drives[id].scsi_device_id;
	uint8_t scsi_lun = zip_drives[id].scsi_device_lun;

	if (zip_drives[id].bus_type != ZIP_BUS_SCSI)
		return;

	SCSIDevices[scsi_id][scsi_lun].Phase = phase;
}

static void zip_cmd_error(uint8_t id)
{
	zip_set_phase(id, SCSI_PHASE_STATUS);
	zip[id].error = ((zip_sense_key & 0xf) << 4) | ABRT_ERR;
	if (zip[id].unit_attention)
		zip[id].error |= MCR_ERR;
	zip[id].status = READY_STAT | ERR_STAT;
	zip[id].phase = 3;
	zip[id].pos = 0;
	zip[id].packet_status = 0x80;
	zip[id].callback = 50LL * ZIP_TIME;
	zip_set_callback(id);
	pclog("ZIP %i: [%02X] ERROR: %02X/%02X/%02X\n", id, zip[id].current_cdb[0], zip_sense_key, zip_asc, zip_ascq);
}

static void zip_unit_attention(uint8_t id)
{
	zip_set_phase(id, SCSI_PHASE_STATUS);
	zip[id].error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
	if (zip[id].unit_attention)
		zip[id].error |= MCR_ERR;
	zip[id].status = READY_STAT | ERR_STAT;
	zip[id].phase = 3;
	zip[id].pos = 0;
	zip[id].packet_status = 0x80;
	zip[id].callback = 50LL * ZIP_TIME;
	zip_set_callback(id);
	zip_log("ZIP %i: UNIT ATTENTION\n", id);
}

static void zip_bus_master_error(uint8_t id)
{
	zip_sense_key = zip_asc = zip_ascq = 0;
	zip_cmd_error(id);
}

static void zip_not_ready(uint8_t id)
{
	zip_sense_key = SENSE_NOT_READY;
	zip_asc = ASC_MEDIUM_NOT_PRESENT;
	zip_ascq = 0;
	zip_cmd_error(id);
}

static void zip_write_protected(uint8_t id)
{
	zip_sense_key = SENSE_UNIT_ATTENTION;
	zip_asc = ASC_WRITE_PROTECTED;
	zip_ascq = 0;
	zip_cmd_error(id);
}

static void zip_invalid_lun(uint8_t id)
{
	zip_sense_key = SENSE_ILLEGAL_REQUEST;
	zip_asc = ASC_INV_LUN;
	zip_ascq = 0;
	zip_cmd_error(id);
}

static void zip_illegal_opcode(uint8_t id)
{
	zip_sense_key = SENSE_ILLEGAL_REQUEST;
	zip_asc = ASC_ILLEGAL_OPCODE;
	zip_ascq = 0;
	zip_cmd_error(id);
}

static void zip_lba_out_of_range(uint8_t id)
{
	zip_sense_key = SENSE_ILLEGAL_REQUEST;
	zip_asc = ASC_LBA_OUT_OF_RANGE;
	zip_ascq = 0;
	zip_cmd_error(id);
}

static void zip_invalid_field(uint8_t id)
{
	zip_sense_key = SENSE_ILLEGAL_REQUEST;
	zip_asc = ASC_INV_FIELD_IN_CMD_PACKET;
	zip_ascq = 0;
	zip_cmd_error(id);
	zip[id].status = 0x53;
}

static void zip_invalid_field_pl(uint8_t id)
{
	zip_sense_key = SENSE_ILLEGAL_REQUEST;
	zip_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
	zip_ascq = 0;
	zip_cmd_error(id);
	zip[id].status = 0x53;
}

static void zip_data_phase_error(uint8_t id)
{
	zip_sense_key = SENSE_ILLEGAL_REQUEST;
	zip_asc = ASC_DATA_PHASE_ERROR;
	zip_ascq = 0;
	zip_cmd_error(id);
}

#define zipbufferb zip[id].buffer

int zip_data(uint8_t id, uint32_t *len, int out)
{
	int i = 0;

	if (zip[id].sector_pos >= zip_drives[id].medium_size) {
		pclog("ZIP %i: Trying to %s beyond the end of disk\n", id, out ? "write" : "read");
		zip_lba_out_of_range(id);
		return 0;
	}

	*len = 0;

	for (i = 0; i < zip[id].requested_blocks; i++) {
		fseek(zip_drives[id].f, (zip[id].sector_pos << 9) + *len, SEEK_SET);
		if (out)
			fwrite(zipbufferb + *len, 1, 512, zip_drives[id].f);
		else
			fread(zipbufferb + *len, 1, 512, zip_drives[id].f);

		*len += 512;
	}

	return 1;
}

int zip_blocks(uint8_t id, uint32_t *len, int first_batch, int out)
{
	int ret = 0;

	zip[id].data_pos = 0;
	
	if (!zip[id].sector_len) {
		zip_command_complete(id);
		return -1;
	}

	zip_log("%sing %i blocks starting from %i...\n", out ? "Writ" : "Read", zip[id].requested_blocks, zip[id].sector_pos);

	ret = zip_data(id, len, out);

	zip_log("%s %i bytes of blocks...\n", out ? "Written" : "Read", *len);

	if (!ret)
		return 0;

	zip[id].sector_pos += zip[id].requested_blocks;
	zip[id].sector_len -= zip[id].requested_blocks;

	return 1;
}

void zip_insert(uint8_t id)
{
	zip[id].unit_attention = 1;
}

/*SCSI Sense Initialization*/
void zip_sense_code_ok(uint8_t id)
{	
	zip_sense_key = SENSE_NONE;
	zip_asc = 0;
	zip_ascq = 0;
}

int zip_pre_execution_check(uint8_t id, uint8_t *cdb)
{
	int ready = 0;

	if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
		if (((zip[id].request_length >> 5) & 7) != zip_drives[id].scsi_device_lun) {
			zip_log("ZIP %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", id, ((zip[id].request_length >> 5) & 7));
			zip_invalid_lun(id);
			return 0;
		}
	}

	if (!(zip_command_flags[cdb[0]] & IMPLEMENTED)) {
		zip_log("ZIP %i: Attempting to execute unknown command %02X over %s\n", id, cdb[0], (zip_drives[id].bus_type == ZIP_BUS_SCSI) ? "SCSI" : ((zip_drives[id].bus_type == ZIP_BUS_ATAPI_PIO_AND_DMA) ? "ATAPI PIO/DMA" : "ATAPI PIO"));

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
	if (!ready && zip[id].unit_attention)
		zip[id].unit_attention = 0;

	/* If the UNIT ATTENTION condition is set and the command does not allow
		execution under it, error out and report the condition. */
	if (zip[id].unit_attention == 1) {
		/* Only increment the unit attention phase if the command can not pass through it. */
		if (!(zip_command_flags[cdb[0]] & ALLOW_UA)) {
			/* zip_log("ZIP %i: Unit attention now 2\n", id); */
			zip[id].unit_attention = 2;
			zip_log("ZIP %i: UNIT ATTENTION: Command %02X not allowed to pass through\n", id, cdb[0]);
			zip_unit_attention(id);
			return 0;
		}
	}
	else if (zip[id].unit_attention == 2) {
		if (cdb[0] != GPCMD_REQUEST_SENSE) {
			/* zip_log("ZIP %i: Unit attention now 0\n", id); */
			zip[id].unit_attention = 0;
		}
	}

	/* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
		the UNIT ATTENTION condition if it's set. */
	if (cdb[0] != GPCMD_REQUEST_SENSE)
		zip_sense_clear(id, cdb[0]);

	/* Next it's time for NOT READY. */
	if (!ready)
		zip[id].media_status = MEC_MEDIA_REMOVAL;
	else
		zip[id].media_status = (zip[id].unit_attention) ? MEC_NEW_MEDIA : MEC_NO_CHANGE;

	if ((zip_command_flags[cdb[0]] & CHECK_READY) && !ready) {
		zip_log("ZIP %i: Not ready (%02X)\n", id, cdb[0]);
		zip_not_ready(id);
		return 0;
	}

	zip_log("ZIP %i: Continuing with command %02X\n", id, cdb[0]);
		
	return 1;
}

void zip_clear_callback(uint8_t channel)
{
	uint8_t id = atapi_zip_drives[channel];

	if (id < ZIP_NUM)
	{
		zip[id].callback = 0LL;
		zip_set_callback(id);
	}
}

static void zip_seek(uint8_t id, uint32_t pos)
{
        /* zip_log("ZIP %i: Seek %08X\n", id, pos); */
        zip[id].sector_pos   = pos;
}

static void zip_rezero(uint8_t id)
{
	zip[id].sector_pos = zip[id].sector_len = 0;
	zip_seek(id, 0);
}

void zip_reset(uint8_t id)
{
	zip_rezero(id);
	zip[id].status = 0;
	zip[id].callback = 0LL;
	zip_set_callback(id);
	zip[id].packet_status = 0xff;
	zip[id].unit_attention = 0;
}

void zip_request_sense(uint8_t id, uint8_t *buffer, uint8_t alloc_length, int desc)
{				
	/*Will return 18 bytes of 0*/
	if (alloc_length != 0) {
		memset(buffer, 0, alloc_length);
		if (!desc)
			memcpy(buffer, zip[id].sense, alloc_length);
		else {
			buffer[1] = zip_sense_key;
			buffer[2] = zip_asc;
			buffer[3] = zip_ascq;
		}
	}

	buffer[0] = desc ? 0x72 : 0x70;

	if (zip[id].unit_attention && (zip_sense_key == 0)) {
		buffer[desc ? 1 : 2]=SENSE_UNIT_ATTENTION;
		buffer[desc ? 2 : 12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
		buffer[desc ? 3 : 13]=0;
	}

	zip_log("ZIP %i: Reporting sense: %02X %02X %02X\n", id, buffer[2], buffer[12], buffer[13]);

	if (buffer[desc ? 1 : 2] == SENSE_UNIT_ATTENTION) {
		/* If the last remaining sense is unit attention, clear
		   that condition. */
		zip[id].unit_attention = 0;
	}

	/* Clear the sense stuff as per the spec. */
	zip_sense_clear(id, GPCMD_REQUEST_SENSE);
}

void zip_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{
	int ready = 0;

	ready = (zip_drives[id].f != NULL);

	if (!ready && zip[id].unit_attention) {
		/* If the drive is not ready, there is no reason to keep the
		   UNIT ATTENTION condition present, as we only use it to mark
		   disc changes. */
		zip[id].unit_attention = 0;
	}

	/* Do *NOT* advance the unit attention phase. */

	zip_request_sense(id, buffer, alloc_length, 0);
}

void zip_set_buf_len(uint8_t id, int32_t *BufLen, uint32_t *src_len)
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

void zip_buf_alloc(uint8_t id, uint32_t len)
{
	zip_log("ZIP %i: Allocated buffer length: %i\n", id, len);
	zipbufferb = (uint8_t *) malloc(len);
}

void zip_buf_free(uint8_t id)
{
	if (zipbufferb) {
		zip_log("ZIP %i: Freeing buffer...\n", id);
		free(zipbufferb);
		zipbufferb = NULL;
	}
}

void zip_command(uint8_t id, uint8_t *cdb)
{
	uint32_t len;
	int pos=0;
	uint32_t max_len;
	unsigned idx = 0;
	unsigned size_idx;
	unsigned preamble_len;
	uint32_t alloc_length;
	int block_desc = 0;
	int ret;
	int32_t blen = 0;
	int32_t *BufLen;
	uint32_t i = 0;

#if 0
	int CdbLength;
#endif
	if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
		BufLen = &SCSIDevices[zip_drives[id].scsi_device_id][zip_drives[id].scsi_device_lun].BufferLength;
		zip[id].status &= ~ERR_STAT;
	} else {
		BufLen = &blen;
		zip[id].error = 0;
	}

	zip[id].packet_len = 0;
	zip[id].request_pos = 0;

	zip[id].data_pos = 0;

	memcpy(zip[id].current_cdb, cdb, zip[id].cdb_len);

	if (cdb[0] != 0) {
		pclog("ZIP %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, Unit attention: %i\n", id, cdb[0], zip_sense_key, zip_asc, zip_ascq, zip[id].unit_attention);
		pclog("ZIP %i: Request length: %04X\n", id, zip[id].request_length);

#if 0
		for (CdbLength = 1; CdbLength < zip[id].cdb_len; CdbLength++)
			pclog("ZIP %i: CDB[%d] = %d\n", id, CdbLength, cdb[CdbLength]);
#endif
	}
	
	zip[id].sector_len = 0;

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
				if (zip_drives[i].read_only)
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
			zip[id].sector_pos = zip[id].sector_len = 0;
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

		case GPCMD_SET_SPEED:
		case GPCMD_SET_SPEED_ALT:
			zip_set_phase(id, SCSI_PHASE_STATUS);
			zip_command_complete(id);
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
					zip[id].sector_len = cdb[4];
					zip[id].sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
					pclog("ZIP %i: Length: %i, LBA: %i\n", id, zip[id].sector_len, zip[id].sector_pos);
					break;
				case GPCMD_READ_10:
					zip[id].sector_len = (cdb[7] << 8) | cdb[8];
					zip[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					zip_log("ZIP %i: Length: %i, LBA: %i\n", id, zip[id].sector_len, zip[id].sector_pos);
					break;
				case GPCMD_READ_12:
					zip[id].sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
					zip[id].sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
					break;
			}

			if (!zip[id].sector_len) {
				zip_set_phase(id, SCSI_PHASE_STATUS);
				/* zip_log("ZIP %i: All done - callback set\n", id); */
				zip[id].packet_status = ZIP_PHASE_COMPLETE;
				zip[id].callback = 20LL * ZIP_TIME;
				zip_set_callback(id);
				break;
			}

			max_len = zip[id].sector_len;
			zip[id].requested_blocks = max_len;	/* If we're reading all blocks in one go for DMA, why not also for PIO, it should NOT
								   matter anyway, this step should be identical and only the way the read dat is
								   transferred to the host should be different. */

			zip[id].packet_len = max_len * alloc_length;
			zip_buf_alloc(id, zip[id].packet_len);

			ret = zip_blocks(id, &alloc_length, 1, 0);
			if (ret <= 0) {
				zip_buf_free(id);
				return;
			}

			zip[id].requested_blocks = max_len;
			zip[id].packet_len = alloc_length;

			zip_set_buf_len(id, BufLen, &zip[id].packet_len);

			zip_data_command_finish(id, alloc_length, 512, alloc_length, 0);

			zip[id].all_blocks_total = zip[id].block_total;
			if (zip[id].packet_status != ZIP_PHASE_COMPLETE)
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

			if ((zip_drives[id].bus_type == ZIP_BUS_SCSI) && zip_drives[id].read_only)
			{
				zip_write_protected(id);
				return;
			}

			switch(cdb[0]) {
				case GPCMD_VERIFY_6:
				case GPCMD_WRITE_6:
					zip[id].sector_len = cdb[4];
					zip[id].sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
					break;
				case GPCMD_VERIFY_10:
				case GPCMD_WRITE_10:
				case GPCMD_WRITE_AND_VERIFY_10:
					zip[id].sector_len = (cdb[7] << 8) | cdb[8];
					zip[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					zip_log("ZIP %i: Length: %i, LBA: %i\n", id, zip[id].sector_len, zip[id].sector_pos);
					break;
				case GPCMD_VERIFY_12:
				case GPCMD_WRITE_12:
				case GPCMD_WRITE_AND_VERIFY_12:
					zip[id].sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
					zip[id].sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
					break;
			}

			if (zip_drives[id].is_250) {
				if ((zip[id].sector_pos >= zip_drives[id].medium_size) || ((zip[id].sector_pos + zip[id].sector_len - 1) >= zip_drives[id].medium_size))
				{
					zip_lba_out_of_range(id);
					return;
				}
			} else {
				if ((zip[id].sector_pos >= ZIP_SECTORS) || ((zip[id].sector_pos + zip[id].sector_len - 1) >= ZIP_SECTORS))
				{
					zip_lba_out_of_range(id);
					return;
				}
			}

			if (!zip[id].sector_len) {
				zip_set_phase(id, SCSI_PHASE_STATUS);
				/* zip_log("ZIP %i: All done - callback set\n", id); */
				zip[id].packet_status = ZIP_PHASE_COMPLETE;
				zip[id].callback = 20LL * ZIP_TIME;
				zip_set_callback(id);
				break;
			}

			max_len = zip[id].sector_len;
			zip[id].requested_blocks = max_len;	/* If we're writing all blocks in one go for DMA, why not also for PIO, it should NOT
								   matter anyway, this step should be identical and only the way the read dat is
								   transferred to the host should be different. */

			zip[id].packet_len = max_len * alloc_length;
			zip_buf_alloc(id, zip[id].packet_len);

			zip[id].requested_blocks = max_len;
			zip[id].packet_len = max_len << 9;

			zip_set_buf_len(id, BufLen, &zip[id].packet_len);

			zip_data_command_finish(id, zip[id].packet_len, 512, zip[id].packet_len, 1);

			zip[id].all_blocks_total = zip[id].block_total;
			if (zip[id].packet_status != ZIP_PHASE_COMPLETE)
				ui_sb_update_icon(SB_ZIP | id, 1);
			else
				ui_sb_update_icon(SB_ZIP | id, 0);
			return;

		case GPCMD_WRITE_SAME_10:
			zip_set_phase(id, SCSI_PHASE_DATA_OUT);
			alloc_length = 512;

			if ((cdb[1] & 6) == 6)
			{
				zip_invalid_field(id);
				return;
			}

			if ((zip_drives[id].bus_type == ZIP_BUS_SCSI) && zip_drives[id].read_only)
			{
				zip_write_protected(id);
				return;
			}

			zip[id].sector_len = (cdb[7] << 8) | cdb[8];
			zip[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

			if (zip_drives[id].is_250) {
				if ((zip[id].sector_pos >= zip_drives[id].medium_size) || ((zip[id].sector_pos + zip[id].sector_len - 1) >= zip_drives[id].medium_size))
				{
					zip_lba_out_of_range(id);
					return;
				}
			} else {
				if ((zip[id].sector_pos >= ZIP_SECTORS) || ((zip[id].sector_pos + zip[id].sector_len - 1) >= ZIP_SECTORS))
				{
					zip_lba_out_of_range(id);
					return;
				}
			}

			if (!zip[id].sector_len) {
				zip_set_phase(id, SCSI_PHASE_STATUS);
				/* zip_log("ZIP %i: All done - callback set\n", id); */
				zip[id].packet_status = ZIP_PHASE_COMPLETE;
				zip[id].callback = 20LL * ZIP_TIME;
				zip_set_callback(id);
				break;
			}

			max_len = zip[id].sector_len;
			zip[id].requested_blocks = max_len;	/* If we're writing all blocks in one go for DMA, why not also for PIO, it should NOT
								   matter anyway, this step should be identical and only the way the read dat is
								   transferred to the host should be different. */

			zip[id].packet_len = max_len * alloc_length;
			zip_buf_alloc(id, zip[id].packet_len);

			zip[id].requested_blocks = max_len;
			zip[id].packet_len = alloc_length;

			zip_set_buf_len(id, BufLen, &zip[id].packet_len);

			zip_data_command_finish(id, zip[id].packet_len, 512, zip[id].packet_len, 1);

			zip[id].all_blocks_total = zip[id].block_total;
			if (zip[id].packet_status != ZIP_PHASE_COMPLETE)
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

			zip[id].current_page_code = cdb[2] & 0x3F;
			pclog("Mode sense page: %02X\n", zip[id].current_page_code);

			if (!(zip_mode_sense_page_flags & (1LL << zip[id].current_page_code))) {
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

			zip[id].total_length = len;
			zip[id].do_page_save = cdb[1] & 1;

			zip[id].current_page_pos = 0;

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

			if (zip_read_capacity(id, zip[id].current_cdb, zipbufferb, &len) == 0) {
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

		default:
			zip_illegal_opcode(id);
			break;
	}

	/* zip_log("ZIP %i: Phase: %02X, request length: %i\n", zip[id].phase, zip[id].request_length); */

	if (zip_atapi_phase_to_scsi(id) == SCSI_PHASE_STATUS)
		zip_buf_free(id);
}

/* The command second phase function, needed for Mode Select. */
uint8_t zip_phase_data_out(uint8_t id)
{
	uint16_t block_desc_len;
	uint16_t pos;

	uint8_t error = 0;
	uint8_t page, page_len;

	uint16_t i = 0;

	uint8_t hdr_len, val, old_val, ch;

	uint32_t last_to_write = 0, len = 0;
	uint32_t c, h, s;

	switch(zip[id].current_cdb[0]) {
		case GPCMD_VERIFY_6:
		case GPCMD_VERIFY_10:
		case GPCMD_VERIFY_12:
			break;
		case GPCMD_WRITE_6:
		case GPCMD_WRITE_10:
		case GPCMD_WRITE_AND_VERIFY_10:
		case GPCMD_WRITE_12:
		case GPCMD_WRITE_AND_VERIFY_12:
			if (zip[id].requested_blocks > 0)
				zip_blocks(id, &len, 1, 1);
			break;
		case GPCMD_WRITE_SAME_10:
			if (!zip[id].current_cdb[7] && !zip[id].current_cdb[8]) {
				if (zip_drives[id].is_250)
					last_to_write = (zip_drives[id].medium_size - 1);
				else
					last_to_write = (ZIP_SECTORS - 1);
			} else
				last_to_write = zip[id].sector_pos + zip[id].sector_len - 1;

			for (i = zip[id].sector_pos; i <= last_to_write; i++) {
				if (zip[id].current_cdb[1] & 2) {
					zipbufferb[0] = (i >> 24) & 0xff;
					zipbufferb[1] = (i >> 16) & 0xff;
					zipbufferb[2] = (i >> 8) & 0xff;
					zipbufferb[3] = i & 0xff;
				} else if (zip[id].current_cdb[1] & 4) {
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
				fseek(zip_drives[id].f, (i << 9), SEEK_SET);
				fwrite(zipbufferb, 1, 512, zip_drives[id].f);
			}
			break;
		case GPCMD_MODE_SELECT_6:
		case GPCMD_MODE_SELECT_10:
			if (zip[id].current_cdb[0] == GPCMD_MODE_SELECT_10)
				hdr_len = 8;
			else
				hdr_len = 4;

			if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
				if (zip[id].current_cdb[0] == GPCMD_MODE_SELECT_6) {
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
				if (zip[id].do_page_save && val)
					zip_mode_sense_save(id);

				if (pos >= zip[id].total_length)
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
void zip_pio_request(uint8_t id, uint8_t out)
{
	int old_pos = 0;
	int ret = 0;

	if (zip_drives[id].bus_type < ZIP_BUS_SCSI) {
		zip_log("ZIP %i: Lowering IDE IRQ\n", id);
		ide_irq_lower(&(ide_drives[zip_drives[id].ide_channel]));
	}
	
	zip[id].status = BUSY_STAT;

	if (zip[id].pos >= zip[id].packet_len) {
		zip_log("ZIP %i: %i bytes %s, command done\n", id, zip[id].pos, out ? "written" : "read");

		zip[id].pos = zip[id].request_pos = 0;
		if (out) {
			ret = zip_phase_data_out(id);
			/* If ret = 0 (phase 1 error), then we do not do anything else other than
			   free the buffer, as the phase and callback have already been set by the
			   error function. */
			if (ret)
				zip_command_complete(id);
		} else
			zip_command_complete(id);
		ui_sb_update_icon(SB_ZIP | id, 0);
		zip_buf_free(id);
	} else {
		zip_log("ZIP %i: %i bytes %s, %i bytes are still left\n", id, zip[id].pos, out ? "written" : "read", zip[id].packet_len - zip[id].pos);

		/* Make sure to keep pos, and reset request_pos to 0. */
		/* Also make sure to not reset total_read. */
		old_pos = zip[id].pos;
		zip[id].packet_status = out ? ZIP_PHASE_DATA_OUT : ZIP_PHASE_DATA_IN;
		zip_command_common(id);
		zip[id].pos = old_pos;
		zip[id].request_pos = 0;
	}
}

void zip_phase_callback(uint8_t id);

int zip_read_from_ide_dma(uint8_t channel)
{
	uint8_t id = atapi_zip_drives[channel];

	if (id > ZIP_NUM)
		return 0;

	if (ide_bus_master_write) {
		if (ide_bus_master_write(channel >> 1, zipbufferb, zip[id].packet_len)) {
			zip_bus_master_error(id);
			zip_phase_callback(id);
			return 0;
		} else
			return 1;
	} else {
		zip_bus_master_error(id);
		zip_phase_callback(id);
		return 0;
	}

	return 0;
}

int zip_read_from_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t id = scsi_zip_drives[scsi_id][scsi_lun];
	int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

	if (id > ZIP_NUM)
		return 0;

	zip_log("Reading from SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
	memcpy(zipbufferb, SCSIDevices[scsi_id][scsi_lun].CmdBuffer, *BufLen);
	return 1;
}

int zip_read_from_dma(uint8_t id)
{
	int32_t *BufLen = &SCSIDevices[zip_drives[id].scsi_device_id][zip_drives[id].scsi_device_lun].BufferLength;

	int ret = 0;

	int in_data_length = 0;

	if (zip_drives[id].bus_type == ZIP_BUS_SCSI)
		ret = zip_read_from_scsi_dma(zip_drives[id].scsi_device_id, zip_drives[id].scsi_device_lun);
	else
		ret = zip_read_from_ide_dma(zip_drives[id].ide_channel);

	if (!ret)
		return 0;

	if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
		in_data_length = *BufLen;
		zip_log("ZIP %i: SCSI Input data length: %i\n", id, in_data_length);
	} else {
		in_data_length = zip[id].request_length;
		zip_log("ZIP %i: ATAPI Input data length: %i\n", id, in_data_length);
	}

	ret = zip_phase_data_out(id);
	if (!ret) {
		zip_phase_callback(id);
		return 0;
	} else
		return 1;

	return 0;
}

int zip_write_to_ide_dma(uint8_t channel)
{
	uint8_t id = atapi_zip_drives[channel];

	if (id > ZIP_NUM) {
		zip_log("ZIP %i: Drive not found\n", id);
		return 0;
	}

	if (ide_bus_master_read) {
		if (ide_bus_master_read(channel >> 1, zipbufferb, zip[id].packet_len)) {
			zip_log("ZIP %i: ATAPI DMA error\n", id);
			zip_bus_master_error(id);
			zip_phase_callback(id);
			return 0;
		}
		else {
			zip_log("ZIP %i: ATAPI DMA success\n", id);
			return 1;
		}
	} else {
		zip_log("ZIP %i: No bus master\n", id);
		zip_bus_master_error(id);
		zip_phase_callback(id);
		return 0;
	}

	return 0;
}

int zip_write_to_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t id = scsi_zip_drives[scsi_id][scsi_lun];
	int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

	if (id > ZIP_NUM)
		return 0;

	zip_log("Writing to SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
	memcpy(SCSIDevices[scsi_id][scsi_lun].CmdBuffer, zipbufferb, *BufLen);
	zip_log("ZIP %i: Data from CD buffer:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, zipbufferb[0], zipbufferb[1], zipbufferb[2], zipbufferb[3], zipbufferb[4], zipbufferb[5], zipbufferb[6], zipbufferb[7]);
	zip_log("ZIP %i: Data from SCSI DMA :  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, SCSIDevices[scsi_id][scsi_lun].CmdBuffer[0], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[1], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[2], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[3], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[4], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[5], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[6], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[7]);
	return 1;
}

int zip_write_to_dma(uint8_t id)
{
	int ret = 0;

	if (zip_drives[id].bus_type == ZIP_BUS_SCSI) {
		zip_log("Write to SCSI DMA: (%02X:%02X)\n", zip_drives[id].scsi_device_id, zip_drives[id].scsi_device_lun);
		ret = zip_write_to_scsi_dma(zip_drives[id].scsi_device_id, zip_drives[id].scsi_device_lun);
	} else
		ret = zip_write_to_ide_dma(zip_drives[id].ide_channel);

	if (!ret)
		return 0;

	return 1;
}

void zip_irq_raise(uint8_t id)
{
	if (zip_drives[id].bus_type < ZIP_BUS_SCSI)
		ide_irq_raise(&(ide_drives[zip_drives[id].ide_channel]));
}

/* If the result is 1, issue an IRQ, otherwise not. */
void zip_phase_callback(uint8_t id)
{
	switch(zip[id].packet_status) {
		case ZIP_PHASE_IDLE:
			zip_log("ZIP %i: ZIP_PHASE_IDLE\n", id);
			zip[id].pos=0;
			zip[id].phase = 1;
			zip[id].status = READY_STAT | DRQ_STAT | (zip[id].status & ERR_STAT);
			return;
		case ZIP_PHASE_COMMAND:
			zip_log("ZIP %i: ZIP_PHASE_COMMAND\n", id);
			zip[id].status = BUSY_STAT | (zip[id].status &ERR_STAT);
			memcpy(zip[id].atapi_cdb, zipbufferb, zip[id].cdb_len);
			zip_command(id, zip[id].atapi_cdb);
			return;
		case ZIP_PHASE_COMPLETE:
			zip_log("ZIP %i: ZIP_PHASE_COMPLETE\n", id);
			zip[id].status = READY_STAT;
			zip[id].phase = 3;
			zip[id].packet_status = 0xFF;
			ui_sb_update_icon(SB_ZIP | id, 0);
			zip_irq_raise(id);
			return;
		case ZIP_PHASE_DATA_OUT:
			zip_log("ZIP %i: ZIP_PHASE_DATA_OUT\n", id);
			zip[id].status = READY_STAT | DRQ_STAT | (zip[id].status & ERR_STAT);
			zip[id].phase = 0;
			zip_irq_raise(id);
			return;
		case ZIP_PHASE_DATA_OUT_DMA:
			zip_log("ZIP %i: ZIP_PHASE_DATA_OUT_DMA\n", id);
			zip_read_from_dma(id);
			zip_buf_free(id);
			zip[id].packet_status = ZIP_PHASE_COMPLETE;
			zip[id].status = READY_STAT;
			zip[id].phase = 3;
			ui_sb_update_icon(SB_ZIP | id, 0);
			zip_irq_raise(id);
			return;
		case ZIP_PHASE_DATA_IN:
			zip_log("ZIP %i: ZIP_PHASE_DATA_IN\n", id);
			zip[id].status = READY_STAT | DRQ_STAT | (zip[id].status & ERR_STAT);
			zip[id].phase = 2;
			zip_irq_raise(id);
			return;
		case ZIP_PHASE_DATA_IN_DMA:
			zip_log("ZIP %i: ZIP_PHASE_DATA_IN_DMA\n", id);
			zip_write_to_dma(id);
			zip_buf_free(id);
			zip[id].packet_status = ZIP_PHASE_COMPLETE;
			zip[id].status = READY_STAT;
			zip[id].phase = 3;
			ui_sb_update_icon(SB_ZIP | id, 0);
			zip_irq_raise(id);
			return;
		case ZIP_PHASE_ERROR:
			zip_log("ZIP %i: ZIP_PHASE_ERROR\n", id);
			zip[id].status = READY_STAT | ERR_STAT;
			zip[id].phase = 3;
			zip_irq_raise(id);
			ui_sb_update_icon(SB_ZIP | id, 0);
			return;
	}
}

/* Reimplement as 8-bit due to reimplementation of IDE data read and write. */
uint32_t zip_read(uint8_t channel, int length)
{
	uint16_t *zipbufferw;
	uint32_t *zipbufferl;

	uint8_t id = atapi_zip_drives[channel];

	uint32_t temp = 0;

	if (id > ZIP_NUM)
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
			temp = (zip[id].pos < zip[id].packet_len) ? zipbufferb[zip[id].pos] : 0;
			zip[id].pos++;
			zip[id].request_pos++;
			break;
		case 2:
			temp = (zip[id].pos < zip[id].packet_len) ? zipbufferw[zip[id].pos >> 1] : 0;
			zip[id].pos += 2;
			zip[id].request_pos += 2;
			break;
		case 4:
			temp = (zip[id].pos < zip[id].packet_len) ? zipbufferl[zip[id].pos >> 2] : 0;
			zip[id].pos += 4;
			zip[id].request_pos += 4;
			break;
		default:
			return 0;
	}

	if (zip[id].packet_status == ZIP_PHASE_DATA_IN) {
		if ((zip[id].request_pos >= zip[id].request_length) || (zip[id].pos >= zip[id].packet_len)) {
			/* Time for a DRQ. */
			// zip_log("ZIP %i: Issuing read callback\n", id);
			zip_pio_request(id, 0);
		}
		// zip_log("ZIP %i: Returning: %02X (buffer position: %i, request position: %i)\n", id, temp, zip[id].pos, zip[id].request_pos);
		return temp;
	} else {
		// zip_log("ZIP %i: Returning zero (buffer position: %i, request position: %i)\n", id, zip[id].pos, zip[id].request_pos);
		return 0;
	}
}

/* Reimplement as 8-bit due to reimplementation of IDE data read and write. */
void zip_write(uint8_t channel, uint32_t val, int length)
{
	uint16_t *zipbufferw;
	uint32_t *zipbufferl;

	uint8_t id = atapi_zip_drives[channel];

	if (id > ZIP_NUM)
		return;

	if (zip[id].packet_status == ZIP_PHASE_IDLE) {
		if (!zipbufferb)
			zip_buf_alloc(id, zip[id].cdb_len);
	}

	zipbufferw = (uint16_t *) zipbufferb;
	zipbufferl = (uint32_t *) zipbufferb;

	if (!zipbufferb)
		return;

	switch(length) {
		case 1:
			zipbufferb[zip[id].pos] = val & 0xff;
			zip[id].pos++;
			zip[id].request_pos++;
			break;
		case 2:
			zipbufferw[zip[id].pos >> 1] = val & 0xffff;
			zip[id].pos += 2;
			zip[id].request_pos += 2;
			break;
		case 4:
			zipbufferl[zip[id].pos >> 2] = val;
			zip[id].pos += 4;
			zip[id].request_pos += 4;
			break;
		default:
			return;
	}

	if (zip[id].packet_status == ZIP_PHASE_DATA_OUT) {
		if ((zip[id].request_pos >= zip[id].request_length) || (zip[id].pos >= zip[id].packet_len)) {
			/* Time for a DRQ. */
			zip_pio_request(id, 1);
		}
		return;
	} else if (zip[id].packet_status == ZIP_PHASE_IDLE) {
		if (zip[id].pos >= zip[id].cdb_len) {
			zip[id].pos=0;
			zip[id].status = BUSY_STAT;
			zip[id].packet_status = ZIP_PHASE_COMMAND;
			timer_process();
			zip_phase_callback(id);
			timer_update_outstanding();
		}
		return;
	}
}

void zip_hard_reset(void)
{
    int i = 0;

    for (i=0; i<ZIP_NUM; i++)
	zip_mode_sense_load(i);
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
zip_global_reset(void)
{
    int c;

    for (c=0; c<ZIP_NUM; c++) {
	if (zip_drives[c].bus_type)
		SCSIReset(zip_drives[c].scsi_device_id, zip_drives[c].scsi_device_lun);

pclog("ZIP global_reset drive=%d host=%02x\n", c, zip_drives[c].host_drive);
	if (wcslen(zip_drives[c].image_path))
		zip_load(c, zip_drives[c].image_path);
    }
}
