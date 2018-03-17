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
 * Version:	@(#)cdrom.c	1.0.39	2018/03/17
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
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
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../plat.h"
#include "../ui.h"
#include "cdrom.h"
#include "cdrom_image.h"
#include "cdrom_null.h"


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

cdrom_t		*cdrom[CDROM_NUM];
cdrom_image_t	cdrom_image[CDROM_NUM];
cdrom_ioctl_t	cdrom_ioctl[CDROM_NUM];
cdrom_drive_t	cdrom_drives[CDROM_NUM];
uint8_t atapi_cdrom_drives[8] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
uint8_t scsi_cdrom_drives[16][8] =	{	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
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


#pragma pack(push,1)
static struct
{
	uint8_t opcode;
	uint8_t polled;
	uint8_t reserved2[2];
	uint8_t class;
	uint8_t reserved3[2];
	uint16_t len;
	uint8_t control;
} *gesn_cdb;
#pragma pack(pop)

#pragma pack(push,1)
static struct
{
	uint16_t len;
	uint8_t notification_class;
	uint8_t supported_events;
} *gesn_event_header;
#pragma pack(pop)


/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
uint8_t cdrom_command_flags[0x100] =
{
	IMPLEMENTED | CHECK_READY | NONDATA,
	IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,
	0,
	IMPLEMENTED | ALLOW_UA,
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA,
	0, 0, 0, 0, 0, 0,
	IMPLEMENTED | ALLOW_UA,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,
	0,
	IMPLEMENTED,
	0, 0, 0, 0,
	IMPLEMENTED,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA,
	0, 0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,	/* Read TOC - can get through UNIT_ATTENTION, per VIDE-CDD.SYS
					   NOTE: The ATAPI reference says otherwise, but I think this is a question of
					   interpreting things right - the UNIT ATTENTION condition we have here
					   is a tradition from not ready to ready, by definition the drive
					   eventually becomes ready, make the condition go away. */
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | ALLOW_UA,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	0,
	IMPLEMENTED | ALLOW_UA,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED,
	0, 0, 0, 0,
	IMPLEMENTED,
	0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY,
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,
	0,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY | ATAPI_ONLY,
	0, 0, 0,
	IMPLEMENTED | CHECK_READY | ATAPI_ONLY,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED,
	IMPLEMENTED | CHECK_READY,
	IMPLEMENTED | CHECK_READY,
	0, 0,
	IMPLEMENTED | CHECK_READY | SCSI_ONLY,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY | SCSI_ONLY,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED | SCSI_ONLY,
	0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

uint64_t cdrom_mode_sense_page_flags = (1LL << GPMODE_R_W_ERROR_PAGE) | (1LL << GPMODE_CDROM_PAGE) | (1LL << GPMODE_CDROM_AUDIO_PAGE) | (1LL << GPMODE_CAPABILITIES_PAGE) | (1LL << GPMODE_ALL_PAGES);


static const mode_sense_pages_t cdrom_mode_sense_pages_default =
{	{
	{                        0,    0 },
	{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
	{                        0,    0 },
	{                        0,    0 },
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
	{                     0x8E,  0xE, 4, 0, 0,  0, 0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{ GPMODE_CAPABILITIES_PAGE, 0x14, 0x3B, 0, 0x71, 0x60, 0x29, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0, 0, 0 }
}	};

static const mode_sense_pages_t cdrom_mode_sense_pages_default_scsi =
{	{
	{                        0,    0 },
	{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
	{                        0,    0 },
	{                        0,    0 },
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
	{                     0x8E,  0xE, 5, 4, 0,0x80,0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{ GPMODE_CAPABILITIES_PAGE, 0x14, 0x3B, 0, 0x71, 0x60, 0x29, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0, 0, 0 }
}	};

static const mode_sense_pages_t cdrom_mode_sense_pages_changeable =
{	{
	{                        0,    0 },
	{    GPMODE_R_W_ERROR_PAGE,    6, 0, 5, 0,  0, 0, 0 },
	{                        0,    0 },
	{                        0,    0 },
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
	{                     0x8E,  0xE, 5, 4, 0,0x80,0, 75, 1, 0xFF, 2, 0xFF, 0, 0, 0, 0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{                        0,    0 },
	{ GPMODE_CAPABILITIES_PAGE, 0x14, 0x3B, 0, 0x71, 0x60, 0x29, 0, 0x02, 0xC2, 0, 2, 0, 0, 0x02, 0xC2, 0, 0, 0, 0, 0, 0 }
}	};

static mode_sense_pages_t cdrom_mode_sense_pages_saved[CDROM_NUM];


#ifdef ENABLE_CDROM_LOG
int cdrom_do_log = ENABLE_CDROM_LOG;
#endif


static void
cdrom_log(const char *format, ...)
{
#ifdef ENABLE_CDROM_LOG
	va_list ap;

	if (cdrom_do_log)
	{
		va_start(ap, format);
		pclog_ex(format, ap);
		va_end(ap);
	}
#endif
}


int find_cdrom_for_channel(uint8_t channel)
{
	uint8_t i = 0;

	for (i = 0; i < CDROM_NUM; i++) {
		if (((cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_ONLY) || (cdrom_drives[i].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA)) && (cdrom_drives[i].ide_channel == channel))
			return i;
	}
	return 0xff;
}

void cdrom_init(int id, int cdb_len_setting);

void build_atapi_cdrom_map()
{
	uint8_t i = 0;

	memset(atapi_cdrom_drives, 0xff, 8);

	for (i = 0; i < 8; i++) {
		atapi_cdrom_drives[i] = find_cdrom_for_channel(i);
		if (atapi_cdrom_drives[i] != 0xff)
			cdrom_init(atapi_cdrom_drives[i], 12);
	}
}

int find_cdrom_for_scsi_id(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t i = 0;

	for (i = 0; i < CDROM_NUM; i++) {
		if ((cdrom_drives[i].bus_type == CDROM_BUS_SCSI) && (cdrom_drives[i].scsi_device_id == scsi_id) && (cdrom_drives[i].scsi_device_lun == scsi_lun))
			return i;
	}
	return 0xff;
}

void build_scsi_cdrom_map()
{
	uint8_t i = 0;
	uint8_t j = 0;

	for (i = 0; i < 16; i++)
		memset(scsi_cdrom_drives[i], 0xff, 8);

	for (i = 0; i < 16; i++) {
		for (j = 0; j < 8; j++) {
			scsi_cdrom_drives[i][j] = find_cdrom_for_scsi_id(i, j);
			if (scsi_cdrom_drives[i][j] != 0xff)
				cdrom_init(scsi_cdrom_drives[i][j], 12);
		}
	}
}

void cdrom_set_callback(uint8_t id)
{
	if (cdrom_drives[id].bus_type != CDROM_BUS_SCSI)
		ide_set_callback(cdrom_drives[id].ide_channel, cdrom[id]->callback);
}

void cdrom_set_cdb_len(int id, int cdb_len)
{
	cdrom[id]->cdb_len = cdb_len;
}

void cdrom_reset_cdb_len(int id)
{
	cdrom[id]->cdb_len = cdrom[id]->cdb_len_setting ? 16 : 12;
}

void cdrom_set_signature(int id)
{
	cdrom_t *dev = cdrom[id];

	if (id >= CDROM_NUM)
		return;
	dev->phase = 1;
	dev->request_length = 0xEB14;
}

void cdrom_destroy_drives(void)
{
	int i;

	for (i = 0; i < CDROM_NUM; i++) {
		if (cdrom[i]) {
			free(cdrom[i]);
			cdrom[i] = NULL;
		}
	}
}

void cdrom_init(int id, int cdb_len_setting)
{
	cdrom_t *dev;

	if (id >= CDROM_NUM)
		return;
	dev = cdrom[id];
	memset(dev, 0, sizeof(cdrom_t));
	dev->requested_blocks = 1;
	if (cdb_len_setting <= 1)
		dev->cdb_len_setting = cdb_len_setting;
	cdrom_reset_cdb_len(id);
	dev->cd_status = CD_STATUS_EMPTY;
	dev->sense[0] = 0xf0;
	dev->sense[7] = 10;
	cdrom_drives[id].bus_mode = 0;
	if (cdrom_drives[id].bus_type >= CDROM_BUS_ATAPI_PIO_AND_DMA)
		cdrom_drives[id].bus_mode |= 2;
	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI)
		cdrom_drives[id].bus_mode |= 1;
	cdrom_log("CD-ROM %i: Bus type %i, bus mode %i\n", id, cdrom_drives[id].bus_type, cdrom_drives[id].bus_mode);
	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI)
		cdrom_set_signature(id);
	cdrom_drives[id].max_blocks_at_once = 85;
	dev->status = READY_STAT | DSC_STAT;
	dev->pos = 0;
	dev->packet_status = 0xff;
	cdrom_sense_key = cdrom_asc = cdrom_ascq = dev->unit_attention = 0;
	dev->cdb_len_setting = 0;
	dev->cdb_len = 12;
}

int cdrom_supports_pio(int id)
{
	return (cdrom_drives[id].bus_mode & 1);
}

int cdrom_supports_dma(int id)
{
	return (cdrom_drives[id].bus_mode & 2);
}

/* Returns: 0 for none, 1 for PIO, 2 for DMA. */
int cdrom_current_mode(int id)
{
	cdrom_t *dev = cdrom[id];

	if (!cdrom_supports_pio(id) && !cdrom_supports_dma(id))
		return 0;
	if (cdrom_supports_pio(id) && !cdrom_supports_dma(id)) {
		cdrom_log("CD-ROM %i: Drive does not support DMA, setting to PIO\n", id);
		return 1;
	}
	if (!cdrom_supports_pio(id) && cdrom_supports_dma(id))
		return 2;
	if (cdrom_supports_pio(id) && cdrom_supports_dma(id)) {
		cdrom_log("CD-ROM %i: Drive supports both, setting to %s\n", id, (dev->features & 1) ? "DMA" : "PIO", id);
		return (dev->features & 1) ? 2 : 1;
	}

	return 0;
}

/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int cdrom_CDROM_PHASE_to_scsi(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	if (dev->status & ERR_STAT)
		return SCSI_STATUS_CHECK_CONDITION;
	else
		return SCSI_STATUS_OK;
}

/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int cdrom_atapi_phase_to_scsi(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

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

int cdrom_lba_to_msf_accurate(int lba)
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

uint32_t cdrom_mode_sense_get_channel(uint8_t id, int channel)
{
	return cdrom_mode_sense_pages_saved[id].pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 10 : 8];
}

uint32_t cdrom_mode_sense_get_volume(uint8_t id, int channel)
{
	return cdrom_mode_sense_pages_saved[id].pages[GPMODE_CDROM_AUDIO_PAGE][channel ? 11 : 9];
}

void cdrom_mode_sense_load(uint8_t id)
{
	FILE *f;
	wchar_t file_name[512];
	int i;
	memset(&cdrom_mode_sense_pages_saved[id], 0, sizeof(mode_sense_pages_t));
	for (i = 0; i < 0x3f; i++) {
		if (cdrom_mode_sense_pages_default.pages[i][1] != 0) {
			if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
				memcpy(cdrom_mode_sense_pages_saved[id].pages[i], cdrom_mode_sense_pages_default_scsi.pages[i], cdrom_mode_sense_pages_default_scsi.pages[i][1] + 2);
			else
				memcpy(cdrom_mode_sense_pages_saved[id].pages[i], cdrom_mode_sense_pages_default.pages[i], cdrom_mode_sense_pages_default.pages[i][1] + 2);
		}
	}
	memset(file_name, 0, 512 * sizeof(wchar_t));
	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
		swprintf(file_name, 512, L"scsi_cdrom_%02i_mode_sense_bin", id);
	else
		swprintf(file_name, 512, L"cdrom_%02i_mode_sense_bin", id);
	f = plat_fopen(nvr_path(file_name), L"rb");
	if (f) {
		fread(cdrom_mode_sense_pages_saved[id].pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, f);
		fclose(f);
	}
}

void cdrom_mode_sense_save(uint8_t id)
{
	FILE *f;
	wchar_t file_name[512];
	memset(file_name, 0, 512 * sizeof(wchar_t));
	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
		swprintf(file_name, 512, L"scsi_cdrom_%02i_mode_sense_bin", id);
	else
		swprintf(file_name, 512, L"cdrom_%02i_mode_sense_bin", id);
	f = plat_fopen(nvr_path(file_name), L"wb");
	if (f) {
		fwrite(cdrom_mode_sense_pages_saved[id].pages[GPMODE_CDROM_AUDIO_PAGE], 1, 0x10, f);
		fclose(f);
	}
}

static void cdrom_command_complete(uint8_t id);

uint8_t cdrom_read_capacity_cdb[12] = {0x25, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

static int cdrom_pass_through(uint8_t id, uint32_t *len, uint8_t *cdb, uint8_t *buffer);

int cdrom_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
	int ret = 0;
	int size = 0;

	if (cdrom_drives[id].handler->pass_through) {
		ret = cdrom_pass_through(id, len, cdb, buffer);
		if (!ret)
			return 0;
		if (*len == 65534)
			*len = 8;
	} else {
		size = cdrom_drives[id].handler->size(id) - 1;		/* IMPORTANT: What's returned is the last LBA block. */
		memset(buffer, 0, 8);
		buffer[0] = (size >> 24) & 0xff;
		buffer[1] = (size >> 16) & 0xff;
		buffer[2] = (size >> 8) & 0xff;
		buffer[3] = size & 0xff;
		buffer[6] = 8;				/* 2048 = 0x0800 */
		*len = 8;
	}
	return 1;
}

/*SCSI Mode Sense 6/10*/
uint8_t cdrom_mode_sense_read(uint8_t id, uint8_t page_control, uint8_t page, uint8_t pos)
{
	switch (page_control) {
		case 0:
		case 3:
			return cdrom_mode_sense_pages_saved[id].pages[page][pos];
			break;
		case 1:
			return cdrom_mode_sense_pages_changeable.pages[page][pos];
			break;
		case 2:
			if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
				return cdrom_mode_sense_pages_default_scsi.pages[page][pos];
			else
				return cdrom_mode_sense_pages_default.pages[page][pos];
			break;
	}

	return 0;
}

uint32_t cdrom_mode_sense(uint8_t id, uint8_t *buf, uint32_t pos, uint8_t type, uint8_t block_descriptor_len)
{
	cdrom_t *dev = cdrom[id];

	uint8_t page_control = (type >> 6) & 3;

	int i = 0;
	int j = 0;

	uint8_t msplen;

	type &= 0x3f;

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
	        if ((type == GPMODE_ALL_PAGES) || (type == i)) {
			if (cdrom_mode_sense_page_flags & (1LL << dev->current_page_code)) {
				buf[pos++] = cdrom_mode_sense_read(id, page_control, i, 0);
				msplen = cdrom_mode_sense_read(id, page_control, i, 1);
				buf[pos++] = msplen;
				cdrom_log("CD-ROM %i: MODE SENSE: Page [%02X] length %i\n", id, i, msplen);
				for (j = 0; j < msplen; j++)
					buf[pos++] = cdrom_mode_sense_read(id, page_control, i, 2 + j);
			}
		}
	}

	return pos;
}

void cdrom_update_request_length(uint8_t id, int len, int block_len)
{
	cdrom_t *dev = cdrom[id];
	uint32_t bt;

	dev->max_transfer_len = dev->request_length;

	/* For media access commands, make sure the requested DRQ length matches the block length. */
	switch (dev->current_cdb[0]) {
		case 0x08:
		case 0x28:
		case 0xa8:
		case 0xb9:
		case 0xbe:
			if (dev->max_transfer_len < block_len)
				dev->max_transfer_len = block_len;
			bt = (dev->requested_blocks * block_len);
			if (len > bt)
				len = bt;
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
	if (len <= dev->max_transfer_len)
		dev->max_transfer_len = len;
	return;
}

static double cdrom_get_short_seek(uint8_t id)
{
	switch(cdrom_drives[id].speed) {
		case 0:
			fatal("CD-ROM %i: 0x speed\n", id);
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

static double cdrom_get_long_seek(uint8_t id)
{
	switch(cdrom_drives[id].speed) {
		case 0:
			fatal("CD-ROM %i: 0x speed\n", id);
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

#define MIN_SEEK	  2000
#define MAX_SEEK	333333

static double cdrom_seek_time(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	uint32_t diff = dev->seek_diff;
	double sd = (double) (MAX_SEEK - MIN_SEEK);

	if (diff < MIN_SEEK)
		return 0.0;
	if (diff > MAX_SEEK)
		diff = MAX_SEEK;

	diff -= MIN_SEEK;

	return cdrom_get_short_seek(id) + ((cdrom_get_long_seek(id) * ((double) diff)) / sd);
}

static double cdrom_bus_speed(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
		dev->callback = -1LL;	/* Speed depends on SCSI controller */
		return 0.0;
	} else if (cdrom_drives[id].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA) {
		if (cdrom_current_mode(id) == 2)
			return 66666666.666666666666666;	/* 66 MB/s MDMA-2 speed */
		else
			return  8333333.333333333333333;	/* 8.3 MB/s PIO-2 speed */
	} else
		return 3333333.333333333333333;		/* 3.3 MB/s PIO-0 speed */
}

static void cdrom_command_common(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	double bytes_per_second, period;
	double dusec;

	dev->status = BUSY_STAT;
	dev->phase = 1;
	dev->pos = 0;
	dev->callback = 0LL;
	if (dev->packet_status == CDROM_PHASE_COMPLETE) {
		cdrom_phase_callback(id);
		dev->callback = 0LL;
	} else {
		switch(dev->current_cdb[0]) {
			case 0x0b:
			case 0x2b:
				/* Seek time is in us. */
				period = cdrom_seek_time(id) * ((double) TIMER_USEC);
				dev->callback += ((int64_t) period);
				cdrom_set_callback(id);
				return;
			case 0x08:
			case 0x28:
			case 0xa8:
				/* Seek time is in us. */
				period = cdrom_seek_time(id) * ((double) TIMER_USEC);
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
				/* bytes_per_second = 150.0 * 1024.0; */
				bytes_per_second = (1000000.0 / 12000.0) * 2048.0;	/* Account for seek time. */
				bytes_per_second *= (double) cdrom_drives[id].speed;
				break;
			default:
				bytes_per_second = cdrom_bus_speed(id);
				if (bytes_per_second == 0.0) {
					dev->callback = -1LL;	/* Speed depends on SCSI controller */
					return;
				}
				break;
		}

		period = 1000000.0 / bytes_per_second;
		dusec = (double) TIMER_USEC;
		dusec = dusec * period * (double) (dev->packet_len);
		dev->callback += ((int64_t) dusec);
	}
	cdrom_set_callback(id);
}

static void cdrom_command_complete(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	dev->packet_status = CDROM_PHASE_COMPLETE;
	cdrom_command_common(id);
}

static void cdrom_command_read(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	dev->packet_status = CDROM_PHASE_DATA_IN;
	cdrom_command_common(id);
	dev->total_read = 0;
}

static void cdrom_command_read_dma(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	dev->packet_status = CDROM_PHASE_DATA_IN_DMA;
	cdrom_command_common(id);
	dev->total_read = 0;
}

static void cdrom_command_write(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	dev->packet_status = CDROM_PHASE_DATA_OUT;
	cdrom_command_common(id);
}

static void cdrom_command_write_dma(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	dev->packet_status = CDROM_PHASE_DATA_OUT_DMA;
	cdrom_command_common(id);
}

/* id = Current CD-ROM device ID;
   len = Total transfer length;
   block_len = Length of a single block (why does it matter?!);
   alloc_len = Allocated transfer length;
   direction = Transfer direction (0 = read from host, 1 = write to host). */
static void cdrom_data_command_finish(uint8_t id, int len, int block_len, int alloc_len, int direction)
{
	cdrom_t *dev = cdrom[id];

	cdrom_log("CD-ROM %i: Finishing command (%02X): %i, %i, %i, %i, %i\n", id, dev->current_cdb[0], len, block_len, alloc_len, direction, dev->request_length);
	dev->pos=0;
	if (alloc_len >= 0) {
		if (alloc_len < len) {
			len = alloc_len;
		}
	}
	if ((len == 0) || (cdrom_current_mode(id) == 0)) {
		if (cdrom_drives[id].bus_type != CDROM_BUS_SCSI) {
			dev->packet_len = 0;
		}
		cdrom_command_complete(id);
	}
	else {
		if (cdrom_current_mode(id) == 2) {
			if (cdrom_drives[id].bus_type != CDROM_BUS_SCSI) {
				dev->packet_len = alloc_len;
			}

			if (direction == 0)
				cdrom_command_read_dma(id);
			else
				cdrom_command_write_dma(id);
		}
		else {
			cdrom_update_request_length(id, len, block_len);
			if (direction == 0)
				cdrom_command_read(id);
			else
				cdrom_command_write(id);
		}
	}
	
	cdrom_log("CD-ROM %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n", id, dev->packet_status, dev->request_length, dev->packet_len, dev->pos, dev->phase);
}

static void cdrom_sense_clear(int id, int command)
{
	cdrom_t *dev = cdrom[id];

	dev->previous_command = command;
	cdrom_sense_key = cdrom_asc = cdrom_ascq = 0;
}

static void cdrom_set_phase(uint8_t id, uint8_t phase)
{
	uint8_t scsi_id = cdrom_drives[id].scsi_device_id;
	uint8_t scsi_lun = cdrom_drives[id].scsi_device_lun;

	if (cdrom_drives[id].bus_type != CDROM_BUS_SCSI)
		return;

	SCSIDevices[scsi_id][scsi_lun].Phase = phase;
}

static void cdrom_cmd_error(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	cdrom_set_phase(id, SCSI_PHASE_STATUS);
	dev->error = ((cdrom_sense_key & 0xf) << 4) | ABRT_ERR;
	if (dev->unit_attention)
		dev->error |= MCR_ERR;
	dev->status = READY_STAT | ERR_STAT;
	dev->phase = 3;
	dev->pos = 0;
	dev->packet_status = 0x80;
	dev->callback = 50LL * CDROM_TIME;
	cdrom_set_callback(id);
	cdrom_log("CD-ROM %i: ERROR: %02X/%02X/%02X\n", id, cdrom_sense_key, cdrom_asc, cdrom_ascq);
}

static void cdrom_unit_attention(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	cdrom_set_phase(id, SCSI_PHASE_STATUS);
	dev->error = (SENSE_UNIT_ATTENTION << 4) | ABRT_ERR;
	if (dev->unit_attention)
		dev->error |= MCR_ERR;
	dev->status = READY_STAT | ERR_STAT;
	dev->phase = 3;
	dev->pos = 0;
	dev->packet_status = 0x80;
	dev->callback = 50LL * CDROM_TIME;
	cdrom_set_callback(id);
	cdrom_log("CD-ROM %i: UNIT ATTENTION\n", id);
}

static void cdrom_not_ready(uint8_t id)
{
	cdrom_sense_key = SENSE_NOT_READY;
	cdrom_asc = ASC_MEDIUM_NOT_PRESENT;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_invalid_lun(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INV_LUN;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_illegal_opcode(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_ILLEGAL_OPCODE;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_lba_out_of_range(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_LBA_OUT_OF_RANGE;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_invalid_field(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INV_FIELD_IN_CMD_PACKET;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
	dev->status = 0x53;
}

static void cdrom_invalid_field_pl(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
	dev->status = 0x53;
}

static void cdrom_illegal_mode(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_ILLEGAL_MODE_FOR_THIS_TRACK;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static void cdrom_incompatible_format(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_INCOMPATIBLE_FORMAT;
	cdrom_ascq = 2;
	cdrom_cmd_error(id);
}

static void cdrom_data_phase_error(uint8_t id)
{
	cdrom_sense_key = SENSE_ILLEGAL_REQUEST;
	cdrom_asc = ASC_DATA_PHASE_ERROR;
	cdrom_ascq = 0;
	cdrom_cmd_error(id);
}

static int cdrom_pass_through(uint8_t id, uint32_t *len, uint8_t *cdb, uint8_t *buffer)
{
	int ret = 0;
	uint8_t temp_cdb[16];

	memset(temp_cdb, 0, 16);

	if (cdb[0] == 8) {
		temp_cdb[0] = 0x28;
		temp_cdb[8] = cdb[4];
		temp_cdb[3] = cdb[1];
		temp_cdb[4] = cdb[2];
		temp_cdb[5] = cdb[3];
	} else
		memcpy(temp_cdb, cdb, 16);

	ret = cdrom_drives[id].handler->pass_through(id, temp_cdb, buffer, len);
	cdrom_log("CD-ROM %i: Data from pass through:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
	cdrom_log("CD-ROM %i: Returned value: %i\n", id, ret);

	if (!ret) {
		/* Command failed with OS error code, return illegal opcode. */
		cdrom_log("CD-ROM %i: Command failed with OS error code, return illegal opcode.\n", id);
		cdrom_illegal_opcode(id);
		return 0;
	} else {
		if ((cdrom_sense_key != 0) || (cdrom_asc != 0) || (cdrom_ascq != 0)) {
			/* Command failed with sense, error with that sense. */
			cdrom_log("CD-ROM %i: Command failed with sense, error with that sense (%02X/%02X/%02X).\n", id, cdrom_sense_key, cdrom_asc, cdrom_ascq);
			cdrom_cmd_error(id);
			return 0;
		} else {
			/* Command was performed successfully. */
			cdrom_log("CD-ROM %i: Command was performed successfully.\n", id);
			return 1;
		}
	}
}

void cdrom_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks)
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

#define cdbufferb dev->buffer

int cdrom_read_data(uint8_t id, int msf, int type, int flags, uint32_t *len)
{
	cdrom_t *dev = cdrom[id];

	int ret = 0;
	int cdsize = 0;

	int i = 0;
	int temp_len = 0;

	int last_valid_data_pos = 0;

	if (cdrom_drives[id].handler->pass_through) {
		cdsize = cdrom_drives[id].handler->size(id);

		ret = cdrom_pass_through(id, len, dev->current_cdb, cdbufferb + dev->data_pos);
		dev->data_pos += *len;

		if (!ret)
			return 0;

		if (dev->sector_pos > (cdsize - 1)) {
			/* cdrom_log("CD-ROM %i: Trying to read beyond the end of disc\n", id); */
			cdrom_lba_out_of_range(id);
			return 0;
		}

		dev->old_len = *len;
	} else {
		if (dev->sector_pos > (cdrom_drives[id].handler->size(id) - 1)) {
			/* cdrom_log("CD-ROM %i: Trying to read beyond the end of disc\n", id); */
			cdrom_lba_out_of_range(id);
			return 0;
		}

		dev->old_len = 0;
		*len = 0;

		for (i = 0; i < dev->requested_blocks; i++) {
			ret = cdrom_drives[id].handler->readsector_raw(id, cdbufferb + dev->data_pos, dev->sector_pos + i, msf, type, flags, &temp_len);

			last_valid_data_pos = dev->data_pos;

			dev->data_pos += temp_len;
			dev->old_len += temp_len;

			*len += temp_len;

			if (!ret) {
				cdrom_illegal_mode(id);
				return 0;
			}
		}

		cdrom_log("CD-ROM %i: Data from raw sector read:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, cdbufferb[last_valid_data_pos + 0], cdbufferb[last_valid_data_pos + 1], cdbufferb[last_valid_data_pos + 2], cdbufferb[last_valid_data_pos + 3], cdbufferb[last_valid_data_pos + 4], cdbufferb[last_valid_data_pos + 5], cdbufferb[last_valid_data_pos + 6], cdbufferb[last_valid_data_pos + 7]);
	}

	return 1;
}

int cdrom_read_blocks(uint8_t id, uint32_t *len, int first_batch)
{
	cdrom_t *dev = cdrom[id];

	int ret = 0;

	int msf = 0;
	
	int type = 0;
	int flags = 0;
	
	if (dev->current_cdb[0] == 0xb9)
		msf = 1;
	
	if ((dev->current_cdb[0] == 0xb9) || (dev->current_cdb[0] == 0xbe)) {
		type = (dev->current_cdb[1] >> 2) & 7;
		flags = dev->current_cdb[9] | (((uint32_t) dev->current_cdb[10]) << 8);
	} else {
		type = 8;
		flags = 0x10;
	}

	dev->data_pos = 0;

	if (!dev->sector_len) {
		cdrom_command_complete(id);
		return -1;
	}

	cdrom_log("Reading %i blocks starting from %i...\n", dev->requested_blocks, dev->sector_pos);

	cdrom_update_cdb(dev->current_cdb, dev->sector_pos, dev->requested_blocks);

	ret = cdrom_read_data(id, msf, type, flags, len);

	cdrom_log("Read %i bytes of blocks...\n", *len);

	if (!ret || ((dev->old_len != *len) && !first_batch)) {
		if ((dev->old_len != *len) && !first_batch)
			cdrom_illegal_mode(id);

		return 0;
	}

	dev->sector_pos += dev->requested_blocks;
	dev->sector_len -= dev->requested_blocks;

	return 1;
}

/*SCSI Get Configuration*/
/*SCSI Read DVD Structure*/
static int cdrom_read_dvd_structure(uint8_t id, int format, const uint8_t *packet, uint8_t *buf)
{
	int layer = packet[6];
	uint64_t total_sectors;

	switch (format) {
        	case 0x00:	/* Physical format information */
			total_sectors = (uint64_t) cdrom_drives[id].handler->size(id);

		        if (layer != 0) {
				cdrom_invalid_field(id);
				return 0;
			}

                	total_sectors >>= 2;
			if (total_sectors == 0) {
				/* return -ASC_MEDIUM_NOT_PRESENT; */
				cdrom_not_ready(id);
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
			cdrom_invalid_field(id);
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
			cdrom_invalid_field(id);
			return 0;
	}
}

void cdrom_insert(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	dev->unit_attention = 1;
}

/*SCSI Sense Initialization*/
void cdrom_sense_code_ok(uint8_t id)
{	
	cdrom_sense_key = SENSE_NONE;
	cdrom_asc = 0;
	cdrom_ascq = 0;
}

int cdrom_pre_execution_check(uint8_t id, uint8_t *cdb)
{
	cdrom_t *dev = cdrom[id];

	int ready = 0;

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
		if (((dev->request_length >> 5) & 7) != cdrom_drives[id].scsi_device_lun) {
			cdrom_log("CD-ROM %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", id, ((dev->request_length >> 5) & 7));
			cdrom_invalid_lun(id);
			return 0;
		}
	}

	if (!(cdrom_command_flags[cdb[0]] & IMPLEMENTED)) {
		cdrom_log("CD-ROM %i: Attempting to execute unknown command %02X over %s\n", id, cdb[0], (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) ? "SCSI" : ((cdrom_drives[id].bus_type == CDROM_BUS_ATAPI_PIO_AND_DMA) ? "ATAPI PIO/DMA" : "ATAPI PIO"));

		cdrom_illegal_opcode(id);
		return 0;
	}

	if ((cdrom_drives[id].bus_type < CDROM_BUS_SCSI) && (cdrom_command_flags[cdb[0]] & SCSI_ONLY)) {
		cdrom_log("CD-ROM %i: Attempting to execute SCSI-only command %02X over ATAPI\n", id, cdb[0]);
		cdrom_illegal_opcode(id);
		return 0;
	}

	if ((cdrom_drives[id].bus_type == CDROM_BUS_SCSI) && (cdrom_command_flags[cdb[0]] & ATAPI_ONLY)) {
		cdrom_log("CD-ROM %i: Attempting to execute ATAPI-only command %02X over SCSI\n", id, cdb[0]);
		cdrom_illegal_opcode(id);
		return 0;
	}

	if ((cdrom_drives[id].handler->status(id) == CD_STATUS_PLAYING) || (cdrom_drives[id].handler->status(id) == CD_STATUS_PAUSED)) {
		ready = 1;
		goto skip_ready_check;
	}

	if (cdrom_drives[id].handler->medium_changed(id))
		cdrom_insert(id);

	ready = cdrom_drives[id].handler->ready(id);

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
		if (!(cdrom_command_flags[cdb[0]] & ALLOW_UA)) {
			/* cdrom_log("CD-ROM %i: Unit attention now 2\n", id); */
			dev->unit_attention = 2;
			cdrom_log("CD-ROM %i: UNIT ATTENTION: Command %02X not allowed to pass through\n", id, cdb[0]);
			cdrom_unit_attention(id);
			return 0;
		}
	}
	else if (dev->unit_attention == 2) {
		if (cdb[0] != GPCMD_REQUEST_SENSE) {
			/* cdrom_log("CD-ROM %i: Unit attention now 0\n", id); */
			dev->unit_attention = 0;
		}
	}

	/* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
		the UNIT ATTENTION condition if it's set. */
	if (cdb[0] != GPCMD_REQUEST_SENSE)
		cdrom_sense_clear(id, cdb[0]);

	/* Next it's time for NOT READY. */
	if (!ready)
		dev->media_status = MEC_MEDIA_REMOVAL;
	else
		dev->media_status = (dev->unit_attention) ? MEC_NEW_MEDIA : MEC_NO_CHANGE;

	if ((cdrom_command_flags[cdb[0]] & CHECK_READY) && !ready) {
		cdrom_log("CD-ROM %i: Not ready (%02X)\n", id, cdb[0]);
		cdrom_not_ready(id);
		return 0;
	}

	cdrom_log("CD-ROM %i: Continuing with command %02X\n", id, cdb[0]);
		
	return 1;
}

void cdrom_clear_callback(uint8_t channel)
{
	cdrom_t *dev;

	uint8_t id = atapi_cdrom_drives[channel];

	dev = cdrom[id];

	if (id < CDROM_NUM)
	{
		dev->callback = 0LL;
		cdrom_set_callback(id);
	}
}

static void cdrom_seek(uint8_t id, uint32_t pos)
{
	cdrom_t *dev = cdrom[id];

        /* cdrom_log("CD-ROM %i: Seek %08X\n", id, pos); */
        dev->seek_pos   = pos;
	if (cdrom_drives[id].handler->stop)
        	cdrom_drives[id].handler->stop(id);
}

static void cdrom_rezero(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	if (cdrom_drives[id].handler->stop)
		cdrom_drives[id].handler->stop(id);
	dev->sector_pos = dev->sector_len = 0;
	cdrom_seek(id, 0);
}

void cdrom_reset(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	cdrom_rezero(id);
	dev->status = 0;
	dev->callback = 0LL;
	cdrom_set_callback(id);
	dev->packet_status = 0xff;
	dev->unit_attention = 0;
}

int cdrom_playing_completed(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	dev->prev_status = dev->cd_status;
	dev->cd_status = cdrom_drives[id].handler->status(id);
	if (((dev->prev_status == CD_STATUS_PLAYING) || (dev->prev_status == CD_STATUS_PAUSED)) && ((dev->cd_status != CD_STATUS_PLAYING) && (dev->cd_status != CD_STATUS_PAUSED)))
		return 1;
	else
		return 0;
}

void cdrom_request_sense(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{				
	cdrom_t *dev = cdrom[id];

	/*Will return 18 bytes of 0*/
	if (alloc_length != 0) {
		memset(buffer, 0, alloc_length);
		memcpy(buffer, dev->sense, alloc_length);
	}

	buffer[0] = 0x70;

	if ((cdrom_sense_key > 0) && ((dev->cd_status < CD_STATUS_PLAYING) || (dev->cd_status == CD_STATUS_STOPPED)) && cdrom_playing_completed(id)) {
		buffer[2]=SENSE_ILLEGAL_REQUEST;
		buffer[12]=ASC_AUDIO_PLAY_OPERATION;
		buffer[13]=ASCQ_AUDIO_PLAY_OPERATION_COMPLETED;
	}
	else if ((cdrom_sense_key == 0) && (dev->cd_status >= CD_STATUS_PLAYING) && (dev->cd_status != CD_STATUS_STOPPED)) {
		buffer[2]=SENSE_ILLEGAL_REQUEST;
		buffer[12]=ASC_AUDIO_PLAY_OPERATION;
		buffer[13]=(dev->cd_status == CD_STATUS_PLAYING) ? ASCQ_AUDIO_PLAY_OPERATION_IN_PROGRESS : ASCQ_AUDIO_PLAY_OPERATION_PAUSED;
	}
	else {
		if (dev->unit_attention && (cdrom_sense_key == 0)) {
			buffer[2]=SENSE_UNIT_ATTENTION;
			buffer[12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
			buffer[13]=0;
		}
	}

	cdrom_log("CD-ROM %i: Reporting sense: %02X %02X %02X\n", id, buffer[2], buffer[12], buffer[13]);

	if (buffer[2] == SENSE_UNIT_ATTENTION) {
		/* If the last remaining sense is unit attention, clear
		   that condition. */
		dev->unit_attention = 0;
	}

	/* Clear the sense stuff as per the spec. */
	cdrom_sense_clear(id, GPCMD_REQUEST_SENSE);
}

void cdrom_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{
	cdrom_t *dev = cdrom[id];

	int ready = 0;

	if (cdrom_drives[id].handler->medium_changed(id))
		cdrom_insert(id);

	ready = cdrom_drives[id].handler->ready(id);

	if (!ready && dev->unit_attention) {
		/* If the drive is not ready, there is no reason to keep the
		   UNIT ATTENTION condition present, as we only use it to mark
		   disc changes. */
		dev->unit_attention = 0;
	}

	/* Do *NOT* advance the unit attention phase. */

	cdrom_request_sense(id, buffer, alloc_length);
}

void cdrom_set_buf_len(uint8_t id, int32_t *BufLen, uint32_t *src_len)
{
	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
		if (*BufLen == -1)
			*BufLen = *src_len;
		else {
			*BufLen = MIN(*src_len, *BufLen);
			*src_len = *BufLen;
		}
		cdrom_log("CD-ROM %i: Actual transfer length: %i\n", id, *BufLen);
	}
}

void cdrom_buf_alloc(uint8_t id, uint32_t len)
{
	cdrom_t *dev = cdrom[id];

	cdrom_log("CD-ROM %i: Allocated buffer length: %i\n", id, len);
	cdbufferb = (uint8_t *) malloc(len);
}

void cdrom_buf_free(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	if (cdbufferb) {
		cdrom_log("CD-ROM %i: Freeing buffer...\n", id);
		free(cdbufferb);
		cdbufferb = NULL;
	}
}

void cdrom_command(uint8_t id, uint8_t *cdb)
{
	uint32_t len;
	int msf;
	int pos=0;
	uint32_t max_len;
	uint32_t feature;
	uint32_t used_len;
	unsigned idx = 0;
	unsigned size_idx;
	unsigned preamble_len;
	int toc_format;
	uint32_t alloc_length;
	int block_desc = 0;
	int format = 0;
	int ret;
	int real_pos;
	int track = 0;
	char device_identify[9] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', 0 };
	char device_identify_ex[15] = { '8', '6', 'B', '_', 'C', 'D', '0', '0', ' ', 'v', '1', '.', '0', '0', 0 };
	int32_t blen = 0;
	int32_t *BufLen;
	uint8_t *b;
	uint32_t profiles[2] = { MMC_PROFILE_CD_ROM, MMC_PROFILE_DVD_ROM };
	uint32_t i = 0;

	cdrom_t *dev = cdrom[id];

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
		BufLen = &SCSIDevices[cdrom_drives[id].scsi_device_id][cdrom_drives[id].scsi_device_lun].BufferLength;
		dev->status &= ~ERR_STAT;
	} else {
		BufLen = &blen;
		dev->error = 0;
	}

	dev->packet_len = 0;
	dev->request_pos = 0;

	device_identify[7] = id + 0x30;

	device_identify_ex[7] = id + 0x30;
	device_identify_ex[10] = EMU_VERSION[0];
	device_identify_ex[12] = EMU_VERSION[2];
	device_identify_ex[13] = EMU_VERSION[3];
	
	dev->data_pos = 0;

	memcpy(dev->current_cdb, cdb, dev->cdb_len);

	dev->cd_status = cdrom_drives[id].handler->status(id);

	if (cdb[0] != 0) {
		cdrom_log("CD-ROM %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, Unit attention: %i\n", id, cdb[0], cdrom_sense_key, cdrom_asc, cdrom_ascq, dev->unit_attention);
		cdrom_log("CD-ROM %i: Request length: %04X\n", id, dev->request_length);

		cdrom_log("CD-ROM %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", id,
			  cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
			  cdb[8], cdb[9], cdb[10], cdb[11]);
	}
	
	msf = cdb[1] & 2;
	dev->sector_len = 0;

	cdrom_set_phase(id, SCSI_PHASE_STATUS);

	/* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
	if (cdrom_pre_execution_check(id, cdb) == 0)
		return;

	switch (cdb[0]) {
		case GPCMD_TEST_UNIT_READY:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
			cdrom_command_complete(id);
			break;

		case GPCMD_REZERO_UNIT:
			if (cdrom_drives[id].handler->stop)
				cdrom_drives[id].handler->stop(id);
			dev->sector_pos = dev->sector_len = 0;
			cdrom_seek(id, 0);
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
			break;

		case GPCMD_REQUEST_SENSE:
			/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
			   should forget about the not ready, and report unit attention straight away. */
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
			max_len = cdb[4];
			cdrom_buf_alloc(id, 256);
			cdrom_set_buf_len(id, BufLen, &max_len);
			cdrom_request_sense(id, cdbufferb, max_len);
			cdrom_data_command_finish(id, 18, 18, cdb[4], 0);
			break;

		case GPCMD_SET_SPEED:
		case GPCMD_SET_SPEED_ALT:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
			cdrom_command_complete(id);
			break;

		case GPCMD_MECHANISM_STATUS:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
			len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

			cdrom_buf_alloc(id, 8);

			cdrom_set_buf_len(id, BufLen, &len);

 			memset(cdbufferb, 0, 8);
			cdbufferb[5] = 1;

			cdrom_data_command_finish(id, 8, 8, len, 0);
			break;

		case GPCMD_READ_TOC_PMA_ATIP:
			dev->toctimes++;

			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
			
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];

			cdrom_buf_alloc(id, 65536);

			if (cdrom_drives[id].handler->pass_through) {
				ret = cdrom_pass_through(id, &len, dev->current_cdb, cdbufferb);
				if (!ret) {
					cdrom_sense_key = cdrom_asc = cdrom_ascq = 0;
					goto cdrom_readtoc_fallback;
				}
				alloc_length = cdbufferb[0];
				alloc_length <<= 8;
				alloc_length |= cdbufferb[1];
				alloc_length += 2;
				len = MIN(alloc_length, len);

				cdrom_set_buf_len(id, BufLen, &len);
			} else {
cdrom_readtoc_fallback:
				toc_format = cdb[2] & 0xf;

				if (toc_format == 0)
					toc_format = (cdb[9] >> 6) & 3;

				switch (toc_format) {
					case 0: /*Normal*/
						len = cdrom_drives[id].handler->readtoc(id, cdbufferb, cdb[6], msf, max_len, 0);
						break;
					case 1: /*Multi session*/
						len = cdrom_drives[id].handler->readtoc_session(id, cdbufferb, msf, max_len);
						cdbufferb[0] = 0; cdbufferb[1] = 0xA;
						break;
					case 2: /*Raw*/
						len = cdrom_drives[id].handler->readtoc_raw(id, cdbufferb, max_len);
						break;
					default:
						cdrom_invalid_field(id);
						cdrom_buf_free(id);
						return;
				}
			}

			if (len > max_len) {
				len = max_len;

				cdbufferb[0] = ((len - 2) >> 8) & 0xff;
				cdbufferb[1] = (len - 2) & 0xff;
			}

			cdrom_set_buf_len(id, BufLen, &len);

			if (len >= 8) {
				cdrom_log("CD-ROM %i: TOC: %02X %02X %02X %02X %02X %02X %02X %02X\n", id,
					  cdbufferb[0], cdbufferb[1], cdbufferb[2], cdbufferb[3],
					  cdbufferb[4], cdbufferb[5], cdbufferb[6], cdbufferb[7]);
			}

			if (len >= 16) {
				cdrom_log("               %02X %02X %02X %02X %02X %02X %02X %02X\n",
					  cdbufferb[8], cdbufferb[9], cdbufferb[10], cdbufferb[11],
					  cdbufferb[12], cdbufferb[13], cdbufferb[14], cdbufferb[15]);
			}

			if (len >= 24) {
				cdrom_log("               %02X %02X %02X %02X %02X %02X %02X %02X\n",
					  cdbufferb[16], cdbufferb[17], cdbufferb[18], cdbufferb[19],
					  cdbufferb[20], cdbufferb[21], cdbufferb[22], cdbufferb[23]);
			}

			if (len >= 32) {
				cdrom_log("               %02X %02X %02X %02X %02X %02X %02X %02X\n",
					  cdbufferb[24], cdbufferb[25], cdbufferb[26], cdbufferb[27],
					  cdbufferb[28], cdbufferb[29], cdbufferb[30], cdbufferb[31]);
			}

			if (len >= 36) {
				cdrom_log("               %02X %02X %02X %02X\n",
					  cdbufferb[32], cdbufferb[33], cdbufferb[34], cdbufferb[35]);
			}

			cdrom_data_command_finish(id, len, len, len, 0);
			/* cdrom_log("CD-ROM %i: READ_TOC_PMA_ATIP format %02X, length %i (%i)\n", id, toc_format, ide->cylinder, cdbufferb[1]); */
			return;
                
		case GPCMD_READ_CD_OLD:
			dev->current_cdb[0] = 0xbe;		/* IMPORTANT: Convert the command to new read CD for pass through purposes. */
		case GPCMD_READ_6:
		case GPCMD_READ_10:
		case GPCMD_READ_12:
		case GPCMD_READ_CD:
		case GPCMD_READ_CD_MSF:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
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
					cdrom_log("CD-ROM %i: Length: %i, LBA: %i\n", id, dev->sector_len, dev->sector_pos);
					msf = 0;
					break;
				case GPCMD_READ_12:
					dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
					dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
					cdrom_log("CD-ROM %i: Length: %i, LBA: %i\n", id, dev->sector_len, dev->sector_pos);
					msf = 0;
					break;
				case GPCMD_READ_CD_MSF:
					/* cdrom_log("CD-ROM %i: Read CD MSF: Start MSF %02X%02X%02X End MSF %02X%02X%02X Flags %02X\n", id, cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]); */
					alloc_length = 2856;
					dev->sector_len = MSFtoLBA(cdb[6], cdb[7], cdb[8]);
					dev->sector_pos = MSFtoLBA(cdb[3], cdb[4], cdb[5]);

					dev->sector_len -= dev->sector_pos;
					dev->sector_len++;
					msf = 1;
					break;
				case GPCMD_READ_CD_OLD:
				case GPCMD_READ_CD:
					/* cdrom_log("CD-ROM %i: Read CD: Start LBA %02X%02X%02X%02X Length %02X%02X%02X Flags %02X\n", id, cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]); */
					alloc_length = 2856;
					dev->sector_len = (cdb[6] << 16) | (cdb[7] << 8) | cdb[8];
					dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];

					msf = 0;
					break;
			}

			dev->seek_diff = ABS((int) (pos - dev->seek_pos));
			dev->seek_pos = dev->sector_pos;

			if (!dev->sector_len) {
				cdrom_set_phase(id, SCSI_PHASE_STATUS);
				/* cdrom_log("CD-ROM %i: All done - callback set\n", id); */
				dev->packet_status = CDROM_PHASE_COMPLETE;
				dev->callback = 20LL * CDROM_TIME;
				cdrom_set_callback(id);
				break;
			}

			max_len = dev->sector_len;
			dev->requested_blocks = max_len;	/* If we're reading all blocks in one go for DMA, why not also for PIO, it should NOT
								   matter anyway, this step should be identical and only the way the read dat is
								   transferred to the host should be different. */

			dev->packet_len = max_len * alloc_length;
			cdrom_buf_alloc(id, dev->packet_len);

			ret = cdrom_read_blocks(id, &alloc_length, 1);
			if (ret <= 0) {
				cdrom_buf_free(id);
				return;
			}

			dev->requested_blocks = max_len;
			dev->packet_len = alloc_length;

			cdrom_set_buf_len(id, BufLen, &dev->packet_len);

			cdrom_data_command_finish(id, alloc_length, alloc_length / dev->requested_blocks, alloc_length, 0);

			dev->all_blocks_total = dev->block_total;
			if (dev->packet_status != CDROM_PHASE_COMPLETE)
				ui_sb_update_icon(SB_CDROM | id, 1);
			else
				ui_sb_update_icon(SB_CDROM | id, 0);
			return;

		case GPCMD_READ_HEADER:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);

			alloc_length = ((cdb[7] << 8) | cdb[8]);
			cdrom_buf_alloc(id, 8);

			if (cdrom_drives[id].handler->pass_through) {
				ret = cdrom_pass_through(id, &len, dev->current_cdb, cdbufferb);
				if (!ret) {
					cdrom_buf_free(id);
					return;
				}
			} else {
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
			}

			len = MIN(len, alloc_length);

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			return;

		case GPCMD_MODE_SENSE_6:
		case GPCMD_MODE_SENSE_10:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
		
			if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
				block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;
			else
				block_desc = 0;

			if (cdb[0] == GPCMD_MODE_SENSE_6) {
				len = cdb[4];
				cdrom_buf_alloc(id, 256);
			} else {
				len = (cdb[8] | (cdb[7] << 8));
				cdrom_buf_alloc(id, 65536);
			}

			dev->current_page_code = cdb[2] & 0x3F;

			if (!(cdrom_mode_sense_page_flags & (1LL << dev->current_page_code))) {
				cdrom_invalid_field(id);
				cdrom_buf_free(id);
				return;
			}
			
			memset(cdbufferb, 0, len);
			alloc_length = len;

			if (cdb[0] == GPCMD_MODE_SENSE_6) {
				len = cdrom_mode_sense(id, cdbufferb, 4, cdb[2], block_desc);
				len = MIN(len, alloc_length);
				cdbufferb[0] = len - 1;
				cdbufferb[1] = cdrom_drives[id].handler->media_type_id(id);
				if (block_desc)
					cdbufferb[3] = 8;
			} else {
				len = cdrom_mode_sense(id, cdbufferb, 8, cdb[2], block_desc);
				len = MIN(len, alloc_length);
				cdbufferb[0]=(len - 2) >> 8;
				cdbufferb[1]=(len - 2) & 255;
				cdbufferb[2] = cdrom_drives[id].handler->media_type_id(id);
				if (block_desc) {
					cdbufferb[6] = 0;
					cdbufferb[7] = 8;
				}
			}

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_log("CD-ROM %i: Reading mode page: %02X...\n", id, cdb[2]);

			cdrom_data_command_finish(id, len, len, alloc_length, 0);
			return;

		case GPCMD_MODE_SELECT_6:
		case GPCMD_MODE_SELECT_10:
			cdrom_set_phase(id, SCSI_PHASE_DATA_OUT);
		
			if (cdb[0] == GPCMD_MODE_SELECT_6) {
				len = cdb[4];
				cdrom_buf_alloc(id, 256);
			} else {
				len = (cdb[7] << 8) | cdb[8];
				cdrom_buf_alloc(id, 65536);
			}

			cdrom_set_buf_len(id, BufLen, &len);

			dev->total_length = len;
			dev->do_page_save = cdb[1] & 1;

			dev->current_page_pos = 0;

			cdrom_data_command_finish(id, len, len, len, 1);
			return;

		case GPCMD_GET_CONFIGURATION:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);

			/* XXX: could result in alignment problems in some architectures */
			feature = (cdb[2] << 8) | cdb[3];
			max_len = (cdb[7] << 8) | cdb[8];

			/* only feature 0 is supported */
			if ((cdb[2] != 0) || (cdb[3] > 2)) {
				cdrom_invalid_field(id);
				cdrom_buf_free(id);
				return;
			}

			cdrom_buf_alloc(id, 65536);
			memset(cdbufferb, 0, max_len);

			alloc_length = 0;
			b = cdbufferb;

			/*
			 * the number of sectors from the media tells us which profile
			 * to use as current.  0 means there is no media
			 */
			if (cdrom_drives[id].handler->ready(id)) {
				len = cdrom_drives[id].handler->size(id);
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

				if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
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

			cdrom_set_buf_len(id, BufLen, &alloc_length);

			cdrom_data_command_finish(id, alloc_length, alloc_length, alloc_length, 0);
			break;

		case GPCMD_GET_EVENT_STATUS_NOTIFICATION:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);

			cdrom_buf_alloc(id, 8 + sizeof(gesn_event_header));
		
			gesn_cdb = (void *) cdb;
			gesn_event_header = (void *) cdbufferb;

			/* It is fine by the MMC spec to not support async mode operations. */
			if (!(gesn_cdb->polled & 0x01)) {
				/* asynchronous mode */
				/* Only polling is supported, asynchronous mode is not. */
				cdrom_invalid_field(id);
				cdrom_buf_free(id);
				return;
			}

			/* polling mode operation */

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

			cdrom_set_buf_len(id, BufLen, &used_len);

			cdrom_data_command_finish(id, used_len, used_len, used_len, 0);
			break;

		case GPCMD_READ_DISC_INFORMATION:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
			
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];

			cdrom_buf_alloc(id, 65536);

			if (cdrom_drives[id].handler->pass_through) {
				ret = cdrom_pass_through(id, &len, dev->current_cdb, cdbufferb);
				if (!ret) {
					cdrom_buf_free(id);
					return;
				}
				alloc_length = cdbufferb[0];
				alloc_length <<= 8;
				alloc_length |= cdbufferb[1];
				alloc_length += 2;
				len = alloc_length;
			} else {
				memset(cdbufferb, 0, 34);
				memset(cdbufferb, 1, 9);
				cdbufferb[0] = 0;
				cdbufferb[1] = 32;
				cdbufferb[2] = 0xe; /* last session complete, disc finalized */
				cdbufferb[7] = 0x20; /* unrestricted use */
				cdbufferb[8] = 0x00; /* CD-ROM */
				len=34;
			}

			len = MIN(len, max_len);

			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			break;

		case GPCMD_READ_TRACK_INFORMATION:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
		
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];

			cdrom_buf_alloc(id, 65536);

			track = ((uint32_t) cdb[2]) << 24;
			track |= ((uint32_t) cdb[3]) << 16;
			track |= ((uint32_t) cdb[4]) << 8;
			track |= (uint32_t) cdb[5];

			if (cdrom_drives[id].handler->pass_through) {
				ret = cdrom_pass_through(id, &len, dev->current_cdb, cdbufferb);
				if (!ret) {
					cdrom_buf_free(id);
					return;
				}
				alloc_length = cdbufferb[0];
				alloc_length <<= 8;
				alloc_length |= cdbufferb[1];
				alloc_length += 2;
				len = MIN(len, alloc_length);
			} else {
				if (((cdb[1] & 0x03) != 1) || (track != 1)) {
					cdrom_invalid_field(id);
					cdrom_buf_free(id);
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
				cdbufferb[24] = (cdrom_drives[id].handler->size(id) >> 24) & 0xff; /* track size */
				cdbufferb[25] = (cdrom_drives[id].handler->size(id) >> 16) & 0xff; /* track size */
				cdbufferb[26] = (cdrom_drives[id].handler->size(id) >> 8) & 0xff; /* track size */
				cdbufferb[27] = cdrom_drives[id].handler->size(id) & 0xff; /* track size */

				if (len > max_len) {
					len = max_len;
					cdbufferb[0] = ((max_len - 2) >> 8) & 0xff;
					cdbufferb[1] = (max_len - 2) & 0xff;
				}
			}
		
			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, max_len, 0);
			break;

		case GPCMD_PLAY_AUDIO_10:
		case GPCMD_PLAY_AUDIO_12:
		case GPCMD_PLAY_AUDIO_MSF:
		case GPCMD_PLAY_AUDIO_TRACK_INDEX:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
		
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

			if (!cdrom_drives[id].handler->is_track_audio)
				break;

			if ((cdrom_drives[id].host_drive < 1) || (dev->cd_status <= CD_STATUS_DATA_ONLY) || !cdrom_drives[id].handler->is_track_audio(id, pos, msf)) {
				cdrom_illegal_mode(id);
				break;
			}

			if (cdrom_drives[id].handler->playaudio)
				cdrom_drives[id].handler->playaudio(id, pos, len, msf);
			else {
				cdrom_illegal_mode(id);
				break;
			}

			cdrom_command_complete(id);
			break;

		case GPCMD_READ_SUBCHANNEL:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
		
			max_len = cdb[7];
			max_len <<= 8;
			max_len |= cdb[8];
			msf = (cdb[1] >> 1) & 1;

			cdrom_buf_alloc(id, 65536);

			cdrom_log("CD-ROM %i: Getting page %i (%s)\n", id, cdb[3], msf ? "MSF" : "LBA");
			if ((cdrom_drives[id].handler->pass_through) && (cdb[3] != 1)) {
				ret = cdrom_pass_through(id, &len, dev->current_cdb, cdbufferb);
				if (!ret) {
					cdrom_buf_free(id);
					return;
				}
				switch(dev->cd_status) {
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
				if (!(cdb[2] & 0x40) || (cdb[3] == 0))
					len = 4;
				else
					len = alloc_length;
			} else {
				if (cdb[3] > 3) {
					/* cdrom_log("CD-ROM %i: Read subchannel check condition %02X\n", id, cdb[3]); */
					cdrom_invalid_field(id);
					cdrom_buf_free(id);
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
					cdbufferb[1] = cdrom_drives[id].handler->getcurrentsubchannel(id, &cdbufferb[5], msf);
					switch(dev->cd_status) {
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
			}

			len = MIN(len, max_len);
			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			break;

		case GPCMD_READ_DVD_STRUCTURE:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);

			alloc_length = (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);

			cdrom_buf_alloc(id, alloc_length);

			if (cdrom_drives[id].handler->pass_through) {
				ret = cdrom_pass_through(id, &len, dev->current_cdb, cdbufferb);
				if (!ret) {
					cdrom_buf_free(id);
					return;
				} else {
					if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
						if (*BufLen == -1)
							*BufLen = len;
						else {
							*BufLen = MIN(len, *BufLen);
							len = *BufLen;
						}
					}
				}
			} else {
				len = cdrom_drives[id].handler->size(id);

 				if (cdb[7] < 0xc0) {
					if (len <= CD_MAX_SECTORS) {
						cdrom_incompatible_format(id);
						cdrom_buf_free(id);
						return;
					}
				}

				memset(cdbufferb, 0, alloc_length);

				if (((cdb[7] >= 0x00) && (cdb[7] <= 0x7f)) || (cdb[7] == 0xff)) {
					if (cdb[1] == 0) {
						ret = cdrom_read_dvd_structure(id, format, cdb, cdbufferb);

						if (ret) {
							cdrom_set_buf_len(id, BufLen, &alloc_length);
							cdrom_data_command_finish(id, alloc_length, alloc_length, len, 0);
						} else
							cdrom_buf_free(id);
						return;
					}
				} else {
					cdrom_invalid_field(id);
					cdrom_buf_free(id);
					return;
				}
			}
			break;

		case GPCMD_START_STOP_UNIT:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
		
			switch(cdb[4] & 3) {
				case 0:		/* Stop the disc. */
					if (cdrom_drives[id].handler->stop)
						cdrom_drives[id].handler->stop(id);
					break;
				case 1:		/* Start the disc and read the TOC. */
					cdrom_drives[id].handler->medium_changed(id);	/* This causes a TOC reload. */
					break;
				case 2:		/* Eject the disc if possible. */
					if (cdrom_drives[id].handler->stop)
						cdrom_drives[id].handler->stop(id);
					cdrom_eject(id);
					break;
				case 3:		/* Load the disc (close tray). */
					cdrom_reload(id);
					break;
			}

			cdrom_command_complete(id);
			break;
                
		case GPCMD_INQUIRY:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);
		
			max_len = cdb[3];
			max_len <<= 8;
			max_len |= cdb[4];

			cdrom_buf_alloc(id, 65536);

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
							cdrom_data_phase_error(id);
							cdrom_buf_free(id);
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
						cdrom_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
						cdrom_invalid_field(id);
						cdrom_buf_free(id);
						return;
				}
			} else {
				preamble_len = 5;
				size_idx = 4;

				memset(cdbufferb, 0, 8);
				cdbufferb[0] = 5; /*CD-ROM*/
				cdbufferb[1] = 0x80; /*Removable*/
				cdbufferb[2] = (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) ? 0x02 : 0x00; /*SCSI-2 compliant*/
				cdbufferb[3] = (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) ? 0x12 : 0x21;
				cdbufferb[4] = 31;
				if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
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
			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, max_len, 0);
			break;

		case GPCMD_PREVENT_REMOVAL:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
			cdrom_command_complete(id);
			break;

		case GPCMD_PAUSE_RESUME_ALT:
		case GPCMD_PAUSE_RESUME:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
		
			if (cdb[8] & 1) {
				if (cdrom_drives[id].handler->resume)
					cdrom_drives[id].handler->resume(id);
				else {
					cdrom_illegal_mode(id);
					break;
				}
			} else {
				if (cdrom_drives[id].handler->pause)
					cdrom_drives[id].handler->pause(id);
				else {
					cdrom_illegal_mode(id);
					break;
				}
			}
			cdrom_command_complete(id);
			break;

		case GPCMD_SEEK_6:
		case GPCMD_SEEK_10:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
		
			switch(cdb[0]) {
				case GPCMD_SEEK_6:
					pos = (cdb[2] << 8) | cdb[3];
					break;
				case GPCMD_SEEK_10:
					pos = (cdb[2] << 24) | (cdb[3]<<16) | (cdb[4]<<8) | cdb[5];
					break;
			}
			dev->seek_diff = ABS((int) (pos - dev->seek_pos));
			cdrom_seek(id, pos);
			cdrom_command_complete(id);
			break;

		case GPCMD_READ_CDROM_CAPACITY:
			cdrom_set_phase(id, SCSI_PHASE_DATA_IN);

			cdrom_buf_alloc(id, 8);

			if (cdrom_read_capacity(id, dev->current_cdb, cdbufferb, &len) == 0) {
				cdrom_buf_free(id);
				return;
			}
			
			cdrom_set_buf_len(id, BufLen, &len);

			cdrom_data_command_finish(id, len, len, len, 0);
			break;

		case GPCMD_STOP_PLAY_SCAN:
			cdrom_set_phase(id, SCSI_PHASE_STATUS);
		
			if (cdrom_drives[id].handler->stop)
				cdrom_drives[id].handler->stop(id);
			else {
				cdrom_illegal_mode(id);
				break;
			}
			cdrom_command_complete(id);
			break;

		default:
			cdrom_illegal_opcode(id);
			break;
	}

	/* cdrom_log("CD-ROM %i: Phase: %02X, request length: %i\n", dev->phase, dev->request_length); */

	if (cdrom_atapi_phase_to_scsi(id) == SCSI_PHASE_STATUS)
		cdrom_buf_free(id);
}

/* The command second phase function, needed for Mode Select. */
uint8_t cdrom_phase_data_out(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	uint16_t block_desc_len;
	uint16_t pos;

	uint8_t error = 0;
	uint8_t page, page_len;

	uint16_t i = 0;

	uint8_t hdr_len, val, old_val, ch;

	FILE *f;

	switch(dev->current_cdb[0]) {
		case GPCMD_MODE_SELECT_6:
		case GPCMD_MODE_SELECT_10:
			f = nvr_fopen(L"modeselect.bin", L"wb");
			fwrite(cdbufferb, 1, dev->total_length, f);
			fclose(f);

			if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10)
				hdr_len = 8;
			else
				hdr_len = 4;

			if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
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

				if (!(cdrom_mode_sense_page_flags & (1LL << ((uint64_t) page)))) {
					cdrom_log("Unimplemented page %02X\n", page);
					error |= 1;
				} else {
					for (i = 0; i < page_len; i++) {
						ch = cdrom_mode_sense_pages_changeable.pages[page][i + 2];
						val = cdbufferb[pos + i];
						old_val = cdrom_mode_sense_pages_saved[id].pages[page][i + 2];
						if (val != old_val) {
							if (ch)
								cdrom_mode_sense_pages_saved[id].pages[page][i + 2] = val;
							else {
								cdrom_log("Unchangeable value on position %02X on page %02X\n", i + 2, page);
								error |= 1;
							}
						}
					}
				}

				pos += page_len;

				if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
					val = cdrom_mode_sense_pages_default_scsi.pages[page][0] & 0x80;
				else
					val = cdrom_mode_sense_pages_default.pages[page][0] & 0x80;
				if (dev->do_page_save && val)
					cdrom_mode_sense_save(id);

				if (pos >= dev->total_length)
					break;
			}

			if (error) {
				cdrom_invalid_field_pl(id);
				return 0;
			}
			break;
	}

	return 1;
}

/* This is the general ATAPI PIO request function. */
void cdrom_pio_request(uint8_t id, uint8_t out)
{
	cdrom_t *dev = cdrom[id];

	int old_pos = 0;
	int ret = 0;

	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI) {
		cdrom_log("CD-ROM %i: Lowering IDE IRQ\n", id);
		ide_irq_lower(&(ide_drives[cdrom_drives[id].ide_channel]));
	}
	
	dev->status = BUSY_STAT;

	if (dev->pos >= dev->packet_len) {
		cdrom_log("CD-ROM %i: %i bytes %s, command done\n", id, dev->pos, out ? "written" : "read");

		dev->pos = dev->request_pos = 0;
		if (out) {
			ret = cdrom_phase_data_out(id);
			/* If ret = 0 (phase 1 error), then we do not do anything else other than
			   free the buffer, as the phase and callback have already been set by the
			   error function. */
			if (ret)
				cdrom_command_complete(id);
		} else
			cdrom_command_complete(id);
		cdrom_buf_free(id);
	} else {
		cdrom_log("CD-ROM %i: %i bytes %s, %i bytes are still left\n", id, dev->pos, out ? "written" : "read", dev->packet_len - dev->pos);

		/* Make sure to keep pos, and reset request_pos to 0. */
		/* Also make sure to not reset total_read. */

		/* If less than (packet length) bytes are remaining, update packet length
		   accordingly. */
		if ((dev->packet_len - dev->pos) < (dev->max_transfer_len))
			dev->max_transfer_len = dev->packet_len - dev->pos;
		cdrom_log("CD-ROM %i: Packet length %i, request length %i\n", id, dev->packet_len, dev->max_transfer_len);

		old_pos = dev->pos;
		dev->packet_status = out ? CDROM_PHASE_DATA_OUT : CDROM_PHASE_DATA_IN;
		cdrom_command_common(id);
		dev->pos = old_pos;

		dev->request_pos = 0;
	}
}

void cdrom_phase_callback(uint8_t id);

int cdrom_read_from_ide_dma(uint8_t channel)
{
	cdrom_t *dev;

	uint8_t id = atapi_cdrom_drives[channel];

	if (id > CDROM_NUM)
		return 0;

	dev = cdrom[id];

	if (ide_bus_master_write) {
		if (ide_bus_master_write(channel >> 1, cdbufferb, dev->packet_len))
			return 0;
		else
			return 1;
	} else
		return 0;

	return 0;
}

int cdrom_read_from_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	cdrom_t *dev;

	uint8_t id = scsi_cdrom_drives[scsi_id][scsi_lun];
	int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

	if (id > CDROM_NUM)
		return 0;

	dev = cdrom[id];

	cdrom_log("Reading from SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
	memcpy(cdbufferb, SCSIDevices[scsi_id][scsi_lun].CmdBuffer, *BufLen);
	return 1;
}

void cdrom_irq_raise(uint8_t id)
{
	if (cdrom_drives[id].bus_type < CDROM_BUS_SCSI)
		ide_irq_raise(&(ide_drives[cdrom_drives[id].ide_channel]));
}

int cdrom_read_from_dma(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	int32_t *BufLen = &SCSIDevices[cdrom_drives[id].scsi_device_id][cdrom_drives[id].scsi_device_lun].BufferLength;

	int ret = 0;

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
		ret = cdrom_read_from_scsi_dma(cdrom_drives[id].scsi_device_id, cdrom_drives[id].scsi_device_lun);
	else
		ret = cdrom_read_from_ide_dma(cdrom_drives[id].ide_channel);

	if (!ret)
		return 0;

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)
		cdrom_log("CD-ROM %i: SCSI Input data length: %i\n", id, *BufLen);
	else
		cdrom_log("CD-ROM %i: ATAPI Input data length: %i\n", id, dev->packet_len);

	ret = cdrom_phase_data_out(id);

	if (ret || (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)) {
		cdrom_buf_free(id);
		dev->packet_status = CDROM_PHASE_COMPLETE;
		dev->status = READY_STAT;
		dev->phase = 3;
		ui_sb_update_icon(SB_CDROM | id, 0);
		cdrom_irq_raise(id);
		if (ret)
			return 1;
		else
			return 0;
	} else
		return 0;
}

