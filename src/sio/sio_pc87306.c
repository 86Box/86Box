/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the NatSemi PC87306 Super I/O chip.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2018 Miran Grca.
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
#include <86box/machine.h>

typedef struct pc87306_t {
    uint8_t   tries;
    uint8_t   regs[29];
    uint8_t   gpio[2];
    uint16_t  gpioba;
    int       cur_reg;
    fdc_t    *fdc;
    serial_t *uart[2];
    nvr_t    *nvr;
} pc87306_t;

static void
pc87306_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    pc87306_t *dev = (pc87306_t *) priv;
    uint32_t gpio = 0xffff0000;

    dev->gpio[port & 0x0001] = val;

    if (port & 0x0001) {
        gpio |= ((uint32_t) val) << 8;
        gpio |= dev->gpio[0];
    } else {
        gpio |= ((uint32_t) dev->gpio[1]) << 8;
        gpio |= val;
    }

    (void) machine_handle_gpio(1, gpio);
}

uint8_t
pc87306_gpio_read(uint16_t port, UNUSED(void *priv))
{
    uint32_t ret = machine_handle_gpio(0, 0xffffffff);

    if (port & 0x0001)
        ret = (ret >> 8) & 0xff;
    else
        ret &= 0xff;

    return ret;
}

static void
pc87306_gpio_remove(pc87306_t *dev)
{
    if (dev->gpioba != 0x0000) {
        io_removehandler(dev->gpioba, 0x0001,
                         pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL, dev);
        io_removehandler(dev->gpioba + 1, 0x0001,
                         pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL, dev);
    }
}

static void
pc87306_gpio_init(pc87306_t *dev)
{
    dev->gpioba = ((uint16_t) dev->regs[0x0f]) << 2;

    if (dev->gpioba != 0x0000) {
        if ((dev->regs[0x12]) & 0x10)
            io_sethandler(dev->gpioba, 0x0001,
                          pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL, dev);

        if ((dev->regs[0x12]) & 0x20)
            io_sethandler(dev->gpioba + 1, 0x0001,
                          pc87306_gpio_read, NULL, NULL, pc87306_gpio_write, NULL, NULL, dev);
    }
}

static void
pc87306_gpio_handler(pc87306_t *dev)
{
    pc87306_gpio_remove(dev);
    pc87306_gpio_init(dev);
}

static void
lpt1_handler(pc87306_t *dev)
{
    int      temp;
    uint16_t lptba;
    uint16_t lpt_port = LPT1_ADDR;
    uint8_t  lpt_irq = LPT2_IRQ;

    temp  = dev->regs[0x01] & 3;
    lptba = ((uint16_t) dev->regs[0x19]) << 2;

    switch (temp) {
        case 0:
            lpt_port = LPT1_ADDR;
            lpt_irq  = (dev->regs[0x02] & 0x08) ? LPT1_IRQ : LPT2_IRQ;
            break;
        case 1:
            if (dev->regs[0x1b] & 0x40)
                lpt_port = lptba;
            else
                lpt_port = LPT_MDA_ADDR;
            lpt_irq = LPT_MDA_IRQ;
            break;
        case 2:
            lpt_port = LPT2_ADDR;
            lpt_irq  = LPT2_IRQ;
            break;
        case 3:
            lpt_port = 0x000;
            lpt_irq  = 0xff;
            break;

        default:
            break;
    }

    if (dev->regs[0x1b] & 0x10)
        lpt_irq = (dev->regs[0x1b] & 0x20) ? 7 : 5;

    if (lpt_port)
        lpt1_setup(lpt_port);

    lpt1_irq(lpt_irq);
}

