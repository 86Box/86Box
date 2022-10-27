/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 * Emulation of the National Semiconductor PC87311 Super I/O
 *
 * Authors:	Tiseno100
 * Copyright 2020 Tiseno100
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

#define HAS_IDE_FUNCTIONALITY dev->ide_function

/* Basic Functionalities */
#define FUNCTION_ENABLE  dev->regs[0x00]
#define FUNCTION_ADDRESS dev->regs[0x01]
#define POWER_TEST       dev->regs[0x02]

/* Base Addresses */
#define LPT_BA   (FUNCTION_ADDRESS & 0x03)
#define UART1_BA ((FUNCTION_ADDRESS >> 2) & 0x03)
#define UART2_BA ((FUNCTION_ADDRESS >> 4) & 0x03)
#define COM_BA   ((FUNCTION_ADDRESS >> 6) & 0x03)

#ifdef ENABLE_PC87311_LOG
int pc87311_do_log = ENABLE_PC87311_LOG;

static void
pc87311_log(const char *fmt, ...)
{
    va_list ap;

    if (pc87311_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pc87311_log(fmt, ...)
#endif

typedef struct
{
    uint8_t   index, regs[256], cfg_lock, ide_function;
    uint16_t  base, irq;
    fdc_t    *fdc_controller;
    serial_t *uart[2];

} pc87311_t;

void pc87311_fdc_handler(pc87311_t *dev);
void pc87311_uart_handler(uint8_t num, pc87311_t *dev);
void pc87311_lpt_handler(pc87311_t *dev);
void pc87311_ide_handler(pc87311_t *dev);
void pc87311_enable(pc87311_t *dev);

static void
pc87311_write(uint16_t addr, uint8_t val, void *priv)
{
    pc87311_t *dev = (pc87311_t *) priv;

    switch (addr) {
        case 0x398:
        case 0x26e:
            dev->index = val;
            break;

        case 0x399:
        case 0x26f:
            switch (dev->index) {
                case 0x00:
                    FUNCTION_ENABLE = val;
                    break;
                case 0x01:
                    FUNCTION_ADDRESS = val;
                    break;
                case 0x02:
                    POWER_TEST = val;
                    break;
            }
            break;
    }

    pc87311_enable(dev);
}

static uint8_t
pc87311_read(uint16_t addr, void *priv)
{
    pc87311_t *dev = (pc87311_t *) priv;

    return dev->regs[dev->index];
}

void
pc87311_fdc_handler(pc87311_t *dev)
{
    fdc_remove(dev->fdc_controller);
    fdc_set_base(dev->fdc_controller, (FUNCTION_ENABLE & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
    pc87311_log("PC87311-FDC: BASE %04x\n", (FUNCTION_ENABLE & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
}

uint16_t
com3(pc87311_t *dev)
{
    switch (COM_BA) {
        case 0:
            return COM3_ADDR;
        case 1:
            return 0x0338;
        case 2:
            return COM4_ADDR;
        case 3:
            return 0x0220;
        default:
            return COM3_ADDR;
    }
}

uint16_t
com4(pc87311_t *dev)
{
    switch (COM_BA) {
        case 0:
            return COM4_ADDR;
        case 1:
            return 0x0238;
        case 2:
            return 0x02e0;
        case 3:
            return 0x0228;
        default:
            return COM4_ADDR;
    }
}

void
pc87311_uart_handler(uint8_t num, pc87311_t *dev)
{
    serial_remove(dev->uart[num & 1]);

    switch (!(num & 1) ? UART1_BA : UART2_BA) {
        case 0:
            dev->base = COM1_ADDR;
            dev->irq  = COM1_IRQ;
            break;
        case 1:
            dev->base = COM2_ADDR;
            dev->irq  = COM2_IRQ;
            break;
        case 2:
            dev->base = com3(dev);
            dev->irq  = COM3_IRQ;
            break;
        case 3:
            dev->base = com4(dev);
            dev->irq  = COM4_IRQ;
            break;
    }
    serial_setup(dev->uart[num & 1], dev->base, dev->irq);
    pc87311_log("PC87311-UART%01x: BASE %04x IRQ %01x\n", num & 1, dev->base, dev->irq);
}

void
pc87311_lpt_handler(pc87311_t *dev)
{
    lpt1_remove();
    switch (LPT_BA) {
        case 0:
            dev->base = LPT1_ADDR;
            dev->irq  = (POWER_TEST & 0x08) ? LPT1_IRQ : LPT2_IRQ;
            break;
        case 1:
            dev->base = LPT_MDA_ADDR;
            dev->irq  = LPT_MDA_IRQ;
            break;
        case 2:
            dev->base = LPT2_ADDR;
            dev->irq  = LPT2_IRQ;
            break;
    }
    lpt1_init(dev->base);
    lpt1_irq(dev->irq);
    pc87311_log("PC87311-LPT: BASE %04x IRQ %01x\n", dev->base, dev->irq);
}

void
pc87311_ide_handler(pc87311_t *dev)
{
    ide_pri_disable();
    ide_sec_disable();

    ide_set_base(0, 0x1f0);
    ide_set_side(0, 0x3f6);
    ide_pri_enable();

    if (FUNCTION_ENABLE & 0x80) {
        ide_set_base(1, 0x170);
        ide_set_side(1, 0x376);
        ide_sec_enable();
    }
    pc87311_log("PC87311-IDE: PRI %01x SEC %01x\n", (FUNCTION_ENABLE >> 6) & 1, (FUNCTION_ENABLE >> 7) & 1);
}

void
pc87311_enable(pc87311_t *dev)
{
    (FUNCTION_ENABLE & 0x01) ? pc87311_lpt_handler(dev) : lpt1_remove();
    (FUNCTION_ENABLE & 0x02) ? pc87311_uart_handler(0, dev) : serial_remove(dev->uart[0]);
    (FUNCTION_ENABLE & 0x04) ? pc87311_uart_handler(1, dev) : serial_remove(dev->uart[1]);
    (FUNCTION_ENABLE & 0x08) ? pc87311_fdc_handler(dev) : fdc_remove(dev->fdc_controller);
    if (FUNCTION_ENABLE & 0x20)
        pc87311_fdc_handler(dev);
    if (HAS_IDE_FUNCTIONALITY) {
        (FUNCTION_ENABLE & 0x40) ? pc87311_ide_handler(dev) : ide_pri_disable();
        (FUNCTION_ADDRESS & 0x80) ? pc87311_ide_handler(dev) : ide_sec_disable();
    }
}

static void
pc87311_close(void *priv)
{
    pc87311_t *dev = (pc87311_t *) priv;

    free(dev);
}

static void *
pc87311_init(const device_t *info)
{
    pc87311_t *dev = (pc87311_t *) malloc(sizeof(pc87311_t));
    memset(dev, 0, sizeof(pc87311_t));

    /* Avoid conflicting with machines that make no use of the PC87311 Internal IDE */
    HAS_IDE_FUNCTIONALITY = info->local;

    dev->fdc_controller = device_add(&fdc_at_nsc_device);
    dev->uart[0]        = device_add_inst(&ns16450_device, 1);
    dev->uart[1]        = device_add_inst(&ns16450_device, 2);

    if (HAS_IDE_FUNCTIONALITY)
        device_add(&ide_isa_2ch_device);

    io_sethandler(0x0398, 0x0002, pc87311_read, NULL, NULL, pc87311_write, NULL, NULL, dev);
    io_sethandler(0x026e, 0x0002, pc87311_read, NULL, NULL, pc87311_write, NULL, NULL, dev);

    pc87311_enable(dev);

    return dev;
}

const device_t pc87311_device = {
    .name          = "National Semiconductor PC87311",
    .internal_name = "pc87311",
    .flags         = 0,
    .local         = 0,
    .init          = pc87311_init,
    .close         = pc87311_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87311_ide_device = {
    .name          = "National Semiconductor PC87311 with IDE functionality",
    .internal_name = "pc87311_ide",
    .flags         = 0,
    .local         = 1,
    .init          = pc87311_init,
    .close         = pc87311_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
