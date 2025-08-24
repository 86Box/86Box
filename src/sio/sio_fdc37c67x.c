/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SMC FDC37C67x Super I/O Chips.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/pci.h>
#include <86box/pic.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/machine.h>
#include <86box/apm.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/video.h>
#include <86box/sio.h>
#include "cpu.h"

typedef struct fdc37c67x_t {
    uint8_t       is_compaq;
    uint8_t       max_ld;
    uint8_t       tries;
    uint8_t       port_370;
    uint8_t       gpio_reg;
    uint8_t       regs[48];
    uint8_t       ld_regs[11][256];
    uint16_t      kbc_type;
    uint16_t      superio_base;
    uint16_t      fdc_base;
    uint16_t      lpt_base;
    uint16_t      kbc_base;
    uint16_t      gpio_base; /* Set to EA */
    uint16_t      uart_base[2];
    int           locked;
    int           cur_reg;
    fdc_t        *fdc;
    void         *kbc;
    serial_t     *uart[2];
    lpt_t        *lpt;
} fdc37c67x_t;

static void    fdc37c67x_write(uint16_t port, uint8_t val, void *priv);
static uint8_t fdc37c67x_read(uint16_t port, void *priv);

static uint16_t
make_port_superio(const fdc37c67x_t *dev)
{
    const uint16_t r0 = dev->regs[0x26];
    const uint16_t r1 = dev->regs[0x27];

    const uint16_t p = (r1 << 8) + r0;

    return p;
}

static uint16_t
make_port(const fdc37c67x_t *dev, const uint8_t ld)
{
    const uint16_t r0 = dev->ld_regs[ld][0x60];
    const uint16_t r1 = dev->ld_regs[ld][0x61];

    const uint16_t p = (r0 << 8) + r1;

    return p;
}

static uint8_t
fdc37c67x_gpio_read(uint16_t port, void *priv)
{
    const fdc37c67x_t *dev = (fdc37c67x_t *) priv;
    uint8_t            ret = 0xff;

    if (dev->locked) { 
        if (dev->is_compaq)
            ret = fdc37c67x_read(port & 0x0001, priv);
    } else if (port & 0x0001)  switch (dev->gpio_reg) {
        case 0x03:
            ret = dev->ld_regs[0x08][0xf4];
            break;
        case 0x0c ... 0x0f:
            ret = dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08];
            break;
    } else
        ret = dev->gpio_reg;

    return ret;
}

static void
fdc37c67x_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;

    if (dev->locked) { 
        if (dev->is_compaq)
            fdc37c67x_write(port & 0x0001, val, priv);
    } else if (port & 0x0001)  switch (dev->gpio_reg) {
        case 0x03:
            dev->ld_regs[0x08][0xf4] = val & 0xef;
            break;
        case 0x0c: case 0x0e:
            dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0x9e;
            break;
        case 0x0d:
            dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0xd7;
            break;
        case 0x0f:
            dev->ld_regs[0x08][0xb0 + dev->gpio_reg - 0x08] = val & 0x17;
            break;
    } else
        dev->gpio_reg = val;
}

static void
fdc37c67x_superio_handler(fdc37c67x_t *dev)
{
    if (!dev->is_compaq) {
        if (dev->superio_base != 0x0000)
            io_removehandler(dev->superio_base, 0x0002,
                             fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);
        dev->superio_base = make_port_superio(dev);
        if (dev->superio_base != 0x0000)
            io_sethandler(dev->superio_base, 0x0002,
                          fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);
    }
}

static void
fdc37c67x_fdc_handler(fdc37c67x_t *dev)
{
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << 0));
    const uint8_t  local_enable  = !!dev->ld_regs[0][0x30];
    const uint16_t old_base      = dev->fdc_base;

    dev->fdc_base = 0x0000;

    if (global_enable && local_enable)
        dev->fdc_base = make_port(dev, 0) & 0xfff8;

    if (dev->fdc_base != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0ff8))
            fdc_remove(dev->fdc);

        if ((dev->fdc_base >= 0x0100) && (dev->fdc_base <= 0x0ff8))
            fdc_set_base(dev->fdc, dev->fdc_base);
    }
}

