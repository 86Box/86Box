/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the Winbond W83977 Super I/O Chips.
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
#include <86box/nvr.h>
#include <86box/apm.h>
#include <86box/plat.h>
#include <86box/plat_unused.h>
#include <86box/video.h>
#include <86box/sio.h>
#include "cpu.h"

typedef struct w83977_gpio_t {
    uint8_t       id;
    uint8_t       reg;
    uint8_t       pulldn;
    uint8_t       pad;

    uint8_t       alt[4];

    uint16_t      base;

    void *        parent;
} w83977_gpio_t;

typedef struct w83977_t {
    uint8_t       id;
    uint8_t       hefras;
    uint8_t       has_nvr;
    uint8_t       tries;
    uint8_t       lockreg;
    uint8_t       gpio_reg;
    uint8_t       regs[48];
    uint8_t       ld_regs[11][256];
    uint16_t      kbc_type;
    uint16_t      superio_base;
    uint16_t      fdc_base;
    uint16_t      lpt_base;
    uint16_t      nvr_base;
    uint16_t      kbc_base[2];
    uint16_t      gpio_base; /* Set to EA */
    uint16_t      uart_base[2];
    int           locked;
    int           cur_reg;
    uint32_t      type;
    w83977_gpio_t gpio[3];
    fdc_t        *fdc;
    nvr_t        *nvr;
    void         *kbc;
    serial_t     *uart[2];
    lpt_t        *lpt;
} w83977_t;

static int next_id = 0;

static void    w83977_write(uint16_t port, uint8_t val, void *priv);
static uint8_t w83977_read(uint16_t port, void *priv);

static uint16_t
make_port(const w83977_t *dev, const uint8_t ld)
{
    const uint16_t r0 = dev->ld_regs[ld][0x60];
    const uint16_t r1 = dev->ld_regs[ld][0x61];

    const uint16_t p = (r0 << 8) + r1;

    return p;
}

static uint16_t
make_port_sec(const w83977_t *dev, const uint8_t ld)
{
    const uint16_t r0 = dev->ld_regs[ld][0x62];
    const uint16_t r1 = dev->ld_regs[ld][0x63];

    const uint16_t p = (r0 << 8) + r1;

    return p;
}

static __inline uint8_t
w83977_do_read_gp(w83977_gpio_t *dev, int reg, int bit)
{
    return dev->reg & dev->pulldn & (1 << bit);
}

static __inline uint8_t
w83977_do_read_alt(const w83977_gpio_t *dev, int alt, int reg, int bit)
{
    return dev->alt[alt] & (1 << bit);
}

static uint8_t
w83977_read_gp(const w83977_gpio_t *dev, int bit)
{
    uint8_t   reg         = dev->id;
    w83977_t *sio         = (w83977_t *) dev->parent;
    uint8_t   gp_func_reg = sio->ld_regs[0x07 + reg - 1][0xe0 + ((((reg - 1) << 3) + bit) & 0x0f)];
    uint8_t   gp_func;
    uint8_t   ret         = 1 << bit;

    if (gp_func_reg & 0x01)  switch (reg) {
        default:
            /* Do nothing, this GP does not exist. */
            break;
        case 1:
            switch (bit) {
               default:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        ret = w83977_do_read_gp((w83977_gpio_t *) dev, reg - 1, bit);
                    else
                        ret = w83977_do_read_alt(dev, gp_func - 1, reg - 1, bit);
                    break;
                case 0: case 1:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        ret = w83977_do_read_gp((w83977_gpio_t *) dev, reg - 1, bit);
                    else
                        ret = w83977_do_read_alt(dev, 0, reg - 1, bit);
                    break;
               case 4:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        ret = w83977_do_read_gp((w83977_gpio_t *) dev, reg - 1, bit);
                    else if (gp_func == 0x02)
                        ret = kbc_at_read_p(sio->kbc, 1, 0x80) ? (1 << bit) : 0x00;
                    else
                        ret = w83977_do_read_alt(dev, gp_func - 1, reg - 1, bit);
                    break;
            }
            break;
        case 2:
            switch (bit) {
                default:
                    break;
               case 0:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        ret = w83977_do_read_gp((w83977_gpio_t *) dev, reg - 1, bit);
                    else if (gp_func == 0x02)
                        ret = kbc_at_read_p(sio->kbc, 2, 0x01) ? (1 << bit) : 0x00;
                    else
                        ret = w83977_do_read_alt(dev, gp_func - 1, reg - 1, bit);
                    break;
               case 1 ... 4:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        ret = w83977_do_read_gp((w83977_gpio_t *) dev, reg - 1, bit);
                    else if (gp_func == 0x02)
                        ret = kbc_at_read_p(sio->kbc, 1, 1 << (bit + 2)) ? (1 << bit) : 0x00;
                    else
                        ret = w83977_do_read_alt(dev, gp_func - 1, reg - 1, bit);
                    break;
                case 5:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        ret = w83977_do_read_gp((w83977_gpio_t *) dev, reg, bit);
                    else
                        ret = kbc_at_read_p(sio->kbc, 2, 0x02) ? (1 << bit) : 0x00;
                    break;
                case 6: case 7:
                    /* Do nothing, these bits do not exist. */
                    break;
            }
            break;
        case 3:
            if (sio->type == W83977TF)  switch (bit) {
                default:
                    break;
               case 0 ... 4:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        ret = w83977_do_read_gp((w83977_gpio_t *) dev, reg - 1, bit);
                    else
                        ret = w83977_do_read_alt(dev, gp_func - 1, reg - 1, bit);
                    break;
                case 5 ... 7:
                    /* Do nothing, these bits have no function. */
                    break;
            }
            break;
    }

    if (gp_func_reg & 0x02)
        ret ^= (1 << bit);

    return ret;
}

