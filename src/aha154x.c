/* Copyright holders: SA1988
   see COPYING for more details
*/
/*Adaptec 154x SCSI emulation and clones (including Buslogic ISA adapters)*/

/* 
ToDo:
Improve support for DOS, Windows 3.x and Windows 9x as well as NT.	
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "dma.h"
#include "rom.h"
#include "pic.h"
#include "timer.h"

#include "cdrom.h"
#include "scsi.h"

#include "aha154x.h"

typedef struct
{
	uint8_t hi;
	uint8_t mid;
	uint8_t lo;
} addr24;

#define ADDR_TO_U32(x) 		(((x).hi << 16) | ((x).mid << 8) | (x).lo & 0xFF)
#define U32_TO_ADDR(a, x) 	do {(a).hi = (x) >> 16; (a).mid = (x) >> 8; (a).lo = (x) & 0xFF;} while(0)
	
// I/O Port interface
// READ  Port x+0: STATUS
// WRITE Port x+0: CONTROL
//
// READ  Port x+1: DATA
// WRITE Port x+1: COMMAND
//
// READ  Port x+2: INTERRUPT STATUS
// WRITE Port x+2: (undefined?)
//
// R/W   Port x+3: (undefined)

// READ STATUS flags
#define STAT_STST   0x80    // self-test in progress
#define STAT_DFAIL  0x40    // internal diagnostic failure
#define STAT_INIT  0x20    // mailbox initialization required
#define STAT_IDLE  0x10    // HBA is idle
#define STAT_CDFULL 0x08    // Command/Data output port is full
#define STAT_DFULL 0x04    // Data input port is full
#define STAT_INVCMD 0x01    // Invalid command

// READ INTERRUPT STATUS flags
#define INTR_ANY   0x80    // any interrupt
#define INTR_SRCD   0x08    // SCSI reset detected
#define INTR_HACC   0x04    // HA command complete
#define INTR_MBOA   0x02    // MBO empty
#define INTR_MBIF   0x01    // MBI full

// WRITE CONTROL commands
#define CTRL_HRST  0x80    // Hard reset
#define CTRL_SRST  0x40    // Soft reset
#define CTRL_IRST   0x20    // interrupt reset
#define CTRL_SCRST  0x10    // SCSI bus reset

// READ/WRITE DATA commands
#define CMD_NOP        					0x00    // No operation
#define CMD_MBINIT      				0x01    // mailbox initialization
#define CMD_START_SCSI  				0x02    // Start SCSI command
#define CMD_INQUIRY     				0x04    // Adapter inquiry
#define CMD_EMBOI       				0x05    // enable Mailbox Out Interrupt
#define CMD_SELTIMEOUT  				0x06    // Set SEL timeout
#define CMD_BUSON_TIME  				0x07    // set bus-On time
#define CMD_BUSOFF_TIME 				0x08    // set bus-off time
#define CMD_DMASPEED    				0x09    // set ISA DMA speed
#define CMD_RETDEVS   					0x0A    // return installed devices
#define CMD_RETCONF     				0x0B    // return configuration data
#define CMD_TARGET      				0x0C    // set HBA to target mode
#define CMD_RETSETUP   					0x0D    // return setup data
#define CMD_ECHO        				0x1F    // ECHO command data

/** Structure for the INQUIRE_SETUP_INFORMATION reply. */
typedef struct ReplyInquireSetupInformationSynchronousValue
{
    uint8_t uOffset :         4;
    uint8_t uTransferPeriod : 3;
    uint8_t fSynchronous :    1;
}ReplyInquireSetupInformationSynchronousValue;

typedef struct ReplyInquireSetupInformation
{
    uint8_t fSynchronousInitiationEnabled : 1;
    uint8_t fParityCheckingEnabled :        1;
    uint8_t uReserved1 :           6;
    uint8_t uBusTransferRate;
    uint8_t uPreemptTimeOnBus;
    uint8_t uTimeOffBus;
    uint8_t cMailbox;
    addr24  MailboxAddress;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId0To7[8];
    uint8_t uDisconnectPermittedId0To7;
    uint8_t uSignature;
    uint8_t uCharacterD;
    uint8_t uHostBusType;
    uint8_t uWideTransferPermittedId0To7;
    uint8_t uWideTransfersActiveId0To7;
    ReplyInquireSetupInformationSynchronousValue SynchronousValuesId8To15[8];
    uint8_t uDisconnectPermittedId8To15;
    uint8_t uReserved2;
    uint8_t uWideTransferPermittedId8To15;
    uint8_t uWideTransfersActiveId8To15;
} ReplyInquireSetupInformation;

/** Structure for the INQUIRE_EXTENDED_SETUP_INFORMATION. */
#pragma pack(1)
typedef struct ReplyInquireExtendedSetupInformation
{
    uint8_t       uBusType;
    uint8_t       uBiosAddress;
    uint16_t      u16ScatterGatherLimit;
    uint8_t       cMailbox;
    uint32_t      uMailboxAddressBase;
    uint8_t 		uReserved1 : 2;
    uint8_t         fFastEISA : 1;
    uint8_t 		uReserved2 : 3;
    uint8_t          fLevelSensitiveInterrupt : 1;
    uint8_t 		uReserved3 : 1;
    uint8_t 		aFirmwareRevision[3];
    uint8_t          fHostWideSCSI : 1;
    uint8_t          fHostDifferentialSCSI : 1;
    uint8_t          fHostSupportsSCAM : 1;
    uint8_t          fHostUltraSCSI : 1;
    uint8_t          fHostSmartTermination : 1;
    uint8_t 		uReserved4 : 3;
} ReplyInquireExtendedSetupInformation;
#pragma pack()

typedef struct MailboxInit_t
{
	uint8_t Count;
	addr24 Address;
} MailboxInit_t;

#pragma pack(1)
typedef struct MailboxInitExtended_t
{
	uint8_t Count;
	uint32_t Address;
} MailboxInitExtended_t;
#pragma pack()

///////////////////////////////////////////////////////////////////////////////
//
// Mailbox Definitions
//
//
///////////////////////////////////////////////////////////////////////////////

//
// Mailbox Out
//
//
// MBO Command Values
//

