/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common code to handle all sorts of hard disk images.
 *
 *
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2019 Miran Grca.
 *		Copyright 2017-2019 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "plat.h"
#include "ui.h"
#include "hdd.h"
#include "cdrom.h"


hard_disk_t	hdd[HDD_NUM];


int
hdd_init(void)
{
    /* Clear all global data. */
    memset(hdd, 0x00, sizeof(hdd));

    return(0);
}


int
hdd_string_to_bus(char *str, int cdrom)
{
    if (! strcmp(str, "none"))
	return(HDD_BUS_DISABLED);

    if (! strcmp(str, "mfm")) {
	if (cdrom) {
no_cdrom:
		ui_msgbox(MBX_ERROR, (wchar_t *)IDS_4099);
		return(0);
	}

	return(HDD_BUS_MFM);
    }

    /* FIXME: delete 'rll' in a year or so.. --FvK */
    if (!strcmp(str, "esdi") || !strcmp(str, "rll")) {
	if (cdrom) goto no_cdrom;

	return(HDD_BUS_ESDI);
    }

    if (! strcmp(str, "ide_pio_only"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "ide"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "atapi_pio_only"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "atapi"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "eide"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "xta"))
	return(HDD_BUS_XTA);

    if (! strcmp(str, "atide"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "ide_pio_and_dma"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "atapi_pio_and_dma"))
	return(HDD_BUS_IDE);

    if (! strcmp(str, "scsi"))
	return(HDD_BUS_SCSI);

    if (! strcmp(str, "usb"))
	ui_msgbox(MBX_ERROR, (wchar_t *)IDS_4110);

    return(0);
}


char *
hdd_bus_to_string(int bus, int cdrom)
{
    char *s = "none";

    switch (bus) {
	case HDD_BUS_DISABLED:
	default:
		break;

	case HDD_BUS_MFM:
		s = "mfm";
		break;

	case HDD_BUS_XTA:
		s = "xta";
		break;

	case HDD_BUS_ESDI:
		s = "esdi";
		break;

	case HDD_BUS_IDE:
		s = cdrom ? "atapi" : "ide";
		break;

	case HDD_BUS_SCSI:
		s = "scsi";
		break;
    }

    return(s);
}


int
hdd_is_valid(int c)
{
    if (hdd[c].bus == HDD_BUS_DISABLED)
	return(0);

    if (wcslen(hdd[c].fn) == 0)
	return(0);

    if ((hdd[c].tracks==0) || (hdd[c].hpc==0) || (hdd[c].spt==0))
	return(0);

    return(1);
}