static __inline void
w83977_do_write_gp(w83977_gpio_t *dev, int reg, int bit, int set)
{
    dev->reg = (dev->reg & ~(1 << bit)) | (set << bit);
}

static __inline void
w83977_do_write_alt(w83977_gpio_t *dev, int alt, int reg, int bit, int set)
{
    dev->alt[alt] = (dev->alt[alt] & ~(1 << bit)) | (set << bit);
}

static void
w83977_write_gp(w83977_gpio_t *dev, int bit, int set)
{
    uint8_t   reg         = dev->id;
    w83977_t *sio         = (w83977_t *) dev->parent;
    uint8_t   gp_func_reg = sio->ld_regs[0x07 + reg - 1][0xe0 + ((((reg - 1) << 3) + bit) & 0x0f)];
    uint8_t   gp_func;

    if (gp_func_reg & 0x02)
        set = !set;

    if (!(gp_func_reg & 0x01))  switch (reg) {
        default:
            /* Do nothing, this GP does not exist. */
            break;
        case 1:
            switch (bit) {
               default:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        w83977_do_write_gp(dev, reg - 1, bit, set);
                    else
                        w83977_do_write_alt(dev, gp_func - 1, reg - 1, bit, set);
                    break;
                case 0: case 1:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        w83977_do_write_gp(dev, reg - 1, bit, set);
                    else
                        w83977_do_write_alt(dev, 0, reg - 1, bit, set);
                    break;
               case 4:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        w83977_do_write_gp(dev, reg - 1, bit, set);
                    else if (gp_func == 0x02)
                        kbc_at_write_p(sio->kbc, 1, 0x7f, set << 7);
                    else
                        w83977_do_write_alt(dev, gp_func - 1, reg - 1, bit, set);
                    break;
            }
            break;
        case 2:
            switch (bit) {
                default:
                    break;
               case 0:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        w83977_do_write_gp(dev, reg - 1, bit, set);
                    else if (gp_func == 0x02)
                        kbc_at_write_p(sio->kbc, 2, 0xfe, set);
                    else
                        w83977_do_write_alt(dev, gp_func - 1, reg - 1, bit, set);
                    break;
               case 1 ... 4:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        w83977_do_write_gp(dev, reg - 1, bit, set);
                    else if (gp_func == 0x02)
                        kbc_at_write_p(sio->kbc, 1, ~(1 << (bit + 2)), set << (bit + 2));
                    else
                        w83977_do_write_alt(dev, gp_func - 1, reg - 1, bit, set);
                    break;
                case 5:
                    gp_func = (gp_func_reg >> 3) & 0x01;
                    if (gp_func == 0x00)
                        w83977_do_write_gp(dev, reg - 1, bit, set);
                    else
                        kbc_at_write_p(sio->kbc, 2, 0xfd, set << 1);
                    break;
                case 6: case 7:
                    /* Do nothing, these bits do not exist. */
                    break;
            }
            break;
        case 3:
            if (sio->type == W83977TF)  switch (bit) {
                default:
                    break;
               case 0 ... 4:
                    gp_func = (gp_func_reg >> 3) & 0x03;
                    if (gp_func == 0x00)
                        w83977_do_write_gp(dev, reg - 1, bit, set);
                    else
                        w83977_do_write_alt(dev, gp_func - 1, reg - 1, bit, set);
                    break;
                case 5 ... 7:
                    /* Do nothing, these bits have no function. */
                    break;
            }
            break;
    }
}

static uint8_t
w83977_gpio_read(uint16_t port, void *priv)
{
    const w83977_gpio_t *dev = (w83977_gpio_t *) priv;
    uint8_t              ret = 0x00;

    for (uint8_t i = 0; i < 8; i++)
        ret |= w83977_read_gp(dev, i);

    return ret;
}

static void
w83977_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    w83977_gpio_t *dev = (w83977_gpio_t *) priv;

    for (uint8_t i = 0; i < 8; i++)
        w83977_write_gp(dev, i, val & (1 << i));
}