#define MBO_FREE                  0x00
#define MBO_START                 0x01
#define MBO_ABORT                 0x02

//
// Mailbox In
//
//
// MBI Status Values
//

#define MBI_FREE                  0x00
#define MBI_SUCCESS               0x01
#define MBI_ABORT                 0x02
#define MBI_NOT_FOUND             0x03
#define MBI_ERROR                 0x04

typedef struct Mailbox_t
{
	uint8_t CmdStatus;
	addr24 CCBPointer;
} Mailbox_t;

typedef struct Mailbox32_t
{
	uint32_t CCBPointer;
	union
	{
		struct
		{
			uint8_t Reserved[3];
			uint8_t ActionCode;
		} out;
		struct
		{
			uint8_t HostStatus;
			uint8_t TargetStatus;
			uint8_t Reserved;
			uint8_t CompletionCode;
		} in;
	} u;
} Mailbox32_t;


///////////////////////////////////////////////////////////////////////////////
//
// CCB - Adaptec SCSI Command Control Block
//
//    The CCB is a superset of the CDB (Command Descriptor Block)
//    and specifies detailed information about a SCSI command.
//
///////////////////////////////////////////////////////////////////////////////

//
//    Byte 0    Command Control Block Operation Code
//

#define SCSI_INITIATOR_COMMAND    	  0x00
#define TARGET_MODE_COMMAND       	  0x01
#define SCATTER_GATHER_COMMAND		  0x02
#define SCSI_INITIATOR_COMMAND_RES	  0x03
#define SCATTER_GATHER_COMMAND_RES    0x04
#define BUS_RESET				  	  0x81

//
//    Byte 1    Address and Direction Control
//

#define CCB_TARGET_ID_SHIFT       0x06            // CCB Op Code = 00, 02
#define CCB_INITIATOR_ID_SHIFT    0x06            // CCB Op Code = 01
#define CCB_DATA_XFER_IN		  0x01
#define CCB_DATA_XFER_OUT		  0x02
#define CCB_LUN_MASK              0x07            // Logical Unit Number

//
//    Byte 2    SCSI_Command_Length - Length of SCSI CDB
//
//    Byte 3    Request Sense Allocation Length
//

#define FOURTEEN_BYTES            0x00            // Request Sense Buffer size
#define NO_AUTO_REQUEST_SENSE     0x01            // No Request Sense Buffer

//
//    Bytes 4, 5 and 6    Data Length             // Data transfer byte count
//
//    Bytes 7, 8 and 9    Data Pointer            // SGD List or Data Buffer
//
//    Bytes 10, 11 and 12 Link Pointer            // Next CCB in Linked List
//
//    Byte 13   Command Link ID                   // TBD (I don't know yet)
//
//    Byte 14   Host Status                       // Host Adapter status
//

#define CCB_COMPLETE              0x00            // CCB completed without error
#define CCB_LINKED_COMPLETE       0x0A            // Linked command completed
#define CCB_LINKED_COMPLETE_INT   0x0B            // Linked complete with interrupt
#define CCB_SELECTION_TIMEOUT     0x11            // Set SCSI selection timed out
#define CCB_DATA_OVER_UNDER_RUN   0x12
#define CCB_UNEXPECTED_BUS_FREE   0x13            // Target dropped SCSI BSY
#define CCB_PHASE_SEQUENCE_FAIL   0x14            // Target bus phase sequence failure
#define CCB_BAD_MBO_COMMAND       0x15            // MBO command not 0, 1 or 2
#define CCB_INVALID_OP_CODE       0x16            // CCB invalid operation code
#define CCB_BAD_LINKED_LUN        0x17            // Linked CCB LUN different from first
#define CCB_INVALID_DIRECTION     0x18            // Invalid target direction
#define CCB_DUPLICATE_CCB         0x19            // Duplicate CCB
#define CCB_INVALID_CCB           0x1A            // Invalid CCB - bad parameter

//
//    Byte 15   Target Status
//
//    See scsi.h files for these statuses.
//

//
//    Bytes 16 and 17   Reserved (must be 0)
//

//
//    Bytes 18 through 18+n-1, where n=size of CDB  Command Descriptor Block
//

typedef struct CCB32
{
	uint8_t Opcode;
	uint8_t Reserved1:3;
	uint8_t ControlByte:2;
	uint8_t TagQueued:1;
	uint8_t QueueTag:2;
	uint8_t CdbLength;
	uint8_t RequestSenseLength;
	uint32_t DataLength;
	uint32_t DataPointer;
	uint8_t Reserved2[2];
	uint8_t HostStatus;
	uint8_t TargetStatus;
	uint8_t Id;
	uint8_t Lun:5;
	uint8_t LegacyTagEnable:1;
	uint8_t LegacyQueueTag:2;
	uint8_t Cdb[12];
	uint8_t Reserved3[6];
	uint32_t SensePointer;
} CCB32;

typedef struct CCB
{
	uint8_t Opcode;
	uint8_t Lun:3;
	uint8_t ControlByte:2;
	uint8_t Id:3;
	uint8_t CdbLength;
	uint8_t RequestSenseLength;
	addr24 DataLength;
	addr24 DataPointer;
	addr24 LinkPointer;
	uint8_t LinkId;
	uint8_t HostStatus;
	uint8_t TargetStatus;
	uint8_t Reserved[2];
	uint8_t Cdb[12];
} CCB;

typedef struct CCBC
{
	uint8_t Opcode;
	uint8_t Pad1:3;
	uint8_t ControlByte:2;
	uint8_t Pad2:3;
	uint8_t CdbLength;
	uint8_t RequestSenseLength;
	uint8_t Pad3[10];
	uint8_t HostStatus;
	uint8_t TargetStatus;
	uint8_t Pad4[2];
	uint8_t Cdb[12];
} CCBC;

typedef union CCBU
{
	CCB32 new;
	CCB   old;
	CCBC  common;
} CCBU;

///////////////////////////////////////////////////////////////////////////////
//
// Scatter/Gather Segment List Definitions
//
///////////////////////////////////////////////////////////////////////////////

//
// Adapter limits
//

