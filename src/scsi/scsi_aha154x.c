/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the AHA-154x series of SCSI Host Adapters
 *		made by Adaptec, Inc. These controllers were designed for
 *		the ISA bus.
 *
 * NOTE:	THIS IS CURRENTLY A MESS, but will be cleaned up as I go.
 *
 * Version:	@(#)scsi_aha154x.c	1.0.15	2017/09/01
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Original Buslogic version by SA1988 and Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "../ibm.h"
#include "../io.h"
#include "../mca.h"
#include "../mem.h"
#include "../mca.h"
#include "../rom.h"
#include "../dma.h"
#include "../pic.h"
#include "../timer.h"
#include "../device.h"
#include "../win/plat_thread.h"
#include "scsi.h"
#include "scsi_bios_command.h"
#include "scsi_device.h"
#include "scsi_aha154x.h"


#define SCSI_DELAY_TM		1			/* was 50 */
#define AHA_RESET_DURATION_US	UINT64_C(5000)


#define ROM_SIZE	16384				/* one ROM is 16K */
#define NVR_SIZE	32				/* size of NVR */


/* EEPROM map and bit definitions. */
#define EE0_HOSTID	0x07	/* EE(0) [2:0]				*/
#define EE0_ALTFLOP	0x80	/* EE(0) [7] FDC at 370h		*/
#define EE1_IRQCH	0x07	/* EE(1) [3:0]				*/
#define EE1_DMACH	0x70	/* EE(1) [7:4]				*/
#define EE2_RMVOK	0x01	/* EE(2) [0] Support removable disks	*/
#define EE2_HABIOS	0x02	/* EE(2) [1] HA Bios Space Reserved	*/
#define EE2_INT19	0x04	/* EE(2) [2] HA Bios Controls INT19	*/
#define EE2_DYNSCAN	0x08	/* EE(2) [3] Dynamically scan bus	*/
#define EE2_TWODRV	0x10	/* EE(2) [4] Allow more than 2 drives	*/
#define EE2_SEEKRET	0x20	/* EE(2) [5] Immediate return on seek	*/
#define EE2_EXT1G	0x80	/* EE(2) [7] Extended Translation >1GB	*/
#define EE3_SPEED	0x00	/* EE(3) [7:0] DMA Speed		*/
#define  SPEED_33	0xFF
#define  SPEED_50	0x00
#define  SPEED_56	0x04
#define  SPEED_67	0x01
#define  SPEED_80	0x02
#define  SPEED_10	0x03
#define EE4_FLOPTOK	0x80	/* EE(4) [7] Support Flopticals		*/
#define EE6_PARITY	0x01	/* EE(6) [0] parity check enable	*/
#define EE6_TERM	0x02	/* EE(6) [1] host term enable		*/
#define EE6_RSTBUS	0x04	/* EE(6) [2] reset SCSI bus on boot	*/
#define EEE_SYNC	0x01	/* EE(E) [0] Enable Sync Negotiation	*/
#define EEE_DISCON	0x02	/* EE(E) [1] Enable Disconnection	*/
#define EEE_FAST	0x04	/* EE(E) [2] Enable FAST SCSI		*/
#define EEE_START	0x08	/* EE(E) [3] Enable Start Unit		*/


/*
 * Host Adapter I/O ports.
 *
 * READ  Port x+0: STATUS
 * WRITE Port x+0: CONTROL
 *
 * READ  Port x+1: DATA
 * WRITE Port x+1: COMMAND
 *
 * READ  Port x+2: INTERRUPT STATUS
 * WRITE Port x+2: (undefined?)
 *
 * R/W   Port x+3: (undefined)
 */

/* WRITE CONTROL commands. */
#define CTRL_HRST	0x80		/* Hard reset */
#define CTRL_SRST	0x40		/* Soft reset */
#define CTRL_IRST	0x20		/* interrupt reset */
#define CTRL_SCRST	0x10		/* SCSI bus reset */

/* READ STATUS. */
#define STAT_STST	0x80		/* self-test in progress */
#define STAT_DFAIL	0x40		/* internal diagnostic failure */
#define STAT_INIT	0x20		/* mailbox initialization required */
#define STAT_IDLE	0x10		/* HBA is idle */
#define STAT_CDFULL	0x08		/* Command/Data output port is full */
#define STAT_DFULL	0x04		/* Data input port is full */
#define STAT_INVCMD	0x01		/* Invalid command */

/* READ/WRITE DATA. */
#define CMD_NOP		0x00		/* No operation */
#define CMD_MBINIT	0x01		/* mailbox initialization */
#define CMD_START_SCSI	0x02		/* Start SCSI command */
#define CMD_BIOSCMD	0x03		/* Execute ROM BIOS command */
#define CMD_INQUIRY	0x04		/* Adapter inquiry */
#define CMD_EMBOI	0x05		/* enable Mailbox Out Interrupt */
#define CMD_SELTIMEOUT	0x06		/* Set SEL timeout */
#define CMD_BUSON_TIME	0x07		/* set bus-On time */
#define CMD_BUSOFF_TIME	0x08		/* set bus-off time */
#define CMD_DMASPEED	0x09		/* set ISA DMA speed */
#define CMD_RETDEVS	0x0A		/* return installed devices */
#define CMD_RETCONF	0x0B		/* return configuration data */
#define CMD_TARGET	0x0C		/* set HBA to target mode */
#define CMD_RETSETUP	0x0D		/* return setup data */
#define CMD_ECHO	0x1F		/* ECHO command data */
#define CMD_OPTIONS	0x21		/* set adapter options */
#define CMD_WRITE_EEPROM 0x22		/* UNDOC: Write EEPROM */
#define CMD_READ_EEPROM	0x23		/* UNDOC: Read EEPROM */
#define CMD_SHADOW_RAM	0x24		/* UNDOC: BIOS shadow ram */
#define CMD_BIOS_MBINIT	0x25		/* UNDOC: BIOS mailbox initialization */
#define CMD_MEMORY_MAP_1 0x26		/* UNDOC: Memory Mapper */
#define CMD_MEMORY_MAP_2 0x27		/* UNDOC: Memory Mapper */
#define CMD_EXTBIOS     0x28		/* UNDOC: return extended BIOS info */
#define CMD_MBENABLE    0x29		/* set mailbox interface enable */
#define CMD_BIOS_SCSI	0x82		/* start ROM BIOS SCSI command */

/* READ INTERRUPT STATUS. */
#define INTR_ANY	0x80		/* any interrupt */
#define INTR_SRCD	0x08		/* SCSI reset detected */
#define INTR_HACC	0x04		/* HA command complete */
#define INTR_MBOA	0x02		/* MBO empty */
#define INTR_MBIF	0x01		/* MBI full */


enum {
    AHA_154xB,
    AHA_154xC,
    AHA_154xCF,
    AHA_154xCP,
    AHA_1640
};


/* Structure for the INQUIRE_SETUP_INFORMATION reply. */
#pragma pack(push,1)
typedef struct {
    uint8_t	uOffset		:4,
		uTransferPeriod :3,
		fSynchronous	:1;
} ReplyInquireSetupInformationSynchronousValue;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	fSynchronousInitiationEnabled	:1,
		fParityCheckingEnabled		:1,
		uReserved1			:6;
    uint8_t	uBusTransferRate;
    uint8_t	uPreemptTimeOnBus;
    uint8_t	uTimeOffBus;
    uint8_t	cMailbox;
    addr24	MailboxAddress;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId0To7[8];
    uint8_t	uDisconnectPermittedId0To7;
    uint8_t	uSignature;
    uint8_t	uCharacterD;
    uint8_t	uHostBusType;
    uint8_t	uWideTransferPermittedId0To7;
    uint8_t	uWideTransfersActiveId0To7;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId8To15[8];
    uint8_t	uDisconnectPermittedId8To15;
    uint8_t	uReserved2;
    uint8_t	uWideTransferPermittedId8To15;
    uint8_t	uWideTransfersActiveId8To15;
} ReplyInquireSetupInformation;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	Count;
    addr24	Address;
} MailboxInit_t;
#pragma pack(pop)

/*
 * Mailbox Definitions.
 *
 * Mailbox Out (MBO) command values.
 */
#define MBO_FREE                  0x00
#define MBO_START                 0x01
#define MBO_ABORT                 0x02

/* Mailbox In (MBI) status values. */
#define MBI_FREE                  0x00
#define MBI_SUCCESS               0x01
#define MBI_ABORT                 0x02
#define MBI_NOT_FOUND             0x03
#define MBI_ERROR                 0x04

#pragma pack(push,1)
typedef struct {
    uint8_t	CmdStatus;
    addr24	CCBPointer;
} Mailbox_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint32_t	CCBPointer;
    union {
	struct {
		uint8_t Reserved[3];
		uint8_t ActionCode;
	} out;
	struct {
		uint8_t HostStatus;
		uint8_t TargetStatus;
		uint8_t Reserved;
		uint8_t CompletionCode;
	} in;
    }		u;
} Mailbox32_t;
#pragma pack(pop)

/*
 *
 * CCB - SCSI Command Control Block
 *
 *    The CCB is a superset of the CDB (Command Descriptor Block)
 *    and specifies detailed information about a SCSI command.
 *
 */
