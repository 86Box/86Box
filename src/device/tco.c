/*
 * Intel TCO Handler
 *
 * Authors:	Tiseno100,
 *
 * Copyright 2022 Tiseno100.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>

#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/tco.h>

#define ENABLE_TCO_LOG 1
#ifdef ENABLE_TCO_LOG
int tco_do_log = ENABLE_TCO_LOG;


static void
tco_log(const char *fmt, ...)
{
    va_list ap;

    if (tco_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define tco_log(fmt, ...)
#endif

void
tco_irq_update(tco_t *dev, uint16_t new_irq)
{
    dev->tco_irq = new_irq;
}

void
tco_write(uint16_t addr, uint8_t val, tco_t *dev)
{
    addr -= 0x60;
    tco_log("TCO: Write 0x%02x to Register 0x%02x\n", val, addr);

    switch(addr)
    {
        case 0x00:
            dev->regs[addr] = val;
        break;

        case 0x01:
            dev->regs[addr] = val & 0x3f;
        break;

        case 0x02: /* TCO Data in */
            dev->regs[addr] = val;
            dev->regs[0x04] |= 2;
            smi_line = 1;
        break;

        case 0x03: /* TCO Data out */
            dev->regs[addr] = val;
            dev->regs[0x04] |= 4;
            picint(dev->tco_irq);
        break;

        case 0x04:
            dev->regs[addr] &= 0x8f;
        break;

        case 0x05:
            dev->regs[addr] &= 0x1f;
        break;

        case 0x06:
            dev->regs[addr] &= 0x07;
        break;

        case 0x09:
            dev->regs[addr] = 0x0f;

            //if(val & 1)
            //    nmi = 1;
        break;

        case 0x0a:
            dev->regs[addr] = val & 0x06; // Intrusion Interrupt or SMI. We never get intruded so we never control it.
        break;

        case 0x0c ... 0x0d:
            dev->regs[addr] = val;
        break;

        case 0x10:
            dev->regs[addr] = val & 0x03;
        break;
    }
}


uint8_t
tco_read(uint16_t addr, tco_t *dev)
{
    addr -= 0x60;

    if(addr <= 0x10){
        tco_log("TCO: Read 0x%02x from Register 0x%02x\n", dev->regs[addr], addr);
        return dev->regs[addr];
    }
    else return 0;
}


static void
tco_reset(void *priv)
{
    tco_t *dev = (tco_t *) priv;

    dev->tco_irq = 9;
}


static void
tco_close(void *priv)
{
    tco_t *dev = (tco_t *) priv;

    free(dev);
}


static void *
tco_init(const device_t *info)
{
    tco_t *dev = (tco_t *) malloc(sizeof(tco_t));
    memset(dev, 0, sizeof(tco_t));

    tco_reset(dev);
    return dev;
}

const device_t tco_device = {
    .name = "Intel TCO",
    .internal_name = "tco",
    .flags = 0,
    .local = 0,
    .init = tco_init,
    .close = tco_close,
    .reset = tco_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