static void
fdc37c67x_lpt_handler(fdc37c67x_t *dev)
{
    uint16_t ld_port         = 0x0000;
    uint16_t mask            = 0xfffc;
    uint8_t  global_enable   = !!(dev->regs[0x22] & (1 << 3));
    uint8_t  local_enable    = !!dev->ld_regs[3][0x30];
    uint8_t  lpt_irq         = dev->ld_regs[3][0x70];
    uint8_t  lpt_dma         = dev->ld_regs[3][0x74];
    uint8_t  lpt_mode        = dev->ld_regs[3][0xf0] & 0x07;
    uint8_t  irq_readout[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x08,
                                 0x00, 0x10, 0x18, 0x20, 0x00, 0x00, 0x28, 0x30 };

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    if (lpt_dma >= 4)
        lpt_dma = 0xff;

    lpt_port_remove(dev->lpt);
    lpt_set_fifo_threshold(dev->lpt, (dev->ld_regs[3][0xf0] & 0x78) >> 3);
    switch (lpt_mode) {
        default:
        case 0x04:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x00:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 1);
            break;
        case 0x01: case 0x05:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x02:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x03: case 0x07:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
    }
    if (global_enable && local_enable) {
        ld_port = (make_port(dev, 3) & 0xfffc) & mask;
        if ((ld_port >= 0x0100) && (ld_port <= (0x0ffc & mask)))
            lpt_port_setup(dev->lpt, ld_port);
    }
    lpt_port_irq(dev->lpt, lpt_irq);
    lpt_port_dma(dev->lpt, lpt_dma);

    lpt_set_cnfgb_readout(dev->lpt, ((lpt_irq > 15) ? 0x00 : irq_readout[lpt_irq]) |
                                    ((lpt_dma >= 4) ? 0x00 : lpt_dma));
}

static void
fdc37c67x_serial_handler(fdc37c67x_t *dev, const int uart)
{
    const uint8_t  uart_no       = 4 + uart;
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << uart_no));
    const uint8_t  local_enable  = !!dev->ld_regs[uart_no][0x30];
    const uint16_t old_base      = dev->uart_base[uart];
    double         clock_src     = 24000000.0 / 13.0;

    dev->uart_base[uart] = 0x0000;

    if (global_enable && local_enable)
        dev->uart_base[uart] = make_port(dev, uart_no) & 0xfff8;

    if (dev->uart_base[uart] != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0ff8))
            serial_remove(dev->uart[uart]);

        if ((dev->uart_base[uart] >= 0x0100) && (dev->uart_base[uart] <= 0x0ff8))
            serial_setup(dev->uart[uart], dev->uart_base[uart], dev->ld_regs[uart_no][0x70]);
    }

    switch (dev->ld_regs[uart_no][0xf0] & 0x03) {
        case 0x00:
            clock_src = 24000000.0 / 13.0;
            break;
        case 0x01:
            clock_src = 24000000.0 / 12.0;
            break;
        case 0x02:
            clock_src = 24000000.0 / 1.0;
            break;
        case 0x03:
            clock_src = 24000000.0 / 1.625;
            break;

        default:
            break;
    }

    serial_set_clock_src(dev->uart[uart], clock_src);

    /*
       TODO: If UART 2's own IRQ pin is also enabled when shared,
             it should also be asserted.
     */
    if (dev->ld_regs[4][0xf0] & 0x80) {
        serial_irq(dev->uart[0], dev->ld_regs[4][0x70]);
        serial_irq(dev->uart[1], dev->ld_regs[4][0x70]);
    } else
        serial_irq(dev->uart[uart], dev->ld_regs[uart_no][0x70]);
}

