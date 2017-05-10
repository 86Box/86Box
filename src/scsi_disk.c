/* SCSI hard disk emulation */

#define _LARGEFILE_SOURCE
#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>

#include <inttypes.h>

#include "86box.h"
#include "cdrom.h"
#include "ibm.h"
#include "ide.h"
#include "piix.h"
#include "scsi.h"
#include "timer.h"

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

uint8_t scsi_hard_disks[16][8] =	{	{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF },
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
uint8_t scsi_hd_command_flags[0x100] =
{
	IMPLEMENTED | CHECK_READY | NONDATA,					/* 0x00 */
	IMPLEMENTED | ALLOW_UA | NONDATA | SCSI_ONLY,				/* 0x01 */
	0,
	IMPLEMENTED | ALLOW_UA,							/* 0x03 */
	IMPLEMENTED | CHECK_READY | ALLOW_UA | NONDATA | SCSI_ONLY,		/* 0x04 */
	0, 0, 0,
	IMPLEMENTED | CHECK_READY,						/* 0x08 */
	0,
	IMPLEMENTED | CHECK_READY,						/* 0x0A */
	0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED | ALLOW_UA,							/* 0x12 */
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,			/* 0x13 */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,						/* 0x1E */
	0, 0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,						/* 0x25 */
	0, 0,
	IMPLEMENTED | CHECK_READY,						/* 0x28 */
	0,
	IMPLEMENTED | CHECK_READY,						/* 0x2A */
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,			/* 0x2F */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY,						/* 0xA8 */
	0,
	IMPLEMENTED | CHECK_READY,						/* 0xAA */
	0, 0, 0, 0,
	IMPLEMENTED | CHECK_READY | NONDATA | SCSI_ONLY,			/* 0xAF */
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	IMPLEMENTED,								/* 0xBD */
	0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

int scsi_hd_do_log = 0;

void scsi_hd_log(const char *format, ...)
{
#ifdef ENABLE_scsi_hd_LOG
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
		if ((hdc[i].bus == 4) && (hdc[i].scsi_id == scsi_id) && (hdc[i].scsi_lun == scsi_lun))
		{
			return i;
		}
	}
	return 0xff;
}

static void scsi_loadhd(int scsi_id, int scsi_lun, int id)
{
	uint32_t sector_size = 512;
	uint32_t zero = 0;
	uint64_t signature = 0xD778A82044445459ll;
	uint64_t full_size = 0;
	int c;
	wchar_t *fn = hdd_fn[id];

	shdc[id].base = 0;

	if (shdf[id] != NULL)
	{
		fclose(shdf[id]);
		shdf[id] == NULL;
	}

	/* Try to open existing hard disk image */
	if (fn[0] == '.')
	{
		scsi_hard_disks[scsi_id][scsi_lun] = 0xff;
		return;
	}
	shdf[id] = _wfopen(fn, L"rb+");
	if (shdf[id] == NULL)
	{
		/* Failed to open existing hard disk image */
		if (errno == ENOENT)
		{
			/* Failed because it does not exist,
			   so try to create new file */
			shdf[id] = _wfopen(fn, L"wb+");
			if (shdf[id] == NULL)
			{
				scsi_hard_disks[scsi_id][scsi_lun] = 0xff;
				return;
			}
			else
			{
				memset(&(shdc[id]), 0, sizeof(scsi_hard_disk_t));
				if (image_is_hdi(fn))
				{
					shdc[id].base = 0x1000;
					fwrite(&zero, 1, 4, shdf[id]);
					fwrite(&zero, 1, 4, shdf[id]);
					fwrite(&(shdc[id].base), 1, 4, shdf[id]);
					fwrite(&full_size, 1, 4, shdf[id]);
					fwrite(&sector_size, 1, 4, shdf[id]);
					fwrite(&(hdc[id].spt), 1, 4, shdf[id]);
					fwrite(&(hdc[id].hpc), 1, 4, shdf[id]);
					fwrite(&(hdc[id].tracks), 1, 4, shdf[id]);
					for (c = 0; c < 0x3f8; c++)
					{
						fwrite(&zero, 1, 4, shdf[id]);
					}
				}
				else if (image_is_hdx(fn, 0))
				{
					shdc[id].base = 0x28;
					fwrite(&signature, 1, 8, shdf[id]);
					fwrite(&full_size, 1, 8, shdf[id]);
					fwrite(&sector_size, 1, 4, shdf[id]);
					fwrite(&(hdc[id].spt), 1, 4, shdf[id]);
					fwrite(&(hdc[id].hpc), 1, 4, shdf[id]);
					fwrite(&(hdc[id].tracks), 1, 4, shdf[id]);
					fwrite(&zero, 1, 4, shdf[id]);
					fwrite(&zero, 1, 4, shdf[id]);
				}
				full_size = hdc[id].spt * hdc[id].hpc * hdc[id].tracks * 512;
				shdc[id].last_sector = (uint32_t) (full_size >> 9) - 1;
				shdc[id].cdb_len = 12;
			}
		}
		else
		{
			/* Failed for another reason */
			scsi_hard_disks[scsi_id][scsi_lun] = 0xff;
			return;
		}
	}
	else
	{
		memset(&(shdc[id]), 0, sizeof(scsi_hard_disk_t));
		if (image_is_hdi(fn))
		{
			fseeko64(shdf[id], 0x8, SEEK_SET);
			fread(&(shdc[id].base), 1, 4, shdf[id]);
			fseeko64(shdf[id], 0x10, SEEK_SET);
			fread(&sector_size, 1, 4, shdf[id]);
			if (sector_size != 512)
			{
				/* Sector size is not 512 */
				fclose(shdf[id]);
				scsi_hard_disks[scsi_id][scsi_lun] = 0xff;
				return;
			}
			fread(&(hdc[id].spt), 1, 4, shdf[id]);
			fread(&(hdc[id].hpc), 1, 4, shdf[id]);
			fread(&(hdc[id].tracks), 1, 4, shdf[id]);
		}
		else if (image_is_hdx(fn, 1))
		{
			shdc[id].base = 0x28;
			fseeko64(shdf[id], 0x10, SEEK_SET);
			fread(&sector_size, 1, 4, shdf[id]);
			if (sector_size != 512)
			{
				/* Sector size is not 512 */
				fclose(shdf[id]);
				scsi_hard_disks[scsi_id][scsi_lun] = 0xff;
				return;
			}
			fread(&(hdc[id].spt), 1, 4, shdf[id]);
			fread(&(hdc[id].hpc), 1, 4, shdf[id]);
			fread(&(hdc[id].tracks), 1, 4, shdf[id]);
			fread(&(hdc[id].at_spt), 1, 4, shdf[id]);
			fread(&(hdc[id].at_hpc), 1, 4, shdf[id]);
		}
		full_size = hdc[id].spt * hdc[id].hpc * hdc[id].tracks * 512;
		shdc[id].last_sector = (uint32_t) (full_size >> 9) - 1;
		shdc[id].cdb_len = 12;
	}
}

void build_scsi_hd_map()
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
				scsi_loadhd(i, j, scsi_hard_disks[i][j]);
			}
		}
	}
}

