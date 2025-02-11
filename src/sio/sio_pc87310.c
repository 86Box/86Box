/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NatSemi PC87310 Super I/O chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *          EngiNerd, <webmaster.crrc@yahoo.it>
 *          Tiseno100,
 *
 *          Copyright 2020-2024 Miran Grca.
 *          Copyright 2021 EngiNerd.
 *          Copyright 2020 Tiseno100.
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
#include <86box/plat_unused.h>

#define FLAG_IDE    0x00000001
#define FLAG_ALI    0x00000002

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

typedef struct pc87310_t {
    uint8_t   tries;
    uint8_t   flags;
    uint8_t   regs[2];
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
    temp = dev->regs[1] & 0x03;

    lpt1_remove();

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

        default:
            break;
    }

    if (lpt_port)
        lpt1_setup(lpt_port);

    lpt1_irq(lpt_irq);
}

static void
serial_handler(pc87310_t *dev)
{
    uint8_t temp, temp2 = 0x00;
    uint16_t base1 = 0x0000, base2 = 0x0000;
    uint8_t irq1, irq2;
    /* - Bit 2: Disable serial port 1;
     * - Bit 3: Disable serial port 2;
     * - Bit 4: Swap serial ports.
     */
    temp = (dev->regs[1] >> 2) & 0x07;

    /* - Bits 1, 0: 0, 0 = Normal (3F8 and 2F8);
     *              0, 1 = 2E8 instead of 2F8;
     *              1, 0 = 3E8 instead of 3F8 and 2E8 instead of 2F8;
     *              1, 1 = 3E8 instead of 3F8.
     *
     * If we XOR bit 0 with bit 1, we get this:
     *              0, 0 = Normal (3F8 and 2F8);
     *              0, 1 = 2E8 instead of 2F8;
     *              1, 0 = 3E8 instead of 3F8;
     *              1, 1 = 3E8 instead of 3F8 and 2E8 instead of 2F8.
     *
     * Then they become simple toggle bits.
     * Therefore, we do this for easier operation.
     */
    if (dev->flags & FLAG_ALI) {
        temp2 = dev->regs[0] & 0x03;
        temp2 ^= ((temp2 & 0x02) >> 1);
    }

    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);

    if (!(temp & 0x01)) {
        base1 = (temp & 0x04) ? COM2_ADDR : COM1_ADDR;
        if ((base1 == COM1_ADDR) && (temp2 & 0x02))
            base1 = 0x03e8;
        else if ((base1 == COM2_ADDR) && (temp2 & 0x01))
            base1 = 0x02e8;
        irq1 = (temp & 0x04) ? COM2_IRQ : COM1_IRQ;
        serial_setup(dev->uart[0], base1, irq1);
        pc87310_log("UART 1 at %04X, IRQ %i\n", base1, irq1);
    }

    if (!(temp & 0x02)) {
        base2 = (temp & 0x04) ? COM1_ADDR : COM2_ADDR;
        if ((base2 == COM1_ADDR) && (temp2 & 0x02))
            base2 = 0x03e8;
        else if ((base2 == COM2_ADDR) && (temp2 & 0x01))
            base2 = 0x02e8;
        irq2 = (temp & 0x04) ? COM1_IRQ : COM2_IRQ;
        serial_setup(dev->uart[1], base2, irq2);
        pc87310_log("UART 2 at %04X, IRQ %i\n", base2, irq2);
    }
}

