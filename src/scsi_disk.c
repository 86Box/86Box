/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of SCSI fixed and removable disks.
 *
 * Version:	@(#)scsi_disk.c	1.0.5	2017/08/23
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *		Copyright 2017 Miran Grca.
 */
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>

#include "86box.h"
#include "cdrom.h"
#include "ibm.h"
#include "hdd_image.h"
#include "ide.h"
#include "piix.h"
#include "scsi.h"
#include "scsi_disk.h"
#include "timer.h"
#include "WIN/plat_iodev.h"


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

#define scsi_hd_sense_error shdc[id].sense[0]
#define scsi_hd_sense_key shdc[id].sense[2]
#define scsi_hd_asc shdc[id].sense[12]
#define scsi_hd_ascq shdc[id].sense[13]


scsi_hard_disk_t shdc[HDC_NUM];
FILE *shdf[HDC_NUM];

uint8_t scsi_hard_disks[16][8] = {
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
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
};


/* Table of all SCSI commands and their flags, needed for the new disc change / not ready handler. */
uint8_t scsi_hd_command_flags[0x100] = {
    IMPLEMENTED | CHECK_READY | NONDATA,			/* 0x00 */
    IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,		/* 0x01 */
    0,
    IMPLEMENTED | ALLOW_UA,					/* 0x03 */
    IMPLEMENTED | CHECK_READY | ALLOW_UA | NONDATA | SCSI_ONLY,	/* 0x04 */
    0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x08 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x0A */
    0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED | ALLOW_UA,					/* 0x12 */
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x13 */
    0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x1B */
    0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x1E */
    0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x25 */
    0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0x28 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0x2A */
    0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0x2F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY,					/* 0xA8 */
    0,
    IMPLEMENTED | CHECK_READY,					/* 0xAA */
    0, 0, 0, 0,
    IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,		/* 0xAF */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    IMPLEMENTED,						/* 0xBD */
    0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};


/* #define ENABLE_SCSI_HD_LOG 0 */
int scsi_hd_do_log = 0;


void scsi_hd_log(const char *format, ...)
{
#ifdef ENABLE_SCSI_HD_LOG
	if (scsi_hd_do_log)
	{
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
	}
#endif
}


/* Translates ATAPI status (ERR_STAT flag) to SCSI status. */
int scsi_hd_err_stat_to_scsi(uint8_t id)
{
	if (shdc[id].status & ERR_STAT)
	{
		return SCSI_STATUS_CHECK_CONDITION;
	}
	else
	{
		return SCSI_STATUS_OK;
	}

	return SCSI_STATUS_OK;
}


/* Translates ATAPI phase (DRQ, I/O, C/D) to SCSI phase (MSG, C/D, I/O). */
int scsi_hd_phase_to_scsi(uint8_t id)
{
	if (shdc[id].status & 8)
	{
		switch (shdc[id].phase & 3)
		{
			case 0:
				return 0;
			case 1:
				return 2;
			case 2:
				return 1;
			case 3:
				return 7;
		}
	}
	else
	{
		if ((shdc[id].phase & 3) == 3)
		{
			return 3;
		}
		else
		{
			/* Translate reserved ATAPI phase to reserved SCSI phase. */
			return 4;
		}
	}

	return 0;
}


int find_hdc_for_scsi_id(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t i = 0;

	for (i = 0; i < HDC_NUM; i++)
	{
		if ((wcslen(hdc[i].fn) == 0) && (hdc[i].bus != HDD_BUS_SCSI_REMOVABLE))
		{
			continue;
		}
		if (((hdc[i].spt == 0) || (hdc[i].hpc == 0) || (hdc[i].tracks == 0)) && (hdc[i].bus != HDD_BUS_SCSI_REMOVABLE))
		{
			continue;
		}
		if (((hdc[i].bus == HDD_BUS_SCSI) || (hdc[i].bus == HDD_BUS_SCSI_REMOVABLE)) && (hdc[i].scsi_id == scsi_id) && (hdc[i].scsi_lun == scsi_lun))
		{
			return i;
		}
	}
	return 0xff;
}


void scsi_disk_insert(uint8_t id)
{
	shdc[id].unit_attention = (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? 1 : 0;
}


void scsi_loadhd(int scsi_id, int scsi_lun, int id)
{
	int ret = 0;

	ret = hdd_image_load(id);

	if (!ret)
	{
		if (hdc[id].bus != HDD_BUS_SCSI_REMOVABLE)
		{
			scsi_hard_disks[scsi_id][scsi_lun] = 0xff;
		}
	}
	else
	{
		scsi_disk_insert(id);
	}
}


void scsi_reloadhd(int id)
{
	int ret = 0;

	if(wcslen(hdc[id].prev_fn) == 0)
	{
		return;
	}
	else
	{
		wcscpy(hdc[id].fn, hdc[id].prev_fn);
		memset(hdc[id].prev_fn, 0, sizeof(hdc[id].prev_fn));
	}

	ret = hdd_image_load(id);

	if (ret)
	{
		scsi_disk_insert(id);
	}
}


void scsi_unloadhd(int scsi_id, int scsi_lun, int id)
{
	hdd_image_unload(id, 1);
}


void build_scsi_hd_map(void)
{
	uint8_t i = 0;
	uint8_t j = 0;

	for (i = 0; i < 16; i++)
	{
		memset(scsi_hard_disks[i], 0xff, 8);
	}

	for (i = 0; i < 16; i++)
	{
		for (j = 0; j < 8; j++)
		{
			scsi_hard_disks[i][j] = find_hdc_for_scsi_id(i, j);
			if (scsi_hard_disks[i][j] != 0xff)
			{
				memset(&(shdc[scsi_hard_disks[i][j]]), 0, sizeof(shdc[scsi_hard_disks[i][j]]));
				if (wcslen(hdc[scsi_hard_disks[i][j]].fn) > 0)
				{
					scsi_loadhd(i, j, scsi_hard_disks[i][j]);
				}
			}
		}
	}
}


int scsi_hd_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
	int size = 0;

	size = hdd_image_get_last_sector(id);
	memset(buffer, 0, 8);
	buffer[0] = (size >> 24) & 0xff;
	buffer[1] = (size >> 16) & 0xff;
	buffer[2] = (size >> 8) & 0xff;
	buffer[3] = size & 0xff;
	buffer[6] = 2;				/* 512 = 0x0200 */
	*len = 8;
	
	pclog("Read Capacity\n");
	pclog("buffer[0]=%x\n", buffer[0]);
	pclog("buffer[1]=%x\n", buffer[1]);
	pclog("buffer[2]=%x\n", buffer[2]);
	pclog("buffer[3]=%x\n", buffer[3]);
	
	return 1;
}


void scsi_hd_update_request_length(uint8_t id, int len, int block_len)
{
	/* For media access commands, make sure the requested DRQ length matches the block length. */
	switch (shdc[id].current_cdb[0])
	{
		case 0x08:
		case 0x0a:
		case 0x28:
		case 0x2a:
		case 0xa8:
		case 0xaa:
			if (shdc[id].request_length < block_len)
			{
				shdc[id].request_length = block_len;
			}
			/* Make sure we respect the limit of how many blocks we can transfer at once. */
			if (shdc[id].requested_blocks > shdc[id].max_blocks_at_once)
			{
				shdc[id].requested_blocks = shdc[id].max_blocks_at_once;
			}
			shdc[id].block_total = (shdc[id].requested_blocks * block_len);
			if (len > shdc[id].block_total)
			{
				len = shdc[id].block_total;
			}
			break;
		default:
			shdc[id].packet_len = len;
			break;
	}
	/* If the DRQ length is odd, and the total remaining length is bigger, make sure it's even. */
	if ((shdc[id].request_length & 1) && (shdc[id].request_length < len))
	{
		shdc[id].request_length &= 0xfffe;
	}
	/* If the DRQ length is smaller or equal in size to the total remaining length, set it to that. */
	if (len <= shdc[id].request_length)
	{
		shdc[id].request_length = len;
	}
	return;
}


static void scsi_hd_command_common(uint8_t id)
{
	shdc[id].status = BUSY_STAT;
	shdc[id].phase = 1;
	shdc[id].pos = 0;
	if (shdc[id].packet_status == CDROM_PHASE_COMPLETE)
	{
		shdc[id].callback = 20 * SCSI_TIME;
	}
	else
	{
		shdc[id].callback = 60 * SCSI_TIME;
	}
}


void scsi_hd_command_complete(uint8_t id)
{
	shdc[id].packet_status = CDROM_PHASE_COMPLETE;
	scsi_hd_command_common(id);
}


static void scsi_hd_command_read_dma(uint8_t id)
{
	shdc[id].packet_status = CDROM_PHASE_DATA_IN_DMA;
	scsi_hd_command_common(id);
	shdc[id].total_read = 0;
}


static void scsi_hd_command_write_dma(uint8_t id)
{
	shdc[id].packet_status = CDROM_PHASE_DATA_OUT_DMA;
	scsi_hd_command_common(id);
}


void scsi_hd_data_command_finish(uint8_t id, int len, int block_len, int alloc_len, int direction)
{
	scsi_hd_log("SCSI HD %i: Finishing command (%02X): %i, %i, %i, %i, %i\n", id, shdc[id].current_cdb[0], len, block_len, alloc_len, direction, shdc[id].request_length);
	shdc[id].pos=0;
	if (alloc_len >= 0)
	{
		if (alloc_len < len)
		{
			len = alloc_len;
		}
	}
	if (len == 0)
	{
		SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength = 0;
		scsi_hd_command_complete(id);
	}
	else
	{
		if (direction == 0)
		{
			SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength = alloc_len;
			scsi_hd_command_read_dma(id);
		}
		else
		{
			scsi_hd_command_write_dma(id);
		}
	}
	
	scsi_hd_log("SCSI HD %i: Status: %i, cylinder %i, packet length: %i, position: %i, phase: %i\n", id, shdc[id].packet_status, shdc[id].request_length, shdc[id].packet_len, shdc[id].pos, shdc[id].phase);
}


static void scsi_hd_sense_clear(int id, int command)
{
	shdc[id].previous_command = command;
	scsi_hd_sense_key = scsi_hd_asc = scsi_hd_ascq = 0;
}


static void scsi_hd_cmd_error(uint8_t id)
{
	shdc[id].error = ((scsi_hd_sense_key & 0xf) << 4) | ABRT_ERR;
	if (shdc[id].unit_attention & 3)
	{
		shdc[id].error |= MCR_ERR;
	}
	shdc[id].status = READY_STAT | ERR_STAT;
	shdc[id].phase = 3;
	shdc[id].packet_status = 0x80;
	shdc[id].callback = 50 * SCSI_TIME;
	scsi_hd_log("SCSI HD %i: ERROR: %02X/%02X/%02X\n", id, scsi_hd_sense_key, scsi_hd_asc, scsi_hd_ascq);
}


static void scsi_hd_unit_attention(uint8_t id)
{
	shdc[id].error = (SENSE_NOT_READY << 4) | ABRT_ERR;
	if (shdc[id].unit_attention & 3)
	{
		shdc[id].error |= MCR_ERR;
	}
	shdc[id].status = READY_STAT | ERR_STAT;
	shdc[id].phase = 3;
	shdc[id].packet_status = 0x80;
	shdc[id].callback = 50 * CDROM_TIME;
	scsi_hd_log("SCSI HD %i: UNIT ATTENTION\n", id);
}


static void scsi_hd_not_ready(uint8_t id)
{
	scsi_hd_sense_key = SENSE_NOT_READY;
	scsi_hd_asc = ASC_MEDIUM_NOT_PRESENT;
	scsi_hd_ascq = 0;
	scsi_hd_cmd_error(id);
}


static void scsi_hd_write_protected(uint8_t id)
{
	scsi_hd_sense_key = SENSE_UNIT_ATTENTION;
	scsi_hd_asc = ASC_WRITE_PROTECTED;
	scsi_hd_ascq = 0;
	scsi_hd_cmd_error(id);
}


static void scsi_hd_invalid_lun(uint8_t id)
{
	scsi_hd_sense_key = SENSE_ILLEGAL_REQUEST;
	scsi_hd_asc = ASC_INV_LUN;
	scsi_hd_ascq = 0;
	scsi_hd_cmd_error(id);
}


static void scsi_hd_illegal_opcode(uint8_t id)
{
	scsi_hd_sense_key = SENSE_ILLEGAL_REQUEST;
	scsi_hd_asc = ASC_ILLEGAL_OPCODE;
	scsi_hd_ascq = 0;
	scsi_hd_cmd_error(id);
}


void scsi_hd_lba_out_of_range(uint8_t id)
{
	scsi_hd_sense_key = SENSE_ILLEGAL_REQUEST;
	scsi_hd_asc = ASC_LBA_OUT_OF_RANGE;
	scsi_hd_ascq = 0;
	scsi_hd_cmd_error(id);
}


static void scsi_hd_invalid_field(uint8_t id)
{
	scsi_hd_sense_key = SENSE_ILLEGAL_REQUEST;
	scsi_hd_asc = ASC_INV_FIELD_IN_CMD_PACKET;
	scsi_hd_ascq = 0;
	scsi_hd_cmd_error(id);
	shdc[id].status = 0x53;
}


static void scsi_hd_data_phase_error(uint8_t id)
{
	scsi_hd_sense_key = SENSE_ILLEGAL_REQUEST;
	scsi_hd_asc = ASC_DATA_PHASE_ERROR;
	scsi_hd_ascq = 0;
	scsi_hd_cmd_error(id);
}


/*SCSI Sense Initialization*/
void scsi_hd_sense_code_ok(uint8_t id)
{	
	scsi_hd_sense_key = SENSE_NONE;
	scsi_hd_asc = 0;
	scsi_hd_ascq = 0;
}


int scsi_hd_pre_execution_check(uint8_t id, uint8_t *cdb)
{
	int ready = 1;

	if (((shdc[id].request_length >> 5) & 7) != hdc[id].scsi_lun)
	{
		scsi_hd_log("SCSI HD %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", id, ((shdc[id].request_length >> 5) & 7));
		scsi_hd_invalid_lun(id);
		return 0;
	}

	if (!(scsi_hd_command_flags[cdb[0]] & IMPLEMENTED))
	{
		scsi_hd_log("SCSI HD %i: Attempting to execute unknown command %02X\n", id, cdb[0]);
		/* pclog("SCSI HD %i: Attempting to execute unknown command %02X (%02X %02X)\n", id, cdb[0], ((cdb[1] >> 3) & 1) ? 0 : 1, cdb[2] & 0x3F); */
		scsi_hd_illegal_opcode(id);
		return 0;
	}

	if (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE)
	{
		/* Removable disk, set ready state. */
		if (wcslen(hdc[id].fn) > 0)
		{
			ready = 1;
		}
		else
		{
			ready = 0;
		}
	}
	else
	{
		/* Fixed disk, clear UNIT ATTENTION, just in case it might have been set when the disk was removable). */
		shdc[id].unit_attention = 0;
	}

	if (!ready && shdc[id].unit_attention)
	{
		/* If the drive is not ready, there is no reason to keep the
		   UNIT ATTENTION condition present, as we only use it to mark
		   disc changes. */
		shdc[id].unit_attention = 0;
	}

	/* If the UNIT ATTENTION condition is set and the command does not allow
		execution under it, error out and report the condition. */
	if (shdc[id].unit_attention == 1)
	{
		/* Only increment the unit attention phase if the command can not pass through it. */
		if (!(scsi_hd_command_flags[cdb[0]] & ALLOW_UA))
		{
			/* scsi_hd_log("SCSI HD %i: Unit attention now 2\n", id); */
			shdc[id].unit_attention = 2;
			scsi_hd_log("SCSI HD %i: UNIT ATTENTION: Command %02X not allowed to pass through\n", id, cdb[0]);
			scsi_hd_unit_attention(id);
			return 0;
		}
	}
	else if (shdc[id].unit_attention == 2)
	{
		if (cdb[0] != GPCMD_REQUEST_SENSE)
		{
			/* scsi_hd_log("SCSI HD %i: Unit attention now 0\n", id); */
			shdc[id].unit_attention = 0;
		}
	}

	/* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
		the UNIT ATTENTION condition if it's set. */
	if (cdb[0] != GPCMD_REQUEST_SENSE)
	{
		scsi_hd_sense_clear(id, cdb[0]);
	}

	/* Next it's time for NOT READY. */
	if ((scsi_hd_command_flags[cdb[0]] & CHECK_READY) && !ready)
	{
		scsi_hd_log("SCSI HD %i: Not ready (%02X)\n", id, cdb[0]);
		scsi_hd_not_ready(id);
		return 0;
	}

	scsi_hd_log("SCSI HD %i: Continuing with command\n", id);
		
	return 1;
}


static void scsi_hd_seek(uint8_t id, uint32_t pos)
{
        /* scsi_hd_log("SCSI HD %i: Seek %08X\n", id, pos); */
	hdd_image_seek(id, pos);
}


static void scsi_hd_rezero(uint8_t id)
{
	if (id == 255)
	{
		return;
	}

	shdc[id].sector_pos = shdc[id].sector_len = 0;
	scsi_hd_seek(id, 0);
}


void scsi_hd_reset(uint8_t id)
{
	scsi_hd_rezero(id);
	shdc[id].status = 0;
	shdc[id].callback = 0;
	shdc[id].packet_status = 0xff;
}


void scsi_hd_request_sense(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{				
	/*Will return 18 bytes of 0*/
	if (alloc_length != 0)
	{
		memset(buffer, 0, alloc_length);
		memcpy(buffer, shdc[id].sense, alloc_length);
	}

	buffer[0] = 0x70;

	if (shdc[id].unit_attention && (scsi_hd_sense_key == 0))
	{
		buffer[2]=SENSE_UNIT_ATTENTION;
		buffer[12]=ASC_MEDIUM_MAY_HAVE_CHANGED;
		buffer[13]=0x00;
	}

	/* scsi_hd_log("SCSI HD %i: Reporting sense: %02X %02X %02X\n", id, hdbufferb[2], hdbufferb[12], hdbufferb[13]); */

	if (buffer[2] == SENSE_UNIT_ATTENTION)
	{
		/* If the last remaining sense is unit attention, clear
		   that condition. */
		shdc[id].unit_attention = 0;
	}

	/* Clear the sense stuff as per the spec. */
	scsi_hd_sense_clear(id, GPCMD_REQUEST_SENSE);
}


void scsi_hd_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{
	int ready = 1;

	if (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE)
	{
		/* Removable disk, set ready state. */
		if (wcslen(hdc[id].fn) > 0)
		{
			ready = 1;
		}
		else
		{
			ready = 0;
		}
	}
	else
	{
		/* Fixed disk, clear UNIT ATTENTION, just in case it might have been set when the disk was removable). */
		shdc[id].unit_attention = 0;
	}

	if (!ready && shdc[id].unit_attention)
	{
		/* If the drive is not ready, there is no reason to keep the
		   UNIT ATTENTION condition present, as we only use it to mark
		   disc changes. */
		shdc[id].unit_attention = 0;
	}

	/* Do *NOT* advance the unit attention phase. */

	scsi_hd_request_sense(id, buffer, alloc_length);
}


void scsi_hd_command(uint8_t id, uint8_t *cdb)
{
	/* uint8_t *hdbufferb = (uint8_t *) shdc[id].buffer; */
	uint8_t *hdbufferb = SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].CmdBuffer;
	uint32_t len;
	int pos=0;
	int max_len;
	unsigned idx = 0;
	unsigned size_idx;
	unsigned preamble_len;
	uint32_t alloc_length;
	char device_identify[9] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', 0 };
	char device_identify_ex[15] = { '8', '6', 'B', '_', 'H', 'D', '0', '0', ' ', 'v', '1', '.', '0', '0', 0 };
	uint8_t *tempbuffer;
	uint32_t last_sector = 0;

#if 0
	int CdbLength;
#endif
	last_sector = hdd_image_get_last_sector(id);

	shdc[id].status &= ~ERR_STAT;

	shdc[id].packet_len = 0;
	shdc[id].request_pos = 0;

	device_identify[6] = (id / 10) + 0x30;
	device_identify[7] = (id % 10) + 0x30;

	device_identify_ex[6] = (id / 10) + 0x30;
	device_identify_ex[7] = (id % 10) + 0x30;
	device_identify_ex[10] = EMU_VERSION[0];
	device_identify_ex[12] = EMU_VERSION[2];
	device_identify_ex[13] = EMU_VERSION[3];

	if (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE)
	{
		device_identify[4] = 'R';

		device_identify_ex[4] = 'R';
	}

	shdc[id].data_pos = 0;

	memcpy(shdc[id].current_cdb, cdb, 12);

	if (cdb[0] != 0)
	{
		scsi_hd_log("SCSI HD %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, %i\n", id, cdb[0], scsi_hd_sense_key, scsi_hd_asc, scsi_hd_ascq, ins);
		scsi_hd_log("SCSI HD %i: Request length: %04X\n", id, shdc[id].request_length);

#if 0
		for (CdbLength = 1; CdbLength < 12; CdbLength++)
		{
			scsi_hd_log("SCSI HD %i: CDB[%d] = 0x%02X\n", id, CdbLength, cdb[CdbLength]);
		}
#endif
	}
	
	shdc[id].sector_len = 0;

	/* This handles the Not Ready/Unit Attention check if it has to be handled at this point. */
	if (scsi_hd_pre_execution_check(id, cdb) == 0)
	{
		return;
	}

	switch (cdb[0])
	{
		case GPCMD_TEST_UNIT_READY:
		case GPCMD_FORMAT_UNIT:
		case GPCMD_VERIFY_6:
		case GPCMD_VERIFY_10:
		case GPCMD_VERIFY_12:
			scsi_hd_command_complete(id);
			break;

		case GPCMD_REZERO_UNIT:
			shdc[id].sector_pos = shdc[id].sector_len = 0;
			scsi_hd_seek(id, 0);
			break;

		case GPCMD_REQUEST_SENSE:
			/* If there's a unit attention condition and there's a buffered not ready, a standalone REQUEST SENSE
			   should forget about the not ready, and report unit attention straight away. */
			scsi_hd_request_sense(id, hdbufferb, cdb[4]);
			scsi_hd_data_command_finish(id, 18, 18, cdb[4], 0);
			break;

		case GPCMD_MECHANISM_STATUS:
			len = (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

 			memset(hdbufferb, 0, 8);
			hdbufferb[5] = 1;

			scsi_hd_data_command_finish(id, 8, 8, len, 0);
			break;

		case GPCMD_READ_6:
		case GPCMD_READ_10:
		case GPCMD_READ_12:
			switch(cdb[0])
			{
				case GPCMD_READ_6:
					shdc[id].sector_len = cdb[4];
					shdc[id].sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
					break;
				case GPCMD_READ_10:
					shdc[id].sector_len = (cdb[7] << 8) | cdb[8];
					shdc[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					break;
				case GPCMD_READ_12:
					shdc[id].sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
					shdc[id].sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
					break;
			}

			if ((shdc[id].sector_pos > last_sector) || ((shdc[id].sector_pos + shdc[id].sector_len - 1) > last_sector))
			{
				scsi_hd_lba_out_of_range(id);
				return;
			}

			if ((!shdc[id].sector_len) || (SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength == 0))
			{
				/* scsi_hd_log("SCSI HD %i: All done - callback set\n", id); */
				shdc[id].packet_status = CDROM_PHASE_COMPLETE;
				shdc[id].callback = 20 * SCSI_TIME;
				break;
			}

			max_len = shdc[id].sector_len;
			shdc[id].requested_blocks = max_len;

			alloc_length = shdc[id].packet_len = max_len << 9;

			if ((shdc[id].requested_blocks > 0) && (SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength > 0))
			{
				if (alloc_length > SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength)
				{
					hdd_image_read(id, shdc[id].sector_pos, SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength >> 9, hdbufferb);
				}
				else
				{
					hdd_image_read(id, shdc[id].sector_pos, max_len, hdbufferb);
				}
			}

			if (shdc[id].requested_blocks > 1)
			{
				scsi_hd_data_command_finish(id, alloc_length, alloc_length / shdc[id].requested_blocks, alloc_length, 0);
			}
			else
			{
				scsi_hd_data_command_finish(id, alloc_length, alloc_length, alloc_length, 0);
			}
			shdc[id].all_blocks_total = shdc[id].block_total;
			if (shdc[id].packet_status != CDROM_PHASE_COMPLETE)
			{
				update_status_bar_icon((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? (SB_RDISK | id) : (SB_HDD | HDD_BUS_SCSI), 1);
			}
			else
			{
				update_status_bar_icon((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? (SB_RDISK | id) : (SB_HDD | HDD_BUS_SCSI), 0);
			}
			return;

		case GPCMD_WRITE_6:
		case GPCMD_WRITE_10:
		case GPCMD_WRITE_12:
			if ((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) && hdc[id].wp)
			{
				scsi_hd_write_protected(id);
				return;
			}

			switch(cdb[0])
			{
				case GPCMD_WRITE_6:
					shdc[id].sector_len = cdb[4];
					shdc[id].sector_pos = ((((uint32_t) cdb[1]) & 0x1f) << 16) | (((uint32_t) cdb[2]) << 8) | ((uint32_t) cdb[3]);
					break;
				case GPCMD_WRITE_10:
					shdc[id].sector_len = (cdb[7] << 8) | cdb[8];
					shdc[id].sector_pos = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
					scsi_hd_log("SCSI HD %i: Length: %i, LBA: %i\n", id, shdc[id].sector_len, shdc[id].sector_pos);
					break;
				case GPCMD_WRITE_12:
					shdc[id].sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
					shdc[id].sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
					break;
			}

			if ((shdc[id].sector_pos > last_sector) || ((shdc[id].sector_pos + shdc[id].sector_len - 1) > last_sector))
			{
				scsi_hd_lba_out_of_range(id);
				return;
			}

			if ((!shdc[id].sector_len) || (SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength == 0))
			{
				/* scsi_hd_log("SCSI HD %i: All done - callback set\n", id); */
				shdc[id].packet_status = CDROM_PHASE_COMPLETE;
				shdc[id].callback = 20 * SCSI_TIME;
				break;
			}

			max_len = shdc[id].sector_len;
			shdc[id].requested_blocks = max_len;

			alloc_length = shdc[id].packet_len = max_len << 9;

			if ((shdc[id].requested_blocks > 0) && (SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength > 0))
			{
				if (alloc_length > SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength)
				{
					hdd_image_write(id, shdc[id].sector_pos, SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength >> 9, hdbufferb);
				}
				else
				{
					hdd_image_write(id, shdc[id].sector_pos, max_len, hdbufferb);
				}
			}

			if (shdc[id].requested_blocks > 1)
			{
				scsi_hd_data_command_finish(id, alloc_length, alloc_length / shdc[id].requested_blocks, alloc_length, 1);
			}
			else
			{
				scsi_hd_data_command_finish(id, alloc_length, alloc_length, alloc_length, 1);
			}
			shdc[id].all_blocks_total = shdc[id].block_total;
			if (shdc[id].packet_status != CDROM_PHASE_COMPLETE)
			{
				update_status_bar_icon((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? (SB_RDISK | id) : (SB_HDD | HDD_BUS_SCSI), 1);
			}
			else
			{
				update_status_bar_icon((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? (SB_RDISK | id) : (SB_HDD | HDD_BUS_SCSI), 0);
			}
			return;

		case GPCMD_START_STOP_UNIT:
			if (hdc[id].bus != HDD_BUS_SCSI_REMOVABLE)
			{
				scsi_hd_illegal_opcode(id);
				break;
			}

			switch(cdb[4] & 3)
			{
				case 0:		/* Stop the disc. */
				case 1:		/* Start the disc and read the TOC. */
					break;
				case 2:		/* Eject the disc if possible. */
					removable_disk_eject(id);
					break;
				case 3:		/* Load the disc (close tray). */
					removable_disk_reload(id);
					break;
			}

			scsi_hd_command_complete(id);
			break;

		case GPCMD_INQUIRY:
			max_len = cdb[3];
			max_len <<= 8;
			max_len |= cdb[4];

			if ((!max_len) || (SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength == 0))
			{
				/* scsi_hd_log("SCSI HD %i: All done - callback set\n", id); */
				shdc[id].packet_status = CDROM_PHASE_COMPLETE;
				shdc[id].callback = 20 * SCSI_TIME;
				break;
			}

			tempbuffer = malloc(1024);

			if (cdb[1] & 1)
			{
				preamble_len = 4;
				size_idx = 3;
					
				tempbuffer[idx++] = 05;
				tempbuffer[idx++] = cdb[2];
				tempbuffer[idx++] = 0;

				idx++;

				switch (cdb[2])
				{
					case 0x00:
						tempbuffer[idx++] = 0x00;
						tempbuffer[idx++] = 0x83;
						break;
					case 0x83:
						if (idx + 24 > max_len)
						{
							scsi_hd_data_phase_error(id);
							return;
						}

						tempbuffer[idx++] = 0x02;
						tempbuffer[idx++] = 0x00;
						tempbuffer[idx++] = 0x00;
						tempbuffer[idx++] = 20;
						ide_padstr8(hdbufferb + idx, 20, "53R141");	/* Serial */
						idx += 20;

						if (idx + 72 > cdb[4])
						{
							goto atapi_out;
						}
						tempbuffer[idx++] = 0x02;
						tempbuffer[idx++] = 0x01;
						tempbuffer[idx++] = 0x00;
						tempbuffer[idx++] = 68;
						ide_padstr8(tempbuffer + idx, 8, EMU_NAME); /* Vendor */
						idx += 8;
						ide_padstr8(tempbuffer + idx, 40, device_identify_ex); /* Product */
						idx += 40;
						ide_padstr8(tempbuffer + idx, 20, "53R141"); /* Product */
						idx += 20;
						break;
					default:
						scsi_hd_log("INQUIRY: Invalid page: %02X\n", cdb[2]);
						scsi_hd_invalid_field(id);
						return;
				}
			}
			else
			{
				preamble_len = 5;
				size_idx = 4;

				memset(tempbuffer, 0, 8);
				tempbuffer[0] = 0; /*SCSI HD*/
				if (hdc[id].bus == HDD_BUS_SCSI_REMOVABLE)
				{
					tempbuffer[1] = 0x80; /*Removable*/
				}
				else
				{
					tempbuffer[1] = 0; /*Fixed*/
				}
				tempbuffer[2] = 0x02; /*SCSI-2 compliant*/
				tempbuffer[3] = 0x02;
				tempbuffer[4] = 31;

				ide_padstr8(tempbuffer + 8, 8, EMU_NAME); /* Vendor */
				ide_padstr8(tempbuffer + 16, 16, device_identify); /* Product */
				ide_padstr8(tempbuffer + 32, 4, EMU_VERSION); /* Revision */
				idx = 36;
			}

atapi_out:
			tempbuffer[size_idx] = idx - preamble_len;
			len=idx;

			if (len > max_len)
			{
				len = max_len;
			}

			if (len > SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength)
			{
				len = SCSIDevices[hdc[id].scsi_id][hdc[id].scsi_lun].InitLength;
			}

			memcpy(hdbufferb, tempbuffer, len);

			free(tempbuffer);

			scsi_hd_data_command_finish(id, len, len, max_len, 0);
			break;

		case GPCMD_PREVENT_REMOVAL:
			scsi_hd_command_complete(id);
			break;

		case GPCMD_SEEK_6:
		case GPCMD_SEEK_10:
			switch(cdb[0])
			{
				case GPCMD_SEEK_6:
					pos = (cdb[2] << 8) | cdb[3];
					break;
				case GPCMD_SEEK_10:
					pos = (cdb[2] << 24) | (cdb[3]<<16) | (cdb[4]<<8) | cdb[5];
					break;
			}
			scsi_hd_seek(id, pos);
			scsi_hd_command_complete(id);
			break;

		case GPCMD_READ_CDROM_CAPACITY:
			if (scsi_hd_read_capacity(id, shdc[id].current_cdb, hdbufferb, &len) == 0)
			{
				return;
			}
			
			scsi_hd_data_command_finish(id, len, len, len, 0);
			break;

		default:
			/* pclog("SCSI HD %i: Attempting to execute pseudo-implemented command %02X\n", id, cdb[0]); */
			scsi_hd_illegal_opcode(id);
			break;
	}

	/* scsi_hd_log("SCSI HD %i: Phase: %02X, request length: %i\n", shdc[id].phase, shdc[id].request_length); */
}


/* If the result is 1, issue an IRQ, otherwise not. */
void scsi_hd_callback(uint8_t id)
{
	switch(shdc[id].packet_status)
	{
		case CDROM_PHASE_IDLE:
			scsi_hd_log("SCSI HD %i: PHASE_IDLE\n", id);
			shdc[id].pos=0;
			shdc[id].phase = 1;
			shdc[id].status = READY_STAT | DRQ_STAT | (shdc[id].status & ERR_STAT);
			return;
		case CDROM_PHASE_COMPLETE:
			scsi_hd_log("SCSI HD %i: PHASE_COMPLETE\n", id);
			shdc[id].status = READY_STAT;
			shdc[id].phase = 3;
			shdc[id].packet_status = 0xFF;
			update_status_bar_icon((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? (SB_RDISK | id) : (SB_HDD | HDD_BUS_SCSI), 0);
			return;
		case CDROM_PHASE_DATA_OUT:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_OUT\n", id);
			shdc[id].status = READY_STAT | DRQ_STAT | (shdc[id].status & ERR_STAT);
			shdc[id].phase = 0;
			return;
		case CDROM_PHASE_DATA_OUT_DMA:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_OUT_DMA\n", id);
			shdc[id].packet_status = CDROM_PHASE_COMPLETE;
			shdc[id].status = READY_STAT;
			shdc[id].phase = 3;
			update_status_bar_icon((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? (SB_RDISK | id) : (SB_HDD | HDD_BUS_SCSI), 0);
			return;
		case CDROM_PHASE_DATA_IN:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_IN\n", id);
			shdc[id].status = READY_STAT | DRQ_STAT | (shdc[id].status & ERR_STAT);
			shdc[id].phase = 2;
			return;
		case CDROM_PHASE_DATA_IN_DMA:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_IN_DMA\n", id);
			shdc[id].packet_status = CDROM_PHASE_COMPLETE;
			shdc[id].status = READY_STAT;
			shdc[id].phase = 3;
			update_status_bar_icon((hdc[id].bus == HDD_BUS_SCSI_REMOVABLE) ? (SB_RDISK | id) : (SB_HDD | HDD_BUS_SCSI), 0);
			return;
		case CDROM_PHASE_ERROR:
			scsi_hd_log("SCSI HD %i: PHASE_ERROR\n", id);
			shdc[id].status = READY_STAT | ERR_STAT;
			shdc[id].phase = 3;
			return;
	}
}
