/* Copyright holders: SA1988, Tenshi
   see COPYING for more details
*/
/*SCSI layer emulation*/
#include <stdlib.h>
#include <string.h>
#include "86box.h"
#include "ibm.h"
#include "timer.h"
#include "device.h"
#include "cdrom.h"
#include "scsi.h"
#include "scsi_aha154x.h"
#include "scsi_buslogic.h"


uint8_t		SCSIPhase = SCSI_PHASE_BUS_FREE;
uint8_t		SCSIStatus = SCSI_STATUS_OK;
uint8_t		scsi_cdrom_id = 3; /*common setting*/
char		scsi_fn[SCSI_NUM][512];
uint16_t	scsi_hd_location[SCSI_NUM];

int		scsi_card_current = 0;
int		scsi_card_last = 0;


typedef struct {
    char	name[64];
    char	internal_name[32];
    device_t	*device;
} SCSI_CARD;


static SCSI_CARD scsi_cards[] = {
    { "None",			"none",		NULL			},
    { "Adaptec AHA-1540B",	"aha1540b",	&aha1540b_device	},
    { "Adaptec AHA-1542CF",	"aha1542cf",	&aha1542cf_device	},
    { "Adaptec AHA-1640",	"aha1640",	&aha1640_device	},
    { "BusLogic BT-542B",	"bt542b",	&buslogic_device	},
    { "BusLogic BT-958D PCI",	"bt958d",	&buslogic_pci_device	},
    { "",			"",		NULL			}
};


int scsi_card_available(int card)
{
    if (scsi_cards[card].device)
	return(device_available(scsi_cards[card].device));

    return(1);
}


char *scsi_card_getname(int card)
{
    return(scsi_cards[card].name);
}


device_t *scsi_card_getdevice(int card)
{
    return(scsi_cards[card].device);
}


int scsi_card_has_config(int card)
{
    if (! scsi_cards[card].device) return(0);

    return(scsi_cards[card].device->config ? 1 : 0);
}


char *scsi_card_get_internal_name(int card)
{
    return(scsi_cards[card].internal_name);
}


int scsi_card_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen(scsi_cards[c].internal_name)) {
	if (!strcmp(scsi_cards[c].internal_name, s))
		return(c);
	c++;
    }
	
    return(0);
}


void scsi_card_init()
{
    if (scsi_cards[scsi_card_current].device)
	device_add(scsi_cards[scsi_card_current].device);

    scsi_card_last = scsi_card_current;
}


/* Initialization function for the SCSI layer */
void SCSIReset(uint8_t id, uint8_t lun)
{
    uint8_t cdrom_id = scsi_cdrom_drives[id][lun];
    uint8_t hdc_id = scsi_hard_disks[id][lun];

    if (hdc_id != 0xff) {
	scsi_hd_reset(cdrom_id);
	SCSIDevices[id][lun].LunType = SCSI_DISK;
    } else {
	if (cdrom_id != 0xff)
	{
		cdrom_reset(cdrom_id);
		SCSIDevices[id][lun].LunType = SCSI_CDROM;
	}
	else
	{
		SCSIDevices[id][lun].LunType = SCSI_NONE;
	}
    }

    if(SCSIDevices[id][lun].CmdBuffer != NULL)
    {
	free(SCSIDevices[id][lun].CmdBuffer);
	SCSIDevices[id][lun].CmdBuffer = NULL;
    }
}
