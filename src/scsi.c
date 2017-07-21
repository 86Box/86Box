/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Handling of the SCSI controllers.
 *
 * NOTE:	THIS IS CURRENTLY A MESS, but will be cleaned up as I go.
 *
 * Version:	@(#)scsi.c	1.0.0	2017/06/14
 *
 * Authors:	Fred N. van Kempen, <decwiz@yahoo.com>
 *		Original Buslogic version by SA1988 and Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
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
    void	(*reset)(void *p);
} SCSI_CARD;


static SCSI_CARD scsi_cards[] = {
    { "None",			"none",		NULL,		      NULL		  },
    { "Adaptec AHA-1540B",	"aha1540b",	&aha1540b_device,     aha_device_reset    },
    { "Adaptec AHA-1542CF",	"aha1542cf",	&aha1542cf_device,    aha_device_reset    },
    { "Adaptec AHA-1640",	"aha1640",	&aha1640_device,      aha_device_reset    },
    { "BusLogic BT-542B",	"bt542b",	&buslogic_device,     BuslogicDeviceReset },
    { "BusLogic BT-958D PCI",	"bt958d",	&buslogic_pci_device, BuslogicDeviceReset },
    { "",			"",		NULL,		      NULL		  },
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


void scsi_card_reset(void)
{
    void *p = NULL;

    if (scsi_cards[scsi_card_current].device)
    {
	p = device_get_priv(scsi_cards[scsi_card_current].device);
	if (p)
	{
		if (scsi_cards[scsi_card_current].reset)
		{
			scsi_cards[scsi_card_current].reset(p);
		}
	}
    }
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
