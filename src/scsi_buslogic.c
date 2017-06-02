/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of BusLogic BT-542B ISA and BT-958D PCI SCSI
 *		controllers.
 *
 * Version:	@(#)scsi_buslogic.c	1.0.0	2017/05/30
 *
 * Author:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2008-2017 Sarah Walker.
 *		Copyright 2016-2017 Miran Grca.
 *		Copyright 2017-2017 Fred N. van Kempen.
 */

/*Buslogic SCSI emulation*/

/* Emulated SCSI controllers:
	0 - BusLogic BT-542B ISA;
	1 - BusLogic BT-958 PCI (but BT-542B ISA on non-PCI machines). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ibm.h"
#include "io.h"
#include "mem.h"
#include "rom.h"
#include "dma.h"
#include "pic.h"
#include "pci.h"
#include "timer.h"
#include "device.h"
#include "scsi.h"
#include "scsi_disk.h"
#include "cdrom.h"
#include "scsi_buslogic.h"


#define BUSLOGIC_RESET_DURATION_NS UINT64_C(50000000)


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
#define CMD_BIOS	0x03		/* Execute ROM BIOS command */
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

/* READ INTERRUPT STATUS. */
#define INTR_ANY	0x80		/* any interrupt */
#define INTR_SRCD	0x08		/* SCSI reset detected */
#define INTR_HACC	0x04		/* HA command complete */
#define INTR_MBOA	0x02		/* MBO empty */
#define INTR_MBIF	0x01		/* MBI full */


/*
 * Auto SCSI structure which is located
 * in host adapter RAM and contains several
 * configuration parameters.
 */
#pragma pack(push,1)
typedef struct {
    uint8_t	aInternalSignature[2];
    uint8_t	cbInformation;
    uint8_t	aHostAdaptertype[6];
    uint8_t	uReserved1;
    uint8_t	fFloppyEnabled		:1,
		fFloppySecondary	:1,
		fLevelSensitiveInterrupt:1,
		uReserved2		:2,
		uSystemRAMAreForBIOS	:3;
    uint8_t	uDMAChannel		:7,
		fDMAAutoConfiguration	:1,
		uIrqChannel		:7,
		fIrqAutoConfiguration	:1;
    uint8_t	uDMATransferRate;
    uint8_t	uSCSIId;
    uint8_t	fLowByteTerminated	:1,
		fParityCheckingEnabled	:1,
		fHighByteTerminated	:1,
		fNoisyCablingEnvironment:1,
		fFastSyncNegotiation	:1,
		fBusResetEnabled	:1,
		fReserved3		:1,
		fActiveNegotiationEna	:1;
    uint8_t	uBusOnDelay;
    uint8_t	uBusOffDelay;
    uint8_t	fHostAdapterBIOSEnabled	:1,
		fBIOSRedirectionOfInt19 :1,
		fExtendedTranslation	:1,
		fMapRemovableAsFixed	:1,
		fReserved4		:1,
		fBIOSMoreThan2Drives	:1,
		fBIOSInterruptMode	:1,
		fFlopticalSupport	:1;
    uint16_t	u16DeviceEnabledMask;
    uint16_t	u16WidePermittedMask;
    uint16_t	u16FastPermittedMask;
    uint16_t	u16SynchronousPermittedMask;
    uint16_t	u16DisconnectPermittedMask;
    uint16_t	u16SendStartUnitCommandMask;
    uint16_t	u16IgnoreInBIOSScanMask;
    unsigned char uPCIInterruptPin :                2;
    unsigned char uHostAdapterIoPortAddress :       2;
    uint8_t          fStrictRoundRobinMode :           1;
    uint8_t          fVesaBusSpeedGreaterThan33MHz :   1;
    uint8_t          fVesaBurstWrite :                 1;
    uint8_t          fVesaBurstRead :                  1;
    uint16_t      u16UltraPermittedMask;
    uint32_t      uReserved5;
    uint8_t       uReserved6;
    uint8_t       uAutoSCSIMaximumLUN;
    uint8_t          fReserved7 :                      1;
    uint8_t          fSCAMDominant :                   1;
    uint8_t          fSCAMenabled :                    1;
    uint8_t          fSCAMLevel2 :                     1;
    unsigned char uReserved8 :                      4;
    uint8_t          fInt13Extension :                 1;
    uint8_t          fReserved9 :                      1;
    uint8_t          fCDROMBoot :                      1;
    unsigned char uReserved10 :                     5;
    unsigned char uBootTargetId :                   4;
    unsigned char uBootChannel :                    4;
    uint8_t          fForceBusDeviceScanningOrder :    1;
    unsigned char uReserved11 :                     7;
    uint16_t      u16NonTaggedToAlternateLunPermittedMask;
    uint16_t      u16RenegotiateSyncAfterCheckConditionMask;
    uint8_t       aReserved12[10];
    uint8_t       aManufacturingDiagnostic[2];
    uint16_t      u16Checksum;
} AutoSCSIRam;
#pragma pack(pop)

/* The local RAM. */
#pragma pack(push,1)
typedef union {
    uint8_t u8View[256];		/* byte view */
    struct {				/* structured view */
        uint8_t     u8Bios[64];		/* offset 0 - 63 is for BIOS */
        AutoSCSIRam autoSCSIData;	/* Auto SCSI structure */
    } structured;
} HALocalRAM;
#pragma pack(pop)

/** Structure for the INQUIRE_SETUP_INFORMATION reply. */
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

/* Structure for the INQUIRE_EXTENDED_SETUP_INFORMATION. */
#pragma pack(push,1)
typedef struct {
    uint8_t	uBusType;
    uint8_t	uBiosAddress;
    uint16_t	u16ScatterGatherLimit;
    uint8_t	cMailbox;
    uint32_t	uMailboxAddressBase;
    uint8_t	uReserved1		:2,
		fFastEISA		:1,
		uReserved2		:3,
		fLevelSensitiveInterrupt:1,
		uReserved3		:1;
    uint8_t	aFirmwareRevision[3];
    uint8_t	fHostWideSCSI		:1,
		fHostDifferentialSCSI	:1,
		fHostSupportsSCAM	:1,
		fHostUltraSCSI		:1,
		fHostSmartTermination	:1,
		uReserved4		:3;
} ReplyInquireExtendedSetupInformation;
#pragma pack(pop)

/* Structure for the INQUIRE_PCI_HOST_ADAPTER_INFORMATION reply. */
#pragma pack(push,1)
typedef struct {
    uint8_t	IsaIOPort;
    uint8_t	IRQ;
    uint8_t	LowByteTerminated	:1,
		HighByteTerminated	:1,
		uReserved		:2,	/* Reserved. */
		JP1			:1,	/* Whatever that means. */
		JP2			:1,	/* Whatever that means. */
		JP3			:1,	/* Whatever that means. */
		InformationIsValid	:1;
    uint8_t	uReserved2;			/* Reserved. */
} BuslogicPCIInformation_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	Count;
    addr24	Address;
} MailboxInit_t;
#pragma pack(pop)

#pragma pack(push,1)
typedef struct {
    uint8_t	Count;
    uint32_t	Address;
} MailboxInitExtended_t;
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
    rom_t	bios;
    int		UseLocalRAM;
    int		StrictRoundRobinMode;
    int		ExtendedLUNCCBFormat;
    HALocalRAM	LocalRAM;
    Req_t Req;
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
    int		Base;
    int		PCIBase;
    int		MMIOBase;
    int		Irq;
    int		DmaChannel;
    int		IrqEnabled;
    int		Mbx24bit;
    int		MailboxOutInterrupts;
    int		MbiActive[256];
    int		PendingInterrupt;
    int		Lock;
    mem_mapping_t mmio_mapping;
    int		chip;
    int		Card;
} Buslogic_t;
#pragma pack(pop)


static int	BuslogicResetCallback = 0;
static int	BuslogicCallback = 0;
static int	BuslogicInOperation = 0;
static Buslogic_t *BuslogicResetDevice;


enum {
    CHIP_BUSLOGIC_ISA,
    CHIP_BUSLOGIC_PCI
};


#ifdef WALTJE
int buslogic_do_log = 1;
# define ENABLE_BUSLOGIC_LOG
#else
int buslogic_do_log = 0;
#endif


static void
BuslogicLog(const char *format, ...)
{
#ifdef ENABLE_BUSLOGIC_LOG
    va_list ap;

    if (buslogic_do_log) {
	va_start(ap, format);
	vprintf(format, ap);
	va_end(ap);
	fflush(stdout);
    }
#endif
}
/* #define pclog	BuslogicLog */


