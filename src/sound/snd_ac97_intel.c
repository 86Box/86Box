/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel AC'97
 *
 *
 *
 * Authors: Tiseno100,
 *
 *          Copyright 2022 Tiseno100.
 *
 */

/*
 * Buffers, AC-Link and other things require understanding.
 * But I also need a functional board with AC'97 to continue.
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

#include <86box/snd_ac97.h>
#include <86box/snd_ac97_intel.h>

#ifdef ENABLE_INTEL_AC97_LOG
int intel_ac97_do_log = ENABLE_INTEL_AC97_LOG;
static void
intel_ac97_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_ac97_do_log) {
	va_start(ap, fmt);
	pclog_ex(fmt, ap);
	va_end(ap);
    }
}
#else
#define intel_ac97_log(fmt, ...)
#endif


/* Mixer Configuration */
static void
intel_ac97_mixer_write(uint16_t addr, uint16_t val, void *priv)
{
    intel_ac97_t *dev = (intel_ac97_t *) priv;

    addr -= dev->mixer_base;
    ac97_codec_writew(dev->mixer, addr, val);
}

static uint16_t
intel_ac97_mixer_read(uint16_t addr, void *priv)
{
    intel_ac97_t *dev = (intel_ac97_t *) priv;

    addr -= dev->mixer_base;
    return ac97_codec_readw(dev->mixer, addr);
}

void
intel_ac97_mixer_base(int enable, uint16_t addr, intel_ac97_t *dev)
{
    if(dev->mixer_base != 0)
        io_removehandler(dev->mixer_base, 256, NULL, intel_ac97_mixer_read, NULL, NULL, intel_ac97_mixer_write, NULL, dev);

    intel_ac97_log("Intel AC'97 Mixer: Base has been set on 0x%x\n", addr);
    dev->mixer_base = addr;

    if((addr != 0) && enable)
        io_sethandler(addr, 256, NULL, intel_ac97_mixer_read, NULL, NULL, intel_ac97_mixer_write, NULL, dev);
}


/* AC'97 Configuration */
void
intel_ac97_set_irq(int irq, intel_ac97_t *dev)
{
    intel_ac97_log("Intel AC'97: IRQ Base was set to %d\n");

    dev->irq = irq;
}

static void
intel_ac97_write(uint16_t addr, uint8_t val, void *priv)
{
    intel_ac97_t *dev = (intel_ac97_t *) priv;
    addr -= dev->ac97_base;

    intel_ac97_log("Intel AC'97: dev->regs[%02x] = %02x\n", addr, val);

    switch(addr)
    {
        case 0x10 ... 0x13: /* Buffer BAR */
            dev->regs[addr] = val;
        break;

        case 0x15: /* Last Valid Index */
            dev->regs[addr] &= val;
        break;

        case 0x16: /* Status */
            dev->regs[addr] &= val;
        break;

        case 0x1b: /* Control */
            dev->regs[addr] = val & 0x1f;
        break;

        case 0x2c: /* Global Control */
            dev->regs[addr] = val & 0x3f;
        break;

        case 0x2e: /* Global Control */
            dev->regs[addr] = val & 0x30;
        break;

        case 0x34: /* Codec Access Semaphore */
            dev->regs[addr] = val & 1;
        break;
    }
}


static uint8_t
intel_ac97_read(uint16_t addr, void *priv)
{
    intel_ac97_t *dev = (intel_ac97_t *) priv;
    addr -= dev->ac97_base;

    if(addr < 0x40) {
        intel_ac97_log("Intel AC'97: dev->regs[%02x] (%02x)\n", addr, dev->regs[addr]);
        return dev->regs[addr];
    }
    else
        return 0xff;
}

void
intel_ac97_base(int enable, uint16_t addr, intel_ac97_t *dev)
{
    if(dev->ac97_base != 0)
        io_removehandler(dev->ac97_base, 64, intel_ac97_read, NULL, NULL, intel_ac97_write, NULL, NULL, dev);

    intel_ac97_log("Intel AC'97: Base has been set on 0x%x\n", addr);
    dev->ac97_base = addr;

    if((addr != 0) && enable)
        io_sethandler(addr, 64, intel_ac97_read, NULL, NULL, intel_ac97_write, NULL, NULL, dev);

}


static void
intel_ac97_reset(void *priv)
{
    intel_ac97_t *dev = (intel_ac97_t *) priv;
    memset(dev->regs, 0, sizeof(dev->regs)); /* Wash out the registers */

    // We got nothing here yet
}


static void
intel_ac97_close(void *priv)
{
    intel_ac97_t *dev = (intel_ac97_t *) priv;

    free(dev);
}


static void *
intel_ac97_init(const device_t *info)
{
    intel_ac97_t *dev = (intel_ac97_t *) malloc(sizeof(intel_ac97_t));
    memset(dev, 0, sizeof(intel_ac97_t));

    intel_ac97_log("Intel AC'97: Started!\n");
    // We got nothing here yet

    dev->mixer = device_add(&ad1881_device); /* Add a mixer with no real functionality for now */

    return dev;
}

const device_t intel_ac97_device = {
    .name = "Intel AC'97 Version 2.1",
    .internal_name = "intel_ac97",
    .flags = 0,
    .local = 0,
    .init = intel_ac97_init,
    .close = intel_ac97_close,
    .reset = intel_ac97_reset,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw = NULL,
    .config = NULL
};
