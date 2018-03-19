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
 * Version:	@(#)hdc.c	1.0.12	2018/03/19
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include "../86box.h"
#include "../machine/machine.h"
#include "../device.h"
#include "hdc.h"
#include "hdc_ide.h"


char	*hdc_name;		/* configured HDC name */
int	hdc_current;


static void *
null_init(const device_t *info)
{
    return(NULL);
}


static void
null_close(void *priv)
{
}


static const device_t null_device = {
    "Null HDC", 0, 0,
    null_init, null_close, NULL,
    NULL, NULL, NULL, NULL, NULL
};


static void *
inthdc_init(const device_t *info)
{
    return(NULL);
}


static void
inthdc_close(void *priv)
{
}


static const device_t inthdc_device = {
    "Internal Controller", 0, 0,
    inthdc_init, inthdc_close, NULL,
    NULL, NULL, NULL, NULL, NULL
};


static const struct {
    const char		*name;
    const char		*internal_name;
    const device_t	*device;
} controllers[] = {
    { "None",						"none",		
      &null_device			},

    { "Internal Controller",				"internal",
      &inthdc_device			},

    { "[ISA] [MFM] IBM PC Fixed Disk Adapter",		"mfm_xt",
      &mfm_xt_xebec_device		},

    { "[ISA] [MFM] DTC-5150X Fixed Disk Adapter",	"mfm_dtc5150x",
      &mfm_xt_dtc5150x_device		},

    { "[ISA] [MFM] IBM PC/AT Fixed Disk Adapter",	"mfm_at",
      &mfm_at_wd1003_device		},

    { "[ISA] [ESDI] PC/AT ESDI Fixed Disk Adapter",	"esdi_at",
      &esdi_at_wd1007vse1_device	},

    { "[ISA] [IDE] PC/AT IDE Adapter",			"ide_isa",
      &ide_isa_device			},

    { "[ISA] [IDE] PC/AT IDE Adapter (Dual-Channel)",	"ide_isa_2ch",
      &ide_isa_2ch_device		},

    { "[ISA] [IDE] PC/AT XTIDE",			"xtide_at",
      &xtide_at_device			},

    { "[ISA] [IDE] PS/2 AT XTIDE (1.1.5)",		"xtide_at_ps2",
      &xtide_at_ps2_device		},

    { "[ISA] [XT IDE] Acculogic XT IDE",		"xtide_acculogic",
      &xtide_acculogic_device		},

    { "[ISA] [XT IDE] PC/XT XTIDE",			"xtide",
      &xtide_device			},

    { "[MCA] [ESDI] IBM PS/2 ESDI Fixed Disk Adapter","esdi_mca",
      &esdi_ps2_device			},

    { "[PCI] [IDE] PCI IDE Adapter",			"ide_pci",
      &ide_pci_device			},

    { "[PCI] [IDE] PCI IDE Adapter (Dual-Channel)",	"ide_pci_2ch",
      &ide_pci_2ch_device		},

    { "[VLB] [IDE] PC/AT IDE Adapter",			"vlb_isa",
      &ide_vlb_device			},

    { "[VLB] [IDE] PC/AT IDE Adapter (Dual-Channel)",	"vlb_isa_2ch",
      &ide_vlb_2ch_device		},

    { "",						"",
      NULL				}
};


/* Initialize the 'hdc_current' value based on configured HDC name. */
void
hdc_init(char *name)
{
    int c;

    pclog("HDC: initializing..\n");

    for (c = 0; controllers[c].device; c++) {
	if (! strcmp(name, (char *) controllers[c].internal_name)) {
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
	hdc_current, (machines[machine].flags & MACHINE_HDC)?1:0);

    /* If we have a valid controller, add its device. */
    if (hdc_current > 1)
	device_add(controllers[hdc_current].device);

    /* Reconfire and reset the IDE layer. */
    ide_ter_disable();
    ide_qua_disable();
    if (ide_enable[2])
	ide_ter_init();
    if (ide_enable[3])
	ide_qua_init();
    ide_reset_hard();
}


char *
hdc_get_name(int hdc)
{
    return((char *) controllers[hdc].name);
}


char *
hdc_get_internal_name(int hdc)
{
    return((char *) controllers[hdc].internal_name);
}


const device_t *
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