static void
BuslogicInterrupt(Buslogic_t *bl, int set)
{
#if 0
	if (bl->chip != CHIP_BUSLOGIC_PCI)
	{
#endif
		if (set)
		{
			picint(1 << bl->Irq);
		}
		else
		{
			picintc(1 << bl->Irq);
		}
#if 0
	}
	else
	{
	        if (set)
		{
        	        pci_set_irq(bl->Card, PCI_INTD);
		}
	        else
		{
        	        pci_clear_irq(bl->Card, PCI_INTD);
		}
	}
#endif
}


static void
BuslogicClearInterrupt(Buslogic_t *bl)
{
    pclog("Buslogic: Lowering Interrupt 0x%02X\n", bl->Interrupt);
    bl->Interrupt = 0;
    pclog("Lowering IRQ %i\n", bl->Irq);
    BuslogicInterrupt(bl, 0);
    if (bl->PendingInterrupt) {
	bl->Interrupt = bl->PendingInterrupt;
	pclog("Buslogic: Raising Interrupt 0x%02X (Pending)\n", bl->Interrupt);
	if (bl->MailboxOutInterrupts || !(bl->Interrupt & INTR_MBOA)) {
		if (bl->IrqEnabled)  BuslogicInterrupt(bl, 1);
	}
	bl->PendingInterrupt = 0;
    }
}


static void
BuslogicLocalRAM(Buslogic_t *bl)
{
    /*
     * These values are mostly from what I think is right
     * looking at the dmesg output from a Linux guest inside
     * a VMware server VM.
     *
     * So they don't have to be right :)
     */
    memset(bl->LocalRAM.u8View, 0, sizeof(HALocalRAM));
    bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt = 1;
    bl->LocalRAM.structured.autoSCSIData.fParityCheckingEnabled = 1;
    bl->LocalRAM.structured.autoSCSIData.fExtendedTranslation = 1; /* Same as in geometry register. */
    bl->LocalRAM.structured.autoSCSIData.u16DeviceEnabledMask = UINT16_MAX; /* All enabled. Maybe mask out non present devices? */
    bl->LocalRAM.structured.autoSCSIData.u16WidePermittedMask = UINT16_MAX;
    bl->LocalRAM.structured.autoSCSIData.u16FastPermittedMask = UINT16_MAX;
    bl->LocalRAM.structured.autoSCSIData.u16SynchronousPermittedMask = UINT16_MAX;
    bl->LocalRAM.structured.autoSCSIData.u16DisconnectPermittedMask = UINT16_MAX;
    bl->LocalRAM.structured.autoSCSIData.fStrictRoundRobinMode = bl->StrictRoundRobinMode;
    bl->LocalRAM.structured.autoSCSIData.u16UltraPermittedMask = UINT16_MAX;
    /** @todo calculate checksum? */
}


static void
BuslogicReset(Buslogic_t *bl)
{
    BuslogicCallback = 0;
    BuslogicResetCallback = 0;
    bl->Status = STAT_IDLE | STAT_INIT;
    bl->Geometry = 0x80;
    bl->Command = 0xFF;
    bl->CmdParam = 0;
    bl->CmdParamLeft = 0;
    bl->IrqEnabled = 1;
    bl->StrictRoundRobinMode = 0;
    bl->ExtendedLUNCCBFormat = 0;
    bl->MailboxOutPosCur = 0;
    bl->MailboxInPosCur = 0;
    bl->MailboxOutInterrupts = 0;
    bl->PendingInterrupt = 0;
    bl->Lock = 0;
    BuslogicInOperation = 0;

    BuslogicClearInterrupt(bl);

    BuslogicLocalRAM(bl);
}


static void
BuslogicSoftReset(void)
{
    if (BuslogicResetDevice != NULL) {
	BuslogicReset(BuslogicResetDevice);
    }
}


static void
BuslogicResetControl(Buslogic_t *bl, uint8_t Reset)
{
    BuslogicReset(bl);
    if (Reset) {
	bl->Status |= STAT_STST;
	bl->Status &= ~STAT_IDLE;
    }
    BuslogicResetCallback = BUSLOGIC_RESET_DURATION_NS * TIMER_USEC;
}


static void
BuslogicCommandComplete(Buslogic_t *bl)
{
    bl->DataReply = 0;
    bl->Status |= STAT_IDLE;
				
    if (bl->Command != 0x02) {
	bl->Status &= ~STAT_DFULL;
	bl->Interrupt = (INTR_ANY | INTR_HACC);
	pclog("Raising IRQ %i\n", bl->Irq);
	if (bl->IrqEnabled)
		BuslogicInterrupt(bl, 1);
    }

    bl->Command = 0xFF;
    bl->CmdParam = 0;
}


static void
BuslogicRaiseInterrupt(Buslogic_t *bl, uint8_t Interrupt)
{
    if (bl->Interrupt & INTR_HACC) {
	pclog("Pending IRQ\n");
	bl->PendingInterrupt = Interrupt;
    } else {
	bl->Interrupt = Interrupt;
	pclog("Raising IRQ %i\n", bl->Irq);
	if (bl->IrqEnabled)
		BuslogicInterrupt(bl, 1);
    }
}


static void
BuslogicMailboxInSetup(Buslogic_t *bl, uint32_t CCBPointer, CCBU *CmdBlock,
		       uint8_t HostStatus, uint8_t TargetStatus,
		       uint8_t MailboxCompletionCode)
{
    Req_t *req = &bl->Req;

    req->CCBPointer = CCBPointer;
    memcpy(&(req->CmdBlock), CmdBlock, sizeof(CCB32));
    req->Is24bit = bl->Mbx24bit;
    req->HostStatus = HostStatus;
    req->TargetStatus = TargetStatus;
    req->MailboxCompletionCode = MailboxCompletionCode;

    pclog("Mailbox in setup\n");

    BuslogicInOperation = 2;
}


static void
BuslogicMailboxIn(Buslogic_t *bl)
{	
    Req_t *req = &bl->Req;
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

    Incoming = bl->MailboxInAddr + (bl->MailboxInPosCur * (bl->Mbx24bit ? sizeof(Mailbox_t) : sizeof(Mailbox32_t)));

    if (MailboxCompletionCode != MBI_NOT_FOUND) {
	CmdBlock->common.HostStatus = HostStatus;
	CmdBlock->common.TargetStatus = TargetStatus;		
		
	/* Rewrite the CCB up to the CDB. */
	pclog("CCB rewritten to the CDB (pointer %08X, length %i)\n", CCBPointer, offsetof(CCBC, Cdb));
	DMAPageWrite(CCBPointer, (char *)CmdBlock, offsetof(CCBC, Cdb));
    } else {
	pclog("Mailbox not found!\n");
    }

    pclog("Host Status 0x%02X, Target Status 0x%02X\n",
			HostStatus, TargetStatus);	
	
    if (bl->Mbx24bit) {
	MailboxIn.CmdStatus = Mailbox32.u.in.CompletionCode;
	U32_TO_ADDR(MailboxIn.CCBPointer, Mailbox32.CCBPointer);
	pclog("Mailbox 24-bit: Status=0x%02X, CCB at 0x%04X\n", MailboxIn.CmdStatus, ADDR_TO_U32(MailboxIn.CCBPointer));

	DMAPageWrite(Incoming, (char *)&MailboxIn, sizeof(Mailbox_t));
	pclog("%i bytes of 24-bit mailbox written to: %08X\n", sizeof(Mailbox_t), Incoming);
    } else {
	pclog("Mailbox 32-bit: Status=0x%02X, CCB at 0x%04X\n", Mailbox32.u.in.CompletionCode, Mailbox32.CCBPointer);

	DMAPageWrite(Incoming, (char *)&Mailbox32, sizeof(Mailbox32_t));		
	pclog("%i bytes of 32-bit mailbox written to: %08X\n", sizeof(Mailbox32_t), Incoming);
    }

    bl->MailboxInPosCur++;
    if (bl->MailboxInPosCur >= bl->MailboxCount)
		bl->MailboxInPosCur = 0;

    BuslogicRaiseInterrupt(bl, INTR_MBIF | INTR_ANY);

    BuslogicInOperation = 0;
}


static void
BuslogicReadSGEntries(int Is24bit, uint32_t SGList, uint32_t Entries, SGE32 *SG)
{
    uint32_t i;
    SGE SGE24[MAX_SG_DESCRIPTORS];

    if (Is24bit) {
	DMAPageRead(SGList, (char *)&SGE24, Entries * sizeof(SGE));

	for (i=0;i<Entries;++i) {
		/* Convert the 24-bit entries into 32-bit entries. */
		SG[i].Segment = ADDR_TO_U32(SGE24[i].Segment);
		SG[i].SegmentPointer = ADDR_TO_U32(SGE24[i].SegmentPointer);
	}
    } else {
	DMAPageRead(SGList, (char *)SG, Entries * sizeof(SGE32));		
    }
}