static void
w83977_superio_handler(w83977_t *dev)
{
    if (dev->superio_base != 0x0000)
        io_removehandler(dev->superio_base, 0x0002,
                         w83977_read, NULL, NULL, w83977_write, NULL, NULL, dev);

    dev->superio_base = (dev->regs[0x26] & 0x40) ? 0x0370 : 0x03f0;

    io_sethandler(dev->superio_base, 0x0002,
                  w83977_read, NULL, NULL, w83977_write, NULL, NULL, dev);
}

static void
w83977_fdc_handler(w83977_t *dev)
{
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << 0));
    const uint8_t  local_enable  = !!dev->ld_regs[0][0x30];
    const uint16_t old_base      = dev->fdc_base;

    dev->fdc_base = 0x0000;

    if (global_enable && local_enable)
        dev->fdc_base = make_port(dev, 0) & 0xfff8;

    if (dev->fdc_base != old_base) {
        if ((dev->id != 1) && (old_base >= 0x0100) && (old_base <= 0x0ff8))
            fdc_remove(dev->fdc);

        if ((dev->id != 1) && (dev->fdc_base >= 0x0100) && (dev->fdc_base <= 0x0ff8))
            fdc_set_base(dev->fdc, dev->fdc_base);
    }
}

static void
w83977_lpt_handler(w83977_t *dev)
{
    uint16_t ld_port         = 0x0000;
    uint16_t mask            = 0xfffc;
    uint8_t  global_enable   = !!(dev->regs[0x22] & (1 << 3));
    uint8_t  local_enable    = !!dev->ld_regs[1][0x30];
    uint8_t  lpt_irq         = dev->ld_regs[1][0x70];
    uint8_t  lpt_dma         = dev->ld_regs[1][0x74];
    uint8_t  lpt_mode        = dev->ld_regs[1][0xf0] & 0x07;
    uint8_t  irq_readout[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x08,
                                 0x00, 0x10, 0x18, 0x20, 0x00, 0x00, 0x28, 0x30 };

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    if (lpt_dma >= 4)
        lpt_dma = 0xff;

    lpt_port_remove(dev->lpt);
    lpt_set_fifo_threshold(dev->lpt, (dev->ld_regs[1][0xf0] & 0x78) >> 3);
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
        ld_port = (make_port(dev, 1) & 0xfffc) & mask;
        if ((ld_port >= 0x0100) && (ld_port <= (0x0ffc & mask)))
            lpt_port_setup(dev->lpt, ld_port);
    }
    lpt_port_irq(dev->lpt, lpt_irq);
    lpt_port_dma(dev->lpt, lpt_dma);

    lpt_set_cnfgb_readout(dev->lpt, ((lpt_irq > 15) ? 0x00 : irq_readout[lpt_irq]) | 0x07);
}

static void
w83977_serial_handler(w83977_t *dev, const int uart)
{
    const uint8_t  uart_no       = 2 + uart;
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

    serial_irq(dev->uart[uart], dev->ld_regs[uart_no][0x70]);
}

static void
w83977_nvr_handler(w83977_t *dev)
{
    uint8_t        local_enable = !!dev->ld_regs[6][0x30];
    const uint16_t old_base     = dev->nvr_base;

    local_enable &= (((dev->ld_regs[6][0xf0] & 0xe0) == 0x80) ||
                     ((dev->ld_regs[6][0xf0] & 0xe0) == 0xe0));

    dev->nvr_base = 0x0000;

    if (local_enable)
        dev->nvr_base = make_port(dev, 6) & 0xfffe;

    if (dev->nvr_base != old_base) {
        if ((dev->id != 1) && dev->has_nvr && (old_base > 0x0000) && (old_base <= 0x0ffe))
            nvr_at_handler(0, dev->nvr_base, dev->nvr);

        if ((dev->id != 1) && dev->has_nvr && (dev->nvr_base > 0x0000) && (dev->nvr_base <= 0x0ffe))
            nvr_at_handler(1, dev->nvr_base, dev->nvr);
    }
}