#define MAX_SG_DESCRIPTORS 32

typedef struct SGE32
{
	uint32_t Segment;
	uint32_t SegmentPointer;
} SGE32;

typedef struct SGE
{
	addr24 Segment;
	addr24 SegmentPointer;
} SGE;

typedef struct AdaptecRequests_t
{
	CCBU CmdBlock;
	uint8_t *RequestSenseBuffer;
	uint32_t CCBPointer;
	int Is24bit;
} AdaptecRequests_t;

typedef struct Adaptec_t
{
	rom_t BiosRom;
	AdaptecRequests_t AdaptecRequests;
	uint8_t Status;
	uint8_t Interrupt;
	uint8_t Geometry;
	uint8_t Control;
	uint8_t Command;
	uint8_t CmdBuf[5];
	uint8_t CmdParam;
	uint8_t CmdParamLeft;
	uint8_t DataBuf[64];
	uint8_t DataReply;
	uint8_t DataReplyLeft;
	uint32_t MailboxCount;
	uint32_t MailboxOutAddr;
	uint32_t MailboxOutPosCur;
	uint32_t MailboxInAddr;
	uint32_t MailboxInPosCur;
	int Irq;
	int DmaChannel;
	int IrqEnabled;
	int Mbx24bit;
} Adaptec_t;

Adaptec_t AdaptecLUN[7];

int scsi_base = 0x330;
int scsi_dma = 6;
int scsi_irq = 11;

static void AdaptecSCSIRequestSetup(Adaptec_t *Adaptec, uint32_t CCBPointer);
static void AdaptecStartMailbox(Adaptec_t *Adaptec);

typedef void (*AdaptecMemCopyCallback)(Adaptec_t *Adaptec, uint32_t Addr, SGBUF *SegmentBuffer,
											uint32_t Copy, uint32_t *Skip);	

int aha154x_do_log = 0;

void AdaptecLog(const char *format, ...)
{
   if (aha154x_do_log)
   {
		va_list ap;
		va_start(ap, format);
		vprintf(format, ap);
		va_end(ap);
		fflush(stdout);
   }
}
					
static void AdaptecClearInterrupt(Adaptec_t *Adaptec)
{
	AdaptecLog("Adaptec: Clearing Interrupt 0x%02X\n", Adaptec->Interrupt);
	Adaptec->Interrupt = 0;
	picintc(1 << Adaptec->Irq);
}

static void AdaptecReset(Adaptec_t *Adaptec)
{
	Adaptec->Status = STAT_IDLE | STAT_INIT;
	Adaptec->Geometry = 0x80;
	Adaptec->Command = 0xFF;
	Adaptec->CmdParam = 0;
	Adaptec->CmdParamLeft = 0;
	Adaptec->IrqEnabled = 1;
	Adaptec->MailboxOutPosCur = 0;
	Adaptec->MailboxInPosCur = 0;
		
	AdaptecClearInterrupt(Adaptec);
}

static void AdaptecResetControl(Adaptec_t *Adaptec, uint8_t Reset)
{
	AdaptecReset(Adaptec);
	if (Reset)
	{
		Adaptec->Status |= STAT_STST;
		Adaptec->Status &= ~STAT_IDLE;				
	}	
}

static void AdaptecCommandComplete(Adaptec_t *Adaptec)
{
	Adaptec->Status |= STAT_IDLE;
	Adaptec->DataReply = 0;
				
	if (Adaptec->Command != CMD_START_SCSI)
	{
		Adaptec->Status &= ~STAT_DFULL;
		Adaptec->Interrupt = INTR_ANY | INTR_HACC;
		picint(1 << Adaptec->Irq);
	}
				
	Adaptec->Command = 0xFF;
	Adaptec->CmdParam = 0;
}

static void AdaptecMailboxIn(Adaptec_t *Adaptec, uint32_t CCBPointer, CCBU *CmdBlock, 
			uint8_t HostStatus, uint8_t TargetStatus, uint8_t MailboxCompletionCode)
{	
	Mailbox32_t Mailbox32;
	Mailbox_t MailboxIn;
	
	Mailbox32.CCBPointer = CCBPointer;
	Mailbox32.u.in.HostStatus = HostStatus;
	Mailbox32.u.in.TargetStatus = TargetStatus;
	Mailbox32.u.in.CompletionCode = MailboxCompletionCode;
	
	uint32_t Incoming = Adaptec->MailboxInAddr + (Adaptec->MailboxInPosCur * (Adaptec->Mbx24bit ? sizeof(Mailbox_t) : sizeof(Mailbox32_t)));

	if (MailboxCompletionCode != MBI_NOT_FOUND)
	{
		CmdBlock->common.HostStatus = HostStatus;
		CmdBlock->common.TargetStatus = TargetStatus;		
		
		//Rewrite the CCB up to the CDB.
		DMAPageWrite(CCBPointer, CmdBlock, offsetof(CCBC, Cdb));
	}
	
	pclog("Host Status 0x%02X, TargetStatus 0x%02X\n", HostStatus, TargetStatus);

	if (Adaptec->Mbx24bit)
	{
		MailboxIn.CmdStatus = Mailbox32.u.in.CompletionCode;
		U32_TO_ADDR(MailboxIn.CCBPointer, Mailbox32.CCBPointer);
		pclog("Mailbox 24-bit: Status=0x%02X, CCB at 0x%08X\n", MailboxIn.CmdStatus, ADDR_TO_U32(MailboxIn.CCBPointer));
		
		DMAPageWrite(Incoming, &MailboxIn, sizeof(Mailbox_t));
	}
	else
	{
		pclog("Mailbox 32-bit: Status=0x%02X, CCB at 0x%08X\n", Mailbox32.u.in.CompletionCode, Mailbox32.CCBPointer);
		
		DMAPageWrite(Incoming, &Mailbox32, sizeof(Mailbox32_t));		
	}
	
	Adaptec->MailboxInPosCur++;
	if (Adaptec->MailboxInPosCur > Adaptec->MailboxCount)
		Adaptec->MailboxInPosCur = 0;

	Adaptec->Interrupt = INTR_MBIF | INTR_ANY;
	picint(1 << Adaptec->Irq);
}

