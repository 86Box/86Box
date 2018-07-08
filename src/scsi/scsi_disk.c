/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of SCSI fixed disks.
 *
 * Version:	@(#)scsi_disk.c	1.0.20	2018/05/28
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2017,2018 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../timer.h"
#include "../device.h"
#include "../nvr.h"
#include "../piix.h"
#include "../disk/hdd.h"
#include "../disk/hdc.h"
#include "../disk/hdc_ide.h"
#include "../plat.h"
#include "../ui.h"
#include "scsi.h"
#include "../cdrom/cdrom.h"
#include "scsi_disk.h"


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

#define MAX_BLOCKS_AT_ONCE	340

#define scsi_disk_sense_error dev->sense[0]
#define scsi_disk_sense_key dev->sense[2]
#define scsi_disk_asc dev->sense[12]
#define scsi_disk_ascq dev->sense[13]


scsi_disk_t *scsi_disk[HDD_NUM];

uint8_t scsi_disks[16] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};


/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
const uint8_t scsi_disk_command_flags[0x100] = {
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x00 */
    IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,		/* 0x01 */
    0,
    IMPLEMENTED | ALLOW_UA,					/* 0x03 */
    IMPLEMENTED | CHECK_READY | ALLOW_UA | NONDATA | SCSI_ONLY,	/* 0x04 */
    0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x08 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x0A */
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x0B */
    0, 0, 0, 0, 0, 0,
    IMPLEMENTED | ALLOW_UA,					/* 0x12 */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x13 */
    0,
    IMPLEMENTED,						/* 0x15 */
    IMPLEMENTED | SCSI_ONLY,					/* 0x16 */
    IMPLEMENTED | SCSI_ONLY,					/* 0x17 */
    0, 0,
    IMPLEMENTED,						/* 0x1A */
    0, 0,
    IMPLEMENTED,						/* 0x1D */
    IMPLEMENTED | CHECK_READY,					/* 0x1E */
    0, 0, 0, 0, 0, 0,
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


uint64_t scsi_disk_mode_sense_page_flags = (1LL << 0x03) | (1LL << 0x04) | (1LL << 0x30) | (1LL << 0x3F);

/* This should be done in a better way but for time being, it's been done this way so it's not as huge and more readable. */
static const mode_sense_pages_t scsi_disk_mode_sense_pages_default =
{	{	[0x03] = {                     0x03, 0x16, 0,    1, 0,  1, 0, 1, 0, 1, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0 },
		[0x04] = {                     0x04, 0x16, 0, 0x10, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 },
		[0x30] = { 		       0xB0, 0x16, '8', '6', 'B', 'o', 'x', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
}	};

static const mode_sense_pages_t scsi_disk_mode_sense_pages_changeable =
{	{	[0x03] = {                     0x03, 0x16, 0,    1, 0,  1, 0, 1, 0, 1, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0 },
		[0x04] = {                     0x04, 0x16, 0, 0x10, 0, 64, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10, 0, 0, 0 },
		[0x30] = { 		       0xB0, 0x16, '8', '6', 'B', 'o', 'x', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ' }
}	};


#ifdef ENABLE_SCSI_DISK_LOG
int scsi_disk_do_log = ENABLE_SCSI_DISK_LOG;
#endif


static void
scsi_disk_log(const char *fmt, ...)
{
#ifdef ENABLE_SCSI_DISK_LOG
    va_list ap;

    if (scsi_disk_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int
scsi_disk_err_stat_to_scsi(scsi_disk_t *dev)
{
    if (dev->status & ERR_STAT)
	return SCSI_STATUS_CHECK_CONDITION;
    else
	return SCSI_STATUS_OK;
}


int
find_hdd_for_scsi_id(uint8_t scsi_id)
{
    uint8_t i = 0;

    for (i = 0; i < HDD_NUM; i++) {
	if (wcslen(hdd[i].fn) == 0)
		continue;
	if ((hdd[i].spt == 0) || (hdd[i].hpc == 0) || (hdd[i].tracks == 0))
		continue;
	if ((hdd[i].bus == HDD_BUS_SCSI) && (hdd[i].scsi_id == scsi_id))
		return i;
    }
    return 0xff;
}


void
scsi_loadhd(int scsi_id, int id)
{
    if (! hdd_image_load(id))
	scsi_disks[scsi_id] = 0xff;
}


void
build_scsi_disk_map(void)
{
    uint8_t i = 0;

    memset(scsi_disks, 0xff, 16);

    for (i = 0; i < 16; i++) {
	scsi_disks[i] = find_hdd_for_scsi_id(i);
	if (scsi_disks[i] != 0xff) {
		if (wcslen(hdd[scsi_disks[i]].fn) > 0)
			scsi_loadhd(i, scsi_disks[i]);
	}
    }
}


void
scsi_disk_mode_sense_load(scsi_disk_t *dev)
{
    FILE *f;
    wchar_t file_name[512];
    int i;
    memset(&dev->ms_pages_saved, 0, sizeof(mode_sense_pages_t));
    for (i = 0; i < 0x3f; i++) {
	if (scsi_disk_mode_sense_pages_default.pages[i][1] != 0) {
		memcpy(dev->ms_pages_saved.pages[i], scsi_disk_mode_sense_pages_default.pages[i],
		       scsi_disk_mode_sense_pages_default.pages[i][1] + 2);
	}
    }
    swprintf(file_name, 512, L"scsi_disk_%02i_mode_sense.bin", dev->id);
    memset(file_name, 0, 512 * sizeof(wchar_t));
    f = plat_fopen(nvr_path(file_name), L"rb");
    if (f) {
	fread(dev->ms_pages_saved.pages[0x30], 1, 0x18, f);
	fclose(f);
    }
}


void
scsi_disk_mode_sense_save(scsi_disk_t *dev)
{
    FILE *f;
    wchar_t file_name[512];
    memset(file_name, 0, 512 * sizeof(wchar_t));
    swprintf(file_name, 512, L"scsi_disk_%02i_mode_sense.bin", dev->id);
    f = plat_fopen(nvr_path(file_name), L"wb");
    if (f) {
	fwrite(dev->ms_pages_saved.pages[0x30], 1, 0x18, f);
	fclose(f);
    }
}


int
scsi_disk_read_capacity(scsi_disk_t *dev, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
    int size = 0;

    size = hdd_image_get_last_sector(dev->id);
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
uint8_t
scsi_disk_mode_sense_read(scsi_disk_t *dev, uint8_t page_control, uint8_t page, uint8_t pos)
{
    switch (page_control) {
	case 0:
	case 3:
		return dev->ms_pages_saved.pages[page][pos];
		break;
	case 1:
		return scsi_disk_mode_sense_pages_changeable.pages[page][pos];
		break;
	case 2:
		return scsi_disk_mode_sense_pages_default.pages[page][pos];
		break;
    }

    return 0;
}


uint32_t
scsi_disk_mode_sense(scsi_disk_t *dev, uint8_t *buf, uint32_t pos, uint8_t type, uint8_t block_descriptor_len)
{
    uint8_t msplen, page_control = (type >> 6) & 3;

    int i = 0, j = 0;
    int size = 0;

    type &= 0x3f;

    size = hdd_image_get_last_sector(dev->id);

    if (block_descriptor_len) {
	buf[pos++] = 1;		/* Density code. */
	buf[pos++] = (size >> 16) & 0xff;	/* Number of blocks (0 = all). */
	buf[pos++] = (size >> 8) & 0xff;
	buf[pos++] = size & 0xff;
	buf[pos++] = 0;		/* Reserved. */
	buf[pos++] = 0;		/* Block length (0x200 = 512 bytes). */
	buf[pos++] = 2;
	buf[pos++] = 0;
    }

    for (i = 0; i < 0x40; i++) {
        if ((type == GPMODE_ALL_PAGES) || (type == i)) {
		if (scsi_disk_mode_sense_page_flags & (1LL << dev->current_page_code)) {
			buf[pos++] = scsi_disk_mode_sense_read(dev, page_control, i, 0);
			msplen = scsi_disk_mode_sense_read(dev, page_control, i, 1);
			buf[pos++] = msplen;
			scsi_disk_log("SCSI HDD %i: MODE SENSE: Page [%02X] length %i\n", dev->id, i, msplen);
			for (j = 0; j < msplen; j++)
				buf[pos++] = scsi_disk_mode_sense_read(dev, page_control, i, 2 + j);
		}
	}
    }

    return pos;
}


static void
scsi_disk_command_common(scsi_disk_t *dev)
{
    dev->status = BUSY_STAT;
    dev->phase = 1;
    if (dev->packet_status == CDROM_PHASE_COMPLETE) {
	scsi_disk_callback(dev);
	dev->callback = 0LL;
    } else
	dev->callback = -1LL;	/* Speed depends on SCSI controller */
}


static void
scsi_disk_command_complete(scsi_disk_t *dev)
{
    dev->packet_status = CDROM_PHASE_COMPLETE;
    scsi_disk_command_common(dev);
}


static void
scsi_disk_command_read_dma(scsi_disk_t *dev)
{
    dev->packet_status = CDROM_PHASE_DATA_IN_DMA;
    scsi_disk_command_common(dev);
}


static void
scsi_disk_command_write_dma(scsi_disk_t *dev)
{
    dev->packet_status = CDROM_PHASE_DATA_OUT_DMA;
    scsi_disk_command_common(dev);
}


static void
scsi_disk_data_command_finish(scsi_disk_t *dev, int len, int block_len, int alloc_len, int direction)
{
    scsi_disk_log("SCSI HD %i: Finishing command (%02X): %i, %i, %i, %i, %i\n", dev->id,
		dev->current_cdb[0], len, block_len, alloc_len, direction, dev->request_length);
    if (alloc_len >= 0) {
	if (alloc_len < len)
		len = alloc_len;
    }
    if (len == 0)
	scsi_disk_command_complete(dev);
    else {
	if (direction == 0)
		scsi_disk_command_read_dma(dev);
	else
		scsi_disk_command_write_dma(dev);
    }
}


static void
scsi_disk_sense_clear(scsi_disk_t *dev, int command)
{
    scsi_disk_sense_key = scsi_disk_asc = scsi_disk_ascq = 0;
}


static void
scsi_disk_set_phase(scsi_disk_t *dev, uint8_t phase)
{
    uint8_t scsi_id = dev->drv->scsi_id;

    if (dev->drv->bus != HDD_BUS_SCSI)
	return;

    SCSIDevices[scsi_id].Phase = phase;
}


static void
scsi_disk_cmd_error(scsi_disk_t *dev)
{
    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
    dev->error = ((scsi_disk_sense_key & 0xf) << 4) | ABRT_ERR;
    dev->status = READY_STAT | ERR_STAT;
    dev->phase = 3;
    dev->packet_status = 0x80;
    dev->callback = 50 * SCSI_TIME;
    scsi_disk_log("SCSI HD %i: ERROR: %02X/%02X/%02X\n", dev->id, scsi_disk_sense_key, scsi_disk_asc, scsi_disk_ascq);
}


static void
scsi_disk_invalid_lun(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc = ASC_INV_LUN;
    scsi_disk_ascq = 0;
    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
    scsi_disk_cmd_error(dev);
}


static void
scsi_disk_illegal_opcode(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc = ASC_ILLEGAL_OPCODE;
    scsi_disk_ascq = 0;
    scsi_disk_cmd_error(dev);
}


static void
scsi_disk_lba_out_of_range(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc = ASC_LBA_OUT_OF_RANGE;
    scsi_disk_ascq = 0;
    scsi_disk_cmd_error(dev);
}


static void
scsi_disk_invalid_field(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc = ASC_INV_FIELD_IN_CMD_PACKET;
    scsi_disk_ascq = 0;
    scsi_disk_cmd_error(dev);
    dev->status = 0x53;
}


static void
scsi_disk_invalid_field_pl(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc = ASC_INV_FIELD_IN_PARAMETER_LIST;
    scsi_disk_ascq = 0;
    scsi_disk_cmd_error(dev);
    dev->status = 0x53;
}


static void
scsi_disk_data_phase_error(scsi_disk_t *dev)
{
    scsi_disk_sense_key = SENSE_ILLEGAL_REQUEST;
    scsi_disk_asc = ASC_DATA_PHASE_ERROR;
    scsi_disk_ascq = 0;
    scsi_disk_cmd_error(dev);
}


static int
scsi_disk_pre_execution_check(scsi_disk_t *dev, uint8_t *cdb)
{
    if ((cdb[0] != GPCMD_REQUEST_SENSE) && (cdb[1] & 0xe0)) {
	scsi_disk_log("SCSI HD %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n",
		    dev->id, ((dev->request_length >> 5) & 7));
	scsi_disk_invalid_lun(dev);
	return 0;
    }

    if (!(scsi_disk_command_flags[cdb[0]] & IMPLEMENTED)) {
	scsi_disk_log("SCSI HD %i: Attempting to execute unknown command %02X\n", dev->id, cdb[0]);
	scsi_disk_illegal_opcode(dev);
	return 0;
    }

    /* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
       the UNIT ATTENTION condition if it's set. */
    if (cdb[0] != GPCMD_REQUEST_SENSE)
	scsi_disk_sense_clear(dev, cdb[0]);

    scsi_disk_log("SCSI HD %i: Continuing with command\n", dev->id);

    return 1;
}


static void
scsi_disk_seek(scsi_disk_t *dev, uint32_t pos)
{
    /* scsi_disk_log("SCSI HD %i: Seek %08X\n", dev->id, pos); */
    hdd_image_seek(dev->id, pos);
}


static void
scsi_disk_rezero(scsi_disk_t *dev)
{
    if (dev->id == 0xff)
	return;

    dev->sector_pos = dev->sector_len = 0;
    scsi_disk_seek(dev, 0);
}


void
scsi_disk_reset(scsi_disk_t *dev)
{
    scsi_disk_rezero(dev);
    dev->status = 0;
    dev->callback = 0;
    dev->packet_status = 0xff;
}


void
scsi_disk_request_sense(scsi_disk_t *dev, uint8_t *buffer, uint8_t alloc_length, int desc)
{				
    /*Will return 18 bytes of 0*/
    if (alloc_length != 0) {
	memset(buffer, 0, alloc_length);
	if (!desc)
		memcpy(buffer, dev->sense, alloc_length);
	else {
		buffer[1] = scsi_disk_sense_key;
		buffer[2] = scsi_disk_asc;
		buffer[3] = scsi_disk_ascq;
	}
    } else
	return;

    buffer[0] = 0x70;

    scsi_disk_log("SCSI HD %i: Reporting sense: %02X %02X %02X\n", dev->id, buffer[2], buffer[12], buffer[13]);

    /* Clear the sense stuff as per the spec. */
    scsi_disk_sense_clear(dev, GPCMD_REQUEST_SENSE);
}


void
scsi_disk_request_sense_for_scsi(scsi_disk_t *dev, uint8_t *buffer, uint8_t alloc_length)
{
    scsi_disk_request_sense(dev, buffer, alloc_length, 0);
}


void
scsi_disk_command(scsi_disk_t *dev, uint8_t *cdb)
{
    uint8_t *hdbufferb;
    int32_t *BufLen;
    uint32_t len;
    int max_len, pos = 0;
    unsigned idx = 0;
    unsigned size_idx, preamble_len;
    uint32_t alloc_length, last_sector = 0;
    char device_identify[9] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', 0 };
    char device_identify_ex[15] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', ' ', 'v', '1', '.', '0', '0', 0 };
    int block_desc = 0;

    hdbufferb = SCSIDevices[dev->drv->scsi_id].CmdBuffer;
    BufLen = &SCSIDevices[dev->drv->scsi_id].BufferLength;

    last_sector = hdd_image_get_last_sector(dev->id);

    dev->status &= ~ERR_STAT;
    dev->packet_len = 0;

    device_identify[6] = (dev->id / 10) + 0x30;
    device_identify[7] = (dev->id % 10) + 0x30;

    device_identify_ex[6] = (dev->id / 10) + 0x30;
    device_identify_ex[7] = (dev->id % 10) + 0x30;
    device_identify_ex[10] = EMU_VERSION[0];
    device_identify_ex[12] = EMU_VERSION[2];
    device_identify_ex[13] = EMU_VERSION[3];

    memcpy(dev->current_cdb, cdb, 12);

    if (cdb[0] != 0) {
	scsi_disk_log("SCSI HD %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X\n",
		    dev->id, cdb[0], scsi_disk_sense_key, scsi_disk_asc, scsi_disk_ascq);
	scsi_disk_log("SCSI HD %i: Request length: %04X\n", dev->id, dev->request_length);

	scsi_disk_log("SCSI HD %i: CDB: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", dev->id,
		    cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7],
		    cdb[8], cdb[9], cdb[10], cdb[11]);
    }

    dev->sector_len = 0;

    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);

    /* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
    if (scsi_disk_pre_execution_check(dev, cdb) == 0)
	return;

    switch (cdb[0]) {
	case GPCMD_SEND_DIAGNOSTIC:
		if (!(cdb[1] & (1 << 2))) {
			scsi_disk_invalid_field(dev);
			return;
		}
	case GPCMD_SCSI_RESERVE:
	case GPCMD_SCSI_RELEASE:
	case GPCMD_TEST_UNIT_READY:
	case GPCMD_FORMAT_UNIT:
		scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
		scsi_disk_command_complete(dev);
		break;

	case GPCMD_REZERO_UNIT:
		dev->sector_pos = dev->sector_len = 0;
		scsi_disk_seek(dev, 0);
		scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
		break;

	case GPCMD_REQUEST_SENSE:
		/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
		   should forget about the not ready, and report unit attention straight away. */
		if ((*BufLen == -1) || (cdb[4] < *BufLen))
			*BufLen = cdb[4];

		if (*BufLen < cdb[4])
			cdb[4] = *BufLen;

		len = (cdb[1] & 1) ? 8 : 18;

		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
		scsi_disk_data_command_finish(dev, len, len, cdb[4], 0);
		break;

	case GPCMD_MECHANISM_STATUS:
		len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

		if ((*BufLen == -1) || (len < *BufLen))
			*BufLen = len;

		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
		scsi_disk_data_command_finish(dev, 8, 8, len, 0);
		break;

	case GPCMD_READ_6:
	case GPCMD_READ_10:
	case GPCMD_READ_12:
		switch(cdb[0]) {
			case GPCMD_READ_6:
				dev->sector_len = cdb[4];
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				break;
			case GPCMD_READ_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				break;
			case GPCMD_READ_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		if ((dev->sector_pos > last_sector) || ((dev->sector_pos + dev->sector_len - 1) > last_sector)) {
			scsi_disk_lba_out_of_range(dev);
			return;
		}

		if ((!dev->sector_len) || (*BufLen == 0)) {
			scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
			scsi_disk_log("SCSI HD %i: All done - callback set\n", dev);
			dev->packet_status = CDROM_PHASE_COMPLETE;
			dev->callback = 20 * SCSI_TIME;
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;

		alloc_length = dev->packet_len = max_len << 9;

		if ((*BufLen == -1) || (alloc_length < *BufLen))
			*BufLen = alloc_length;

		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);

		if (dev->requested_blocks > 1)
			scsi_disk_data_command_finish(dev, alloc_length, alloc_length / dev->requested_blocks, alloc_length, 0);
		else
			scsi_disk_data_command_finish(dev, alloc_length, alloc_length, alloc_length, 0);
		return;

	case GPCMD_VERIFY_6:
	case GPCMD_VERIFY_10:
	case GPCMD_VERIFY_12:
		if (!(cdb[1] & 2)) {
			scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
			scsi_disk_command_complete(dev);
			break;
		}
	case GPCMD_WRITE_6:
	case GPCMD_WRITE_10:
	case GPCMD_WRITE_AND_VERIFY_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_12:
		switch(cdb[0])
		{
			case GPCMD_VERIFY_6:
			case GPCMD_WRITE_6:
				dev->sector_len = cdb[4];
				dev->sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
				scsi_disk_log("SCSI HD %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_VERIFY_10:
			case GPCMD_WRITE_10:
			case GPCMD_WRITE_AND_VERIFY_10:
				dev->sector_len = (cdb[7] << 8) | cdb[8];
				dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
				scsi_disk_log("SCSI HD %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);
				break;
			case GPCMD_VERIFY_12:
			case GPCMD_WRITE_12:
			case GPCMD_WRITE_AND_VERIFY_12:
				dev->sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
				dev->sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
				break;
		}

		if ((dev->sector_pos > last_sector) ||
		    ((dev->sector_pos + dev->sector_len - 1) > last_sector)) {
			scsi_disk_lba_out_of_range(dev);
			return;
		}

		if ((!dev->sector_len) || (*BufLen == 0)) {
			scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
			scsi_disk_log("SCSI HD %i: All done - callback set\n", dev->id);
			dev->packet_status = CDROM_PHASE_COMPLETE;
			dev->callback = 20 * SCSI_TIME;
			break;
		}

		max_len = dev->sector_len;
		dev->requested_blocks = max_len;

		alloc_length = dev->packet_len = max_len << 9;

		if ((*BufLen == -1) || (alloc_length < *BufLen))
			*BufLen = alloc_length;

		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_OUT);

		if (dev->requested_blocks > 1)
			scsi_disk_data_command_finish(dev, alloc_length, alloc_length / dev->requested_blocks, alloc_length, 1);
		else
			scsi_disk_data_command_finish(dev, alloc_length, alloc_length, alloc_length, 1);
		return;

	case GPCMD_WRITE_SAME_10:
		if ((cdb[1] & 6) == 6) {
			scsi_disk_invalid_field(dev);
			return;
		}

		dev->sector_len = (cdb[7] << 8) | cdb[8];
		dev->sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
		scsi_disk_log("SCSI HD %i: Length: %i, LBA: %i\n", dev->id, dev->sector_len, dev->sector_pos);

		if ((dev->sector_pos > last_sector) ||
		    ((dev->sector_pos + dev->sector_len - 1) > last_sector)) {
			scsi_disk_lba_out_of_range(dev);
			return;
		}

		if ((!dev->sector_len) || (*BufLen == 0)) {
			scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
			scsi_disk_log("SCSI HD %i: All done - callback set\n", dev->id);
			dev->packet_status = CDROM_PHASE_COMPLETE;
			dev->callback = 20 * SCSI_TIME;
			break;
		}

		max_len = 1;
		dev->requested_blocks = max_len;

		alloc_length = dev->packet_len = max_len << 9;

		if ((*BufLen == -1) || (alloc_length < *BufLen))
			*BufLen = alloc_length;

		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_OUT);

		if (dev->requested_blocks > 1)
			scsi_disk_data_command_finish(dev, alloc_length, alloc_length / dev->requested_blocks, alloc_length, 1);
		else
			scsi_disk_data_command_finish(dev, alloc_length, alloc_length, alloc_length, 1);
		return;

	case GPCMD_MODE_SENSE_6:
	case GPCMD_MODE_SENSE_10:
		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);

		block_desc = ((cdb[1] >> 3) & 1) ? 0 : 1;

		if (cdb[0] == GPCMD_MODE_SENSE_6)
			len = cdb[4];
		else
			len = (cdb[8] | (cdb[7] << 8));

		dev->current_page_code = cdb[2] & 0x3F;

		alloc_length = len;

		dev->temp_buffer = (uint8_t *) malloc(65536);
		memset(dev->temp_buffer, 0, 65536);

		if (cdb[0] == GPCMD_MODE_SENSE_6) {
			len = scsi_disk_mode_sense(dev, dev->temp_buffer, 4, cdb[2], block_desc);
			if (len > alloc_length)
				len = alloc_length;
			dev->temp_buffer[0] = len - 1;
			dev->temp_buffer[1] = 0;
			if (block_desc)
				dev->temp_buffer[3] = 8;
		} else {
			len = scsi_disk_mode_sense(dev, dev->temp_buffer, 8, cdb[2], block_desc);
			if (len > alloc_length)
				len = alloc_length;
			dev->temp_buffer[0]=(len - 2) >> 8;
			dev->temp_buffer[1]=(len - 2) & 255;
			dev->temp_buffer[2] = 0;
			if (block_desc) {
				dev->temp_buffer[6] = 0;
				dev->temp_buffer[7] = 8;
			}
		}

		if (len > alloc_length)
			len = alloc_length;
		else if (len < alloc_length)
			alloc_length = len;

		if ((*BufLen == -1) || (alloc_length < *BufLen))
			*BufLen = alloc_length;

		scsi_disk_log("SCSI HDD %i: Reading mode page: %02X...\n", dev->id, cdb[2]);

		scsi_disk_data_command_finish(dev, len, len, alloc_length, 0);
		return;

	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_OUT);

		if (cdb[0] == GPCMD_MODE_SELECT_6)
			len = cdb[4];
		else
			len = (cdb[7] << 8) | cdb[8];

		if ((*BufLen == -1) || (len < *BufLen))
			*BufLen = len;

		dev->total_length = len;
		dev->do_page_save = cdb[1] & 1;

		scsi_disk_data_command_finish(dev, len, len, len, 1);
		return;

	case GPCMD_INQUIRY:
		max_len = cdb[3];
		max_len <<= 8;
		max_len |= cdb[4];

		if ((!max_len) || (*BufLen == 0)) {
			scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
			/* scsi_disk_log("SCSI HD %i: All done - callback set\n", dev->id); */
			dev->packet_status = CDROM_PHASE_COMPLETE;
			dev->callback = 20 * SCSI_TIME;
			break;
		}			

		dev->temp_buffer = malloc(1024);

		if (cdb[1] & 1) {
			preamble_len = 4;
			size_idx = 3;

			dev->temp_buffer[idx++] = 05;
			dev->temp_buffer[idx++] = cdb[2];
			dev->temp_buffer[idx++] = 0;

			idx++;

			switch (cdb[2]) {
				case 0x00:
					dev->temp_buffer[idx++] = 0x00;
					dev->temp_buffer[idx++] = 0x83;
					break;
				case 0x83:
					if (idx + 24 > max_len) {
						free(dev->temp_buffer);
						dev->temp_buffer = NULL;
						scsi_disk_data_phase_error(dev);
						return;
					}

					dev->temp_buffer[idx++] = 0x02;
					dev->temp_buffer[idx++] = 0x00;
					dev->temp_buffer[idx++] = 0x00;
					dev->temp_buffer[idx++] = 20;
					ide_padstr8(dev->temp_buffer + idx, 20, "53R141");	/* Serial */
					idx += 20;

					if (idx + 72 > cdb[4])
						goto atapi_out;
					dev->temp_buffer[idx++] = 0x02;
					dev->temp_buffer[idx++] = 0x01;
					dev->temp_buffer[idx++] = 0x00;
					dev->temp_buffer[idx++] = 68;
					ide_padstr8(dev->temp_buffer + idx, 8, EMU_NAME); /* Vendor */
					idx += 8;
					ide_padstr8(dev->temp_buffer + idx, 40, device_identify_ex); /* Product */
					idx += 40;
					ide_padstr8(dev->temp_buffer + idx, 20, "53R141"); /* Product */
					idx += 20;
					break;
				default:
					scsi_disk_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
					free(dev->temp_buffer);
					dev->temp_buffer = NULL;
					scsi_disk_invalid_field(dev);
					return;
			}
		} else {
			preamble_len = 5;
			size_idx = 4;

			memset(dev->temp_buffer, 0, 8);
			dev->temp_buffer[0] = 0; /*SCSI HD*/
			dev->temp_buffer[1] = 0; /*Fixed*/
			dev->temp_buffer[2] = 0x02; /*SCSI-2 compliant*/
			dev->temp_buffer[3] = 0x02;
			dev->temp_buffer[4] = 31;
			dev->temp_buffer[6] = 1;	/* 16-bit transfers supported */
			dev->temp_buffer[7] = 0x20;	/* Wide bus supported */

			ide_padstr8(dev->temp_buffer + 8, 8, EMU_NAME); /* Vendor */
			ide_padstr8(dev->temp_buffer + 16, 16, device_identify); /* Product */
			ide_padstr8(dev->temp_buffer + 32, 4, EMU_VERSION); /* Revision */
			idx = 36;

			if (max_len == 96) {
				dev->temp_buffer[4] = 91;
				idx = 96;
			}
		}

atapi_out:
		dev->temp_buffer[size_idx] = idx - preamble_len;
		len=idx;

		scsi_disk_log("scsi_disk_command(): Inquiry (%08X, %08X)\n", hdbufferb, dev->temp_buffer);
		
		if (len > max_len)
			len = max_len;

		if ((*BufLen == -1) || (len < *BufLen))
			*BufLen = len;

		if (len > *BufLen)
			len = *BufLen;

		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
		scsi_disk_data_command_finish(dev, len, len, max_len, 0);
		break;

	case GPCMD_PREVENT_REMOVAL:
		scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
		scsi_disk_command_complete(dev);
		break;

	case GPCMD_SEEK_6:
	case GPCMD_SEEK_10:
		switch(cdb[0]) {
			case GPCMD_SEEK_6:
				pos = (cdb[2] << 8) | cdb[3];
				break;
			case GPCMD_SEEK_10:
				pos = (cdb[2] << 24) | (cdb[3]<<16) | (cdb[4]<<8) | cdb[5];
				break;
		}
		scsi_disk_seek(dev, pos);

		scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
		scsi_disk_command_complete(dev);
		break;

	case GPCMD_READ_CDROM_CAPACITY:
		dev->temp_buffer = (uint8_t *) malloc(8);

		if (scsi_disk_read_capacity(dev, dev->current_cdb, dev->temp_buffer, &len) == 0) {
			scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
			return;
		}

		if ((*BufLen == -1) || (len < *BufLen))
			*BufLen = len;

		scsi_disk_set_phase(dev, SCSI_PHASE_DATA_IN);
		scsi_disk_data_command_finish(dev, len, len, len, 0);
		break;

	default:
		scsi_disk_illegal_opcode(dev);
		break;
    }

    /* scsi_disk_log("SCSI HD %i: Phase: %02X, request length: %i\n", dev->id, dev->phase, dev->request_length); */
}


static void
scsi_disk_phase_data_in(scsi_disk_t *dev)
{
    uint8_t *hdbufferb = SCSIDevices[dev->drv->scsi_id].CmdBuffer;
    int32_t *BufLen = &SCSIDevices[dev->drv->scsi_id].BufferLength;

    if (!*BufLen) {
	scsi_disk_log("scsi_disk_phase_data_in(): Buffer length is 0\n");
	scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);

	return;
    }

    switch (dev->current_cdb[0]) {
	case GPCMD_REQUEST_SENSE:
		scsi_disk_log("SCSI HDD %i: %08X, %08X\n", dev->id, hdbufferb, *BufLen);
		scsi_disk_request_sense(dev, hdbufferb, *BufLen, dev->current_cdb[1] & 1);
		break;
	case GPCMD_MECHANISM_STATUS:
		memset(hdbufferb, 0, *BufLen);
		hdbufferb[5] = 1;
		break;
	case GPCMD_READ_6:
	case GPCMD_READ_10:
	case GPCMD_READ_12:
		if ((dev->requested_blocks > 0) && (*BufLen > 0)) {
			if (dev->packet_len > *BufLen)
				hdd_image_read(dev->id, dev->sector_pos, *BufLen >> 9, hdbufferb);
			else
				hdd_image_read(dev->id, dev->sector_pos, dev->requested_blocks, hdbufferb);
		}
		break;
	case GPCMD_MODE_SENSE_6:
	case GPCMD_MODE_SENSE_10:
	case GPCMD_INQUIRY:
	case GPCMD_READ_CDROM_CAPACITY:
		scsi_disk_log("scsi_disk_phase_data_in(): Filling buffer (%08X, %08X)\n", hdbufferb, dev->temp_buffer);
		memcpy(hdbufferb, dev->temp_buffer, *BufLen);
		free(dev->temp_buffer);
		dev->temp_buffer = NULL;
		scsi_disk_log("%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			    hdbufferb[0], hdbufferb[1], hdbufferb[2], hdbufferb[3], hdbufferb[4], hdbufferb[5], hdbufferb[6], hdbufferb[7],
			    hdbufferb[8], hdbufferb[9], hdbufferb[10], hdbufferb[11], hdbufferb[12], hdbufferb[13], hdbufferb[14], hdbufferb[15]);
		break;
	default:
		fatal("SCSI HDD %i: Bad Command for phase 2 (%02X)\n", dev->id, dev->current_cdb[0]);
		break;
    }

    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
}


static void
scsi_disk_phase_data_out(scsi_disk_t *dev)
{
    uint8_t *hdbufferb = SCSIDevices[dev->drv->scsi_id].CmdBuffer;
    int i;
    int32_t *BufLen = &SCSIDevices[dev->drv->scsi_id].BufferLength;
    uint32_t last_sector = hdd_image_get_last_sector(dev->id);
    uint32_t c, h, s, last_to_write = 0;
    uint16_t block_desc_len, pos;
    uint8_t hdr_len, val, old_val, ch, error = 0;
    uint8_t page, page_len;

    if (!*BufLen) {
	scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);

	return;
    }

    switch (dev->current_cdb[0]) {
	case GPCMD_VERIFY_6:
	case GPCMD_VERIFY_10:
	case GPCMD_VERIFY_12:
		break;
	case GPCMD_WRITE_6:
	case GPCMD_WRITE_10:
	case GPCMD_WRITE_AND_VERIFY_10:
	case GPCMD_WRITE_12:
	case GPCMD_WRITE_AND_VERIFY_12:
		if ((dev->requested_blocks > 0) && (*BufLen > 0)) {
			if (dev->packet_len > *BufLen)
				hdd_image_write(dev->id, dev->sector_pos, *BufLen >> 9, hdbufferb);
			else
				hdd_image_write(dev->id, dev->sector_pos, dev->requested_blocks, hdbufferb);
		}
		break;
	case GPCMD_WRITE_SAME_10:
		if (!dev->current_cdb[7] && !dev->current_cdb[8])
			last_to_write = last_sector;
		else
			last_to_write = dev->sector_pos + dev->sector_len - 1;

		for (i = dev->sector_pos; i <= last_to_write; i++) {
			if (dev->current_cdb[1] & 2) {
				hdbufferb[0] = (i >> 24) & 0xff;
				hdbufferb[1] = (i >> 16) & 0xff;
				hdbufferb[2] = (i >> 8) & 0xff;
				hdbufferb[3] = i & 0xff;
			} else if (dev->current_cdb[1] & 4) {
				s = (i % dev->drv->spt);
				h = ((i - s) / dev->drv->spt) % dev->drv->hpc;
				c = ((i - s) / dev->drv->spt) / dev->drv->hpc;
				hdbufferb[0] = (c >> 16) & 0xff;
				hdbufferb[1] = (c >> 8) & 0xff;
				hdbufferb[2] = c & 0xff;
				hdbufferb[3] = h & 0xff;
				hdbufferb[4] = (s >> 24) & 0xff;
				hdbufferb[5] = (s >> 16) & 0xff;
				hdbufferb[6] = (s >> 8) & 0xff;
				hdbufferb[7] = s & 0xff;
			}
			hdd_image_write(dev->id, i, 1, hdbufferb);
		}
		break;
	case GPCMD_MODE_SELECT_6:
	case GPCMD_MODE_SELECT_10:
		if (dev->current_cdb[0] == GPCMD_MODE_SELECT_10)
			hdr_len = 8;
		else
			hdr_len = 4;

		if (dev->current_cdb[0] == GPCMD_MODE_SELECT_6) {
			block_desc_len = hdbufferb[2];
			block_desc_len <<= 8;
			block_desc_len |= hdbufferb[3];
		} else {
			block_desc_len = hdbufferb[6];
			block_desc_len <<= 8;
			block_desc_len |= hdbufferb[7];
		}

		pos = hdr_len + block_desc_len;

		while(1) {
			page = hdbufferb[pos] & 0x3F;
			page_len = hdbufferb[pos + 1];

			pos += 2;

			if (!(scsi_disk_mode_sense_page_flags & (1LL << ((uint64_t) page))))
				error |= 1;
			else {
				for (i = 0; i < page_len; i++) {
					ch = scsi_disk_mode_sense_pages_changeable.pages[page][i + 2];
					val = hdbufferb[pos + i];
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

			val = scsi_disk_mode_sense_pages_default.pages[page][0] & 0x80;
			if (dev->do_page_save && val)
				scsi_disk_mode_sense_save(dev);

			if (pos >= dev->total_length)
				break;
		}

		if (error)
			scsi_disk_invalid_field_pl(dev);
		break;
	default:
		fatal("SCSI HDD %i: Bad Command for phase 2 (%02X)\n", dev->id, dev->current_cdb[0]);
		break;
    }

    scsi_disk_set_phase(dev, SCSI_PHASE_STATUS);
}


/* If the result is 1, issue an IRQ, otherwise not. */
void
scsi_disk_callback(scsi_disk_t *dev)
{
    switch(dev->packet_status) {
	case CDROM_PHASE_IDLE:
		scsi_disk_log("SCSI HD %i: PHASE_IDLE\n", dev->id);
		dev->phase = 1;
		dev->status = READY_STAT | DRQ_STAT | (dev->status & ERR_STAT);
		return;
	case CDROM_PHASE_COMPLETE:
		scsi_disk_log("SCSI HD %i: PHASE_COMPLETE\n", dev->id);
		dev->status = READY_STAT;
		dev->phase = 3;
		dev->packet_status = 0xFF;
		return;
	case CDROM_PHASE_DATA_OUT_DMA:
		scsi_disk_log("SCSI HD %i: PHASE_DATA_OUT_DMA\n", dev->id);
		scsi_disk_phase_data_out(dev);
		dev->packet_status = CDROM_PHASE_COMPLETE;
		dev->status = READY_STAT;
		dev->phase = 3;
		return;
	case CDROM_PHASE_DATA_IN_DMA:
		scsi_disk_log("SCSI HD %i: PHASE_DATA_IN_DMA\n", dev->id);
		scsi_disk_phase_data_in(dev);
		dev->packet_status = CDROM_PHASE_COMPLETE;
		dev->status = READY_STAT;
		dev->phase = 3;
		return;
	case CDROM_PHASE_ERROR:
		scsi_disk_log("SCSI HD %i: PHASE_ERROR\n", dev->id);
		dev->status = READY_STAT | ERR_STAT;
		dev->phase = 3;
		return;
    }
}


/* Peform a master init on the entire module. */
void
scsi_disk_global_init(void)
{
    /* Clear the global data. */
    memset(scsi_disk, 0x00, sizeof(scsi_disk));
}


void
scsi_disk_hard_reset(void)
{
    int c;

    for (c = 0; c < HDD_NUM; c++) {
	if (hdd[c].bus == HDD_BUS_SCSI) {
		scsi_disk_log("SCSI disk hard_reset drive=%d\n", c);

		if (!scsi_disk[c]) {
			scsi_disk[c] = (scsi_disk_t *) malloc(sizeof(scsi_disk_t));
			memset(scsi_disk[c], 0, sizeof(scsi_disk_t));
		}

		scsi_disk[c]->id = c;
		scsi_disk[c]->drv = &hdd[c];

		scsi_disk_mode_sense_load(scsi_disk[c]);
	}
    }
}


void
scsi_disk_close(void)
{
    scsi_disk_t *dev;
    int c;

    for (c = 0; c < HDD_NUM; c++) {
	dev = scsi_disk[c];

	if (dev) {
		hdd_image_close(c);

		free(scsi_disk[c]);
		scsi_disk[c] = NULL;
	}
    }
}