static void
BuslogicDataBufferAllocate(Req_t *req, int Is24bit)
{
    uint32_t sg_buffer_pos = 0;
    uint32_t DataPointer, DataLength;
    uint32_t SGEntryLength = (Is24bit ? sizeof(SGE) : sizeof(SGE32));

    if (Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;		
    }
    pclog("Data Buffer write: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);	

    if (SCSIDevices[req->TargetID][req->LUN].CmdBuffer != NULL)
    {
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

			BuslogicReadSGEntries(Is24bit, SGAddrCurrent, SGRead, SGBuffer);

			for (ScatterEntry = 0; ScatterEntry < SGRead; ScatterEntry++) {
				uint32_t Address;

				pclog("BusLogic S/G Write: ScatterEntry=%u\n", ScatterEntry);

				Address = SGBuffer[ScatterEntry].SegmentPointer;
				DataToTransfer += SGBuffer[ScatterEntry].Segment;

				pclog("BusLogic S/G Write: Address=%08X DatatoTransfer=%u\n", Address, DataToTransfer);
			}

			SGAddrCurrent += SGRead * SGEntryLength;
		} while (SGLeft > 0);

		pclog("Data to transfer (S/G) %d\n", DataToTransfer);

		SCSIDevices[req->TargetID][req->LUN].InitLength = DataToTransfer;

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

				BuslogicReadSGEntries(Is24bit, SGAddrCurrent,
						      SGRead, SGBuffer);

				for (ScatterEntry = 0; ScatterEntry < SGRead; ScatterEntry++) {
					uint32_t Address;

					pclog("BusLogic S/G Write: ScatterEntry=%u\n", ScatterEntry);

					Address = SGBuffer[ScatterEntry].SegmentPointer;
					DataToTransfer = SGBuffer[ScatterEntry].Segment;

					pclog("BusLogic S/G Write: Address=%08X DatatoTransfer=%u\n", Address, DataToTransfer);

					DMAPageRead(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer + sg_buffer_pos, DataToTransfer);
					sg_buffer_pos += DataToTransfer;
				}

				SGAddrCurrent += SGRead * (Is24bit ? sizeof(SGE) : sizeof(SGE32));
			} while (SGLeft > 0);				
		}
	} else if (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
		   req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) {
			uint32_t Address = DataPointer;

			SCSIDevices[req->TargetID][req->LUN].InitLength = DataLength;

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
BuslogicDataBufferFree(Req_t *req)
{
    uint32_t DataPointer = 0;
    uint32_t DataLength = 0;
    uint32_t sg_buffer_pos = 0;
    uint32_t SGRead;
    uint32_t ScatterEntry;
    SGE32 SGBuffer[MAX_SG_DESCRIPTORS];
    uint32_t SGEntrySize;
    uint32_t SGLeft;
    uint32_t SGAddrCurrent;
    uint32_t Address;
    uint32_t Residual;

    if (req->Is24bit) {
	DataPointer = ADDR_TO_U32(req->CmdBlock.old.DataPointer);
	DataLength = ADDR_TO_U32(req->CmdBlock.old.DataLength);
    } else {
	DataPointer = req->CmdBlock.new.DataPointer;
	DataLength = req->CmdBlock.new.DataLength;		
    }

    if ((DataLength != 0) && (req->CmdBlock.common.Cdb[0] == GPCMD_TEST_UNIT_READY)) {
	pclog("Data length not 0 with TEST UNIT READY: %i (%i)\n",
		DataLength, SCSIDevices[req->TargetID][req->LUN].InitLength);
    }

    if (req->CmdBlock.common.Cdb[0] == GPCMD_TEST_UNIT_READY) {
	DataLength = 0;
    }

    pclog("Data Buffer read: length %d, pointer 0x%04X\n",
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

			BuslogicReadSGEntries(req->Is24bit, SGAddrCurrent,
					      SGRead, SGBuffer);

			for (ScatterEntry = 0; ScatterEntry < SGRead; ScatterEntry++) {
				uint32_t Address;
				uint32_t DataToTransfer;

				pclog("BusLogic S/G: ScatterEntry=%u\n", ScatterEntry);

				Address = SGBuffer[ScatterEntry].SegmentPointer;
				DataToTransfer = SGBuffer[ScatterEntry].Segment;

				pclog("BusLogic S/G: Writing %i bytes at %08X\n", DataToTransfer, Address);

				DMAPageWrite(Address, (char *)SCSIDevices[req->TargetID][req->LUN].CmdBuffer + sg_buffer_pos, DataToTransfer);
				sg_buffer_pos += DataToTransfer;
			}

			SGAddrCurrent += (SGRead * SGEntrySize);
		} while (SGLeft > 0);
	} else if (req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
		   req->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES) {
		Address = DataPointer;

		pclog("BusLogic DMA: Writing %i bytes at %08X\n", DataLength, Address);
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
		pclog("24-bit Residual data length for reading: %d\n",
			ADDR_TO_U32(req->CmdBlock.old.DataLength));
	} else {
		req->CmdBlock.new.DataLength = Residual;
		pclog("32-bit Residual data length for reading: %d\n",
				req->CmdBlock.new.DataLength);
	}
    }

    if (SCSIDevices[req->TargetID][req->LUN].CmdBuffer != NULL)
    {
	free(SCSIDevices[req->TargetID][req->LUN].CmdBuffer);
	SCSIDevices[req->TargetID][req->LUN].CmdBuffer = NULL;
    }
}


static uint8_t
BuslogicRead(uint16_t Port, void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;
    uint8_t Temp;

    switch (Port & 3) {
	case 0:
		Temp = bl->Status;
		break;
		
	case 1:
		if (bl->UseLocalRAM)
			Temp = bl->LocalRAM.u8View[bl->DataReply];
		else
			Temp = bl->DataBuf[bl->DataReply];
		if (bl->DataReplyLeft) {
			bl->DataReply++;
			bl->DataReplyLeft--;
			if (!bl->DataReplyLeft) {
				BuslogicCommandComplete(bl);
			}
		}
		break;
		
	case 2:
		Temp = bl->Interrupt;
		break;
		
	case 3:
		Temp = bl->Geometry;
		break;
    }

    if (Port < 0x1000) {
	pclog("Buslogic: Read Port 0x%02X, Returned Value %02X\n",
							Port, Temp);
    }

    return(Temp);
}


static uint16_t
BuslogicReadW(uint16_t Port, void *p)
{
    return BuslogicRead(Port, p);
}


static uint32_t
BuslogicReadL(uint16_t Port, void *p)
{
    return BuslogicRead(Port, p);
}