static void AdaptecReadSGEntries(int Is24bit, uint32_t SGList, uint32_t Entries, SGE32 *SG)
{
	if (Is24bit)
	{
		uint32_t i;
		SGE SGE24[MAX_SG_DESCRIPTORS];
		
		DMAPageRead(SGList, &SGE24, Entries * sizeof(SGE));

		for (i=0;i<Entries;++i)
		{
			//Convert the 24-bit entries into 32-bit entries.
			SG[i].Segment = ADDR_TO_U32(SGE24[i].Segment);
			SG[i].SegmentPointer = ADDR_TO_U32(SGE24[i].SegmentPointer);
		}
	}
	else
	{
		DMAPageRead(SGList, SG, Entries * sizeof(SGE32));		
	}
}

static void AdaptecQueryDataBufferSize(Adaptec_t *Adaptec, CCBU *CmdBlock, int Is24bit, uint32_t *pBufferSize)
{	
	uint32_t BufferSize = 0;
	
	if (Is24bit)
	{
		DataPointer = ADDR_TO_U32(CmdBlock->old.DataPointer);
		DataLength = ADDR_TO_U32(CmdBlock->old.DataLength);
	}
	else
	{
		DataPointer = CmdBlock->new.DataPointer;
		DataLength = CmdBlock->new.DataLength;		
	}
	
	if (DataLength && (CmdBlock->common.Opcode != 3))
	{
		if (CmdBlock->common.Opcode == SCATTER_GATHER_COMMAND ||
			CmdBlock->common.Opcode == SCATTER_GATHER_COMMAND_RES)
		{
			uint32_t ScatterGatherRead;
			uint32_t ScatterEntry;
			SGE32 ScatterGatherBuffer[MAX_SG_DESCRIPTORS];
			uint32_t ScatterGatherLeft = DataLength / (Is24bit ? sizeof(SGE) : sizeof(SGE32));
			uint32_t ScatterGatherAddrCurrent = DataPointer;
						
			do
			{
				ScatterGatherRead = (ScatterGatherLeft < ELEMENTS(ScatterGatherBuffer))
									? ScatterGatherLeft : ELEMENTS(ScatterGatherBuffer);
							
				ScatterGatherLeft -= ScatterGatherRead;

				AdaptecReadSGEntries(Is24bit, ScatterGatherAddrCurrent, ScatterGatherRead, ScatterGatherBuffer);
							
				for (ScatterEntry = 0; ScatterEntry < ScatterGatherRead; ScatterEntry++)
					BufferSize += ScatterGatherBuffer[ScatterEntry].Segment;
							
				ScatterGatherAddrCurrent += ScatterGatherRead * (Is24bit ? sizeof(SGE) : sizeof(SGE32));
			} while (ScatterGatherLeft > 0);
				
			AdaptecLog("Adaptec: Query Data Buffer\n");
			AdaptecLog("Adaptec: Data Buffer Size=%u\n", BufferSize);
		}
		else if (CmdBlock->common.Opcode == SCSI_INITIATOR_COMMAND ||
				CmdBlock->common.Opcode == SCSI_INITIATOR_COMMAND_RES)
			BufferSize = DataLength;
	}

	*pBufferSize = BufferSize;
}

static void AdaptecCopyBufferFromGuestWorker(Adaptec_t *Adaptec, uint32_t Address, SGBUF *SegmentBuffer,
											uint32_t Copy, uint32_t *pSkip)
{
	uint32_t Skipped = MIN(Copy, *pSkip);
	Copy -= Skipped;
	Address += Skipped;
	*pSkip -= Skipped;
	
	while (Copy)
	{
		uint32_t Segment = Copy;
		uint8_t *SegmentPointer = SegmentBufferGetNextSegment(SegmentBuffer, Segment);

		DMAPageRead(Address, SegmentPointer, Segment);

		Address += Segment;
		Copy -= Segment;
	}
}

static void AdaptecCopyBufferToGuestWorker(Adaptec_t *Adaptec, uint32_t Address, SGBUF *SegmentBuffer,
											uint32_t Copy, uint32_t *pSkip)
{
	uint32_t Skipped = MIN(Copy, *pSkip);
	Copy -= Skipped;
	Address += Skipped;
	*pSkip -= Skipped;
	
	while (Copy)
	{
		uint32_t Segment = Copy;
		uint8_t *SegmentPointer = SegmentBufferGetNextSegment(SegmentBuffer, Segment);
		
		DMAPageWrite(Address, SegmentPointer, Segment);

		Address += Segment;
		Copy -= Segment;
	}
}