int scsi_hd_read_capacity(uint8_t id, uint8_t *cdb, uint8_t *buffer, uint32_t *len)
{
	int ret = 0;
	int size = 0;

	size = shdc[id].last_sector;
	memset(buffer, 0, 8);
	buffer[0] = (size >> 24) & 0xff;
	buffer[1] = (size >> 16) & 0xff;
	buffer[2] = (size >> 8) & 0xff;
	buffer[3] = size & 0xff;
	buffer[6] = 2;				/* 512 = 0x0200 */
	*len = 8;

	return 1;
}

void scsi_hd_set_cdb_len(int id, int cdb_len)
{
	shdc[id].cdb_len = cdb_len;
}

void scsi_hd_reset_cdb_len(int id)
{
	shdc[id].cdb_len = 12;
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

static void scsi_hd_command_complete(uint8_t id)
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

static void scsi_hd_data_command_finish(uint8_t id, int len, int block_len, int alloc_len, int direction)
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
	shdc[id].status = READY_STAT | ERR_STAT;
	shdc[id].phase = 3;
	shdc[id].packet_status = 0x80;
	shdc[id].callback = 50 * SCSI_TIME;
	scsi_hd_log("SCSI HD %i: ERROR: %02X/%02X/%02X\n", id, scsi_hd_sense_key, scsi_hd_asc, scsi_hd_ascq);
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

static void scsi_hd_lba_out_of_range(uint8_t id)
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

void scsi_hd_update_cdb(uint8_t *cdb, int lba_pos, int number_of_blocks)
{
	switch(cdb[0])
	{
		case GPCMD_READ_6:
		case GPCMD_WRITE_6:
			cdb[1] = (lba_pos >> 16) & 0xff;
			cdb[2] = (lba_pos >> 8) & 0xff;
			cdb[3] = lba_pos & 0xff;
			break;

		case GPCMD_READ_10:
		case GPCMD_WRITE_10:
			cdb[2] = (lba_pos >> 24) & 0xff;
			cdb[3] = (lba_pos >> 16) & 0xff;
			cdb[4] = (lba_pos >> 8) & 0xff;
			cdb[5] = lba_pos & 0xff;
			cdb[7] = (number_of_blocks >> 8) & 0xff;
			cdb[8] = number_of_blocks & 0xff;
			break;

		case GPCMD_READ_12:
		case GPCMD_WRITE_12:
			cdb[2] = (lba_pos >> 24) & 0xff;
			cdb[3] = (lba_pos >> 16) & 0xff;
			cdb[4] = (lba_pos >> 8) & 0xff;
			cdb[5] = lba_pos & 0xff;
			cdb[6] = (number_of_blocks >> 24) & 0xff;
			cdb[7] = (number_of_blocks >> 16) & 0xff;
			cdb[8] = (number_of_blocks >> 8) & 0xff;
			cdb[9] = number_of_blocks & 0xff;
			break;
	}
}

int scsi_hd_read_data(uint8_t id, uint32_t *len)
{
	uint8_t *hdbufferb = (uint8_t *) shdc[id].buffer;

	int temp_len = 0;

	int last_valid_data_pos = 0;

	uint64_t pos64 = (uint64_t) shdc[id].sector_pos;

	if (shdc[id].sector_pos > shdc[id].last_sector)
	{
		/* scsi_hd_log("SCSI HD %i: Trying to read beyond the end of disk\n", id); */
		scsi_hd_lba_out_of_range(id);
		return 0;
	}

	shdc[id].old_len = 0;
	*len = 0;

	fseeko64(shdf[id], pos64 << 9, SEEK_SET);
	fread(hdbufferb + shdc[id].data_pos, (shdc[id].sector_len << 9), 1, shdf[id]);
	temp_len = (shdc[id].sector_len << 9);

	last_valid_data_pos = shdc[id].data_pos;

	shdc[id].data_pos += temp_len;
	shdc[id].old_len += temp_len;

	*len += temp_len;

	scsi_hd_log("SCSI HD %i: Data from raw sector read:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, hdbufferb[last_valid_data_pos + 0], hdbufferb[last_valid_data_pos + 1], hdbufferb[last_valid_data_pos + 2], hdbufferb[last_valid_data_pos + 3], hdbufferb[last_valid_data_pos + 4], hdbufferb[last_valid_data_pos + 5], hdbufferb[last_valid_data_pos + 6], hdbufferb[last_valid_data_pos + 7]);

	return 1;
}

int scsi_hd_read_blocks(uint8_t id, uint32_t *len, int first_batch)
{
	int ret = 0;
	
	shdc[id].data_pos = 0;
	
	if (!shdc[id].sector_len)
	{
		scsi_hd_command_complete(id);
		return -1;
	}

	scsi_hd_log("Reading %i blocks starting from %i...\n", shdc[id].requested_blocks, shdc[id].sector_pos);

	scsi_hd_update_cdb(shdc[id].current_cdb, shdc[id].sector_pos, shdc[id].requested_blocks);

	ret = scsi_hd_read_data(id, len);

	scsi_hd_log("Read %i bytes of blocks...\n", *len);

	if (!ret)
	{
		return 0;
	}

	shdc[id].sector_pos += shdc[id].requested_blocks;
	shdc[id].sector_len -= shdc[id].requested_blocks;

	return 1;
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
	if (((shdc[id].request_length >> 5) & 7) != hdc[id].scsi_lun)
	{
		scsi_hd_log("SCSI HD %i: Attempting to execute a unknown command targeted at SCSI LUN %i\n", id, ((shdc[id].request_length >> 5) & 7));
		scsi_hd_invalid_lun(id);
		return 0;
	}

	if (!(scsi_hd_command_flags[cdb[0]] & IMPLEMENTED))
	{
		scsi_hd_log("SCSI HD %i: Attempting to execute unknown command %02X\n", id, cdb[0]);
		scsi_hd_illegal_opcode(id);
		return 0;
	}

	/* Unless the command is REQUEST SENSE, clear the sense. This will *NOT*
		the UNIT ATTENTION condition if it's set. */
	if (cdb[0] != GPCMD_REQUEST_SENSE)
	{
		scsi_hd_sense_clear(id, cdb[0]);
	}

	scsi_hd_log("SCSI HD %i: Continuing with command\n", id);
		
	return 1;
}

static void scsi_hd_seek(uint8_t id, uint32_t pos)
{
        /* scsi_hd_log("SCSI HD %i: Seek %08X\n", id, pos); */
        shdc[id].seek_pos   = pos;
}

static void scsi_hd_rezero(uint8_t id)
{
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

	/* scsi_hd_log("SCSI HD %i: Reporting sense: %02X %02X %02X\n", id, hdbufferb[2], hdbufferb[12], hdbufferb[13]); */

	/* Clear the sense stuff as per the spec. */
	scsi_hd_sense_clear(id, GPCMD_REQUEST_SENSE);
}

void scsi_hd_request_sense_for_scsi(uint8_t id, uint8_t *buffer, uint8_t alloc_length)
{
	/* Do *NOT* advance the unit attention phase. */

	scsi_hd_request_sense(id, buffer, alloc_length);
}

int scsi_hd_read_from_dma(uint8_t id);

void scsi_hd_command(uint8_t id, uint8_t *cdb)
{
	uint8_t *hdbufferb = (uint8_t *) shdc[id].buffer;
	uint32_t len;
	int pos=0;
	int max_len;
	unsigned idx = 0;
	unsigned size_idx;
	unsigned preamble_len;
	uint32_t alloc_length;
	int ret;
	uint64_t pos64;
	char device_identify[8] = { '8', '6', 'B', '_', 'H', 'D', '0', 0 };
	char device_identify_ex[14] = { '8', '6', 'B', '_', 'H', 'D', '0', ' ', 'v', '1', '.', '0', '0', 0 };

#if 0
	int CdbLength;
#endif
	shdc[id].status &= ~ERR_STAT;

	shdc[id].packet_len = 0;
	shdc[id].request_pos = 0;

	device_identify[6] = id + 0x30;

	device_identify_ex[6] = id + 0x30;
	device_identify_ex[9] = emulator_version[0];
	device_identify_ex[11] = emulator_version[2];
	device_identify_ex[12] = emulator_version[3];
	
	shdc[id].data_pos = 0;

	memcpy(shdc[id].current_cdb, cdb, shdc[id].cdb_len);

	if (cdb[0] != 0)
	{
		scsi_hd_log("SCSI HD %i: Command 0x%02X, Sense Key %02X, Asc %02X, Ascq %02X, %i\n", id, cdb[0], scsi_hd_sense_key, scsi_hd_asc, scsi_hd_ascq, ins);
		scsi_hd_log("SCSI HD %i: Request length: %04X\n", id, shdc[id].request_length);

#if 0
		for (CdbLength = 1; CdbLength < shdc[id].cdb_len; CdbLength++)
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
			len = (hdbufferb[7] << 16) | (hdbufferb[8] << 8) | hdbufferb[9];

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
					scsi_hd_log("SCSI HD %i: Length: %i, LBA: %i\n", id, shdc[id].sector_len, shdc[id].sector_pos);
					break;
				case GPCMD_READ_12:
					shdc[id].sector_len = (((uint32_t) cdb[6]) << 24) | (((uint32_t) cdb[7]) << 16) | (((uint32_t) cdb[8]) << 8) | ((uint32_t) cdb[9]);
					shdc[id].sector_pos = (((uint32_t) cdb[2]) << 24) | (((uint32_t) cdb[3]) << 16) | (((uint32_t) cdb[4]) << 8) | ((uint32_t) cdb[5]);
					break;
			}

			if (!shdc[id].sector_len)
			{
				/* scsi_hd_log("SCSI HD %i: All done - callback set\n", id); */
				shdc[id].packet_status = CDROM_PHASE_COMPLETE;
				shdc[id].callback = 20 * SCSI_TIME;
				break;
			}

			max_len = shdc[id].sector_len;
			shdc[id].requested_blocks = max_len;

#if 0
			ret = scsi_hd_read_blocks(id, &alloc_length, 1);
			if (ret <= 0)
			{
				return;
			}
#endif

			pos64 = (uint64_t) shdc[id].sector_pos;

			if (shdc[id].requested_blocks > 0)
			{
				fseeko64(shdf[id], pos64 << 9, SEEK_SET);
				fread(hdbufferb, (shdc[id].sector_len << 9), 1, shdf[id]);
			}

			alloc_length = shdc[id].packet_len = max_len << 9;
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
				update_status_bar_icon(0x23, 1);
			}
			else
			{
				update_status_bar_icon(0x23, 0);
			}
			return;

		case GPCMD_WRITE_6:
		case GPCMD_WRITE_10:
		case GPCMD_WRITE_12:
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

			if (!shdc[id].sector_len)
			{
				/* scsi_hd_log("SCSI HD %i: All done - callback set\n", id); */
				shdc[id].packet_status = CDROM_PHASE_COMPLETE;
				shdc[id].callback = 20 * SCSI_TIME;
				break;
			}

			max_len = shdc[id].sector_len;
			shdc[id].requested_blocks = max_len;

			scsi_hd_read_from_dma(id);

			pos64 = (uint64_t) shdc[id].sector_pos;

			if (shdc[id].requested_blocks > 0)
			{
				fseeko64(shdf[id], pos64 << 9, SEEK_SET);
				fwrite(hdbufferb, 1, (shdc[id].sector_len << 9), shdf[id]);
			}

			alloc_length = shdc[id].packet_len = max_len << 9;
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
				update_status_bar_icon(0x23, 1);
			}
			else
			{
				update_status_bar_icon(0x23, 0);
			}
			return;

		case GPCMD_INQUIRY:
			max_len = cdb[3];
			max_len <<= 8;
			max_len |= cdb[4];

			if (cdb[1] & 1)
			{
				preamble_len = 4;
				size_idx = 3;
					
				hdbufferb[idx++] = 05;
				hdbufferb[idx++] = cdb[2];
				hdbufferb[idx++] = 0;

				idx++;

				switch (cdb[2])
				{
					case 0x00:
						hdbufferb[idx++] = 0x00;
						hdbufferb[idx++] = 0x83;
						break;
					case 0x83:
						if (idx + 24 > max_len)
						{
							scsi_hd_data_phase_error(id);
							return;
						}

						hdbufferb[idx++] = 0x02;
						hdbufferb[idx++] = 0x00;
						hdbufferb[idx++] = 0x00;
						hdbufferb[idx++] = 20;
						ide_padstr8(hdbufferb + idx, 20, "53R141");	/* Serial */
						idx += 20;

						if (idx + 72 > cdb[4])
						{
							goto atapi_out;
						}
						hdbufferb[idx++] = 0x02;
						hdbufferb[idx++] = 0x01;
						hdbufferb[idx++] = 0x00;
						hdbufferb[idx++] = 68;
						ide_padstr8(hdbufferb + idx, 8, "86Box"); /* Vendor */
						idx += 8;
						ide_padstr8(hdbufferb + idx, 40, device_identify_ex); /* Product */
						idx += 40;
						ide_padstr8(hdbufferb + idx, 20, "53R141"); /* Product */
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

				memset(hdbufferb, 0, 8);
				hdbufferb[0] = 0; /*SCSI HD*/
				hdbufferb[1] = 0; /*Fixed*/
				hdbufferb[2] = 0x02; /*SCSI-2 compliant*/
				hdbufferb[3] = 0x02;
				hdbufferb[4] = 31;

				ide_padstr8(hdbufferb + 8, 8, "86Box"); /* Vendor */
				ide_padstr8(hdbufferb + 16, 16, device_identify); /* Product */
				ide_padstr8(hdbufferb + 32, 4, emulator_version); /* Revision */
				idx = 36;
			}

atapi_out:
			hdbufferb[size_idx] = idx - preamble_len;
			len=idx;

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
			scsi_hd_illegal_opcode(id);
			break;
	}

	/* scsi_hd_log("SCSI HD %i: Phase: %02X, request length: %i\n", shdc[id].phase, shdc[id].request_length); */
}

