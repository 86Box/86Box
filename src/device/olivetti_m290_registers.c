/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the Olivetti M290 registers Readout
 *
 * Authors: EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2020-2021 EngiNerd
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
#include <86box/chipset.h>
#include <86box/video.h>

typedef struct
{
    uint8_t	reg_067;
    uint8_t reg_069;
} olivetti_m290_registers_t;

#ifdef ENABLE_OLIVETTI_M290_REGISTERS_LOG
int olivetti_m290_registers_do_log = ENABLE_OLIVETTI_M290_REGISTERS_LOG;
static void
olivetti_m290_registers_log(const char *fmt, ...)
{
    va_list ap;

    if (olivetti_m290_registers_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
    	va_end(ap);
    }
}
#else
#define olivetti_m290_registers_log(fmt, ...)
#endif

static void
olivetti_m290_registers_write(uint16_t addr, uint8_t val, void *priv)
{
    olivetti_m290_registers_t *dev = (olivetti_m290_registers_t *) priv;
    olivetti_m290_registers_log("Olivetti M290 registers: Write %02x at %02x\n", val, addr);
    switch (addr) {
        case 0x067:
            dev->reg_067 = val;
            break;
        case 0x069:
            dev->reg_069 = val;
            break;
    }
}

static uint8_t
olivetti_m290_registers_read(uint16_t addr, void *priv)
{
    olivetti_m290_registers_t *dev = (olivetti_m290_registers_t *) priv;
    uint8_t ret = 0xff;
    switch (addr) {
        case 0x067:
            ret = dev->reg_067;
            break;
        case 0x069:
            ret = dev->reg_069;
            break;
    }
    olivetti_m290_registers_log("Olivetti M290 registers: Read %02x at %02x\n", ret, addr);
    return ret;
}


static void
olivetti_m290_registers_close(void *priv)
{
    olivetti_m290_registers_t *dev = (olivetti_m290_registers_t *) priv;

    free(dev);
}

static void *
olivetti_m290_registers_init(const device_t *info)
{
    olivetti_m290_registers_t *dev = (olivetti_m290_registers_t *) malloc(sizeof(olivetti_m290_registers_t));
    memset(dev, 0, sizeof(olivetti_m290_registers_t));

    dev->reg_067 = 0x0;
    dev->reg_069 = 0x0;
    
    io_sethandler(0x0067, 0x0003, olivetti_m290_registers_read, NULL, NULL, olivetti_m290_registers_write, NULL, NULL, dev);

    return dev;
}

const device_t olivetti_m290_registers_device = {
    "Olivetti M290 registers Readout",
    0,
    0,
    olivetti_m290_registers_init, olivetti_m290_registers_close, NULL,
    { NULL }, NULL, NULL,
    NULL
};