static void
serial_handler(pc87306_t *dev, int uart)
{
    int     temp;
    uint8_t fer_irq;
    uint8_t pnp1_irq;
    uint8_t fer_shift;
    uint8_t pnp_shift;
    uint8_t irq;

    temp = (dev->regs[1] >> (2 << uart)) & 3;

    fer_shift = 2 << uart;       /* 2 for UART 1, 4 for UART 2 */
    pnp_shift = 2 + (uart << 2); /* 2 for UART 1, 6 for UART 2 */

    /* 0 = COM1 (IRQ 4), 1 = COM2 (IRQ 3), 2 = COM3 (IRQ 4), 3 = COM4 (IRQ 3) */
    fer_irq  = ((dev->regs[1] >> fer_shift) & 1) ? 3 : 4;
    pnp1_irq = ((dev->regs[0x1c] >> pnp_shift) & 1) ? 4 : 3;

    irq = (dev->regs[0x1c] & 1) ? pnp1_irq : fer_irq;

    switch (temp) {
        case 0:
            serial_setup(dev->uart[uart], COM1_ADDR, irq);
            break;
        case 1:
            serial_setup(dev->uart[uart], COM2_ADDR, irq);
            break;
        case 2:
            switch ((dev->regs[1] >> 6) & 3) {
                case 0:
                    serial_setup(dev->uart[uart], COM3_ADDR, irq);
                    break;
                case 1:
                    serial_setup(dev->uart[uart], 0x338, irq);
                    break;
                case 2:
                    serial_setup(dev->uart[uart], COM4_ADDR, irq);
                    break;
                case 3:
                    serial_setup(dev->uart[uart], 0x220, irq);
                    break;

                default:
                    break;
            }
            break;
        case 3:
            switch ((dev->regs[1] >> 6) & 3) {
                case 0:
                    serial_setup(dev->uart[uart], COM4_ADDR, irq);
                    break;
                case 1:
                    serial_setup(dev->uart[uart], 0x238, irq);
                    break;
                case 2:
                    serial_setup(dev->uart[uart], 0x2e0, irq);
                    break;
                case 3:
                    serial_setup(dev->uart[uart], 0x228, irq);
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static void
pc87306_write(uint16_t port, uint8_t val, void *priv)
{
    pc87306_t *dev = (pc87306_t *) priv;
    uint8_t    index;
    uint8_t    valxor;

    index = (port & 1) ? 0 : 1;

    if (index) {
        dev->cur_reg = val & 0x1f;
        dev->tries   = 0;
        return;
    } else {
        if (dev->tries) {
            if ((dev->cur_reg == 0) && (val == 8))
                val = 0x4b;
            valxor     = val ^ dev->regs[dev->cur_reg];
            dev->tries = 0;
            if ((dev->cur_reg <= 28) && (dev->cur_reg != 8)) {
                if (dev->cur_reg == 0)
                    val &= 0x5f;
                dev->regs[dev->cur_reg] = val;
            } else
                return;
        } else {
            dev->tries++;
            return;
        }
    }

    switch (dev->cur_reg) {
        case 0x00:
            if (valxor & 1) {
                lpt1_remove();
                if ((val & 1) && !(dev->regs[2] & 1))
                    lpt1_handler(dev);
            }
            if (valxor & 2) {
                serial_remove(dev->uart[0]);
                if ((val & 2) && !(dev->regs[2] & 1))
                    serial_handler(dev, 0);
            }
            if (valxor & 4) {
                serial_remove(dev->uart[1]);
                if ((val & 4) && !(dev->regs[2] & 1))
                    serial_handler(dev, 1);
            }
            if (valxor & 0x28) {
                fdc_remove(dev->fdc);
                if ((val & 8) && !(dev->regs[2] & 1))
                    fdc_set_base(dev->fdc, (val & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
            }
            break;
        case 0x01:
            if (valxor & 3) {
                lpt1_remove();
                if ((dev->regs[0] & 1) && !(dev->regs[2] & 1))
                    lpt1_handler(dev);
            }
            if (valxor & 0xcc) {
                serial_remove(dev->uart[0]);
                if ((dev->regs[0] & 2) && !(dev->regs[2] & 1))
                    serial_handler(dev, 0);
            }
            if (valxor & 0xf0) {
                serial_remove(dev->uart[1]);
                if ((dev->regs[0] & 4) && !(dev->regs[2] & 1))
                    serial_handler(dev, 1);
            }
            break;
        case 0x02:
            if (valxor & 0x01) {
                lpt1_remove();
                serial_remove(dev->uart[0x00]);
                serial_remove(dev->uart[0x01]);
                fdc_remove(dev->fdc);

                if (!(val & 1)) {
                    if (dev->regs[0x00] & 0x01)
                        lpt1_handler(dev);
                    if (dev->regs[0x00] & 0x02)
                        serial_handler(dev, 0);
                    if (dev->regs[0x00] & 0x04)
                        serial_handler(dev, 1);
                    if (dev->regs[0x00] & 0x08)
                        fdc_set_base(dev->fdc, (dev->regs[0x00] & 0x20) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
                }
            }
            if (valxor & 0x08) {
                lpt1_remove();
                if ((dev->regs[0x00] & 1) && !(dev->regs[0x02] & 1))
                    lpt1_handler(dev);
            }
            break;
        case 0x04:
            if (valxor & 0x80)
                nvr_lock_set(0x00, 256, !!(val & 0x80), dev->nvr);
            break;
        case 0x05:
            if (valxor & 0x08)
                nvr_at_handler(!!(val & 0x08), 0x0070, dev->nvr);
            if (valxor & 0x20)
                nvr_bank_set(0, !!(val & 0x20), dev->nvr);
            break;
        case 0x09:
            if (valxor & 0x44) {
                fdc_update_enh_mode(dev->fdc, (val & 4) ? 1 : 0);
                fdc_update_densel_polarity(dev->fdc, (val & 0x40) ? 1 : 0);
            }
            break;
        case 0x0f:
            if (valxor)
                pc87306_gpio_handler(dev);
            break;
        case 0x12:
            if (valxor & 0x01)
                nvr_wp_set(!!(val & 0x01), 0, dev->nvr);
            if (valxor & 0x30)
                pc87306_gpio_handler(dev);
            break;
        case 0x19:
            if (valxor) {
                lpt1_remove();
                if ((dev->regs[0] & 1) && !(dev->regs[2] & 1))
                    lpt1_handler(dev);
            }
            break;
        case 0x1b:
            if (valxor & 0x70) {
                lpt1_remove();
                if (!(val & 0x40))
                    dev->regs[0x19] = 0xEF;
                if ((dev->regs[0] & 1) && !(dev->regs[2] & 1))
                    lpt1_handler(dev);
            }
            break;
        case 0x1c:
            if (valxor) {
                serial_remove(dev->uart[0]);
                serial_remove(dev->uart[1]);

                if ((dev->regs[0] & 2) && !(dev->regs[2] & 1))
                    serial_handler(dev, 0);
                if ((dev->regs[0] & 4) && !(dev->regs[2] & 1))
                    serial_handler(dev, 1);
            }
            break;

        default:
            break;
    }
}

uint8_t
pc87306_read(uint16_t port, void *priv)
{
    pc87306_t *dev = (pc87306_t *) priv;
    uint8_t    ret = 0xff;
    uint8_t    index;

    index = (port & 1) ? 0 : 1;

    dev->tries = 0;

    if (index)
        ret = dev->cur_reg & 0x1f;
    else {
        if (dev->cur_reg == 8)
            ret = 0x70;
        else if (dev->cur_reg < 28)
            ret = dev->regs[dev->cur_reg];
    }

    return ret;
}

void
pc87306_reset_common(void *priv)
{
    pc87306_t *dev = (pc87306_t *) priv;

    memset(dev->regs, 0, 29);

    dev->regs[0x00] = 0x0B;
    dev->regs[0x01] = 0x01;
    dev->regs[0x03] = 0x01;
    dev->regs[0x05] = 0x0D;
    dev->regs[0x08] = 0x70;
    dev->regs[0x09] = 0xC0;
    dev->regs[0x0b] = 0x80;
    dev->regs[0x0f] = 0x1E;
    dev->regs[0x12] = 0x30;
    dev->regs[0x19] = 0xEF;

    /*
        0 = 360 rpm @ 500 kbps for 3.5"
        1 = Default, 300 rpm @ 500, 300, 250, 1000 kbps for 3.5"
    */
    lpt1_remove();
    lpt1_handler(dev);
    serial_remove(dev->uart[0]);
    serial_remove(dev->uart[1]);
    serial_handler(dev, 0);
    serial_handler(dev, 1);
    fdc_reset(dev->fdc);
    pc87306_gpio_init(dev);
    nvr_lock_set(0x00, 256, 0, dev->nvr);
    nvr_at_handler(0, 0x0070, dev->nvr);
    nvr_at_handler(1, 0x0070, dev->nvr);
    nvr_bank_set(0, 0, dev->nvr);
    nvr_wp_set(0, 0, dev->nvr);
}

void
pc87306_reset(void *priv)
{
    pc87306_t *dev = (pc87306_t *) priv;

    pc87306_gpio_write(0x0000, 0xff, dev);
    pc87306_gpio_write(0x0001, 0xff, dev);

    pc87306_reset_common(dev);
}

static void
pc87306_close(void *priv)
{
    pc87306_t *dev = (pc87306_t *) priv;

    free(dev);
}

static void *
pc87306_init(UNUSED(const device_t *info))
{
    pc87306_t *dev = (pc87306_t *) calloc(1, sizeof(pc87306_t));

    dev->fdc = device_add(&fdc_at_nsc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->nvr = device_add(&at_mb_nvr_device);

    dev->gpio[0] = dev->gpio[1] = 0xff;

    pc87306_reset_common(dev);

    io_sethandler(0x02e, 0x0002,
                  pc87306_read, NULL, NULL, pc87306_write, NULL, NULL, dev);

    return dev;
}

const device_t pc87306_device = {
    .name          = "National Semiconductor PC87306 Super I/O",
    .internal_name = "pc87306",
    .flags         = 0,
    .local         = 0,
    .init          = pc87306_init,
    .close         = pc87306_close,
    .reset         = pc87306_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
