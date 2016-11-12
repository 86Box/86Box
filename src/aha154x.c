/* Copyright holders: SA1988
   see COPYING for more details
*/
/*Adaptec 154x SCSI emulation*/
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

typedef struct MailboxInit_t
{
	uint8_t Count;
	addr24 Address;
} MailboxInit_t;

typedef struct MailboxInitExtended_t
{
	uint8_t Count;
	uint32_t Address;
} MailboxInitExtended_t;

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
	uint8_t CmdStatus;
	uint32_t CCBPointer;
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
//    See SCSI.H files for these statuses.
//

//
//    Bytes 16 and 17   Reserved (must be 0)
//

//
//    Bytes 18 through 18+n-1, where n=size of CDB  Command Descriptor Block
//

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

///////////////////////////////////////////////////////////////////////////////
//
// Scatter/Gather Segment List Definitions
//
///////////////////////////////////////////////////////////////////////////////

//
// Adapter limits
//

#define MAX_SG_DESCRIPTORS 17

typedef struct SGE
{
	addr24 Segment;
	addr24 SegmentPointer;
} SGE;

typedef struct AdaptecRequests_t
{
	CCB CmdBlock;
	uint8_t *RequestSenseBuffer;
	uint32_t CCBPointer;
} AdaptecRequests_t;

typedef struct Adaptec_t
{
	AdaptecRequests_t AdaptecRequests;
	uint8_t Status;
	uint8_t Interrupt;
	uint8_t Geometry;
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
	int DmaPort1, DmaData1;
	int DmaPort2, DmaData2;
	int BusOn;
	int BusOff;
	int DmaSpeed;
	int ScsiBios;
} Adaptec_t;

Adaptec_t AdaptecLUN;

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

