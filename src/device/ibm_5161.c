/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		Emulation of the IBM Expansion Unit (5161).
 *
 *
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
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/apm.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/pit.h>
#include <86box/port_92.h>
#include <86box/machine.h>


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
	case 0x210: /* Write to latch expansion bus data (ED0-ED7) */
				/* Read to verify expansion bus data (ED0-ED7) */
		break;
	case 0x214: /* Write to latch data bus bits (DO - 07) */
				/* Read data bus bits (DO - D7) */
		break;
	case 0x211: /* Read high-order address bits (A8 - A 15) */
	case 0x215: /* Read high-order address bits (A8 - A 15) */
		ret = (get_last_addr() >> 8) & 0xff;
		break;
	case 0x212: /* Read low-order address bits (A0 - A7) */
	case 0x216: /* Read low-order address bits (A0 - A7) */
		ret = get_last_addr() & 0xff;
		break;
	case 0x213: /* Write 00 to disable expansion unit */
				/* Write 01 to enable expansion unit */
				/* Read status of expansion unit
						00 = enable/disable
						01 = wait-state request flag
						02-03 = not used
						04-07 = switch position
							1 = Off
							0 =On */
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

const device_t ibm_5161_device = {
    .name = "IBM Expansion Unit (5161)",
    .internal_name = "ibm_5161",
    .flags = DEVICE_ISA,
    .local = 0,
    .init = ibm_5161_init,
    .close = ibm_5161_close,
    .reset = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
