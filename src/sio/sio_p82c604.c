/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Chips & Technologies P82C604 Super I/O
 *          chip used by the Philips P3345, based on reverse engineering.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2026 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/gameport.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/nvr.h>
#include <86box/sio.h>
#include <86box/machine.h>
#ifdef ENABLE_P82C604_LOG
#include "cpu.h"
#endif

typedef struct p82c604_t {
    uint8_t   index;
    uint8_t   regs[256];

    serial_t *uart[2];
    lpt_t    *lpt;
} p82c604_t;

#ifdef ENABLE_P82C604_LOG
int p82c604_do_log = ENABLE_P82C604_LOG;

static void
p82c604_log(const char *fmt, ...)
{
    va_list ap;

    if (p82c604_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define p82c604_log(fmt, ...)
#endif

static void
p82c604_update_ports(p82c604_t *dev, int set)
{
    uint8_t  uart1_int  = 0xff;
    uint8_t  uart2_int  = 0xff;
    uint8_t  lpt_int    = 0xff;
    uint16_t uart1_addr = 0x0000;
    uint16_t uart2_addr = 0x0000;
    uint16_t lpt_addr   = 0x0000;

    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    lpt_port_remove(dev->lpt);

    if (!set)
        return;

    /*
       [xxxx000x] Parallel disabled;
       [xxxx001x] Parallel on 378h;
       [xxxx110x] Parallel on 278h.
     */
    switch (dev->regs[0x18] & 0x0e) {
        default:
        case 0x00:
            lpt_addr  = 0x0000;
            lpt_int   = 0xff;
            break;
        case 0x02:
            lpt_addr  = 0x0378;
            lpt_int   = 7;
            break;
        case 0x0c:
            lpt_addr  = 0x0278;
            lpt_int   = 5;
            break;
    }

    /*
       [0000xxxx] Serial 1 disabled, Serial 2 disabled;
       [0010xxxx] Serial 1 on 3F8h, Serial 2 disabled;
       [0011xxxx] Serial 1 disabled, Serial 2 on 3F8h;
       [1000xxxx] Serial 1 on 2F8h, Serial 2 disabled;
       [1011xxxx] Serial 1 on 2F8h, Serial 2 on 3F8h.
       [1100xxxx] Serial 1 disabled, Serial 2 on 2F8h;
       [1110xxxx] Serial 1 on 3F8h, Serial 2 on 2F8h.
     */
    switch (dev->regs[0x18] & 0xf0) {
        default:
        case 0x00:
            uart1_addr = uart2_addr = 0x0000;
            uart1_int  = uart2_int  = 0xff;
            break;
        case 0x20:
            uart1_addr = 0x03f8;
            uart2_addr = 0x0000;
            uart1_int  = 4;
            uart2_int  = 0xff;
            break;
        case 0x30:
            uart1_addr = 0x0000;
            uart2_addr = 0x03f8;
            uart1_int  = 0xff;
            uart2_int  = 4;
            break;
        case 0x80:
            uart1_addr = 0x02f8;
            uart2_addr = 0x0000;
            uart1_int  = 3;
            uart2_int  = 0xff;
            break;
        case 0xb0:
            uart1_addr = 0x02f8;
            uart2_addr = 0x03f8;
            uart1_int  = 3;
            uart2_int  = 4;
            break;
        case 0xc0:
            uart1_addr = 0x0000;
            uart2_addr = 0x02f8;
            uart1_int  = 0xff;
            uart2_int  = 3;
            break;
        case 0xe0:
            uart1_addr = 0x03f8;
            uart2_addr = 0x02f8;
            uart1_int  = 4;
            uart2_int  = 3;
            break;
    }

    /*
       [xxx0xxx] Parallel disabled.
       [xxx1xxx] Parallel enabled.
     */
    if (!(dev->regs[0x10] & 0x08)) {
        lpt_addr  = 0x0000;
        lpt_int   = 0xff;
    }

    /*
       [xxxx0xx] Serial 2 disabled;
       [xxxx1xx] Serial 2 enabled.
     */
    if (!(dev->regs[0x10] & 0x04)) {
        uart2_addr = 0x0000;
        uart2_int  = 0xff;
    }

    /*
       [xxxxx0x] Serial 2 disabled;
       [xxxxx1x] Serial 2 enabled.
     */
    if (!(dev->regs[0x10] & 0x02)) {
        uart1_addr = 0x0000;
        uart1_int  = 0xff;
    }

    if (uart1_addr != 0x0000) {
        serial_setup(dev->uart[0], uart1_addr, uart1_int);
        p82c604_log("UART 1 at %04X, IRQ %i\n", uart1_addr, uart1_int);
    }

    if (uart2_addr != 0x0000) {
        serial_setup(dev->uart[1], uart2_addr, uart2_int);
        p82c604_log("UART 2 at %04X, IRQ %i\n", uart2_addr, uart2_int);
    }

    if (lpt_addr != 0x0000) {
        lpt_port_setup(dev->lpt, lpt_addr);
        lpt_port_irq(dev->lpt, lpt_int);
        p82c604_log("LPT1 at %04X, IRQ %i\n", lpt_addr, lpt_int);
    }
}

static uint8_t
p82c604_config_read(uint16_t port, void *priv)
{
    const p82c604_t *dev = (p82c604_t *) priv;
    uint8_t          ret = 0xff;

    switch (port) {
        case 0x02dc:
            ret = dev->index;
            break;
        case 0x02dd:
            ret = dev->regs[dev->index];
            p82c604_log("[%04X:%08X] [R] %02X = %02X\n", CS, cpu_state.pc, dev->index, ret);
            break;
    }

    return ret;
}

static void
p82c604_config_write(uint16_t port, uint8_t val, void *priv)
{
    p82c604_t *dev = (p82c604_t *) priv;

    switch (port) {
        case 0x02dc:
            dev->index            = val;
            break;
        case 0x02dd:
            if ((dev->index == 0x10) || (dev->index == 0x18))
                p82c604_update_ports(dev, 0);

            dev->regs[dev->index] = val;
            p82c604_log("[%04X:%08X] [W] %02X = %02X\n", CS, cpu_state.pc, dev->index, val);

            if ((dev->index == 0x10) || (dev->index == 0x18))
                p82c604_update_ports(dev, 1);
            break;
    }
}

static void
p82c604_reset(void *priv)
{
    p82c604_t *dev = (p82c604_t *) priv;

    p82c604_update_ports(dev, 0);

    /* Set power-on defaults. */
    dev->regs[0x10] = 0x80;
    dev->regs[0x18] = 0xed;

    p82c604_update_ports(dev, 1);
}

static void
p82c604_close(void *priv)
{
    p82c604_t *dev = (p82c604_t *) priv;

    free(dev);
}

static void *
p82c604_init(const device_t *info)
{
    p82c604_t *dev = (p82c604_t *) calloc(1, sizeof(p82c604_t));

    dev->uart[0]   = device_add_inst(&ns16450_device, 1);
    dev->uart[1]   = device_add_inst(&ns16450_device, 2);

    dev->lpt       = device_add_inst(&lpt_port_device, 1);

    io_sethandler(0x02dc, 0x0002, p82c604_config_read, NULL, NULL, p82c604_config_write, NULL, NULL, dev);

    p82c604_reset(dev);

    return dev;
}

const device_t p82c604_device = {
    .name          = "82C604 Multifunction Controller",
    .internal_name = "p82c604",
    .flags         = 0,
    .local         = 0,
    .init          = p82c604_init,
    .close         = p82c604_close,
    .reset         = p82c604_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