static void
fdc37c67x_kbc_handler(fdc37c67x_t *dev)
{
    const uint8_t  local_enable = !!dev->ld_regs[7][0x30];
    const uint16_t old_base = dev->kbc_base;

    dev->kbc_base = local_enable ? 0x0060 : 0x0000;

    if (dev->kbc_base != old_base)
        kbc_at_handler(local_enable, dev->kbc_base, dev->kbc);

    kbc_at_set_irq(0, dev->ld_regs[7][0x70], dev->kbc);
    kbc_at_set_irq(1, dev->ld_regs[7][0x72], dev->kbc);
}

static void
fdc37c67x_gpio_handler(fdc37c67x_t *dev)
{
    const uint8_t local_enable = !!(dev->regs[0x03] & 0x80) ||
                                   (dev->is_compaq && dev->locked);
    const uint16_t old_base    = dev->gpio_base;

    dev->gpio_base = 0x0000;

    if (local_enable)  switch (dev->regs[0x03] & 0x03) {
        default:
            break;
        case 0:
            dev->gpio_base = 0x00e0;
            break;
        case 1:
            dev->gpio_base = 0x00e2;
            break;
        case 2:
            dev->gpio_base = 0x00e4;
            break;
        case 3:
            dev->gpio_base = 0x00ea; /* Default */
            break;
    }

    if (dev->gpio_base != old_base) {
        if (old_base != 0x0000)
            io_removehandler(old_base, 0x0002,
                         fdc37c67x_gpio_read, NULL, NULL, fdc37c67x_gpio_write, NULL, NULL, dev);

        if (dev->gpio_base > 0x0000)
            io_sethandler(dev->gpio_base, 0x0002,
                          fdc37c67x_gpio_read, NULL, NULL, fdc37c67x_gpio_write, NULL, NULL, dev);
    }
}

static void
fdc37c67x_state_change(fdc37c67x_t *dev, const uint8_t locked)
{
    dev->locked = locked;
    fdc_3f1_enable(dev->fdc, !locked);
}