static void
pc87310_write(UNUSED(uint16_t port), uint8_t val, void *priv)
{
    pc87310_t *dev = (pc87310_t *) priv;
    uint8_t    valxor;
    uint8_t    idx = (uint8_t) ((port & 0x0002) >> 1);

    pc87310_log("[%04X:%08X] [W] %02X = %02X (%i)\n", CS, cpu_state.pc, port, val, dev->tries);

    if (dev->tries) {
        /* Second write to config register. */
        valxor         = val ^ dev->regs[idx];
        dev->tries     = 0;
        dev->regs[idx] = val;

        if (idx) {
            /* Register, common to both PC87310 and ALi M5105. */
            pc87310_log("SIO: Common register written %02X\n", val);

            /* Reconfigure parallel port. */
            if (valxor & 0x03)
                /* Bits 1, 0: 1, 1 = Disable parallel port. */
                lpt1_handler(dev);

            /* Reconfigure serial ports. */
            if (valxor & 0x1c)
                serial_handler(dev);

            /* Reconfigure IDE controller. */
            if ((dev->flags & FLAG_IDE) && (valxor & 0x20))  {
                pc87310_log("SIO: HDC disabled\n");
                ide_pri_disable();
                /* Bit 5: 1 = Disable IDE controller. */
                if (!(val & 0x20)) {
                    pc87310_log("SIO: HDC enabled\n");
                    ide_set_base(0, 0x1f0);
                   ide_set_side(0, 0x3f6);
                     ide_pri_enable();
                }
            }

            /* Reconfigure floppy disk controller. */
            if (valxor & 0x40) {
                pc87310_log("SIO: FDC disabled\n");
                fdc_remove(dev->fdc);
                /* Bit 6: 1 = Disable FDC. */
                if (!(val & 0x40)) {
                    pc87310_log("SIO: FDC enabled\n");
                    fdc_set_base(dev->fdc, FDC_PRIMARY_ADDR);
                }
            }
        } else {
            /* ALi M5105 extension register. */
            pc87310_log("SIO: M5105 extension register written %02X\n", val);

            /* Reconfigure serial ports. */
            if (valxor & 0x03)
                serial_handler(dev);
        }
    } else
        /* First write to config register. */
        dev->tries++;
}

uint8_t
pc87310_read(UNUSED(uint16_t port), void *priv)
{
    pc87310_t *dev = (pc87310_t *) priv;
    uint8_t    ret = 0xff;
    uint8_t    idx = (uint8_t) ((port & 0x0002) >> 1);

    dev->tries = 0;

    ret = dev->regs[idx];

    pc87310_log("[%04X:%08X] [R] %02X = %02X\n", CS, cpu_state.pc, port, ret);

    return ret;
}

void
pc87310_reset(pc87310_t *dev)
{
    dev->regs[0] = 0x00;
    dev->regs[1] = 0x00;

    dev->tries   = 0;

    lpt1_handler(dev);
    serial_handler(dev);
    if (dev->flags & FLAG_IDE) {
        ide_pri_disable();
        ide_pri_enable();
    }
    fdc_reset(dev->fdc);
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
    pc87310_t *dev = (pc87310_t *) calloc(1, sizeof(pc87310_t));

    /* Avoid conflicting with machines that make no use of the PC87310 Internal IDE */
    dev->flags = info->local;

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16450_device, 1);
    dev->uart[1] = device_add_inst(&ns16450_device, 2);

    if (dev->flags & FLAG_IDE)
        device_add((dev->flags & FLAG_ALI) ? &ide_vlb_device : &ide_isa_device);

    pc87310_reset(dev);

    io_sethandler(0x3f3, 0x0001,
                  pc87310_read, NULL, NULL, pc87310_write, NULL, NULL, dev);

    if (dev->flags & FLAG_ALI)
        io_sethandler(0x3f1, 0x0001,
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
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t pc87310_ide_device = {
    .name          = "National Semiconductor PC87310 Super I/O with IDE functionality",
    .internal_name = "pc87310_ide",
    .flags         = 0,
    .local         = FLAG_IDE,
    .init          = pc87310_init,
    .close         = pc87310_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t ali5105_device = {
    .name          = "ALi M5105 Super I/O",
    .internal_name = "ali5105",
    .flags         = 0,
    .local         = FLAG_ALI,
    .init          = pc87310_init,
    .close         = pc87310_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