static void
w83977_kbc_handler(w83977_t *dev)
{
    const uint8_t  local_enable = !!dev->ld_regs[5][0x30];
    const uint16_t old_base = dev->kbc_base[0];
    const uint16_t old_base2 = dev->kbc_base[1];

    dev->kbc_base[0] = dev->kbc_base[1] = 0x0000;

    if (local_enable) {
        dev->kbc_base[0] = make_port(dev, 5);
        dev->kbc_base[1] = make_port_sec(dev, 5);
    }

    if (dev->kbc_base[0] != old_base) {
        if ((dev->id != 1) && (dev->kbc != NULL) && (old_base >= 0x0100) && (old_base <= 0x0ff8))
            kbc_at_port_handler(0, 0, old_base, dev->kbc);

        if ((dev->id != 1) && (dev->kbc != NULL) && (dev->kbc_base[0] >= 0x0100) && (dev->kbc_base[0] <= 0x0ff8))
            kbc_at_port_handler(0, 1, dev->kbc_base[0], dev->kbc);
    }

    if (dev->kbc_base[1] != old_base2) {
        if ((dev->id != 1) && (dev->kbc != NULL) && (old_base2 >= 0x0100) && (old_base2 <= 0x0ff8))
            kbc_at_port_handler(1, 0, old_base2, dev->kbc);

        if ((dev->id != 1) && (dev->kbc != NULL) && (dev->kbc_base[1] >= 0x0100) && (dev->kbc_base[1] <= 0x0ff8))
            kbc_at_port_handler(1, 1, dev->kbc_base[1], dev->kbc);
    }

    if ((dev->id != 1) && (dev->kbc != NULL)) {
        kbc_at_set_irq(0, dev->ld_regs[5][0x70], dev->kbc);
        kbc_at_set_irq(1, dev->ld_regs[5][0x72], dev->kbc);
    }
}

static void
w83977_gpio_handler(w83977_t *dev, const int gpio)
{
    const uint8_t  gpio_no      = 7 + gpio;
    const uint8_t  local_enable = !!dev->ld_regs[gpio_no][0x30];
    const uint16_t old_base     = dev->gpio[gpio].base;

    dev->gpio[gpio].base = 0x0000;

    if (local_enable)
        dev->gpio[gpio].base = make_port(dev, gpio_no) & 0xfff8;

    if (dev->gpio[gpio].base != old_base) {
        if ((old_base >= 0x0100) && (old_base <= 0x0ff8))
            io_removehandler(old_base, 0x0002,
                             w83977_gpio_read, NULL, NULL, w83977_gpio_write, NULL, NULL, dev);

        if ((dev->gpio[gpio].base >= 0x0100) && (dev->gpio[gpio].base <= 0x0ff8))
            io_sethandler(dev->gpio[gpio].base, 0x0002,
                          w83977_gpio_read, NULL, NULL, w83977_gpio_write, NULL, NULL, dev);
    }
}

static void
w83977_state_change(w83977_t *dev, const uint8_t locked)
{
    dev->locked = locked;

    if (dev->id != 1)
        fdc_3f1_enable(dev->fdc, !locked);
}

