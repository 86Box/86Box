/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the IBM Expansion Unit (5161).
 *
 * Version:	@(#)ibm_5161.c	1.0.0	2019/06/28
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *
 *		Copyright 2016-2018 Miran Grca.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include "86box.h"
#include "device.h"
#include "io.h"
#include "apm.h"
#include "dma.h"
#include "mem.h"
#include "pci.h"
#include "timer.h"
#include "pit.h"
#include "port_92.h"
#include "machine/machine.h"
#include "intel_sio.h"


typedef struct
{
    uint8_t	regs[8];
} ibm_5161_t;


static void
ibm_5161_out(uint16_t port, uint8_t val, void *priv)
{
    ibm_5161_t *dev = (ibm_5161_t *) priv;

    dev->regs[port & 0x0007] = val;
}


static uint8_t
ibm_5161_in(uint16_t port, void *priv)
{
    ibm_5161_t *dev = (ibm_5161_t *) priv;
    uint8_t ret = 0xff;

    ret = dev->regs[port & 0x0007];

    switch (port) {
	case 0x211:
	case 0x215:
		ret = (get_last_addr() >> 8) & 0xff;
		break;
	case 0x212:
	case 0x216:
		ret = get_last_addr() & 0xff;
		break;
	case 0x213:
		ret = dev->regs[3] & 0x01;
		break;
    }

    return ret;
}


static void
ibm_5161_close(void *p)
{
    ibm_5161_t *dev = (ibm_5161_t *) p;

    free(dev);
}


static void *
ibm_5161_init(const device_t *info)
{
    ibm_5161_t *dev = (ibm_5161_t *) malloc(sizeof(ibm_5161_t));
    memset(dev, 0, sizeof(ibm_5161_t));

    /* Extender Card Registers */
    io_sethandler(0x0210, 0x0004,
		  ibm_5161_in, NULL, NULL, ibm_5161_out, NULL, NULL, dev);

    /* Receiver Card Registers */
    io_sethandler(0x0214, 0x0003,
		  ibm_5161_in, NULL, NULL, ibm_5161_out, NULL, NULL, dev);

    return dev;
}


const device_t ibm_5161_device =
{
    "IBM Expansion Unit (5161)",
    0,
    0,
    ibm_5161_init,
    ibm_5161_close,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};