static void
fdc37c67x_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c67x_t *dev    = (fdc37c67x_t *) priv;
    uint8_t      index  = !(port & 1);
    uint8_t      valxor;

    if (port == 0x00fb) {
        fdc37c67x_state_change(dev, 1);
        dev->tries = 0;
    } else if (port == 0x00f9)
        fdc37c67x_state_change(dev, 0);
    else if (index) {
        if ((!dev->is_compaq) && (val == 0x55) && !dev->locked) {
            fdc37c67x_state_change(dev, 1);
            dev->tries = 0;
        } else if (dev->locked) {
            if ((!dev->is_compaq) && (val == 0xaa))
                fdc37c67x_state_change(dev, 0);
            else
                dev->cur_reg = val;
        } else if ((!dev->is_compaq) && dev->tries)
            dev->tries = 0;
    } else if (dev->locked) {
        if (dev->cur_reg < 0x30) {
            valxor = val ^ dev->regs[dev->cur_reg];

            switch (dev->cur_reg) {
                case 0x02:
                    dev->regs[dev->cur_reg] = val;
                    if (val == 0x02)
                        fdc37c67x_state_change(dev, 0);
                    break;
                case 0x03:
                    dev->regs[dev->cur_reg] = val & 0x83;
                    fdc37c67x_gpio_handler(dev);
                    break;
                case 0x07: case 0x26:
                case 0x2b ... 0x2f:
                    dev->regs[dev->cur_reg] = val;
                    break;
                case 0x22:
                    dev->regs[dev->cur_reg] = val & 0x39;

                    if (valxor & 0x01)
                        fdc37c67x_fdc_handler(dev);
                    if (valxor & 0x08)
                        fdc37c67x_lpt_handler(dev);
                    if (valxor & 0x10)
                        fdc37c67x_serial_handler(dev, 0);
                    if (valxor & 0x20)
                        fdc37c67x_serial_handler(dev, 1);
                    break;
                case 0x23:
                    dev->regs[dev->cur_reg] = val & 0x39;
                    break;
                case 0x24:
                    dev->regs[dev->cur_reg] = val & 0x4e;
                    break;
                case 0x27:
                    dev->regs[dev->cur_reg] = val;
                    fdc37c67x_superio_handler(dev);
                    break;
                default:
                    break;
            }
        } else {
            valxor = val ^ dev->ld_regs[dev->regs[7]][dev->cur_reg];

            if (dev->regs[7] <= dev->max_ld)  switch (dev->regs[7]) {
                case 0x00:    /* FDD */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x01;
                            if (valxor)
                                fdc37c67x_fdc_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xef;

                            if (valxor & 0x01)
                                fdc_update_enh_mode(dev->fdc, val & 0x01);
                            if (valxor & 0x0c) {
                                fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
                                switch (val & 0x0c) {
                                    case 0x00:
                                        fdc_set_flags(dev->fdc, FDC_FLAG_PS2);
                                        break;
                                    case 0x04:
                                        fdc_set_flags(dev->fdc, FDC_FLAG_PS2_MCA);
                                        break;
                                }
                            }
                            if (valxor & 0x10)
                                fdc_set_swap(dev->fdc, (val & 0x10) >> 4);
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xfc;

                            if (valxor & 0x0c)
                                fdc_update_densel_force(dev->fdc, (val & 0xc) >> 2);
                            break;
                        case 0xf2:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor & 0xc0)
                                fdc_update_rwc(dev->fdc, 3, (val & 0xc0) >> 6);
                            if (valxor & 0x30)
                                fdc_update_rwc(dev->fdc, 2, (val & 0x30) >> 4);
                            if (valxor & 0x0c)
                                fdc_update_rwc(dev->fdc, 1, (val & 0x0c) >> 2);
                            if (valxor & 0x03)
                                fdc_update_rwc(dev->fdc, 0, (val & 0x03));
                            break;
                        case 0xf4 ... 0xf7:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x5b;

                            if (valxor & 0x18)
                                fdc_update_drvrate(dev->fdc, dev->cur_reg - 0xf4,
                                                   (val & 0x18) >> 3);
                            break;
                    }
                    break;
                case 0x03:    /* Parallel Port */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x08;
                            if (valxor)
                                fdc37c67x_lpt_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            if (valxor & 0x7f)
                                fdc37c67x_lpt_handler(dev);
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;
                            break;
                    }
                    break;
                case 0x04:    /* Serial port 1 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x10;
                            if (valxor)
                                fdc37c67x_serial_handler(dev, 0);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x83;

                            if (valxor & 0x83) {
                                fdc37c67x_serial_handler(dev, 0);
                                fdc37c67x_serial_handler(dev, 1);
                            }
                            break;
                    }
                    break;
                case 0x05:    /* Serial port 2 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x20;
                            if (valxor)
                                fdc37c67x_serial_handler(dev, 1);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;

                            if (valxor & 0x03) {
                                fdc37c67x_serial_handler(dev, 0);
                                fdc37c67x_serial_handler(dev, 1);
                            }
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x7f;
                            break;
                        case 0xf2:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                    }
                    break;
                case 0x07:    /* Keyboard */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x70: case 0x72:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                fdc37c67x_kbc_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x84;
                            break;
                    }
                    break;
                case 0x08:    /* Aux. I/O */
                    switch (dev->cur_reg) {
                        case 0x30:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xb4: case 0xb6:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x9e;
                            break;
                        case 0xb5:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xd7;
                            break;
                        case 0xb7:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x17;
                            break;
                        case 0xb8:
                        case 0xf1 ... 0xf3:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xc0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = (dev->ld_regs[dev->regs[7]][dev->cur_reg] & 0xe4) | (val & 0x1b);
                            break;
                        case 0xc1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            for (int i = 0; i < 4; i++)
                                fdc_set_fdd_changed(i, !!(val & (1 << i)));
                            break;
                        case 0xf4:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xef;
                            break;
                    }
                    break;
            }
        }
    }
}

