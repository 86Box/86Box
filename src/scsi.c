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

uint8_t scsi_cdrom_id = 3; /*common setting*/

//Initialization function for the SCSI layer
void SCSIReset(uint8_t id, uint8_t lun)
{
	uint8_t cdrom_id = scsi_cdrom_drives[id][lun];

	if (buslogic_scsi_drive_is_cdrom(id, lun))
	{
		cdrom_reset(cdrom_id);
		SCSIDevices[id][lun].LunType = SCSI_CDROM;
	}
	else
	{
		SCSIDevices[id][lun].LunType = SCSI_NONE;
	}
}