static void
w83977_write(uint16_t port, uint8_t val, void *priv)
{
    w83977_t *dev    = (w83977_t *) priv;
    uint8_t   index  = !(port & 1);
    uint8_t   valxor;

    if (index) {
        if ((val == 0x87) && !dev->locked) {
            if (dev->tries) {
                w83977_state_change(dev, 1);
                dev->tries = 0;
            } else
                dev->tries++;
        } else if (dev->locked) {
            if (val == 0xaa)
                w83977_state_change(dev, 0);
            else
                dev->cur_reg = val;
        } else if (dev->tries)
            dev->tries = 0;
    } else if (dev->locked && !dev->lockreg) {
        if (dev->cur_reg < 0x30) {
            valxor = val ^ dev->regs[dev->cur_reg];

            switch (dev->cur_reg) {
                case 0x02:
                    dev->regs[dev->cur_reg] = val;
                    if (val == 0x02)
                        w83977_state_change(dev, 0);
                    break;
                case 0x07:
                case 0x2c ... 0x2f:
                    dev->regs[dev->cur_reg] = val;
                    break;
                case 0x22:
                    if (dev->type == W83977F)
                        dev->regs[dev->cur_reg] = val & 0x3d;
                    else
                        dev->regs[dev->cur_reg] = val & 0x39;

                    if (valxor & 0x01)
                        w83977_fdc_handler(dev);
                    if (valxor & 0x08)
                        w83977_lpt_handler(dev);
                    if (valxor & 0x10)
                        w83977_serial_handler(dev, 0);
                    if (valxor & 0x20)
                        w83977_serial_handler(dev, 1);
                    break;
                case 0x23:
                    if (dev->type == W83977F)
                        dev->regs[dev->cur_reg] = val & 0x3f;
                    else
                        dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0xfe) | (val & 0x01);
                    break;
                case 0x24:
                    if (dev->type == W83977F)
                        dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x04) | (val & 0xf3);
                    else
                        dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x04) | (val & 0xc1);
                    break;
                case 0x25:
                    if (dev->type == W83977F)
                        dev->regs[dev->cur_reg] = val & 0x3d;
                    else
                        dev->regs[dev->cur_reg] = val & 0x39;
                    break;
                case 0x26:
                    if (dev->type == W83977F)
                        dev->regs[dev->cur_reg] = val;
                    else
                        dev->regs[dev->cur_reg] = val & 0xef;
                    dev->lockreg = !!(val & 0x20);
                    w83977_superio_handler(dev);
                    break;
                case 0x28:
                    dev->regs[dev->cur_reg] = val & 0x17;
                    break;
                case 0x2a:
                    if (dev->type == W83977TF)
                        dev->regs[dev->cur_reg] = val & 0xf3;
                    else
                        dev->regs[dev->cur_reg] = val;
                    break;
                case 0x2b:
                    if (dev->type == W83977TF)
                        dev->regs[dev->cur_reg] = val & 0xf9;
                    else
                        dev->regs[dev->cur_reg] = val;
                    break;

                default:
                    break;
            }
        } else {
            valxor = val ^ dev->ld_regs[dev->regs[7]][dev->cur_reg];

            if (dev->regs[7] <= 0x0a)  switch (dev->regs[7]) {
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
                                w83977_fdc_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((valxor & 0x01) && (val & 0x01)) {
                                uint8_t reg_f2 = dev->ld_regs[dev->regs[7]][0xf2];

                                fdc_update_rwc(dev->fdc, 3, (reg_f2 & 0xc0) >> 6);
                                fdc_update_rwc(dev->fdc, 2, (reg_f2 & 0x30) >> 4);
                                fdc_update_rwc(dev->fdc, 1, (reg_f2 & 0x0c) >> 2);
                                fdc_update_rwc(dev->fdc, 0, (reg_f2 & 0x03));
                            } else {
                                fdc_update_rwc(dev->fdc, 3, 0x00);
                                fdc_update_rwc(dev->fdc, 2, 0x00);
                                fdc_update_rwc(dev->fdc, 1, 0x00);
                                fdc_update_rwc(dev->fdc, 0, 0x00);
                            }
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
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor & 0x01)
                                fdc_set_swwp(dev->fdc, !!(val & 0x01));
                            if (valxor & 0x02)
                                fdc_set_diswr(dev->fdc, !!(val & 0x02));
                            if (valxor & 0x0c)
                                fdc_update_densel_force(dev->fdc, (val & 0xc) >> 2);
                            break;
                        case 0xf2:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (dev->ld_regs[dev->regs[7]][0xf0] & 0x01) {
                                if (valxor & 0xc0)
                                    fdc_update_rwc(dev->fdc, 3, (val & 0xc0) >> 6);
                                if (valxor & 0x30)
                                    fdc_update_rwc(dev->fdc, 2, (val & 0x30) >> 4);
                                if (valxor & 0x0c)
                                    fdc_update_rwc(dev->fdc, 1, (val & 0x0c) >> 2);
                                if (valxor & 0x03)
                                    fdc_update_rwc(dev->fdc, 0, (val & 0x03));
                            }
                            break;
                        case 0xf4 ... 0xf7:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x5b;

                            if (valxor & 0x18)
                                fdc_update_drvrate(dev->fdc, dev->cur_reg - 0xf4,
                                                   (val & 0x18) >> 3);
                            break;
                    }
                    break;
                case 0x01:    /* Parallel Port */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x08;
                            if (valxor)
                                w83977_lpt_handler(dev);
                            break;
                        /*
                           Bits 2:0: Mode:
                               - 000: Bi-directional (SPP);
                               - 001: EPP-1.9 and SPP;
                               - 010: ECP;
                               - 011: ECP and EPP-1.9;
                               - 100: Printer Mode (Default);
                               - 101: EPP-1.7 and SPP;
                               - 110: ECP and EPP-1.7.
                           Bits 6:3: ECP FIFO Threshold.
                         */
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            if (valxor)
                                w83977_lpt_handler(dev);
                            break;
                    }
                    break;
                case 0x02:    /* Serial port 1 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x10;
                            if (valxor)
                                w83977_serial_handler(dev, 0);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;

                            if (valxor & 0x03)
                                w83977_serial_handler(dev, 0);
                            break;
                    }
                    break;
                case 0x03:    /* Serial port 2 */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x20;
                            if (valxor)
                                w83977_serial_handler(dev, 1);
                            break;
                        case 0xf0:
                            if (dev->type == W83977F)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;

                            if (valxor & 0x03)
                                w83977_serial_handler(dev, 1);
                            break;
                        case 0xf1:
                            if (dev->type != W83977F)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x7f;
                            break;
                    }
                    break;
                case 0x04:    /* Real Time Clock */
                    if (dev->type == W83977F)  switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                w83977_nvr_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->id != 1) && dev->has_nvr && valxor) {
                                nvr_lock_set(0x80, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x01), dev->nvr);
                                nvr_lock_set(0xa0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x02), dev->nvr);
                                nvr_lock_set(0xc0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x04), dev->nvr);
                                nvr_lock_set(0xe0, 0x20, !!(dev->ld_regs[6][dev->cur_reg] & 0x08), dev->nvr);

                                nvr_bank_set(0, val >> 6, dev->nvr);
                            }
                            break;
                    }
                    break;
                case 0x05:    /* KBC */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70: case 0x72:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                w83977_kbc_handler(dev);
                            break;
                        case 0xf0:
                            if (dev->type == W83977F)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xc7;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x83;
                            if (valxor & 0x01)
                                kbc_at_set_fast_reset(val & 0x01);
                            break;
                    }
                    break;
                case 0x06:    /* IR */
                    if (dev->type == W83977F)  switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74: case 0x75:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            break;
                    }
                    break;
                case 0x07:    /* GP I/O Port I */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x64: case 0x65:
                        case 0x70: case 0x72:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                w83977_gpio_handler(dev, 0);
                            break;
                        case 0xe0 ... 0xe7:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1b;
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;
                            break;
                    }
                    break;
                case 0x08:    /* GP I/O Port II */
                    switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70: case 0x72:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                w83977_gpio_handler(dev, 0);
                            break;
                        case 0xe8 ... 0xed:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1f;
                            break;
                        case 0xee:
                            if (dev->type == W83977TF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1f;
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x08;
                            break;
                        case 0xf2:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xf3:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0e;
                            break;
                        case 0xf4:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            break;
                    }
                    break;
                case 0x09:    /* GP I/O Port III */
                    if (dev->type == W83977TF)  switch (dev->cur_reg) {
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x64: case 0x65:
                        case 0x70: case 0x72:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                w83977_gpio_handler(dev, 0);
                            break;
                        case 0xe0 ... 0xe7:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1b;
                            break;
                        case 0xf1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x07;
                            break;
                    }
                    break;
                case 0x0a:    /* ACPI */
                    if (dev->type != W83977F)  switch (dev->cur_reg) {
                        case 0x30:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x64: case 0x65:
                            if (dev->type == W83977TF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xe0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xf7;
                            break;
                        case 0xe1: case 0xe2:
                        case 0xfe: case 0xff:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xe4:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xf0;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xe5:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x7f;
                            break;
                        case 0xe7:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;
                            break;
                        case 0xf0:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xcf;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x8f;
                            break;
                        case 0xf1:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0xcf);
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0x0f);
                            break;
                        case 0xf2:
                            if (dev->type != W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0x0f);
                            break;
                        case 0xf3:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0x7f);
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0x3f);
                            break;
                        case 0xf4:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0x17);
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            break;
                        case 0xf5:
                            if (dev->type != W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            break;
                        case 0xf6:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x7f;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x3f;
                            break;
                        case 0xf7:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x17;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x01;
                            break;
                        case 0xf9:
                            if (dev->type == W83977EF)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x07;
                            break;
                    }
                    break;
            }
        }
    }
} 

