/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SMC FDC37M60x and FDC37M70x Super I/O
 *          Chips.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
 */
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/lpt.h>
#include <86box/pic.h>
#include <86box/serial.h>
#include <86box/fdc.h>
#include <86box/keyboard.h>
#include <86box/sio.h>

typedef struct fdc37mx0x_t {
    uint8_t       chip_id;
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
    pc_timer_t    watchdog_timer;
} fdc37mx0x_t;

static void    fdc37mx0x_write(uint16_t port, uint8_t val, void *priv);
static uint8_t fdc37mx0x_read(uint16_t port, void *priv);

fdc37mx0x_t *fdc37mx0x = NULL;

static uint16_t
make_port_superio(const fdc37mx0x_t *dev)
{
    const uint16_t r0 = dev->regs[0x26];
    const uint16_t r1 = dev->regs[0x27];

    const uint16_t p = (r1 << 8) + r0;

    return p;
}

static uint16_t
make_port(const fdc37mx0x_t *dev, const uint8_t ld)
{
    const uint16_t r0 = dev->ld_regs[ld][0x60];
    const uint16_t r1 = dev->ld_regs[ld][0x61];

    const uint16_t p = (r0 << 8) + r1;

    return p;
}

static void
fdc37mx0x_watchdog_timeout(void *priv)
{
    fdc37mx0x_t *dev = (fdc37mx0x_t *) priv;
    const int    irq = (dev->ld_regs[0x08][0xf3] & 0xf0) >> 4;

    dev->ld_regs[0x08][0xf4] |= 0x01;

    if ((irq != 0) && (irq != 2))
        picint(1 << irq);
}

static void
fdc37mx0x_p2_write_hook(void *priv, uint8_t old_p2, uint8_t new_p2)
{
    fdc37mx0x_t *dev = (fdc37mx0x_t *) priv;

    if ((dev->ld_regs[0x08][0xf4] & 0x08) && !(old_p2 & 0x01) && (new_p2 & 0x01))
        fdc37mx0x_watchdog_timeout(dev);
}

static void
fdc37mx0x_watchdog_irq_reset(const fdc37mx0x_t *dev)
{
   const int irq = (dev->ld_regs[0x08][0xf3] & 0xf0) >> 4;

    if ((irq != 0) && (irq != 2))
        picintc(1 << irq);
}

static void
fdc37mx0x_watchdog_reset(fdc37mx0x_t *dev)
{
    double period = (double) dev->ld_regs[0x08][0xf2] * 1000000.0;

    if (!(dev->ld_regs[0x08][0xf1] & 0x80))
        period *= 60.0;

    dev->ld_regs[0x08][0xf4] &= 0xfe;

    if (timer_is_on(&dev->watchdog_timer))
        timer_stop(&dev->watchdog_timer);

    if (dev->ld_regs[0x08][0xf2] == 0x00)
        timer_on_auto(&dev->watchdog_timer, period);

    fdc37mx0x_watchdog_irq_reset(dev);
}

void
fdc37mx0x_watchdog_reset_ext(const int origin)
{
    if (fdc37mx0x != NULL) {
        if (fdc37mx0x->ld_regs[0x08][0xf3] & (1 << origin))
            fdc37mx0x_watchdog_reset(fdc37mx0x);
    }
}

static uint8_t
fdc37mx0x_gpio_read(uint16_t port, void *priv)
{
    fdc37mx0x_t *dev = (fdc37mx0x_t *) priv;
    uint8_t      ret = 0xff;

    if (dev->locked) {
        if (dev->is_compaq)
            ret = fdc37mx0x_read(port & 0x0001, priv);
    } else if (port & 0x0001)  switch (dev->gpio_reg) {
        default:
            break;
        case 0x03:
            ret = dev->ld_regs[0x08][0xf4];
            break;
        case 0x0c ... 0x0f:
            ret = dev->ld_regs[0x08][dev->gpio_reg + 0xa8];
            if (dev->gpio_reg == 0x0f)
                dev->ld_regs[0x08][dev->gpio_reg + 0xa8] &= 0xfb;
            break;
    } else
        ret = dev->gpio_reg;

    return ret;
}