static int AdaptecScatterGatherBufferWalker(Adaptec_t *Adaptec, AdaptecRequests_t *AdaptecRequests, 
											AdaptecMemCopyCallback IoCopyWorker, SGBUF *SegmentBuffer,
											uint32_t Skip, uint32_t Copy)
{
	uint32_t Copied = 0;
	
	Copy += Skip;

	if (AdaptecRequests->Is24bit)
	{
		DataPointer = ADDR_TO_U32(AdaptecRequests->CmdBlock.old.DataPointer);
		DataLength = ADDR_TO_U32(AdaptecRequests->CmdBlock.old.DataLength);
	}
	else
	{
		DataPointer = AdaptecRequests->CmdBlock.new.DataPointer;
		DataLength = AdaptecRequests->CmdBlock.new.DataLength;		
	}
	
	/*Mostly a hack for NT 1991 as the CCB describes a 2K buffer, but Test Unit Ready is executed
		and it returns no data, the buffer must be left alone*/
	if (AdaptecRequests->CmdBlock.common.Cdb[0] == GPCMD_TEST_UNIT_READY)
		DataLength = 0;
	
	if ((DataLength > 0) && (AdaptecRequests->CmdBlock.common.ControlByte == CCB_DATA_XFER_IN || 
		AdaptecRequests->CmdBlock.common.ControlByte == CCB_DATA_XFER_OUT))
	{
		if (AdaptecRequests->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND ||
			AdaptecRequests->CmdBlock.common.Opcode == SCATTER_GATHER_COMMAND_RES)
		{
			uint32_t ScatterGatherRead;
			uint32_t ScatterEntry;
			SGE32 ScatterGatherBuffer[MAX_SG_DESCRIPTORS];
			uint32_t ScatterGatherLeft = DataLength / (AdaptecRequests->Is24bit ? sizeof(SGE) : sizeof(SGE32));
			uint32_t ScatterGatherAddrCurrent = DataPointer;
						
			do
			{
				ScatterGatherRead = (ScatterGatherLeft < ELEMENTS(ScatterGatherBuffer))
									? ScatterGatherLeft : ELEMENTS(ScatterGatherBuffer);
						
				ScatterGatherLeft -= ScatterGatherRead;

				AdaptecReadSGEntries(AdaptecRequests->Is24bit, ScatterGatherAddrCurrent, ScatterGatherRead, ScatterGatherBuffer);

				for (ScatterEntry = 0; ScatterEntry < ScatterGatherRead; ScatterEntry++)
				{
					uint32_t Address;
					uint32_t CopyThis;
								
					AdaptecLog("Adaptec: Scatter Entry=%u\n", ScatterEntry);
								
					Address = ScatterGatherBuffer[ScatterEntry].SegmentPointer;
					CopyThis = MIN(Copy, ScatterGatherBuffer[ScatterEntry].Segment);
								
					AdaptecLog("Adaptec: S/G Address=0x%04X, Copy=%u\n", Address, CopyThis);
								
					IoCopyWorker(Adaptec, Address, SegmentBuffer, CopyThis, &Skip);
					Copied += CopyThis;
					Copy -= CopyThis;
				}
							
				ScatterGatherAddrCurrent += ScatterGatherRead * sizeof(SGE);
			} while (ScatterGatherLeft > 0 && Copy > 0);
		}
		else if (AdaptecRequests->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND ||
			AdaptecRequests->CmdBlock.common.Opcode == SCSI_INITIATOR_COMMAND_RES)
		{
			uint32_t Address = DataPointer;
						
			AdaptecLog("Adaptec: Non-scattered buffer\n");
			AdaptecLog("Data Pointer=%#x\n", DataPointer);
			AdaptecLog("Data Length=%u\n", DataLength);
			AdaptecLog("Pointer Address=%#x\n", Address);
			
			IoCopyWorker(Adaptec, Address, SegmentBuffer, MIN(DataLength, Copy), &Skip);
			Copied += MIN(DataLength, Copy);				
		}
	}
	
	pclog("Opcode %02X\n", AdaptecRequests->CmdBlock.common.Opcode);
	
	return Copied - MIN(Skip, Copied);
}

static int AdaptecCopySegmentBufferToGuest(Adaptec_t *Adaptec, AdaptecRequests_t *AdaptecRequests,
												SGBUF *SegmentBuffer, uint32_t Skip, uint32_t Copy)
{
	return AdaptecScatterGatherBufferWalker(Adaptec, AdaptecRequests, AdaptecCopyBufferToGuestWorker, SegmentBuffer, Skip, Copy);								
}

static int AdaptecCopySegmentBufferFromGuest(Adaptec_t *Adaptec, AdaptecRequests_t *AdaptecRequests,
												SGBUF *SegmentBuffer, uint32_t Skip, uint32_t Copy)
{
	return AdaptecScatterGatherBufferWalker(Adaptec, AdaptecRequests, AdaptecCopyBufferFromGuestWorker, SegmentBuffer, Skip, Copy);								
}

uint8_t AdaptecRead(uint16_t Port, void *p)
{
	Adaptec_t *Adaptec = &AdaptecLUN[scsi_cdrom_id];
	uint8_t Temp;
	
	switch (Port & 3)
	{
		case 0:
		Temp = Adaptec->Status;
		if (Adaptec->Status & STAT_STST)
		{
			Adaptec->Status &= ~STAT_STST;
			Adaptec->Status |= STAT_IDLE;
			Temp = Adaptec->Status;
		}
		break;
		
		case 1:
		Temp = Adaptec->DataBuf[Adaptec->DataReply];
		if (Adaptec->DataReplyLeft)
		{
			Adaptec->DataReply++;
			Adaptec->DataReplyLeft--;
			if (!Adaptec->DataReplyLeft)
			{
				AdaptecCommandComplete(Adaptec);
			}
		}
		break;
		
		case 2:
		Temp = Adaptec->Interrupt;
		break;
		
		case 3:
		Temp = Adaptec->Geometry;
		break;
	}
	
	AdaptecLog("Adaptec: Read Port 0x%02X, Returned Value %02X\n", Port, Temp);
	return Temp;	
}

