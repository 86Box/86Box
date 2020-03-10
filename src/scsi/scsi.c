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
 * Version:	@(#)scsi.c	1.0.25	2018/10/31
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *		TheCollector1995, <mariogplayer@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include "../86box.h"
#include "../device.h"
#include "../disk/hdc.h"
#include "../disk/hdd.h"
#include "../plat.h"
#include "scsi.h"
#include "scsi_device.h"
#include "../cdrom/cdrom.h"
#include "../disk/zip.h"
#include "scsi_disk.h"
#include "scsi_aha154x.h"
#include "scsi_buslogic.h"
#include "scsi_ncr5380.h"
#include "scsi_ncr53c8xx.h"
#include "scsi_spock.h"
#ifdef WALTJE
# include "scsi_wd33c93.h"
#endif


int		scsi_card_current = 0;
int		scsi_card_last = 0;


typedef const struct {
    const char		*name;
    const char		*internal_name;
    const device_t	*device;
} SCSI_CARD;


static SCSI_CARD scsi_cards[] = {
    { "None",				"none",		NULL,			},
    { "[ISA] Adaptec AHA-154xA",	"aha154xa",	&aha154xa_device,	},
    { "[ISA] Adaptec AHA-154xB",	"aha154xb",	&aha154xb_device,	},
    { "[ISA] Adaptec AHA-154xC",	"aha154xc",	&aha154xc_device,	},
    { "[ISA] Adaptec AHA-154xCF",	"aha154xcf",	&aha154xcf_device,	},
    { "[ISA] BusLogic BT-542B",		"bt542b",	&buslogic_542b_1991_device,	},
    { "[ISA] BusLogic BT-542BH",	"bt542bh",	&buslogic_device,	},
    { "[ISA] BusLogic BT-545S",		"bt545s",	&buslogic_545s_device,	},
    { "[ISA] Longshine LCS-6821N",	"lcs6821n",	&scsi_lcs6821n_device,	},
    { "[ISA] Rancho RT1000B",		"rt1000b",	&scsi_rt1000b_device,	},
    { "[ISA] Trantor T130B",		"t130b",	&scsi_t130b_device,	},
#ifdef WALTJE
    { "[ISA] Sumo SCSI-AT",		"scsiat",	&scsi_scsiat_device,	},
    { "[ISA] Generic WDC33C93",		"wd33c93",	&scsi_wd33c93_device,	},
#endif
    { "[MCA] Adaptec AHA-1640",		"aha1640",	&aha1640_device,	},
    { "[MCA] BusLogic BT-640A",		"bt640a",	&buslogic_640a_device,	},
    { "[MCA] IBM PS/2 SCSI",		"spock",	&spock_device,		},
    { "[PCI] BusLogic BT-958D",		"bt958d",	&buslogic_pci_device,	},
    { "[PCI] NCR 53C810",		"ncr53c810",	&ncr53c810_pci_device,	},
    //{ "[PCI] NCR 53C825A",		"ncr53c825a",	&ncr53c825a_pci_device,	},
    { "[PCI] NCR 53C875",		"ncr53c875",	&ncr53c875_pci_device,	},
    { "[VLB] BusLogic BT-445S",		"bt445s",	&buslogic_445s_device,	},
    { "",				"",		NULL,			},
};


int scsi_card_available(int card)
{
    if (scsi_cards[card].device)
	return(device_available(scsi_cards[card].device));

    return(1);
}


char *scsi_card_getname(int card)
{
    return((char *) scsi_cards[card].name);
}


const device_t *scsi_card_getdevice(int card)
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
    return((char *) scsi_cards[card].internal_name);
}


int scsi_card_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen((char *) scsi_cards[c].internal_name)) {
	if (!strcmp((char *) scsi_cards[c].internal_name, s))
		return(c);
	c++;
    }
	
    return(0);
}


void scsi_card_init(void)
{
    int i;
    scsi_device_t *dev;

    if (!scsi_cards[scsi_card_current].device)
	return;

    for (i = 0; i < SCSI_ID_MAX; i++) {
	dev = &(scsi_devices[i]);

	memset(dev, 0, sizeof(scsi_device_t));
	dev->type = SCSI_NONE;
    }

    device_add(scsi_cards[scsi_card_current].device);

    scsi_card_last = scsi_card_current;
}