static uint8_t
fdc37c67x_read(uint16_t port, void *priv)
{
    fdc37c67x_t *dev   = (fdc37c67x_t *) priv;
    uint8_t      index = (port & 1) ? 0 : 1;
    uint8_t      ret   = 0xff;

    /* Compaq Presario 4500: Unlock at FB, Register at EA, Data at EB, Lock at F9. */
    if ((port == 0xea) || (port == 0xf9) || (port == 0xfb))
        index = 1;
    else if (port == 0xeb)
        index = 0;

    if (dev->locked) {
        if (index)
            ret = dev->cur_reg;
        else {
            if (dev->cur_reg < 0x30) {
                if (dev->cur_reg == 0x20)
                    ret = 0x47;
                else
                    ret = dev->regs[dev->cur_reg];
            } else if (dev->regs[7] <= dev->max_ld) {
                if ((dev->regs[7] == 0x00) && (dev->cur_reg == 0xf2))
                    ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2) |
                          (fdc_get_rwc(dev->fdc, 2) << 4) | (fdc_get_rwc(dev->fdc, 3) << 6));
                else if ((dev->regs[7] == 0x08) && (dev->cur_reg == 0xc1)) {
                    ret = dev->ld_regs[dev->regs[7]][dev->cur_reg] & 0xf0;
                    for (int i = 0; i < 4; i++)
                        ret |= (fdc_get_fdd_changed(i) << i);
                } else if ((dev->regs[7] == 0x08) && (dev->cur_reg == 0xc2))
                    ret = fdc_get_shadow(dev->fdc);
                else if ((dev->regs[7] == 0x08) && (dev->cur_reg == 0xc3))
                    ret = serial_get_shadow(dev->uart[0]);
                else if ((dev->regs[7] == 0x08) && (dev->cur_reg == 0xc4))
                    ret = serial_get_shadow(dev->uart[1]);
                else if ((dev->regs[7] != 0x06) || (dev->cur_reg != 0xf3))
                    ret = dev->ld_regs[dev->regs[7]][dev->cur_reg];
            }
        }
    }

    return ret;
}