void AdaptecWrite(uint16_t Port, uint8_t Val, void *p)
{
	Adaptec_t *Adaptec = &AdaptecLUN[scsi_cdrom_id];
	pclog("Adaptec: Write Port 0x%02X, Value %02X\n", Port, Val);
	
	switch (Port & 3)
	{
		case 0:
		if ((Val & CTRL_HRST) || (Val & CTRL_SRST))
		{	
			uint8_t Reset = !!(Val & CTRL_HRST);
			AdaptecResetControl(Adaptec, Reset);
			break;
		}
		
		if (Val & CTRL_IRST)
			AdaptecClearInterrupt(Adaptec);
		break;
		
		case 1:
		if ((Val == 0x02) && (Adaptec->Command == 0xFF))
		{
			ScsiCallback[scsi_cdrom_id] = 1;
			break;
		}
		
		if (Adaptec->Command == 0xFF)
		{
			Adaptec->Command = Val;
			Adaptec->CmdParam = 0;
			
			Adaptec->Status &= ~(STAT_INVCMD | STAT_IDLE);
			pclog("Adaptec: Operation Code 0x%02X\n", Val);
			switch (Adaptec->Command)
			{
				case 0x00:
				case 0x03:
				case 0x04:
				case 0x0A:
				case 0x0B:
				case 0x84:
				case 0x85:
				Adaptec->CmdParamLeft = 0;
				break;
				
				case 0x07:
				case 0x08:
				case 0x09:
				case 0x0D:
				case 0x1F:
				case 0x21:
				case 0x8B:
				case 0x8D:
				case 0x25:
				Adaptec->CmdParamLeft = 1;
				break;
				
				case 0x06:
				Adaptec->CmdParamLeft = 4;
				break;
				
				case 0x01:
				Adaptec->CmdParamLeft = sizeof(MailboxInit_t);
				break;
				
				case 0x81:
				Adaptec->CmdParamLeft = sizeof(MailboxInitExtended_t);
				break;

				case 0x28:
				case 0x29:
				Adaptec->CmdParamLeft = 0;
				break;
			}
		}
		else
		{
			Adaptec->CmdBuf[Adaptec->CmdParam] = Val;
			Adaptec->CmdParam++;
			Adaptec->CmdParamLeft--;
		}
		
		if (!Adaptec->CmdParamLeft)
		{
			pclog("Running Operation Code 0x%02X\n", Adaptec->Command);
			switch (Adaptec->Command)
			{
				case 0x00:
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x01:
				{
					Adaptec->Mbx24bit = 1;
					
					MailboxInit_t *MailboxInit = (MailboxInit_t *)Adaptec->CmdBuf;

					Adaptec->MailboxCount = MailboxInit->Count;
					Adaptec->MailboxOutAddr = ADDR_TO_U32(MailboxInit->Address);
					Adaptec->MailboxInAddr = Adaptec->MailboxOutAddr + (Adaptec->MailboxCount * sizeof(Mailbox_t));
				
					AdaptecLog("Adaptec Initialize Mailbox Command\n");
					AdaptecLog("Mailbox Out Address=0x%08X\n", Adaptec->MailboxOutAddr);
					AdaptecLog("Mailbox In Address=0x%08X\n", Adaptec->MailboxInAddr);
					pclog("Initialized Mailbox, %d entries at 0x%08X\n", MailboxInit->Count, ADDR_TO_U32(MailboxInit->Address));
				
					Adaptec->Status &= ~STAT_INIT;
					Adaptec->DataReplyLeft = 0;
				}
				break;
				
				case 0x81:
				{
					Adaptec->Mbx24bit = 0;
					
					MailboxInitExtended_t *MailboxInit = (MailboxInitExtended_t *)Adaptec->CmdBuf;

					Adaptec->MailboxCount = MailboxInit->Count;
					Adaptec->MailboxOutAddr = MailboxInit->Address;
					Adaptec->MailboxInAddr = MailboxInit->Address + (Adaptec->MailboxCount * sizeof(Mailbox32_t));
				
					AdaptecLog("Adaptec Extended Initialize Mailbox Command\n");
					AdaptecLog("Mailbox Out Address=0x%08X\n", Adaptec->MailboxOutAddr);
					AdaptecLog("Mailbox In Address=0x%08X\n", Adaptec->MailboxInAddr);
					pclog("Initialized Extended Mailbox, %d entries at 0x%08X\n", MailboxInit->Count, MailboxInit->Address);
				
					Adaptec->Status &= ~STAT_INIT;
					Adaptec->DataReplyLeft = 0;					
				}
				break;
				
				case 0x03:
				break;
				
				case 0x04:
				Adaptec->DataBuf[0] = 0x41;
				Adaptec->DataBuf[1] = 0x41; 
				Adaptec->DataBuf[2] = '4';
				Adaptec->DataBuf[3] = '7';
				Adaptec->DataReplyLeft = 4;
				break;

				case 0x84:
				Adaptec->DataBuf[0] = '0';
				Adaptec->DataReplyLeft = 1;
				break;				

				case 0x85:
				Adaptec->DataBuf[0] = 'M';
				Adaptec->DataReplyLeft = 1;
				break;
				
				case 0x06:
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x07:
				Adaptec->DataReplyLeft = 0;
				pclog("Bus-on time: %d\n", Adaptec->CmdBuf[0]);
				break;
				
				case 0x08:
				Adaptec->DataReplyLeft = 0;				
				pclog("Bus-off time: %d\n", Adaptec->CmdBuf[0]);
				break;
				
				case 0x09:
				Adaptec->DataReplyLeft = 0;				
				pclog("DMA transfer rate: %02X\n", Adaptec->CmdBuf[0]);
				break;
				
				case 0x0A:
				if (ScsiDrives[scsi_cdrom_id].LunType == SCSI_CDROM)
					Adaptec->DataBuf[scsi_cdrom_id] = 1;

				Adaptec->DataBuf[7] = 0;
				Adaptec->DataReplyLeft = 8;
				break;
				
				case 0x0B:
				Adaptec->DataBuf[0] = (1 << Adaptec->DmaChannel);
				Adaptec->DataBuf[1] = (1 << (Adaptec->Irq - 9));
				Adaptec->DataBuf[2] = 7;
				Adaptec->DataReplyLeft = 3;
				break;
				
				case 0x0D:
				{
					Adaptec->DataReplyLeft = Adaptec->CmdBuf[0];

					ReplyInquireSetupInformation *Reply = (ReplyInquireSetupInformation *)Adaptec->DataBuf;

					Reply->fSynchronousInitiationEnabled = 1;
					Reply->fParityCheckingEnabled = 1;
					Reply->cMailbox = Adaptec->MailboxCount;
					U32_TO_ADDR(Reply->MailboxAddress, Adaptec->MailboxOutAddr);
					Reply->uSignature = 'B';
					/* The 'D' signature prevents Adaptec's OS/2 drivers from getting too
					 * friendly with BusLogic hardware and upsetting the HBA state.
					 */
					Reply->uCharacterD = 'D';      /* BusLogic model. */
					Reply->uHostBusType = 'A';     /* ISA bus. */
				}
				break;
				
				case 0x8B:
				{
					int i;
		
					/* The reply length is set by the guest and is found in the first byte of the command buffer. */
					Adaptec->DataReplyLeft = Adaptec->CmdBuf[0];
					memset(Adaptec->DataBuf, 0, Adaptec->DataReplyLeft);
					const char aModelName[] = "542B ";  /* Trailing \0 is fine, that's the filler anyway. */
					int cCharsToTransfer =   Adaptec->DataReplyLeft <= sizeof(aModelName)
										   ? Adaptec->DataReplyLeft
										   : sizeof(aModelName);

					for (i = 0; i < cCharsToTransfer; i++)
						Adaptec->DataBuf[i] = aModelName[i];
				
				}
				break;
				
				case 0x8D:
				{
					Adaptec->DataReplyLeft = Adaptec->CmdBuf[0];
					ReplyInquireExtendedSetupInformation *Reply = (ReplyInquireExtendedSetupInformation *)Adaptec->DataBuf;

					Reply->uBusType = 'A';         /* ISA style */
					Reply->u16ScatterGatherLimit = 16;
					Reply->cMailbox = Adaptec->MailboxCount;
					Reply->uMailboxAddressBase = Adaptec->MailboxOutAddr;
					Reply->fLevelSensitiveInterrupt = 1;
					memcpy(Reply->aFirmwareRevision, "70M", sizeof(Reply->aFirmwareRevision));
					
				}
				break;
				
				case 0x1F:
				Adaptec->DataBuf[0] = Adaptec->CmdBuf[0];
				Adaptec->DataReplyLeft = 1;
				break;
				
				case 0x21:
				if (Adaptec->CmdParam == 1)
					Adaptec->CmdParamLeft = Adaptec->CmdBuf[0];
				
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x25:
				if (Adaptec->CmdBuf[0] == 0)
					Adaptec->IrqEnabled = 0;
				else
					Adaptec->IrqEnabled = 1;
				break;
				
				case 0x28:
				case 0x29:
				Adaptec->DataReplyLeft = 0;
				Adaptec->Status |= STAT_INVCMD;
				break;				
			}
		}
		
		if (Adaptec->DataReplyLeft)
			Adaptec->Status |= STAT_DFULL;
		else if (!Adaptec->CmdParamLeft)
			AdaptecCommandComplete(Adaptec);
		break;
		
		case 2:
		Adaptec->Irq = Val;
		break;
		
		case 3:
		Adaptec->Geometry = Val;
		break;
	}
}