static uint8_t
w83977_read(uint16_t port, void *priv)
{
    w83977_t *dev   = (w83977_t *) priv;
    uint8_t      index = (port & 1) ? 0 : 1;
    uint8_t      ret   = 0xff;

    if (dev->locked) {
        if (index)
            ret = dev->cur_reg;
        else {
            if (dev->cur_reg < 0x30) {
                if (dev->cur_reg == 0x20)
                    ret = dev->type >> (W83977_TYPE_SHIFT + 8);
                else if (dev->cur_reg == 0x21)
                    ret = (dev->type >> W83977_TYPE_SHIFT) & 0xff;
                else
                    ret = dev->regs[dev->cur_reg];
            } else if ((dev->regs[7] == 0x0a) && (dev->cur_reg == 0xe3)) {
                ret = dev->ld_regs[dev->regs[7]][dev->cur_reg];
                if (dev->type == W83977EF)
                    dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~0x17;
                else
                    dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~0x07;
            } else if (dev->regs[7] <= 0x0a) {
                if ((dev->regs[7] == 0x00) && (dev->cur_reg == 0xf2))
                    ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2) |
                          (fdc_get_rwc(dev->fdc, 2) << 4) | (fdc_get_rwc(dev->fdc, 3) << 6));
                else
                    ret = dev->ld_regs[dev->regs[7]][dev->cur_reg];
            }
        }
    }

    return ret;
}

