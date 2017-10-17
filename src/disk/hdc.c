/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Common code to handle all sorts of disk controllers.
 *
 * Version:	@(#)hdc.c	1.0.4	2017/10/16
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016,2017 Miran Grca.
 *		Copyright 2017 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../ibm.h"
#include "../cpu/cpu.h"
#include "../device.h"
#include "../machine/machine.h"
#include "hdc.h"


char	hdc_name[16];		/* configured HDC name */
int	hdc_current;


static void *
null_init(device_t *info)
{
    return(NULL);
}


static void
null_close(void *priv)
{
}


static device_t null_device = {
    "Null HDC", 0, 0,
    null_init, null_close, NULL,
    NULL, NULL, NULL, NULL, NULL
};


static void *
inthdc_init(device_t *info)
{
    return(NULL);
}


static void
inthdc_close(void *priv)
{
}


static device_t inthdc_device = {
    "Internal Controller", 0, 0,
    inthdc_init, inthdc_close, NULL,
    NULL, NULL, NULL, NULL, NULL
};


static struct {
    char	name[50];
    char	internal_name[16];
    device_t	*device;
    int		is_mfm;
} controllers[] = {
    { "None",					"none",		
      &null_device,				0		},

    { "Internal Controller",			"internal",
      &inthdc_device,				0		},

    { "[ISA] [MFM] IBM PC Fixed Disk Adapter",	"mfm_xebec",
      &mfm_xt_xebec_device,			1		},

    { "[ISA] [MFM] DTC-5150X Fixed Disk Adapter",	"mfm_dtc5150x",
      &mfm_xt_dtc5150x_device,			1		},

    { "[ISA] [MFM] IBM PC/AT Fixed Disk Adapter",	"mfm_at",
      &mfm_at_wd1003_device,			1		},

    { "[ISA] [ESDI] PC/AT ESDI Fixed Disk Adapter",	"esdi_wd1007vse1",
      &esdi_at_wd1007vse1_device,		0		},

#if 0
    { "[ISA] [IDE] PC/AT IDE Adapter",		"ide_isa",
      &ide_isa_device,				0		},

    { "[PCI] [IDE] PCI IDE Adapter",		"ide_pci",
      &ide_pci_device,				0		},
#endif

    { "[ISA] [IDE] PC/XT XTIDE",		"xtide",
      &xtide_device		,		0		},

    { "[ISA] [IDE] PC/AT XTIDE",		"xtide_at",
      &xtide_at_device,				0		},

    { "[ISA] [IDE] PS/2 XTIDE (Acculogic)",	"xtide_ps2",
      &xtide_ps2_device,			0		},

    { "[ISA] [IDE] PS/2 AT XTIDE (1.1.5)",	"xtide_at_ps2",
      &xtide_at_ps2_device,			0		},

    { "[MCA] [ESDI] IBM PS/2 ESDI Fixed Disk Adapter","esdi_mca",
      &esdi_ps2_device,				1		},

    { "",					"", NULL, 0	}
};


/* Initialize the 'hdc_current' value based on configured HDC name. */
void
hdc_init(char *name)
{
    int c;

    pclog("HDC: initializing..\n");

    for (c=0; controllers[c].device; c++) {
	if (! strcmp(name, controllers[c].internal_name)) {
		hdc_current = c;
		break;
	}
    }
}


/* Reset the HDC, whichever one that is. */
void
hdc_reset(void)
{
    pclog("HDC: reset(current=%d, internal=%d)\n",
	hdc_current, (machines[machine].flags & MACHINE_HAS_HDC)?1:0);

    /* If we have a valid controller, add its device. */
    if (hdc_current > 1)
	device_add(controllers[hdc_current].device);
}


char *
hdc_get_name(int hdc)
{
    return(controllers[hdc].name);
}


char *
hdc_get_internal_name(int hdc)
{
    return(controllers[hdc].internal_name);
}


device_t *
hdc_get_device(int hdc)
{
    return(controllers[hdc].device);
}


int
hdc_get_flags(int hdc)
{
    return(controllers[hdc].device->flags);
}


int
hdc_available(int hdc)
{
    return(device_available(controllers[hdc].device));
}


int
hdc_current_is_mfm(void)
{
    return(controllers[hdc_current].is_mfm);
}
