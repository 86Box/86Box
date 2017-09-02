/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of BusLogic ISA and PCI SCSI controllers. Boards
 *		supported:
 *
 *		  0 - BT-545C ISA;
 *		  1 - BT-958D PCI (but BT-545C ISA on non-PCI machines)
 *
 * Version:	@(#)scsi_buslogic.c	1.0.11	2017/09/01
 *
 * Authors:	TheCollector1995, <mariogplayer@gmail.com>
 *		Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "../ibm.h"
#include "../io.h"
#include "../mem.h"
#include "../rom.h"
#include "../dma.h"
#include "../pic.h"
#include "../pci.h"
#include "../timer.h"
#include "../device.h"
#include "../win/plat_thread.h"
#include "scsi.h"
#include "scsi_bios_command.h"
#include "scsi_device.h"
#include "scsi_buslogic.h"


#define BUSLOGIC_RESET_DURATION_US UINT64_C(50)


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
    uint8_t	uSCSIConfiguration;
    uint8_t	uBusOnDelay;
    uint8_t	uBusOffDelay;
    uint8_t	uBIOSConfiguration;
    uint16_t	u16DeviceEnabledMask;
    uint16_t	u16WidePermittedMask;
    uint16_t	u16FastPermittedMask;
    uint16_t	u16SynchronousPermittedMask;
    uint16_t	u16DisconnectPermittedMask;
    uint16_t	u16SendStartUnitCommandMask;
    uint16_t	u16IgnoreInBIOSScanMask;
    unsigned char uPCIInterruptPin :                2;
    unsigned char uHostAdapterIoPortAddress :       2;
    uint8_t          fAggressiveRoundRobinMode :           1;
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
    unsigned char uReserved10 :                     2;
    uint8_t          fMultiBoot :                      1;
    unsigned char uReserved11 :                     2;
    unsigned char uBootTargetId :                   4;
    unsigned char uBootChannel :                    4;
    uint8_t          fForceBusDeviceScanningOrder :    1;
    unsigned char uReserved12 :                     7;
    uint16_t      u16NonTaggedToAlternateLunPermittedMask;
    uint16_t      u16RenegotiateSyncAfterCheckConditionMask;
    uint8_t       aReserved14[10];
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
typedef struct
{
    /** Data length. */
    uint32_t        DataLength;
    /** Data pointer. */
    uint32_t        DataPointer;
    /** The device the request is sent to. */
    uint8_t         TargetId;
    /** The LUN in the device. */
    uint8_t         LogicalUnit;
    /** Reserved */
    unsigned char   Reserved1 : 3;
    /** Data direction for the request. */
    unsigned char   DataDirection : 2;
    /** Reserved */
    unsigned char   Reserved2 : 3;
    /** Length of the SCSI CDB. */
    uint8_t         CDBLength;
    /** The SCSI CDB.  (A CDB can be 12 bytes long.)   */
    uint8_t         CDB[12];
} ESCMD;
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
    int		ExtendedLUNCCBFormat;
    HALocalRAM	LocalRAM;
    Req_t Req;
    uint8_t	Status;
    uint8_t	Interrupt;
    uint8_t	Geometry;
    uint8_t	Control;
    uint8_t	Command;
    uint8_t	CmdBuf[128];
    uint8_t	CmdParam;
    uint8_t	CmdParamLeft;
    uint8_t	DataBuf[65536];
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
    int		has_bios;
    uint32_t	bios_addr,
		bios_size,
		bios_mask;
    uint8_t     AutoSCSIROM[32768];
    uint8_t     SCAMData[65536];
    event_t	*evt;
    int		scan_restart;
} Buslogic_t;
#pragma pack(pop)


static int	BuslogicResetCallback = 0;


static void	BuslogicCommandThread(void *p);
static thread_t	*poll_tid;


enum {
    CHIP_BUSLOGIC_ISA,
    CHIP_BUSLOGIC_MCA,
    CHIP_BUSLOGIC_VLB,
    CHIP_BUSLOGIC_PCI
};

/* #define ENABLE_BUSLOGIC_LOG 0 */
int buslogic_do_log = 0;

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
#define pclog	BuslogicLog

static void
BuslogicInterrupt(Buslogic_t *bl, int set)
{
	if (bl->chip == CHIP_BUSLOGIC_PCI)
	{
	        if (set)
		{
        	        pci_set_irq(bl->Card, PCI_INTB);
		}
	        else
		{
        	        pci_clear_irq(bl->Card, PCI_INTB);
		}
	}
	else
	{
		if (set)
		{
			if (bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt)
			{
				picintlevel(1 << bl->Irq);
			}
			else
			{
				picint(1 << bl->Irq);
			}
			/* pclog("Interrupt Set\n"); */
		}
		else
		{
			picintc(1 << bl->Irq);
			/* pclog("Interrupt Cleared\n"); */
		}
	}
}


static wchar_t *
BuslogicGetNVRFileName(Buslogic_t *bl)
{
	switch(bl->chip)
	{
		case CHIP_BUSLOGIC_ISA:
			return L"bt545c.nvr";
		case CHIP_BUSLOGIC_MCA:
			return L"bt640a.nvr";
		case CHIP_BUSLOGIC_VLB:
			return L"bt445s.nvr";
		case CHIP_BUSLOGIC_PCI:
			return L"bt958d.nvr";
		default:
			fatal("Unrecognized BusLogic chip: %i\n", bl->chip);
			return NULL;
	}
}


