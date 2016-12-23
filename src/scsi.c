/* Copyright holders: SA1988, Tenshi
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

#include "timer.h"

int SCSICallback[7] = {0,0,0,0,0,0,0};
uint8_t scsi_cdrom_id = 3; /*common setting*/

//Get the transfer length of the command
void SCSIGetLength(uint8_t id, int *datalen)
{
	*datalen = SCSIDevices[id].CmdBufferLength;
}

//Execute SCSI command
void SCSIExecCommand(uint8_t id, uint8_t *cdb)
{
	SCSICDROM_Command(id, cdb);
}

//Read pending data from the resulting SCSI command
void SCSIReadData(uint8_t id, uint8_t *cdb, uint8_t *data, int datalen)
{
	SCSICDROM_ReadData(id, cdb, data, datalen);
}

//Write pending data to the resulting SCSI command
void SCSIWriteData(uint8_t id, uint8_t *cdb, uint8_t *data, int datalen)
{
	SCSICDROM_WriteData(id, cdb, data, datalen);
}

/////
void SCSIDMAResetPosition(uint8_t Id)
{
	//Reset position in memory after reaching the 
	SCSIDevices[Id].pos = 0;
}

//Read data from buffer with given position in buffer memory
void SCSIRead(uint8_t Id, uint32_t len_size)
{
	if (!len_size) //If there's no data, don't try to do anything.
		return;
	
	int c;
	
	for (c = 0; c <= len_size; c++) //Count as many bytes as the length of the buffer is requested
	{
		memcpy(SCSIDevices[Id].CmdBuffer, SCSIDevices[Id].CmdBuffer + SCSIDevices[Id].pos, len_size);
		SCSIDevices[Id].pos = c;
		
		//pclog("SCSI Read: position at %i\n", SCSIDevices[Id].pos);
	}
}

//Write data to buffer with given position in buffer memory
void SCSIWrite(uint8_t Id, uint32_t len_size)
{
	if (!len_size) //If there's no data, don't try to do anything.
		return;	
	
	int c;
	
	for (c = 0; c <= len_size; c++) //Count as many bytes as the length of the buffer is requested
	{
		memcpy(SCSIDevices[Id].CmdBuffer + SCSIDevices[Id].pos, SCSIDevices[Id].CmdBuffer, len_size);
		SCSIDevices[Id].pos = c;
		
		//Mode Sense/Select stuff
		if ((SCSIDevices[Id].pos >= prefix_len+4) && (page_flags[page_current] & PAGE_CHANGEABLE))
		{
			mode_pages_in[page_current][SCSIDevices[Id].pos - prefix_len - 4] = SCSIDevices[Id].CmdBuffer[SCSIDevices[Id].pos - 2];
			mode_pages_in[page_current][SCSIDevices[Id].pos - prefix_len - 3] = SCSIDevices[Id].CmdBuffer[SCSIDevices[Id].pos - 1];
		}

		//pclog("SCSI Write: position at %i\n", SCSIDevices[Id].pos);			
	}
}
/////

//Initialization function for the SCSI layer
void SCSIReset(uint8_t Id) 
{
	page_flags[GPMODE_CDROM_AUDIO_PAGE] &= 0xFD;		/* Clear changed flag for CDROM AUDIO mode page. */
	memset(mode_pages_in[GPMODE_CDROM_AUDIO_PAGE], 0, 256);	/* Clear the page itself. */

	if (cdrom_enabled && scsi_cdrom_enabled)
	{
		SCSICallback[Id]=0;
		SCSIDevices[Id].LunType = SCSI_CDROM;
	}
	else
	{
		SCSIDevices[Id].LunType = SCSI_NONE;
	}

	page_flags[GPMODE_CDROM_AUDIO_PAGE] &= ~PAGE_CHANGED;
	
	SCSISense.UnitAttention = 0;
}