static void
fdc37mx0x_gpio_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37mx0x_t *dev = (fdc37mx0x_t *) priv;

    if (dev->locked) {
        if (dev->is_compaq)
            fdc37mx0x_write(port & 0x0001, val, priv);
    } else if (port & 0x0001)  switch (dev->gpio_reg) {
        default:
            break;
        case 0x03:
            dev->ld_regs[0x08][0xf4] = val & 0x09;
            if (val & 0x04)
                fdc37mx0x_watchdog_timeout(dev);
            break;
        case 0x0c:
            dev->ld_regs[0x08][dev->gpio_reg + 0xa8] = val & 0x9e;
            break;
        case 0x0d:
            dev->ld_regs[0x08][dev->gpio_reg + 0xa8] = val & 0x57;
            break;
    } else
        dev->gpio_reg = val;
}

static void
fdc37mx0x_superio_handler(fdc37mx0x_t *dev)
{
    if (!dev->is_compaq) {
        if (dev->superio_base != 0x0000)
            io_removehandler(dev->superio_base, 0x0002,
                             fdc37mx0x_read, NULL, NULL, fdc37mx0x_write, NULL, NULL, dev);
        dev->superio_base = make_port_superio(dev);
        if (dev->superio_base != 0x0000)
            io_sethandler(dev->superio_base, 0x0002,
                          fdc37mx0x_read, NULL, NULL, fdc37mx0x_write, NULL, NULL, dev);
    }
}

static void
fdc37mx0x_fdc_handler(fdc37mx0x_t *dev)
{
    const uint8_t  global_enable = !!(dev->regs[0x22] & (1 << 0));
    const uint8_t  local_enable  = !!dev->ld_regs[0][0x30];

    dev->fdc_base = 0x0000;

    if (global_enable && local_enable)
        dev->fdc_base = make_port(dev, 0) & 0xfff8;

    fdc_remove(dev->fdc);

    if ((dev->fdc_base >= 0x0100) && (dev->fdc_base <= 0x0ff8))
        fdc_set_base(dev->fdc, dev->fdc_base);
}

static void
fdc37mx0x_lpt_handler(fdc37mx0x_t *dev)
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
fdc37mx0x_serial_handler(fdc37mx0x_t *dev, const int uart)
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
fdc37mx0x_kbc_handler(fdc37mx0x_t *dev)
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
fdc37mx0x_gpio_handler(fdc37mx0x_t *dev)
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
                         fdc37mx0x_gpio_read, NULL, NULL, fdc37mx0x_gpio_write, NULL, NULL, dev);

        if (dev->gpio_base > 0x0000)
            io_sethandler(dev->gpio_base, 0x0002,
                          fdc37mx0x_gpio_read, NULL, NULL, fdc37mx0x_gpio_write, NULL, NULL, dev);
    }
}

static void
fdc37mx0x_state_change(fdc37mx0x_t *dev, const uint8_t locked)
{
    dev->locked = locked;
    fdc_3f1_enable(dev->fdc, !locked);
}