static uint8_t AdaptecConvertSenseLength(uint8_t RequestSenseLength)
{
	if (RequestSenseLength == 0)
		RequestSenseLength = 14;
	else if (RequestSenseLength == 1)
		RequestSenseLength = 0;
	
	return RequestSenseLength;
}

static void AdaptecSenseBufferAllocate(AdaptecRequests_t *AdaptecRequests)
{
	uint8_t SenseLength = AdaptecConvertSenseLength(AdaptecRequests->CmdBlock.common.RequestSenseLength);
	
	if (SenseLength)
		AdaptecRequests->RequestSenseBuffer = malloc(SenseLength);
}

static void AdaptecSenseBufferFree(AdaptecRequests_t *AdaptecRequests, int Copy)
{
	uint8_t SenseLength = AdaptecConvertSenseLength(AdaptecRequests->CmdBlock.common.RequestSenseLength);	
	
	if (Copy && SenseLength)
	{
		uint32_t SenseBufferAddress;
		
		/*The Sense address, in 32-bit mode, is located in the Sense Pointer of the CCB, but in 
		24-bit mode, it is located at the end of the Command Descriptor Block. */
		
		if (AdaptecRequests->Is24bit)
		{
			SenseBufferAddress = AdaptecRequests->CCBPointer;
			SenseBufferAddress += AdaptecRequests->CmdBlock.common.CdbLength + offsetof(CCB, Cdb);
		}
		else
			SenseBufferAddress = AdaptecRequests->CmdBlock.new.SensePointer;
		
		DMAPageWrite(SenseBufferAddress, AdaptecRequests->RequestSenseBuffer, SenseLength);		
	}
	//Free the sense buffer when needed.
	free(AdaptecRequests->RequestSenseBuffer);
}

uint32_t AdaptecIoRequestCopyFromBuffer(uint32_t OffDst, SGBUF *SegmentBuffer, uint32_t Copy)
{
	Adaptec_t *Adaptec = &AdaptecLUN[scsi_cdrom_id];
	uint32_t Copied = 0;
	
	Copied = AdaptecCopySegmentBufferToGuest(Adaptec, &Adaptec->AdaptecRequests, SegmentBuffer, OffDst, Copy);
	
	return Copied;
}

uint32_t AdaptecIoRequestCopyToBuffer(uint32_t OffSrc, SGBUF *SegmentBuffer, uint32_t Copy)
{
	Adaptec_t *Adaptec = &AdaptecLUN[scsi_cdrom_id];
	uint32_t Copied = 0;
	
	Copied = AdaptecCopySegmentBufferFromGuest(Adaptec, &Adaptec->AdaptecRequests, SegmentBuffer, OffSrc, Copy);
	
	return Copied;
}

static void AdaptecSCSIRequestComplete(Adaptec_t *Adaptec, AdaptecRequests_t *AdaptecRequests)
{
	if (AdaptecRequests->RequestSenseBuffer)
		AdaptecSenseBufferFree(AdaptecRequests, (ScsiStatus != SCSI_STATUS_OK));
	
	uint8_t Status = ScsiStatus;
	uint32_t CCBPointer = AdaptecRequests->CCBPointer;
	CCBU CmdBlock;
	memcpy(&CmdBlock, &AdaptecRequests->CmdBlock, sizeof(CCBU));
	
	if (Status == SCSI_STATUS_OK)
	{
		//A Good status must return good results.
		AdaptecMailboxIn(Adaptec, CCBPointer, &CmdBlock, CCB_COMPLETE, SCSI_STATUS_OK,
													MBI_SUCCESS);
	}
	else if (ScsiStatus == SCSI_STATUS_CHECK_CONDITION)
	{
		AdaptecMailboxIn(Adaptec, CCBPointer, &CmdBlock, CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION,
													MBI_ERROR);
	}
}