static void
BuslogicAutoSCSIRamSetDefaults(Buslogic_t *bl, uint8_t safe)
{
	HALocalRAM *HALR = &bl->LocalRAM;

	memset(&(HALR->structured.autoSCSIData), 0, sizeof(AutoSCSIRam));

	HALR->structured.autoSCSIData.aInternalSignature[0] = 'F';
	HALR->structured.autoSCSIData.aInternalSignature[1] = 'A';

	HALR->structured.autoSCSIData.cbInformation = 64;

	HALR->structured.autoSCSIData.aHostAdaptertype[0] = ' ';
	switch (bl->chip)
	{
		case CHIP_BUSLOGIC_ISA:
			HALR->structured.autoSCSIData.aHostAdaptertype[1] = '5';
			HALR->structured.autoSCSIData.aHostAdaptertype[2] = '4';
			HALR->structured.autoSCSIData.aHostAdaptertype[3] = '5';
			HALR->structured.autoSCSIData.aHostAdaptertype[4] = 'C';
			break;
		case CHIP_BUSLOGIC_VLB:
			HALR->structured.autoSCSIData.aHostAdaptertype[1] = '4';
			HALR->structured.autoSCSIData.aHostAdaptertype[2] = '4';
			HALR->structured.autoSCSIData.aHostAdaptertype[3] = '5';
			HALR->structured.autoSCSIData.aHostAdaptertype[4] = 'S';
			break;
		case CHIP_BUSLOGIC_MCA:
			HALR->structured.autoSCSIData.aHostAdaptertype[1] = '6';
			HALR->structured.autoSCSIData.aHostAdaptertype[2] = '4';
			HALR->structured.autoSCSIData.aHostAdaptertype[3] = '0';
			HALR->structured.autoSCSIData.aHostAdaptertype[4] = 'A';
			break;
		case CHIP_BUSLOGIC_PCI:
			HALR->structured.autoSCSIData.aHostAdaptertype[1] = '9';
			HALR->structured.autoSCSIData.aHostAdaptertype[2] = '5';
			HALR->structured.autoSCSIData.aHostAdaptertype[3] = '8';
			HALR->structured.autoSCSIData.aHostAdaptertype[4] = 'D';
			break;
	}
	HALR->structured.autoSCSIData.aHostAdaptertype[5] = ' ';

	HALR->structured.autoSCSIData.fLevelSensitiveInterrupt = (bl->chip == CHIP_BUSLOGIC_PCI) ? 1 : 0;
	HALR->structured.autoSCSIData.uSystemRAMAreForBIOS = 6;

	if (bl->chip != CHIP_BUSLOGIC_PCI)
	{
		switch(bl->DmaChannel)
		{
			case 5:
				HALR->structured.autoSCSIData.uDMAChannel = 1;
				break;
			case 6:
				HALR->structured.autoSCSIData.uDMAChannel = 2;
				break;
			case 7:
				HALR->structured.autoSCSIData.uDMAChannel = 3;
				break;
			default:
				HALR->structured.autoSCSIData.uDMAChannel = 0;
				break;
		}
	}
	HALR->structured.autoSCSIData.fDMAAutoConfiguration = (bl->chip == CHIP_BUSLOGIC_PCI) ? 0 : 1;

	if (bl->chip != CHIP_BUSLOGIC_PCI)
	{
		switch(bl->Irq)
		{
			case 9:
				HALR->structured.autoSCSIData.uIrqChannel = 1;
				break;
			case 10:
				HALR->structured.autoSCSIData.uIrqChannel = 2;
				break;
			case 11:
				HALR->structured.autoSCSIData.uIrqChannel = 3;
				break;
			case 12:
				HALR->structured.autoSCSIData.uIrqChannel = 4;
				break;
			case 14:
				HALR->structured.autoSCSIData.uIrqChannel = 5;
				break;
			case 15:
				HALR->structured.autoSCSIData.uIrqChannel = 6;
				break;
			default:
				HALR->structured.autoSCSIData.uIrqChannel = 0;
				break;
		}
	}
	HALR->structured.autoSCSIData.fIrqAutoConfiguration = (bl->chip == CHIP_BUSLOGIC_PCI) ? 0 : 1;

	HALR->structured.autoSCSIData.uDMATransferRate = (bl->chip == CHIP_BUSLOGIC_ISA) ? 1 : 0;

	HALR->structured.autoSCSIData.uSCSIId = 7;
	HALR->structured.autoSCSIData.uSCSIConfiguration = 0x3F;
	HALR->structured.autoSCSIData.uBusOnDelay = (bl->chip == CHIP_BUSLOGIC_PCI) ? 0 : 7;
	HALR->structured.autoSCSIData.uBusOffDelay = (bl->chip == CHIP_BUSLOGIC_PCI) ? 0 : 4;
	HALR->structured.autoSCSIData.uBIOSConfiguration = (bl->has_bios) ? 0x33 : 0x32;
	if (!safe)
	{
		HALR->structured.autoSCSIData.uBIOSConfiguration |= 0x04;
	}

	HALR->structured.autoSCSIData.u16DeviceEnabledMask = 0xffff;
	HALR->structured.autoSCSIData.u16WidePermittedMask = 0xffff;
	HALR->structured.autoSCSIData.u16FastPermittedMask = 0xffff;
	HALR->structured.autoSCSIData.u16DisconnectPermittedMask = 0xffff;

	HALR->structured.autoSCSIData.uPCIInterruptPin = PCI_INTB;
	HALR->structured.autoSCSIData.fVesaBusSpeedGreaterThan33MHz = 1;

	HALR->structured.autoSCSIData.uAutoSCSIMaximumLUN = 7;

	HALR->structured.autoSCSIData.fForceBusDeviceScanningOrder = 1;
	HALR->structured.autoSCSIData.fInt13Extension = safe ? 0 : 1;
	HALR->structured.autoSCSIData.fCDROMBoot = safe ? 0 : 1;
	HALR->structured.autoSCSIData.fMultiBoot = safe ? 0 : 1;
	HALR->structured.autoSCSIData.fAggressiveRoundRobinMode = safe ? 0 : 1;	/* 1 = aggressive, 0 = strict */
}


static void BuslogicInitializeAutoSCSIRam(Buslogic_t *bl)
{
    FILE *f;

    f = nvrfopen(BuslogicGetNVRFileName(bl), L"rb");
    if (f)
    {
	fread(&(bl->LocalRAM.structured.autoSCSIData), 1, 64, f);
	fclose(f);
	f = NULL;
    }
    else
    {
	BuslogicAutoSCSIRamSetDefaults(bl, 0);
    }
}


static void
BuslogicRaiseInterrupt(Buslogic_t *bl, int suppress, uint8_t Interrupt)
{
	if (Interrupt & (INTR_MBIF | INTR_MBOA))
	{
		if (!(bl->Interrupt & INTR_HACC))
		{
			bl->Interrupt |= Interrupt;	/* Report now. */
		}
		else
		{
			bl->PendingInterrupt |= Interrupt;	/* Report later. */
		}
	}
	else if (Interrupt & INTR_HACC)
	{
		if (bl->Interrupt == 0 || bl->Interrupt == (INTR_ANY | INTR_HACC))
		{
			pclog("BuslogicRaiseInterrupt(): Interrupt=%02X\n", bl->Interrupt);
		}
		bl->Interrupt |= Interrupt;
	}
	else
	{
		pclog("BuslogicRaiseInterrupt(): Invalid interrupt state!\n");
	}

	bl->Interrupt |= INTR_ANY;

	if (bl->IrqEnabled && !suppress)
	{
		BuslogicInterrupt(bl, 1);
	}
}


static void
BuslogicClearInterrupt(Buslogic_t *bl)
{
    /* pclog("Buslogic: Lowering Interrupt 0x%02X\n", bl->Interrupt); */
    bl->Interrupt = 0;
    /* pclog("Lowering IRQ %i\n", bl->Irq); */
    BuslogicInterrupt(bl, 0);
    if (bl->PendingInterrupt) {
	/* pclog("Buslogic: Raising Interrupt 0x%02X (Pending)\n", bl->PendingInterrupt); */
	if (bl->MailboxOutInterrupts || !(bl->Interrupt & INTR_MBOA)) {
		BuslogicRaiseInterrupt(bl,  0, bl->PendingInterrupt);
	}
	bl->PendingInterrupt = 0;
    }
}

static void
BuslogicReset(Buslogic_t *bl)
{
    /* pclog("BuslogicReset()\n"); */
    if (bl->evt)
    {
	thread_destroy_event(bl->evt);
	bl->evt = NULL;
	if (poll_tid)
	{
		thread_kill(poll_tid);
		poll_tid = NULL;
	}
    }

    BuslogicResetCallback = 0;
    bl->scan_restart = 0;

    bl->Geometry = 0x80;
    bl->Status = STAT_IDLE | STAT_INIT;
    bl->Command = 0xFF;
    bl->CmdParam = 0;
    bl->CmdParamLeft = 0;
    bl->IrqEnabled = 1;
    bl->ExtendedLUNCCBFormat = 0;
    bl->MailboxOutPosCur = 0;
    bl->MailboxInPosCur = 0;
    bl->MailboxOutInterrupts = 0;
    bl->PendingInterrupt = 0;
    bl->Lock = 0;

    BuslogicClearInterrupt(bl);
}


static void
BuslogicResetControl(Buslogic_t *bl, uint8_t Reset)
{
	/* pclog("BuslogicResetControl()\n");	 */
    BuslogicReset(bl);
    if (Reset) {
	bl->Status |= STAT_STST;
	bl->Status &= ~STAT_IDLE;
    }
    BuslogicResetCallback = BUSLOGIC_RESET_DURATION_US * TIMER_USEC;
}