void scsi_hd_callback(uint8_t id);

int scsi_hd_read_from_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t *hdbufferb;

	uint8_t id = scsi_hard_disks[scsi_id][scsi_lun];

	hdbufferb = (uint8_t *) shdc[id].buffer;

	if (id > HDC_NUM)
	{
		return 0;
	}

	scsi_hd_log("Reading from SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, SCSIDevices[scsi_id][scsi_lun].InitLength);
	memcpy(hdbufferb, SCSIDevices[scsi_id][scsi_lun].CmdBuffer, SCSIDevices[scsi_id][scsi_lun].InitLength);
	return 1;
}

int scsi_hd_read_from_dma(uint8_t id)
{
	int ret = 0;

	ret = scsi_hd_read_from_scsi_dma(hdc[id].scsi_id, hdc[id].scsi_lun);

	if (!ret)
	{
		return 0;
	}

	return 0;
}

int scsi_hd_write_to_scsi_dma(uint8_t scsi_id, uint8_t scsi_lun)
{
	uint8_t *hdbufferb;

	uint8_t id = scsi_hard_disks[scsi_id][scsi_lun];

	if (id > HDC_NUM)
	{
		return 0;
	}

	hdbufferb = (uint8_t *) shdc[id].buffer;

	scsi_hd_log("Writing to SCSI DMA: SCSI ID %02X, init length %i\n", scsi_id, SCSIDevices[scsi_id][scsi_lun].InitLength);
	memcpy(SCSIDevices[scsi_id][scsi_lun].CmdBuffer, hdbufferb, SCSIDevices[scsi_id][scsi_lun].InitLength);
	scsi_hd_log("SCSI HD %i: Data from HD buffer:  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, hdbufferb[0], hdbufferb[1], hdbufferb[2], hdbufferb[3], hdbufferb[4], hdbufferb[5], hdbufferb[6], hdbufferb[7]);
	scsi_hd_log("SCSI HD %i: Data from SCSI DMA :  %02X %02X %02X %02X %02X %02X %02X %02X\n", id, SCSIDevices[scsi_id][scsi_lun].CmdBuffer[0], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[1], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[2], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[3], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[4], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[5], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[6], SCSIDevices[scsi_id][scsi_lun].CmdBuffer[7]);
	return 1;
}

int scsi_hd_write_to_dma(uint8_t id)
{
	int ret = 0;

	ret = scsi_hd_write_to_scsi_dma(hdc[id].scsi_id, hdc[id].scsi_lun);

	if (!ret)
	{
		return 0;
	}

	return 1;
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
		case CDROM_PHASE_COMMAND:
			scsi_hd_log("SCSI HD %i: PHASE_COMMAND\n", id);
			shdc[id].status = BUSY_STAT | (shdc[id].status &ERR_STAT);
			memcpy(shdc[id].hd_cdb, (uint8_t *) shdc[id].buffer, shdc[id].cdb_len);
			scsi_hd_command(id, shdc[id].hd_cdb);
			return;
		case CDROM_PHASE_COMPLETE:
			scsi_hd_log("SCSI HD %i: PHASE_COMPLETE\n", id);
			shdc[id].status = READY_STAT;
			shdc[id].phase = 3;
			shdc[id].packet_status = 0xFF;
			update_status_bar_icon(0x23, 0);
			return;
		case CDROM_PHASE_DATA_OUT:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_OUT\n", id);
			shdc[id].status = READY_STAT | DRQ_STAT | (shdc[id].status & ERR_STAT);
			shdc[id].phase = 0;
			return;
		case CDROM_PHASE_DATA_OUT_DMA:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_OUT_DMA\n", id);
			scsi_hd_read_from_dma(id);
			shdc[id].packet_status = CDROM_PHASE_COMPLETE;
			shdc[id].status = READY_STAT;
			shdc[id].phase = 3;
			update_status_bar_icon(0x23, 0);
			return;
		case CDROM_PHASE_DATA_IN:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_IN\n", id);
			shdc[id].status = READY_STAT | DRQ_STAT | (shdc[id].status & ERR_STAT);
			shdc[id].phase = 2;
			return;
		case CDROM_PHASE_DATA_IN_DMA:
			scsi_hd_log("SCSI HD %i: PHASE_DATA_IN_DMA\n", id);
			scsi_hd_write_to_dma(id);
			shdc[id].packet_status = CDROM_PHASE_COMPLETE;
			shdc[id].status = READY_STAT;
			shdc[id].phase = 3;
			update_status_bar_icon(0x23, 0);
			return;
		case CDROM_PHASE_ERROR:
			scsi_hd_log("SCSI HD %i: PHASE_ERROR\n", id);
			shdc[id].status = READY_STAT | ERR_STAT;
			shdc[id].phase = 3;
			return;
	}
}