static void BuslogicWriteW(uint16_t Port, uint16_t Val, void *p);
static void BuslogicWriteL(uint16_t Port, uint32_t Val, void *p);
static void
BuslogicWrite(uint16_t Port, uint8_t Val, void *p)
{
    int i = 0;
    uint8_t j = 0;
    Buslogic_t *bl = (Buslogic_t *)p;
    uint8_t Offset;
    MailboxInit_t *MailboxInit;
    ReplyInquireSetupInformation *ReplyISI;
    MailboxInitExtended_t *MailboxInitE;
    ReplyInquireExtendedSetupInformation *ReplyIESI;
    BuslogicPCIInformation_t *ReplyPI;
    char aModelName[] = "542B ";  /* Trailing \0 is fine, that's the filler anyway. */
    int cCharsToTransfer;

    pclog("Buslogic: Write Port 0x%02X, Value %02X\n", Port, Val);

    switch (Port & 3) {
	case 0:
		if ((Val & CTRL_HRST) || (Val & CTRL_SRST)) {	
			uint8_t Reset = !(Val & CTRL_HRST);
			BuslogicResetControl(bl, Reset);
			break;
		}
		
		if (Val & CTRL_IRST) {
			BuslogicClearInterrupt(bl);
		}
		break;

	case 1:
		/* Fast path for the mailbox execution command. */
		if ((Val == 0x02) && (bl->Command == 0xFF)) {
			/* If there are no mailboxes configured, don't even try to do anything. */
			if (bl->MailboxCount) {
				if (!BuslogicCallback) {
					BuslogicCallback = 50 * SCSI_TIME;
				}
			}
			return;
		}

		if (bl->Command == 0xFF) {
			bl->Command = Val;
			bl->CmdParam = 0;
			bl->CmdParamLeft = 0;
			
			bl->Status &= ~(STAT_INVCMD | STAT_IDLE);
			pclog("Buslogic: Operation Code 0x%02X\n", Val);
			switch (bl->Command) {
				case 0x01:
					bl->CmdParamLeft = sizeof(MailboxInit_t);
					break;
				
				case 0x25:
					bl->CmdParamLeft = 1;
					break;

				case 0x05:
				case 0x07:
				case 0x08:
				case 0x09:
				case 0x0D:
				case 0x1F:
				case 0x21:
				case 0x24:
					bl->CmdParamLeft = 1;
					break;	

				case 0x06:
					bl->CmdParamLeft = 4;
					break;

				case 0x1C:
				case 0x1D:
					bl->CmdParamLeft = 3;
					break;

				case 0x8B:
				case 0x8D:
				case 0x8F:
				case 0x96:
					bl->CmdParamLeft = 1;
					break;				

				case 0x81:
					bl->CmdParamLeft = sizeof(MailboxInitExtended_t);
					break;

				case 0x8C:
					bl->CmdParamLeft = 1;
					break;

				case 0x91:
					bl->CmdParamLeft = 2;
					break;
				
				case 0x95: /* Valid only for PCI */
					bl->CmdParamLeft = (bl->chip == CHIP_BUSLOGIC_PCI) ? 1 : 0;
					break;
			}
		} else {
			bl->CmdBuf[bl->CmdParam] = Val;
			bl->CmdParam++;
			bl->CmdParamLeft--;
		}
		
		if (!bl->CmdParamLeft) {
			pclog("Running Operation Code 0x%02X\n", bl->Command);
			switch (bl->Command) {
				case 0x00:
					bl->DataReplyLeft = 0;
					break;

				case 0x01:
					bl->Mbx24bit = 1;
							
					MailboxInit = (MailboxInit_t *)bl->CmdBuf;

					bl->MailboxCount = MailboxInit->Count;
					bl->MailboxOutAddr = ADDR_TO_U32(MailboxInit->Address);
					bl->MailboxInAddr = bl->MailboxOutAddr + (bl->MailboxCount * sizeof(Mailbox_t));
						
					pclog("Buslogic Initialize Mailbox Command\n");
					pclog("Mailbox Out Address=0x%08X\n", bl->MailboxOutAddr);
					pclog("Mailbox In Address=0x%08X\n", bl->MailboxInAddr);
					pclog("Initialized Mailbox, %d entries at 0x%08X\n", MailboxInit->Count, ADDR_TO_U32(MailboxInit->Address));
						
					bl->Status &= ~STAT_INIT;
					bl->DataReplyLeft = 0;
					break;

				case 0x03:
					bl->DataBuf[0] = 0x00;
					bl->DataReplyLeft = 1;
					break;

				case 0x04:
					bl->DataBuf[0] = 0x41;
					bl->DataBuf[1] = 0x41;
					bl->DataBuf[2] = '5';
					bl->DataBuf[3] = '0';
					bl->DataReplyLeft = 4;
					break;

				case 0x05:
					if (bl->CmdBuf[0] <= 1) {
						bl->MailboxOutInterrupts = bl->CmdBuf[0];
						pclog("Mailbox out interrupts: %s\n", bl->MailboxOutInterrupts ? "ON" : "OFF");
					} else {
						bl->Status |= STAT_INVCMD;
					}
					bl->DataReplyLeft = 0;
					break;

				case 0x06:
					bl->DataReplyLeft = 0;
					break;
						
				case 0x07:
					bl->DataReplyLeft = 0;
					bl->LocalRAM.structured.autoSCSIData.uBusOnDelay = bl->CmdBuf[0];
					pclog("Bus-on time: %d\n", bl->CmdBuf[0]);
					break;
						
				case 0x08:
					bl->DataReplyLeft = 0;
					bl->LocalRAM.structured.autoSCSIData.uBusOffDelay = bl->CmdBuf[0];
					pclog("Bus-off time: %d\n", bl->CmdBuf[0]);
					break;
						
				case 0x09:
					bl->DataReplyLeft = 0;
					bl->LocalRAM.structured.autoSCSIData.uDMATransferRate = bl->CmdBuf[0];
					pclog("DMA transfer rate: %02X\n", bl->CmdBuf[0]);
					break;

				case 0x0A:
					memset(bl->DataBuf, 0, 8);
					for (i=0; i<7; i++) {
					    bl->DataBuf[i] = 0;
					    for (j=0; j<8; j++) {
						if (SCSIDevices[i][j].LunType != SCSI_NONE)
						    bl->DataBuf[i] |= (1 << j);
					    }
					}
					bl->DataBuf[7] = 0;
					bl->DataReplyLeft = 8;
					break;				

				case 0x0B:
					bl->DataBuf[0] = (1 << bl->DmaChannel);
					if ((bl->Irq >= 9) && (bl->Irq <= 15))
					{
						bl->DataBuf[1] = (1<<(bl->Irq-9));
					}
					else
						bl->DataBuf[1] = 0;
					{
					}
					bl->DataBuf[2] = 7;	/* HOST ID */
					bl->DataReplyLeft = 3;
					break;

				case 0x0D:
				{
					bl->DataReplyLeft = bl->CmdBuf[0];

					ReplyISI = (ReplyInquireSetupInformation *)bl->DataBuf;
					memset(ReplyISI, 0, sizeof(ReplyInquireSetupInformation));
					
					ReplyISI->fSynchronousInitiationEnabled = 1;
					ReplyISI->fParityCheckingEnabled = 1;
					ReplyISI->cMailbox = bl->MailboxCount;
					U32_TO_ADDR(ReplyISI->MailboxAddress, bl->MailboxOutAddr);
					
					ReplyISI->uSignature = 'B';
					/* The 'D' signature prevents Buslogic's OS/2 drivers from getting too
					* friendly with Adaptec hardware and upsetting the HBA state.
					*/
					ReplyISI->uCharacterD = 'D';      /* BusLogic model. */
					ReplyISI->uHostBusType = (bl->chip == CHIP_BUSLOGIC_PCI) ? 'F' : 'A';     /* ISA bus. */

					pclog("Return Setup Information: %d\n", bl->CmdBuf[0]);
				}
				break;

				case 0x1C:
				{
					uint32_t FIFOBuf;
					addr24 Address;
					
					bl->DataReplyLeft = 0;
					Address.hi = bl->CmdBuf[0];
					Address.mid = bl->CmdBuf[1];
					Address.lo = bl->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					DMAPageRead(FIFOBuf, (char *)&bl->LocalRAM.u8View[64], 64);
				}
				break;
						
				case 0x1D:
				{
					uint32_t FIFOBuf;
					addr24 Address;
					
					bl->DataReplyLeft = 0;
					Address.hi = bl->CmdBuf[0];
					Address.mid = bl->CmdBuf[1];
					Address.lo = bl->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					pclog("Buslogic FIFO: Writing 64 bytes at %08X\n", FIFOBuf);
					DMAPageWrite(FIFOBuf, (char *)&bl->LocalRAM.u8View[64], 64);
				}
				break;	
						
				case 0x1F:
					bl->DataBuf[0] = bl->CmdBuf[0];
					bl->DataReplyLeft = 1;
					break;

				case 0x20:
					bl->DataReplyLeft = 0;
					BuslogicResetControl(bl, 1);
					break;

				case 0x21:
					if (bl->CmdParam == 1)
						bl->CmdParamLeft = bl->CmdBuf[0];
					bl->DataReplyLeft = 0;
					break;

				case 0x23:
					memset(bl->DataBuf, 0, 8);
					for (i = 8; i < 16; i++) {
#if 1	/* FIXME: Kotori, check this! */
					    bl->DataBuf[i-8] = 0;
					    for (j=0; j<8; j++) {
						if (SCSIDevices[i][j].LunType != SCSI_NONE)
						    bl->DataBuf[i-8] |= (1<<j);
#else
					    bl->DataBuf[i] = 0;
					    for (i=0; j<8; j++) {
						if (SCSIDevices[i][j].LunType != SCSI_NONE)
						    bl->DataBuf[i] |= (1<<j);
#endif
					    }
					}
					bl->DataReplyLeft = 8;
					break;

				case 0x24:
				{
					uint16_t TargetsPresentMask = 0;
							
					for (i=0; i<16; i++) {
					    for (j=0; j<8; j++) {
						if (SCSIDevices[i][j].LunType != SCSI_NONE)
						    TargetsPresentMask |= (1 << i);
						}
					}
					bl->DataBuf[0] = TargetsPresentMask & 0xFF;
					bl->DataBuf[1] = TargetsPresentMask >> 8;
					bl->DataReplyLeft = 2;
				}
				break;

				case 0x25:
					if (bl->CmdBuf[0] == 0)
						bl->IrqEnabled = 0;
					else
						bl->IrqEnabled = 1;
					pclog("Lowering IRQ %i\n", bl->Irq);
					BuslogicInterrupt(bl, 0);
					break;

				case 0x81:
				{
					bl->Mbx24bit = 0;

					MailboxInitE = (MailboxInitExtended_t *)bl->CmdBuf;

					bl->MailboxCount = MailboxInitE->Count;
					bl->MailboxOutAddr = MailboxInitE->Address;
					bl->MailboxInAddr = MailboxInitE->Address + (bl->MailboxCount * sizeof(Mailbox32_t));

					pclog("Buslogic Extended Initialize Mailbox Command\n");
					pclog("Mailbox Out Address=0x%08X\n", bl->MailboxOutAddr);
					pclog("Mailbox In Address=0x%08X\n", bl->MailboxInAddr);
					pclog("Initialized Extended Mailbox, %d entries at 0x%08X\n", MailboxInitE->Count, MailboxInitE->Address);

					bl->Status &= ~STAT_INIT;
					bl->DataReplyLeft = 0;
				}
				break;
				
				case 0x84:
					bl->DataBuf[0] = '7';
					bl->DataReplyLeft = 1;
					break;				

				case 0x85:
					bl->DataBuf[0] = 'B';
					bl->DataReplyLeft = 1;
					break;
		
				case 0x86:
				if (bl->chip == CHIP_BUSLOGIC_PCI)
				{
					ReplyPI = (BuslogicPCIInformation_t *) bl->DataBuf;
					memset(ReplyPI, 0, sizeof(BuslogicPCIInformation_t));
					ReplyPI->InformationIsValid = 0;
					switch(bl->Base)
					{
						case 0x330:
							ReplyPI->IsaIOPort = 0;
							break;
						case 0x334:
							ReplyPI->IsaIOPort = 1;
							break;
						case 0x230:
							ReplyPI->IsaIOPort = 2;
							break;
						case 0x234:
							ReplyPI->IsaIOPort = 3;
							break;
						case 0x130:
							ReplyPI->IsaIOPort = 4;
							break;
						case 0x134:
							ReplyPI->IsaIOPort = 5;
							break;
						default:
							ReplyPI->IsaIOPort = 0xFF;
							break;
					}
					ReplyPI->IRQ = bl->Irq;
					bl->DataReplyLeft = sizeof(BuslogicPCIInformation_t);
				} else {
					bl->DataReplyLeft = 0;
					bl->Status |= STAT_INVCMD;					
				}
				break;
		
				case 0x8B:
				{
					/* The reply length is set by the guest and is found in the first byte of the command buffer. */
					bl->DataReplyLeft = bl->CmdBuf[0];
					memset(bl->DataBuf, 0, bl->DataReplyLeft);
					if (bl->chip == CHIP_BUSLOGIC_PCI) {
						aModelName[0] = '9';
						aModelName[1] = '5';
						aModelName[2] = '8';
						aModelName[3] = 'D';
					}
					cCharsToTransfer =   bl->DataReplyLeft <= sizeof(aModelName)
							? bl->DataReplyLeft
							: sizeof(aModelName);

					for (i = 0; i < cCharsToTransfer; i++)
						bl->DataBuf[i] = aModelName[i];
				}
				break;
				
				case 0x8C:
					bl->DataReplyLeft = bl->CmdBuf[0];
					memset(bl->DataBuf, 0, bl->DataReplyLeft);
					break;
		
				case 0x8D:
					bl->DataReplyLeft = bl->CmdBuf[0];
					ReplyIESI = (ReplyInquireExtendedSetupInformation *)bl->DataBuf;
					memset(ReplyIESI, 0, sizeof(ReplyInquireExtendedSetupInformation));

					ReplyIESI->uBusType = (bl->chip == CHIP_BUSLOGIC_PCI) ? 'E' : 'A';         /* ISA style */
					ReplyIESI->uBiosAddress = 0;
					ReplyIESI->u16ScatterGatherLimit = 8192;
					ReplyIESI->cMailbox = bl->MailboxCount;
					ReplyIESI->uMailboxAddressBase = bl->MailboxOutAddr;
					if (bl->chip == CHIP_BUSLOGIC_PCI) {
						ReplyIESI->fLevelSensitiveInterrupt = 1;
						ReplyIESI->fHostWideSCSI = 1;
						ReplyIESI->fHostUltraSCSI = 1;
					}
					memcpy(ReplyIESI->aFirmwareRevision, "07B", sizeof(ReplyIESI->aFirmwareRevision));
					pclog("Return Extended Setup Information: %d\n", bl->CmdBuf[0]);
					break;

				/* VirtualBox has these two modes implemented in reverse.
				   According to the BusLogic datasheet:
				   0 is the strict round robin mode, which is also the one used by the AHA-154x according to the
				   Adaptec specification;
				   1 is the aggressive round robin mode, which "hunts" for an active outgoing mailbox and then
				   processes it. */
				case 0x8F:
					if (bl->CmdBuf[0] == 0)
						bl->StrictRoundRobinMode = 1;
					else if (bl->CmdBuf[0] == 1)
						bl->StrictRoundRobinMode = 0;

					bl->DataReplyLeft = 0;
					break;

				case 0x91:
					Offset = bl->CmdBuf[0];
					bl->DataReplyLeft = bl->CmdBuf[1];
							
					bl->UseLocalRAM = 1;
					bl->DataReply = Offset;
					break;

				case 0x95:
				if (bl->chip == CHIP_BUSLOGIC_PCI) {
					if (bl->Base != 0) {
						io_removehandler(bl->Base, 4,
								 BuslogicRead,
								 BuslogicReadW,
								 BuslogicReadL,
								 BuslogicWrite,
								 BuslogicWriteW,
								 BuslogicWriteL,
								 bl);
					}
					switch(bl->CmdBuf[0]) {
						case 0:
							bl->Base = 0x330;
							break;
						case 1:
							bl->Base = 0x334;
							break;
						case 2:
							bl->Base = 0x230;
							break;
						case 3:
							bl->Base = 0x234;
							break;
						case 4:
							bl->Base = 0x130;
							break;
						case 5:
							bl->Base = 0x134;
							break;
						default:
							bl->Base = 0;
							break;
					}
					if (bl->Base != 0) {
						io_sethandler(bl->Base, 4,
							      BuslogicRead,
							      BuslogicReadW,
							      BuslogicReadL,
							      BuslogicWrite,
							      BuslogicWriteW,
							      BuslogicWriteL,
							      bl);
					}
					bl->DataReplyLeft = 0;
				} else {
					bl->DataReplyLeft = 0;
					bl->Status |= STAT_INVCMD;					
				}
				break;

				case 0x96:
					if (bl->CmdBuf[0] == 0)
						bl->ExtendedLUNCCBFormat = 0;
					else if (bl->CmdBuf[0] == 1)
						bl->ExtendedLUNCCBFormat = 1;
					
					bl->DataReplyLeft = 0;
					break;
				
				default:
					bl->DataReplyLeft = 0;
					bl->Status |= STAT_INVCMD;
					break;
			}
		}
		
		if (bl->DataReplyLeft)
			bl->Status |= STAT_DFULL;
		else if (!bl->CmdParamLeft)
			BuslogicCommandComplete(bl);
		break;
		
		case 2:
			bl->Interrupt = Val;
			break;
		
		case 3:
			bl->Geometry = Val;
			break;
	}
}


static void
BuslogicWriteW(uint16_t Port, uint16_t Val, void *p)
{
    BuslogicWrite(Port, Val & 0xFF, p);
}


static void
BuslogicWriteL(uint16_t Port, uint32_t Val, void *p)
{
    BuslogicWrite(Port, Val & 0xFF, p);
}


static uint8_t
BuslogicConvertSenseLength(uint8_t RequestSenseLength)
{
    pclog("Unconverted Request Sense length %i\n", RequestSenseLength);

    if (RequestSenseLength == 0)
	RequestSenseLength = 14;
    else if (RequestSenseLength == 1)
	RequestSenseLength = 0;

    pclog("Request Sense length %i\n", RequestSenseLength);

    return(RequestSenseLength);
}


static void
BuslogicSenseBufferFree(Req_t *req, int Copy, int is_hd)
{
    uint8_t SenseLength = BuslogicConvertSenseLength(req->CmdBlock.common.RequestSenseLength);
    uint8_t cdrom_id = scsi_cdrom_drives[req->TargetID][req->LUN];
    uint8_t hdc_id = scsi_hard_disks[req->TargetID][req->LUN];
    uint32_t SenseBufferAddress;
    uint8_t temp_sense[256];

    if (SenseLength && Copy) {
	if (is_hd)
	{
		scsi_hd_request_sense_for_scsi(hdc_id, temp_sense, SenseLength);
	}
	else
	{
		cdrom_request_sense_for_scsi(cdrom_id, temp_sense, SenseLength);
	}

	/*
	 * The sense address, in 32-bit mode, is located in the
	 * Sense Pointer of the CCB, but in 24-bit mode, it is
	 * located at the end of the Command Descriptor Block.
	 */
	if (req->Is24bit) {
		SenseBufferAddress = req->CCBPointer;
		SenseBufferAddress += req->CmdBlock.common.CdbLength + offsetof(CCB, Cdb);
	} else {
		SenseBufferAddress = req->CmdBlock.new.SensePointer;
	}

	pclog("Request Sense address: %02X\n", SenseBufferAddress);

	pclog("BuslogicSenseBufferFree(): Writing %i bytes at %08X\n",
					SenseLength, SenseBufferAddress);
	DMAPageWrite(SenseBufferAddress, (char *)temp_sense, SenseLength);
	pclog("Sense data written to buffer: %02X %02X %02X\n",
		temp_sense[2], temp_sense[12], temp_sense[13]);
    }
}


static void
BuslogicHDCommand(Buslogic_t *bl)
{
    Req_t *req = &bl->Req;
    uint8_t Id, Lun;
    uint8_t hdc_id;
    uint8_t hd_phase;
    uint8_t temp_cdb[12];
    uint32_t i;

    Id = req->TargetID;
    Lun = req->LUN;
    hdc_id = scsi_hard_disks[Id][Lun];

    if (hdc_id == 0xff)  fatal("SCSI hard disk on %02i:%02i has disappeared\n", Id, Lun);

    pclog("SCSI HD command being executed on: SCSI ID %i, SCSI LUN %i, HD %i\n",
							Id, Lun, hdc_id);

    pclog("SCSI Cdb[0]=0x%02X\n", req->CmdBlock.common.Cdb[0]);
    for (i = 1; i < req->CmdBlock.common.CdbLength; i++) {
	pclog("SCSI Cdb[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);
    }

    memset(temp_cdb, 0, shdc[hdc_id].cdb_len);
    if (req->CmdBlock.common.CdbLength <= shdc[hdc_id].cdb_len) {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb,
		req->CmdBlock.common.CdbLength);
    } else {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb, shdc[hdc_id].cdb_len);
    }

    /*
     * Since that field in the HDC struct is never used when
     * the bus type is SCSI, let's use it for this scope.
     */
    shdc[hdc_id].request_length = temp_cdb[1];

    if (req->CmdBlock.common.CdbLength != 12) {
	/*
	 * Make sure the LUN field of the temporary CDB is always 0,
	 * otherwise Daemon Tools drives will misbehave when a command
	 * is passed through to them.
	 */
	temp_cdb[1] &= 0x1f;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    SCSIPhase = SCSI_PHASE_COMMAND;
    scsi_hd_command(hdc_id, temp_cdb);
    SCSIStatus = scsi_hd_err_stat_to_scsi(hdc_id);
    if (SCSIStatus == SCSI_STATUS_OK) {
	hd_phase = scsi_hd_phase_to_scsi(hdc_id);
	if (hd_phase == 2) {
		/* Command completed - call the phase callback to complete the command. */
		scsi_hd_callback(hdc_id);
	} else {
		/* Command first phase complete - call the callback to execute the second phase. */
		scsi_hd_callback(hdc_id);
		SCSIStatus = scsi_hd_err_stat_to_scsi(hdc_id);
		/* Command second phase complete - call the callback to complete the command. */
		scsi_hd_callback(hdc_id);
	}
    } else {
	/* Error (Check Condition) - call the phase callback to complete the command. */
	scsi_hd_callback(hdc_id);
    }

    pclog("SCSI Status: %s, Sense: %02X, ASC: %02X, ASCQ: %02X\n", (SCSIStatus == SCSI_STATUS_OK) ? "OK" : "CHECK CONDITION", shdc[hdc_id].sense[2], shdc[hdc_id].sense[12], shdc[hdc_id].sense[13]);

    BuslogicDataBufferFree(req);

    BuslogicSenseBufferFree(req, (SCSIStatus != SCSI_STATUS_OK), 1);

    pclog("Request complete\n");

    if (SCSIStatus == SCSI_STATUS_OK) {
	BuslogicMailboxInSetup(bl, req->CCBPointer, &req->CmdBlock,
			       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
    } else if (SCSIStatus == SCSI_STATUS_CHECK_CONDITION) {
	BuslogicMailboxInSetup(bl, req->CCBPointer, &req->CmdBlock,
			CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION, MBI_ERROR);
    }
}


static void
BuslogicCDROMCommand(Buslogic_t *bl)
{
    Req_t *req = &bl->Req;
    uint8_t Id, Lun;
    uint8_t cdrom_id;
    uint8_t cdrom_phase;
    uint8_t temp_cdb[12];
    uint32_t i;

    Id = req->TargetID;
    Lun = req->LUN;
    cdrom_id = scsi_cdrom_drives[Id][Lun];

    if (cdrom_id == 0xff)  fatal("SCSI CD-ROM on %02i:%02i has disappeared\n", Id, Lun);

    pclog("CD-ROM command being executed on: SCSI ID %i, SCSI LUN %i, CD-ROM %i\n",
							Id, Lun, cdrom_id);

    pclog("SCSI Cdb[0]=0x%02X\n", req->CmdBlock.common.Cdb[0]);
    for (i = 1; i < req->CmdBlock.common.CdbLength; i++) {
	pclog("SCSI Cdb[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);
    }

    memset(temp_cdb, 0, cdrom[cdrom_id].cdb_len);
    if (req->CmdBlock.common.CdbLength <= cdrom[cdrom_id].cdb_len) {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb,
		req->CmdBlock.common.CdbLength);
    } else {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb, cdrom[cdrom_id].cdb_len);
    }

    /*
     * Since that field in the CDROM struct is never used when
     * the bus type is SCSI, let's use it for this scope.
     */
    cdrom[cdrom_id].request_length = temp_cdb[1];

    if (req->CmdBlock.common.CdbLength != 12) {
	/*
	 * Make sure the LUN field of the temporary CDB is always 0,
	 * otherwise Daemon Tools drives will misbehave when a command
	 * is passed through to them.
	 */
	temp_cdb[1] &= 0x1f;
    }

    /* Finally, execute the SCSI command immediately and get the transfer length. */
    SCSIPhase = SCSI_PHASE_COMMAND;
    cdrom_command(cdrom_id, temp_cdb);
    SCSIStatus = cdrom_CDROM_PHASE_to_scsi(cdrom_id);
    if (SCSIStatus == SCSI_STATUS_OK) {
	cdrom_phase = cdrom_atapi_phase_to_scsi(cdrom_id);
	if (cdrom_phase == 2) {
		/* Command completed - call the phase callback to complete the command. */
		cdrom_phase_callback(cdrom_id);
	} else {
		/* Command first phase complete - call the callback to execute the second phase. */
		cdrom_phase_callback(cdrom_id);
		SCSIStatus = cdrom_CDROM_PHASE_to_scsi(cdrom_id);
		/* Command second phase complete - call the callback to complete the command. */
		cdrom_phase_callback(cdrom_id);
	}
    } else {
	/* Error (Check Condition) - call the phase callback to complete the command. */
	cdrom_phase_callback(cdrom_id);
    }

    pclog("SCSI Status: %s, Sense: %02X, ASC: %02X, ASCQ: %02X\n", (SCSIStatus == SCSI_STATUS_OK) ? "OK" : "CHECK CONDITION", cdrom[cdrom_id].sense[2], cdrom[cdrom_id].sense[12], cdrom[cdrom_id].sense[13]);

    BuslogicDataBufferFree(req);

    BuslogicSenseBufferFree(req, (SCSIStatus != SCSI_STATUS_OK), 0);

    pclog("Request complete\n");

    if (SCSIStatus == SCSI_STATUS_OK) {
	BuslogicMailboxInSetup(bl, req->CCBPointer, &req->CmdBlock,
			       CCB_COMPLETE, SCSI_STATUS_OK, MBI_SUCCESS);
    } else if (SCSIStatus == SCSI_STATUS_CHECK_CONDITION) {
	BuslogicMailboxInSetup(bl, req->CCBPointer, &req->CmdBlock,
			CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION, MBI_ERROR);
    }
}


static void
BuslogicSCSIRequestSetup(Buslogic_t *bl, uint32_t CCBPointer, Mailbox32_t *Mailbox32)
{	
    Req_t *req = &bl->Req;
    uint8_t Id, Lun;
    uint8_t last_id = (bl->chip >= CHIP_BUSLOGIC_ISA) ? 15 : 7;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (char *)&req->CmdBlock, sizeof(CCB32));

    req->Is24bit = bl->Mbx24bit;
    req->CCBPointer = CCBPointer;
    req->TargetID = bl->Mbx24bit ? req->CmdBlock.old.Id : req->CmdBlock.new.Id;
    req->LUN = bl->Mbx24bit ? req->CmdBlock.old.Lun : req->CmdBlock.new.Lun;
 
    Id = req->TargetID;
    Lun = req->LUN;
    if ((Id > last_id) || (Lun > 7)) {
	BuslogicMailboxInSetup(bl, CCBPointer, &req->CmdBlock,
			       CCB_INVALID_CCB, SCSI_STATUS_OK, MBI_ERROR);
	return;
    }
	
    pclog("Scanning SCSI Target ID %i\n", Id);		

    SCSIStatus = SCSI_STATUS_OK;
    SCSIDevices[Id][Lun].InitLength = 0;

    BuslogicDataBufferAllocate(req, req->Is24bit);

    if (SCSIDevices[Id][Lun].LunType == SCSI_NONE) {
	pclog("SCSI Target ID %i and LUN %i have no device attached\n",Id,Lun);
	BuslogicDataBufferFree(req);
	BuslogicSenseBufferFree(req, 0, 0);
	BuslogicMailboxInSetup(bl, CCBPointer, &req->CmdBlock,
			       CCB_SELECTION_TIMEOUT,SCSI_STATUS_OK,MBI_ERROR);
    } else {
	pclog("SCSI Target ID %i and LUN %i detected and working\n", Id, Lun);

	pclog("Transfer Control %02X\n", req->CmdBlock.common.ControlByte);
	pclog("CDB Length %i\n", req->CmdBlock.common.CdbLength);	
	pclog("CCB Opcode %x\n", req->CmdBlock.common.Opcode);		
	if (req->CmdBlock.common.ControlByte > 0x03) {
		pclog("Invalid control byte: %02X\n",
			req->CmdBlock.common.ControlByte);
	}

	BuslogicInOperation = (SCSIDevices[Id][Lun].LunType == SCSI_DISK) ? 0x11 : 1;
	pclog("SCSI (%i:%i) -> %i\n", Id, Lun, SCSIDevices[Id][Lun].LunType);
    }
}


