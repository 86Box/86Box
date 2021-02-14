/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Implementation of the OPTi 82C546/82C547(Python) & 82C596/82C597(Cobra) chipsets.
 
 * Authors:	plant/nerd73,
 *          Miran Grca, <mgrca8@gmail.com>
 *          Tiseno100
 *
 * Copyright 2020 plant/nerd73.
 * Copyright 2020 Miran Grca.
 * Copyright 2021 Tiseno100.
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
#include <86box/mem.h>
#include <86box/port_92.h>
#include <86box/chipset.h>

/* Shadow RAM */

/* Register 4h: C0000-CFFFF range | Register 5h: D0000-DFFFF range */
#define CURRENT_REGISTER dev->regs[4 + !!(i & 4)]

/*
Bits 7-6: xC000-xFFFF
Bits 5-4: x8000-xBFFF
Bits 3-2: x4000-x7FFF
Bits 0-1: x0000-x3FFF

     x-y
     0 0 Read/Write AT bus
     1 0 Read from AT - Write to DRAM
     1 1 Read from DRAM - Write to DRAM
     0 1 Read from DRAM (write protected)
*/
#define CAN_READ (1 << (i - (4 * !!(i & 4))) * 2)
#define CAN_WRITE (1 << ((i - (4 * !!(i & 4))) * 2 + 1))

/* Shadow Recalc for the C/D segments */
#define SHADOW_RECALC (((CURRENT_REGISTER & CAN_READ) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((CURRENT_REGISTER & CAN_WRITE) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY))

/* Shadow Recalc for the E/F segments */
#define SHADOW_EF_RECALC (((dev->regs[6] & CAN_READ) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) | ((dev->regs[6] & CAN_WRITE) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY))

typedef struct
{
    uint8_t idx, regs[16];
} opti5x7_t;

#ifdef ENABLE_OPTI5X7_LOG
int opti5x7_do_log = ENABLE_OPTI5X7_LOG;

static void
opti5x7_log(const char *fmt, ...)
{
    va_list ap;

    if (opti5x7_do_log)
    {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#define opti5x7_log(fmt, ...)
#endif

static void
shadow_map(opti5x7_t *dev)
{
    for (int i = 0; i < 8; i++)
    {
        mem_set_mem_state_both(0xc0000 + (i << 14), 0x4000, SHADOW_RECALC);
        if (i < 2)
            mem_set_mem_state_both(0xe0000 + (i << 16), 0x10000, SHADOW_EF_RECALC);
    }
    shadowbios = !!(dev->regs[0x06] & 5);
    shadowbios_write = !!(dev->regs[0x06] & 0x0a);
    flushmmucache();
}

static void
opti5x7_write(uint16_t addr, uint8_t val, void *priv)
{
    opti5x7_t *dev = (opti5x7_t *)priv;

    switch (addr)
    {
    case 0x22:
        dev->idx = val;
        break;
    case 0x24:
        opti5x7_log("OPTi 5x7: dev->regs[%02x] = %02x\n", dev->idx, val);
        switch (dev->idx)
        {
        case 0x00: /* DRAM Configuration Register #1 */
            dev->regs[dev->idx] = val & 0x7f;
            break;
        case 0x01: /* DRAM Control Register #1 */
            dev->regs[dev->idx] = val;
            break;
        case 0x02: /* Cache Control Register #1 */
            dev->regs[dev->idx] = val;
            cpu_cache_ext_enabled = !!(dev->regs[0x02] & 0x0c);
            cpu_update_waitstates();
            break;
        case 0x03: /* Cache Control Register #2 */
            dev->regs[dev->idx] = val;
            break;
        case 0x04: /* Shadow RAM Control Register #1 */
        case 0x05: /* Shadow RAM Control Register #2 */
        case 0x06: /* Shadow RAM Control Register #3 */
            dev->regs[dev->idx] = val;
            shadow_map(dev);
            break;
        case 0x07: /* Tag Test Register */
        case 0x08: /* CPU Cache Control Register #1 */
        case 0x09: /* System Memory Function Register #1 */
        case 0x0a: /* System Memory Address Decode Register #1 */
        case 0x0b: /* System Memory Address Decode Register #2 */
            dev->regs[dev->idx] = val;
            break;
        case 0x0c: /* Extended DMA Register */
            dev->regs[dev->idx] = val & 0xcf;
            break;
        case 0x0d: /* ROMCS# Register */
        case 0x0e: /* Local Master Preemption Register */
        case 0x0f: /* Deturbo Control Register #1 */
        case 0x10: /* Cache Write-Hit Control Register */
        case 0x11: /* Master Cycle Control Register */
            dev->regs[dev->idx] = val;
            break;
        }
        break;
    }
}

static uint8_t
opti5x7_read(uint16_t addr, void *priv)
{
    opti5x7_t *dev = (opti5x7_t *)priv;

    return (addr == 0x24) ? dev->regs[dev->idx] : 0xff;
}

static void
opti5x7_close(void *priv)
{
    opti5x7_t *dev = (opti5x7_t *)priv;

    free(dev);
}

static void *
opti5x7_init(const device_t *info)
{
    opti5x7_t *dev = (opti5x7_t *)malloc(sizeof(opti5x7_t));
    memset(dev, 0, sizeof(opti5x7_t));

    io_sethandler(0x0022, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);
    io_sethandler(0x0024, 0x0001, opti5x7_read, NULL, NULL, opti5x7_write, NULL, NULL, dev);

    device_add(&port_92_device);

    return dev;
}

const device_t opti5x7_device = {
    "OPTi 82C5x6/82C5x7",
    0,
    0,
    opti5x7_init,
    opti5x7_close,
    NULL,
    {NULL},
    NULL,
    NULL,
    NULL};
