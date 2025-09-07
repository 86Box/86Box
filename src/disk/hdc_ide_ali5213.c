/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M1489 chipset.
 *
 *
 *
 * Authors: Tiseno100,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2021 Tiseno100.
 *          Copyright 2020-2021 Miran Grca.
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

#include <86box/hdc_ide.h>
#include <86box/hdc.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/plat_unused.h>
#include <86box/port_92.h>
#include <86box/smram.h>

#include <86box/chipset.h>

#ifdef ENABLE_ALI5213_LOG
int ali5213_do_log = ENABLE_ALI5213_LOG;

static void
ali5213_log(const char *fmt, ...)
{
    va_list ap;

    if (ali5213_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ali5213_log(fmt, ...)
#endif

typedef struct ali5213_t {
    uint8_t index;
    uint8_t chip_id;

    uint8_t regs[256];
} ali5213_t;

static void
ali5213_ide_handler(ali5213_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();
    if (dev->regs[0x01] & 0x01) {
        ide_pri_enable();
        if (!(dev->regs[0x35] & 0x40))
            ide_sec_enable();
    }
}

static void
ali5213_write(uint16_t addr, uint8_t val, void *priv)
{
    ali5213_t *dev = (ali5213_t *) priv;

    ali5213_log("[%04X:%08X] [W] %02X = %02X\n", CS, cpu_state.pc, addr, val);

    switch (addr) {
        case 0xf4: /* Usually it writes 30h here */
            dev->chip_id = val;
            break;

        case 0xf8:
            dev->index = val;
            break;

        case 0xfc:
            if (dev->chip_id != 0x30)
                break;

            switch (dev->index) {
                case 0x01: /* IDE Configuration Register */
                    dev->regs[dev->index] = val & 0x8f;
                    ali5213_ide_handler(dev);
                    break;
                case 0x02: /* DBA Data Byte Cative Count for IDE-1 */
                case 0x03: /* D0RA Disk 0 Read Active Count for IDE-1 */
                case 0x04: /* D0WA Disk 0 Write Active Count for IDE-1 */
                case 0x05: /* D1RA Disk 1 Read Active Count for IDE-1 */
                case 0x06: /* D1WA Disk 1 Write Active Count for IDE-1 */
                case 0x25: /* DBR Data Byte Recovery Count for IDE-1 */
                case 0x26: /* D0RR Disk 0 Read Byte Recovery Count for IDE-1 */
                case 0x27: /* D0WR Disk 0 Write Byte Recovery Count for IDE-1 */
                case 0x28: /* D1RR Disk 1 Read Byte Recovery Count for IDE-1 */
                case 0x29: /* D1WR Disk 1 Write Byte Recovery Count for IDE-1 */
                case 0x2a: /* DBA Data Byte Cative Count for IDE-2 */
                case 0x2b: /* D0RA Disk 0 Read Active Count for IDE-2 */
                case 0x2c: /* D0WA Disk 0 Write Active Count for IDE-2 */
                case 0x2d: /* D1RA Disk 1 Read Active Count for IDE-2 */
                case 0x2e: /* D1WA Disk 1 Write Active Count for IDE-2 */
                case 0x2f: /* DBR Data Byte Recovery Count for IDE-2 */
                case 0x30: /* D0RR Disk 0 Read Byte Recovery Count for IDE-2 */
                case 0x31: /* D0WR Disk 0 Write Byte Recovery Count for IDE-2 */
                case 0x32: /* D1RR Disk 1 Read Byte Recovery Count for IDE-2 */
                case 0x33: /* D1WR Disk 1 Write Byte Recovery Count for IDE-2 */
                    dev->regs[dev->index] = val & 0x1f;
                    break;
                case 0x07: /* Buffer Mode Register 1 */
                    dev->regs[dev->index] = val;
                    break;
                case 0x09: /* IDEPE1 IDE Port Enable Register 1 */
                    dev->regs[dev->index] = val & 0xc3;
                    break;
                case 0x0a: /* Buffer Mode Register 2 */
                    dev->regs[dev->index] = val & 0x4f;
                    break;
                case 0x0b: /* IDE Channel 1 Disk 0 Sector Byte Count Register 1 */
                case 0x0d: /* IDE Channel 1 Disk 1 Sector Byte Count Register 1 */
                case 0x0f: /* IDE Channel 2 Disk 0 Sector Byte Count Register 1 */
                case 0x11: /* IDE Channel 2 Disk 1 Sector Byte Count Register 1 */
                    dev->regs[dev->index] = val & 0x03;
                    break;
                case 0x0c: /* IDE Channel 1 Disk 0 Sector Byte Count Register 2 */
                case 0x0e: /* IDE Channel 1 Disk 1 Sector Byte Count Register 2 */
                case 0x10: /* IDE Channel 2 Disk 1 Sector Byte Count Register 2 */
                case 0x12: /* IDE Channel 2 Disk 1 Sector Byte Count Register 2 */
                    dev->regs[dev->index] = val & 0x1f;
                    break;
                case 0x35: /* IDEPE3 IDE Port Enable Register 3 */
                    dev->regs[dev->index] = val;
                    ali5213_ide_handler(dev);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
ali5213_read(uint16_t addr, void *priv)
{
    const ali5213_t *dev = (ali5213_t *) priv;
    uint8_t          ret = 0xff;

    switch (addr) {
        case 0xf4:
            ret = dev->chip_id;
            break;
        case 0xfc:
            ret = dev->regs[dev->index];
            break;

        default:
            break;
    }

    ali5213_log("[%04X:%08X] [R] %02X = %02X\n", CS, cpu_state.pc, addr, ret);

    return ret;
}

static void
ali5213_reset(void *priv)
{
    ali5213_t *dev = (ali5213_t *) priv;

    memset(dev->regs, 0x00, 256);

    ide_pri_disable();
    ide_sec_disable();

    /* IDE registers */
    dev->regs[0x00] = 0x57;
    dev->regs[0x01] = 0x02;
    dev->regs[0x08] = 0xff;
    dev->regs[0x09] = 0x41;
    dev->regs[0x0c] = 0x02;
    dev->regs[0x0e] = 0x02;
    dev->regs[0x10] = 0x02;
    dev->regs[0x12] = 0x02;
    dev->regs[0x34] = 0xff;
    dev->regs[0x35] = 0x01;

    ali5213_ide_handler(dev);
}

static void
ali5213_close(void *priv)
{
    ali5213_t *dev = (ali5213_t *) priv;

    free(dev);
}

static void *
ali5213_init(UNUSED(const device_t *info))
{
    ali5213_t *dev = (ali5213_t *) calloc(1, sizeof(ali5213_t));

    /* M5213/M1489 IDE controller
       F4h Chip ID we write always 30h onto it
       F8h Index Port
       FCh Data Port
    */
    io_sethandler(0x0f4, 0x0001, ali5213_read, NULL, NULL, ali5213_write, NULL, NULL, dev);
    io_sethandler(0x0f8, 0x0001, ali5213_read, NULL, NULL, ali5213_write, NULL, NULL, dev);
    io_sethandler(0x0fc, 0x0001, ali5213_read, NULL, NULL, ali5213_write, NULL, NULL, dev);

    device_add(info->local ? &ide_pci_2ch_device : &ide_vlb_2ch_device);

    ali5213_reset(dev);

    return dev;
}

const device_t ide_ali1489_device = {
    .name          = "ALi M1489 IDE",
    .internal_name = "ali1489_ide",
    .flags         = 0,
    .local         = 1,
    .init          = ali5213_init,
    .close         = ali5213_close,
    .reset         = ali5213_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ide_ali5213_device = {
    .name          = "ALi M5213",
    .internal_name = "ali5213",
    .flags         = 0,
    .local         = 0,
    .init          = ali5213_init,
    .close         = ali5213_close,
    .reset         = ali5213_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
