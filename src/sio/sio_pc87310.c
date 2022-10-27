/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the NatSemi PC87310 Super I/O chip.
 *
 *
 *
 * Author:	Miran Grca, <mgrca8@gmail.com>
 *      Tiseno100
 *      EngiNerd <webmaster.crrc@yahoo.it>
 *
 *		Copyright 2020 Miran Grca.
 *      Copyright 2020 Tiseno100
 *      Copyright 2021 EngiNerd.
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
#include <86box/mem.h>
#include <86box/nvr.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

#define HAS_IDE_FUNCTIONALITY dev->ide_function

#ifdef ENABLE_PC87310_LOG
int pc87310_do_log = ENABLE_PC87310_LOG;

static void
pc87310_log(const char *fmt, ...)
{
    va_list ap;

    if (pc87310_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define pc87310_log(fmt, ...)
#endif

typedef struct {
    uint8_t tries, ide_function,
        reg;
    fdc_t    *fdc;
    serial_t *uart[2];
} pc87310_t;

static void
lpt1_handler(pc87310_t *dev)
{
    int      temp;
    uint16_t lpt_port = LPT1_ADDR;
    uint8_t  lpt_irq  = LPT1_IRQ;

    /* bits 0-1:
     * 00 378h
     * 01 3bch
     * 10 278h
     * 11 disabled
     */
    temp = dev->reg & 3;

    switch (temp) {
        case 0:
            lpt_port = LPT1_ADDR;
            break;
        case 1:
            lpt_port = LPT_MDA_ADDR;
            break;
        case 2:
            lpt_port = LPT2_ADDR;
            break;
        case 3:
            lpt_port = 0x000;
            lpt_irq  = 0xff;
            break;
    }

    if (lpt_port)
        lpt1_init(lpt_port);

    lpt1_irq(lpt_irq);
}

static void
serial_handler(pc87310_t *dev, int uart)
{
    int temp;
    /* bit 2: disable serial port 1
     * bit 3: disable serial port 2
     * bit 4: swap serial ports
     */
    temp = (dev->reg >> (2 + uart)) & 1;

    // current serial port is enabled
    if (!temp) {
        // configure serial port as COM2
        if (((dev->reg >> 4) & 1) ^ uart)
            serial_setup(dev->uart[uart], COM2_ADDR, COM2_IRQ);
        // configure serial port as COM1
        else
            serial_setup(dev->uart[uart], COM1_ADDR, COM1_IRQ);
    }
}

static void
pc87310_write(uint16_t port, uint8_t val, void *priv)
{
    pc87310_t *dev = (pc87310_t *) priv;
    uint8_t    valxor;

    // second write to config register
    if (dev->tries) {
        valxor     = val ^ dev->reg;
        dev->tries = 0;
        dev->reg   = val;
        // first write to config register
    } else {
        dev->tries++;
        return;
    }

    pc87310_log("SIO: written %01X\n", val);

    /* reconfigure parallel port */
    if (valxor & 0x03) {
        lpt1_remove();
        /* bits 0-1: 11 disable parallel port */
        if (!((val & 1) && (val & 2)))
            lpt1_handler(dev);
    }
    /* reconfigure serial ports */
    if (valxor & 0x1c) {
        serial_remove(dev->uart[0]);
        serial_remove(dev->uart[1]);
        /* bit 2: 1 disable first serial port */
        if (!(val & 4))
            serial_handler(dev, 0);
        /* bit 3: 1 disable second serial port */
        if (!(val & 8))
            serial_handler(dev, 1);
    }
    /* reconfigure IDE controller */
    if (valxor & 0x20) {
        pc87310_log("SIO: HDC disabled\n");
        ide_pri_disable();
        /* bit 5: 1 disable ide controller */
        if (!(val & 0x20) && HAS_IDE_FUNCTIONALITY) {
            pc87310_log("SIO: HDC enabled\n");
            ide_set_base(0, 0x1f0);
            ide_set_side(0, 0x3f6);
            ide_pri_enable();
        }
    }
    /* reconfigure floppy disk controller */
    if (valxor & 0x40) {
        pc87310_log("SIO: FDC disabled\n");
        fdc_remove(dev->fdc);
        /* bit 6: 1 disable fdc */
        if (!(val & 0x40)) {
            pc87310_log("SIO: FDC enabled\n");
            fdc_set_base(dev->fdc, FDC_PRIMARY_ADDR);
        }
    }
    return;
}

uint8_t
pc87310_read(uint16_t port, void *priv)
{
    pc87310_t *dev = (pc87310_t *) priv;
    uint8_t    ret = 0xff;

    dev->tries = 0;

    ret = dev->reg;

    pc87310_log("SIO: read %01X\n", ret);

    return ret;
}

void
pc87310_reset(pc87310_t *dev)
{
    dev->reg   = 0x0;
    dev->tries = 0;
    /*
        0 = 360 rpm @ 500 kbps for 3.5"
        1 = Default, 300 rpm @ 500,300,250,1000 kbps for 3.5"
    */
    lpt1_remove();
    lpt1_handler(dev);
    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    serial_handler(dev, 0);
    serial_handler(dev, 1);
    fdc_reset(dev->fdc);
    // ide_pri_enable();
}

static void
pc87310_close(void *priv)
{
    pc87310_t *dev = (pc87310_t *) priv;

    free(dev);
}

static void *
pc87310_init(const device_t *info)
{
    pc87310_t *dev = (pc87310_t *) malloc(sizeof(pc87310_t));
    memset(dev, 0, sizeof(pc87310_t));

    /* Avoid conflicting with machines that make no use of the PC87310 Internal IDE */
    HAS_IDE_FUNCTIONALITY = info->local;

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    if (HAS_IDE_FUNCTIONALITY)
        device_add(&ide_isa_device);

    pc87310_reset(dev);

    io_sethandler(0x3f3, 0x0001,
                  pc87310_read, NULL, NULL, pc87310_write, NULL, NULL, dev);

    return dev;
}

const device_t pc87310_device = {
    .name          = "National Semiconductor PC87310 Super I/O",
    .internal_name = "pc87310",
    .flags         = 0,
    .local         = 0,
    .init          = pc87310_init,
    .close         = pc87310_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87310_ide_device = {
    .name          = "National Semiconductor PC87310 Super I/O with IDE functionality",
    .internal_name = "pc87310_ide",
    .flags         = 0,
    .local         = 1,
    .init          = pc87310_init,
    .close         = pc87310_close,
    .reset         = NULL,
    { .available = NULL },
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
