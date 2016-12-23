/* Copyright holders: SA1988
   see COPYING for more details
*/
/*SCSI layer emulation*/
#include <stdlib.h>
#include <string.h>
#include "86box.h"
#include "ibm.h"
#include "device.h"

#include "cdrom.h"
#include "scsi.h"

int ScsiCallback[7] = {0,0,0,0,0,0,0};
uint8_t scsi_cdrom_id = 3; /*common setting*/

void SCSIQueryResidual(SCSI *Scsi, uint32_t *Residual)
{
	*Residual = ScsiStatus == SCSI_STATUS_OK ? 0 : Scsi->SegmentData.Length;
}

static uint32_t SCSICopyFromBuffer(uint32_t OffDst, SGBUF *SegmentBuffer,
										uint32_t Copy)
{
	const SGSEG *SegmentArray = SegmentBuffer->SegmentPtr;
	unsigned SegmentNum = SegmentBuffer->SegmentNum;
	uint32_t Copied = 0;
	
	SGBUF SegmentBuffer2;
	SegmentBufferInit(&SegmentBuffer2, SegmentArray, SegmentNum);
		
	SegmentBufferAdvance(&SegmentBuffer2, OffDst);
	Copied = SegmentBufferCopy(&SegmentBuffer2, SegmentBuffer, Copy);

	return Copied;
}

static uint32_t SCSICopyToBuffer(uint32_t OffSrc, SGBUF *SegmentBuffer,
										uint32_t Copy)
{
	const SGSEG *SegmentArray = SegmentBuffer->SegmentPtr;
	unsigned SegmentNum = SegmentBuffer->SegmentNum;
	uint32_t Copied = 0;
	
	SGBUF SegmentBuffer2;
	SegmentBufferInit(&SegmentBuffer2, SegmentArray, SegmentNum);
		
	SegmentBufferAdvance(&SegmentBuffer2, OffSrc);
	Copied = SegmentBufferCopy(&SegmentBuffer2, SegmentBuffer, Copy);

	return Copied;
}

void SCSIWriteTransfer(SCSI *Scsi, uint8_t Id)
{	
	if ((Scsi->BufferPosition >= prefix_len + 4) && (page_flags[page_current] & PAGE_CHANGEABLE))
	{
		mode_pages_in[page_current][Scsi->BufferPosition - prefix_len - 4] = Scsi->SegmentData.Address[Scsi->BufferPosition - 2];
		mode_pages_in[page_current][Scsi->BufferPosition - prefix_len - 3] = Scsi->SegmentData.Address[Scsi->BufferPosition - 1];
	}
	SGBUF SegmentBuffer;
	SCSICopyToBuffer(Scsi->SegmentData.Length, &SegmentBuffer, 1);
	pfnIoRequestCopyToBuffer(0, &SegmentBuffer, Scsi->SegmentData.Length);		
}

void SCSIReadTransfer(SCSI *Scsi, uint8_t Id)
{
	SCSICDROM_ReadCallback(Scsi, Id);
	
	SGBUF SegmentBuffer;
	SCSICopyFromBuffer(Scsi->SegmentData.Length, &SegmentBuffer, 1);
	pfnIoRequestCopyFromBuffer(0, &SegmentBuffer, Scsi->SegmentData.Length);
}

void SCSISendCommand(SCSI *Scsi, uint8_t Id, uint8_t *Cdb, uint8_t CdbLength, uint32_t DataBufferLength, uint8_t *SenseBufferPointer, uint8_t SenseBufferLength)
{
	uint32_t i;
	for (i = 0; i < CdbLength; i++)
		pclog("Cdb[%d]=0x%02X\n", i, Cdb[i]);
	
	Scsi->CdbLength = CdbLength;
	
	pclog("SCSI CD-ROM in ID %d\n", Id);
	SCSICDROM_RunCommand(Scsi, Id, Cdb, SenseBufferLength, SenseBufferPointer, DataBufferLength);
}

void SCSIReset(SCSI *Scsi, uint8_t Id)
{	
	page_flags[GPMODE_CDROM_AUDIO_PAGE] &= 0xFD;		/* Clear changed flag for CDROM AUDIO mode page. */
	memset(mode_pages_in[GPMODE_CDROM_AUDIO_PAGE], 0, 256);	/* Clear the page itself. */
	
	if (scsi_cdrom_enabled)
	{
		if (cdrom_enabled)
		{
			Scsi->LunType = SCSI_CDROM;
		}
	}
	else
	{
		Scsi->LunType = SCSI_NONE;
	}	
	
	pfnIoRequestCopyFromBuffer = SCSICopyFromBuffer;
	pfnIoRequestCopyToBuffer = SCSICopyToBuffer;	
	
	page_flags[GPMODE_CDROM_AUDIO_PAGE] &= ~PAGE_CHANGED;
}