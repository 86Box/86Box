/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the code common to the AHA-154x series of
 *		SCSI Host Adapters made by Adaptec, Inc. and the BusLogic
 *		series of SCSI Host Adapters made by Mylex.
 *		These controllers were designed for various buses.
 *
 *
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/dma.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/mca.h>
#include <86box/mem.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/nvr.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/scsi_aha154x.h>
#include <86box/scsi_x54x.h>


#define X54X_RESET_DURATION_US	UINT64_C(50000)


static void	x54x_cmd_callback(void *priv);


#ifdef ENABLE_X54X_LOG
int x54x_do_log = ENABLE_X54X_LOG;


static void
x54x_log(const char *fmt, ...)
{
    va_list ap;

    if (x54x_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define x54x_log(fmt, ...)
#endif


static void
x54x_irq(x54x_t *dev, int set)
{
    int int_type = 0;
    int irq;

    if (dev->ven_get_irq)
	irq = dev->ven_get_irq(dev);
      else
	irq = dev->Irq;

    if (dev->card_bus & DEVICE_PCI) {
	x54x_log("PCI IRQ: %02X, PCI_INTA\n", dev->pci_slot);
        if (set)
       	        pci_set_irq(dev->pci_slot, PCI_INTA);
	else
       	        pci_clear_irq(dev->pci_slot, PCI_INTA);
    } else {
	x54x_log("%sing IRQ %i\n", set ? "Rais" : "Lower", irq);

	if (set) {
		if (dev->interrupt_type)
			int_type = dev->interrupt_type(dev);

		if (int_type)
			picintlevel(1 << irq);
		else
			picint(1 << irq);
	} else
		picintc(1 << irq);
    }
}


static void
raise_irq(x54x_t *dev, int suppress, uint8_t Interrupt)
{
    if (Interrupt & (INTR_MBIF | INTR_MBOA)) {
	x54x_log("%s: RaiseInterrupt(): Interrupt=%02X %s\n",
		dev->name, Interrupt, (! (dev->Interrupt & INTR_HACC)) ? "Immediate" : "Pending");
	if (! (dev->Interrupt & INTR_HACC)) {
		dev->Interrupt |= Interrupt;		/* Report now. */
	} else {
		dev->PendingInterrupt |= Interrupt;	/* Report later. */
	}
    } else if (Interrupt & INTR_HACC) {
	if (dev->Interrupt == 0 || dev->Interrupt == (INTR_ANY | INTR_HACC)) {
		x54x_log("%s: RaiseInterrupt(): Interrupt=%02X\n",
					dev->name, dev->Interrupt);
	}
	dev->Interrupt |= Interrupt;
    } else {
	x54x_log("%s: RaiseInterrupt(): Invalid interrupt state!\n", dev->name);
    }

    dev->Interrupt |= INTR_ANY;

    if (dev->IrqEnabled && !suppress)
	x54x_irq(dev, 1);
}


static void
clear_irq(x54x_t *dev)
{
    dev->Interrupt = 0;
    x54x_log("%s: lowering IRQ %i (stat 0x%02x)\n",
		dev->name, dev->Irq, dev->Interrupt);
    x54x_irq(dev, 0);
    if (dev->PendingInterrupt) {
	x54x_log("%s: Raising Interrupt 0x%02X (Pending)\n",
				dev->name, dev->Interrupt);
	if (dev->MailboxOutInterrupts || !(dev->Interrupt & INTR_MBOA)) {
		raise_irq(dev, 0, dev->PendingInterrupt);
	}
	dev->PendingInterrupt = 0;
    }
}


static void
target_check(x54x_t *dev, uint8_t id)
{
    if (! scsi_device_valid(&scsi_devices[dev->bus][id]))
	fatal("BIOS INT13 device on ID %02i has disappeared\n", id);
}


static uint8_t
completion_code(uint8_t *sense)
{
    uint8_t ret = 0xff;

    switch (sense[12]) {
	case ASC_NONE:
		ret = 0x00;
		break;

	case ASC_ILLEGAL_OPCODE:
	case ASC_INV_FIELD_IN_CMD_PACKET:
	case ASC_INV_FIELD_IN_PARAMETER_LIST:
	case ASC_DATA_PHASE_ERROR:
		ret = 0x01;
		break;

	case 0x12:
	case ASC_LBA_OUT_OF_RANGE:
		ret = 0x02;
		break;

	case ASC_WRITE_PROTECTED:
		ret = 0x03;
		break;

	case 0x14:
	case 0x16:
		ret = 0x04;
		break;

	case ASC_INCOMPATIBLE_FORMAT:
	case ASC_ILLEGAL_MODE_FOR_THIS_TRACK:
		ret = 0x0c;
		break;

	case 0x10:
	case 0x11:
		ret = 0x10;
		break;

	case 0x17:
	case 0x18:
		ret = 0x11;
		break;

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
		ret = 0x20;
		break;

	case 0x15:
	case 0x02:
		ret = 0x40;
		break;

	case 0x25:
		ret = 0x80;
		break;

	case ASC_NOT_READY:
	case ASC_MEDIUM_MAY_HAVE_CHANGED:
	case 0x29:
	case ASC_CAPACITY_DATA_CHANGED:
	case ASC_MEDIUM_NOT_PRESENT:
		ret = 0xaa;
		break;
    };

    return(ret);
}


static uint8_t
x54x_bios_scsi_command(scsi_device_t *dev, uint8_t *cdb, uint8_t *buf, int len, uint32_t addr, int transfer_size)
{
    dev->buffer_length = -1;

    scsi_device_command_phase0(dev, cdb);

    if (dev->phase == SCSI_PHASE_STATUS)
	return(completion_code(scsi_device_sense(dev)));

    if (len > 0) {
	if (dev->buffer_length == -1) {
		fatal("Buffer length -1 when doing SCSI DMA\n");
		return(0xff);
	}

	if (dev->phase == SCSI_PHASE_DATA_IN) {
		if (buf)
			memcpy(buf, dev->sc->temp_buffer, dev->buffer_length);
		else
			dma_bm_write(addr, dev->sc->temp_buffer, dev->buffer_length, transfer_size);
	} else if (dev->phase == SCSI_PHASE_DATA_OUT) {
		if (buf)
			memcpy(dev->sc->temp_buffer, buf, dev->buffer_length);
		else
			dma_bm_read(addr, dev->sc->temp_buffer, dev->buffer_length, transfer_size);
	}
    }

    scsi_device_command_phase1(dev);

    return (completion_code(scsi_device_sense(dev)));
}


static uint8_t
x54x_bios_read_capacity(scsi_device_t *sd, uint8_t *buf, int transfer_size)
{
    uint8_t *cdb;
    uint8_t ret;

    cdb = (uint8_t *) malloc(12);
    memset(cdb, 0, 12);
    cdb[0] = GPCMD_READ_CDROM_CAPACITY;

    memset(buf, 0, 8);
    ret = x54x_bios_scsi_command(sd, cdb, buf, 8, 0, transfer_size);

    free(cdb);

    return(ret);
}


static uint8_t
x54x_bios_inquiry(scsi_device_t *sd, uint8_t *buf, int transfer_size)
{
    uint8_t *cdb;
    uint8_t ret;

    cdb = (uint8_t *) malloc(12);
    memset(cdb, 0, 12);
    cdb[0] = GPCMD_INQUIRY;
    cdb[4] = 36;

    memset(buf, 0, 36);
    ret = x54x_bios_scsi_command(sd, cdb, buf, 36, 0, transfer_size);

    free(cdb);

    return(ret);
}


static uint8_t
x54x_bios_command_08(scsi_device_t *sd, uint8_t *buffer, int transfer_size)
{
    uint8_t *rcbuf;
    uint8_t ret;
    int i;

    memset(buffer, 0x00, 6);

    rcbuf = (uint8_t *) malloc(8);
    ret = x54x_bios_read_capacity(sd, rcbuf, transfer_size);
    if (ret) {
	free(rcbuf);
	return(ret);
   }

    memset(buffer, 0x00, 6);
    for (i=0; i<4; i++)
	buffer[i] = rcbuf[i];
    for (i=4; i<6; i++)
	buffer[i] = rcbuf[(i + 2) ^ 1];
    x54x_log("BIOS Command 0x08: %02X %02X %02X %02X %02X %02X\n",
	buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

    free(rcbuf);

    return(0);
}


static int
x54x_bios_command_15(scsi_device_t *sd, uint8_t *buffer, int transfer_size)
{
    uint8_t *inqbuf, *rcbuf;
    uint8_t ret;
    int i;

    memset(buffer, 0x00, 6);

    inqbuf = (uint8_t *) malloc(36);
    ret = x54x_bios_inquiry(sd, inqbuf, transfer_size);
    if (ret) {
	free(inqbuf);
	return(ret);
    }

    buffer[4] = inqbuf[0];
    buffer[5] = inqbuf[1];

    rcbuf = (uint8_t *) malloc(8);
    ret = x54x_bios_read_capacity(sd, rcbuf, transfer_size);
    if (ret) {
	free(rcbuf);
	free(inqbuf);
	return(ret);
   }

    for (i = 0; i < 4; i++)
	buffer[i] = rcbuf[i];

    x54x_log("BIOS Command 0x15: %02X %02X %02X %02X %02X %02X\n",
	buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

    free(rcbuf);
    free(inqbuf);

    return(0);
}


/* This returns the completion code. */
static uint8_t
x54x_bios_command(x54x_t *x54x, uint8_t max_id, BIOSCMD *cmd, int8_t islba)
{
    const int bios_cmd_to_scsi[18] = { 0, 0, GPCMD_READ_10, GPCMD_WRITE_10, GPCMD_VERIFY_10, 0, 0,
				       GPCMD_FORMAT_UNIT, 0, 0, 0, 0, GPCMD_SEEK_10, 0, 0, 0,
				       GPCMD_TEST_UNIT_READY, GPCMD_REZERO_UNIT };
    uint8_t cdb[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t *buf;
    scsi_device_t *dev = NULL;
    uint32_t dma_address = 0;
    uint32_t lba;
    int sector_len = cmd->secount;
    uint8_t ret = 0x00;

    if (islba)
	lba = lba32_blk(cmd);
      else
	lba = (cmd->u.chs.cyl << 9) + (cmd->u.chs.head << 5) + cmd->u.chs.sec;

    x54x_log("BIOS Command = 0x%02X\n", cmd->command);

    if (cmd->id > max_id) {
	x54x_log("BIOS Target ID %i or LUN %i are above maximum\n",
						cmd->id, cmd->lun);
	ret = 0x80;
    }

    if (cmd->lun) {
	x54x_log("BIOS Target LUN is not 0\n");
	ret = 0x80;
    }

    if (!ret) {
	/* Get pointer to selected device. */
	dev = &scsi_devices[x54x->bus][cmd->id];
	dev->buffer_length = 0;

	if (! scsi_device_present(dev)) {
		x54x_log("BIOS Target ID %i has no device attached\n", cmd->id);
		ret = 0x80;
	} else {
		scsi_device_identify(dev, 0xff);

		if ((dev->type == SCSI_REMOVABLE_CDROM) && !(x54x->flags & X54X_CDROM_BOOT)) {
			x54x_log("BIOS Target ID %i is CD-ROM on unsupported BIOS\n", cmd->id);
			return(0x80);
		} else {
			dma_address = ADDR_TO_U32(cmd->dma_address);

			x54x_log("BIOS Data Buffer write: length %d, pointer 0x%04X\n",
				 sector_len, dma_address);
		}
	}
    }

    if (!ret)  switch(cmd->command) {
	case 0x00:	/* Reset Disk System, in practice it's a nop */
		ret = 0x00;
		break;

	case 0x01:	/* Read Status of Last Operation */
		target_check(x54x, cmd->id);

		/*
		 * Assuming 14 bytes because that is the default
		 * length for SCSI sense, and no command-specific
		 * indication is given.
		 */
		if (sector_len > 0) {
			x54x_log("BIOS DMA: Reading 14 bytes at %08X\n",
							dma_address);
			dma_bm_write(dma_address, scsi_device_sense(dev), 14, x54x->transfer_size);
		}

		return(0);
		break;

	case 0x02:	/* Read Desired Sectors to Memory */
	case 0x03:	/* Write Desired Sectors from Memory */
	case 0x04:	/* Verify Desired Sectors */
	case 0x0c:	/* Seek */
		target_check(x54x, cmd->id);

		cdb[0] = bios_cmd_to_scsi[cmd->command];
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;
		if (cmd->command != 0x0c)
			cdb[8] = sector_len;

		ret = x54x_bios_scsi_command(dev, cdb, NULL, sector_len, dma_address, x54x->transfer_size);
		if (cmd->command == 0x0c)
			ret = !!ret;
		break;

	default:
		x54x_log("BIOS: Unimplemented command: %02X\n", cmd->command);
	case 0x05:	/* Format Track, invalid since SCSI has no tracks */
	case 0x0a:	/* ???? */
	case 0x0b:	/* ???? */
	case 0x12:	/* ???? */
	case 0x13:	/* ???? */
//FIXME: add a longer delay here --FvK
		ret = 0x01;
		break;

	case 0x06:	/* Identify SCSI Devices, in practice it's a nop */
	case 0x09:	/* Initialize Drive Pair Characteristics, in practice it's a nop */
	case 0x0d:	/* Alternate Disk Reset, in practice it's a nop */
	case 0x0e:	/* Read Sector Buffer */
	case 0x0f:	/* Write Sector Buffer */
	case 0x14:	/* Controller Diagnostic */
//FIXME: add a longer delay here --FvK
		ret = 0x00;
		break;

	case 0x07:	/* Format Unit */
	case 0x10:	/* Test Drive Ready */
	case 0x11:	/* Recalibrate */
		target_check(x54x, cmd->id);

		cdb[0] = bios_cmd_to_scsi[cmd->command];
		cdb[1] = (cmd->lun & 7) << 5;

		ret = x54x_bios_scsi_command(dev, cdb, NULL, sector_len, dma_address, x54x->transfer_size);
		break;

	case 0x08:	/* Read Drive Parameters */
	case 0x15:	/* Read DASD Type */
		target_check(x54x, cmd->id);

		dev->buffer_length = 6;

		buf = (uint8_t *) malloc(6);
		if (cmd->command == 0x08)
			ret = x54x_bios_command_08(dev, buf, x54x->transfer_size);
		else
			ret = x54x_bios_command_15(dev, buf, x54x->transfer_size);

		x54x_log("BIOS DMA: Reading 6 bytes at %08X\n", dma_address);
		dma_bm_write(dma_address, buf, 4, x54x->transfer_size);
		free(buf);

		break;
    }
	
    x54x_log("BIOS Request %02X complete: %02X\n", cmd->command, ret);
    return(ret);
}


static void
x54x_cmd_done(x54x_t *dev, int suppress)
{
    int fast = 0;

    dev->DataReply = 0;
    dev->Status |= STAT_IDLE;

    if (dev->ven_cmd_is_fast) {
	fast = dev->ven_cmd_is_fast(dev);
    }

    if ((dev->Command != CMD_START_SCSI) || fast) {
	dev->Status &= ~STAT_DFULL;
	x54x_log("%s: Raising IRQ %i\n", dev->name, dev->Irq);
	raise_irq(dev, suppress, INTR_HACC);
    }

    dev->Command = 0xff;
    dev->CmdParam = 0;
}


static void
x54x_add_to_period(x54x_t *dev, int TransferLength)
{
    dev->temp_period += (uint64_t) TransferLength;
}


static void
x54x_mbi_setup(x54x_t *dev, uint32_t CCBPointer, CCBU *CmdBlock,
	       uint8_t HostStatus, uint8_t TargetStatus, uint8_t mbcc)
{
    Req_t *req = &dev->Req;

    req->CCBPointer = CCBPointer;
    memcpy(&(req->CmdBlock), CmdBlock, sizeof(CCB32));
    req->Is24bit = !!(dev->flags & X54X_MBX_24BIT);
    req->HostStatus = HostStatus;
    req->TargetStatus = TargetStatus;
    req->MailboxCompletionCode = mbcc;

    x54x_log("Mailbox in setup\n");
}


static void
x54x_ccb(x54x_t *dev)
{
    Req_t *req = &dev->Req;
    uint8_t bytes[4] = { 0, 0, 0, 0};

    /* Rewrite the CCB up to the CDB. */
    x54x_log("CCB completion code and statuses rewritten (pointer %08X)\n", req->CCBPointer);
    dma_bm_read(req->CCBPointer + 0x000C, (uint8_t *) bytes, 4, dev->transfer_size);
    bytes[1] = req->MailboxCompletionCode;
    bytes[2] = req->HostStatus;
    bytes[3] = req->TargetStatus;
    dma_bm_write(req->CCBPointer + 0x000C, (uint8_t *) bytes, 4, dev->transfer_size);
    x54x_add_to_period(dev, 3);

    if (dev->MailboxOutInterrupts)
	dev->ToRaise = INTR_MBOA | INTR_ANY;
      else
	dev->ToRaise = 0;
}


static void
x54x_mbi(x54x_t *dev)
{	
    Req_t *req = &dev->Req;
//  uint32_t CCBPointer = req->CCBPointer;
    addr24 CCBPointer;
    CCBU *CmdBlock = &(req->CmdBlock);
    uint8_t HostStatus = req->HostStatus;
    uint8_t TargetStatus = req->TargetStatus;
    uint32_t MailboxCompletionCode = req->MailboxCompletionCode;
    uint32_t Incoming;
    uint8_t bytes[4] = { 0, 0, 0, 0 };

    Incoming = dev->MailboxInAddr + (dev->MailboxInPosCur * ((dev->flags & X54X_MBX_24BIT) ? sizeof(Mailbox_t) : sizeof(Mailbox32_t)));

    if (MailboxCompletionCode != MBI_NOT_FOUND) {
	CmdBlock->common.HostStatus = HostStatus;
	CmdBlock->common.TargetStatus = TargetStatus;

	/* Rewrite the CCB up to the CDB. */
	x54x_log("CCB statuses rewritten (pointer %08X)\n", req->CCBPointer);
	dma_bm_read(req->CCBPointer + 0x000C, (uint8_t *) bytes, 4, dev->transfer_size);
	bytes[2] = req->HostStatus;
	bytes[3] = req->TargetStatus;
    	dma_bm_write(req->CCBPointer + 0x000C, (uint8_t *) bytes, 4, dev->transfer_size);
	x54x_add_to_period(dev, 2);
    } else {
	x54x_log("Mailbox not found!\n");
    }

    x54x_log("Host Status 0x%02X, Target Status 0x%02X\n",HostStatus,TargetStatus);

    if (dev->flags & X54X_MBX_24BIT) {
	U32_TO_ADDR(CCBPointer, req->CCBPointer);
	x54x_log("Mailbox 24-bit: Status=0x%02X, CCB at 0x%04X\n", req->MailboxCompletionCode, CCBPointer);
	bytes[0] = req->MailboxCompletionCode;
	memcpy(&(bytes[1]), (uint8_t *)&CCBPointer, 3);
	dma_bm_write(Incoming, (uint8_t *) bytes, 4, dev->transfer_size);
	x54x_add_to_period(dev, 4);
	x54x_log("%i bytes of 24-bit mailbox written to: %08X\n", sizeof(Mailbox_t), Incoming);
    } else {
	x54x_log("Mailbox 32-bit: Status=0x%02X, CCB at 0x%04X\n", req->MailboxCompletionCode, CCBPointer);
	dma_bm_write(Incoming, (uint8_t *)&(req->CCBPointer), 4, dev->transfer_size);
	dma_bm_read(Incoming + 4, (uint8_t *) bytes, 4, dev->transfer_size);
	bytes[0] = req->HostStatus;
	bytes[1] = req->TargetStatus;
	bytes[3] = req->MailboxCompletionCode;
	dma_bm_write(Incoming + 4, (uint8_t *) bytes, 4, dev->transfer_size);
	x54x_add_to_period(dev, 7);
	x54x_log("%i bytes of 32-bit mailbox written to: %08X\n", sizeof(Mailbox32_t), Incoming);
    }

    dev->MailboxInPosCur++;
    if (dev->MailboxInPosCur >= dev->MailboxCount)
		dev->MailboxInPosCur = 0;

    dev->ToRaise = INTR_MBIF | INTR_ANY;
    if (dev->MailboxOutInterrupts)
	dev->ToRaise |= INTR_MBOA;
}


static void
x54x_rd_sge(x54x_t *dev, int Is24bit, uint32_t Address, SGE32 *SG)
{
    SGE SGE24;
    uint8_t bytes[8];

    if (Is24bit) {
	if (dev->transfer_size == 4) {
		/* 32-bit device, do this to make the transfer divisible by 4 bytes. */
		dma_bm_read(Address, (uint8_t *) bytes, 8, dev->transfer_size);
		memcpy((uint8_t *)&SGE24, bytes, sizeof(SGE));
	} else {
		/* 16-bit device, special handling not needed. */
		dma_bm_read(Address, (uint8_t *)&SGE24, 8, dev->transfer_size);
	}
	x54x_add_to_period(dev, sizeof(SGE));

	/* Convert the 24-bit entries into 32-bit entries. */
	x54x_log("Read S/G block: %06X, %06X\n", SGE24.Segment, SGE24.SegmentPointer);
	SG->Segment = ADDR_TO_U32(SGE24.Segment);
	SG->SegmentPointer = ADDR_TO_U32(SGE24.SegmentPointer);
    } else {
	dma_bm_read(Address, (uint8_t *)SG, sizeof(SGE32), dev->transfer_size);
	x54x_add_to_period(dev, sizeof(SGE32));
    }
}


static int
x54x_get_length(x54x_t *dev, Req_t *req, int Is24bit)
{
    uint32_t DataPointer, DataLength;
    uint32_t SGEntryLength = (Is24bit ? sizeof(SGE) : sizeof(SGE32));
    SGE32 SGBuffer;
    uint32_t DataToTransfer = 0, i = 0;

    if (Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
	x54x_log("Data length: %08X\n", req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;
    }
    x54x_log("Data Buffer write: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);

    if (!DataLength)
	return(0);

    if (req->CmdBlock.common.ControlByte != 0x03) {
	if (req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND ||
	    req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES) {
		for (i = 0; i < DataLength; i += SGEntryLength) {
			x54x_rd_sge(dev, Is24bit, DataPointer + i, &SGBuffer);

			DataToTransfer += SGBuffer.Segment;
		}
		return(DataToTransfer);
	} else if (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
		   req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) {
		return(DataLength);
	} else {
		return(0);
	}
    } else {
	return(0);
    }
}


static void
x54x_set_residue(x54x_t *dev, Req_t *req, int32_t TransferLength)
{
    uint32_t Residue = 0;
    addr24 Residue24;
    int32_t BufLen = scsi_devices[dev->bus][req->TargetID].buffer_length;
    uint8_t bytes[4] = { 0, 0, 0, 0 };

    if ((req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) ||
	(req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {

	if ((TransferLength > 0) && (req->CmdBlock.common.ControlByte < 0x03)) {
		TransferLength -= BufLen;
		if (TransferLength > 0)
			Residue = TransferLength;
	}

	if (req->Is24bit) {
		U32_TO_ADDR(Residue24, Residue);
		dma_bm_read(req->CCBPointer + 0x0004, (uint8_t *) bytes, 4, dev->transfer_size);
		memcpy((uint8_t *) bytes, (uint8_t *)&Residue24, 3);
		dma_bm_write(req->CCBPointer + 0x0004, (uint8_t *) bytes, 4, dev->transfer_size);
		x54x_add_to_period(dev, 3);
		x54x_log("24-bit Residual data length for reading: %d\n", Residue);
	} else {
    		dma_bm_write(req->CCBPointer + 0x0004, (uint8_t *)&Residue, 4, dev->transfer_size);
		x54x_add_to_period(dev, 4);
		x54x_log("32-bit Residual data length for reading: %d\n", Residue);
	}
    }
}


static void
x54x_buf_dma_transfer(x54x_t *dev, Req_t *req, int Is24bit, int TransferLength, int dir)
{
    uint32_t DataPointer, DataLength;
    uint32_t SGEntryLength = (Is24bit ? sizeof(SGE) : sizeof(SGE32));
    uint32_t Address, i;
    int32_t BufLen = scsi_devices[dev->bus][req->TargetID].buffer_length;
    uint8_t read_from_host = (dir && ((req->CmdBlock.common.ControlByte == CCB_DATA_XFER_OUT) || (req->CmdBlock.common.ControlByte == 0x00)));
    uint8_t write_to_host = (!dir && ((req->CmdBlock.common.ControlByte == CCB_DATA_XFER_IN) || (req->CmdBlock.common.ControlByte == 0x00)));
    int sg_pos = 0;
    SGE32 SGBuffer;
    uint32_t DataToTransfer = 0;

    if (Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;
    }
    x54x_log("Data Buffer %s: length %d (%u), pointer 0x%04X\n",
	     dir ? "write" : "read", BufLen, DataLength, DataPointer);

    if ((req->CmdBlock.common.ControlByte != 0x03) && TransferLength && BufLen) {
	if ((req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND) ||
	    (req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {

		/* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
		   checking its length, so do this procedure for both no read/write commands. */
		if ((DataLength > 0) && (req->CmdBlock.common.ControlByte < 0x03)) {
			for (i = 0; i < DataLength; i += SGEntryLength) {
				x54x_rd_sge(dev, Is24bit, DataPointer + i, &SGBuffer);

				Address = SGBuffer.SegmentPointer;
				DataToTransfer = MIN((int) SGBuffer.Segment, BufLen);

				if (read_from_host && DataToTransfer) {
					x54x_log("Reading S/G segment %i: length %i, pointer %08X\n", i, DataToTransfer, Address);
					dma_bm_read(Address, &(scsi_devices[dev->bus][req->TargetID].sc->temp_buffer[sg_pos]), DataToTransfer, dev->transfer_size);
				}
				else if (write_to_host && DataToTransfer) {
					x54x_log("Writing S/G segment %i: length %i, pointer %08X\n", i, DataToTransfer, Address);
					dma_bm_write(Address, &(scsi_devices[dev->bus][req->TargetID].sc->temp_buffer[sg_pos]), DataToTransfer, dev->transfer_size);
				}
				else
					x54x_log("No action on S/G segment %i: length %i, pointer %08X\n", i, DataToTransfer, Address);

				sg_pos += SGBuffer.Segment;

				BufLen -= SGBuffer.Segment;
				if (BufLen < 0)
					BufLen = 0;

				x54x_log("After S/G segment done: %i, %i\n", sg_pos, BufLen);
			}
		}
	} else if ((req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND) ||
		   (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES)) {
		Address = DataPointer;

		if ((DataLength > 0) && (BufLen > 0) && (req->CmdBlock.common.ControlByte < 0x03)) {
			if (read_from_host)
				dma_bm_read(Address, scsi_devices[dev->bus][req->TargetID].sc->temp_buffer, MIN(BufLen, (int) DataLength), dev->transfer_size);
			else if (write_to_host)
				dma_bm_write(Address, scsi_devices[dev->bus][req->TargetID].sc->temp_buffer, MIN(BufLen, (int) DataLength), dev->transfer_size);
		}
	}
    }
}


static uint8_t
ConvertSenseLength(uint8_t RequestSenseLength)
{
    x54x_log("Unconverted Request Sense length %i\n", RequestSenseLength);

    if (RequestSenseLength == 0)
	RequestSenseLength = 14;
    else if (RequestSenseLength == 1)
	RequestSenseLength = 0;

    x54x_log("Request Sense length %i\n", RequestSenseLength);

    return(RequestSenseLength);
}


uint32_t
SenseBufferPointer(Req_t *req)
{
    uint32_t SenseBufferAddress;
    if (req->Is24bit) {
	SenseBufferAddress = req->CCBPointer;
	SenseBufferAddress += req->CmdBlock.common.CdbLength + 18;
    } else {
	SenseBufferAddress = req->CmdBlock.new.SensePointer;
    }

    return(SenseBufferAddress);
}


static void
SenseBufferFree(x54x_t *dev, Req_t *req, int Copy)
{
    uint8_t SenseLength = ConvertSenseLength(req->CmdBlock.common.RequestSenseLength);
    uint32_t SenseBufferAddress;
    uint8_t temp_sense[256];

    if (SenseLength && Copy) {
        scsi_device_request_sense(&scsi_devices[dev->bus][req->TargetID], temp_sense, SenseLength);

	/*
	 * The sense address, in 32-bit mode, is located in the
	 * Sense Pointer of the CCB, but in 24-bit mode, it is
	 * located at the end of the Command Descriptor Block.
	 */
	SenseBufferAddress = SenseBufferPointer(req);

	x54x_log("Request Sense address: %02X\n", SenseBufferAddress);

	x54x_log("SenseBufferFree(): Writing %i bytes at %08X\n",
					SenseLength, SenseBufferAddress);
	dma_bm_write(SenseBufferAddress, temp_sense, SenseLength, dev->transfer_size);
	x54x_add_to_period(dev, SenseLength);
	x54x_log("Sense data written to buffer: %02X %02X %02X\n",
		temp_sense[2], temp_sense[12], temp_sense[13]);
    }
}


static void
x54x_scsi_cmd(x54x_t *dev)
{
    Req_t *req = &dev->Req;
    uint8_t bit24 = !!req->Is24bit;
    uint32_t i, target_cdb_len = 12;
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][req->TargetID];

    target_cdb_len = 12;
    dev->target_data_len = x54x_get_length(dev, req, bit24);

    if (!scsi_device_valid(sd))
	fatal("SCSI target on %02i has disappeared\n", req->TargetID);

    x54x_log("dev->target_data_len = %i\n", dev->target_data_len);

    x54x_log("SCSI command being executed on ID %i, LUN %i\n", req->TargetID, req->LUN);

    x54x_log("SCSI CDB[0]=0x%02X\n", req->CmdBlock.common.Cdb[0]);
    for (i=1; i<req->CmdBlock.common.CdbLength; i++)
	x54x_log("SCSI CDB[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);

    memset(dev->temp_cdb, 0x00, target_cdb_len);
    if (req->CmdBlock.common.CdbLength <= target_cdb_len) {
	memcpy(dev->temp_cdb, req->CmdBlock.common.Cdb,
	       req->CmdBlock.common.CdbLength);
	x54x_add_to_period(dev, req->CmdBlock.common.CdbLength);
    } else {
	memcpy(dev->temp_cdb, req->CmdBlock.common.Cdb, target_cdb_len);
	x54x_add_to_period(dev, target_cdb_len);
    }

    dev->Residue = 0;

    sd->buffer_length = dev->target_data_len;

    scsi_device_command_phase0(sd, dev->temp_cdb);
    dev->scsi_cmd_phase = sd->phase;

    x54x_log("Control byte: %02X\n", (req->CmdBlock.common.ControlByte == 0x03));

    if (dev->scsi_cmd_phase == SCSI_PHASE_STATUS)
	dev->callback_sub_phase = 3;
    else
	dev->callback_sub_phase = 2;

    x54x_log("scsi_devices[%02i][%02i].Status = %02X\n", dev->bus, req->TargetID, sd->status);
}


static void
x54x_scsi_cmd_phase1(x54x_t *dev)
{
    Req_t *req = &dev->Req;
    double p;
    uint8_t bit24 = !!req->Is24bit;
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][req->TargetID];

    if (dev->scsi_cmd_phase != SCSI_PHASE_STATUS) {
	if ((dev->temp_cdb[0] != 0x03) || (req->CmdBlock.common.ControlByte != 0x03)) {
		p = scsi_device_get_callback(sd);
		if (p <= 0.0)
			x54x_add_to_period(dev, sd->buffer_length);
		else
			dev->media_period += p;
		x54x_buf_dma_transfer(dev, req, bit24, dev->target_data_len, (dev->scsi_cmd_phase == SCSI_PHASE_DATA_OUT));
		scsi_device_command_phase1(sd);
	}
    }

    dev->callback_sub_phase = 3;
    x54x_log("scsi_devices[%02xi][%02i].Status = %02X\n", x54x->bus, req->TargetID, sd->status);
}


static void
x54x_request_sense(x54x_t *dev)
{
    Req_t *req = &dev->Req;
    uint32_t SenseBufferAddress;
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][req->TargetID];

    if (dev->scsi_cmd_phase != SCSI_PHASE_STATUS) {
	if ((dev->temp_cdb[0] == 0x03) && (req->CmdBlock.common.ControlByte == 0x03)) {
		/* Request sense in non-data mode - sense goes to sense buffer. */
		sd->buffer_length = ConvertSenseLength(req->CmdBlock.common.RequestSenseLength);
		if ((sd->status != SCSI_STATUS_OK) && (sd->buffer_length > 0)) {
			SenseBufferAddress = SenseBufferPointer(req);
			dma_bm_write(SenseBufferAddress, scsi_devices[dev->bus][req->TargetID].sc->temp_buffer, sd->buffer_length, dev->transfer_size);
			x54x_add_to_period(dev, sd->buffer_length);
		}
		scsi_device_command_phase1(sd);
	} else
		SenseBufferFree(dev, req, (sd->status != SCSI_STATUS_OK));
    } else
	SenseBufferFree(dev, req, (sd->status != SCSI_STATUS_OK));

    x54x_set_residue(dev, req, dev->target_data_len);

    x54x_log("Request complete\n");

    if (sd->status == SCSI_STATUS_OK) {
	x54x_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
		       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
    } else if (sd->status == SCSI_STATUS_CHECK_CONDITION) {
	x54x_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
		       CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION, MBI_ERROR);
    }

    dev->callback_sub_phase = 4;
    x54x_log("scsi_devices[%02i][%02i].Status = %02X\n", dev->bus, req->TargetID, sd->status);
}


static void
x54x_mbo_free(x54x_t *dev)
{
    uint8_t CmdStatus = MBO_FREE;
    uint32_t CodeOffset = 0;

    CodeOffset = (dev->flags & X54X_MBX_24BIT) ? 0 : 7;

    x54x_log("x54x_mbo_free(): Writing %i bytes at %08X\n", sizeof(CmdStatus), dev->Outgoing + CodeOffset);
    dma_bm_write(dev->Outgoing + CodeOffset, &CmdStatus, 1, dev->transfer_size);
}


static void
x54x_notify(x54x_t *dev)
{
    Req_t *req = &dev->Req;
    scsi_device_t *sd;

    sd = &scsi_devices[dev->bus][req->TargetID];

    x54x_mbo_free(dev);

    if (dev->MailboxIsBIOS)
	x54x_ccb(dev);
    else
	x54x_mbi(dev);

    /* Make sure to restore device to non-IDENTIFY'd state as we disconnect. */
    if (sd->type != SCSI_NONE)
	scsi_device_identify(sd, SCSI_LUN_USE_CDB);
}


static void
x54x_req_setup(x54x_t *dev, uint32_t CCBPointer, Mailbox32_t *Mailbox32)
{	
    Req_t *req = &dev->Req;
    uint8_t id, lun;
    scsi_device_t *sd;

    /* Fetch data from the Command Control Block. */
    dma_bm_read(CCBPointer, (uint8_t *)&req->CmdBlock, sizeof(CCB32), dev->transfer_size);
    x54x_add_to_period(dev, sizeof(CCB32));

    req->Is24bit = !!(dev->flags & X54X_MBX_24BIT);
    req->CCBPointer = CCBPointer;
    req->TargetID = req->Is24bit ? req->CmdBlock.old.Id : req->CmdBlock.new.Id;
    req->LUN = req->Is24bit ? req->CmdBlock.old.Lun : req->CmdBlock.new.Lun;

    id = req->TargetID;
    sd = &scsi_devices[dev->bus][id];
    lun = req->LUN;
    if ((id > dev->max_id) || (lun > 7)) {
	x54x_log("SCSI Target ID %i or LUN %i is not valid\n",id,lun);
	x54x_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		      CCB_SELECTION_TIMEOUT, SCSI_STATUS_OK, MBI_ERROR);
	dev->callback_sub_phase = 4;
	return;
    }

    x54x_log("Scanning SCSI Target ID %i\n", id);

    sd->status = SCSI_STATUS_OK;

    if (!scsi_device_present(sd) || (lun > 0)) {
	x54x_log("SCSI Target ID %i and LUN %i have no device attached\n",id,lun);
	x54x_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		       CCB_SELECTION_TIMEOUT, SCSI_STATUS_OK, MBI_ERROR);
	dev->callback_sub_phase = 4;
    } else {
	x54x_log("SCSI Target ID %i detected and working\n", id);
	scsi_device_identify(sd, lun);

	x54x_log("Transfer Control %02X\n", req->CmdBlock.common.ControlByte);
	x54x_log("CDB Length %i\n", req->CmdBlock.common.CdbLength);	
	x54x_log("CCB Opcode %x\n", req->CmdBlock.common.Opcode);		
	if ((req->CmdBlock.common.Opcode > 0x04) && (req->CmdBlock.common.Opcode != 0x81)) {
		x54x_log("Invalid opcode: %02X\n",
			req->CmdBlock.common.ControlByte);
		x54x_mbi_setup(dev, CCBPointer, &req->CmdBlock, CCB_INVALID_OP_CODE, SCSI_STATUS_OK, MBI_ERROR);
		dev->callback_sub_phase = 4;
		return;
	}
	if (req->CmdBlock.common.Opcode == 0x81) {
		x54x_log("Bus reset opcode\n");
		scsi_device_reset(sd);
		x54x_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
		dev->callback_sub_phase = 4;
		return;
	}

	dev->callback_sub_phase = 1;
    }
}


static void
x54x_req_abort(x54x_t *dev, uint32_t CCBPointer)
{
    CCBU CmdBlock;

    /* Fetch data from the Command Control Block. */
    dma_bm_read(CCBPointer, (uint8_t *)&CmdBlock, sizeof(CCB32), dev->transfer_size);
    x54x_add_to_period(dev, sizeof(CCB32));

    x54x_mbi_setup(dev, CCBPointer, &CmdBlock,
		  0x26, SCSI_STATUS_OK, MBI_NOT_FOUND);
    dev->callback_sub_phase = 4;
}


static uint32_t
x54x_mbo(x54x_t *dev, Mailbox32_t *Mailbox32)
{	
    Mailbox_t MailboxOut;
    uint32_t Outgoing;
    uint32_t ccbp;
    uint32_t Addr;
    uint32_t Cur;

    if (dev->MailboxIsBIOS) {
	Addr = dev->BIOSMailboxOutAddr;
	Cur = dev->BIOSMailboxOutPosCur;
    } else {
	Addr = dev->MailboxOutAddr;
	Cur = dev->MailboxOutPosCur;
    }

    if (dev->flags & X54X_MBX_24BIT) {
	Outgoing = Addr + (Cur * sizeof(Mailbox_t));
	dma_bm_read(Outgoing, (uint8_t *)&MailboxOut, sizeof(Mailbox_t), dev->transfer_size);
	x54x_add_to_period(dev, sizeof(Mailbox_t));

	ccbp = *(uint32_t *) &MailboxOut;
	Mailbox32->CCBPointer = (ccbp >> 24) | ((ccbp >> 8) & 0xff00) | ((ccbp << 8) & 0xff0000);
	Mailbox32->u.out.ActionCode = MailboxOut.CmdStatus;
    } else {
	Outgoing = Addr + (Cur * sizeof(Mailbox32_t));

	dma_bm_read(Outgoing, (uint8_t *)Mailbox32, sizeof(Mailbox32_t), dev->transfer_size);
	x54x_add_to_period(dev, sizeof(Mailbox32_t));
    }

    return(Outgoing);
}


uint8_t
x54x_mbo_process(x54x_t *dev)
{
    Mailbox32_t mb32;

    dev->ToRaise = 0;
    dev->Outgoing = x54x_mbo(dev, &mb32);

    if (mb32.u.out.ActionCode == MBO_START) {
	x54x_log("Start Mailbox Command\n");
	x54x_req_setup(dev, mb32.CCBPointer, &mb32);
    } else if (!dev->MailboxIsBIOS && (mb32.u.out.ActionCode == MBO_ABORT)) {
	x54x_log("Abort Mailbox Command\n");
	x54x_req_abort(dev, mb32.CCBPointer);
    } /* else {
	x54x_log("Invalid action code: %02X\n", mb32.u.out.ActionCode);
    } */

    if ((mb32.u.out.ActionCode == MBO_START) || (!dev->MailboxIsBIOS && (mb32.u.out.ActionCode == MBO_ABORT))) {
	/* We got the mailbox, decrease the number of pending requests. */
	if (dev->MailboxIsBIOS)
		dev->BIOSMailboxReq--;
	else
		dev->MailboxReq--;

	return(1);
    }

    return(0);
}


static void
x54x_do_mail(x54x_t *dev)
{
    int aggressive = 1;

    dev->MailboxIsBIOS = 0;

    if (dev->is_aggressive_mode) {
	aggressive = dev->is_aggressive_mode(dev);
	x54x_log("Processing mailboxes in %s mode...\n", aggressive ? "aggressive" : "strict");
    }/* else {
	x54x_log("Defaulting to process mailboxes in %s mode...\n", aggressive ? "aggressive" : "strict");
    }*/

    if (!dev->MailboxCount) {
	x54x_log("x54x_do_mail(): No Mailboxes\n");
	return;
    }

    if (aggressive) {
	/* Search for a filled mailbox - stop if we have scanned all mailboxes. */
	for (dev->MailboxOutPosCur = 0; dev->MailboxOutPosCur < dev->MailboxCount; dev->MailboxOutPosCur++) {
		if (x54x_mbo_process(dev))
			break;
	}
    } else {
	/* Strict round robin mode - only process the current mailbox and advance the pointer if successful. */
	if (x54x_mbo_process(dev)) {
		dev->MailboxOutPosCur++;
		dev->MailboxOutPosCur %= dev->MailboxCount;
	}
    }
}


static void
x54x_cmd_done(x54x_t *dev, int suppress);


static void
x54x_cmd_callback(void *priv)
{
    double period;
    x54x_t *dev = (x54x_t *) priv;

    int mailboxes_present, bios_mailboxes_present;

    mailboxes_present = (!(dev->Status & STAT_INIT) && dev->MailboxInit && dev->MailboxReq);
    bios_mailboxes_present = (dev->ven_callback && dev->BIOSMailboxInit && dev->BIOSMailboxReq);

    dev->temp_period = 0;
    dev->media_period = 0.0;

    switch (dev->callback_sub_phase) {
	case 0:
		/* Sub-phase 0 - Look for mailbox. */
		if ((dev->callback_phase == 0) && mailboxes_present)
			x54x_do_mail(dev);
		else if ((dev->callback_phase == 1) && bios_mailboxes_present)
			dev->ven_callback(dev);

		if (dev->ven_callback && (dev->callback_sub_phase == 0))
			dev->callback_phase ^= 1;
		break;
	case 1:
		/* Sub-phase 1 - Do SCSI command phase 0. */
		x54x_log("%s: Callback: Process SCSI request\n", dev->name);
		x54x_scsi_cmd(dev);
		break;
	case 2:
		/* Sub-phase 2 - Do SCSI command phase 1. */
		x54x_log("%s: Callback: Process SCSI request\n", dev->name);
		x54x_scsi_cmd_phase1(dev);
		break;
	case 3:
		/* Sub-phase 3 - Request sense. */
		x54x_log("%s: Callback: Process SCSI request\n", dev->name);
		x54x_request_sense(dev);
		break;
	case 4:
		/* Sub-phase 4 - Notify. */
		x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
		x54x_notify(dev);

		/* Go back to lookup phase. */
		dev->callback_sub_phase = 0;

		/* Toggle normal/BIOS mailbox - only has an effect if both types of mailboxes
		   have been initialized. */
		if (dev->ven_callback)
			dev->callback_phase ^= 1;

		/* Add to period and raise the IRQ if needed. */
		x54x_add_to_period(dev, 1);

		if (dev->ToRaise)
			raise_irq(dev, 0, dev->ToRaise);
		break;
	default:
		x54x_log("Invalid sub-phase: %02X\n", dev->callback_sub_phase);
		break;
    }

    period = (1000000.0 / dev->ha_bps) * ((double) dev->temp_period);
    timer_on(&dev->timer, dev->media_period + period + 10.0, 0);
    // x54x_log("Temporary period: %lf us (%" PRIi64 " periods)\n", dev->timer.period, dev->temp_period);
}


static uint8_t
x54x_in(uint16_t port, void *priv)
{
    x54x_t *dev = (x54x_t *)priv;
    uint8_t ret;

    switch (port & 3) {
	case 0:
	default:
		ret = dev->Status;
		break;

	case 1:
		ret = dev->DataBuf[dev->DataReply];
		if (dev->DataReplyLeft) {
			dev->DataReply++;
			dev->DataReplyLeft--;
			if (! dev->DataReplyLeft)
				x54x_cmd_done(dev, 0);
		}
		break;

	case 2:
		if (dev->flags & X54X_INT_GEOM_WRITABLE)
			ret = dev->Interrupt;
		else
			ret = dev->Interrupt & ~0x70;
		break;

	case 3:
		/* Bits according to ASPI4DOS.SYS v3.36:
			0		Not checked
			1		Must be 0
			2		Must be 0-0-0-1
			3		Must be 0
			4		Must be 0-1-0-0
			5		Must be 0
			6		Not checked
			7		Not checked
		*/
		if (dev->flags & X54X_INT_GEOM_WRITABLE)
			ret = dev->Geometry;
		else {
			switch(dev->Geometry) {
				case 0: default: ret = 'A'; break;
				case 1: ret = 'D'; break;
				case 2: ret = 'A'; break;
				case 3: ret = 'P'; break;
			}
			ret ^= 1;
			dev->Geometry++;
			dev->Geometry &= 0x03;
			break;
		}
		break;
    }

#ifdef ENABLE_X54X_LOG
    if (port == 0x0332)
	x54x_log("x54x_in(): %04X, %02X, %08X\n", port, ret, dev->DataReplyLeft);
    else
	x54x_log("x54x_in(): %04X, %02X\n", port, ret);
#endif

    return(ret);
}


static uint16_t
x54x_inw(uint16_t port, void *priv)
{
    return((uint16_t) x54x_in(port, priv));
}


static uint32_t
x54x_inl(uint16_t port, void *priv)
{
    return((uint32_t) x54x_in(port, priv));
}


static uint8_t
x54x_readb(uint32_t port, void *priv)
{
    return(x54x_in(port & 3, priv));
}


static uint16_t
x54x_readw(uint32_t port, void *priv)
{
    return(x54x_inw(port & 3, priv));
}


static uint32_t
x54x_readl(uint32_t port, void *priv)
{
    return(x54x_inl(port & 3, priv));
}


static void
x54x_reset_poll(void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    dev->Status = STAT_INIT | STAT_IDLE;
}


static void
x54x_reset(x54x_t *dev)
{
    int i;

    clear_irq(dev);
    if (dev->flags & X54X_INT_GEOM_WRITABLE)
	dev->Geometry = 0x80;
    else
	dev->Geometry = 0x00;
    dev->callback_phase = 0;
    dev->callback_sub_phase = 0;
    timer_stop(&dev->timer);
    timer_set_delay_u64(&dev->timer, (uint64_t) (dev->timer.period * ((double) TIMER_USEC)));
    dev->Command = 0xFF;
    dev->CmdParam = 0;
    dev->CmdParamLeft = 0;
    dev->flags |= X54X_MBX_24BIT;
    dev->MailboxInPosCur = 0;
    dev->MailboxOutInterrupts = 0;
    dev->PendingInterrupt = 0;
    dev->IrqEnabled = 1;
    dev->MailboxCount = 0;
    dev->MailboxOutPosCur = 0;

    /* Reset all devices on controller reset. */
    for (i = 0; i < 16; i++)
	scsi_device_reset(&scsi_devices[dev->bus][i]);

    if (dev->ven_reset)
	dev->ven_reset(dev);
}


void
x54x_reset_ctrl(x54x_t *dev, uint8_t Reset)
{
    /* Say hello! */
    x54x_log("%s %s (IO=0x%04X, IRQ=%d, DMA=%d, BIOS @%05lX) ID=%d\n",
	dev->vendor, dev->name, dev->Base, dev->Irq, dev->DmaChannel,
	dev->rom_addr, dev->HostID);

    x54x_reset(dev);

    if (Reset) {
	dev->Status = STAT_STST;
	timer_set_delay_u64(&dev->ResetCB, X54X_RESET_DURATION_US * TIMER_USEC);
    } else
	dev->Status = STAT_INIT | STAT_IDLE;
}


static void
x54x_out(uint16_t port, uint8_t val, void *priv)
{
    ReplyInquireSetupInformation *ReplyISI;
    x54x_t *dev = (x54x_t *)priv;
    MailboxInit_t *mbi;
    int i = 0;
    BIOSCMD *cmd;
    uint16_t cyl = 0;
    int suppress = 0;
    uint32_t FIFOBuf;
    uint8_t reset;
    addr24 Address;
    uint8_t host_id = dev->HostID;
    uint8_t irq = 0;

    x54x_log("%s: Write Port 0x%02X, Value %02X\n", dev->name, port, val);

    switch (port & 3) {
	case 0:
		if ((val & CTRL_HRST) || (val & CTRL_SRST)) {
			reset = (val & CTRL_HRST);
			x54x_log("Reset completed = %x\n", reset);
			x54x_reset_ctrl(dev, reset);
			x54x_log("Controller reset\n");
			break;
		}

		if (val & CTRL_SCRST) {
			/* Reset all devices on SCSI bus reset. */
			for (i = 0; i < 16; i++)
				scsi_device_reset(&scsi_devices[dev->bus][i]);
		}

		if (val & CTRL_IRST) {
			clear_irq(dev);
			x54x_log("Interrupt reset\n");
		}
		break;

	case 1:
		/* Fast path for the mailbox execution command. */
		if ((val == CMD_START_SCSI) && (dev->Command == 0xff)) {
			dev->MailboxReq++;
			x54x_log("Start SCSI command\n");
			return;
		}
		if (dev->ven_fast_cmds) {
			if (dev->Command == 0xff) {
				if (dev->ven_fast_cmds(dev, val))
					return;
			}
		}

		if (dev->Command == 0xff) {
			dev->Command = val;
			dev->CmdParam = 0;
			dev->CmdParamLeft = 0;

			dev->Status &= ~(STAT_INVCMD | STAT_IDLE);
			x54x_log("%s: Operation Code 0x%02X\n", dev->name, val);
			switch (dev->Command) {
				case CMD_MBINIT:
					dev->CmdParamLeft = sizeof(MailboxInit_t);
					break;

				case CMD_BIOSCMD:
					dev->CmdParamLeft = 10;
					break;

				case CMD_EMBOI:
				case CMD_BUSON_TIME:
				case CMD_BUSOFF_TIME:
				case CMD_DMASPEED:
				case CMD_RETSETUP:
				case CMD_ECHO:
				case CMD_OPTIONS:
					dev->CmdParamLeft = 1;
					break;	

				case CMD_SELTIMEOUT:
					dev->CmdParamLeft = 4;
					break;

				case CMD_WRITE_CH2:
				case CMD_READ_CH2:
					dev->CmdParamLeft = 3;
					break;

				default:
					if (dev->get_ven_param_len)
						dev->CmdParamLeft = dev->get_ven_param_len(dev);
					break;
			}
		} else {
			dev->CmdBuf[dev->CmdParam] = val;
			dev->CmdParam++;
			dev->CmdParamLeft--;

			if (dev->ven_cmd_phase1)
				dev->ven_cmd_phase1(dev);
		}
		
		if (! dev->CmdParamLeft) {
			x54x_log("Running Operation Code 0x%02X\n", dev->Command);
			switch (dev->Command) {
				case CMD_NOP: /* No Operation */
					dev->DataReplyLeft = 0;
					break;

				case CMD_MBINIT: /* mailbox initialization */
					dev->flags |= X54X_MBX_24BIT;

					mbi = (MailboxInit_t *)dev->CmdBuf;

					dev->MailboxInit = 1;
					dev->MailboxCount = mbi->Count;
					dev->MailboxOutAddr = ADDR_TO_U32(mbi->Address);
					dev->MailboxInAddr = dev->MailboxOutAddr + (dev->MailboxCount * sizeof(Mailbox_t));

					x54x_log("Initialize Mailbox: MBO=0x%08lx, MBI=0x%08lx, %d entries at 0x%08lx\n",
						dev->MailboxOutAddr,
						dev->MailboxInAddr,
						mbi->Count,
						ADDR_TO_U32(mbi->Address));

					dev->Status &= ~STAT_INIT;
					dev->DataReplyLeft = 0;
					x54x_log("Mailbox init: ");
					break;

				case CMD_BIOSCMD: /* execute BIOS */
					cmd = (BIOSCMD *)dev->CmdBuf;
					if (!(dev->flags & X54X_LBA_BIOS)) {
						/* 1640 uses LBA. */
						cyl = ((cmd->u.chs.cyl & 0xff) << 8) | ((cmd->u.chs.cyl >> 8) & 0xff);
						cmd->u.chs.cyl = cyl;
					}
					if (dev->flags & X54X_LBA_BIOS) {
						/* 1640 uses LBA. */
						x54x_log("BIOS LBA=%06lx (%lu)\n",
							lba32_blk(cmd),
							lba32_blk(cmd));
					} else {
						cmd->u.chs.head &= 0xf;
						cmd->u.chs.sec &= 0x1f;
						x54x_log("BIOS CHS=%04X/%02X%02X\n",
							cmd->u.chs.cyl,
							cmd->u.chs.head,
							cmd->u.chs.sec);
					}
					dev->DataBuf[0] = x54x_bios_command(dev, dev->max_id, cmd, !!(dev->flags & X54X_LBA_BIOS));
					x54x_log("BIOS Completion/Status Code %x\n", dev->DataBuf[0]);
					dev->DataReplyLeft = 1;
					break;

				case CMD_INQUIRY: /* Inquiry */
					memcpy(dev->DataBuf, dev->fw_rev, 4);
					x54x_log("Adapter inquiry: %c %c %c %c\n", dev->fw_rev[0], dev->fw_rev[1], dev->fw_rev[2], dev->fw_rev[3]);
					dev->DataReplyLeft = 4;
					break;

				case CMD_EMBOI: /* enable MBO Interrupt */
					if (dev->CmdBuf[0] <= 1) {
						dev->MailboxOutInterrupts = dev->CmdBuf[0];
						x54x_log("Mailbox out interrupts: %s\n", dev->MailboxOutInterrupts ? "ON" : "OFF");
						suppress = 1;
					} else {
						dev->Status |= STAT_INVCMD;
					}
					dev->DataReplyLeft = 0;
					break;

				case CMD_SELTIMEOUT: /* Selection Time-out */
					dev->DataReplyLeft = 0;
					break;

				case CMD_BUSON_TIME: /* bus-on time */
					dev->BusOnTime = dev->CmdBuf[0];
					dev->DataReplyLeft = 0;
					x54x_log("Bus-on time: %d\n", dev->CmdBuf[0]);
					break;

				case CMD_BUSOFF_TIME: /* bus-off time */
					dev->BusOffTime = dev->CmdBuf[0];
					dev->DataReplyLeft = 0;
					x54x_log("Bus-off time: %d\n", dev->CmdBuf[0]);
					break;

				case CMD_DMASPEED: /* DMA Transfer Rate */
					dev->ATBusSpeed = dev->CmdBuf[0];
					dev->DataReplyLeft = 0;
					x54x_log("DMA transfer rate: %02X\n", dev->CmdBuf[0]);
					break;

				case CMD_RETDEVS: /* return Installed Devices */
					memset(dev->DataBuf, 0x00, 8);

				        if (dev->ven_get_host_id)
						host_id = dev->ven_get_host_id(dev);

					for (i=0; i<8; i++) {
					    dev->DataBuf[i] = 0x00;

					    /* Skip the HA .. */
					    if (i == host_id) continue;

					    /* TODO: Query device for LUN's. */
					    if (scsi_device_present(&scsi_devices[dev->bus][i]))
						dev->DataBuf[i] |= 1;
					}
					dev->DataReplyLeft = i;
					break;

				case CMD_RETCONF: /* return Configuration */
					if (dev->ven_get_dma)
						dev->DataBuf[0] = (1 << dev->ven_get_dma(dev));
					else
						dev->DataBuf[0] = (1 << dev->DmaChannel);

					if (dev->ven_get_irq)
						irq = dev->ven_get_irq(dev);
					else
						irq = dev->Irq;

					if (irq >= 9)
					    dev->DataBuf[1]=(1<<(irq-9));
					else
					    dev->DataBuf[1]=0;
					if (dev->ven_get_host_id)
						dev->DataBuf[2] = dev->ven_get_host_id(dev);
					else
						dev->DataBuf[2] = dev->HostID;
					x54x_log("Configuration data: %02X %02X %02X\n", dev->DataBuf[0], dev->DataBuf[1], dev->DataBuf[2]);
					dev->DataReplyLeft = 3;
					break;

				case CMD_RETSETUP: /* return Setup */
					ReplyISI = (ReplyInquireSetupInformation *)dev->DataBuf;
					memset(ReplyISI, 0x00, sizeof(ReplyInquireSetupInformation));

					ReplyISI->uBusTransferRate = dev->ATBusSpeed;
					ReplyISI->uPreemptTimeOnBus = dev->BusOnTime;
					ReplyISI->uTimeOffBus = dev->BusOffTime;
					ReplyISI->cMailbox = dev->MailboxCount;
					U32_TO_ADDR(ReplyISI->MailboxAddress, dev->MailboxOutAddr);

					if (dev->get_ven_data)
						dev->get_ven_data(dev);

					dev->DataReplyLeft = dev->CmdBuf[0];
					x54x_log("Return Setup Information: %d (length: %i)\n", dev->CmdBuf[0], sizeof(ReplyInquireSetupInformation));
					break;

				case CMD_ECHO: /* ECHO data */
					dev->DataBuf[0] = dev->CmdBuf[0];
					dev->DataReplyLeft = 1;
					break;

				case CMD_WRITE_CH2:	/* write channel 2 buffer */
					dev->DataReplyLeft = 0;
					Address.hi = dev->CmdBuf[0];
					Address.mid = dev->CmdBuf[1];
					Address.lo = dev->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					x54x_log("Adaptec LocalRAM: Reading 64 bytes at %08X\n", FIFOBuf);
					dma_bm_read(FIFOBuf, dev->dma_buffer, 64, dev->transfer_size);
					break;

				case CMD_READ_CH2:	/* write channel 2 buffer */
					dev->DataReplyLeft = 0;
					Address.hi = dev->CmdBuf[0];
					Address.mid = dev->CmdBuf[1];
					Address.lo = dev->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					x54x_log("Adaptec LocalRAM: Writing 64 bytes at %08X\n", FIFOBuf);
					dma_bm_write(FIFOBuf, dev->dma_buffer, 64, dev->transfer_size);
					break;

				case CMD_OPTIONS: /* Set adapter options */
					if (dev->CmdParam == 1)
						dev->CmdParamLeft = dev->CmdBuf[0];
					dev->DataReplyLeft = 0;
					break;

				default:
					if (dev->ven_cmds)
						suppress = dev->ven_cmds(dev);
					else {
						dev->DataReplyLeft = 0;
						dev->Status |= STAT_INVCMD;
					}
					break;
			}
		}

		if (dev->DataReplyLeft)
			dev->Status |= STAT_DFULL;
		else if (!dev->CmdParamLeft)
			x54x_cmd_done(dev, suppress);
		break;

	case 2:
		if (dev->flags & X54X_INT_GEOM_WRITABLE)
			dev->Interrupt = val;
		break;

	case 3:
		if (dev->flags & X54X_INT_GEOM_WRITABLE)
			dev->Geometry = val;
		break;
    }
}


static void
x54x_outw(uint16_t port, uint16_t val, void *priv)
{
    x54x_out(port, val & 0xFF, priv);
}


static void
x54x_outl(uint16_t port, uint32_t val, void *priv)
{
    x54x_out(port, val & 0xFF, priv);
}


static void
x54x_writeb(uint32_t port, uint8_t val, void *priv)
{
    x54x_out(port & 3, val, priv);
}


static void
x54x_writew(uint32_t port, uint16_t val, void *priv)
{
    x54x_outw(port & 3, val, priv);
}


static void
x54x_writel(uint32_t port, uint32_t val, void *priv)
{
    x54x_outl(port & 3, val, priv);
}


static int
x54x_is_32bit(x54x_t *dev)
{
    int bit32 = 0;

    if (dev->card_bus & DEVICE_PCI)
	bit32 = 1;
    else if ((dev->card_bus & DEVICE_MCA) && (dev->flags & X54X_32BIT))
	bit32 = 1;

    return bit32;
}


void
x54x_io_set(x54x_t *dev, uint32_t base, uint8_t len)
{
    if (x54x_is_32bit(dev)) {
	x54x_log("x54x: [PCI] Setting I/O handler at %04X\n", base);
	io_sethandler(base, len,
		      x54x_in, x54x_inw, x54x_inl,
                      x54x_out, x54x_outw, x54x_outl, dev);
    } else {
	x54x_log("x54x: [ISA] Setting I/O handler at %04X\n", base);
	io_sethandler(base, len,
		      x54x_in, x54x_inw, NULL,
                      x54x_out, x54x_outw, NULL, dev);
    }
}


void
x54x_io_remove(x54x_t *dev, uint32_t base, uint8_t len)
{
    x54x_log("x54x: Removing I/O handler at %04X\n", base);

    if (x54x_is_32bit(dev)) {
	io_removehandler(base, len,
		      x54x_in, x54x_inw, x54x_inl,
                      x54x_out, x54x_outw, x54x_outl, dev);
    } else {
	io_removehandler(base, len,
		      x54x_in, x54x_inw, NULL,
                      x54x_out, x54x_outw, NULL, dev);
    }
}


void
x54x_mem_init(x54x_t *dev, uint32_t addr)
{
    if (x54x_is_32bit(dev)) {
	mem_mapping_add(&dev->mmio_mapping, addr, 0x20,
		        x54x_readb, x54x_readw, x54x_readl,
			x54x_writeb, x54x_writew, x54x_writel,
			NULL, MEM_MAPPING_EXTERNAL, dev);
    } else {
	mem_mapping_add(&dev->mmio_mapping, addr, 0x20,
		        x54x_readb, x54x_readw, NULL,
			x54x_writeb, x54x_writew, NULL,
			NULL, MEM_MAPPING_EXTERNAL, dev);
    }
}


void
x54x_mem_enable(x54x_t *dev)
{
    mem_mapping_enable(&dev->mmio_mapping);
}


void
x54x_mem_set_addr(x54x_t *dev, uint32_t base)
{
    mem_mapping_set_addr(&dev->mmio_mapping, base, 0x20);
}


void
x54x_mem_disable(x54x_t *dev)
{
    mem_mapping_disable(&dev->mmio_mapping);
}


/* General initialization routine for all boards. */
void *
x54x_init(const device_t *info)
{
    x54x_t *dev;

    /* Allocate control block and set up basic stuff. */
    dev = malloc(sizeof(x54x_t));
    if (dev == NULL) return(dev);
    memset(dev, 0x00, sizeof(x54x_t));
    dev->type = info->local;

    dev->card_bus = info->flags;
    dev->callback_phase = 0;
	
    timer_add(&dev->ResetCB, x54x_reset_poll, dev, 0);
    timer_add(&dev->timer, x54x_cmd_callback, dev, 1);
    dev->timer.period = 10.0;
    timer_set_delay_u64(&dev->timer, (uint64_t) (dev->timer.period * ((double) TIMER_USEC)));

     if (x54x_is_32bit(dev))
	dev->transfer_size = 4;
     else
	dev->transfer_size = 2;

    return(dev);
}


void
x54x_close(void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    if (dev) {
	/* Tell the timer to terminate. */
	timer_stop(&dev->timer);

	/* Also terminate the reset callback timer. */
	timer_disable(&dev->ResetCB);

	dev->MailboxInit = dev->BIOSMailboxInit = 0;
	dev->MailboxCount = dev->BIOSMailboxCount = 0;
	dev->MailboxReq = dev->BIOSMailboxReq = 0;

	if (dev->ven_data)
		free(dev->ven_data);

	if (dev->nvr != NULL)
		free(dev->nvr);

	free(dev);
	dev = NULL;
    }
}


void
x54x_device_reset(void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    x54x_reset_ctrl(dev, 1);

    timer_disable(&dev->ResetCB);
    dev->Status = STAT_IDLE | STAT_INIT;
}