static void
BuslogicSCSIRequestAbort(Buslogic_t *bl, uint32_t CCBPointer)
{
    CCBU CmdBlock;

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (char *)&CmdBlock, sizeof(CCB32));

    /* Only SCSI CD-ROMs are supported at the moment, SCSI hard disk support will come soon. */
    BuslogicMailboxInSetup(bl, CCBPointer, &CmdBlock,
			   0x26, SCSI_STATUS_OK, MBI_NOT_FOUND);
}


static uint32_t
BuslogicMailboxOut(Buslogic_t *bl, Mailbox32_t *Mailbox32)
{	
    Mailbox_t MailboxOut;
    uint32_t Outgoing;

    if (bl->Mbx24bit) {
	Outgoing = bl->MailboxOutAddr + (bl->MailboxOutPosCur * sizeof(Mailbox_t));
	DMAPageRead(Outgoing, (char *)&MailboxOut, sizeof(Mailbox_t));

	Mailbox32->CCBPointer = ADDR_TO_U32(MailboxOut.CCBPointer);
	Mailbox32->u.out.ActionCode = MailboxOut.CmdStatus;
    } else {
	Outgoing = bl->MailboxOutAddr + (bl->MailboxOutPosCur * sizeof(Mailbox32_t));

	DMAPageRead(Outgoing, (char *)Mailbox32, sizeof(Mailbox32_t));	
    }
	
    return Outgoing;
}