static void AdaptecSetDMAChannel(int DmaPort1, int DmaData1, int DmaPort2, int DmaData2)
{
	dma_channel_write(DmaPort1, DmaData1);
	dma_channel_write(DmaPort2, DmaData2);
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
	Adaptec->MailboxOutPosCur = 0;
	Adaptec->MailboxInPosCur = 0;
		
	AdaptecClearInterrupt(Adaptec);
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
	
static void AdaptecInitReset(Adaptec_t *Adaptec, int Reset)
{
	AdaptecReset(Adaptec);
	if (Reset)
	{
		Adaptec->Status |= STAT_STST;
		Adaptec->Status &= ~STAT_IDLE;
	}
}

static void AdaptecMailboxLogInformation(Mailbox_t *Mailbox)
{
	AdaptecLog("Adaptec: Mailbox Dump Log\n");
	AdaptecLog("CCB Pointer=%#x\n", ADDR_TO_U32(Mailbox->CCBPointer));
	AdaptecLog("Command or Status Code=%02X\n", Mailbox->CmdStatus);
}

static void AdaptecCCBLogInformation(CCB *CmdBlock)
{
	pclog("Adaptec: CCB Dump Log\n");
	pclog("Opcode=%#x\n", CmdBlock->Opcode);
	pclog("Data Direction=%u\n", CmdBlock->ControlByte);
	pclog("Cdb Length=%d\n", CmdBlock->CdbLength);
	pclog("Sense Length=%u\n", CmdBlock->RequestSenseLength);
	pclog("Data Length=%u\n", ADDR_TO_U32(CmdBlock->DataLength));
	pclog("Data Pointer=%u\n", ADDR_TO_U32(CmdBlock->DataPointer));
	pclog("Host Adapter Status=0x%02X\n", CmdBlock->HostStatus);
	pclog("Target Device Status=0x%02X\n", CmdBlock->TargetStatus);
	pclog("Cdb[0]=%#x\n", CmdBlock->Cdb[0]);
	pclog("Cdb[1]=%#x\n", CmdBlock->Cdb[1]);
}

static void AdaptecMailboxIn(Adaptec_t *Adaptec, uint32_t CCBPointer, CCB *CmdBlock, uint8_t HostStatus, uint8_t TargetStatus, uint8_t MailboxCompletionCode)
{	
	Mailbox32_t Mailbox32;
	Mailbox_t MailboxIn;
	
	Mailbox32.CCBPointer = CCBPointer;
	Mailbox32.CmdStatus = MailboxCompletionCode;

	uint32_t Incoming = Adaptec->MailboxInAddr + (Adaptec->MailboxInPosCur * sizeof(Mailbox_t));

	if (MailboxCompletionCode != MBI_NOT_FOUND)
	{
		CmdBlock->HostStatus = HostStatus;
		CmdBlock->TargetStatus = TargetStatus;

		uint32_t CCBSize = offsetof(CCB, Cdb);
		const void *Data = (const void *)&CmdBlock;
		uint32_t l = PageLengthReadWrite(CCBPointer, CCBSize);
		//AdaptecBufferWrite(&Adaptec->AdaptecRequests, Incoming, &MailboxIn, sizeof(Mailbox_t));
		memcpy(&ram[CCBPointer], Data, l);
		CCBPointer += l;
		Data -= l;
		CCBSize += l;		
	}
	
	MailboxIn.CmdStatus = Mailbox32.CmdStatus;
	U32_TO_ADDR(MailboxIn.CCBPointer, Mailbox32.CCBPointer);
	AdaptecLog("Adaptec: Mailbox: status code=0x%02X, CCB at 0x%08X\n", MailboxIn.CmdStatus, ADDR_TO_U32(MailboxIn.CCBPointer));
	
	uint32_t MailboxSize = sizeof(Mailbox_t);
	const void *Data = (const void *)&MailboxIn;
	uint32_t l = PageLengthReadWrite(Incoming, MailboxSize);
	//AdaptecBufferWrite(&Adaptec->AdaptecRequests, Incoming, &MailboxIn, sizeof(Mailbox_t));
	memcpy(&ram[Incoming], Data, l);
	Incoming += l;
	Data -= l;
	MailboxSize += l;
	
	Adaptec->MailboxInPosCur++;
	if (Adaptec->MailboxInPosCur > Adaptec->MailboxCount)
		Adaptec->MailboxInPosCur = 0;

	Adaptec->Interrupt = INTR_MBIF | INTR_ANY;
	picint(1 << Adaptec->Irq);
}

static void AdaptecReadSGEntries(uint32_t SGList, uint32_t Entries, SGE *SG)
{
	uint32_t SGSize = Entries * sizeof(SGE);
	void *Data = (void *)SG;
	uint32_t l = PageLengthReadWrite(SGList, SGSize);
	//AdaptecBufferRead(&Adaptec->AdaptecRequests, SGList, SG, Entries * sizeof(SGE));
	memcpy(Data, &ram[SGList], l);
	SGList += l;
	Data -= l;
	SGSize += l;
}

static void AdaptecQueryDataBufferSize(Adaptec_t *Adaptec, CCB *CmdBlock, uint32_t *pBufferSize)
{	
	uint32_t BufferSize = 0;
	
	DataPointer = ADDR_TO_U32(CmdBlock->DataPointer);
	DataLength = ADDR_TO_U32(CmdBlock->DataLength);
	
	if (DataLength)
	{
		if (CmdBlock->Opcode == SCATTER_GATHER_COMMAND ||
			CmdBlock->Opcode == SCATTER_GATHER_COMMAND_RES)
		{
			uint32_t ScatterGatherRead;
			uint32_t ScatterEntry;
			SGE ScatterGatherBuffer[MAX_SG_DESCRIPTORS];
			uint32_t ScatterGatherLeft = DataLength / sizeof(SGE);
			uint32_t ScatterGatherAddrCurrent = DataPointer;
					
			do
			{
				ScatterGatherRead = (ScatterGatherLeft < ELEMENTS(ScatterGatherBuffer))
									? ScatterGatherLeft : ELEMENTS(ScatterGatherBuffer);
						
				ScatterGatherLeft -= ScatterGatherRead;

				AdaptecReadSGEntries(ScatterGatherAddrCurrent, ScatterGatherRead, ScatterGatherBuffer);
						
				for (ScatterEntry = 0; ScatterEntry < ScatterGatherRead; ScatterEntry++)
					BufferSize += ADDR_TO_U32(ScatterGatherBuffer[ScatterEntry].Segment);
						
				ScatterGatherAddrCurrent += ScatterGatherRead * sizeof(SGE);
			} while (ScatterGatherLeft > 0);
					
			AdaptecLog("Adaptec: Data Buffer Size=%u\n", BufferSize);
		}
		else if (CmdBlock->Opcode == SCSI_INITIATOR_COMMAND ||
				CmdBlock->Opcode == SCSI_INITIATOR_COMMAND_RES)
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

		uint32_t SegmentSize = Segment;
		void *Data = (void *)SegmentPointer;
		uint32_t l = PageLengthReadWrite(Address, SegmentSize);
		//AdaptecBufferRead(&Adaptec->AdaptecRequests, Address, SegmentPointer, Segment);
		memcpy(Data, &ram[Address], l);
		Address += l;
		Data -= l;
		SegmentSize += l;
		
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
		
		uint32_t SegmentSize = Segment;
		const void *Data = (const void *)SegmentPointer;
		uint32_t l = PageLengthReadWrite(Address, SegmentSize);
		//AdaptecBufferWrite(&Adaptec->AdaptecRequests, Address, SegmentPointer, Segment);
		memcpy(&ram[Address], Data, l);
		Address += l;
		Data -= l;
		SegmentSize += l;

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
	
	DataPointer = ADDR_TO_U32(AdaptecRequests->CmdBlock.DataPointer);
	DataLength = ADDR_TO_U32(AdaptecRequests->CmdBlock.DataLength);

	AdaptecLog("Adaptec: S/G Buffer Walker\n");
	AdaptecLog("Data Length=%u\n", DataLength);
	AdaptecLog("Data Buffer Copy=%u\n", Copy);
	
	if ((DataLength > 0) && (AdaptecRequests->CmdBlock.ControlByte == CCB_DATA_XFER_IN || 
		AdaptecRequests->CmdBlock.ControlByte == CCB_DATA_XFER_OUT))
	{
		if (AdaptecRequests->CmdBlock.Opcode == SCATTER_GATHER_COMMAND ||
			AdaptecRequests->CmdBlock.Opcode == SCATTER_GATHER_COMMAND_RES)
		{
			uint32_t ScatterGatherRead;
			uint32_t ScatterEntry;
			SGE ScatterGatherBuffer[MAX_SG_DESCRIPTORS];
			uint32_t ScatterGatherLeft = DataLength / sizeof(SGE);
			uint32_t ScatterGatherAddrCurrent = DataPointer;
					
			do
			{
				ScatterGatherRead = (ScatterGatherLeft < ELEMENTS(ScatterGatherBuffer))
									? ScatterGatherLeft : ELEMENTS(ScatterGatherBuffer);
						
				ScatterGatherLeft -= ScatterGatherRead;

				AdaptecReadSGEntries(ScatterGatherAddrCurrent, ScatterGatherRead, ScatterGatherBuffer);

				for (ScatterEntry = 0; ScatterEntry < ScatterGatherRead; ScatterEntry++)
				{
					uint32_t Address;
					uint32_t CopyThis;
							
					AdaptecLog("Adaptec: Scatter Entry=%u\n", ScatterEntry);
							
					Address = ADDR_TO_U32(ScatterGatherBuffer[ScatterEntry].SegmentPointer);
					CopyThis = MIN(Copy, ADDR_TO_U32(ScatterGatherBuffer[ScatterEntry].Segment));
							
					AdaptecLog("Adaptec: S/G Address=0x%04X, Copy=%u", Address, CopyThis);
							
					IoCopyWorker(Adaptec, Address, SegmentBuffer, CopyThis, &Skip);
					Copied += CopyThis;
					Copy -= CopyThis;
				}
						
				ScatterGatherAddrCurrent += ScatterGatherRead * sizeof(SGE);
			} while (ScatterGatherLeft > 0 && Copy > 0);
		}
		else if (AdaptecRequests->CmdBlock.Opcode == SCSI_INITIATOR_COMMAND ||
				AdaptecRequests->CmdBlock.Opcode == SCSI_INITIATOR_COMMAND_RES)
		{
			uint32_t Address = DataPointer;
					
			AdaptecLog("Adaptec: Non-scattered buffer\n");
			pclog("Data Pointer=%#x\n", DataPointer);
			pclog("Data Length=%u\n", DataLength);
			AdaptecLog("Pointer Address=%#x\n", Address);
					
			IoCopyWorker(Adaptec, Address, SegmentBuffer, MIN(DataLength, Copy), &Skip);
			Copied += MIN(DataLength, Copy);				
		}
	}
	
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
	Adaptec_t *Adaptec = &AdaptecLUN;
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
		if (Adaptec->ScsiBios)
		{
			Adaptec->Interrupt = INTR_MBIF | INTR_ANY;
			picint(1 << Adaptec->Irq);
		}
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
	Adaptec_t *Adaptec = &AdaptecLUN;
	AdaptecLog("Adaptec: Write Port 0x%02X, Value %02X\n", Port, Val);
	
	switch (Port & 3)
	{
		case 0:
		if ((Val & CTRL_HRST) || (Val & CTRL_SRST))
		{
			int HardReset = !!(Val & CTRL_HRST);

			AdaptecLog("Adaptec: %s reset\n", HardReset ? "hard" : "soft");
			AdaptecInitReset(Adaptec, HardReset);
			break;
		}
		
		if (Val & CTRL_IRST)
			AdaptecClearInterrupt(Adaptec);
		break;
		
		case 1:
		if ((Val == 0x02) && (Adaptec->Command == 0xFF))
		{
			if (Adaptec->MailboxCount)
			{
				ScsiCallback[scsi_cdrom_id] = 1;
			}
			break;
		}
		
		if (Adaptec->Command == 0xFF)
		{
			Adaptec->Command = Val;
			Adaptec->CmdParam = 0;
			
			Adaptec->Status &= ~(STAT_INVCMD | STAT_IDLE);
			AdaptecLog("Adaptec: Operation Code 0x%02X\n", Val);
			switch (Adaptec->Command)
			{
				case 0x03:
				case 0x00:
				case 0x04:
				case 0x84:
				case 0x85:
				case 0x0A:
				case 0x0B:
				Adaptec->CmdParamLeft = 0;
				break;
				
				case 0x07:
				case 0x08:
				case 0x09:
				case 0x0D:
				case 0x1F:
				Adaptec->CmdParamLeft = 1;
				break;
				
				case 0x06:
				Adaptec->CmdParamLeft = 4;
				break;
				
				case 0x01:
				Adaptec->CmdParamLeft = sizeof(MailboxInit_t);
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
			switch (Adaptec->Command)
			{
				case 0x00:
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x01:
				{
					MailboxInit_t *MailboxInit = (MailboxInit_t *)Adaptec->CmdBuf;
					
					Adaptec->MailboxCount = MailboxInit->Count;
					Adaptec->MailboxOutAddr = ADDR_TO_U32(MailboxInit->Address);
					Adaptec->MailboxInAddr = Adaptec->MailboxOutAddr + (Adaptec->MailboxCount * sizeof(Mailbox_t));
				
					AdaptecLog("Adaptec Initialize Mailbox Command\n");
					AdaptecLog("Mailbox Out Address=0x%08X\n", Adaptec->MailboxOutAddr);
					AdaptecLog("Mailbox In Address=0x%08X\n", Adaptec->MailboxInAddr);
					AdaptecLog("Initialized Mailbox, %d entries at 0x%08X\n", MailboxInit->Count, ADDR_TO_U32(MailboxInit->Address));
				
					Adaptec->Status &= ~STAT_INIT;
					Adaptec->DataReplyLeft = 0;
				}
				break;
				
				case 0x03:
				break;
				
				case 0x04:
				Adaptec->DataBuf[0] = 'A';
				Adaptec->DataBuf[1] = '0';
				Adaptec->DataBuf[2] = '3';
				Adaptec->DataBuf[3] = '1';
				Adaptec->DataReplyLeft = 4;
				break;

				case 0x84:
				Adaptec->DataBuf[0] = '0';
				Adaptec->DataReplyLeft = 1;
				break;
				
				case 0x85:
				Adaptec->DataBuf[0] = 'A';
				Adaptec->DataReplyLeft = 1;
				break;
				
				case 0x06:
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x07:
				Adaptec->BusOn = Adaptec->CmdBuf[0];
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x08:
				Adaptec->BusOff = Adaptec->CmdBuf[0];
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x09:
				Adaptec->DmaSpeed = Adaptec->CmdBuf[0];
				Adaptec->DataReplyLeft = 0;
				break;
				
				case 0x0A:
				if (ScsiDrives[scsi_cdrom_id].LunType == SCSI_CDROM)
					Adaptec->DataBuf[scsi_cdrom_id] = 1;

				Adaptec->DataBuf[7] = 0;
				Adaptec->DataReplyLeft = 8;
				break;
				
				case 0x0B:
				Adaptec->DataBuf[0] = (1 << Adaptec->DmaChannel);
				
				if (Adaptec->DmaChannel >= 0)
					AdaptecSetDMAChannel(Adaptec->DmaPort1, Adaptec->DmaData1,
										Adaptec->DmaPort2, Adaptec->DmaData2);
	
				Adaptec->DataBuf[1] = (1 << (Adaptec->Irq - 9));
				Adaptec->DataBuf[2] = 7;
				Adaptec->DataReplyLeft = 3;
				break;
				
				case 0x0D:
				Adaptec->DataReplyLeft = Adaptec->CmdBuf[0];
				Adaptec->DataBuf[1] = Adaptec->DmaSpeed;
				Adaptec->DataBuf[2] = Adaptec->BusOn;
				Adaptec->DataBuf[3] = Adaptec->BusOff;
				Adaptec->DataBuf[4] = Adaptec->MailboxCount;
				Adaptec->DataBuf[5] = Adaptec->MailboxOutAddr&0xFF;
				Adaptec->DataBuf[6] = (Adaptec->MailboxOutAddr>>8);
				Adaptec->DataBuf[7] = (Adaptec->MailboxOutAddr>>16);
				Adaptec->DataBuf[8+scsi_cdrom_id] = 1;
				break;
				
				case 0x1F:
				Adaptec->DataBuf[0] = Adaptec->CmdBuf[0];
				Adaptec->DataReplyLeft = 1;
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
		break;
		
		case 3:
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
	uint8_t SenseLength = AdaptecConvertSenseLength(AdaptecRequests->CmdBlock.RequestSenseLength);
	
	AdaptecRequests->RequestSenseBuffer = malloc(SenseLength);
}

static void AdaptecSenseBufferFree(AdaptecRequests_t *AdaptecRequests, int Copy)
{
	uint8_t SenseLength = AdaptecConvertSenseLength(AdaptecRequests->CmdBlock.RequestSenseLength);
		
	if (Copy && SenseLength)
	{
		uint32_t SenseBufferAddress;
			
		SenseBufferAddress = AdaptecRequests->CCBPointer;
		SenseBufferAddress += AdaptecRequests->CmdBlock.CdbLength + offsetof(CCB, Cdb);
		
		uint32_t SenseSize = SenseLength;
		const void *Data = (const void *)AdaptecRequests->RequestSenseBuffer;
		uint32_t l = PageLengthReadWrite(SenseBufferAddress, SenseSize);
		//AdaptecBufferWrite(AdaptecRequests, SenseBufferAddress, AdaptecRequests->RequestSenseBuffer, SenseLength);
		memcpy(&ram[SenseBufferAddress], Data, l);
		SenseBufferAddress += l;
		Data -= l;
		SenseSize += l;
	}
	free(AdaptecRequests->RequestSenseBuffer);
}

static void AdaptecRequestComplete(SCSI *Scsi, Adaptec_t *Adaptec,
									AdaptecRequests_t *AdaptecRequests)
{	
	if (AdaptecRequests->RequestSenseBuffer)
		AdaptecSenseBufferFree(AdaptecRequests, (ScsiStatus != SCSI_STATUS_OK));
	
	if (AdaptecRequests->CmdBlock.Opcode == SCSI_INITIATOR_COMMAND_RES ||
		AdaptecRequests->CmdBlock.Opcode == SCATTER_GATHER_COMMAND_RES)
	{
		uint32_t Residual = 0;
		SCSIQueryResidual(Scsi, &Residual);
		U32_TO_ADDR(AdaptecRequests->CmdBlock.DataLength, Residual);
	}
	
	uint8_t Status = ScsiStatus;
	uint32_t CCBPointer = AdaptecRequests->CCBPointer;
	CCB CmdBlock;
	memcpy(&CmdBlock, &AdaptecRequests->CmdBlock, sizeof(CCB));
	
	if (Status == SCSI_STATUS_OK)
		AdaptecMailboxIn(Adaptec, CCBPointer, &CmdBlock, CCB_COMPLETE, SCSI_STATUS_OK,
					MBI_SUCCESS);
	else if (Status == SCSI_STATUS_CHECK_CONDITION)
		AdaptecMailboxIn(Adaptec, CCBPointer, &CmdBlock, CCB_COMPLETE, SCSI_STATUS_CHECK_CONDITION,
					MBI_ERROR);
}

uint32_t AdaptecIoRequestCopyFromBuffer(uint32_t OffDst, SGBUF *SegmentBuffer, uint32_t Copy)
{
	Adaptec_t *Adaptec = &AdaptecLUN;
	uint32_t Copied = 0;
	
	Copied = AdaptecCopySegmentBufferToGuest(Adaptec, &Adaptec->AdaptecRequests, SegmentBuffer, OffDst, Copy);
	
	return Copied;
}

uint32_t AdaptecIoRequestCopyToBuffer(uint32_t OffSrc, SGBUF *SegmentBuffer, uint32_t Copy)
{
	Adaptec_t *Adaptec = &AdaptecLUN;
	uint32_t Copied = 0;
	
	Copied = AdaptecCopySegmentBufferFromGuest(Adaptec, &Adaptec->AdaptecRequests, SegmentBuffer, OffSrc, Copy);
	
	return Copied;	
}

static void AdaptecSCSIRequestSetup(Adaptec_t *Adaptec, uint32_t CCBPointer)
{	
	AdaptecRequests_t *AdaptecRequests = &Adaptec->AdaptecRequests;
	CCB CmdBlock;

	uint32_t CCBUSize = sizeof(CCB);
	void *Data = (void *)&CmdBlock;
	uint32_t l = PageLengthReadWrite(CCBPointer, CCBUSize);
	//AdaptecBufferRead(&Adaptec->AdaptecRequests, CCBPointer, &CmdBlock, sizeof(CCBU));
	memcpy(Data, &ram[CCBPointer], l);
	CCBPointer += l;
	Data -= l;
	CCBUSize += l;
	
	pclog("Scanning SCSI Target ID %d\n", CmdBlock.Id);	

	if (CmdBlock.Id == scsi_cdrom_id && CmdBlock.Lun == 0)
	{
		pclog("SCSI Target ID %d detected and working\n", CmdBlock.Id);
		
		SCSI *Scsi = &ScsiDrives[CmdBlock.Id];

		AdaptecRequests->CCBPointer = CCBPointer - 30;

		memcpy(&AdaptecRequests->CmdBlock, &CmdBlock, sizeof(CmdBlock));

		AdaptecSenseBufferAllocate(AdaptecRequests);
				
		uint32_t BufferSize = 0;
		AdaptecQueryDataBufferSize(Adaptec, &CmdBlock, &BufferSize);
				
		uint8_t SenseLength = AdaptecConvertSenseLength(CmdBlock.RequestSenseLength);
		
		pclog("Control Byte Transfer Direction %02X\n", CmdBlock.ControlByte);
		
		if (CmdBlock.ControlByte == CCB_DATA_XFER_OUT)
		{
			pclog("Adaptec Write Transfer\n");
			SCSIWriteTransfer(Scsi, CmdBlock.Id);
		}
		else if (CmdBlock.ControlByte == CCB_DATA_XFER_IN)
		{
			pclog("Adaptec Read Transfer\n");
			SCSIReadTransfer(Scsi, CmdBlock.Id);
		}
		else
		{
			AdaptecMailboxIn(Adaptec, AdaptecRequests->CCBPointer, &CmdBlock, CCB_INVALID_DIRECTION, SCSI_STATUS_OK,
					MBI_ERROR);			
		}
		
		SCSISendCommand(Scsi, CmdBlock.Id, CmdBlock.Cdb, CmdBlock.CdbLength, BufferSize, AdaptecRequests->RequestSenseBuffer, SenseLength);

		AdaptecRequestComplete(Scsi, Adaptec, AdaptecRequests);
	}
	else
	{
		CmdBlock.Id++;
	}
}

static uint32_t AdaptecMailboxOut(Adaptec_t *Adaptec, Mailbox32_t *Mailbox32)
{	
	Mailbox_t MailboxOut;
	uint32_t Outgoing;

	Outgoing = Adaptec->MailboxOutAddr + (Adaptec->MailboxOutPosCur * sizeof(Mailbox_t));
	uint32_t MailboxSize = sizeof(Mailbox_t);
	void *Data = (void *)&MailboxOut;
	uint32_t l = PageLengthReadWrite(Outgoing, MailboxSize);
	//AdaptecBufferRead(&Adaptec->AdaptecRequests, Outgoing, &MailboxOut, sizeof(Mailbox_t));
	memcpy(Data, &ram[Outgoing], l);
	Outgoing += l;
	Data -= l;
	MailboxSize += l;
		
	Mailbox32->CCBPointer = ADDR_TO_U32(MailboxOut.CCBPointer);
	Mailbox32->CmdStatus = MailboxOut.CmdStatus;
	
	return Outgoing;
}

static void AdaptecStartMailbox(Adaptec_t *Adaptec)
{	
	Mailbox32_t Mailbox32;
	Mailbox_t MailboxOut;
	uint32_t Outgoing;
	
	uint8_t MailboxPosCur = Adaptec->MailboxOutPosCur;
	
	do 
	{
		Outgoing = AdaptecMailboxOut(Adaptec, &Mailbox32);
		Adaptec->MailboxOutPosCur = (Adaptec->MailboxOutPosCur + 1) % Adaptec->MailboxCount;

	} while ((MailboxPosCur != Adaptec->MailboxOutPosCur) && (Mailbox32.CmdStatus == MBO_FREE));

	AdaptecLog("Adaptec: Got loaded mailbox at slot %u, CCB phys 0x%08X\n", Adaptec->MailboxOutPosCur, Mailbox32.CCBPointer);

	uint8_t CmdStatus = MBO_FREE;
	unsigned CodeOffset = offsetof(Mailbox_t, CmdStatus);
	
	uint32_t MailboxOffset = Outgoing + CodeOffset;
	uint32_t CmdStatusSize = sizeof(CmdStatus);
	const void *Data = (const void *)&CmdStatus;
	uint32_t l = PageLengthReadWrite(MailboxOffset, CmdStatusSize);
	//AdaptecBufferRead(&Adaptec->AdaptecRequests, Outgoing, &MailboxOut, sizeof(Mailbox_t));
	memcpy(&ram[MailboxOffset], Data, l);
	MailboxOffset += l;
	Data -= l;
	CmdStatusSize += l;	

	Mailbox32.CmdStatus = MBO_START;
	AdaptecSCSIRequestSetup(Adaptec, Mailbox32.CCBPointer);
}

void AdaptecCallback(void *p)
{
	Adaptec_t *Adaptec = &AdaptecLUN;

	ScsiCallback[scsi_cdrom_id] = 0;
	
	if (Adaptec->MailboxCount)
	{
		AdaptecStartMailbox(Adaptec);		
	}
}

void AdaptecInit(uint8_t Id)
{
	AdaptecLUN.Irq = 11;
	AdaptecLUN.DmaChannel = 6;

	AdaptecLUN.DmaPort1 = 0xD6;
	AdaptecLUN.DmaData1 = 0xD4;
	AdaptecLUN.DmaPort2 = 0xC2;
	AdaptecLUN.DmaData2 = 0x02;		

	pfnIoRequestCopyFromBuffer     	= AdaptecIoRequestCopyFromBuffer;
	pfnIoRequestCopyToBuffer       	= AdaptecIoRequestCopyToBuffer;	

	io_sethandler(0x0334, 0x0004, AdaptecRead, NULL, NULL, AdaptecWrite, NULL, NULL, NULL);
	timer_add(AdaptecCallback, &ScsiCallback[Id], &ScsiCallback[Id], NULL);

	AdaptecReset(&AdaptecLUN);
}
