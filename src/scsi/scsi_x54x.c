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
 * Version:	@(#)scsi_x54x.c	1.0.11	2018/01/06
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2018 Miran Grca.
 *		Copyright 2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../io.h"
#include "../dma.h"
#include "../pic.h"
#include "../pci.h"
#include "../mca.h"
#include "../mem.h"
#include "../rom.h"
#include "../nvr.h"
#include "../device.h"
#include "../timer.h"
#include "../plat.h"
#include "scsi.h"
#include "scsi_device.h"
#include "scsi_aha154x.h"
#include "scsi_x54x.h"


#define X54X_RESET_DURATION_US	UINT64_C(50000)


static void	x54x_cmd_thread(void *priv);

static volatile
thread_t	*poll_tid;
static volatile
int	busy;

static volatile
event_t	*evt;
static volatile
event_t	*wait_evt;

static volatile
event_t	*wake_poll_thread;
static volatile
event_t	*thread_started;

static volatile
x54x_t	*x54x_dev;


#ifdef ENABLE_X54X_LOG
int x54x_do_log = ENABLE_X54X_LOG;
#endif


static void
x54x_log(const char *fmt, ...)
{
#ifdef ENABLE_X54X_LOG
    va_list ap;

    if (x54x_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
#endif
}


static void
x54x_irq(x54x_t *dev, int set)
{
    int int_type = 0;
    int irq;

    if (dev->ven_get_irq)
	irq = dev->ven_get_irq(dev);
      else
	irq = dev->Irq;

    if (dev->bus & DEVICE_PCI) {
	x54x_log("PCI IRQ: %02X, PCI_INTA\n", dev->pci_slot);
        if (set) {
       	        pci_set_irq(dev->pci_slot, PCI_INTA);
	} else {
       	        pci_clear_irq(dev->pci_slot, PCI_INTA);
	}
    } else {
	if (set) {
		if (dev->interrupt_type)
			int_type = dev->interrupt_type(dev);

		if (int_type) {
			picintlevel(1 << irq);
		} else {
			picint(1 << irq);
		}
	} else {
		picintc(1 << irq);
	}
    }
}


static void
raise_irq(x54x_t *dev, int suppress, uint8_t Interrupt)
{
    if (Interrupt & (INTR_MBIF | INTR_MBOA)) {
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


static uint8_t
x54x_bios_command_08(uint8_t id, uint8_t lun, uint8_t *buffer)
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
    x54x_log("BIOS Command 0x08: %02X %02X %02X %02X %02X %02X\n",
	buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

    return(0);
}


static int
x54x_bios_command_15(uint8_t id, uint8_t lun, uint8_t *buffer)
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

    x54x_log("BIOS Command 0x15: %02X %02X %02X %02X %02X %02X\n",
	buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);

    return(sc);
}


/* This returns the completion code. */
static uint8_t
x54x_bios_command(x54x_t *x54x, uint8_t max_id, BIOSCMD *cmd, int8_t islba)
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

    x54x_log("BIOS Command = 0x%02X\n", cmd->command);

    if ((cmd->id > max_id) || (cmd->lun > 7)) {
	x54x_log("BIOS Target ID %i or LUN %i are above maximum\n",
						cmd->id, cmd->lun);
	return(0x80);
    }

    /* Get pointer to selected device. */
    dev = &SCSIDevices[cmd->id][cmd->lun];
    dev->BufferLength = 0;

    if (! scsi_device_present(cmd->id, cmd->lun)) {
	x54x_log("BIOS Target ID %i and LUN %i have no device attached\n",
							cmd->id, cmd->lun);
	return(0x80);
    }

    if ((dev->LunType == SCSI_CDROM) && !x54x->cdrom_boot) {
	x54x_log("BIOS Target ID %i and LUN %i is CD-ROM on unsupported BIOS\n",
							cmd->id, cmd->lun);
	return(0x80);
    }

    dma_address = ADDR_TO_U32(cmd->dma_address);

    x54x_log("BIOS Data Buffer write: length %d, pointer 0x%04X\n",
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
		dev->BufferLength = 14;
		dev->CmdBuffer = (uint8_t *)malloc(14);
		memset(dev->CmdBuffer, 0x00, 14);

		if (sector_len > 0) {
			x54x_log("BIOS DMA: Reading 14 bytes at %08X\n",
							dma_address);
			DMAPageWrite(dma_address,
			    scsi_device_sense(cmd->id, cmd->lun), 14);
		}

		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(0);

	case 0x02:	/* Read Desired Sectors to Memory */
		target_check(cmd->id, cmd->lun);

		dev->BufferLength = sector_len << block_shift;
		dev->CmdBuffer = (uint8_t *)malloc(dev->BufferLength);
		memset(dev->CmdBuffer, 0x00, dev->BufferLength);

		cdb[0] = GPCMD_READ_10;
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;
		cdb[7] = (sector_len >> 8) & 0xff;
		cdb[8] = sector_len & 0xff;
#if 0
		x54x_log("BIOS CMD(READ, %08lx, %d)\n", lba, cmd->secount);
#endif

		scsi_device_command_phase0(cmd->id, cmd->lun, 12, cdb);

		if (dev->Phase == SCSI_PHASE_STATUS)
			goto skip_read_phase1;

		scsi_device_command_phase1(cmd->id, cmd->lun);
		if (sector_len > 0) {
			x54x_log("BIOS DMA: Reading %i bytes at %08X\n",
					dev->BufferLength, dma_address);
			DMAPageWrite(dma_address,
				     dev->CmdBuffer, dev->BufferLength);
		}

skip_read_phase1:
		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x03:	/* Write Desired Sectors from Memory */
		target_check(cmd->id, cmd->lun);

		dev->BufferLength = sector_len << block_shift;
		dev->CmdBuffer = (uint8_t *)malloc(dev->BufferLength);
		memset(dev->CmdBuffer, 0x00, dev->BufferLength);

		cdb[0] = GPCMD_WRITE_10;
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;
		cdb[7] = (sector_len >> 8) & 0xff;
		cdb[8] = sector_len & 0xff;
#if 0
		x54x_log("BIOS CMD(WRITE, %08lx, %d)\n", lba, cmd->secount);
#endif

		scsi_device_command_phase0(cmd->id, cmd->lun, 12, cdb);

		if (dev->Phase == SCSI_PHASE_STATUS)
			goto skip_write_phase1;

		if (sector_len > 0) {
			x54x_log("BIOS DMA: Reading %i bytes at %08X\n",
					dev->BufferLength, dma_address);
			DMAPageRead(dma_address,
				    dev->CmdBuffer, dev->BufferLength);
		}

		scsi_device_command_phase1(cmd->id, cmd->lun);

skip_write_phase1:
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

		scsi_device_command_phase0(cmd->id, cmd->lun, 12, cdb);

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

		scsi_device_command_phase0(cmd->id, cmd->lun, 12, cdb);

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x08:	/* Read Drive Parameters */
		target_check(cmd->id, cmd->lun);

		dev->BufferLength = 6;
		dev->CmdBuffer = (uint8_t *)malloc(dev->BufferLength);
		memset(dev->CmdBuffer, 0x00, dev->BufferLength);

		ret = x54x_bios_command_08(cmd->id, cmd->lun, dev->CmdBuffer);

		x54x_log("BIOS DMA: Reading 6 bytes at %08X\n", dma_address);
		DMAPageWrite(dma_address,
			     dev->CmdBuffer, dev->BufferLength);

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

		cdb[0] = GPCMD_SEEK_10;
		cdb[1] = (cmd->lun & 7) << 5;
		cdb[2] = (lba >> 24) & 0xff;
		cdb[3] = (lba >> 16) & 0xff;
		cdb[4] = (lba >> 8) & 0xff;
		cdb[5] = lba & 0xff;

		scsi_device_command_phase0(cmd->id, cmd->lun, 12, cdb);

		return((dev->Status == SCSI_STATUS_OK) ? 1 : 0);

	case 0x0d:	/* Alternate Disk Reset, in practice it's a nop */
//FIXME: add a longer delay here --FvK
		return(0);

	case 0x10:	/* Test Drive Ready */
		target_check(cmd->id, cmd->lun);

		cdb[0] = GPCMD_TEST_UNIT_READY;
		cdb[1] = (cmd->lun & 7) << 5;

		scsi_device_command_phase0(cmd->id, cmd->lun, 12, cdb);

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x11:	/* Recalibrate */
		target_check(cmd->id, cmd->lun);

		cdb[0] = GPCMD_REZERO_UNIT;
		cdb[1] = (cmd->lun & 7) << 5;

		scsi_device_command_phase0(cmd->id, cmd->lun, 12, cdb);

		return(completion_code(scsi_device_sense(cmd->id, cmd->lun)));

	case 0x14:	/* Controller Diagnostic */
//FIXME: add a longer delay here --FvK
		return(0);

	case 0x15:	/* Read DASD Type */
		target_check(cmd->id, cmd->lun);

		dev->BufferLength = 6;
		dev->CmdBuffer = (uint8_t *)malloc(dev->BufferLength);
		memset(dev->CmdBuffer, 0x00, dev->BufferLength);

		ret = x54x_bios_command_15(cmd->id, cmd->lun, dev->CmdBuffer);

		x54x_log("BIOS DMA: Reading 6 bytes at %08X\n", dma_address);
		DMAPageWrite(dma_address,
			     dev->CmdBuffer, dev->BufferLength);

		if (dev->CmdBuffer != NULL) {
			free(dev->CmdBuffer);
			dev->CmdBuffer = NULL;
		}

		return(ret);

	default:
		x54x_log("BIOS: Unimplemented command: %02X\n", cmd->command);
		return(1);
    }
	
    x54x_log("BIOS Request complete\n");
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
x54x_mbi_setup(x54x_t *dev, uint32_t CCBPointer, CCBU *CmdBlock,
	       uint8_t HostStatus, uint8_t TargetStatus, uint8_t mbcc)
{
    Req_t *req = &dev->Req;

    req->CCBPointer = CCBPointer;
    memcpy(&(req->CmdBlock), CmdBlock, sizeof(CCB32));
    req->Is24bit = dev->Mbx24bit;
    req->HostStatus = HostStatus;
    req->TargetStatus = TargetStatus;
    req->MailboxCompletionCode = mbcc;

    x54x_log("Mailbox in setup\n");
}


static void
x54x_ccb(x54x_t *dev)
{
    Req_t *req = &dev->Req;

    /* Rewrite the CCB up to the CDB. */
    x54x_log("CCB completion code and statuses rewritten (pointer %08X)\n", req->CCBPointer);
    DMAPageWrite(req->CCBPointer + 0x000D, &(req->MailboxCompletionCode), 1);
    DMAPageWrite(req->CCBPointer + 0x000E, &(req->HostStatus), 1);
    DMAPageWrite(req->CCBPointer + 0x000F, &(req->TargetStatus), 1);

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

    Incoming = dev->MailboxInAddr + (dev->MailboxInPosCur * (dev->Mbx24bit ? sizeof(Mailbox_t) : sizeof(Mailbox32_t)));

    if (MailboxCompletionCode != MBI_NOT_FOUND) {
	CmdBlock->common.HostStatus = HostStatus;
	CmdBlock->common.TargetStatus = TargetStatus;

	/* Rewrite the CCB up to the CDB. */
	x54x_log("CCB statuses rewritten (pointer %08X)\n", req->CCBPointer);
    	DMAPageWrite(req->CCBPointer + 0x000E, &(req->HostStatus), 1);
	DMAPageWrite(req->CCBPointer + 0x000F, &(req->TargetStatus), 1);
    } else {
	x54x_log("Mailbox not found!\n");
    }

    x54x_log("Host Status 0x%02X, Target Status 0x%02X\n",HostStatus,TargetStatus);

    if (dev->Mbx24bit) {
	U32_TO_ADDR(CCBPointer, req->CCBPointer);
	x54x_log("Mailbox 24-bit: Status=0x%02X, CCB at 0x%04X\n", req->MailboxCompletionCode, CCBPointer);
	DMAPageWrite(Incoming, &(req->MailboxCompletionCode), 1);
	DMAPageWrite(Incoming + 1, (uint8_t *)&CCBPointer, 3);
	x54x_log("%i bytes of 24-bit mailbox written to: %08X\n", sizeof(Mailbox_t), Incoming);
    } else {
	x54x_log("Mailbox 32-bit: Status=0x%02X, CCB at 0x%04X\n", req->MailboxCompletionCode, CCBPointer);
	DMAPageWrite(Incoming, (uint8_t *)&(req->CCBPointer), 4);
	DMAPageWrite(Incoming + 4, &(req->HostStatus), 1);
	DMAPageWrite(Incoming + 5, &(req->TargetStatus), 1);
	DMAPageWrite(Incoming + 7, &(req->MailboxCompletionCode), 1);
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
x54x_rd_sge(int Is24bit, uint32_t Address, SGE32 *SG)
{
    SGE SGE24;

    if (Is24bit) {
	DMAPageRead(Address, (uint8_t *)&SGE24, sizeof(SGE));

	/* Convert the 24-bit entries into 32-bit entries. */
	x54x_log("Read S/G block: %06X, %06X\n", SGE24.Segment, SGE24.SegmentPointer);
	SG->Segment = ADDR_TO_U32(SGE24.Segment);
	SG->SegmentPointer = ADDR_TO_U32(SGE24.SegmentPointer);
    } else {
	DMAPageRead(Address, (uint8_t *)SG, sizeof(SGE32));		
    }
}


static int
x54x_get_length(Req_t *req, int Is24bit)
{
    uint32_t DataPointer, DataLength;
    uint32_t SGEntryLength = (Is24bit ? sizeof(SGE) : sizeof(SGE32));
    SGE32 SGBuffer;
    uint32_t DataToTransfer = 0;
    int i = 0;

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
			x54x_rd_sge(Is24bit, DataPointer + i, &SGBuffer);

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
x54x_set_residue(Req_t *req, int32_t TransferLength)
{
    uint32_t Residue = 0;
    addr24 Residue24;
    int32_t BufLen = SCSIDevices[req->TargetID][req->LUN].BufferLength;

    if ((req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) ||
	(req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {

	if ((TransferLength > 0) && (req->CmdBlock.common.ControlByte < 0x03)) {
		TransferLength -= BufLen;
		if (TransferLength > 0)
			Residue = TransferLength;
	}

	if (req->Is24bit) {
		U32_TO_ADDR(Residue24, Residue);
    		DMAPageWrite(req->CCBPointer + 0x0004, (uint8_t *)&Residue24, 3);
		x54x_log("24-bit Residual data length for reading: %d\n", Residue);
	} else {
    		DMAPageWrite(req->CCBPointer + 0x0004, (uint8_t *)&Residue, 4);
		x54x_log("32-bit Residual data length for reading: %d\n", Residue);
	}
    }
}


static void
x54x_buf_dma_transfer(Req_t *req, int Is24bit, int TransferLength, int dir)
{
    uint32_t DataPointer, DataLength;
    uint32_t SGEntryLength = (Is24bit ? sizeof(SGE) : sizeof(SGE32));
    uint32_t Address;
    int i = 0;
    int32_t BufLen = SCSIDevices[req->TargetID][req->LUN].BufferLength;
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
    x54x_log("Data Buffer %s: length %d, pointer 0x%04X\n",
	     dir ? "write" : "read", BufLen, DataPointer);

    if ((req->CmdBlock.common.ControlByte != 0x03) && TransferLength && BufLen) {
	if ((req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND) ||
	    (req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {

		/* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
		   checking its length, so do this procedure for both no read/write commands. */
		if ((DataLength > 0) && (req->CmdBlock.common.ControlByte < 0x03)) {
			for (i = 0; i < DataLength; i += SGEntryLength) {
				x54x_rd_sge(Is24bit, DataPointer + i, &SGBuffer);

				Address = SGBuffer.SegmentPointer;
				DataToTransfer = MIN(SGBuffer.Segment, BufLen);

				if (read_from_host && DataToTransfer) {
					x54x_log("Reading S/G segment %i: length %i, pointer %08X\n", i, DataToTransfer, Address);
					DMAPageRead(Address, &(SCSIDevices[req->TargetID][req->LUN].CmdBuffer[sg_pos]), DataToTransfer);
				}
				else if (write_to_host && DataToTransfer) {
					x54x_log("Writing S/G segment %i: length %i, pointer %08X\n", i, DataToTransfer, Address);
					DMAPageWrite(Address, &(SCSIDevices[req->TargetID][req->LUN].CmdBuffer[sg_pos]), DataToTransfer);
				}
				else {
					x54x_log("No action on S/G segment %i: length %i, pointer %08X\n", i, DataToTransfer, Address);
				}

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
			if (read_from_host) {
				DMAPageRead(Address, SCSIDevices[req->TargetID][req->LUN].CmdBuffer, MIN(BufLen, DataLength));
			} else if (write_to_host) {
				DMAPageWrite(Address, SCSIDevices[req->TargetID][req->LUN].CmdBuffer, MIN(BufLen, DataLength));
			}
		}
	}
    }
}


void
x54x_buf_alloc(uint8_t id, uint8_t lun, int length)
{
    if (SCSIDevices[id][lun].CmdBuffer != NULL) {
	free(SCSIDevices[id][lun].CmdBuffer);
	SCSIDevices[id][lun].CmdBuffer = NULL;
    }

    x54x_log("Allocating data buffer (%i bytes)\n", length);
    SCSIDevices[id][lun].CmdBuffer = (uint8_t *) malloc(length);
    memset(SCSIDevices[id][lun].CmdBuffer, 0, length);
}


void
x54x_buf_free(uint8_t id, uint8_t lun)
{
    if (SCSIDevices[id][lun].CmdBuffer != NULL) {
	free(SCSIDevices[id][lun].CmdBuffer);
	SCSIDevices[id][lun].CmdBuffer = NULL;
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
SenseBufferFree(Req_t *req, int Copy)
{
    uint8_t SenseLength = ConvertSenseLength(req->CmdBlock.common.RequestSenseLength);
    uint32_t SenseBufferAddress;
    uint8_t temp_sense[256];

    if (SenseLength && Copy) {
        scsi_device_request_sense(req->TargetID, req->LUN, temp_sense, SenseLength);

	/*
	 * The sense address, in 32-bit mode, is located in the
	 * Sense Pointer of the CCB, but in 24-bit mode, it is
	 * located at the end of the Command Descriptor Block.
	 */
	SenseBufferAddress = SenseBufferPointer(req);

	x54x_log("Request Sense address: %02X\n", SenseBufferAddress);

	x54x_log("SenseBufferFree(): Writing %i bytes at %08X\n",
					SenseLength, SenseBufferAddress);
	DMAPageWrite(SenseBufferAddress, temp_sense, SenseLength);
	x54x_log("Sense data written to buffer: %02X %02X %02X\n",
		temp_sense[2], temp_sense[12], temp_sense[13]);
    }
}


static void
x54x_scsi_cmd(x54x_t *dev)
{
    Req_t *req = &dev->Req;
    uint8_t id, lun;
    uint8_t temp_cdb[12];
    uint32_t i;
    int target_cdb_len = 12;
    int target_data_len;
    uint8_t bit24 = !!req->Is24bit;
    int32_t *BufLen;
    uint8_t phase;
    uint32_t SenseBufferAddress;

    id = req->TargetID;
    lun = req->LUN;

    target_cdb_len = scsi_device_cdb_length(id, lun);
    target_data_len = x54x_get_length(req, bit24);

    if (!scsi_device_valid(id, lun))
	fatal("SCSI target on %02i:%02i has disappeared\n", id, lun);

    x54x_log("target_data_len = %i\n", target_data_len);

    x54x_log("SCSI command being executed on ID %i, LUN %i\n", id, lun);

    x54x_log("SCSI CDB[0]=0x%02X\n", req->CmdBlock.common.Cdb[0]);
    for (i=1; i<req->CmdBlock.common.CdbLength; i++)
	x54x_log("SCSI CDB[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);

    memset(temp_cdb, 0x00, target_cdb_len);
    if (req->CmdBlock.common.CdbLength <= target_cdb_len) {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb,
	       req->CmdBlock.common.CdbLength);
    } else {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb, target_cdb_len);
    }

    dev->Residue = 0;

    BufLen = scsi_device_get_buf_len(id, lun);
    *BufLen = target_data_len;

    x54x_log("Command buffer: %08X\n", SCSIDevices[id][lun].CmdBuffer);

    scsi_device_command_phase0(id, lun, req->CmdBlock.common.CdbLength, temp_cdb);

    phase = SCSIDevices[id][lun].Phase;

    x54x_log("Control byte: %02X\n", (req->CmdBlock.common.ControlByte == 0x03));

    if (phase != SCSI_PHASE_STATUS) {
	if ((temp_cdb[0] == 0x03) && (req->CmdBlock.common.ControlByte == 0x03)) {
		/* Request sense in non-data mode - sense goes to sense buffer. */
		*BufLen = ConvertSenseLength(req->CmdBlock.common.RequestSenseLength);
	    	x54x_buf_alloc(id, lun, *BufLen);
		scsi_device_command_phase1(id, lun);
		if ((SCSIDevices[id][lun].Status != SCSI_STATUS_OK) && (*BufLen > 0)) {
			SenseBufferAddress = SenseBufferPointer(req);
			DMAPageWrite(SenseBufferAddress, SCSIDevices[id][lun].CmdBuffer, *BufLen);
		}
	} else {
	    	x54x_buf_alloc(id, lun, MIN(target_data_len, *BufLen));
		if (phase == SCSI_PHASE_DATA_OUT)
			x54x_buf_dma_transfer(req, bit24, target_data_len, 1);
		scsi_device_command_phase1(id, lun);
		if (phase == SCSI_PHASE_DATA_IN)
			x54x_buf_dma_transfer(req, bit24, target_data_len, 0);

		SenseBufferFree(req, (SCSIDevices[id][lun].Status != SCSI_STATUS_OK));
	}
    } else
	SenseBufferFree(req, (SCSIDevices[id][lun].Status != SCSI_STATUS_OK));

    x54x_set_residue(req, target_data_len);

    x54x_buf_free(id, lun);

    x54x_log("Request complete\n");

    if (SCSIDevices[id][lun].Status == SCSI_STATUS_OK) {
	x54x_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
		       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
    } else if (SCSIDevices[id][lun].Status == SCSI_STATUS_CHECK_CONDITION) {
	x54x_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
		       CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION, MBI_ERROR);
    }

    x54x_log("SCSIDevices[%02i][%02i].Status = %02X\n", id, lun, SCSIDevices[id][lun].Status);

    if (temp_cdb[0] == 0x42) {
	thread_wait_event((event_t *) evt, 10);
    }
}


static void
x54x_notify(x54x_t *dev)
{
    if (dev->MailboxIsBIOS)
	x54x_ccb(dev);
      else
	x54x_mbi(dev);
}


static void
x54x_req_setup(x54x_t *dev, uint32_t CCBPointer, Mailbox32_t *Mailbox32)
{	
    Req_t *req = &dev->Req;
    uint8_t id, lun;
    uint8_t max_id = SCSI_ID_MAX-1;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (uint8_t *)&req->CmdBlock, sizeof(CCB32));

    req->Is24bit = dev->Mbx24bit;
    req->CCBPointer = CCBPointer;
    req->TargetID = dev->Mbx24bit ? req->CmdBlock.old.Id : req->CmdBlock.new.Id;
    req->LUN = dev->Mbx24bit ? req->CmdBlock.old.Lun : req->CmdBlock.new.Lun;

    id = req->TargetID;
    lun = req->LUN;
    if ((id > max_id) || (lun > 7)) {
	x54x_log("SCSI Target ID %i or LUN %i is not valid\n",id,lun);
	x54x_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		      CCB_SELECTION_TIMEOUT, SCSI_STATUS_OK, MBI_ERROR);
	x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
	x54x_notify(dev);
	return;
    }

    x54x_log("Scanning SCSI Target ID %i\n", id);

    SCSIDevices[id][lun].Status = SCSI_STATUS_OK;

    /* If there is no device at ID:0, timeout the selection - the LUN is then checked later. */
    if (! scsi_device_present(id, 0)) {
	x54x_log("SCSI Target ID %i and LUN %i have no device attached\n",id,lun);
	x54x_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		       CCB_SELECTION_TIMEOUT, SCSI_STATUS_OK, MBI_ERROR);
	x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
	x54x_notify(dev);
    } else {
	x54x_log("SCSI Target ID %i detected and working\n", id);

	x54x_log("Transfer Control %02X\n", req->CmdBlock.common.ControlByte);
	x54x_log("CDB Length %i\n", req->CmdBlock.common.CdbLength);	
	x54x_log("CCB Opcode %x\n", req->CmdBlock.common.Opcode);		
	if ((req->CmdBlock.common.Opcode > 0x04) && (req->CmdBlock.common.Opcode != 0x81)) {
		x54x_log("Invalid opcode: %02X\n",
			req->CmdBlock.common.ControlByte);
		x54x_mbi_setup(dev, CCBPointer, &req->CmdBlock, CCB_INVALID_OP_CODE, SCSI_STATUS_OK, MBI_ERROR);
		x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
		x54x_notify(dev);
		return;
	}
	if (req->CmdBlock.common.Opcode == 0x81) {
		x54x_log("Bus reset opcode\n");
		x54x_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
		x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
		x54x_notify(dev);
		return;
	}
	if (req->CmdBlock.common.ControlByte > 0x03) {
		x54x_log("Invalid control byte: %02X\n",
			req->CmdBlock.common.ControlByte);
		x54x_mbi_setup(dev, CCBPointer, &req->CmdBlock, CCB_INVALID_DIRECTION, SCSI_STATUS_OK, MBI_ERROR);
		x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
		x54x_notify(dev);
		return;
	}

	x54x_log("%s: Callback: Process SCSI request\n", dev->name);
	x54x_scsi_cmd(dev);

	x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
	x54x_notify(dev);
    }
}


static void
x54x_req_abort(x54x_t *dev, uint32_t CCBPointer)
{
    CCBU CmdBlock;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (uint8_t *)&CmdBlock, sizeof(CCB32));

    x54x_mbi_setup(dev, CCBPointer, &CmdBlock,
		  0x26, SCSI_STATUS_OK, MBI_NOT_FOUND);
    x54x_log("%s: Callback: Send incoming mailbox\n", dev->name);
    x54x_notify(dev);
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

    if (dev->Mbx24bit) {
	Outgoing = Addr + (Cur * sizeof(Mailbox_t));
	DMAPageRead(Outgoing, (uint8_t *)&MailboxOut, sizeof(Mailbox_t));

	ccbp = *(uint32_t *) &MailboxOut;
	Mailbox32->CCBPointer = (ccbp >> 24) | ((ccbp >> 8) & 0xff00) | ((ccbp << 8) & 0xff0000);
	Mailbox32->u.out.ActionCode = MailboxOut.CmdStatus;
    } else {
	Outgoing = Addr + (Cur * sizeof(Mailbox32_t));

	DMAPageRead(Outgoing, (uint8_t *)Mailbox32, sizeof(Mailbox32_t));
    }

    return(Outgoing);
}


uint8_t
x54x_mbo_process(x54x_t *dev)
{
    Mailbox32_t mb32;
    uint32_t Outgoing;
    uint8_t CmdStatus = MBO_FREE;
    uint32_t CodeOffset = 0;

    CodeOffset = dev->Mbx24bit ? 0 : 7;

    Outgoing = x54x_mbo(dev, &mb32);

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
	/* We got the mailbox, mark it as free in the guest. */
	x54x_log("x54x_do_mail(): Writing %i bytes at %08X\n", sizeof(CmdStatus), Outgoing + CodeOffset);
	DMAPageWrite(Outgoing + CodeOffset, &CmdStatus, 1);

	if (dev->ToRaise) {
		raise_irq(dev, 0, dev->ToRaise);

		while (dev->Interrupt)
			;
	}

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
x54x_do_mail_again:
	if (x54x_mbo_process(dev)) {
		dev->MailboxOutPosCur++;
		dev->MailboxOutPosCur %= dev->MailboxCount;
		goto x54x_do_mail_again;
	}
    }
}


static void
x54x_cmd_done(x54x_t *dev, int suppress);


void
x54x_wait_for_poll(void)
{
    if (x54x_is_busy()) {
	thread_wait_event((event_t *) wake_poll_thread, -1);
    }
    thread_reset_event((event_t *) wake_poll_thread);
}


static void
x54x_cmd_thread(void *priv)
{
    x54x_t *dev = (x54x_t *) x54x_dev;

    thread_set_event((event_t *) thread_started);

    x54x_log("Polling thread started\n");

    while (x54x_dev) {
	scsi_mutex_wait(1);

	if ((dev->Status & STAT_INIT) || (!dev->MailboxInit && !dev->BIOSMailboxInit) || (!dev->MailboxReq && !dev->BIOSMailboxReq)) {
		/* If we did not get anything, wait a while. */
		thread_wait_event((event_t *) wait_evt, 10);

		scsi_mutex_wait(0);
		continue;
	}

	if (!(x54x_dev->Status & STAT_INIT) && x54x_dev->MailboxInit && dev->MailboxReq) {
		x54x_wait_for_poll();

		x54x_do_mail(dev);
	}

	if (dev->ven_thread) {
		dev->ven_thread(dev);
	}

	scsi_mutex_wait(0);
    }

    x54x_log("%s: Callback: polling stopped.\n", dev->name);
}


void
x54x_busy(uint8_t set)
{
    busy = !!set;
    if (!set)
	    thread_set_event((event_t *) wake_poll_thread);
}


void
x54x_thread_start(x54x_t *dev)
{
    if (!poll_tid) {
	x54x_log("Starting thread...\n");
	poll_tid = thread_create(x54x_cmd_thread, dev);
    }
}


uint8_t
x54x_is_busy(void)
{
    return(!!busy);
}


void
x54x_set_wait_event(void)
{
    thread_set_event((event_t *)wait_evt);
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
		ret = dev->Interrupt;
		break;

	case 3:
		ret = dev->Geometry;
		break;
    }

#if 0
    x54x_log("%s: Read Port 0x%02X, Value %02X\n", dev->name, port, ret);
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
x54x_read(uint32_t port, void *priv)
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

    dev->ResetCB = 0LL;
}


static void
x54x_reset(x54x_t *dev)
{
    clear_irq(dev);
    dev->Geometry = 0x80;
    dev->Command = 0xFF;
    dev->CmdParam = 0;
    dev->CmdParamLeft = 0;
    dev->Mbx24bit = 1;
    dev->MailboxInPosCur = 0;
    dev->MailboxOutInterrupts = 0;
    dev->PendingInterrupt = 0;
    dev->IrqEnabled = 1;
    dev->MailboxCount = 0;
    dev->MailboxOutPosCur = 0;

    if (dev->ven_reset) {
	dev->ven_reset(dev);
    }
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
	dev->ResetCB = X54X_RESET_DURATION_US * TIMER_USEC;
    } else {
	dev->Status = STAT_INIT | STAT_IDLE;
    }
}


static void
x54x_out(uint16_t port, uint8_t val, void *priv)
{
    ReplyInquireSetupInformation *ReplyISI;
    x54x_t *dev = (x54x_t *)priv;
    MailboxInit_t *mbi;
    int i = 0;
    uint8_t j = 0;
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
			x54x_busy(1);
			reset = (val & CTRL_HRST);
			x54x_log("Reset completed = %x\n", reset);
			x54x_reset_ctrl(dev, reset);
			x54x_log("Controller reset: ");
			x54x_busy(0);
			break;
		}

		if (val & CTRL_IRST) {
			x54x_busy(1);
			clear_irq(dev);
			x54x_log("Interrupt reset: ");
			x54x_busy(0);
		}
		break;

	case 1:
		/* Fast path for the mailbox execution command. */
		if ((val == CMD_START_SCSI) && (dev->Command == 0xff)) {
			x54x_busy(1);
			dev->MailboxReq++;
			x54x_set_wait_event();
			x54x_log("Start SCSI command: ");
			x54x_busy(0);
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
					x54x_busy(1);
					dev->Mbx24bit = 1;

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
					x54x_busy(0);
					break;

				case CMD_BIOSCMD: /* execute BIOS */
					cmd = (BIOSCMD *)dev->CmdBuf;
					if (!dev->lba_bios) {
						/* 1640 uses LBA. */
						cyl = ((cmd->u.chs.cyl & 0xff) << 8) | ((cmd->u.chs.cyl >> 8) & 0xff);
						cmd->u.chs.cyl = cyl;
					}
					if (dev->lba_bios) {
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
					dev->DataBuf[0] = x54x_bios_command(dev, dev->max_id, cmd, (dev->lba_bios)?1:0);
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

					for (i=0; i<SCSI_ID_MAX; i++) {
					    dev->DataBuf[i] = 0x00;

					    /* Skip the HA .. */
					    if (i == host_id) continue;

					    for (j=0; j<SCSI_LUN_MAX; j++) {
						if (scsi_device_present(i, j))
						    dev->DataBuf[i] |= (1<<j);
					    }
					}
					dev->DataReplyLeft = i;
					break;

				case CMD_RETCONF: /* return Configuration */
					if (dev->ven_get_dma)
						dev->DataBuf[0] = (1<<dev->ven_get_dma(dev));
					else
						dev->DataBuf[0] = (1<<dev->DmaChannel);

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
				{
					ReplyISI = (ReplyInquireSetupInformation *)dev->DataBuf;
					memset(ReplyISI, 0x00, sizeof(ReplyInquireSetupInformation));

					ReplyISI->uBusTransferRate = dev->ATBusSpeed;
					ReplyISI->uPreemptTimeOnBus = dev->BusOnTime;
					ReplyISI->uTimeOffBus = dev->BusOffTime;
					ReplyISI->cMailbox = dev->MailboxCount;
					U32_TO_ADDR(ReplyISI->MailboxAddress, dev->MailboxOutAddr);

					if (dev->get_ven_data) {
						dev->get_ven_data(dev);
					}

					dev->DataReplyLeft = dev->CmdBuf[0];
					x54x_log("Return Setup Information: %d (length: %i)\n", dev->CmdBuf[0], sizeof(ReplyInquireSetupInformation));
				}
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
					DMAPageRead(FIFOBuf, dev->dma_buffer, 64);
					break;

				case CMD_READ_CH2:	/* write channel 2 buffer */
					dev->DataReplyLeft = 0;
					Address.hi = dev->CmdBuf[0];
					Address.mid = dev->CmdBuf[1];
					Address.lo = dev->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					x54x_log("Adaptec LocalRAM: Writing 64 bytes at %08X\n", FIFOBuf);
					DMAPageWrite(FIFOBuf, dev->dma_buffer, 64);
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
		if (dev->int_geom_writable)
			dev->Interrupt = val;
		break;

	case 3:
		if (dev->int_geom_writable)
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
x54x_write(uint32_t port, uint8_t val, void *priv)
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


void
x54x_io_set(x54x_t *dev, uint32_t base, uint8_t len)
{
    int bit32 = 0;

    if (dev->bus & DEVICE_PCI)
	bit32 = 1;
    else if ((dev->bus & DEVICE_MCA) && dev->bit32)
	bit32 = 1;

    if (bit32) {
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
    int bit32 = 0;

    if (dev->bus & DEVICE_PCI)
	bit32 = 1;
    else if ((dev->bus & DEVICE_MCA) && dev->bit32)
	bit32 = 1;

    x54x_log("x54x: Removing I/O handler at %04X\n", base);

    if (bit32) {
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
    int bit32 = 0;

    if (dev->bus & DEVICE_PCI)
	bit32 = 1;
    else if ((dev->bus & DEVICE_MCA) && dev->bit32)
	bit32 = 1;

    if (bit32) {
	mem_mapping_add(&dev->mmio_mapping, addr, 0x20,
		        x54x_read, x54x_readw, x54x_readl,
			x54x_write, x54x_writew, x54x_writel,
			NULL, MEM_MAPPING_EXTERNAL, dev);
    } else {
	mem_mapping_add(&dev->mmio_mapping, addr, 0x20,
		        x54x_read, x54x_readw, NULL,
			x54x_write, x54x_writew, NULL,
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
x54x_init(device_t *info)
{
    x54x_t *dev;

    /* Allocate control block and set up basic stuff. */
    dev = malloc(sizeof(x54x_t));
    if (dev == NULL) return(dev);
    memset(dev, 0x00, sizeof(x54x_t));
    dev->type = info->local;

    dev->bus = info->flags;

    timer_add(x54x_reset_poll, &dev->ResetCB, &dev->ResetCB, dev);

    x54x_dev = dev;

    scsi_mutex(1);

    wake_poll_thread = thread_create_event();
    thread_started = thread_create_event();

    /* Create a waitable event. */
    evt = thread_create_event();
    wait_evt = thread_create_event();

    x54x_thread_start(dev);
    thread_wait_event((event_t *) thread_started, -1);

    return(dev);
}


void
x54x_close(void *priv)
{
    x54x_t *dev = (x54x_t *)priv;

    if (dev) {
	x54x_dev = NULL;

        /* Tell the thread to terminate. */
	if (poll_tid != NULL) {
		x54x_busy(0);

		x54x_log("Waiting for SCSI thread to end...\n");
		/* Wait for the end event. */
		thread_wait((event_t *) poll_tid, -1);
		x54x_log("SCSI thread ended\n");

		poll_tid = NULL;
	}

	dev->MailboxInit = dev->BIOSMailboxInit = 0;
	dev->MailboxCount = dev->BIOSMailboxCount = 0;
	dev->MailboxReq = dev->BIOSMailboxReq = 0;

	if (dev->ven_data)
		free(dev->ven_data);

	if (wait_evt) {
		thread_destroy_event((event_t *) evt);
		evt = NULL;
	}

	if (evt) {
		thread_destroy_event((event_t *) evt);
		evt = NULL;
	}

	if (thread_started) {
		thread_destroy_event((event_t *) thread_started);
		thread_started = NULL;
	}

	if (wake_poll_thread) {
		thread_destroy_event((event_t *) wake_poll_thread);
		wake_poll_thread = NULL;
	}

	scsi_mutex(0);

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

    dev->ResetCB = 0LL;
    dev->Status = STAT_IDLE | STAT_INIT;
}