/*    Byte 0    Command Control Block Operation Code */
#define SCSI_INITIATOR_COMMAND		0x00
#define TARGET_MODE_COMMAND		0x01
#define SCATTER_GATHER_COMMAND		0x02
#define SCSI_INITIATOR_COMMAND_RES	0x03
#define SCATTER_GATHER_COMMAND_RES	0x04
#define BUS_RESET			0x81

/*    Byte 1    Address and Direction Control */
#define CCB_TARGET_ID_SHIFT		0x06	/* CCB Op Code = 00, 02 */
#define CCB_INITIATOR_ID_SHIFT		0x06	/* CCB Op Code = 01 */
#define CCB_DATA_XFER_IN		0x01
#define CCB_DATA_XFER_OUT		0x02
#define CCB_LUN_MASK			0x07	/* Logical Unit Number */

/*    Byte 2    SCSI_Command_Length - Length of SCSI CDB
      Byte 3    Request Sense Allocation Length */
#define FOURTEEN_BYTES			0x00	/* Request Sense Buffer size */
#define NO_AUTO_REQUEST_SENSE		0x01	/* No Request Sense Buffer */

/*    Bytes 4, 5 and 6    Data Length		 - Data transfer byte count */
/*    Bytes 7, 8 and 9    Data Pointer		 - SGD List or Data Buffer */
/*    Bytes 10, 11 and 12 Link Pointer		 - Next CCB in Linked List */
/*    Byte 13   Command Link ID			 - TBD (I don't know yet) */
/*    Byte 14   Host Status			 - Host Adapter status */
#define CCB_COMPLETE			0x00	/* CCB completed without error */
#define CCB_LINKED_COMPLETE		0x0A	/* Linked command completed */
#define CCB_LINKED_COMPLETE_INT		0x0B	/* Linked complete with intr */
#define CCB_SELECTION_TIMEOUT		0x11	/* Set SCSI selection timed out */
#define CCB_DATA_OVER_UNDER_RUN		0x12
#define CCB_UNEXPECTED_BUS_FREE		0x13	/* Trg dropped SCSI BSY */
#define CCB_PHASE_SEQUENCE_FAIL		0x14	/* Trg bus phase sequence fail */
#define CCB_BAD_MBO_COMMAND		0x15	/* MBO command not 0, 1 or 2 */
#define CCB_INVALID_OP_CODE		0x16	/* CCB invalid operation code */
#define CCB_BAD_LINKED_LUN		0x17	/* Linked CCB LUN diff from 1st */
#define CCB_INVALID_DIRECTION		0x18	/* Invalid target direction */
#define CCB_DUPLICATE_CCB		0x19	/* Duplicate CCB */
#define CCB_INVALID_CCB			0x1A	/* Invalid CCB - bad parameter */

/*    Byte 15   Target Status

      See scsi.h files for these statuses.
      Bytes 16 and 17   Reserved (must be 0)
      Bytes 18 through 18+n-1, where n=size of CDB  Command Descriptor Block */

#pragma pack(push,1)
typedef struct {
    uint8_t	Opcode;
    uint8_t	Reserved1	:3,
		ControlByte	:2,
		TagQueued	:1,
		QueueTag	:2;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    uint32_t	DataLength;
    uint32_t	DataPointer;
    uint8_t	Reserved2[2];
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Id;
    uint8_t	Lun		:5,
		LegacyTagEnable	:1,
		LegacyQueueTag	:2;
    uint8_t	Cdb[12];
    uint8_t	Reserved3[6];
    uint32_t	SensePointer;
} CCB32;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	Opcode;
    uint8_t	Lun		:3,
		ControlByte	:2,
		Id		:3;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    addr24	DataLength;
    addr24	DataPointer;
    addr24	LinkPointer;
    uint8_t	LinkId;
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Reserved[2];
    uint8_t	Cdb[12];
} CCB;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	Opcode;
    uint8_t	Pad1		:3,
		ControlByte	:2,
		Pad2		:3;
    uint8_t	CdbLength;
    uint8_t	RequestSenseLength;
    uint8_t	Pad3[10];
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	Pad4[2];
    uint8_t	Cdb[12];
} CCBC;
#pragma pack(pop)

#pragma pack(push,1)
typedef union {
    CCB32	new;
    CCB		old;
    CCBC	common;
} CCBU;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    CCBU	CmdBlock;
    uint8_t	*RequestSenseBuffer;
    uint32_t	CCBPointer;
    int		Is24bit;
    uint8_t	TargetID;
    uint8_t	LUN;
    uint8_t	HostStatus;
    uint8_t	TargetStatus;
    uint8_t	MailboxCompletionCode;
} Req_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    int8_t	type;				/* type of device */
    char	name[16];			/* name of device */

    int8_t	Irq;
    int8_t	DmaChannel;
    int8_t	HostID;
    uint32_t	Base;
    uint8_t	pos_regs[8];			/* MCA */

    uint8_t	bid;				/* board ID */
    char	fwl, fwh;			/* firmware info */

    wchar_t	*bios_path;			/* path to BIOS image file */
    uint32_t	rom_addr;			/* address of BIOS ROM */
    uint16_t	rom_ioaddr;			/* offset in BIOS of I/O addr */
    uint16_t	rom_shram;			/* index to shared RAM */
    uint16_t	rom_shramsz;			/* size of shared RAM */
    uint16_t	rom_fwhigh;			/* offset in BIOS of ver ID */
    rom_t	bios;				/* BIOS memory descriptor */
    uint8_t	*rom1;				/* main BIOS image */
    uint8_t	*rom2;				/* SCSI-Select image */

    wchar_t	*nvr_path;			/* path to NVR image file */
    uint8_t	*nvr;				/* EEPROM buffer */

    int		ResetCB;

    int		ExtendedLUNCCBFormat;
    Req_t	Req;
    uint8_t	Status;
    uint8_t	Interrupt;
    uint8_t	Geometry;
    uint8_t	Control;
    uint8_t	Command;
    uint8_t	CmdBuf[53];
    uint8_t	CmdParam;
    uint8_t	CmdParamLeft;
    uint8_t	DataBuf[64];
    uint16_t	DataReply;
    uint16_t	DataReplyLeft;
    uint32_t	MailboxCount;
    uint32_t	MailboxOutAddr;
    uint32_t	MailboxOutPosCur;
    uint32_t	MailboxInAddr;
    uint32_t	MailboxInPosCur;
    int		Mbx24bit;
    int		MailboxOutInterrupts;
    int		MbiActive[256];
    int		PendingInterrupt;
    int		Lock;
    event_t	*evt;
    int		scan_restart;
} aha_t;
#pragma pack(pop)


static uint16_t	aha_ports[] = {
    0x0330, 0x0334, 0x0230, 0x0234,
    0x0130, 0x0134, 0x0000, 0x0000
};


static void	aha_cmd_thread(void *priv);
static thread_t	*poll_tid;


#if ENABLE_AHA154X_LOG
int aha_do_log = ENABLE_AHA154X_LOG;
#endif