static void
w83977_reset(void *priv)
{
    w83977_t *dev = (w83977_t *) priv;

    dev->lockreg = 0;

    memset(dev->regs, 0x00, sizeof(dev->regs));

    dev->regs[0x03] = 0x03;
    dev->regs[0x20] = dev->type >> (W83977_TYPE_SHIFT + 8);
    dev->regs[0x21] = (dev->type >> W83977_TYPE_SHIFT) & 0xff;
    dev->regs[0x22] = 0xff;
    dev->regs[0x23] = (dev->type != W83977F) ? 0xfe : 0x00;
    dev->regs[0x24] = 0x80;
    dev->regs[0x26] = dev->hefras << 6;

    for (uint8_t i = 0; i <= 0x0a; i++)
        memset(dev->ld_regs[i], 0x00, 256);

    /* Logical device 0: FDD */
    dev->ld_regs[0x00][0x30] = 0x01;
    dev->ld_regs[0x00][0x60] = 0x03;
    dev->ld_regs[0x00][0x61] = (dev->id == 1) ? 0x70 : 0xf0;
    dev->ld_regs[0x00][0x70] = 0x06;
    if (dev->type == W83977F)
        dev->ld_regs[0x00][0x71] = 0x02;
    dev->ld_regs[0x00][0x74] = 0x02;
    dev->ld_regs[0x00][0xf0] = 0x0e;
    dev->ld_regs[0x00][0xf2] = 0xff;

    /* Logical device 1: Parallel Port */
    dev->ld_regs[0x01][0x30] = 0x01;
    dev->ld_regs[0x01][0x60] = (dev->id == 1) ? 0x02 : 0x03;
    dev->ld_regs[0x01][0x61] = 0x78;
    dev->ld_regs[0x01][0x70] = (dev->id == 1) ? 0x05 : 0x07;
    if (dev->type == W83977F)
        dev->ld_regs[0x01][0x71] = 0x02;
    dev->ld_regs[0x01][0x74] = 0x04;
    dev->ld_regs[0x01][0xf0] = 0x3f;

    /* Logical device 2: Serial Port 1 */
    dev->ld_regs[0x02][0x30] = 0x01;
    dev->ld_regs[0x02][0x60] = 0x03;
    dev->ld_regs[0x02][0x61] = (dev->id == 1) ? 0xe8 : 0xf8;
    dev->ld_regs[0x02][0x70] = 0x04;
    if (dev->type == W83977F)
        dev->ld_regs[0x02][0x71] = 0x02;
    serial_irq(dev->uart[0], dev->ld_regs[2][0x70]);

    /* Logical device 3: Serial Port 2 */
    dev->ld_regs[0x03][0x30] = 0x01;
    dev->ld_regs[0x03][0x60] = 0x02;
    dev->ld_regs[0x03][0x61] = (dev->id == 1) ? 0xe8 : 0xf8;
    dev->ld_regs[0x03][0x70] = 0x03;
    if (dev->type == W83977F)
        dev->ld_regs[0x03][0x71] = 0x02;
    dev->ld_regs[0x03][0x74] = 0x04;
    serial_irq(dev->uart[1], dev->ld_regs[3][0x70]);

    if (dev->type == W83977F) {
        /* Logical device 4: Real Time Clock */
        dev->ld_regs[0x04][0x30] = 0x01;
        dev->ld_regs[0x04][0x61] = 0x70;
        dev->ld_regs[0x04][0x70] = 0x08;
    }

    /* Logical device 5: KBC */
    dev->ld_regs[0x05][0x30] = 0x01;
    dev->ld_regs[0x05][0x61] = 0x60;
    dev->ld_regs[0x05][0x63] = 0x64;
    dev->ld_regs[0x05][0x70] = 0x01;
    if (dev->type == W83977F)
        dev->ld_regs[0x05][0x71] = 0x02;
    dev->ld_regs[0x05][0x72] = 0x0c;
    if (dev->type == W83977F)
        dev->ld_regs[0x05][0x73] = 0x02;
    if (dev->type == W83977F)
        dev->ld_regs[0x05][0xf0] = 0x40;
    else
        dev->ld_regs[0x05][0xf0] = 0x83;

    if (dev->type == W83977F) {
        /* Logical device 6: IR */
        dev->ld_regs[0x06][0x71] = 0x02;
        dev->ld_regs[0x06][0x74] = 0x04;
    }

    /* Logical device 7: GP I/O Port I */
    if (dev->type == W83977F)
        dev->ld_regs[0x07][0x71] = 0x02;
    dev->ld_regs[0x07][0xe0] = 0x01;
    dev->ld_regs[0x07][0xe1] = 0x01;
    dev->ld_regs[0x07][0xe2] = 0x01;
    dev->ld_regs[0x07][0xe3] = 0x01;
    dev->ld_regs[0x07][0xe4] = 0x01;
    dev->ld_regs[0x07][0xe5] = 0x01;
    dev->ld_regs[0x07][0xe6] = 0x01;
    dev->ld_regs[0x07][0xe7] = 0x01;

    /* Logical device 8: GP I/O Port II */
    if (dev->type == W83977F)
        dev->ld_regs[0x08][0x71] = 0x02;
    dev->ld_regs[0x08][0xe8] = 0x01;
    dev->ld_regs[0x08][0xe9] = 0x01;
    dev->ld_regs[0x08][0xea] = 0x01;
    dev->ld_regs[0x08][0xeb] = 0x01;
    dev->ld_regs[0x08][0xec] = 0x01;
    dev->ld_regs[0x08][0xed] = 0x01;
    if (dev->type == W83977TF)
        dev->ld_regs[0x08][0xee] = 0x01;

    if (dev->type == W83977TF) {
        /* Logical device 9: GP I/O Port III */
        dev->ld_regs[0x09][0xe0] = 0x01;
        dev->ld_regs[0x09][0xe1] = 0x01;
        dev->ld_regs[0x09][0xe2] = 0x01;
        dev->ld_regs[0x09][0xe3] = 0x01;
        dev->ld_regs[0x09][0xe4] = 0x01;
        dev->ld_regs[0x09][0xe5] = 0x01;
        dev->ld_regs[0x09][0xe6] = 0x01;
        dev->ld_regs[0x09][0xe7] = 0x01;
    }

    /* Logical device A: ACPI - not on W83977F */
    if (dev->type == W83977EF)
        dev->ld_regs[0x0a][0xe3] = 0x10;

    w83977_lpt_handler(dev);
    w83977_serial_handler(dev, 0);
    w83977_serial_handler(dev, 1);

    /* W83977EF has ACPI but no ACPI I/O ports. */

    if (dev->id != 1) {
        fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
        fdc_reset(dev->fdc);

        w83977_fdc_handler(dev);

        if ((dev->type == W83977F) && dev->has_nvr) {
            w83977_nvr_handler(dev);
            nvr_bank_set(0, 0, dev->nvr);

            nvr_lock_set(0x80, 0x20, 0, dev->nvr);
            nvr_lock_set(0xa0, 0x20, 0, dev->nvr);
            nvr_lock_set(0xc0, 0x20, 0, dev->nvr);
            nvr_lock_set(0xe0, 0x20, 0, dev->nvr);
        }

        w83977_kbc_handler(dev);
    }

    w83977_superio_handler(dev);

    for (int i = 0; i < 3; i++) {
        dev->gpio[i].reg    = 0xff;
        dev->gpio[i].pulldn = 0xff;

        w83977_gpio_handler(dev, i);
    }

    dev->locked = 0;
}