static void
fdc37mx0x_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37mx0x_t *dev    = (fdc37mx0x_t *) priv;
    uint8_t      index  = !(port & 1);
    uint8_t      valxor;

    if (port == 0x00fb) {
        fdc37mx0x_state_change(dev, 1);
        dev->tries = 0;
    } else if (port == 0x00f9)
        fdc37mx0x_state_change(dev, 0);
    else if (index) {
        if ((!dev->is_compaq) && (val == 0x55) && !dev->locked) {
            fdc37mx0x_state_change(dev, 1);
            dev->tries = 0;
        } else if (dev->locked) {
            if ((!dev->is_compaq) && (val == 0xaa))
                fdc37mx0x_state_change(dev, 0);
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
                        fdc37mx0x_state_change(dev, 0);
                    break;
                case 0x03:
                    if (dev->chip_id == FDC37M70X) {
                        dev->regs[dev->cur_reg] = val & 0x83;
                        fdc37mx0x_gpio_handler(dev);
                    }
                    break;
                case 0x07: case 0x26:
                case 0x2b ... 0x2f:
                    dev->regs[dev->cur_reg] = val;
                    break;
                case 0x22:
                    dev->regs[dev->cur_reg] = val & 0x39;

                    if (valxor & 0x01)
                        fdc37mx0x_fdc_handler(dev);
                    if (valxor & 0x08)
                        fdc37mx0x_lpt_handler(dev);
                    if (valxor & 0x10)
                        fdc37mx0x_serial_handler(dev, 0);
                    if (valxor & 0x20)
                        fdc37mx0x_serial_handler(dev, 1);
                    break;
                case 0x23:
                    dev->regs[dev->cur_reg] = val & 0x39;
                    break;
                case 0x24:
                    dev->regs[dev->cur_reg] = val & 0x4e;
                    break;
                case 0x27:
                    dev->regs[dev->cur_reg] = val;
                    fdc37mx0x_superio_handler(dev);
                    break;
                default:
                    break;
            }
        } else {
            valxor = val ^ dev->ld_regs[dev->regs[7]][dev->cur_reg];

            if (dev->regs[7] <= dev->max_ld)  switch (dev->regs[7]) {
                default:
                    break;
                case 0x00:    /* FDD */
                    switch (dev->cur_reg) {
                        default:
                            break;
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x01;
                            if (valxor)
                                fdc37mx0x_fdc_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xef;

                            if (valxor & 0x01)
                                fdc_update_enh_mode(dev->fdc, val & 0x01);
                            if (valxor & 0x0c) {
                                fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
                                switch (val & 0x0c) {
                                    default:
                                        break;
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
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0d;
                            else
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xfc;

                            if (valxor & 0x0c)
                                fdc_update_densel_force(dev->fdc, (val & 0xc) >> 2);
                            if ((dev->chip_id == FDC37M70X) & (valxor & 0x01))
                                fdc_set_swwp(dev->fdc, val & 0x01);
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
                        default:
                            break;
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x08;
                            if (valxor)
                                fdc37mx0x_lpt_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            if (valxor & 0x7f)
                                fdc37mx0x_lpt_handler(dev);
                            break;
                        case 0xf1:
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;
                            break;
                    }
                    break;
                case 0x04:    /* Serial port 1 */
                    switch (dev->cur_reg) {
                        default:
                            break;
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x70:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x10;
                            if (valxor)
                                fdc37mx0x_serial_handler(dev, 0);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x83;

                            if (valxor & 0x83) {
                                fdc37mx0x_serial_handler(dev, 0);
                                fdc37mx0x_serial_handler(dev, 1);
                            }
                            break;
                    }
                    break;
                case 0x05:    /* Serial port 2 */
                    switch (dev->cur_reg) {
                        default:
                            break;
                        case 0x30:
                        case 0x60: case 0x61:
                        case 0x62: case 0x63:
                        case 0x70:
                        case 0x74:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if ((dev->cur_reg == 0x30) && (val & 0x01))
                                dev->regs[0x22] |= 0x20;
                            if (valxor)
                                fdc37mx0x_serial_handler(dev, 1);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x03;

                            if (valxor & 0x03) {
                                fdc37mx0x_serial_handler(dev, 0);
                                fdc37mx0x_serial_handler(dev, 1);
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
                        default:
                            break;
                        case 0x30:
                        case 0x70: case 0x72:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;

                            if (valxor)
                                fdc37mx0x_kbc_handler(dev);
                            break;
                        case 0xf0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x84;
                            break;
                    }
                    break;
                case 0x08:    /* Aux. I/O */
                    switch (dev->cur_reg) {
                        default:
                            break;
                        case 0x30:
                        case 0xb8:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                            break;
                        case 0xb4:
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x9e;
                            break;
                        case 0xb5:
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x57;
                            break;
                        case 0xc0:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = (dev->ld_regs[dev->regs[7]][dev->cur_reg] & 0xe4) | (val & 0x1b);
                            break;
                        case 0xc1:
                            dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x0f;
                            for (int i = 0; i < 4; i++)
                                fdc_set_fdd_changed(dev->fdc, i, !!(val & (1 << i)));
                            break;
                        case 0xc5:
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x01;
                            break;
                        case 0xc6:
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0x01);
                            break;
                        case 0xc7:
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] &= ~(val & 0x1e);
                            break;
                        case 0xc8:
                            if (dev->chip_id == FDC37M70X)
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x1e;
                            break;
                        case 0xf1:
                            if (dev->chip_id == FDC37M70X) {
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x80;
                                if (valxor & 0x80)
                                    fdc37mx0x_watchdog_reset(dev);
                            }
                            break;
                        case 0xf2:
                            if (dev->chip_id == FDC37M70X) {
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val;
                                if (valxor)
                                    fdc37mx0x_watchdog_reset(dev);
                            }
                            break;
                        case 0xf3:
                            if (dev->chip_id == FDC37M70X) {
                                if (valxor & 0xf0)
                                    fdc37mx0x_watchdog_irq_reset(dev);
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0xf7;
                            }
                            break;
                        case 0xf4:
                            if (dev->chip_id == FDC37M70X) {
                                dev->ld_regs[dev->regs[7]][dev->cur_reg] = val & 0x09;
                                if (val & 0x04)
                                    fdc37mx0x_watchdog_timeout(dev);
                            }
                            break;
                    }
                    break;
            }
        }
    }
}