static void
aha_log(const char *fmt, ...)
{
#if ENABLE_AHA154X_LOG
    va_list ap;

    if (aha_do_log) {
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}


/*
 * Write data to the BIOS space.
 *
 * AHA-1542C's and up have a feature where they map a 128-byte
 * RAM space into the ROM BIOS' address space, and then use it
 * as working memory. This function implements the writing to
 * that memory.
 *
 * We enable/disable this memory through AHA command 0x24.
 */
static void
aha_mem_write(uint32_t addr, uint8_t val, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    if ((addr & rom->mask) >= 0x3F80)
	rom->rom[addr & rom->mask] = val;
}


static uint8_t
aha_mem_read(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    return(rom->rom[addr & rom->mask]);
}


static uint16_t
aha_mem_readw(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    return(*(uint16_t *)&rom->rom[addr & rom->mask]);
}


static uint32_t
aha_mem_readl(uint32_t addr, void *priv)
{
    rom_t *rom = (rom_t *)priv;

    return(*(uint32_t *)&rom->rom[addr & rom->mask]);
}


static uint8_t
aha154x_shram(aha_t *dev, uint8_t cmd)
{
    /* If not supported, give up. */
    if (dev->rom_shram == 0x0000) return(0x04);

    switch(cmd) {
	case 0x00:	/* disable, make it look like ROM */
		memset(&dev->bios.rom[dev->rom_shram], 0xFF, dev->rom_shramsz);
		break;

	case 0x02:	/* clear it */
		memset(&dev->bios.rom[dev->rom_shram], 0x00, dev->rom_shramsz);
		break;

	case 0x03:	/* enable, clear for use */
		memset(&dev->bios.rom[dev->rom_shram], 0x00, dev->rom_shramsz);
		break;
    }

    /* Firmware expects 04 status. */
    return(0x04);
}


static uint8_t
aha154x_eeprom(aha_t *dev, uint8_t cmd,uint8_t arg,uint8_t len,uint8_t off,uint8_t *bufp)
{
    uint8_t r = 0xff;

    aha_log("%s: EEPROM cmd=%02x, arg=%02x len=%d, off=%02x\n",
				dev->name, cmd, arg, len, off);

    /* Only if we can handle it.. */
    if (dev->nvr == NULL) return(r);

    if ((off+len) > NVR_SIZE) return(r);	/* no can do.. */

    if (cmd == 0x22) {
	/* Write data to the EEPROM. */
	memcpy(&dev->nvr[off], bufp, len);
	r = 0;
    }

    if (cmd == 0x23) {
	/* Read data from the EEPROM. */
	memcpy(bufp, &dev->nvr[off], len);
	r = len;
    }

    return(r);
}


/* Mess with the AHA-154xCF's Shadow RAM. */
static uint8_t
aha154x_mmap(aha_t *dev, uint8_t cmd)
{
    aha_log("%s: MEMORY cmd=%02x\n", dev->name, cmd);

    switch(cmd) {
	case 0x26:
		/* Disable the mapper, so, set ROM1 active. */
		dev->bios.rom = dev->rom1;
		break;

	case 0x27:
		/* Enable the mapper, so, set ROM2 active. */
		dev->bios.rom = dev->rom2;
		break;
    }

    return(0);
}


static void
RaiseIntr(aha_t *dev, int suppress, uint8_t Interrupt)
{
	if (Interrupt & (INTR_MBIF | INTR_MBOA))
	{
		if (!(dev->Interrupt & INTR_HACC))
		{
			dev->Interrupt |= Interrupt;	/* Report now. */
		}
		else
		{
			dev->PendingInterrupt |= Interrupt;	/* Report later. */
		}
	}
	else if (Interrupt & INTR_HACC)
	{
		if (dev->Interrupt == 0 || dev->Interrupt == (INTR_ANY | INTR_HACC))
		{
			aha_log("%s: BuslogicRaiseInterrupt(): Interrupt=%02X\n", dev->name, dev->Interrupt);
		}
		dev->Interrupt |= Interrupt;
	}
	else
	{
		aha_log("%s: BuslogicRaiseInterrupt(): Invalid interrupt state!\n", dev->name);
	}

	dev->Interrupt |= INTR_ANY;

	if (!suppress)
	{
		picint(1 << dev->Irq);
	}
}


static void
ClearIntr(aha_t *dev)
{
    dev->Interrupt = 0;
    aha_log("%s: lowering IRQ %i (stat 0x%02x)\n",
		dev->name, dev->Irq, dev->Interrupt);
    picintc(1 << dev->Irq);
    if (dev->PendingInterrupt) {
	aha_log("%s: Raising Interrupt 0x%02X (Pending)\n",
				dev->name, dev->Interrupt);
	if (dev->MailboxOutInterrupts || !(dev->Interrupt & INTR_MBOA)) {
		RaiseIntr(dev, 0, dev->PendingInterrupt);
	}
	dev->PendingInterrupt = 0;
    }
}


static void
aha_reset(aha_t *dev)
{
    if (dev->evt) {
	thread_destroy_event(dev->evt);
	dev->evt = NULL;
	if (poll_tid) {
		poll_tid = NULL;
	}
   }

    dev->ResetCB = 0;
    dev->scan_restart = 0;

    dev->Status = STAT_IDLE | STAT_INIT;
    dev->Geometry = 0x80;
    dev->Command = 0xFF;
    dev->CmdParam = 0;
    dev->CmdParamLeft = 0;
    dev->ExtendedLUNCCBFormat = 0;
    dev->MailboxOutPosCur = 0;
    dev->MailboxInPosCur = 0;
    dev->MailboxOutInterrupts = 0;
    dev->PendingInterrupt = 0;
    dev->Lock = 0;

    ClearIntr(dev);
}


static void
aha_reset_ctrl(aha_t *dev, uint8_t Reset)
{
    /* Only if configured.. */
    if (dev->Base == 0x0000) return;

    /* Say hello! */
    pclog("Adaptec %s (IO=0x%04X, IRQ=%d, DMA=%d, BIOS @%05lX) ID=%d\n",
	dev->name, dev->Base, dev->Irq, dev->DmaChannel,
	dev->rom_addr, dev->HostID);

    aha_reset(dev);
    if (Reset) {
	dev->Status |= STAT_STST;
	dev->Status &= ~STAT_IDLE;
    }
    dev->ResetCB = AHA_RESET_DURATION_US * TIMER_USEC;
}


static void
aha_reset_poll(void *priv)
{
    aha_t *dev = (aha_t *)priv;

    dev->Status &= ~STAT_STST;
    dev->Status |= STAT_IDLE;

    dev->ResetCB = 0;
}


static void
aha_cmd_done(aha_t *dev, int suppress)
{
    dev->DataReply = 0;
    dev->Status |= STAT_IDLE;

    if ((dev->Command != CMD_START_SCSI) && (dev->Command != CMD_BIOS_SCSI)) {
	dev->Status &= ~STAT_DFULL;
	aha_log("%s: Raising IRQ %i\n", dev->name, dev->Irq);
	RaiseIntr(dev, suppress, INTR_HACC);
    }

    dev->Command = 0xff;
    dev->CmdParam = 0;
}


static void
aha_mbi_setup(aha_t *dev, uint32_t CCBPointer, CCBU *CmdBlock,
	      uint8_t HostStatus, uint8_t TargetStatus, uint8_t mbcc)
{
    Req_t *req = &dev->Req;

    req->CCBPointer = CCBPointer;
    memcpy(&(req->CmdBlock), CmdBlock, sizeof(CCB32));
    req->Is24bit = dev->Mbx24bit;
    req->HostStatus = HostStatus;
    req->TargetStatus = TargetStatus;
    req->MailboxCompletionCode = mbcc;

    aha_log("Mailbox in setup\n");
}


static void
aha_mbi(aha_t *dev)
{	
    Req_t *req = &dev->Req;
    uint32_t CCBPointer = req->CCBPointer;
    CCBU *CmdBlock = &(req->CmdBlock);
    uint8_t HostStatus = req->HostStatus;
    uint8_t TargetStatus = req->TargetStatus;
    uint8_t MailboxCompletionCode = req->MailboxCompletionCode;
    Mailbox32_t Mailbox32;
    Mailbox_t MailboxIn;
    uint32_t Incoming;

    Mailbox32.CCBPointer = CCBPointer;
    Mailbox32.u.in.HostStatus = HostStatus;
    Mailbox32.u.in.TargetStatus = TargetStatus;
    Mailbox32.u.in.CompletionCode = MailboxCompletionCode;

    Incoming = dev->MailboxInAddr + (dev->MailboxInPosCur * (dev->Mbx24bit ? sizeof(Mailbox_t) : sizeof(Mailbox32_t)));

    if (MailboxCompletionCode != MBI_NOT_FOUND) {
	CmdBlock->common.HostStatus = HostStatus;
	CmdBlock->common.TargetStatus = TargetStatus;		
		
	/* Rewrite the CCB up to the CDB. */
	aha_log("CCB rewritten to the CDB (pointer %08X)\n", CCBPointer);
	DMAPageWrite(CCBPointer, (char *)CmdBlock, 18);
    } else {
	aha_log("Mailbox not found!\n");
    }

    aha_log("Host Status 0x%02X, Target Status 0x%02X\n",HostStatus,TargetStatus);

    if (dev->Mbx24bit) {
	MailboxIn.CmdStatus = Mailbox32.u.in.CompletionCode;
	U32_TO_ADDR(MailboxIn.CCBPointer, Mailbox32.CCBPointer);
	aha_log("Mailbox 24-bit: Status=0x%02X, CCB at 0x%04X\n", MailboxIn.CmdStatus, ADDR_TO_U32(MailboxIn.CCBPointer));

	DMAPageWrite(Incoming, (char *)&MailboxIn, sizeof(Mailbox_t));
	aha_log("%i bytes of 24-bit mailbox written to: %08X\n", sizeof(Mailbox_t), Incoming);
    } else {
	aha_log("Mailbox 32-bit: Status=0x%02X, CCB at 0x%04X\n", Mailbox32.u.in.CompletionCode, Mailbox32.CCBPointer);

	DMAPageWrite(Incoming, (char *)&Mailbox32, sizeof(Mailbox32_t));		
	aha_log("%i bytes of 32-bit mailbox written to: %08X\n", sizeof(Mailbox32_t), Incoming);
    }

    dev->MailboxInPosCur++;
    if (dev->MailboxInPosCur >= dev->MailboxCount)
		dev->MailboxInPosCur = 0;

    RaiseIntr(dev, 0, INTR_MBIF | INTR_ANY);

    while (dev->Interrupt) {
	thread_wait_event(dev->evt, 10);
    }
}


static void
aha_rd_sge(int Is24bit, uint32_t SGList, uint32_t Entries, SGE32 *SG)
{
    SGE SGE24[MAX_SG_DESCRIPTORS];
    uint32_t i;

    if (Is24bit) {
	DMAPageRead(SGList, (char *)&SGE24, Entries * sizeof(SGE));

	for (i=0; i<Entries; ++i) {
		/* Convert the 24-bit entries into 32-bit entries. */
		SG[i].Segment = ADDR_TO_U32(SGE24[i].Segment);
		SG[i].SegmentPointer = ADDR_TO_U32(SGE24[i].SegmentPointer);
	}
    } else {
	DMAPageRead(SGList, (char *)SG, Entries * sizeof(SGE32));		
    }
}


static void
aha_buf_alloc(Req_t *req, int Is24bit)
{
    uint32_t sg_buffer_pos = 0;
    uint32_t DataPointer, DataLength;
    uint32_t SGEntryLength = (Is24bit ? sizeof(SGE) : sizeof(SGE32));
    uint32_t Address;

    if (Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;		
    }
    aha_log("Data Buffer write: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);	

    if (SCSIDevices[req->TargetID][req->LUN].CmdBuffer != NULL) {
	free(SCSIDevices[req->TargetID][req->LUN].CmdBuffer);
	SCSIDevices[req->TargetID][req->LUN].CmdBuffer = NULL;
    }

    if ((req->CmdBlock.common.ControlByte != 0x03) && DataLength) {
	if (req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND ||
	    req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES) {
		uint32_t SGRead;
		uint32_t ScatterEntry;
		SGE32 SGBuffer[MAX_SG_DESCRIPTORS];
		uint32_t SGLeft = DataLength / SGEntryLength;
		uint32_t SGAddrCurrent = DataPointer;
		uint32_t DataToTransfer = 0;
			
		do {
			SGRead = (SGLeft < ELEMENTS(SGBuffer)) ? SGLeft : ELEMENTS(SGBuffer);
			SGLeft -= SGRead;

			aha_rd_sge(Is24bit, SGAddrCurrent, SGRead, SGBuffer);

			for (ScatterEntry=0; ScatterEntry<SGRead; ScatterEntry++) {
				aha_log("S/G Write: ScatterEntry=%u\n", ScatterEntry);

				Address = SGBuffer[ScatterEntry].SegmentPointer;
				DataToTransfer += SGBuffer[ScatterEntry].Segment;

				aha_log("S/G Write: Address=%08X DatatoTransfer=%u\n", Address, DataToTransfer);
			}

			SGAddrCurrent += SGRead * SGEntryLength;
		} while (SGLeft > 0);

		aha_log("Data to transfer (S/G) %d\n", DataToTransfer);

		SCSIDevices[req->TargetID][req->LUN].InitLength = DataToTransfer;

		aha_log("Allocating buffer for Scatter/Gather (%i bytes)\n", DataToTransfer);
		SCSIDevices[req->TargetID][req->LUN].CmdBuffer = (uint8_t *) malloc(DataToTransfer);
		memset(SCSIDevices[req->TargetID][req->LUN].CmdBuffer, 0, DataToTransfer);

		/* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
		   checking its length, so do this procedure for both no read/write commands. */
		if ((req->CmdBlock.common.ControlByte == CCB_DATA_XFER_OUT) ||
		    (req->CmdBlock.common.ControlByte == 0x00)) {
			SGLeft = DataLength / SGEntryLength;
			SGAddrCurrent = DataPointer;

			do {
				SGRead = (SGLeft < ELEMENTS(SGBuffer)) ? SGLeft : ELEMENTS(SGBuffer);
				SGLeft -= SGRead;

				aha_rd_sge(Is24bit, SGAddrCurrent,
						      SGRead, SGBuffer);

				for (ScatterEntry=0; ScatterEntry<SGRead; ScatterEntry++) {
					aha_log("S/G Write: ScatterEntry=%u\n", ScatterEntry);

					Address = SGBuffer[ScatterEntry].SegmentPointer;
					DataToTransfer = SGBuffer[ScatterEntry].Segment;

					aha_log("S/G Write: Address=%08X DatatoTransfer=%u\n", Address, DataToTransfer);

					DMAPageRead(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer + sg_buffer_pos, DataToTransfer);
					sg_buffer_pos += DataToTransfer;
				}

				SGAddrCurrent += SGRead * (Is24bit ? sizeof(SGE) : sizeof(SGE32));
			} while (SGLeft > 0);				
		}
	} else if (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
		   req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) {
			Address = DataPointer;

			SCSIDevices[req->TargetID][req->LUN].InitLength = DataLength;

			aha_log("Allocating buffer for direct transfer (%i bytes)\n", DataLength);
			SCSIDevices[req->TargetID][req->LUN].CmdBuffer = (uint8_t *) malloc(DataLength);
			memset(SCSIDevices[req->TargetID][req->LUN].CmdBuffer, 0, DataLength);

			if (DataLength > 0) {
				DMAPageRead(Address,
					    (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer,
					    SCSIDevices[req->TargetID][req->LUN].InitLength);
			}
	}
    }
}


static void
aha_buf_free(Req_t *req)
{
    SGE32 SGBuffer[MAX_SG_DESCRIPTORS];
    uint32_t DataPointer = 0;
    uint32_t DataLength = 0;
    uint32_t sg_buffer_pos = 0;
    uint32_t SGRead;
    uint32_t ScatterEntry;
    uint32_t SGEntrySize;
    uint32_t SGLeft;
    uint32_t SGAddrCurrent;
    uint32_t Address;
    uint32_t Residual;
    uint32_t DataToTransfer;

    if (req->Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;		
    }

    if ((DataLength != 0) && (req->CmdBlock.common.Cdb[0] == GPCMD_TEST_UNIT_READY)) {
	aha_log("Data length not 0 with TEST UNIT READY: %i (%i)\n",
		DataLength, SCSIDevices[req->TargetID][req->LUN].InitLength);
    }

    if (req->CmdBlock.common.Cdb[0] == GPCMD_TEST_UNIT_READY) {
	DataLength = 0;
    }

    aha_log("Data Buffer read: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);

    /* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
       checking its length, so do this procedure for both read/write commands. */
    if ((DataLength > 0) &&
        ((req->CmdBlock.common.ControlByte == CCB_DATA_XFER_IN) ||
	(req->CmdBlock.common.ControlByte == 0x00))) {	
	if ((req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND) ||
	    (req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {
		SGEntrySize = (req->Is24bit ? sizeof(SGE) : sizeof(SGE32));			
		SGLeft = DataLength / SGEntrySize;
		SGAddrCurrent = DataPointer;

		do {
			SGRead = (SGLeft < ELEMENTS(SGBuffer)) ? SGLeft : ELEMENTS(SGBuffer);
			SGLeft -= SGRead;

			aha_rd_sge(req->Is24bit, SGAddrCurrent,
					      SGRead, SGBuffer);

			for (ScatterEntry=0; ScatterEntry<SGRead; ScatterEntry++) {
				aha_log("S/G: ScatterEntry=%u\n", ScatterEntry);

				Address = SGBuffer[ScatterEntry].SegmentPointer;
				DataToTransfer = SGBuffer[ScatterEntry].Segment;

				aha_log("S/G: Writing %i bytes at %08X\n", DataToTransfer, Address);

				DMAPageWrite(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer + sg_buffer_pos, DataToTransfer);
				sg_buffer_pos += DataToTransfer;
			}

			SGAddrCurrent += (SGRead * SGEntrySize);
		} while (SGLeft > 0);
	} else if (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
		   req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) {
		Address = DataPointer;

		aha_log("DMA: Writing %i bytes at %08X\n",
					DataLength, Address);
		DMAPageWrite(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer, DataLength);
	}
    }

    if ((req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) ||
	(req->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)) {
	/* Should be 0 when scatter/gather? */
	if (DataLength >= SCSIDevices[req->TargetID][req->LUN].InitLength) {
		Residual = DataLength;
		Residual -= SCSIDevices[req->TargetID][req->LUN].InitLength;
	} else {
		Residual = 0;
	}

	if (req->Is24bit) {
		U32_TO_ADDR(req->CmdBlock.old.DataLength, Residual);
		aha_log("24-bit Residual data length for reading: %d\n",
			ADDR_TO_U32(req->CmdBlock.old.DataLength));
	} else {
		req->CmdBlock.new.DataLength = Residual;
		aha_log("32-bit Residual data length for reading: %d\n",
				req->CmdBlock.new.DataLength);
	}
    }

    if (SCSIDevices[req->TargetID][req->LUN].CmdBuffer != NULL) {
	free(SCSIDevices[req->TargetID][req->LUN].CmdBuffer);
	SCSIDevices[req->TargetID][req->LUN].CmdBuffer = NULL;
    }
}


static uint8_t
ConvertSenseLength(uint8_t RequestSenseLength)
{
    aha_log("Unconverted Request Sense length %i\n", RequestSenseLength);

    if (RequestSenseLength == 0)
	RequestSenseLength = 14;
    else if (RequestSenseLength == 1)
	RequestSenseLength = 0;

    aha_log("Request Sense length %i\n", RequestSenseLength);

    return(RequestSenseLength);
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
	if (req->Is24bit) {
		SenseBufferAddress = req->CCBPointer;
		SenseBufferAddress += req->CmdBlock.common.CdbLength + 18;
	} else {
		SenseBufferAddress = req->CmdBlock.new.SensePointer;
	}

	aha_log("Request Sense address: %02X\n", SenseBufferAddress);

	aha_log("SenseBufferFree(): Writing %i bytes at %08X\n",
					SenseLength, SenseBufferAddress);
	DMAPageWrite(SenseBufferAddress, (char *)temp_sense, SenseLength);
	aha_log("Sense data written to buffer: %02X %02X %02X\n",
		temp_sense[2], temp_sense[12], temp_sense[13]);
    }
}


static void
aha_scsi_cmd(aha_t *dev)
{
    Req_t *req = &dev->Req;
    uint8_t id, lun;
    uint8_t temp_cdb[12];
    uint32_t i;
    int target_cdb_len = 12;

    id = req->TargetID;
    lun = req->LUN;

    target_cdb_len = scsi_device_cdb_length(id, lun);

    if (!scsi_device_valid(id, lun))
	fatal("SCSI target on %02i:%02i has disappeared\n", id, lun);

    aha_log("SCSI command being executed on ID %i, LUN %i\n", id, lun);

    aha_log("SCSI CDB[0]=0x%02X\n", req->CmdBlock.common.Cdb[0]);
    for (i=1; i<req->CmdBlock.common.CdbLength; i++)
	aha_log("SCSI CDB[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);

    memset(temp_cdb, 0x00, target_cdb_len);
    if (req->CmdBlock.common.CdbLength <= target_cdb_len) {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb,
		req->CmdBlock.common.CdbLength);
    } else {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb, target_cdb_len);
    }

    scsi_device_command(id, lun, req->CmdBlock.common.CdbLength, temp_cdb);

    aha_buf_free(req);

    SenseBufferFree(req, (SCSIStatus != SCSI_STATUS_OK));

    aha_log("Request complete\n");

    if (SCSIStatus == SCSI_STATUS_OK) {
	aha_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
    } else if (SCSIStatus == SCSI_STATUS_CHECK_CONDITION) {
	aha_mbi_setup(dev, req->CCBPointer, &req->CmdBlock,
			CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION, MBI_ERROR);
    }
}


static void
aha_req_setup(aha_t *dev, uint32_t CCBPointer, Mailbox32_t *Mailbox32)
{	
    Req_t *req = &dev->Req;
    uint8_t id, lun;
    uint8_t max_id = SCSI_ID_MAX-1;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (char *)&req->CmdBlock, sizeof(CCB32));

    req->Is24bit = dev->Mbx24bit;
    req->CCBPointer = CCBPointer;
    req->TargetID = dev->Mbx24bit ? req->CmdBlock.old.Id : req->CmdBlock.new.Id;
    req->LUN = dev->Mbx24bit ? req->CmdBlock.old.Lun : req->CmdBlock.new.Lun;

    id = req->TargetID;
    lun = req->LUN;
    if ((id > max_id) || (lun > 7)) {
	aha_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		      CCB_INVALID_CCB, SCSI_STATUS_OK, MBI_ERROR);
	aha_log("%s: Callback: Send incoming mailbox\n", dev->name);
	aha_mbi(dev);
	return;
    }
	
    aha_log("Scanning SCSI Target ID %i\n", id);		

    SCSIStatus = SCSI_STATUS_OK;
    SCSIDevices[id][lun].InitLength = 0;

    aha_buf_alloc(req, req->Is24bit);

    if (! scsi_device_present(id, lun)) {
	aha_log("SCSI Target ID %i and LUN %i have no device attached\n",id,lun);
	aha_buf_free(req);
	SenseBufferFree(req, 0);
	aha_mbi_setup(dev, CCBPointer, &req->CmdBlock,
		      CCB_SELECTION_TIMEOUT,SCSI_STATUS_OK,MBI_ERROR);
	aha_log("%s: Callback: Send incoming mailbox\n", dev->name);
	aha_mbi(dev);
    } else {
	aha_log("SCSI Target ID %i and LUN %i detected and working\n", id, lun);

	aha_log("Transfer Control %02X\n", req->CmdBlock.common.ControlByte);
	aha_log("CDB Length %i\n", req->CmdBlock.common.CdbLength);	
	aha_log("CCB Opcode %x\n", req->CmdBlock.common.Opcode);		
	if (req->CmdBlock.common.ControlByte > 0x03) {
		aha_log("Invalid control byte: %02X\n",
			req->CmdBlock.common.ControlByte);
	}

	aha_log("%s: Callback: Process SCSI request\n", dev->name);
	aha_scsi_cmd(dev);

	aha_log("%s: Callback: Send incoming mailbox\n", dev->name);
	aha_mbi(dev);
    }
}


static void
aha_req_abort(aha_t *dev, uint32_t CCBPointer)
{
    CCBU CmdBlock;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (char *)&CmdBlock, sizeof(CCB32));

    aha_mbi_setup(dev, CCBPointer, &CmdBlock,
		  0x26, SCSI_STATUS_OK, MBI_NOT_FOUND);
    aha_log("%s: Callback: Send incoming mailbox\n", dev->name);
    aha_mbi(dev);
}


static uint32_t
aha_mbo(aha_t *dev, Mailbox32_t *Mailbox32)
{	
    Mailbox_t MailboxOut;
    uint32_t Outgoing;

    if (dev->Mbx24bit) {
	Outgoing = dev->MailboxOutAddr + (dev->MailboxOutPosCur * sizeof(Mailbox_t));
	DMAPageRead(Outgoing, (char *)&MailboxOut, sizeof(Mailbox_t));

	Mailbox32->CCBPointer = ADDR_TO_U32(MailboxOut.CCBPointer);
	Mailbox32->u.out.ActionCode = MailboxOut.CmdStatus;
    } else {
	Outgoing = dev->MailboxOutAddr + (dev->MailboxOutPosCur * sizeof(Mailbox32_t));

	DMAPageRead(Outgoing, (char *)Mailbox32, sizeof(Mailbox32_t));	
    }

    return(Outgoing);
}


static void
aha_mbo_adv(aha_t *dev)
{
    dev->MailboxOutPosCur = (dev->MailboxOutPosCur + 1) % dev->MailboxCount;
}


static uint8_t
aha_do_mail(aha_t *dev)
{
    Mailbox32_t mb32;
    uint32_t Outgoing;
    uint8_t CmdStatus = MBO_FREE;
    uint32_t CodeOffset = 0;

    CodeOffset = dev->Mbx24bit ? 0 : 7;

#if 0
    if (dev->Interrupt || dev->PendingInterrupt)
    {
	aha_log("%s: Interrupt set, waiting...\n", dev->name);
	return 1;
    }
#endif

    uint8_t MailboxCur = dev->MailboxOutPosCur;

    /* Search for a filled mailbox - stop if we have scanned all mailboxes. */
    do {
	/* Fetch mailbox from guest memory. */
	Outgoing = aha_mbo(dev, &mb32);

	/* Check the next mailbox. */
	aha_mbo_adv(dev);
    } while ((mb32.u.out.ActionCode == MBO_FREE) && (MailboxCur != dev->MailboxOutPosCur));

    if (mb32.u.out.ActionCode != MBO_FREE) {
	/* We got the mailbox, mark it as free in the guest. */
	aha_log("aha_do_mail(): Writing %i bytes at %08X\n", sizeof(CmdStatus), Outgoing + CodeOffset);
		DMAPageWrite(Outgoing + CodeOffset, (char *)&CmdStatus, sizeof(CmdStatus));
    }
    else {
	return 0;
    }

    if (dev->MailboxOutInterrupts) {
	RaiseIntr(dev, 0, INTR_MBOA | INTR_ANY);

	while (dev->Interrupt) {
		thread_wait_event(dev->evt, 10);
	}
    }

    if (mb32.u.out.ActionCode == MBO_START) {
	aha_log("Start Mailbox Command\n");
	aha_req_setup(dev, mb32.CCBPointer, &mb32);
    } else if (mb32.u.out.ActionCode == MBO_ABORT) {
		aha_log("Abort Mailbox Command\n");
		aha_req_abort(dev, mb32.CCBPointer);
    } else {
	aha_log("Invalid action code: %02X\n", mb32.u.out.ActionCode);
    }

    return 1;
}


static void
aha_cmd_thread(void *priv)
{
    aha_t *dev = (aha_t *)priv;

aha_event_restart:
    /* Create a waitable event. */
    dev->evt = thread_create_event();

aha_scan_restart:
    while (aha_do_mail(dev) != 0)
    {
	thread_wait_event(dev->evt, 10);
    }

    if (dev->scan_restart)
    {
	goto aha_scan_restart;
    }

    thread_destroy_event(dev->evt);
    dev->evt = NULL;

    if (dev->scan_restart)
    {
	goto aha_event_restart;
    }

    poll_tid = NULL;

    aha_log("%s: Callback: polling stopped.\n", dev->name);
}


static uint8_t
aha_read(uint16_t port, void *priv)
{
    aha_t *dev = (aha_t *)priv;
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
				aha_cmd_done(dev, 0);
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
#ifndef WALTJE
    aha_log("%s: Read Port 0x%02X, Returned Value %02X\n",
					dev->name, port, ret);
#endif
#endif

    return(ret);
}


static uint16_t
aha_readw(uint16_t port, void *priv)
{
    return(aha_read(port, priv));
}


static void
aha_write(uint16_t port, uint8_t val, void *priv)
{
    ReplyInquireSetupInformation *ReplyISI;
    aha_t *dev = (aha_t *)priv;
    MailboxInit_t *mbi;
    int i = 0;
    uint8_t j = 0;
    BIOSCMD *cmd;
    uint16_t cyl = 0;
    int suppress = 0;

    aha_log("%s: Write Port 0x%02X, Value %02X\n", dev->name, port, val);

    switch (port & 3) {
	case 0:
		if ((val & CTRL_HRST) || (val & CTRL_SRST)) {	
			uint8_t Reset = (val & CTRL_HRST);
			aha_log("Reset completed = %x\n", Reset);
			aha_reset_ctrl(dev, Reset);
			break;
		}
		
		if (val & CTRL_IRST) {
			ClearIntr(dev);
		}
		break;

	case 1:
		/* Fast path for the mailbox execution command. */
		if (((val == CMD_START_SCSI) || (val == CMD_BIOS_SCSI)) &&
		    (dev->Command == 0xff)) {
			/* If there are no mailboxes configured, don't even try to do anything. */
			if (dev->MailboxCount) {
				if (! poll_tid) {
					aha_log("%s: starting thread..\n", dev->name);
					poll_tid = thread_create(aha_cmd_thread, dev);
					dev->scan_restart = 0;
				}
				else {
					dev->scan_restart = 1;
				}
			}
			return;
		}

		if (dev->Command == 0xff) {
			dev->Command = val;
			dev->CmdParam = 0;
			dev->CmdParamLeft = 0;
			
			dev->Status &= ~(STAT_INVCMD | STAT_IDLE);
			aha_log("%s: Operation Code 0x%02X\n", dev->name, val);
			switch (dev->Command) {
				case CMD_MBINIT:
					dev->CmdParamLeft = sizeof(MailboxInit_t);
					break;
				
				case CMD_BIOSCMD:
					dev->CmdParamLeft = 10;
					break;

				case CMD_BIOS_MBINIT:
					/* Same as 0x01 for AHA. */
					dev->CmdParamLeft = sizeof(MailboxInit_t);
					break;

				case CMD_EMBOI:
				case CMD_BUSON_TIME:
				case CMD_BUSOFF_TIME:
				case CMD_DMASPEED:
				case CMD_RETSETUP:
				case CMD_ECHO:
				case CMD_OPTIONS:
				case CMD_SHADOW_RAM:
					dev->CmdParamLeft = 1;
					break;	

				case CMD_SELTIMEOUT:
					dev->CmdParamLeft = 4;
					break;

				case CMD_WRITE_EEPROM:
					dev->CmdParamLeft = 3+32;
					break;

				case CMD_READ_EEPROM:
					dev->CmdParamLeft = 3;
					break;

				case CMD_MBENABLE:
					dev->CmdParamLeft = 2;
					break;
			}
		} else {
			dev->CmdBuf[dev->CmdParam] = val;
			dev->CmdParam++;
			dev->CmdParamLeft--;
		}
		
		if (! dev->CmdParamLeft) {
			aha_log("Running Operation Code 0x%02X\n", dev->Command);
			switch (dev->Command) {
				case CMD_NOP: /* No Operation */
					dev->DataReplyLeft = 0;
					break;

				case CMD_MBINIT: /* mailbox initialization */
aha_0x01:
				{
					dev->Mbx24bit = 1;
							
					mbi = (MailboxInit_t *)dev->CmdBuf;

					dev->MailboxCount = mbi->Count;
					dev->MailboxOutAddr = ADDR_TO_U32(mbi->Address);
					dev->MailboxInAddr = dev->MailboxOutAddr + (dev->MailboxCount * sizeof(Mailbox_t));
						
					aha_log("Initialize Mailbox: MBI=0x%08lx, MBO=0x%08lx, %d entries at 0x%08lx\n",
						dev->MailboxOutAddr,
						dev->MailboxInAddr,
						mbi->Count,
						ADDR_TO_U32(mbi->Address));

					dev->Status &= ~STAT_INIT;
					dev->DataReplyLeft = 0;
				}
				break;

				case CMD_BIOSCMD: /* execute BIOS */
					cmd = (BIOSCMD *)dev->CmdBuf;
					if (dev->type != AHA_1640) {
						/* 1640 uses LBA. */
						cyl = ((cmd->u.chs.cyl & 0xff) << 8) | ((cmd->u.chs.cyl >> 8) & 0xff);
					cmd->u.chs.cyl = cyl;						
					}
					if (dev->type == AHA_1640) {
						/* 1640 uses LBA. */
						aha_log("BIOS LBA=%06lx (%lu)\n",
							lba32_blk(cmd),
							lba32_blk(cmd));
					} else {
						cmd->u.chs.head &= 0xf;
						cmd->u.chs.sec &= 0x1f;
						aha_log("BIOS CHS=%04X/%02X%02X\n",
							cmd->u.chs.cyl,
							cmd->u.chs.head,
							cmd->u.chs.sec);
					}
					dev->DataBuf[0] = scsi_bios_command(7, cmd, (dev->type==AHA_1640)?1:0);
					aha_log("BIOS Completion/Status Code %x\n", dev->DataBuf[0]);
					dev->DataReplyLeft = 1;
					break;

				case CMD_INQUIRY: /* Inquiry */
					dev->DataBuf[0] = dev->bid;
					dev->DataBuf[1] = (dev->type != AHA_1640) ? 0x30 : 0x42;
					dev->DataBuf[2] = dev->fwh;
					dev->DataBuf[3] = dev->fwl;
					dev->DataReplyLeft = 4;
					break;

				case CMD_EMBOI: /* enable MBO Interrupt */
					if (dev->CmdBuf[0] <= 1) {
						dev->MailboxOutInterrupts = dev->CmdBuf[0];
						aha_log("Mailbox out interrupts: %s\n", dev->MailboxOutInterrupts ? "ON" : "OFF");
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
					dev->DataReplyLeft = 0;
					aha_log("Bus-on time: %d\n", dev->CmdBuf[0]);
					break;
						
				case CMD_BUSOFF_TIME: /* bus-off time */
					dev->DataReplyLeft = 0;
					aha_log("Bus-off time: %d\n", dev->CmdBuf[0]);
					break;
						
				case CMD_DMASPEED: /* DMA Transfer Rate */
					dev->DataReplyLeft = 0;
					aha_log("DMA transfer rate: %02X\n", dev->CmdBuf[0]);
					break;

				case CMD_RETDEVS: /* return Installed Devices */
					memset(dev->DataBuf, 0x00, 8);
					for (i=0; i<SCSI_ID_MAX; i++) {
					    dev->DataBuf[i] = 0x00;

					    /* Skip the HA .. */
					    if (i == dev->HostID) continue;

					    for (j=0; j<SCSI_LUN_MAX; j++) {
						if (scsi_device_present(i, j))
						    dev->DataBuf[i] |= (1<<j);
					    }
					}
					dev->DataReplyLeft = i;
					break;				

				case CMD_RETCONF: /* return Configuration */
					dev->DataBuf[0] = (1<<dev->DmaChannel);
					if (dev->Irq >= 8)
					    dev->DataBuf[1]=(1<<(dev->Irq-9));
					else
					    dev->DataBuf[1]=(1<<dev->Irq);
					dev->DataBuf[2] = dev->HostID;
					dev->DataReplyLeft = 3;
					break;

				case CMD_RETSETUP: /* return Setup */
				{
					dev->DataReplyLeft = dev->CmdBuf[0];

					ReplyISI = (ReplyInquireSetupInformation *)dev->DataBuf;
					memset(ReplyISI, 0x00, sizeof(ReplyInquireSetupInformation));
					
					ReplyISI->fSynchronousInitiationEnabled = 1;
					ReplyISI->fParityCheckingEnabled = 1;
					ReplyISI->cMailbox = dev->MailboxCount;
					U32_TO_ADDR(ReplyISI->MailboxAddress, dev->MailboxOutAddr);
					aha_log("Return Setup Information: %d\n", dev->CmdBuf[0]);
				}
				break;
						
				case CMD_ECHO: /* ECHO data */
					dev->DataBuf[0] = dev->CmdBuf[0];
					dev->DataReplyLeft = 1;
					break;

				case CMD_OPTIONS: /* Set adapter options */
					if (dev->CmdParam == 1)
						dev->CmdParamLeft = dev->CmdBuf[0];
					dev->DataReplyLeft = 0;
					break;

				case CMD_WRITE_EEPROM:	/* write EEPROM */
					/* Sent by CF BIOS. */
					dev->DataReplyLeft =
					    aha154x_eeprom(dev,
						   dev->Command,
						   dev->CmdBuf[0],
						   dev->CmdBuf[1],
						   dev->CmdBuf[2],
						   dev->DataBuf);
					if (dev->DataReplyLeft == 0xff) {
						dev->DataReplyLeft = 0;
						dev->Status |= STAT_INVCMD;
					}
					break;

				case CMD_READ_EEPROM: /* read EEPROM */
					/* Sent by CF BIOS. */
					dev->DataReplyLeft =
					    aha154x_eeprom(dev,
						   dev->Command,
						   dev->CmdBuf[0],
						   dev->CmdBuf[1],
						   dev->CmdBuf[2],
						   dev->DataBuf);
					if (dev->DataReplyLeft == 0xff) {
						dev->DataReplyLeft = 0;
						dev->Status |= STAT_INVCMD;
					}
					break;

				case CMD_SHADOW_RAM: /* Shadow RAM */
					/*
					 * For AHA1542CF, this is the command
					 * to play with the Shadow RAM.  BIOS
					 * gives us one argument (00,02,03)
					 * and expects a 0x04 back in the INTR
					 * register.  --FvK
					 */
					dev->Interrupt = aha154x_shram(dev,val);
					break;

				case CMD_BIOS_MBINIT: /* BIOS Mailbox Initialization */
					/* Sent by CF BIOS. */
					goto aha_0x01;

				case CMD_MEMORY_MAP_1:	/* AHA memory mapper */
				case CMD_MEMORY_MAP_2:	/* AHA memory mapper */
					/* Sent by CF BIOS. */
					dev->DataReplyLeft =
					    aha154x_mmap(dev, dev->Command);
					break;

				case CMD_EXTBIOS: /* Return extended BIOS information */
					dev->DataBuf[0] = 0x08;
					dev->DataBuf[1] = dev->Lock;
					dev->DataReplyLeft = 2;
					break;
					
				case CMD_MBENABLE: /* Mailbox interface enable Command */
					dev->DataReplyLeft = 0;
					if (dev->CmdBuf[1] == dev->Lock) {
						if (dev->CmdBuf[0] & 1) {
							dev->Lock = 1;
						} else {
							dev->Lock = 0;
						}
					}
					break;

				case 0x2C:	/* AHA-1542CP sends this */
					dev->DataBuf[0] = 0x00;
					dev->DataReplyLeft = 1;
					break;

				case 0x33:	/* AHA-1542CP sends this */
					dev->DataBuf[0] = 0x00;
					dev->DataBuf[1] = 0x00;
					dev->DataBuf[2] = 0x00;
					dev->DataBuf[3] = 0x00;
					dev->DataReplyLeft = 256;
					break;

				default:
					dev->DataReplyLeft = 0;
					dev->Status |= STAT_INVCMD;
					break;
			}
		}
		
		if (dev->DataReplyLeft)
			dev->Status |= STAT_DFULL;
		else if (!dev->CmdParamLeft)
			aha_cmd_done(dev, suppress);
		break;
		
	case 2:
		break;
		
	case 3:
		break;
    }
}


static void
aha_writew(uint16_t Port, uint16_t Val, void *p)
{
    aha_write(Port, Val & 0xFF, p);
}


static uint8_t
aha_mca_read(int port, void *priv)
{
    aha_t *dev = (aha_t *)priv;

    return(dev->pos_regs[port & 7]);
}


static void
aha_mca_write(int port, uint8_t val, void *priv)
{
    aha_t *dev = (aha_t *)priv;

    /* MCA does not write registers below 0x0100. */
    if (port < 0x0102) return;

    /* Save the MCA register value. */
    dev->pos_regs[port & 7] = val;

    /* Get the new assigned I/O base address. */
    switch(dev->pos_regs[3] & 0xc7) {
	case 0x01:		/* [1]=00xx x001 */
		dev->Base = 0x0130;
		break;

	case 0x02:		/* [1]=00xx x010 */
		dev->Base = 0x0230;
		break;

	case 0x03:		/* [1]=00xx x011 */
		dev->Base = 0x0330;
		break;

	case 0x41:
		dev->Base = 0x0134;
		break;

	case 0x42:		/* [1]=01xx x010 */
		dev->Base = 0x0234;
		break;

	case 0x43:		/* [1]=01xx x011 */
		dev->Base = 0x0334;
		break;
    }

    /* Save the new IRQ and DMA channel values. */
    dev->Irq = (dev->pos_regs[4] & 0x07) + 8;
    dev->DmaChannel = dev->pos_regs[5] & 0x0f;	

    /* Extract the BIOS ROM address info. */
    if (! (dev->pos_regs[2] & 0x80)) switch(dev->pos_regs[3] & 0x38) {
	case 0x38:		/* [1]=xx11 1xxx */
		dev->rom_addr = 0xDC000;
		break;

	case 0x30:		/* [1]=xx11 0xxx */
		dev->rom_addr = 0xD8000;
		break;

	case 0x28:		/* [1]=xx10 1xxx */
		dev->rom_addr = 0xD4000;
		break;

	case 0x20:		/* [1]=xx10 0xxx */
		dev->rom_addr = 0xD0000;
		break;

	case 0x18:		/* [1]=xx01 1xxx */
		dev->rom_addr = 0xCC000;
		break;

	case 0x10:		/* [1]=xx01 0xxx */
		dev->rom_addr = 0xC8000;
		break;
    } else {
	/* Disabled. */
	dev->rom_addr = 0x000000;
    }

    /*
     * Get misc SCSI config stuff.  For now, we are only
     * interested in the configured HA target ID:
     *
     *  pos[2]=111xxxxx = 7
     *  pos[2]=000xxxxx = 0
     */
    dev->HostID = (dev->pos_regs[4] >> 5) & 0x07;

    /*
     * SYNC mode is pos[2]=xxxx1xxx.
     *
     * SCSI Parity is pos[2]=xxx1xxxx.
     */

    /*
     * The PS/2 Model 80 BIOS always enables a card if it finds one,
     * even if no resources were assigned yet (because we only added
     * the card, but have not run AutoConfig yet...)
     *
     * So, remove current address, if any.
     */
    io_removehandler(dev->Base, 4,
		     aha_read, aha_readw, NULL,
		     aha_write, aha_writew, NULL, dev);		
    mem_mapping_disable(&dev->bios.mapping);

    /* Initialize the device if fully configured. */
    if (dev->pos_regs[2] & 0x01) {
	/* Card enabled; register (new) I/O handler. */
	io_sethandler(dev->Base, 4,
		      aha_read, aha_readw, NULL,
		      aha_write, aha_writew, NULL, dev);

	/* Reset the device. */
	aha_reset_ctrl(dev, CTRL_HRST);

	/* Enable or disable the BIOS ROM. */
	if (dev->rom_addr != 0x000000) {
		mem_mapping_enable(&dev->bios.mapping);
		mem_mapping_set_addr(&dev->bios.mapping, dev->rom_addr, ROM_SIZE);
	}
    }
}


/* Initialize the board's ROM BIOS. */
static void
aha_setbios(aha_t *dev)
{
    uint32_t size;
    uint32_t mask;
    uint32_t temp;
    FILE *f;
    int i;

    /* Only if this device has a BIOS ROM. */
    if (dev->bios_path == NULL) return;

    /* Open the BIOS image file and make sure it exists. */
    pclog_w(L"%S: loading BIOS from '%s'\n", dev->name, dev->bios_path);
    if ((f = romfopen(dev->bios_path, L"rb")) == NULL) {
	pclog("%s: BIOS ROM not found!\n", dev->name);
	return;
    }

    /*
     * Manually load and process the ROM image.
     *
     * We *could* use the system "rom_init" function here, but for
     * this special case, we can't: we may need WRITE access to the
     * memory later on.
     */
    (void)fseek(f, 0L, SEEK_END);
    temp = ftell(f);
    (void)fseek(f, 0L, SEEK_SET);

    /* Load first chunk of BIOS (which is the main BIOS, aka ROM1.) */
    dev->rom1 = malloc(ROM_SIZE);
    (void)fread(dev->rom1, ROM_SIZE, 1, f);
    temp -= ROM_SIZE;
    if (temp > 0) {
	dev->rom2 = malloc(ROM_SIZE);
	(void)fread(dev->rom2, ROM_SIZE, 1, f);
	temp -= ROM_SIZE;
    } else {
	dev->rom2 = NULL;
    }
    if (temp != 0) {
	pclog("%s: BIOS ROM size invalid!\n", dev->name);
	free(dev->rom1);
	if (dev->rom2 != NULL)
		free(dev->rom2);
	(void)fclose(f);
	return;
    }
    temp = ftell(f);
    if (temp > ROM_SIZE)
	temp = ROM_SIZE;
    (void)fclose(f);

    /* Adjust BIOS size in chunks of 2K, as per BIOS spec. */
    size = 0x10000;
    if (temp <= 0x8000)
	size = 0x8000;
    if (temp <= 0x4000)
	size = 0x4000;
    if (temp <= 0x2000)
	size = 0x2000;
    mask = (size - 1);
    pclog("%s: BIOS at 0x%06lX, size %lu, mask %08lx\n",
			dev->name, dev->rom_addr, size, mask);

    /* Initialize the ROM entry for this BIOS. */
    memset(&dev->bios, 0x00, sizeof(rom_t));

    /* Enable ROM1 into the memory map. */
    dev->bios.rom = dev->rom1;

    /* Set up an address mask for this memory. */
    dev->bios.mask = mask;

    /* Map this system into the memory map. */
    mem_mapping_add(&dev->bios.mapping, dev->rom_addr, size,
		    aha_mem_read, aha_mem_readw, aha_mem_readl,
		    aha_mem_write, NULL, NULL,
		    dev->bios.rom, MEM_MAPPING_EXTERNAL, &dev->bios);
    mem_mapping_disable(&dev->bios.mapping);

    /*
     * Patch the ROM BIOS image for stuff Adaptec deliberately
     * made hard to understand. Well, maybe not, maybe it was
     * their way of handling issues like these at the time..
     *
     * Patch 1: emulate the I/O ADDR SW setting by patching a
     *	    byte in the BIOS that indicates the I/O ADDR
     *	    switch setting on the board.
     */
    if (dev->rom_ioaddr != 0x0000) {
	/* Look up the I/O address in the table. */
	for (i=0; i<8; i++)
		if (aha_ports[i] == dev->Base) break;
	if (i == 8) {
		pclog("%s: invalid I/O address %04x selected!\n",
					dev->name, dev->Base);
		return;
	}
	dev->bios.rom[dev->rom_ioaddr] = (uint8_t)i;
    }

    /*
     * The more recent BIOS images have their version ID
     * encoded in the image, and we can use that info to
     * report it back.
     *
     * Start out with a fake BIOS firmware version.
     */
    dev->fwh = '1';
    dev->fwl = '0';
    if (dev->rom_fwhigh != 0x0000) {
	/* Read firmware version from the BIOS. */
	dev->fwh = dev->bios.rom[dev->rom_fwhigh];
	dev->fwl = dev->bios.rom[dev->rom_fwhigh+1];
    }

    /*
     * Do a checksum on the ROM.
     *
     * The BIOS ROMs on the 154xC(F) boards will always fail
     * the checksum, because they are incomplete: on the real
     * boards, a shadow RAM and some other (config) registers
     * are mapped into its space.  It is assumed that boards
     * have logic that automatically generate a "fixup" byte
     * at the end of the data to 'make up' for this.
     *
     * We emulated some of those in the patch routine, so now
     * it is time to "fix up" the BIOS image so that the main
     * (system) BIOS considers it valid.
     */
again:
    mask = 0;
    for (temp=0; temp<ROM_SIZE; temp++)
	mask += dev->bios.rom[temp];
    mask &= 0xff;
    if (mask != 0x00) {
	dev->bios.rom[temp-1] += (256 - mask);
	goto again;
    }
}


/* Initialize the board's EEPROM (NVR.) */
static void
aha_setnvr(aha_t *dev)
{
    /* Only if this device has an EEPROM. */
    if (dev->nvr_path == NULL) return;

    /* Allocate and initialize the EEPROM. */
    dev->nvr = (uint8_t *)malloc(NVR_SIZE);
    memset(dev->nvr, 0x00, NVR_SIZE);

    /* Initialize the on-board EEPROM. */
    dev->nvr[0] = dev->HostID;			/* SCSI ID 7 */
    dev->nvr[0] |= (0x10 | 0x20 | 0x40);
    dev->nvr[1] = dev->Irq-9;			/* IRQ15 */
    dev->nvr[1] |= (dev->DmaChannel<<4);	/* DMA6 */
    dev->nvr[2] = (EE2_HABIOS	| 		/* BIOS enabled		*/
		   EE2_DYNSCAN	|		/* scan bus		*/
		   EE2_EXT1G | EE2_RMVOK);	/* Imm return on seek	*/
    dev->nvr[3] = SPEED_50;			/* speed 5.0 MB/s	*/
    dev->nvr[6] = (EE6_TERM	|		/* host term enable	*/
		   EE6_RSTBUS);			/* reset SCSI bus on boot*/
}


/* General initialization routine for all boards. */
static void *
aha_init(int type)
{
    aha_t *dev;

    /* Allocate control block and set up basic stuff. */
    dev = malloc(sizeof(aha_t));
    if (dev == NULL) return(dev);
    memset(dev, 0x00, sizeof(aha_t));
    dev->type = type;

    /*
     * Set up the (initial) I/O address, IRQ and DMA info.
     *
     * Note that on MCA, configuration is handled by the BIOS,
     * and so any info we get here will be overwritten by the
     * MCA-assigned values later on!
     */
    dev->Base = device_get_config_hex16("base");
    dev->Irq = device_get_config_int("irq");
    dev->DmaChannel = device_get_config_int("dma");
    dev->rom_addr = device_get_config_hex20("bios_addr");
#if NOT_YET_USED
    dev->HostID = device_get_config_int("hostid");
#else
    dev->HostID = 7;		/* default HA ID */
#endif

    /* Perform per-board initialization. */
    switch(type) {
	case AHA_154xB:
		strcpy(dev->name, "AHA-154xB");
		switch(dev->Base) {
			case 0x0330:
				dev->bios_path =
				    L"roms/scsi/adaptec/aha1540b320_330.bin";
				break;

			case 0x0334:
				dev->bios_path =
				    L"roms/scsi/adaptec/aha1540b320_334.bin";
				break;
		}
		dev->bid = 'A';
		break;

	case AHA_154xC:
		strcpy(dev->name, "AHA-154xC");
		dev->bios_path = L"roms/scsi/adaptec/aha1542c101.bin";
		dev->bid = 'D';
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->rom_ioaddr = 0x3F7E;	/* [2:0] idx into addr table */
		dev->rom_fwhigh = 0x0022;	/* firmware version (hi/lo) */
		break;

	case AHA_154xCF:
		strcpy(dev->name, "AHA-154xCF");
		dev->bios_path = L"roms/scsi/adaptec/aha1542cf211.bin";
		dev->nvr_path = L"aha1540cf.nvr";
		dev->bid = 'E';
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->rom_ioaddr = 0x3F7E;	/* [2:0] idx into addr table */
		dev->rom_fwhigh = 0x0022;	/* firmware version (hi/lo) */
		break;

	case AHA_154xCP:
		strcpy(dev->name, "AHA-154xCP");
		dev->bios_path = L"roms/scsi/adaptec/aha1542cp102.bin";
		dev->nvr_path = L"aha1540cp.nvr";
		dev->bid = 'F';
		dev->rom_shram = 0x3F80;	/* shadow RAM address base */
		dev->rom_shramsz = 128;		/* size of shadow RAM */
		dev->rom_ioaddr = 0x3F7E;	/* [2:0] idx into addr table */
		dev->rom_fwhigh = 0x0055;	/* firmware version (hi/lo) */
		break;

	case AHA_1640:
		strcpy(dev->name, "AHA-1640");
		dev->bios_path = L"roms/scsi/adaptec/aha1640.bin";
		dev->bid = 'B';

		/* Enable MCA. */
		dev->pos_regs[0] = 0x1F;	/* MCA board ID */
		dev->pos_regs[1] = 0x0F;	
		mca_add(aha_mca_read, aha_mca_write, dev);
		break;
    }	

    /* Initialize ROM BIOS if needed. */
    aha_setbios(dev);

    /* Initialize EEPROM (NVR) if needed. */
    aha_setnvr(dev);

    timer_add(aha_reset_poll, &dev->ResetCB, &dev->ResetCB, dev);

    if (dev->Base != 0) {
	/* Register our address space. */
	io_sethandler(dev->Base, 4,
		      aha_read, aha_readw, NULL,
		      aha_write, aha_writew, NULL, dev);

	/* Initialize the device. */
	aha_reset_ctrl(dev, CTRL_HRST);

	/* Enable the memory. */
	if (dev->rom_addr != 0x000000) {
		mem_mapping_enable(&dev->bios.mapping);
		mem_mapping_set_addr(&dev->bios.mapping, dev->rom_addr, ROM_SIZE);
	}
    }

    return(dev);
}


static void *
aha_154xB_init(void)
{
    return(aha_init(AHA_154xB));
}


static void *
aha_154xCF_init(void)
{
    return(aha_init(AHA_154xCF));
}


static void *
aha_1640_init(void)
{
    return(aha_init(AHA_1640));
}


static void
aha_close(void *priv)
{
    aha_t *dev = (aha_t *)priv;

    if (dev)
    {
	if (dev->evt) {
		thread_destroy_event(dev->evt);
		dev->evt = NULL;
		if (poll_tid) {
			poll_tid = NULL;
		}
	}

	if (dev->nvr != NULL)
		free(dev->nvr);

	free(dev);
	dev = NULL;
    }
}


void
aha_device_reset(void *priv)
{
    aha_t *dev = (aha_t *)priv;

    aha_reset_ctrl(dev, 1);
}


static device_config_t aha_154x_config[] = {
        {
		"base", "Address", CONFIG_HEX16, "", 0x334,
                {
                        {
                                "None",      0
                        },
                        {
                                "0x330", 0x330
                        },
                        {
                                "0x334", 0x334
                        },
                        {
                                "0x230", 0x230
                        },
                        {
                                "0x234", 0x234
                        },
                        {
                                "0x130", 0x130
                        },
                        {
                                "0x134", 0x134
                        },
                        {
                                ""
                        }
                },
        },
        {
		"irq", "IRQ", CONFIG_SELECTION, "", 9,
                {
                        {
                                "IRQ 9", 9
                        },
                        {
                                "IRQ 10", 10
                        },
                        {
                                "IRQ 11", 11
                        },
                        {
                                "IRQ 12", 12
                        },
                        {
                                "IRQ 14", 14
                        },
                        {
                                "IRQ 15", 15
                        },
                        {
                                ""
                        }
                },
        },
        {
		"dma", "DMA channel", CONFIG_SELECTION, "", 6,
                {
                        {
                                "DMA 5", 5
                        },
                        {
                                "DMA 6", 6
                        },
                        {
                                "DMA 7", 7
                        },
                        {
                                ""
                        }
                },
        },
        {
                "bios_addr", "BIOS Address", CONFIG_HEX20, "", 0,
                {
                        {
                                "Disabled", 0
                        },
                        {
                                "C800H", 0xc8000
                        },
                        {
                                "D000H", 0xd0000
                        },
                        {
                                "D800H", 0xd8000
                        },
                        {
                                ""
                        }
                },
        },
	{
		"", "", -1
	}
};


device_t aha1540b_device = {
    "Adaptec AHA-1540B",
    0,
    aha_154xB_init,
    aha_close,
    NULL,
    NULL,
    NULL,
    NULL,
    aha_154x_config
};

device_t aha1542cf_device = {
    "Adaptec AHA-1542CF",
    0,
    aha_154xCF_init,
    aha_close,
    NULL,
    NULL,
    NULL,
    NULL,
    aha_154x_config
};

device_t aha1640_device = {
    "Adaptec AHA-1640",
    DEVICE_MCA,
    aha_1640_init,
    aha_close,
    NULL,
    NULL,
    NULL,
    NULL
};
