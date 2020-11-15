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
 *
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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/hdc.h>
#include <86box/hdd.h>
#include <86box/plat.h>
#include <86box/scsi.h>
#include <86box/scsi_device.h>
#include <86box/cdrom.h>
#include <86box/zip.h>
#include <86box/scsi_disk.h>
#include <86box/scsi_aha154x.h>
#include <86box/scsi_buslogic.h>
#include <86box/scsi_ncr5380.h>
#include <86box/scsi_ncr53c8xx.h>
#include <86box/scsi_pcscsi.h>
#include <86box/scsi_spock.h>
#ifdef WALTJE
# include "scsi_wd33c93.h"
#endif


int		scsi_card_current = 0;
int		scsi_card_last = 0;


typedef const struct {
    const char		*internal_name;
    const device_t	*device;
} SCSI_CARD;


static SCSI_CARD scsi_cards[] = {
    { "none",		NULL,			},
    { "aha154xa",	&aha154xa_device,	},
    { "aha154xb",	&aha154xb_device,	},
    { "aha154xc",	&aha154xc_device,	},
    { "aha154xcf",	&aha154xcf_device,	},
    { "bt542b",		&buslogic_542b_1991_device,	},
    { "bt542bh",	&buslogic_device,	},
    { "bt545s",		&buslogic_545s_device,	},
    { "lcs6821n",	&scsi_lcs6821n_device,	},
    { "rt1000b",	&scsi_rt1000b_device,	},
    { "t130b",		&scsi_t130b_device,	},
#ifdef WALTJE
    { "scsiat",		&scsi_scsiat_device,	},
    { "wd33c93",	&scsi_wd33c93_device,	},
#endif
    { "aha1640",	&aha1640_device,	},
    { "bt640a",		&buslogic_640a_device,	},
    { "spock",		&spock_device,		},
    { "bt958d",		&buslogic_pci_device,	},
    { "ncr53c810",	&ncr53c810_pci_device,	},
    { "ncr53c825a",	&ncr53c825a_pci_device,	},
    { "ncr53c860",	&ncr53c860_pci_device,	},
    { "ncr53c875",	&ncr53c875_pci_device,	},
    { "dc390",		&dc390_pci_device,	},
    { "bt445s",		&buslogic_445s_device,	},
    { "",		NULL,			},
};


int
scsi_card_available(int card)
{
    if (scsi_cards[card].device)
	return(device_available(scsi_cards[card].device));

    return(1);
}


const device_t *
scsi_card_getdevice(int card)
{
    return(scsi_cards[card].device);
}


int
scsi_card_has_config(int card)
{
    if (! scsi_cards[card].device) return(0);

    return(scsi_cards[card].device->config ? 1 : 0);
}


char *
scsi_card_get_internal_name(int card)
{
    return((char *) scsi_cards[card].internal_name);
}


int
scsi_card_get_from_internal_name(char *s)
{
    int c = 0;

    while (strlen((char *) scsi_cards[c].internal_name)) {
	if (!strcmp((char *) scsi_cards[c].internal_name, s))
		return(c);
	c++;
    }
	
    return(0);
}


void
scsi_card_init(void)
{
    if (!scsi_cards[scsi_card_current].device)
	return;

    device_add(scsi_cards[scsi_card_current].device);

    scsi_card_last = scsi_card_current;
}
