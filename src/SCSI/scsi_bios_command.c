/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		The shared AHA and Buslogic SCSI BIOS command handler.
 *
 * Version:	@(#)scsi_bios_command.c	1.0.0	2017/08/26
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdlib.h>

#include "../ibm.h"
#include "../dma.h"
#include "scsi.h"
#include "scsi_bios_command.h"
#include "scsi_device.h"

static void
scsi_bios_command_log(const char *format, ...)
{
#ifdef ENABLE_SCSI_BIOS_COMMAND_LOG
	va_list ap;
	
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	fflush(stdout);
#endif
}

static uint8_t scsi_bios_completion_code(uint8_t *sense)
{
	switch (sense[12])
	{
		case 0x00:
			return 0x00;
		case 0x20:
			return 0x01;
		case 0x12:
		case 0x21:
			return 0x02;
		case 0x27:
			return 0x03;
		case 0x14: case 0x16:
			return 0x04;
		case 0x10: case 0x11:
			return 0x10;
		case 0x17: case 0x18:
			return 0x11;
		case 0x01: case 0x03: case 0x05: case 0x06: case 0x07: case 0x08: case 0x09:
		case 0x1B: case 0x1C: case 0x1D:
		case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46:
		case 0x47: case 0x48: case 0x49:
			return 0x20;
		case 0x15:
		case 0x02:
			return 0x40;
		case 0x04:
		case 0x28: case 0x29: case 0x2A:
			return 0xAA;
		default:
			return 0xFF;
	}
}

uint8_t scsi_bios_command_08(uint8_t id, uint8_t lun, uint8_t *buffer)
{
	uint32_t len = 0;
	uint8_t cdb[12] = { GPCMD_READ_CDROM_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t rcbuf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int ret = 0;
	int i = 0;
	uint8_t sc = 0;

	ret = scsi_device_read_capacity(id, lun, cdb, rcbuf, &len);
	sc = scsi_bios_completion_code(scsi_device_sense(id, lun));

	if (ret == 0)
	{
		return sc;
	}

	memset(buffer, 0, 6);

	for (i = 0; i < 4; i++)
	{
		buffer[i] = rcbuf[i];
	}

	for (i = 4; i < 6; i++)
	{
		buffer[i] = rcbuf[(i + 2) ^ 1];
	}

	scsi_bios_command_log("BIOS Command 0x08: %02X %02X %02X %02X %02X %02X\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
	
	return 0;
}

int scsi_bios_command_15(uint8_t id, uint8_t lun, uint8_t *buffer)
{
	uint32_t len = 0;
	uint8_t cdb[12] = { GPCMD_READ_CDROM_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint8_t rcbuf[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int ret = 0;
	int i = 0;
	uint8_t sc = 0;

	ret = scsi_device_read_capacity(id, lun, cdb, rcbuf, &len);
	sc = scsi_bios_completion_code(scsi_device_sense(id, lun));

	memset(buffer, 0, 6);

	for (i = 0; i < 4; i++)
	{
		buffer[i] = (ret == 0) ? 0 : rcbuf[i];
	}

	scsi_device_type_data(id, lun, &(buffer[4]), &(buffer[5]));

	scsi_bios_command_log("BIOS Command 0x15: %02X %02X %02X %02X %02X %02X\n", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
	
	return sc;
}

static void BuslogicIDCheck(uint8_t id, uint8_t lun)
{
	if (!scsi_device_valid(id, lun))
	{
		fatal("BIOS INT13 CD-ROM on %02i:%02i has disappeared\n", id, lun);
	}
}


/* This returns the completion code. */
uint8_t scsi_bios_command(uint8_t last_id, BIOSCMD *BiosCmd, int8_t islba)
{
	uint32_t dma_address;	
	uint32_t lba;
	int sector_len = BiosCmd->secount;
	int block_shift = 9;
	uint8_t ret = 0;
	uint8_t cdb[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	if (islba)
		lba = lba32_blk(BiosCmd);
	  else
	lba = (BiosCmd->u.chs.cyl << 9) + (BiosCmd->u.chs.head << 5) + BiosCmd->u.chs.sec;

	scsi_bios_command_log("BIOS Command = 0x%02X\n", BiosCmd->command);	
	
	if ((BiosCmd->id > last_id) || (BiosCmd->lun > 7)) {
		return 0x80;
	}

	SCSIDevices[BiosCmd->id][BiosCmd->lun].InitLength = 0;

	if (!scsi_device_present(BiosCmd->id, BiosCmd->lun)) 
	{
		scsi_bios_command_log("BIOS Target ID %i and LUN %i have no device attached\n",BiosCmd->id,BiosCmd->lun);
		return 0x80;
	}

	dma_address = ADDR_TO_U32(BiosCmd->dma_address);

	scsi_bios_command_log("BIOS Data Buffer write: length %d, pointer 0x%04X\n", sector_len, dma_address);	

	if (SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer != NULL)
	{
		free(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);
		SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = NULL;
	}

	block_shift = scsi_device_block_shift(BiosCmd->id, BiosCmd->lun);

	switch(BiosCmd->command)
	{
		case 0x00:	/* Reset Disk System, in practice it's a nop */
			return 0;

		case 0x01:	/* Read Status of Last Operation */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			/* Assuming 14 bytes because that's the default length for SCSI sense, and no command-specific
			   indication is given. */
			SCSIDevices[BiosCmd->id][BiosCmd->lun].InitLength = 14;

			SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = (uint8_t *) malloc(14);
			memset(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, 0, 14);

			/* SCSIStatus = scsi_bios_command_08(BiosCmd->id, BiosCmd->lun, SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer) ? SCSI_STATUS_OK : SCSI_STATUS_CHECK_CONDITION; */

			if (sector_len > 0) 
			{
				scsi_bios_command_log("BusLogic BIOS DMA: Reading 14 bytes at %08X\n", dma_address);
				DMAPageWrite(dma_address, (char *)scsi_device_sense(BiosCmd->id, BiosCmd->lun), 14);
			}

			if (SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer != NULL)
			{
				free(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);
				SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = NULL;
			}

			return 0;

			break;

		case 0x02:	/* Read Desired Sectors to Memory */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			SCSIDevices[BiosCmd->id][BiosCmd->lun].InitLength = sector_len << block_shift;

			SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = (uint8_t *) malloc(sector_len << block_shift);
			memset(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, 0, sector_len << block_shift);

			cdb[0] = GPCMD_READ_10;
			cdb[1] = (BiosCmd->lun & 7) << 5;
			cdb[2] = (lba >> 24) & 0xff;
			cdb[3] = (lba >> 16) & 0xff;
			cdb[4] = (lba >> 8) & 0xff;
			cdb[5] = lba & 0xff;
			cdb[7] = (sector_len >> 8) & 0xff;
			cdb[8] = sector_len & 0xff;
#if 0
pclog("BIOS CMD(READ, %08lx, %d)\n", lba, BiosCmd->secount);
#endif

			scsi_device_command(BiosCmd->id, BiosCmd->lun, 12, cdb);

			if (sector_len > 0) 
			{
				scsi_bios_command_log("BIOS DMA: Reading %i bytes at %08X\n", sector_len << block_shift, dma_address);
				DMAPageWrite(dma_address, (char *)SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, sector_len << block_shift);
			}

			if (SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer != NULL)
			{
				free(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);
				SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = NULL;
			}

			return scsi_bios_completion_code(scsi_device_sense(BiosCmd->id, BiosCmd->lun));

			break;

		case 0x03:	/* Write Desired Sectors from Memory */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			SCSIDevices[BiosCmd->id][BiosCmd->lun].InitLength = sector_len << block_shift;

			SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = (uint8_t *) malloc(sector_len << block_shift);
			memset(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, 0, sector_len << block_shift);

			if (sector_len > 0) 
			{
				scsi_bios_command_log("BIOS DMA: Reading %i bytes at %08X\n", sector_len << block_shift, dma_address);
				DMAPageRead(dma_address, (char *)SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, sector_len << block_shift);
			}

			cdb[0] = GPCMD_WRITE_10;
			cdb[1] = (BiosCmd->lun & 7) << 5;
			cdb[2] = (lba >> 24) & 0xff;
			cdb[3] = (lba >> 16) & 0xff;
			cdb[4] = (lba >> 8) & 0xff;
			cdb[5] = lba & 0xff;
			cdb[7] = (sector_len >> 8) & 0xff;
			cdb[8] = sector_len & 0xff;
#if 0
pclog("BIOS CMD(WRITE, %08lx, %d)\n", lba, BiosCmd->secount);
#endif

			scsi_device_command(BiosCmd->id, BiosCmd->lun, 12, cdb);

			if (SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer != NULL)
			{
				free(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);
				SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = NULL;
			}

			return scsi_bios_completion_code(scsi_device_sense(BiosCmd->id, BiosCmd->lun));

			break;

		case 0x04:	/* Verify Desired Sectors */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			cdb[0] = GPCMD_VERIFY_10;
			cdb[1] = (BiosCmd->lun & 7) << 5;
			cdb[2] = (lba >> 24) & 0xff;
			cdb[3] = (lba >> 16) & 0xff;
			cdb[4] = (lba >> 8) & 0xff;
			cdb[5] = lba & 0xff;
			cdb[7] = (sector_len >> 8) & 0xff;
			cdb[8] = sector_len & 0xff;

			scsi_device_command(BiosCmd->id, BiosCmd->lun, 12, cdb);

			return scsi_bios_completion_code(scsi_device_sense(BiosCmd->id, BiosCmd->lun));

			break;

		case 0x05:	/* Format Track, invalid since SCSI has no tracks */
			return 1;

			break;

		case 0x06:	/* Identify SCSI Devices, in practice it's a nop */
			return 0;

			break;

		case 0x07:	/* Format Unit */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			cdb[0] = GPCMD_FORMAT_UNIT;
			cdb[1] = (BiosCmd->lun & 7) << 5;

			scsi_device_command(BiosCmd->id, BiosCmd->lun, 12, cdb);

			return scsi_bios_completion_code(scsi_device_sense(BiosCmd->id, BiosCmd->lun));

			break;

		case 0x08:	/* Read Drive Parameters */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			SCSIDevices[BiosCmd->id][BiosCmd->lun].InitLength = 6;

			SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = (uint8_t *) malloc(6);
			memset(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, 0, 6);

			ret = scsi_bios_command_08(BiosCmd->id, BiosCmd->lun, SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);

			scsi_bios_command_log("BIOS DMA: Reading 6 bytes at %08X\n", dma_address);
			DMAPageWrite(dma_address, (char *)SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, 6);

			if (SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer != NULL)
			{
				free(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);
				SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = NULL;
			}

			return ret;

			break;

		case 0x09:	/* Initialize Drive Pair Characteristics, in practice it's a nop */
			return 0;

			break;

		case 0x0C:	/* Seek */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			SCSIDevices[BiosCmd->id][BiosCmd->lun].InitLength = sector_len << block_shift;

			cdb[0] = GPCMD_SEEK_10;
			cdb[1] = (BiosCmd->lun & 7) << 5;
			cdb[2] = (lba >> 24) & 0xff;
			cdb[3] = (lba >> 16) & 0xff;
			cdb[4] = (lba >> 8) & 0xff;
			cdb[5] = lba & 0xff;

			scsi_device_command(BiosCmd->id, BiosCmd->lun, 12, cdb);

			return (SCSIStatus == SCSI_STATUS_OK) ? 1 : 0;

			break;

		case 0x0D:	/* Alternate Disk Reset, in practice it's a nop */
			return 0;

			break;

		case 0x10:	/* Test Drive Ready */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			cdb[0] = GPCMD_TEST_UNIT_READY;
			cdb[1] = (BiosCmd->lun & 7) << 5;

			scsi_device_command(BiosCmd->id, BiosCmd->lun, 12, cdb);

			return scsi_bios_completion_code(scsi_device_sense(BiosCmd->id, BiosCmd->lun));

			break;

		case 0x11:	/* Recalibrate */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			cdb[0] = GPCMD_REZERO_UNIT;
			cdb[1] = (BiosCmd->lun & 7) << 5;

			scsi_device_command(BiosCmd->id, BiosCmd->lun, 12, cdb);

			return scsi_bios_completion_code(scsi_device_sense(BiosCmd->id, BiosCmd->lun));

			break;

		case 0x14:	/* Controller Diagnostic */
			return 0;

			break;

		case 0x15:	/* Read DASD Type */
			BuslogicIDCheck(BiosCmd->id, BiosCmd->lun);

			SCSIDevices[BiosCmd->id][BiosCmd->lun].InitLength = 6;

			SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = (uint8_t *) malloc(6);
			memset(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, 0, 6);

			ret = scsi_bios_command_15(BiosCmd->id, BiosCmd->lun, SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);

			scsi_bios_command_log("BusLogic BIOS DMA: Reading 6 bytes at %08X\n", dma_address);
			DMAPageWrite(dma_address, (char *)SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer, 6);

			if (SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer != NULL)
			{
				free(SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer);
				SCSIDevices[BiosCmd->id][BiosCmd->lun].CmdBuffer = NULL;
			}

			return ret;

			break;

		default:
			scsi_bios_command_log("BusLogic BIOS: Unimplemented command: %02X\n", BiosCmd->command);
			return 1;

			break;
	}
	
	pclog("BIOS Request complete\n");
}
