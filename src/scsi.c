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

uint8_t SCSIPhase = SCSI_PHASE_BUS_FREE;
uint8_t SCSIStatus = SCSI_STATUS_OK;

int SCSICallback[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
uint8_t scsi_cdrom_id = 3; /*common setting*/

//Get the transfer length of the command
void SCSIGetLength(uint8_t id, int *datalen)
{
	*datalen = SCSIDevices[id].CmdBufferLength;
}

#if 0
//Execute SCSI command
void SCSIExecCommand(uint8_t id, uint8_t *buffer, uint8_t *cdb)
{
	SCSICDROM_Command(id, buffer, cdb);
}

//Read pending data from the resulting SCSI command
void SCSIReadData(uint8_t id, uint8_t *cdb, uint8_t *data, int datalen)
{
	SCSICDROM_ReadData(id, cdb, data, datalen);
}

/////
void SCSIDMAResetPosition(uint8_t Id)
{
	//Reset position in memory after reaching its limit
	SCSIDevices[Id].pos = 0;
}

//Read data from buffer with given position in buffer memory
void SCSIRead(uint8_t Id, uint8_t *dstbuf, uint8_t *srcbuf, uint32_t len_size)
{
	if (!len_size) //If there's no data, don't try to do anything.
		return;
	
	int c;
	
	for (c = 0; c <= len_size; c++) //Count as many bytes as the length of the buffer is requested
	{
		memcpy(dstbuf, srcbuf + SCSIDevices[Id].pos, len_size);
		SCSIDevices[Id].pos = c;
		
		//pclog("SCSI Read: position at %i\n", SCSIDevices[Id].pos);
	}
}

//Write data to buffer with given position in buffer memory
void SCSIWrite(uint8_t Id, uint8_t *srcbuf, uint8_t *dstbuf, uint32_t len_size)
{
	int c;
	
	for (c = 0; c <= len_size; c++) //Count as many bytes as the length of the buffer is requested
	{
		memcpy(srcbuf + SCSIDevices[Id].pos, dstbuf, len_size);
		SCSIDevices[Id].pos = c;

		//pclog("SCSI Write: position at %i\n", SCSIDevices[Id].pos);			
	}
}
/////
#endif

//Initialization function for the SCSI layer
void SCSIReset(uint8_t Id)
{
	uint8_t cdrom_id = scsi_cdrom_drives[Id];

	if (buslogic_scsi_drive_is_cdrom(Id))
	{
		SCSICallback[cdrom_id]=0;

		cdrom_reset(cdrom_id);
		SCSIDevices[Id].LunType = SCSI_CDROM;
	}
	else
	{
		SCSIDevices[Id].LunType = SCSI_NONE;
	}
}
