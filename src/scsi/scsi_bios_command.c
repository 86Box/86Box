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
 * Version:	@(#)scsi_bios_command.c	1.0.3	2017/10/04
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#include "../ibm.h"
#include "../dma.h"
#include "../device.h"
#include "scsi.h"
#include "scsi_bios_command.h"
#include "scsi_device.h"


#if ENABLE_SCSI_BIOS_COMMAND_LOG
int scsi_bios_command_do_log = ENABLE_SCSI_BIOS_COMMAND_LOG;
#endif


static void
cmd_log(const char *fmt, ...)
{
#if ENABLE_SCSI_BIOS_COMMAND_LOG
    va_list ap;

    if (scsi_bios_command_do_log) {
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}


static void
target_check(uint8_t id, uint8_t lun)
{
    if (! scsi_device_valid(id, lun)) {
	fatal("BIOS INT13 device on %02i:%02i has disappeared\n", id, lun);
    }
}


static uint8_t
completion_code(uint8_t *sense)
{
    switch (sense[12]) {
	case 0x00:
		return(0x00);

	case 0x20:
		return(0x01);

	case 0x12:
	case 0x21:
		return(0x02);

	case 0x27:
		return(0x03);

	case 0x14:
	case 0x16:
		return(0x04);

	case 0x10:
	case 0x11:
		return(0x10);

	case 0x17:
	case 0x18:
		return(0x11);

	case 0x01:
	case 0x03:
	case 0x05:
	case 0x06:
	case 0x07:
	case 0x08:
	case 0x09:
	case 0x1B:
	case 0x1C:
	case 0x1D:
	case 0x40:
	case 0x41:
	case 0x42:
	case 0x43:
	case 0x44:
	case 0x45:
	case 0x46:
	case 0x47:
	case 0x48:
	case 0x49:
		return(0x20);

	case 0x15:
	case 0x02:
		return(0x40);

	case 0x04:
	case 0x28:
	case 0x29:
	case 0x2a:
		return(0xaa);

	default:
		break;
    };

    return(0xff);
}


uint8_t
scsi_bios_command_08(uint8_t id, uint8_t lun, uint8_t *buffer)
{
    uint8_t cdb[12] = { GPCMD_READ_CDROM_CAPACITY, 0,0,0,0,0,0,0,0,0,0,0 };
    uint8_t rcbuf[8] = { 0,0,0,0,0,0,0,0 };
    uint32_t len = 0;
    int i, ret, sc;

    ret = scsi_device_read_capacity(id, lun, cdb, rcbuf, &len);
    sc = completion_code(scsi_device_sense(id, lun));
    if (ret == 0) return(sc);

    memset(buffer, 0x00, 6);
    for (i=0; i<4; i++)
	buffer[i] = rcbuf[i];
    for (i=4; i<6; i++)
	buffer[i] = rcbuf[(i + 2) ^ 1];
    cmd_log("BIOS Command 0x08: %02X %02X %02X %02X %02X %02X\n",
	buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

    return(0);
}


int
scsi_bios_command_15(uint8_t id, uint8_t lun, uint8_t *buffer)
{
    uint8_t cdb[12] = { GPCMD_READ_CDROM_CAPACITY, 0,0,0,0,0,0,0,0,0,0,0 };
    uint8_t rcbuf[8] = { 0,0,0,0,0,0,0,0 };
    uint32_t len = 0;
    int i, ret, sc;

    ret = scsi_device_read_capacity(id, lun, cdb, rcbuf, &len);
    sc = completion_code(scsi_device_sense(id, lun));

    memset(buffer, 0x00, 6);
    for (i=0; i<4; i++)
	buffer[i] = (ret == 0) ? 0 : rcbuf[i];

    scsi_device_type_data(id, lun, &(buffer[4]), &(buffer[5]));

    cmd_log("BIOS Command 0x15: %02X %02X %02X %02X %02X %02X\n",
	buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

    return(sc);
}


/* This returns the completion code. */
uint8_t
scsi_bios_command(uint8_t max_id, BIOSCMD *cmd, int8_t islba)
{
    uint8_t cdb[12] = { 0,0,0,0,0,0,0,0,0,0,0,0 };
    scsi_device_t *dev;
    uint32_t dma_address;
    uint32_t lba;
    int sector_len = cmd->secount;
    int block_shift;
    uint8_t ret;

    if (islba)
	lba = lba32_blk(cmd);
      else
	lba = (cmd->u.chs.cyl << 9) + (cmd->u.chs.head << 5) + cmd->u.chs.sec;

    cmd_log("BIOS Command = 0x%02X\n", cmd->command);	
	
    if ((cmd->id > max_id) || (cmd->lun > 7)) return(0x80);

    /* Get pointer to selected device. */
    dev = &SCSIDevices[cmd->id][cmd->lun];
    dev->InitLength = 0;

    if (! scsi_device_present(cmd->id, cmd->lun)) {
	cmd_log("BIOS Target ID %i and LUN %i have no device attached\n",
							cmd->id, cmd->lun);
	return(0x80);
    }

    dma_address = ADDR_TO_U32(cmd->dma_address);

    cmd_log("BIOS Data Buffer write: length %d, pointer 0x%04X\n",
					sector_len, dma_address);	

    if (dev->CmdBuffer != NULL) {
	free(dev->CmdBuffer);
	dev->CmdBuffer = NULL;
    }

    block_shift = scsi_device_block_shift(cmd->id, cmd->lun);

    switch(cmd->command) {
	case 0x00:	/* Reset Disk System, in practice it's a nop */
		return(0);

	case 0x01:	/* Read Status of Last Operation */
		target_check(cmd->id, cmd->lun);

		/*
		 * Assuming 14 bytes because that is the default
		 * length for SCSI sense, and no command-specific
		 * indication is given.
		 */
		dev->InitLength = 14;
		dev->CmdBuffer = (uint8_t *)malloc(14);
		memset(dev->CmdBuffer, 0x00, 14);

#if 0
		SCSIStatus = scsi_bios_command_08(cmd->id, cmd->lun, dev->CmdBuffer) ? SCSI_STATUS_OK : SCSI_STATUS_CHECK_CONDITION;
#endif

		if (sector_len > 0) {
			cmd_log("BIOS DMA: Reading 14 bytes at %08X\n",
							dma_address);
			DMAPageWrite(dma_address,
			    (char *)scsi_device_sense(cmd->id, cmd->lun), 14);
		}

		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(0);

	case 0x02:	/* Read Desired Sectors to Memory */
		target_check(cmd->id, cmd->lun);

		dev->InitLength = sector_len << block_shift;
		dev->CmdBuffer = (uint8_t *)malloc(dev->InitLength);
		memset(dev->CmdBuffer, 0x00, dev->InitLength);

		cdb[0] = GPCMD_READ_10;
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;
		cdb[7] = (sector_len >> 8) & 0xff;
		cdb[8] = sector_len & 0xff;
#if 0
		cmd_log("BIOS CMD(READ, %08lx, %d)\n", lba, cmd->secount);
#endif

		scsi_device_command(cmd->id, cmd->lun, 12, cdb);
		if (sector_len > 0) {
			cmd_log("BIOS DMA: Reading %i bytes at %08X\n",
					dev->InitLength, dma_address);
			DMAPageWrite(dma_address,
				     (char *)dev->CmdBuffer, dev->InitLength);
		}

		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x03:	/* Write Desired Sectors from Memory */
		target_check(cmd->id, cmd->lun);

		dev->InitLength = sector_len << block_shift;
		dev->CmdBuffer = (uint8_t *)malloc(dev->InitLength);
		memset(dev->CmdBuffer, 0x00, dev->InitLength);

		if (sector_len > 0) {
			cmd_log("BIOS DMA: Reading %i bytes at %08X\n",
					dev->InitLength, dma_address);
			DMAPageRead(dma_address,
				    (char *)dev->CmdBuffer, dev->InitLength);
		}

		cdb[0] = GPCMD_WRITE_10;
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;
		cdb[7] = (sector_len >> 8) & 0xff;
		cdb[8] = sector_len & 0xff;
#if 0
		cmd_log("BIOS CMD(WRITE, %08lx, %d)\n", lba, cmd->secount);
#endif

		scsi_device_command(cmd->id, cmd->lun, 12, cdb);

		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x04:	/* Verify Desired Sectors */
		target_check(cmd->id, cmd->lun);

		cdb[0] = GPCMD_VERIFY_10;
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;
		cdb[7] = (sector_len >> 8) & 0xff;
		cdb[8] = sector_len & 0xff;

		scsi_device_command(cmd->id, cmd->lun, 12, cdb);

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x05:	/* Format Track, invalid since SCSI has no tracks */
//FIXME: add a longer delay here --FvK
		return(1);

	case 0x06:	/* Identify SCSI Devices, in practice it's a nop */
//FIXME: add a longer delay here --FvK
		return(0);

	case 0x07:	/* Format Unit */
		target_check(cmd->id, cmd->lun);

		cdb[0] = GPCMD_FORMAT_UNIT;
		cdb[1] = (cmd->lun & 7) << 5;

		scsi_device_command(cmd->id, cmd->lun, 12, cdb);

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x08:	/* Read Drive Parameters */
		target_check(cmd->id, cmd->lun);

		dev->InitLength = 6;
		dev->CmdBuffer = (uint8_t *)malloc(dev->InitLength);
		memset(dev->CmdBuffer, 0x00, dev->InitLength);

		ret = scsi_bios_command_08(cmd->id, cmd->lun, dev->CmdBuffer);

		cmd_log("BIOS DMA: Reading 6 bytes at %08X\n", dma_address);
		DMAPageWrite(dma_address,
			     (char *)dev->CmdBuffer, dev->InitLength);

		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(ret);

	case 0x09:	/* Initialize Drive Pair Characteristics, in practice it's a nop */
//FIXME: add a longer delay here --FvK
		return(0);

	case 0x0c:	/* Seek */
		target_check(cmd->id, cmd->lun);

//FIXME: is this needed?  Looks like a copy-paste leftover.. --FvK
		dev->InitLength = sector_len << block_shift;

		cdb[0] = GPCMD_SEEK_10;
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;

		scsi_device_command(cmd->id, cmd->lun, 12, cdb);

		return((SCSIStatus == SCSI_STATUS_OK) ? 1 : 0);

	case 0x0d:	/* Alternate Disk Reset, in practice it's a nop */
//FIXME: add a longer delay here --FvK
		return(0);

	case 0x10:	/* Test Drive Ready */
		target_check(cmd->id, cmd->lun);

		cdb[0] = GPCMD_TEST_UNIT_READY;
		cdb[1] = (cmd->lun & 7) << 5;

		scsi_device_command(cmd->id, cmd->lun, 12, cdb);

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x11:	/* Recalibrate */
		target_check(cmd->id, cmd->lun);

		cdb[0] = GPCMD_REZERO_UNIT;
		cdb[1] = (cmd->lun & 7) << 5;

		scsi_device_command(cmd->id, cmd->lun, 12, cdb);

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x14:	/* Controller Diagnostic */
//FIXME: add a longer delay here --FvK
		return(0);

	case 0x15:	/* Read DASD Type */
		target_check(cmd->id, cmd->lun);

		dev->InitLength = 6;
		dev->CmdBuffer = (uint8_t *)malloc(dev->InitLength);
		memset(dev->CmdBuffer, 0x00, dev->InitLength);

		ret = scsi_bios_command_15(cmd->id, cmd->lun, dev->CmdBuffer);

		cmd_log("BIOS DMA: Reading 6 bytes at %08X\n", dma_address);
		DMAPageWrite(dma_address,
			     (char *)dev->CmdBuffer, dev->InitLength);

		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(ret);

	default:
		cmd_log("BIOS: Unimplemented command: %02X\n", cmd->command);
		return(1);
    }
	
    cmd_log("BIOS Request complete\n");
}