static void
fdc37c67x_reset(void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x03] = 0x03;
    dev->regs[0x20] = 0x40;
    dev->regs[0x21] = 0x01;
    dev->regs[0x22] = 0x39;
    dev->regs[0x24] = 0x04;
    dev->regs[0x26] = dev->port_370 ? 0x70 : 0xf0;
    dev->regs[0x27] = 0x03;

    for (uint8_t i = 0; i <= 0x0a; i++)
        memset(dev->ld_regs[i], 0x00, 256);

    /* Logical device 0: FDD */
    dev->ld_regs[0x00][0x30] = 0x00;
    dev->ld_regs[0x00][0x60] = 0x03;
    dev->ld_regs[0x00][0x61] = 0xf0;
    dev->ld_regs[0x00][0x70] = 0x06;
    dev->ld_regs[0x00][0x74] = 0x02;
    dev->ld_regs[0x00][0xf0] = 0x0e;
    dev->ld_regs[0x00][0xf2] = 0xff;

    /* Logical device 3: Parallel Port */
    dev->ld_regs[0x03][0x30] = 0x00;
    dev->ld_regs[0x03][0x60] = 0x03;
    dev->ld_regs[0x03][0x61] = 0x78;
    dev->ld_regs[0x03][0x70] = 0x07;
    dev->ld_regs[0x03][0x74] = 0x04;
    dev->ld_regs[0x03][0xf0] = 0x3c;

    /* Logical device 4: Serial Port 1 */
    dev->ld_regs[0x04][0x30] = 0x00;
    dev->ld_regs[0x04][0x60] = 0x03;
    dev->ld_regs[0x04][0x61] = 0xf8;
    dev->ld_regs[0x04][0x70] = 0x04;
    serial_irq(dev->uart[0], dev->ld_regs[4][0x70]);

    /* Logical device 5: Serial Port 2 */
    dev->ld_regs[0x05][0x30] = 0x00;
    dev->ld_regs[0x05][0x60] = 0x02;
    dev->ld_regs[0x05][0x61] = 0xf8;
    dev->ld_regs[0x05][0x70] = 0x03;
    dev->ld_regs[0x05][0x74] = 0x04;
    dev->ld_regs[0x05][0xf1] = 0x02;
    dev->ld_regs[0x05][0xf2] = 0x03;
    serial_irq(dev->uart[1], dev->ld_regs[5][0x70]);

    /* Logical device 7: Keyboard */
    dev->ld_regs[0x07][0x30] = 0x00;
    dev->ld_regs[0x07][0x61] = 0x60;
    dev->ld_regs[0x07][0x70] = 0x01;

    /* Logical device 8: Auxiliary I/O */
    dev->ld_regs[0x08][0x30] = 0x00;
    dev->ld_regs[0x08][0x60] = 0x00;
    dev->ld_regs[0x08][0x61] = 0x00;
    dev->ld_regs[0x08][0xc0] = 0x06;
    dev->ld_regs[0x08][0xc1] = 0x03;

    fdc37c67x_gpio_handler(dev);
    fdc37c67x_lpt_handler(dev);
    fdc37c67x_serial_handler(dev, 0);
    fdc37c67x_serial_handler(dev, 1);

    fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
    fdc_reset(dev->fdc);

    fdc37c67x_fdc_handler(dev);

    for (int i = 0; i < 4; i++)
        fdc_set_fdd_changed(i, 1);

    fdc37c67x_kbc_handler(dev);

    fdc37c67x_superio_handler(dev);

    dev->locked = 0;
}

static void
fdc37c67x_close(void *priv)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) priv;

    free(dev);
}

static void *
fdc37c67x_init(const device_t *info)
{
    fdc37c67x_t *dev = (fdc37c67x_t *) calloc(1, sizeof(fdc37c67x_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0]   = device_add_inst(&ns16550_device, 1);
    dev->uart[1]   = device_add_inst(&ns16550_device, 2);

    dev->lpt       = device_add_inst(&lpt_port_device, 1);

    dev->kbc_type  = info->local & FDC37XXXX_KBC;

    dev->is_compaq = (dev->kbc_type == FDC37XXX1);

    dev->port_370  = !!(info->local & FDC37XXXX_370);

    dev->max_ld    = 8;

    if (dev->is_compaq) {
        io_sethandler(0x0f9, 0x0001,
                      fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);
        io_sethandler(0x0fb, 0x0001,
                      fdc37c67x_read, NULL, NULL, fdc37c67x_write, NULL, NULL, dev);
    }

    switch (dev->kbc_type) {
        case FDC37XXX1:
            dev->kbc = device_add_params(&kbc_at_device, (void *) KBC_VEN_COMPAQ);
            break;
        case FDC37XXX2:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_AMI | 0x00003500));
            break;
        case FDC37XXX3:
        default:
            dev->kbc = device_add(&kbc_at_device);
            break;
        case FDC37XXX5:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00013800));
            break;
        case FDC37XXX7:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00041600));
            break;
    }

    /* Set the defaults here so the ports can be removed by fdc37c67x_reset(). */
    dev->fdc_base     = 0x03f0;
    dev->lpt_base     = 0x0378;
    dev->uart_base[0] = 0x03f8;
    dev->uart_base[1] = 0x02f8;
    dev->kbc_base     = 0x0060;

    fdc37c67x_reset(dev);

    return dev;
}

const device_t fdc37c67x_device = {
    .name          = "SMC FDC37C67x Super I/O",
    .internal_name = "fdc37c67x",
    .flags         = 0,
    .local         = 0,
    .init          = fdc37c67x_init,
    .close         = fdc37c67x_close,
    .reset         = fdc37c67x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