static void
BuslogicMailboxOutAdvance(Buslogic_t *bl)
{
    bl->MailboxOutPosCur = (bl->MailboxOutPosCur + 1) % bl->MailboxCount;
}


static void
BuslogicProcessMailbox(Buslogic_t *bl)
{
    Mailbox32_t mb32;
    uint32_t Outgoing;
    uint8_t CmdStatus = MBO_FREE;
    uint32_t CodeOffset = 0;

    CodeOffset = bl->Mbx24bit ? offsetof(Mailbox_t, CmdStatus) : offsetof(Mailbox32_t, u.out.ActionCode);

    if (! bl->StrictRoundRobinMode) {
	uint8_t MailboxCur = bl->MailboxOutPosCur;

	/* Search for a filled mailbox - stop if we have scanned all mailboxes. */
	do {
		/* Fetch mailbox from guest memory. */
		Outgoing = BuslogicMailboxOut(bl, &mb32);

		/* Check the next mailbox. */
		BuslogicMailboxOutAdvance(bl);
	} while ((mb32.u.out.ActionCode == MBO_FREE) && (MailboxCur != bl->MailboxOutPosCur));
    } else {
	Outgoing = BuslogicMailboxOut(bl, &mb32);
    }

    if (mb32.u.out.ActionCode != MBO_FREE) {
	/* We got the mailbox, mark it as free in the guest. */
	pclog("BuslogicProcessMailbox(): Writing %i bytes at %08X\n", sizeof(CmdStatus), Outgoing + CodeOffset);
		DMAPageWrite(Outgoing + CodeOffset, (char *)&CmdStatus, sizeof(CmdStatus));
    }

    if (bl->MailboxOutInterrupts)
	BuslogicRaiseInterrupt(bl, INTR_MBOA | INTR_ANY);

    /* Check if the mailbox is actually loaded. */
    if (mb32.u.out.ActionCode == MBO_FREE) {
	return;
    }

    if (mb32.u.out.ActionCode == MBO_START) {
	pclog("Start Mailbox Command\n");
	BuslogicSCSIRequestSetup(bl, mb32.CCBPointer, &mb32);
    } else if (mb32.u.out.ActionCode == MBO_ABORT) {
		pclog("Abort Mailbox Command\n");
		BuslogicSCSIRequestAbort(bl, mb32.CCBPointer);
    } else {
	pclog("Invalid action code: %02X\n", mb32.u.out.ActionCode);
    }

    /* Advance to the next mailbox. */
    if (bl->StrictRoundRobinMode)
	BuslogicMailboxOutAdvance(bl);
}