static void AdaptecSCSIRequestSetup(Adaptec_t *Adaptec, uint32_t CCBPointer)
{	
	AdaptecRequests_t *AdaptecRequests = &Adaptec->AdaptecRequests;
	CCBU CmdBlock;

	//Fetch data from the Command Control Block.
	DMAPageRead(CCBPointer, &CmdBlock, sizeof(CCB32));
	
	uint8_t Id = Adaptec->Mbx24bit ? CmdBlock.old.Id : CmdBlock.new.Id;
	uint8_t Lun = Adaptec->Mbx24bit ? CmdBlock.old.Lun : CmdBlock.new.Lun;

	pclog("Scanning SCSI Target ID %d\n", Id);
	
	if (Id < ELEMENTS(ScsiDrives))
	{	
		if (Id == scsi_cdrom_id && Lun == 0)
		{
			int retcode;
			
			pclog("SCSI Target ID %d detected and working\n", Id);
							
			SCSI *Scsi = &ScsiDrives[Id];
				
			AdaptecRequests->CCBPointer = CCBPointer;
			AdaptecRequests->Is24bit = Adaptec->Mbx24bit;
				
			memcpy(&AdaptecRequests->CmdBlock, &CmdBlock, sizeof(CmdBlock));

			uint8_t SenseLength = AdaptecConvertSenseLength(CmdBlock.common.RequestSenseLength);			
			
			AdaptecSenseBufferAllocate(AdaptecRequests);
						
			uint32_t BufferSize = 0;
			AdaptecQueryDataBufferSize(Adaptec, &AdaptecRequests->CmdBlock, AdaptecRequests->Is24bit, &BufferSize);
			
			if (CmdBlock.common.ControlByte == CCB_DATA_XFER_IN)
			{
				SCSIReadTransfer(Scsi, Id);
			}
			else if (CmdBlock.common.ControlByte == CCB_DATA_XFER_OUT)
			{
				SCSIWriteTransfer(Scsi, Id);
			}		
			
			SCSISendCommand(Scsi, Id, AdaptecRequests->CmdBlock.common.Cdb, AdaptecRequests->CmdBlock.common.CdbLength, BufferSize, AdaptecRequests->RequestSenseBuffer, SenseLength);

			AdaptecSCSIRequestComplete(Adaptec, AdaptecRequests);
			
			pclog("Status %02X, Sense Key %02X, Asc %02X, Ascq %02X\n", ScsiStatus, SCSISense.SenseKey, SCSISense.Asc, SCSISense.Ascq);			
			pclog("Transfer Control %02X\n", CmdBlock.common.ControlByte);
		}
		else
		{
			AdaptecMailboxIn(Adaptec, CCBPointer, &CmdBlock, CCB_SELECTION_TIMEOUT, SCSI_STATUS_OK,
													MBI_ERROR);
		}
	}
	else
	{
		AdaptecMailboxIn(Adaptec, CCBPointer, &CmdBlock, CCB_INVALID_CCB, SCSI_STATUS_OK,
													MBI_ERROR);		
	}
}

static uint32_t AdaptecMailboxOut(Adaptec_t *Adaptec, Mailbox32_t *Mailbox32)
{	
	Mailbox_t MailboxOut;
	uint32_t Outgoing;
	
	if (Adaptec->Mbx24bit)
	{
		Outgoing = Adaptec->MailboxOutAddr + (Adaptec->MailboxOutPosCur * sizeof(Mailbox_t));
	
		DMAPageRead(Outgoing, &MailboxOut, sizeof(Mailbox_t));

		Mailbox32->CCBPointer = ADDR_TO_U32(MailboxOut.CCBPointer);
		Mailbox32->u.out.ActionCode = MailboxOut.CmdStatus;
	}
	else
	{
		Outgoing = Adaptec->MailboxOutAddr + (Adaptec->MailboxOutPosCur * sizeof(Mailbox32_t));

		DMAPageRead(Outgoing, Mailbox32, sizeof(Mailbox32_t));	
	}
	
	return Outgoing;
}

static void AdaptecStartMailbox(Adaptec_t *Adaptec)
{	
	Mailbox32_t Mailbox32;
	Mailbox_t MailboxOut;
	uint32_t Outgoing;

	uint8_t MailboxOutCur = Adaptec->MailboxOutPosCur;
	
	do
	{
		Outgoing = AdaptecMailboxOut(Adaptec, &Mailbox32);
		Adaptec->MailboxOutPosCur = (Adaptec->MailboxOutPosCur + 1) % Adaptec->MailboxCount;
	} while (Mailbox32.u.out.ActionCode == MBO_FREE && MailboxOutCur != Adaptec->MailboxOutPosCur);
	
	Adaptec->MailboxOutPosCur = MailboxOutCur;
	
	uint8_t CmdStatus = MBO_FREE;
	uint32_t CodeOffset = Adaptec->Mbx24bit ? offsetof(Mailbox_t, CmdStatus) : offsetof(Mailbox32_t, u.out.ActionCode);

	DMAPageWrite(Outgoing + CodeOffset, &CmdStatus, sizeof(CmdStatus));
	
	if (Mailbox32.u.out.ActionCode == MBO_START)
	{
		Adaptec->MailboxOutPosCur = 1; //Make sure that at least one outgoing mailbox is loaded.
		pclog("Start Mailbox Command\n");
		AdaptecSCSIRequestSetup(Adaptec, Mailbox32.CCBPointer);		
	}
}

void AdaptecCallback(void *p)
{
	Adaptec_t *Adaptec = &AdaptecLUN[scsi_cdrom_id];

	ScsiCallback[scsi_cdrom_id] = 0;
	
	AdaptecStartMailbox(Adaptec);
}

void AdaptecInit()
{
	Adaptec_t *Adaptec = &AdaptecLUN[scsi_cdrom_id];

	Adaptec->Irq = scsi_irq;
	Adaptec->DmaChannel = scsi_dma;

	pfnIoRequestCopyFromBuffer     	= AdaptecIoRequestCopyFromBuffer;
	pfnIoRequestCopyToBuffer       	= AdaptecIoRequestCopyToBuffer;

	io_sethandler(scsi_base, 0x0004, AdaptecRead, NULL, NULL, AdaptecWrite, NULL, NULL, NULL);
	timer_add(AdaptecCallback, &ScsiCallback[scsi_cdrom_id], &ScsiCallback[scsi_cdrom_id], NULL);
	pclog("Adaptec on port 0x%04X\n", scsi_base);

	AdaptecReset(Adaptec);
	Adaptec->Status |= STAT_STST;
	Adaptec->Status &= ~STAT_IDLE;
}