int cdrom_write_to_ide_dma(uint8_t channel)
{
	cdrom_t *dev;

	uint8_t id = atapi_cdrom_drives[channel];

	if (id > CDROM_NUM)
		return 0;

	dev = cdrom[id];

	if (ide_bus_master_read) {
		if (ide_bus_master_read(channel >> 1, cdbufferb, dev->packet_len))
			return 0;
		else
			return 1;
	} else
		return 0;
}

int cdrom_write_to_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	cdrom_t *dev;

	uint8_t id = scsi_cdrom_drives[scsi_id][scsi_lun];
	int32_t *BufLen = &SCSIDevices[scsi_id][scsi_lun].BufferLength;

	if (id > CDROM_NUM)
		return 0;

	dev = cdrom[id];

	cdrom_log("Writing to SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, *BufLen);
	memcpy(SCSIDevices[scsi_id][scsi_lun].CmdBuffer, cdbufferb, *BufLen);
	cdrom_log("CD-ROM %i: Data from CD buffer:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, cdbufferb[0], cdbufferb[1], cdbufferb[2], cdbufferb[3], cdbufferb[4], cdbufferb[5], cdbufferb[6], cdbufferb[7]);
	cdrom_log("CD-ROM %i: Data from SCSI DMA :  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, SCSIDevices[scsi_id][scsi_lun].CmdBuffer[0], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[1], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[2], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[3], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[4], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[5], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[6], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[7]);
	return 1;
}

int cdrom_write_to_dma(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	int ret = 0;

	if (cdrom_drives[id].bus_type == CDROM_BUS_SCSI) {
		cdrom_log("Write to SCSI DMA: (%02X:%02X)\n", cdrom_drives[id].scsi_device_id, cdrom_drives[id].scsi_device_lun);
		ret = cdrom_write_to_scsi_dma(cdrom_drives[id].scsi_device_id, cdrom_drives[id].scsi_device_lun);
	} else
		ret = cdrom_write_to_ide_dma(cdrom_drives[id].ide_channel);

	if (ret || (cdrom_drives[id].bus_type == CDROM_BUS_SCSI)) {
		cdrom_buf_free(id);
		dev->packet_status = CDROM_PHASE_COMPLETE;
		dev->status = READY_STAT;
		dev->phase = 3;
		ui_sb_update_icon(SB_CDROM | id, 0);
		cdrom_irq_raise(id);
		if (ret)
			return 1;
		else
			return 0;
	} else
		return 0;
}

/* If the result is 1, issue an IRQ, otherwise not. */
void cdrom_phase_callback(uint8_t id)
{
	cdrom_t *dev = cdrom[id];

	switch(dev->packet_status) {
		case CDROM_PHASE_IDLE:
			cdrom_log("CD-ROM %i: CDROM_PHASE_IDLE\n", id);
			dev->pos=0;
			dev->phase = 1;
			dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
			return;
		case CDROM_PHASE_COMMAND:
			cdrom_log("CD-ROM %i: CDROM_PHASE_COMMAND\n", id);
			dev->status = BUSY_STAT | (dev->status &ERR_STAT);
			memcpy(dev->atapi_cdb, cdbufferb, dev->cdb_len);
			cdrom_command(id, dev->atapi_cdb);
			return;
		case CDROM_PHASE_COMPLETE:
			cdrom_log("CD-ROM %i: CDROM_PHASE_COMPLETE\n", id);
			dev->status = READY_STAT;
			dev->phase = 3;
			dev->packet_status = 0xFF;
			ui_sb_update_icon(SB_CDROM | id, 0);
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_DATA_OUT:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_OUT\n", id);
			dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
			dev->phase = 0;
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_DATA_OUT_DMA:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_OUT_DMA\n", id);
			cdrom_read_from_dma(id);
			return;
		case CDROM_PHASE_DATA_IN:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_IN\n", id);
			dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
			dev->phase = 2;
			cdrom_irq_raise(id);
			return;
		case CDROM_PHASE_DATA_IN_DMA:
			cdrom_log("CD-ROM %i: CDROM_PHASE_DATA_IN_DMA\n", id);
			cdrom_write_to_dma(id);
			return;
		case CDROM_PHASE_ERROR:
			cdrom_log("CD-ROM %i: CDROM_PHASE_ERROR\n", id);
			dev->status = READY_STAT | ERR_STAT;
			dev->phase = 3;
			cdrom_irq_raise(id);
			ui_sb_update_icon(SB_CDROM | id, 0);
			return;
	}
}

/* Reimplement as 8-bit due to reimplementation of IDE data read and write. */
uint32_t cdrom_read(uint8_t channel, int length)
{
	cdrom_t *dev;

	uint16_t *cdbufferw;
	uint32_t *cdbufferl;

	uint8_t id = atapi_cdrom_drives[channel];

	uint32_t temp = 0;

	if (id > CDROM_NUM)
		return 0;

	dev = cdrom[id];

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

	if (dev->packet_status == CDROM_PHASE_DATA_IN) {
		if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
			/* Time for a DRQ. */
			// cdrom_log("CD-ROM %i: Issuing read callback\n", id);
			cdrom_pio_request(id, 0);
		}
		// cdrom_log("CD-ROM %i: Returning: %02X (buffer position: %i, request position: %i)\n", id, temp, dev->pos, dev->request_pos);
		return temp;
	} else {
		// cdrom_log("CD-ROM %i: Returning zero (buffer position: %i, request position: %i)\n", id, dev->pos, dev->request_pos);
		return 0;
	}
}

/* Reimplement as 8-bit due to reimplementation of IDE data read and write. */
void cdrom_write(uint8_t channel, uint32_t val, int length)
{
	cdrom_t *dev;

	uint16_t *cdbufferw;
	uint32_t *cdbufferl;

	uint8_t id = atapi_cdrom_drives[channel];

	if (id > CDROM_NUM)
		return;

	dev = cdrom[id];

	if (dev->packet_status == CDROM_PHASE_IDLE) {
		if (!cdbufferb)
			cdrom_buf_alloc(id, dev->cdb_len);
	}

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

	if (dev->packet_status == CDROM_PHASE_DATA_OUT) {
		if ((dev->request_pos >= dev->max_transfer_len) || (dev->pos >= dev->packet_len)) {
			/* Time for a DRQ. */
			cdrom_pio_request(id, 1);
		}
		return;
	} else if (dev->packet_status == CDROM_PHASE_IDLE) {
		if (dev->pos >= dev->cdb_len) {
			dev->pos=0;
			dev->status = BUSY_STAT;
			dev->packet_status = CDROM_PHASE_COMMAND;
			timer_process();
			cdrom_phase_callback(id);
			timer_update_outstanding();
		}
		return;
	}
}

/* Peform a master init on the entire module. */
void
cdrom_global_init(void)
{
    int c;

    /* Clear the global data. */
    memset(cdrom, 0x00, sizeof(cdrom));
    memset(cdrom_drives, 0x00, sizeof(cdrom_drives));

    /* Initialize the host devices, if any. */
    cdrom_init_host_drives();

    /* Set all drives to NULL mode. */
    for (c=0; c<CDROM_NUM; c++)
	cdrom_null_open(c, cdrom_drives[c].host_drive);
}


void
cdrom_hard_reset(void)
{
    int c;

    cdrom_destroy_drives();

    for (c=0; c<CDROM_NUM; c++) {
	if (cdrom_drives[c].bus_type) {
		cdrom_log("CDROM global_reset drive=%d host=%02x\n", c, cdrom_drives[c].host_drive);

		if (!cdrom[c])
			cdrom[c] = (cdrom_t *) malloc(sizeof(cdrom_t));

		SCSIReset(cdrom_drives[c].scsi_device_id, cdrom_drives[c].scsi_device_lun);

		if (cdrom_drives[c].host_drive == 200) {
			image_open(c, cdrom_image[c].image_path);
			image_reset(c);
		} else if ((cdrom_drives[c].host_drive>='A') && (cdrom_drives[c].host_drive <= 'Z')) {
			ioctl_open(c, cdrom_drives[c].host_drive);
			ioctl_reset(c);
		} else
			cdrom_null_open(c, cdrom_drives[c].host_drive);
	}

	cdrom_mode_sense_load(c);
    }
}


void
cdrom_close(uint8_t id)
{
    switch (cdrom_drives[id].host_drive) {
	case 0:
		null_close(id);
		break;
	case 200:
		image_close(id);
		break;
	default:
		ioctl_close(id);
		break;
    }
}