static void
BuslogicCommandComplete(Buslogic_t *bl, int suppress)
{
	pclog("BuslogicCommandComplete()\n");
	bl->DataReply = 0;
	bl->Status |= STAT_IDLE;

	if (bl->Command != 0x02)
	{
		bl->Status &= ~STAT_DFULL;

		pclog("BuslogicCommandComplete(): Raising IRQ %i\n", bl->Irq);
		BuslogicRaiseInterrupt(bl, suppress, INTR_HACC);
   	}

	bl->Command = 0xFF;
	bl->CmdParam = 0;	
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
	pclog("CCB rewritten to the CDB (pointer %08X, length 18)\n", CCBPointer);
	DMAPageWrite(CCBPointer, (char *)CmdBlock, 18);
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

    BuslogicRaiseInterrupt(bl, 0, INTR_MBIF | INTR_ANY);

    while (bl->Interrupt) {
	thread_wait_event(bl->evt, 10);
    }
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
BuslogicSCSIBIOSDataBufferAllocate(ESCMD *ESCSICmd, uint8_t TargetID, uint8_t LUN)
{
    uint32_t DataPointer, DataLength;
	
	DataPointer = ESCSICmd->DataPointer;
	DataLength = ESCSICmd->DataLength;		

    pclog("BIOS Data Buffer write: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);	

    if (SCSIDevices[TargetID][LUN].CmdBuffer != NULL)
    {
	free(SCSIDevices[TargetID][LUN].CmdBuffer);
	SCSIDevices[TargetID][LUN].CmdBuffer = NULL;
    }

    if ((ESCSICmd->DataDirection != 0x03) && DataLength) 
	{
		uint32_t Address = DataPointer;

		SCSIDevices[TargetID][LUN].InitLength = DataLength;

		SCSIDevices[TargetID][LUN].CmdBuffer = (uint8_t *) malloc(DataLength);
		memset(SCSIDevices[TargetID][LUN].CmdBuffer, 0, DataLength);

		if (DataLength > 0) {
			DMAPageRead(Address,
					(char *)SCSIDevices[TargetID][LUN].CmdBuffer,
					SCSIDevices[TargetID][LUN].InitLength);
		}
	}
}

static void
BuslogicSCSIBIOSDataBufferFree(ESCMD *ESCSICmd, uint8_t TargetID, uint8_t LUN)
{
    uint32_t DataPointer = 0;
    uint32_t DataLength = 0;
    uint32_t Address;
    uint32_t Residual;

	DataPointer = ESCSICmd->DataPointer;
	DataLength = ESCSICmd->DataLength;		

    if ((DataLength != 0) && (ESCSICmd->CDB[0] == GPCMD_TEST_UNIT_READY)) {
	pclog("Data length not 0 with TEST UNIT READY: %i (%i)\n",
		DataLength, SCSIDevices[TargetID][LUN].InitLength);
    }

    if (ESCSICmd->CDB[0] == GPCMD_TEST_UNIT_READY) {
	DataLength = 0;
    }

    pclog("BIOS Data Buffer read: length %d, pointer 0x%04X\n",
				DataLength, DataPointer);

    /* If the control byte is 0x00, it means that the transfer direction is set up by the SCSI command without
       checking its length, so do this procedure for both read/write commands. */
    if ((DataLength > 0) &&
        ((ESCSICmd->DataDirection == CCB_DATA_XFER_IN) ||
	(ESCSICmd->DataDirection == 0x00))) 
	{
		Address = DataPointer;

		pclog("BusLogic BIOS DMA: Writing %i bytes at %08X\n", DataLength, Address);
		DMAPageWrite(Address, (char *)SCSIDevices[TargetID][LUN].CmdBuffer, DataLength);
    }

	/* Should be 0 when scatter/gather? */
	if (DataLength >= SCSIDevices[TargetID][LUN].InitLength) {
		Residual = DataLength;
		Residual -= SCSIDevices[TargetID][LUN].InitLength;
	} else {
		Residual = 0;
	}

	ESCSICmd->DataLength = Residual;
	pclog("BIOS Residual data length for reading: %d\n",
			ESCSICmd->DataLength);

    if (SCSIDevices[TargetID][LUN].CmdBuffer != NULL)
    {
	free(SCSIDevices[TargetID][LUN].CmdBuffer);
	SCSIDevices[TargetID][LUN].CmdBuffer = NULL;
    }
}

static void
BuslogicSCSIBIOSRequestSetup(Buslogic_t *bl, uint8_t *CmdBuf, uint8_t *DataInBuf, uint8_t DataReply)
{	
	ESCMD *ESCSICmd = (ESCMD *)CmdBuf;
	uint32_t i;
	uint8_t temp_cdb[12];
        int target_cdb_len = 12;
        uint8_t target_id = 0;

	DataInBuf[0] = DataInBuf[1] = 0;

    if ((ESCSICmd->TargetId > 15) || (ESCSICmd->LogicalUnit > 7)) {
	DataInBuf[2] = CCB_INVALID_CCB;
	DataInBuf[3] = SCSI_STATUS_OK;
	return;
    }
		
    pclog("Scanning SCSI Target ID %i\n", ESCSICmd->TargetId);		

    SCSIStatus = SCSI_STATUS_OK;
    SCSIDevices[ESCSICmd->TargetId][ESCSICmd->LogicalUnit].InitLength = 0;

    BuslogicSCSIBIOSDataBufferAllocate(ESCSICmd, ESCSICmd->TargetId, ESCSICmd->LogicalUnit);

    if (!scsi_device_present(ESCSICmd->TargetId, ESCSICmd->LogicalUnit)) {
	pclog("SCSI Target ID %i and LUN %i have no device attached\n",ESCSICmd->TargetId,ESCSICmd->LogicalUnit);
	BuslogicSCSIBIOSDataBufferFree(ESCSICmd, ESCSICmd->TargetId, ESCSICmd->LogicalUnit);
	/* BuslogicSCSIBIOSSenseBufferFree(ESCSICmd, Id, Lun, 0, 0); */
	DataInBuf[2] = CCB_SELECTION_TIMEOUT;
	DataInBuf[3] = SCSI_STATUS_OK;
    } else {
	pclog("SCSI Target ID %i and LUN %i detected and working\n", ESCSICmd->TargetId, ESCSICmd->LogicalUnit);

	pclog("Transfer Control %02X\n", ESCSICmd->DataDirection);
	pclog("CDB Length %i\n", ESCSICmd->CDBLength);	
	if (ESCSICmd->DataDirection > 0x03) {
		pclog("Invalid control byte: %02X\n",
			ESCSICmd->DataDirection);
	}
    }

    target_cdb_len = scsi_device_cdb_length(ESCSICmd->TargetId, ESCSICmd->LogicalUnit);

    if (!scsi_device_valid(ESCSICmd->TargetId, ESCSICmd->LogicalUnit))  fatal("SCSI target on %02i:%02i has disappeared\n", ESCSICmd->TargetId, ESCSICmd->LogicalUnit);

    pclog("SCSI target command being executed on: SCSI ID %i, SCSI LUN %i, Target %i\n", ESCSICmd->TargetId, ESCSICmd->LogicalUnit, target_id);

    pclog("SCSI Cdb[0]=0x%02X\n", ESCSICmd->CDB[0]);
    for (i = 1; i < ESCSICmd->CDBLength; i++) {
	pclog("SCSI Cdb[%i]=%i\n", i, ESCSICmd->CDB[i]);
    }

    memset(temp_cdb, 0, target_cdb_len);
    if (ESCSICmd->CDBLength <= target_cdb_len) {
	memcpy(temp_cdb, ESCSICmd->CDB, ESCSICmd->CDBLength);
    } else {
	memcpy(temp_cdb, ESCSICmd->CDB, target_cdb_len);
    }

    scsi_device_command(ESCSICmd->TargetId, ESCSICmd->LogicalUnit, ESCSICmd->CDBLength, temp_cdb);

    BuslogicSCSIBIOSDataBufferFree(ESCSICmd, ESCSICmd->TargetId, ESCSICmd->LogicalUnit);
    /* BuslogicSCSIBIOSSenseBufferFree(ESCSICmd, Id, Lun, (SCSIStatus != SCSI_STATUS_OK), 1); */

    pclog("BIOS Request complete\n");

    if (SCSIStatus == SCSI_STATUS_OK) {
	DataInBuf[2] = CCB_COMPLETE;
	DataInBuf[3] = SCSI_STATUS_OK;
    } else if (SCSIStatus == SCSI_STATUS_CHECK_CONDITION) {
	DataInBuf[2] = CCB_COMPLETE;
	DataInBuf[3] = SCSI_STATUS_CHECK_CONDITION;			
    }
	
	bl->DataReplyLeft = DataReply;	
}

static uint8_t
BuslogicRead(uint16_t Port, void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;
    uint8_t Temp;

    switch (Port & 3) {
	case 0:
	default:
		Temp = bl->Status;
		break;
		
	case 1:
		Temp = bl->DataBuf[bl->DataReply];
		if (bl->DataReplyLeft) 
		{
			bl->DataReply++;
			bl->DataReplyLeft--;
			if (!bl->DataReplyLeft) {
				BuslogicCommandComplete(bl, 0);
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

    /* if (Port < 0x1000) { */
    if (Port & 3) {
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


static uint8_t
BuslogicMemRead(uint32_t addr, void *p)
{
    pclog("BuslogicMemRead(%08X, %08X)\n", addr, p);
    return BuslogicRead(addr & 3, p);
}


static uint16_t
BuslogicMemReadW(uint32_t addr, void *p)
{
    pclog("BuslogicMemReadW(%08X, %08X)\n", addr, p);
    return BuslogicReadW(addr & 3, p);
}

static uint32_t
BuslogicMemReadL(uint32_t addr, void *p)
{
    pclog("BuslogicMemReadL(%08X, %08X)\n", addr, p);
    return BuslogicReadL(addr & 3, p);
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
    BIOSCMD *BiosCmd;
    ReplyInquireSetupInformation *ReplyISI;
    MailboxInitExtended_t *MailboxInitE;
    ReplyInquireExtendedSetupInformation *ReplyIESI;
    BuslogicPCIInformation_t *ReplyPI;
    char aModelName[] = "542B ";  /* Trailing \0 is fine, that's the filler anyway. */
    int cCharsToTransfer;
    int suppress = 0;
    uint16_t cyl = 0;
    FILE *f;

    pclog("Buslogic: Write Port 0x%02X, Value %02X\n", Port, Val);

    switch (Port & 3) 
	{
	case 0:
		if ((Val & CTRL_HRST) || (Val & CTRL_SRST)) 
		{	
			uint8_t Reset = (Val & CTRL_HRST);
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
				if (!poll_tid) {
					pclog("Buslogic: starting thread..\n");
					poll_tid = thread_create(BuslogicCommandThread, bl);
					bl->scan_restart = 0;
				}
				else {
					bl->scan_restart = 1;
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

				case 0x03:
					bl->CmdParamLeft = 10;
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
					bl->CmdParamLeft = 1;
					break;
					
				case 0x21:
					bl->CmdParamLeft = 5;
					break;	

				case 0x1A:
				case 0x1B:
					bl->CmdParamLeft = 3;
					break;

				case 0x06:
					bl->CmdParamLeft = 4;
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
					
				case 0x83:
					bl->CmdParamLeft = 12;
					break;

				case 0x8C:
					bl->CmdParamLeft = 1;
					break;

				case 0x90:	
					bl->CmdParamLeft = 2;
					break;
					
				case 0x91:
					bl->CmdParamLeft = 2;
					break;
					
				case 0x92:
					bl->CmdParamLeft = 1;
					break;
					
				case 0x94:
					bl->CmdParamLeft = 3;
					break;
				
				case 0x95: /* Valid only for PCI */
					bl->CmdParamLeft = (bl->chip == CHIP_BUSLOGIC_PCI) ? 1 : 0;
					break;

				case 0x97: /* Valid only for PCI */
				case 0xA7: /* Valid only for PCI */
					bl->CmdParamLeft = (bl->chip == CHIP_BUSLOGIC_PCI) ? 10 : 0;

				case 0xA8: /* Valid only for PCI */
				case 0xA9: /* Valid only for PCI */
					bl->CmdParamLeft = (bl->chip == CHIP_BUSLOGIC_PCI) ? 4 : 0;
					break;
			}
		} else {
			bl->CmdBuf[bl->CmdParam] = Val;
			bl->CmdParam++;
			bl->CmdParamLeft--;

			if ((bl->CmdParam == 2) && (bl->Command == 0x90))
			{
				bl->CmdParamLeft = bl->CmdBuf[1];
			}

			if ((bl->CmdParam == 10) && ((bl->Command == 0x97) || (bl->Command == 0xA7)))
			{
				bl->CmdParamLeft = bl->CmdBuf[6];
				bl->CmdParamLeft <<= 8;
				bl->CmdParamLeft |= bl->CmdBuf[7];
				bl->CmdParamLeft <<= 8;
				bl->CmdParamLeft |= bl->CmdBuf[8];
			}

			if ((bl->CmdParam == 4) && (bl->Command == 0xA9))
			{
				bl->CmdParamLeft = bl->CmdBuf[3];
				bl->CmdParamLeft <<= 8;
				bl->CmdParamLeft |= bl->CmdBuf[2];
			}
		}
		
		if (!bl->CmdParamLeft) 
		{
			pclog("Running Operation Code 0x%02X\n", bl->Command);
			bl->DataReply = 0;
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
					BiosCmd = (BIOSCMD *)bl->CmdBuf;

					if (bl->chip != CHIP_BUSLOGIC_MCA) {
						cyl = ((BiosCmd->u.chs.cyl & 0xff) << 8) | ((BiosCmd->u.chs.cyl >> 8) & 0xff);
						BiosCmd->u.chs.cyl = cyl;						
					}

					if (bl->chip == CHIP_BUSLOGIC_MCA) {
						pclog("BIOS LBA=%06lx (%lu)\n",
							lba32_blk(BiosCmd),
							lba32_blk(BiosCmd));
					} else {
						BiosCmd->u.chs.head &= 0xf;
						BiosCmd->u.chs.sec &= 0x1f;
						pclog("BIOS CHS=%04X/%02X%02X\n",
							BiosCmd->u.chs.cyl,
							BiosCmd->u.chs.head,
							BiosCmd->u.chs.sec);
					}

					BiosCmd->u.chs.cyl = cyl;
					BiosCmd->u.chs.head &= 0x0f;
					BiosCmd->u.chs.sec &= 0x1f;
					pclog("C: %04X, H: %02X, S: %02X\n", BiosCmd->u.chs.cyl, BiosCmd->u.chs.head, BiosCmd->u.chs.sec);
					bl->DataBuf[0] = scsi_bios_command(15, BiosCmd, (bl->chip == CHIP_BUSLOGIC_MCA) ? 1 : 0);
					pclog("BIOS Completion/Status Code %x\n", bl->DataBuf[0]);
					bl->DataReplyLeft = 1;
					break;

				case 0x04:
					pclog("Inquire Board\n");
					bl->DataBuf[0] = (bl->chip == CHIP_BUSLOGIC_MCA) ? 0x42 : 0x41;
					bl->DataBuf[1] = 0x41;
					bl->DataBuf[2] = (bl->chip == CHIP_BUSLOGIC_PCI) ? '5' : '4';
					bl->DataBuf[3] = (bl->chip == CHIP_BUSLOGIC_PCI) ? '0' : '2';
					bl->DataReplyLeft = 4;
					break;

				case 0x05:
					if (bl->CmdBuf[0] <= 1) {
						bl->MailboxOutInterrupts = bl->CmdBuf[0];
						pclog("Mailbox out interrupts: %s\n", bl->MailboxOutInterrupts ? "ON" : "OFF");
						suppress = 1;
					} else {
						bl->Status |= STAT_INVCMD;
					}
					bl->DataReplyLeft = 0;
					break;
						
				case 0x06:
					pclog("Selection Time-Out\n");
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
					for (i=0; i<8; i++) {
					    bl->DataBuf[i] = 0;
					    for (j=0; j<8; j++) {
						if (scsi_device_present(i, j) && (i != bl->LocalRAM.structured.autoSCSIData.uSCSIId))
						    bl->DataBuf[i] |= (1 << j);
					    }
					}
					bl->DataReplyLeft = 8;
					break;				

				case 0x0B:
					pclog("Inquire Configuration\n");
					bl->DataBuf[0] = (1 << bl->DmaChannel);
					if ((bl->Irq >= 9) && (bl->Irq <= 15))
					{
						bl->DataBuf[1] = (1<<(bl->Irq-9));
					}
					else
						bl->DataBuf[1] = 0;
					{
					}
					bl->DataBuf[2] = bl->LocalRAM.structured.autoSCSIData.uSCSIId;	/* HOST ID */
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
					switch(bl->chip)
					{
						case CHIP_BUSLOGIC_ISA:
							ReplyISI->uHostBusType = 'A';
							break;
						case CHIP_BUSLOGIC_MCA:
							ReplyISI->uHostBusType = 'B';
							break;
						case CHIP_BUSLOGIC_VLB:
							ReplyISI->uHostBusType = 'E';
							break;
						case CHIP_BUSLOGIC_PCI:
							ReplyISI->uHostBusType = 'F';
							break;
					}

					pclog("Return Setup Information: %d\n", bl->CmdBuf[0]);
				}
				break;

				case 0x1A:
				{
					uint32_t FIFOBuf;
					addr24 Address;
					
					bl->DataReplyLeft = 0;
					Address.hi = bl->CmdBuf[0];
					Address.mid = bl->CmdBuf[1];
					Address.lo = bl->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					pclog("Buslogic LocalRAM: Reading 64 bytes at %08X\n", FIFOBuf);
					DMAPageRead(FIFOBuf, (char *)bl->LocalRAM.u8View, 64);
				}
				break;
						
				case 0x1B:
				{
					uint32_t FIFOBuf;
					addr24 Address;
					
					bl->DataReplyLeft = 0;
					Address.hi = bl->CmdBuf[0];
					Address.mid = bl->CmdBuf[1];
					Address.lo = bl->CmdBuf[2];
					FIFOBuf = ADDR_TO_U32(Address);
					pclog("Buslogic LocalRAM: Writing 64 bytes at %08X\n", FIFOBuf);
					DMAPageWrite(FIFOBuf, (char *)bl->LocalRAM.u8View, 64);
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
					for (i = 8; i < 15; i++) {
					    bl->DataBuf[i-8] = 0;
					    for (j=0; j<8; j++) {
						if (scsi_device_present(i, j) && (i != bl->LocalRAM.structured.autoSCSIData.uSCSIId))
						    bl->DataBuf[i-8] |= (1<<j);
					    }
					}
					bl->DataReplyLeft = 8;
					break;

				case 0x24:
				{
					uint16_t TargetsPresentMask = 0;
							
					for (i=0; i<15; i++) {
						if (scsi_device_present(i, 0) && (i != bl->LocalRAM.structured.autoSCSIData.uSCSIId))
						    TargetsPresentMask |= (1 << i);
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
					/* pclog("Lowering IRQ %i\n", bl->Irq);
					BuslogicInterrupt(bl, 0); */
					suppress = 1;
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

				case 0x83:
					if (bl->CmdParam == 12)
					{
						bl->CmdParamLeft = bl->CmdBuf[11];
						pclog("Execute SCSI BIOS Command: %u more bytes follow\n", bl->CmdParamLeft);
					}
					else
					{
						pclog("Execute SCSI BIOS Command: received %u bytes\n", bl->CmdBuf[0]);
						BuslogicSCSIBIOSRequestSetup(bl, bl->CmdBuf, bl->DataBuf, 4);				
					}
					break;
				
				case 0x84:
					bl->DataBuf[0] = (bl->chip == CHIP_BUSLOGIC_PCI) ? '7' : '1';
					bl->DataReplyLeft = 1;
					break;				

				case 0x85:
					bl->DataBuf[0] = (bl->chip == CHIP_BUSLOGIC_PCI) ? 'B' : 'E';
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
					switch(bl->chip)
					{
						case CHIP_BUSLOGIC_ISA:
							aModelName[0] = '5';
							aModelName[1] = '4';
							aModelName[2] = '5';
							aModelName[3] = 'C';
							break;
						case CHIP_BUSLOGIC_MCA:
							aModelName[0] = '6';
							aModelName[1] = '4';
							aModelName[2] = '0';
							aModelName[3] = 'A';
							break;
						case CHIP_BUSLOGIC_VLB:
							aModelName[0] = '4';
							aModelName[1] = '4';
							aModelName[2] = '5';
							aModelName[3] = 'S';
							break;
						case CHIP_BUSLOGIC_PCI:
							aModelName[0] = '9';
							aModelName[1] = '5';
							aModelName[2] = '8';
							aModelName[3] = 'D';
							break;
					}
					cCharsToTransfer =   bl->DataReplyLeft <= sizeof(aModelName)
							? bl->DataReplyLeft
							: sizeof(aModelName);

					for (i = 0; i < cCharsToTransfer; i++)
						bl->DataBuf[i] = aModelName[i];
					
					pclog("Model Name\n");
					pclog("Buffer 0: %x\n", bl->DataBuf[0]);
					pclog("Buffer 1: %x\n", bl->DataBuf[1]);
					pclog("Buffer 2: %x\n", bl->DataBuf[2]);
					pclog("Buffer 3: %x\n", bl->DataBuf[3]);
					pclog("Buffer 4: %x\n", bl->DataBuf[4]);
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

					switch (bl->chip)
					{
						case CHIP_BUSLOGIC_ISA:
						case CHIP_BUSLOGIC_VLB:
							ReplyIESI->uBusType = 'A';				   /* ISA style */
							break;
						case CHIP_BUSLOGIC_MCA:
							ReplyIESI->uBusType = 'M';				   /* MCA style */
							break;
						case CHIP_BUSLOGIC_PCI:
							ReplyIESI->uBusType = 'E';				   /* PCI style */
							break;
					}
					ReplyIESI->uBiosAddress = 0xd8;
					ReplyIESI->u16ScatterGatherLimit = 8192;
					ReplyIESI->cMailbox = bl->MailboxCount;
					ReplyIESI->uMailboxAddressBase = bl->MailboxOutAddr;
					ReplyIESI->fHostWideSCSI = 1;						  /* This should be set for the BT-542B as well. */
					ReplyIESI->fLevelSensitiveInterrupt = bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt;
					if (bl->chip == CHIP_BUSLOGIC_PCI) {
						ReplyIESI->fHostUltraSCSI = 1;
					}
					memcpy(ReplyIESI->aFirmwareRevision, (bl->chip == CHIP_BUSLOGIC_PCI) ? "07B" : "21E", sizeof(ReplyIESI->aFirmwareRevision));
					pclog("Return Extended Setup Information: %d\n", bl->CmdBuf[0]);
					break;

				/* VirtualBox has these two modes implemented in reverse.
				   According to the BusLogic datasheet:
				   0 is the strict round robin mode, which is also the one used by the AHA-154x according to the
				   Adaptec specification;
				   1 is the aggressive round robin mode, which "hunts" for an active outgoing mailbox and then
				   processes it. */
				case 0x8F:
					bl->LocalRAM.structured.autoSCSIData.fAggressiveRoundRobinMode = bl->CmdBuf[0] & 1;

					bl->DataReplyLeft = 0;
					break;

				case 0x90:	
					pclog("Store Local RAM\n");
					Offset = bl->CmdBuf[0];
					bl->DataReplyLeft = 0;
					memcpy(&(bl->LocalRAM.u8View[Offset]), &(bl->CmdBuf[2]), bl->CmdBuf[1]);
	
					bl->DataReply = 0;
					break;
				
				case 0x91:
					pclog("Fetch Local RAM\n");
					Offset = bl->CmdBuf[0];
					bl->DataReplyLeft = bl->CmdBuf[1];
					memcpy(bl->DataBuf, &(bl->LocalRAM.u8View[Offset]), bl->CmdBuf[1]);

					bl->DataReply = 0;
					break;

				case 0x92:
					bl->DataReplyLeft = 0;

					switch (bl->CmdBuf[0])
					{
						case 0:
						case 2:
							BuslogicAutoSCSIRamSetDefaults(bl, 0);
							break;
						case 3:
							BuslogicAutoSCSIRamSetDefaults(bl, 3);
							break;
						case 1:
							f = nvrfopen(BuslogicGetNVRFileName(bl), L"wb");
							if (f)
							{
								fwrite(&(bl->LocalRAM.structured.autoSCSIData), 1, 64, f);
								fclose(f);
								f = NULL;
							}
							break;
						default:
							bl->Status |= STAT_INVCMD;
							break;
					}
					break;

				case 0x94:
					if (bl->CmdBuf[0])
					{
						pclog("Invalid AutoSCSI command mode %x\n", bl->CmdBuf[0]);
						bl->DataReplyLeft = 0;
						bl->Status |= STAT_INVCMD;
					}
					else
					{
						bl->DataReplyLeft = bl->CmdBuf[2];
						bl->DataReplyLeft <<= 8;
						bl->DataReplyLeft |= bl->CmdBuf[1];
						memcpy(bl->DataBuf, bl->AutoSCSIROM, bl->DataReplyLeft);
					}
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
					suppress = 1;
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

				case 0x97:
				case 0xA7:
					/* TODO: Actually correctly implement this whole SCSI BIOS Flash stuff. */
					bl->DataReplyLeft = 0;
					break;
					
				case 0xA8:
					if (bl->chip != CHIP_BUSLOGIC_PCI)
					{
						bl->DataReplyLeft = 0;
						bl->Status |= STAT_INVCMD;
						break;
					}

					Offset = bl->CmdBuf[1];
					Offset <<= 8;
					Offset |= bl->CmdBuf[0];

					bl->DataReplyLeft = bl->CmdBuf[3];
					bl->DataReplyLeft <<= 8;
					bl->DataReplyLeft |= bl->CmdBuf[2];

					memcpy(bl->DataBuf, &(bl->SCAMData[Offset]), bl->DataReplyLeft);

					bl->DataReply = 0;
					break;

				case 0xA9:
					if (bl->chip != CHIP_BUSLOGIC_PCI)
					{
						bl->DataReplyLeft = 0;
						bl->Status |= STAT_INVCMD;
						break;
					}

					Offset = bl->CmdBuf[1];
					Offset <<= 8;
					Offset |= bl->CmdBuf[0];

					bl->DataReplyLeft = bl->CmdBuf[3];
					bl->DataReplyLeft <<= 8;
					bl->DataReplyLeft |= bl->CmdBuf[2];

					memcpy(&(bl->SCAMData[Offset]), &(bl->CmdBuf[4]), bl->DataReplyLeft);
					bl->DataReplyLeft = 0;

					bl->DataReply = 0;
					break;

				default:
					pclog("Invalid command %x\n", bl->Command);
					bl->DataReplyLeft = 0;
					bl->Status |= STAT_INVCMD;
					break;
			}
		}

		if (bl->DataReplyLeft)
		{
			bl->Status |= STAT_DFULL;
			pclog("Data Full\n");
		}
		else if (!bl->CmdParamLeft)
		{
			BuslogicCommandComplete(bl, suppress);
			pclog("No Command Parameters Left, completing command\n");
		}
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


static void
BuslogicMemWrite(uint32_t addr, uint8_t Val, void *p)
{
    pclog("BuslogicMemWrite(%08X, %02X, %08X)\n", addr, Val, p);
    BuslogicWrite(addr & 3, Val, p);
}


static void
BuslogicMemWriteW(uint32_t addr, uint16_t Val, void *p)
{
    pclog("BuslogicMemWriteW(%08X, %04X, %08X)\n", addr, Val, p);
    BuslogicWriteW(addr & 3, Val, p);
}


static void
BuslogicMemWriteL(uint32_t addr, uint32_t Val, void *p)
{
    pclog("BuslogicMemWriteL(%08X, %08X, %08X)\n", addr, Val, p);
    BuslogicWriteL(addr & 3, Val, p);
}


static void
BuslogicSenseBufferFree(Req_t *req, int Copy)
{
    uint8_t SenseLength = BuslogicConvertSenseLength(req->CmdBlock.common.RequestSenseLength);
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

	pclog("BuslogicSenseBufferFree(): Request Sense address: %02X\n", SenseBufferAddress);

	pclog("BuslogicSenseBufferFree(): Writing %i bytes at %08X\n",
					SenseLength, SenseBufferAddress);
	DMAPageWrite(SenseBufferAddress, (char *)temp_sense, SenseLength);
	pclog("BuslogicSenseBufferFree(): Sense data written to buffer: %02X %02X %02X\n",
		temp_sense[2], temp_sense[12], temp_sense[13]);
    }
}


static void
BuslogicSCSICommand(Buslogic_t *bl)
{
    Req_t *req = &bl->Req;
    uint8_t Id, Lun;
    uint8_t temp_cdb[12];
    uint32_t i;
    int target_cdb_len = 12;

    Id = req->TargetID;
    Lun = req->LUN;

    target_cdb_len = scsi_device_cdb_length(Id, Lun);

    if (!scsi_device_valid(Id, Lun))  fatal("BuslogicSCSICommand(): Target on %02i:%02i has disappeared\n", Id, Lun);

    pclog("BuslogicSCSICommand(): Target command being executed on: SCSI ID %i, SCSI LUN %i\n", Id, Lun);

    for (i = 0; i < req->CmdBlock.common.CdbLength; i++) {
	pclog("BuslogicSCSICommand(): SCSI Cdb[%i]=%i\n", i, req->CmdBlock.common.Cdb[i]);
    }

    memset(temp_cdb, 0, target_cdb_len);
    if (req->CmdBlock.common.CdbLength <= target_cdb_len) {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb,
		req->CmdBlock.common.CdbLength);
    } else {
	memcpy(temp_cdb, req->CmdBlock.common.Cdb, target_cdb_len);
    }

    scsi_device_command(Id, Lun, req->CmdBlock.common.CdbLength, temp_cdb);

    BuslogicDataBufferFree(req);

    BuslogicSenseBufferFree(req, (SCSIStatus != SCSI_STATUS_OK));

    pclog("BuslogicSCSICommand(): Request complete\n");

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

    /* Fetch data from the Command Control Block. */
    DMAPageRead(CCBPointer, (char *)&req->CmdBlock, sizeof(CCB32));

    req->Is24bit = bl->Mbx24bit;
    req->CCBPointer = CCBPointer;
    req->TargetID = bl->Mbx24bit ? req->CmdBlock.old.Id : req->CmdBlock.new.Id;
    req->LUN = bl->Mbx24bit ? req->CmdBlock.old.Lun : req->CmdBlock.new.Lun;

    if (!bl->Mbx24bit)
    {
	if (req->CmdBlock.new.TagQueued || req->CmdBlock.new.LegacyTagEnable) {
		fatal("BuslogicSCSIRequestSetup(): Attempting to queue tags\n");
		BuslogicMailboxInSetup(bl, CCBPointer, &req->CmdBlock,
				       CCB_INVALID_CCB, SCSI_STATUS_OK, MBI_ERROR);
		pclog("BusLogic Callback: Send incoming mailbox\n");
		BuslogicMailboxIn(bl);
		return;
	}
    }
 
    Id = req->TargetID;
    Lun = req->LUN;
    if ((Id > 15) || (Lun > 7)) {
	BuslogicMailboxInSetup(bl, CCBPointer, &req->CmdBlock,
			       CCB_INVALID_CCB, SCSI_STATUS_OK, MBI_ERROR);
	return;

	pclog("BusLogic Callback: Send incoming mailbox\n");
	BuslogicMailboxIn(bl);
    }
	
    pclog("BuslogicSCSIRequestSetup(): Scanning SCSI Target ID %i\n", Id);		

    SCSIStatus = SCSI_STATUS_OK;
    SCSIDevices[Id][Lun].InitLength = 0;

    BuslogicDataBufferAllocate(req, req->Is24bit);

    if (!scsi_device_present(Id, Lun)) {
	pclog("BuslogicSCSIRequestSetup(): SCSI Target ID %i and LUN %i have no device attached\n",Id,Lun);
	BuslogicDataBufferFree(req);
	BuslogicSenseBufferFree(req, 0);
	BuslogicMailboxInSetup(bl, CCBPointer, &req->CmdBlock,
			       CCB_SELECTION_TIMEOUT,SCSI_STATUS_OK,MBI_ERROR);

	pclog("BusLogic Callback: Send incoming mailbox\n");
	BuslogicMailboxIn(bl);
    } else {
	pclog("BuslogicSCSIRequestSetup(): SCSI Target ID %i and LUN %i detected and working\n", Id, Lun);

	pclog("BuslogicSCSIRequestSetup(): Transfer Control %02X\n", req->CmdBlock.common.ControlByte);
	pclog("BuslogicSCSIRequestSetup(): CDB Length %i\n", req->CmdBlock.common.CdbLength);	
	pclog("BuslogicSCSIRequestSetup(): CCB Opcode %x\n", req->CmdBlock.common.Opcode);		
	if (req->CmdBlock.common.ControlByte > 0x03) {
		pclog("BuslogicSCSIRequestSetup(): Invalid control byte: %02X\n",
			req->CmdBlock.common.ControlByte);
	}

	pclog("BusLogic Callback: Process SCSI request\n");
	BuslogicSCSICommand(bl);

	pclog("BusLogic Callback: Send incoming mailbox\n");
	BuslogicMailboxIn(bl);
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

    pclog("BusLogic Callback: Send incoming mailbox\n");
    BuslogicMailboxIn(bl);
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


static uint8_t
BuslogicProcessMailbox(Buslogic_t *bl)
{
    Mailbox32_t mb32;
    uint32_t Outgoing;
    uint8_t CmdStatus = MBO_FREE;
    uint32_t CodeOffset = 0;

    CodeOffset = bl->Mbx24bit ? 0 : 7;

#if 0
    pclog("BuslogicProcessMailbox(): Operating in %s mode\n", bl->LocalRAM.structured.autoSCSIData.fAggressiveRoundRobinMode ? "aggressive" : "strict");
#endif

    /* 0 = strict, 1 = aggressive */
    if (bl->LocalRAM.structured.autoSCSIData.fAggressiveRoundRobinMode) {
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
    else
    {
	return 0;
    }

    if (bl->MailboxOutInterrupts) {
	BuslogicRaiseInterrupt(bl, 0, INTR_MBOA | INTR_ANY);

	while (bl->Interrupt) {
		thread_wait_event(bl->evt, 10);
	}
    }

#if 0
    pclog("BuslogicProcessMailbox(): Outgoing mailbox action code: %i\n", mb32.u.out.ActionCode);
#endif

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
    if (! bl->LocalRAM.structured.autoSCSIData.fAggressiveRoundRobinMode)
	BuslogicMailboxOutAdvance(bl);

    return 1;
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
BuslogicCommandThread(void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;

BuslogicEventRestart:
    /* Create a waitable event. */
    bl->evt = thread_create_event();

BuslogicScanRestart:
    while (BuslogicProcessMailbox(bl) && bl->MailboxCount)
    {
	thread_wait_event(bl->evt, 10);
    }

    if (!bl->MailboxCount)
    {
	thread_destroy_event(bl->evt);
	bl->evt = NULL;
	poll_tid = NULL;
	return;
    }

    if (bl->scan_restart)
    {
	bl->scan_restart = 0;
	goto BuslogicScanRestart;
    }

    thread_destroy_event(bl->evt);
    bl->evt = NULL;

    if (bl->scan_restart)
    {
	bl->scan_restart = 0;
	goto BuslogicEventRestart;
    }

    poll_tid = NULL;

    pclog("Buslogic: polling stopped.\n");
}


uint8_t	buslogic_pci_regs[256];
bar_t	buslogic_pci_bar[3];


static void
BuslogicBIOSUpdate(Buslogic_t *bl)
{
    int bios_enabled = buslogic_pci_bar[2].addr_regs[0] & 0x01;

    if (!bl->has_bios) {
	return;
    }

    /* PCI BIOS stuff, just enable_disable. */
    if ((bl->bios_addr > 0) && bios_enabled) {
	mem_mapping_enable(&bl->bios.mapping);
	mem_mapping_set_addr(&bl->bios.mapping,
			     bl->bios_addr, bl->bios_size);
	pclog("BT-958D: BIOS now at: %06X\n", bl->bios_addr);
    } else {
	pclog("BT-958D: BIOS disabled\n");
	mem_mapping_disable(&bl->bios.mapping);
    }
}

static uint8_t
BuslogicPCIRead(int func, int addr, void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;

    pclog("BT-958D: Reading register %02X\n", addr & 0xff);

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
		return buslogic_pci_regs[0x04] & 0x03;	/*Respond to IO and memory accesses*/
	case 0x05:
		return 0;
	case 0x07:
		return 2;
	case 0x08:
		return 1;			/*Revision ID*/
	case 0x09:
		return 0;			/*Programming interface*/
	case 0x0A:
		return 0;			/*Subclass*/
	case 0x0B:
		return 1;			/*Class code*/
	case 0x0E:
		return 0;			/*Header type */
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
	case 0x30:			/* PCI_ROMBAR */
		pclog("BT-958D: BIOS BAR 00 = %02X\n", buslogic_pci_bar[2].addr_regs[0] & 0x01);
		return buslogic_pci_bar[2].addr_regs[0] & 0x01;
	case 0x31:			/* PCI_ROMBAR 15:11 */
		pclog("BT-958D: BIOS BAR 01 = %02X\n", (buslogic_pci_bar[2].addr_regs[1] & bl->bios_mask));
		return buslogic_pci_bar[2].addr_regs[1];
		break;
	case 0x32:			/* PCI_ROMBAR 23:16 */
		pclog("BT-958D: BIOS BAR 02 = %02X\n", buslogic_pci_bar[2].addr_regs[2]);
		return buslogic_pci_bar[2].addr_regs[2];
		break;
	case 0x33:			/* PCI_ROMBAR 31:24 */
		pclog("BT-958D: BIOS BAR 03 = %02X\n", buslogic_pci_bar[2].addr_regs[3]);
		return buslogic_pci_bar[2].addr_regs[3];
		break;
	case 0x3C:
		return bl->Irq;
	case 0x3D:
		return PCI_INTB;
    }

    return(0);
}


static void
BuslogicPCIWrite(int func, int addr, uint8_t val, void *p)
{
    Buslogic_t *bl = (Buslogic_t *)p;
    uint8_t valxor;

    pclog("BT-958D: Write value %02X to register %02X\n", val, addr & 0xff);

    switch (addr) {
	case 0x04:
		valxor = (val & 0x27) ^ buslogic_pci_regs[addr];
		if (valxor & PCI_COMMAND_IO) {
			io_removehandler(bl->PCIBase, 0x20,
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
			if (bl->MMIOBase != 0) {
				mem_mapping_set_addr(&bl->mmio_mapping,
						     bl->MMIOBase, 0x20);
			}
		}
		return;	

	case 0x30:			/* PCI_ROMBAR */
	case 0x31:			/* PCI_ROMBAR */
	case 0x32:			/* PCI_ROMBAR */
	case 0x33:			/* PCI_ROMBAR */
		buslogic_pci_bar[2].addr_regs[addr & 3] = val;
		buslogic_pci_bar[2].addr &= 0xffffc001;
		bl->bios_addr = buslogic_pci_bar[2].addr & 0xffffc000;
		pclog("BT-958D: BIOS BAR %02X = NOW %02X (%02X)\n", addr & 3, buslogic_pci_bar[2].addr_regs[addr & 3], val);
		BuslogicBIOSUpdate(bl);
		return;

	case 0x3C:
		buslogic_pci_regs[addr] = val;
		if (val != 0xFF) {
			BuslogicLog("BusLogic IRQ now: %i\n", val);
			bl->Irq = val;
		}
		return;
    }
}


static void
BuslogicInitializeLocalRAM(Buslogic_t *bl)
{
	memset(bl->LocalRAM.u8View, 0, sizeof(HALocalRAM));
	if (PCI && (bl->chip == CHIP_BUSLOGIC_PCI))
	{
		bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt = 1;
	}
	else
	{
		bl->LocalRAM.structured.autoSCSIData.fLevelSensitiveInterrupt = 0;
	}
	bl->LocalRAM.structured.autoSCSIData.u16DeviceEnabledMask = ~0;
	bl->LocalRAM.structured.autoSCSIData.u16WidePermittedMask = ~0;
	bl->LocalRAM.structured.autoSCSIData.u16FastPermittedMask = ~0;
	bl->LocalRAM.structured.autoSCSIData.u16SynchronousPermittedMask = ~0;
	bl->LocalRAM.structured.autoSCSIData.u16DisconnectPermittedMask = ~0;
	bl->LocalRAM.structured.autoSCSIData.fAggressiveRoundRobinMode = 0;
	bl->LocalRAM.structured.autoSCSIData.u16UltraPermittedMask = ~0;
}


void
BuslogicDeviceReset(void *p)
{
	Buslogic_t *dev = (Buslogic_t *) p;
	BuslogicResetControl(dev, 1);

	BuslogicInitializeLocalRAM(dev);
	BuslogicInitializeAutoSCSIRam(dev);
}


static void *
BuslogicInit(int chip)
{
    Buslogic_t *bl;
    wchar_t *bios_rom_name;
    uint16_t bios_rom_size;
    uint16_t bios_rom_mask;
    uint8_t has_autoscsi_rom;
    wchar_t *autoscsi_rom_name;
    uint16_t autoscsi_rom_size;
    uint8_t has_scam_rom;
    wchar_t *scam_rom_name;
    uint16_t scam_rom_size;
    FILE *f;

    bl = malloc(sizeof(Buslogic_t));
    memset(bl, 0x00, sizeof(Buslogic_t));

    if (!PCI && (chip == CHIP_BUSLOGIC_PCI))
    {
	chip = CHIP_BUSLOGIC_ISA;
    }
    bl->chip = chip;
    bl->Base = device_get_config_hex16("base");
    bl->PCIBase = 0;
    bl->MMIOBase = 0;
    bl->Irq = device_get_config_int("irq");
    bl->DmaChannel = device_get_config_int("dma");
    bl->has_bios = device_get_config_int("bios");

	
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


	switch(bl->chip)
	{
		case CHIP_BUSLOGIC_ISA:
		default:
			bios_rom_name = L"roms/scsi/buslogic/BT-545C_BIOS.rom";
			bios_rom_size = 0x4000;
			bios_rom_mask = 0x3fff;
			has_autoscsi_rom = 1;
			autoscsi_rom_name = L"roms/scsi/buslogic/BT-545C_AutoSCSI.rom";
			autoscsi_rom_size = 0x4000;
			has_scam_rom = 0;
			break;
		case CHIP_BUSLOGIC_PCI:
			bios_rom_name = L"roms/scsi/buslogic/BT-958D_BIOS.rom";
			bios_rom_size = 0x4000;
			bios_rom_mask = 0x3fff;
			has_autoscsi_rom = 1;
			autoscsi_rom_name = L"roms/scsi/buslogic/BT-958D_AutoSCSI.rom";
			autoscsi_rom_size = 0x8000;
			has_scam_rom = 1;
			scam_rom_name = L"roms/scsi/buslogic/BT-958D_SCAM.rom";
			scam_rom_size = 0x0200;
			break;
	}


	memset(bl->AutoSCSIROM, 0xff, 32768);

	memset(bl->SCAMData, 0x00, 65536);


	if (bl->has_bios)
	{
		bl->bios_size = bios_rom_size;

		bl->bios_mask = 0xffffc000;

       		rom_init(&bl->bios, bios_rom_name, 0xd8000, bios_rom_size, bios_rom_mask, 0, MEM_MAPPING_EXTERNAL);

		if (has_autoscsi_rom)
		{
			f = romfopen(autoscsi_rom_name, L"rb");
			if (f)
			{
				fread(bl->AutoSCSIROM, 1, autoscsi_rom_size, f);
				fclose(f);
				f = NULL;
			}
		}

		if (has_scam_rom)
		{
			f = romfopen(scam_rom_name, L"rb");
			if (f)
			{
				fread(bl->SCAMData, 1, scam_rom_size, f);
				fclose(f);
				f = NULL;
			}
		}
	}
	else
	{
		bl->bios_size = 0;

		bl->bios_mask = 0;
	}

    timer_add(BuslogicResetPoll,
	      &BuslogicResetCallback, &BuslogicResetCallback, bl);

    if (bl->chip == CHIP_BUSLOGIC_PCI) {
	bl->Card = pci_add_card(PCI_ADD_NORMAL, BuslogicPCIRead, BuslogicPCIWrite, bl);

	buslogic_pci_bar[0].addr_regs[0] = 1;
	buslogic_pci_bar[1].addr_regs[0] = 0;
       	buslogic_pci_regs[0x04] = 3;
#if 0
        buslogic_pci_regs[0x05] = 0;
        buslogic_pci_regs[0x07] = 2;
#endif

	/* Enable our BIOS space in PCI, if needed. */
	if (bl->has_bios)
	{
		buslogic_pci_bar[2].addr = 0xFFFFC000;
	}
	else
	{
		buslogic_pci_bar[2].addr = 0;
	}

	mem_mapping_add(&bl->mmio_mapping, 0xfffd0000, 0x20,
		        BuslogicMemRead, BuslogicMemReadW, BuslogicMemReadL,
			BuslogicMemWrite, BuslogicMemWriteW, BuslogicMemWriteL,
			NULL, MEM_MAPPING_EXTERNAL, bl);
	mem_mapping_disable(&bl->mmio_mapping);
	mem_mapping_disable(&bl->bios.mapping);
    }
	
    pclog("Buslogic on port 0x%04X\n", bl->Base);
	
    BuslogicResetControl(bl, CTRL_HRST);

    BuslogicInitializeLocalRAM(bl);
    BuslogicInitializeAutoSCSIRam(bl);

    return(bl);
}


static void *
Buslogic_545C_Init(void)
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
    if (bl)
    {
	bl->MailboxCount = 0;

	if (bl->evt)
	{
		thread_destroy_event(bl->evt);
		bl->evt = NULL;

		if (poll_tid)
		{
			thread_kill(poll_tid);
			poll_tid = NULL;
		}
	}

    	free(bl);
	bl = NULL;
    }
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
		"bios", "Enable BIOS", CONFIG_BINARY, "", 0
	},
	{
		"", "", -1
	}
};


device_t buslogic_device = {
	"Buslogic BT-545C ISA",
	0,
	Buslogic_545C_Init,
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