static uint8_t
fdc37mx0x_read(uint16_t port, void *priv)
{
    fdc37mx0x_t *dev   = (fdc37mx0x_t *) priv;
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
                        ret |= (fdc_get_fdd_changed(dev->fdc, i) << i);
                } else if (((dev->chip_id == FDC37M70X) && dev->regs[7] == 0x08) && (dev->cur_reg == 0xb7)) {
                    ret = dev->ld_regs[dev->regs[7]][dev->cur_reg];
                    dev->ld_regs[dev->regs[7]][dev->cur_reg] &= 0xfb;
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
fdc37mx0x_reset(void *priv)
{
    fdc37mx0x_t *dev = (fdc37mx0x_t *) priv;

    memset(dev->regs, 0x00, sizeof(dev->regs));

    if (dev->chip_id == FDC37M70X)
        dev->regs[0x03] = 0x03;
    dev->regs[0x20] = dev->chip_id;
    dev->regs[0x22] = 0x39;
    dev->regs[0x24] = 0x04;
    dev->regs[0x26] = dev->port_370 ? 0x70 : 0xf0;
    dev->regs[0x27] = 0x03;

    if (dev->chip_id == FDC37M70X)
        fdc37mx0x_watchdog_irq_reset(dev);

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

    if (dev->chip_id == FDC37M70X)
        fdc37mx0x_gpio_handler(dev);
    fdc37mx0x_lpt_handler(dev);
    fdc37mx0x_serial_handler(dev, 0);
    fdc37mx0x_serial_handler(dev, 1);

    fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);
    fdc_reset(dev->fdc);

    fdc37mx0x_fdc_handler(dev);

    for (int i = 0; i < 4; i++)
        fdc_set_fdd_changed(dev->fdc, i, 1);

    fdc37mx0x_kbc_handler(dev);

    fdc37mx0x_superio_handler(dev);

    if (dev->chip_id == FDC37M70X)
        fdc37mx0x_watchdog_reset(dev);

    dev->locked = 0;
}

static void
fdc37mx0x_close(void *priv)
{
    fdc37mx0x_t *dev = (fdc37mx0x_t *) priv;

    fdc37mx0x = NULL;

    free(dev);
}

static void *
fdc37mx0x_init(const device_t *info)
{
    fdc37mx0x_t *dev = (fdc37mx0x_t *) calloc(1, sizeof(fdc37mx0x_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0]   = device_add_inst(&ns16550_device, 1);
    dev->uart[1]   = device_add_inst(&ns16550_device, 2);

    dev->lpt       = device_add_inst(&lpt_port_device, 1);

    dev->kbc_type  = info->local & FDC37XXXX_KBC;

    dev->is_compaq = (dev->kbc_type == FDC37XXX1);

    dev->port_370  = !!(info->local & FDC37XXXX_370);

    dev->max_ld    = 8;

    dev->chip_id   = info->local & FDC37XXXX_CHIP_ID;

    if (dev->is_compaq) {
        io_sethandler(0x0f9, 0x0001,
                      fdc37mx0x_read, NULL, NULL, fdc37mx0x_write, NULL, NULL, dev);
        io_sethandler(0x0fb, 0x0001,
                      fdc37mx0x_read, NULL, NULL, fdc37mx0x_write, NULL, NULL, dev);
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

    /* Set the defaults here so the ports can be removed by fdc37mx0x_reset(). */
    dev->fdc_base     = 0x03f0;
    dev->lpt_base     = 0x0378;
    dev->uart_base[0] = 0x03f8;
    dev->uart_base[1] = 0x02f8;
    dev->kbc_base     = 0x0060;

    if (dev->chip_id == FDC37M70X) {
        timer_add(&dev->watchdog_timer, fdc37mx0x_watchdog_timeout, dev, 0);

        if (dev->kbc != NULL)
            kbc_at_set_p2_write_hook(dev->kbc, fdc37mx0x_p2_write_hook, dev);

        fdc37mx0x = dev;
    }

    fdc37mx0x_reset(dev);

    return dev;
}

const device_t fdc37mx0x_device = {
    .name          = "SMC FDC37Mx0x Super I/O",
    .internal_name = "fdc37mx0x",
    .flags         = 0,
    .local         = 0,
    .init          = fdc37mx0x_init,
    .close         = fdc37mx0x_close,
    .reset         = fdc37mx0x_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