static void
BuslogicResetPoll(void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;

    bl->Status &= ~STAT_STST;
    bl->Status |= STAT_IDLE;

    BuslogicResetCallback = 0;
}


static void
BuslogicCommandCallback(void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;

    if (BuslogicInOperation == 0) {
	if (bl->MailboxCount) {
		BuslogicProcessMailbox(bl);
	} else {
		BuslogicCallback += 50 * SCSI_TIME;
		return;
	}
    } else if (BuslogicInOperation == 1) {
	pclog("BusLogic Callback: Process CD-ROM request\n");
	BuslogicCDROMCommand(bl);
	if (bl->Req.CmdBlock.common.Cdb[0] == 0x42)
	{
		/* This is needed since CD Audio inevitably means READ SUBCHANNEL spam. */
		BuslogicCallback += 1000 * SCSI_TIME;
		return;
	}
    } else if (BuslogicInOperation == 2) {
	pclog("BusLogic Callback: Send incoming mailbox\n");
	BuslogicMailboxIn(bl);
    } else if (BuslogicInOperation == 0x11) {
	pclog("BusLogic Callback: Process hard disk request\n");
	BuslogicHDCommand(bl);
    } else {
	fatal("Invalid BusLogic callback phase: %i\n", BuslogicInOperation);
    }

    BuslogicCallback += 50 * SCSI_TIME;
}


static uint8_t
mem_read_null(uint32_t addr, void *priv)
{
    return(0);
}


static uint16_t
mem_read_nullw(uint32_t addr, void *priv)
{
    return(0);
}


static uint32_t
mem_read_nulll(uint32_t addr, void *priv)
{
    return(0);
}


typedef union {
    uint32_t	addr;
    uint8_t	addr_regs[4];
} bar_t;


uint8_t	buslogic_pci_regs[256];
bar_t	buslogic_pci_bar[3];