static void
w83977_close(void *priv)
{
    w83977_t *dev = (w83977_t *) priv;

    next_id = 0;

    free(dev);
}

static void *
w83977_init(const device_t *info)
{
    w83977_t *dev = (w83977_t *) calloc(1, sizeof(w83977_t));

    dev->hefras    = info->local & W83977_370;

    dev->id = next_id;

    if (next_id == 1)
        dev->hefras   ^= W83977_370;
    else
        dev->fdc       = device_add(&fdc_at_smc_device);

    dev->uart[0]   = device_add_inst(&ns16550_device, (next_id << 1) + 1);
    dev->uart[1]   = device_add_inst(&ns16550_device, (next_id << 1) + 2);

    dev->lpt       = device_add_inst(&lpt_port_device, next_id + 1);

    dev->type      = info->local & W83977_TYPE;

    dev->kbc_type  = info->local & W83977_KBC;

    dev->has_nvr   = !(info->local & W83977_NO_NVR);

    if (dev->has_nvr && (dev->id != 1)) {
        dev->nvr = device_add_params(&nvr_at_device, (void *) (uintptr_t) NVR_AT_ZERO_DEFAULT);

        nvr_bank_set(0, 0, dev->nvr);
    }

    switch (dev->kbc_type) {
        case W83977_AMI:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_AMI | 0x00004800));
            break;
        case W83977_PHOENIX:
            dev->kbc = device_add_params(&kbc_at_device, (void *) (KBC_VEN_PHOENIX | 0x00041900));
            break;
    }

    /* Set the defaults here so the ports can be removed by w83977_reset(). */
    dev->fdc_base     = (dev->id == 1) ? 0x0000 : 0x03f0;
    dev->lpt_base     = (dev->id == 1) ? 0x0278 : 0x0378;
    dev->uart_base[0] = (dev->id == 1) ? 0x03e8 : 0x03f8;
    dev->uart_base[1] = (dev->id == 1) ? 0x02e8 : 0x02f8;
    dev->nvr_base     = (dev->id == 1) ? 0x0000 : 0x0070;
    dev->kbc_base[0]  = (dev->id == 1) ? 0x0000 : 0x0060;
    dev->kbc_base[1]  = (dev->id == 1) ? 0x0000 : 0x0064;

    for (int i = 0; i < 3; i++) {
        dev->gpio[i].id     = i + 1;
 
        dev->gpio[i].reg    = 0xff;
        dev->gpio[i].pulldn = 0xff;

        for (int j = 0; j < 4; j++)
            dev->gpio[i].alt[j] = 0xff;

        dev->gpio[i].parent = dev;
    }

    w83977_reset(dev);

    next_id++;

    return dev;
}

const device_t w83977_device = {
    .name          = "Winbond W83977F/TF/EF Super I/O",
    .internal_name = "w83977",
    .flags         = 0,
    .local         = 0,
    .init          = w83977_init,
    .close         = w83977_close,
    .reset         = w83977_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
