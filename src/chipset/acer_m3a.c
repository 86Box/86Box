/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Acer M3A and V35N ports EAh and EBh.
 *
 *
 *
 * Authors:	Sarah Walker, <http://pcem-emulator.co.uk/>
 *		Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2019 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "mem.h"
#include "86box_io.h"
#include "rom.h"
#include "pci.h"
#include "device.h"
#include "keyboard.h"
#include "chipset.h"


typedef struct
{
    int		index;
} acerm3a_t;


static void
acerm3a_out(uint16_t port, uint8_t val, void *p)
{
    acerm3a_t *dev = (acerm3a_t *) p;

    if (port == 0xea)
	dev->index = val;
}


static uint8_t
acerm3a_in(uint16_t port, void *p)
{
    acerm3a_t *dev = (acerm3a_t *) p;

    if (port == 0xeb) {
	switch (dev->index) {
		case 2:
			return 0xfd;
	}
    }
    return 0xff;
}


static void
acerm3a_close(void *p)
{
    acerm3a_t *dev = (acerm3a_t *)p;

    free(dev);
}


static void
*acerm3a_init(const device_t *info)
{
    acerm3a_t *acerm3a = (acerm3a_t *) malloc(sizeof(acerm3a_t));
    memset(acerm3a, 0, sizeof(acerm3a_t));

    io_sethandler(0x00ea, 0x0002, acerm3a_in, NULL, NULL, acerm3a_out, NULL, NULL, acerm3a);

    return acerm3a;
}


const device_t acerm3a_device =
{
    "Acer M3A Register",
    0,
    0,
    acerm3a_init, 
    acerm3a_close, 
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