static uint8_t
BuslogicPCIRead(int func, int addr, void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;

    switch (addr) {
	case 0x00:
		return 0x4b;
	case 0x01:
		return 0x10;
	case 0x02:
		return 0x40;
	case 0x03:
		return 0x10;
	case 0x04:
		return buslogic_pci_regs[0x04];	/*Respond to IO and memory accesses*/
	case 0x05:
		return buslogic_pci_regs[0x05];
	case 0x07:
		return 2;
	case 0x08:
		return 1;			/*Revision ID*/
	case 0x09:
		return 0;			/*Programming interface*/
	case 0x0A:
		return 0;			/*Subclass*/
	case 0x0B:
		return 1;			/* Class code*/
	case 0x10:
		return (buslogic_pci_bar[0].addr_regs[0] & 0xe0) | 1;	/*I/O space*/
	case 0x11:
		return buslogic_pci_bar[0].addr_regs[1];
	case 0x12:
		return buslogic_pci_bar[0].addr_regs[2];
	case 0x13:
		return buslogic_pci_bar[0].addr_regs[3];
	case 0x14:
		return (buslogic_pci_bar[1].addr_regs[0] & 0xe0);	/*Memory space*/
	case 0x15:
		return buslogic_pci_bar[1].addr_regs[1];
	case 0x16:
		return buslogic_pci_bar[1].addr_regs[2];
	case 0x17:
		return buslogic_pci_bar[1].addr_regs[3];
	case 0x2C:
		return 0x4b;
	case 0x2D:
		return 0x10;
	case 0x2E:
		return 0x40;
	case 0x2F:
		return 0x10;
	case 0x30:
		return buslogic_pci_bar[2].addr_regs[0] & 0x01;	/*BIOS ROM address*/
	case 0x31:
		return buslogic_pci_bar[2].addr_regs[1] | 0x18;
	case 0x32:
		return buslogic_pci_bar[2].addr_regs[2];
	case 0x33:
		return buslogic_pci_bar[2].addr_regs[3];
	case 0x3C:
		return bl->Irq;
	case 0x3D:
		return 4;
    }

    return(0);
}


static void
BuslogicPCIWrite(int func, int addr, uint8_t val, void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;
    uint8_t valxor;

    switch (addr) {
	case 0x04:
		valxor = (val & 0x27) ^ buslogic_pci_regs[addr];
		if (valxor & PCI_COMMAND_IO) {
			io_removehandler(bl->PCIBase, 4,
				 BuslogicRead, BuslogicReadW, BuslogicReadL,
				 BuslogicWrite, BuslogicWriteW, BuslogicWriteL,
				 bl);
			if ((bl->PCIBase != 0) && (val & PCI_COMMAND_IO)) {
				io_sethandler(bl->PCIBase, 0x0020,
					      BuslogicRead, BuslogicReadW,
					      BuslogicReadL, BuslogicWrite,
					      BuslogicWriteW, BuslogicWriteL,
					      bl);
			}
		}
		if (valxor & PCI_COMMAND_MEM) {
			mem_mapping_disable(&bl->mmio_mapping);
			if ((bl->MMIOBase != 0) & (val & PCI_COMMAND_MEM)) {
				mem_mapping_set_addr(&bl->mmio_mapping,
						     bl->MMIOBase, 0x20);
			}
		}
		buslogic_pci_regs[addr] = val & 0x27;
		break;

	case 0x10:
		val &= 0xe0;
		val |= 1;

	case 0x11: case 0x12: case 0x13:
		/* I/O Base set. */
		/* First, remove the old I/O. */
		io_removehandler(bl->PCIBase, 0x0020,
				 BuslogicRead, BuslogicReadW, BuslogicReadL,
				 BuslogicWrite, BuslogicWriteW, BuslogicWriteL,
				 bl);
		/* Then let's set the PCI regs. */
		buslogic_pci_bar[0].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		bl->PCIBase = buslogic_pci_bar[0].addr & 0xffe0;
		/* Log the new base. */
		pclog("BusLogic PCI: New I/O base is %04X\n" , bl->PCIBase);
		/* We're done, so get out of the here. */
		if (buslogic_pci_regs[4] & PCI_COMMAND_IO) {
			if (bl->PCIBase != 0) {
				io_sethandler(bl->PCIBase, 0x0020,
					      BuslogicRead, BuslogicReadW,
					      BuslogicReadL, BuslogicWrite,
					      BuslogicWriteW, BuslogicWriteL,
					      bl);
			}
		}
		return;

	case 0x14:
		val &= 0xe0;

	case 0x15: case 0x16: case 0x17:
		/* I/O Base set. */
		/* First, remove the old I/O. */
		mem_mapping_disable(&bl->mmio_mapping);
		/* Then let's set the PCI regs. */
		buslogic_pci_bar[1].addr_regs[addr & 3] = val;
		/* Then let's calculate the new I/O base. */
		bl->MMIOBase = buslogic_pci_bar[1].addr & 0xffffffe0;
		/* Log the new base. */
		pclog("BusLogic PCI: New MMIO base is %04X\n" , bl->MMIOBase);
		/* We're done, so get out of the here. */
		if (buslogic_pci_regs[4] & PCI_COMMAND_MEM) {
			if (bl->PCIBase != 0) {
				mem_mapping_set_addr(&bl->mmio_mapping,
						     bl->MMIOBase, 0x20);
			}
		}
		return;

#if 0
	case 0x3C:
		buslogic_pci_regs[addr] = val;
		if (val != 0xFF) {
			BuslogicLog("BusLogic IRQ now: %i\n", val);
			bl->Irq = val;
		}
		return;
#endif
    }
}


static void *
BuslogicInit(int chip)
{
    Buslogic_t *bl;

    int i = 0;
    int j = 0;

    bl = malloc(sizeof(Buslogic_t));
    memset(bl, 0x00, sizeof(Buslogic_t));

    BuslogicResetDevice = bl;
    if (!PCI && (chip == CHIP_BUSLOGIC_PCI))
    {
	chip = CHIP_BUSLOGIC_ISA;
    }
    bl->chip = chip;
    bl->Base = device_get_config_hex16("addr");
    bl->PCIBase = 0;
    bl->MMIOBase = 0;
    bl->Irq = device_get_config_int("irq");
    bl->DmaChannel = device_get_config_int("dma");

    if (bl->Base != 0) {
	if (bl->chip == CHIP_BUSLOGIC_PCI) {
		io_sethandler(bl->Base, 4,
			      BuslogicRead, BuslogicReadW, BuslogicReadL,
			      BuslogicWrite, BuslogicWriteW, BuslogicWriteL,
			      bl);
	} else {
		io_sethandler(bl->Base, 4,
			      BuslogicRead, BuslogicReadW, NULL,
			      BuslogicWrite, BuslogicWriteW, NULL, bl);
	}
    }

    pclog("Building SCSI hard disk map...\n");
    build_scsi_hd_map();
    pclog("Building SCSI CD-ROM map...\n");
    build_scsi_cdrom_map();

    for (i=0; i<16; i++) {
	for (j=0; j<8; j++) {
		if (scsi_hard_disks[i][j] != 0xff) {
			SCSIDevices[i][j].LunType = SCSI_DISK;
		}
		else if (find_cdrom_for_scsi_id(i, j) != 0xff) {
			SCSIDevices[i][j].LunType = SCSI_CDROM;
		}
		else {
			SCSIDevices[i][j].LunType = SCSI_NONE;
		}
	}
    }

    timer_add(BuslogicResetPoll,
	      &BuslogicResetCallback, &BuslogicResetCallback, bl);
    timer_add(BuslogicCommandCallback,
	      &BuslogicCallback, &BuslogicCallback, bl);

    if (bl->chip == CHIP_BUSLOGIC_PCI) {
	pci_add(BuslogicPCIRead, BuslogicPCIWrite, bl);

	buslogic_pci_bar[0].addr_regs[0] = 1;
	buslogic_pci_bar[1].addr_regs[0] = 0;
       	buslogic_pci_regs[0x04] = 3;
#if 0
        buslogic_pci_regs[0x05] = 0;
        buslogic_pci_regs[0x07] = 2;
#endif
	buslogic_pci_bar[2].addr = 0;

	mem_mapping_add(&bl->mmio_mapping, 0xfffd0000, 0x20,
		        mem_read_null, mem_read_nullw, mem_read_nulll,
			mem_write_null, mem_write_nullw, mem_write_nulll,
			NULL, MEM_MAPPING_EXTERNAL, bl);
	mem_mapping_disable(&bl->mmio_mapping);
    }
	
    pclog("Buslogic on port 0x%04X\n", bl->Base);
	
    BuslogicResetControl(bl, CTRL_HRST);

    return(bl);
}


static void *
Buslogic_542B_Init(void)
{
	return BuslogicInit(CHIP_BUSLOGIC_ISA);
}


static void *
Buslogic_958D_Init(void)
{
	return BuslogicInit(CHIP_BUSLOGIC_PCI);
}


static void
BuslogicClose(void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;
    free(bl);
    BuslogicResetDevice = NULL;
}


static device_config_t BuslogicConfig[] = {
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
		"", "", -1
	}
};


device_t buslogic_device = {
	"Buslogic BT-542B ISA",
	0,
	Buslogic_542B_Init,
	BuslogicClose,
	NULL,
	NULL,
	NULL,
	NULL,
	BuslogicConfig
};

device_t buslogic_pci_device = {
	"Buslogic BT-958D PCI",
	0,
	Buslogic_958D_Init,
	BuslogicClose,
	NULL,
	NULL,
	NULL,
	NULL,
	BuslogicConfig
};
