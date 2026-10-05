/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the ALi M5123/1543C Super I/O Chip.
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
#include <86box/pic.h>
#include <86box/pci.h>
#include <86box/keyboard.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include "cpu.h"
#include <86box/sio.h>

#define AB_RST 0x80

typedef struct ali5123_t {
    uint8_t   chip_id;
    uint8_t   no_uart3;
    uint8_t   is_apm;
    uint8_t   tries;
    uint8_t   regs[48];
    uint8_t   ld_regs[13][256];
    int       locked;
    int       cur_reg;
    fdc_t    *fdc;
    serial_t *uart[3];
    lpt_t    *lpt;
} ali5123_t;

static void    ali5123_write(uint16_t port, uint8_t val, void *priv);
static uint8_t ali5123_read(uint16_t port, void *priv);

static uint16_t
make_port(ali5123_t *dev, uint8_t ld)
{
    uint16_t r0 = dev->ld_regs[ld][0x60];
    uint16_t r1 = dev->ld_regs[ld][0x61];

    uint16_t p = (r0 << 8) + r1;

    return p;
}

static void
ali5123_fdc_handler(ali5123_t *dev)
{
    uint16_t ld_port       = 0;
    uint8_t  global_enable = !(dev->regs[0x22] & (1 << 0));
    uint8_t  local_enable  = !!dev->ld_regs[0][0x30];

    fdc_remove(dev->fdc);
    if (global_enable && local_enable) {
        ld_port = make_port(dev, 0) & 0xFFF8;
        if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
            fdc_set_base(dev->fdc, ld_port);
    }
}

static void
ali5123_lpt_handler(ali5123_t *dev)
{
    uint16_t ld_port         = 0x0000;
    uint16_t mask            = 0xfffc;
    uint8_t  global_enable   = !(dev->regs[0x22] & (1 << 3));
    uint8_t  local_enable    = !!dev->ld_regs[3][0x30];
    uint8_t  lpt_irq         = dev->ld_regs[3][0x70];
    uint8_t  lpt_dma         = dev->ld_regs[3][0x74];
    uint8_t  lpt_mode        = dev->ld_regs[3][0xf0] & 0x07;
    uint8_t  irq_readout[16] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x38, 0x00, 0x08,
                                 0x00, 0x10, 0x18, 0x20, 0x00, 0x28, 0x30, 0x00 };

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    if (lpt_dma >= 4)
        lpt_dma = 0xff;

    lpt_port_remove(dev->lpt);
    lpt_set_fifo_threshold(dev->lpt, (dev->ld_regs[3][0xf0] & 0x78) >> 3);
    if ((lpt_mode == 0x04) && (dev->ld_regs[3][0xf1] & 0x80))
        lpt_mode = 0x00;
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

/* The serial ports are assigned by the pins they drive, not by ALi's UART
   numbering:
   - dev->uart[0] (COM1) is UART1 on SIN1/SOUT1, logical device 4;
   - dev->uart[1] (COM2) is the UART on SIN2/SOUT2: UART2 on the M1543,
     UART3 on the M1543C/M5123;
   - dev->uart[2] (COM3) is the M1543C/M5123's UART2, the IrDA/FIR port.
   On the M1543C/M5123, register 2Dh bit 5 (default 1) puts UART3 at
   logical device 5 and UART2 at logical device B, and 0 the other way round.
   The M1543 has no UART3: logical device 5 is always UART2, and 2Dh is
   reserved. */
static uint8_t
ali5123_uart_ld(const ali5123_t *dev, int uart)
{
    switch (uart) {
        case 0:
            return 0x04;
        case 1:
            return (dev->no_uart3 || (dev->regs[0x2d] & 0x20)) ? 0x05 : 0x0b;
        default:
            return (dev->regs[0x2d] & 0x20) ? 0x0b : 0x05;
    }
}

/* ALi's UART number, as used by the power down bits in 22h/23h. */
static int
ali5123_uart_chip_no(const ali5123_t *dev, int uart)
{
    switch (uart) {
        case 0:
            return 1;
        case 1:
            return dev->no_uart3 ? 2 : 3;
        default:
            return 2;
    }
}

static int
ali5123_ld_uart(const ali5123_t *dev, uint8_t ld)
{
    for (int uart = 0; uart < 3; uart++) {
        if (dev->uart[uart] && (ali5123_uart_ld(dev, uart) == ld))
            return uart;
    }

    return -1;
}

static void
ali5123_serial_handler(ali5123_t *dev, int uart)
{
    uint16_t ld_port       = 0;
    uint8_t  uart_no       = ali5123_uart_ld(dev, uart);
    int      chip_no       = ali5123_uart_chip_no(dev, uart);
    uint8_t  global_enable = !(dev->regs[0x22] & (1 << (3 + chip_no)));
    uint8_t  local_enable  = !!dev->ld_regs[uart_no][0x30];
    /* The IR UART has no 2 MHz (MIDI) clock option. */
    uint8_t  mask          = (!dev->no_uart3 && (chip_no == 2)) ? 0x04 : 0x05;

    if (dev->uart[uart] == NULL)
        return;

    serial_remove(dev->uart[uart]);
    if (global_enable && local_enable) {
        ld_port = make_port(dev, uart_no) & 0xFFF8;
        if ((ld_port >= 0x0100) && (ld_port <= 0x0FF8))
            serial_setup(dev->uart[uart], ld_port, dev->ld_regs[uart_no][0x70]);
    }

    switch (dev->ld_regs[uart_no][0xf0] & mask) {
        case 0x00:
            serial_set_clock_src(dev->uart[uart], 1843200.0);
            break;
        case 0x04:
            serial_set_clock_src(dev->uart[uart], 8000000.0);
            break;
        case 0x01:
        case 0x05:
            serial_set_clock_src(dev->uart[uart], 2000000.0);
            break;

        default:
            break;
    }
}

static void
ali5123_reset(void *priv)
{
    ali5123_t *dev = (ali5123_t *) priv;

    memset(dev->regs, 0, 48);

    dev->regs[0x20] = 0x43;
    dev->regs[0x21] = 0x15;
    dev->regs[0x2d] = 0x20;

    for (uint8_t i = 0; i < 13; i++)
        memset(dev->ld_regs[i], 0, 256);

    /* Logical device 0: FDD */
    dev->ld_regs[0][0x60] = 3;
    dev->ld_regs[0][0x61] = 0xf0;
    dev->ld_regs[0][0x70] = 6;
    dev->ld_regs[0][0x74] = 2;
    dev->ld_regs[0][0xf0] = 0x08;
    dev->ld_regs[0][0xf2] = 0xff;

    /* Logical device 3: Parallel Port */
    dev->ld_regs[3][0x60] = 3;
    dev->ld_regs[3][0x61] = 0x78;
    dev->ld_regs[3][0x70] = 5;
    dev->ld_regs[3][0x74] = 4;
    dev->ld_regs[3][0xf0] = 0x8c;
    dev->ld_regs[3][0xf1] = 0x85;

    /* Logical device 4: UART1 */
    dev->ld_regs[4][0x60] = 3;
    dev->ld_regs[4][0x61] = 0xf8;
    dev->ld_regs[4][0x70] = 4;
    dev->ld_regs[4][0xf2] = 0x0c;

    /* Logical device 5: the UART on the second serial port's pins (UART3 on
       the M1543C/M5123 with 2Dh bit 5 at its default, UART2 on the M1543) */
    dev->ld_regs[5][0x60] = 2;
    dev->ld_regs[5][0x61] = 0xf8;
    dev->ld_regs[5][0x70] = 3;
    dev->ld_regs[5][0xf2] = 0x0c;

    /* Logical device 7: Keyboard */
    dev->ld_regs[7][0x30] = 1;
    dev->ld_regs[7][0x70] = 1;
    /* TODO: Register F0 bit 6: 0 = PS/2, 1 = AT */

    /* Logical device B: UART2, the IrDA/FIR port (M1543C/M5123 only) */
    dev->ld_regs[0x0b][0x60] = 3;
    dev->ld_regs[0x0b][0x61] = 0xe8;
    dev->ld_regs[0x0b][0x70] = 9;
    dev->ld_regs[0x0b][0x74] = 4;
    dev->ld_regs[0x0b][0xf0] = 0x80;

    /* Logical device C: Hotkey */
    dev->ld_regs[0x0c][0xf0] = 0x35;
    dev->ld_regs[0x0c][0xf1] = 0x14;
    dev->ld_regs[0x0c][0xf2] = 0x11;
    dev->ld_regs[0x0c][0xf3] = 0x71;
    dev->ld_regs[0x0c][0xf4] = 0x42;

    ali5123_lpt_handler(dev);
    ali5123_serial_handler(dev, 0);
    ali5123_serial_handler(dev, 1);
    ali5123_serial_handler(dev, 2);

    fdc_reset(dev->fdc);
    ali5123_fdc_handler(dev);

    dev->locked = 0;
}

static void
ali5123_write(uint16_t port, uint8_t val, void *priv)
{
    ali5123_t *dev    = (ali5123_t *) priv;
    uint8_t    index  = (port & 1) ? 0 : 1;
    uint8_t    valxor = 0x00;
    uint8_t    cur_ld = dev->regs[7];

    if (index) {
        if (((val == 0x51) && (!dev->tries) && (!dev->locked)) || ((val == 0x23) && (dev->tries) && (!dev->locked))) {
            if (dev->tries) {
                dev->locked = 1;
                fdc_3f1_enable(dev->fdc, 0);
                dev->tries = 0;
            } else
                dev->tries++;
        } else {
            if (dev->locked) {
                if (val == 0xbb) {
                    dev->locked = 0;
                    fdc_3f1_enable(dev->fdc, 1);
                    return;
                }
                dev->cur_reg = val;
            } else {
                if (dev->tries)
                    dev->tries = 0;
            }
        }
        return;
    } else {
        if (dev->locked) {
            if (dev->cur_reg < 48) {
                valxor = val ^ dev->regs[dev->cur_reg];
                if ((val >= 0x1f) && (val <= 0x21))
                    return;
                dev->regs[dev->cur_reg] = val;
            } else {
                valxor = val ^ dev->ld_regs[cur_ld][dev->cur_reg];
                if (((dev->cur_reg & 0xf0) == 0x70) && (cur_ld < 4) && (cur_ld != 3))
                    return;
                /* Block writes to some logical devices. */
                if (cur_ld > 0x0c)
                    return;
                else
                    switch (cur_ld) {
                        case 0x01:
                        case 0x02:
                        case 0x06:
                        case 0x08 ... 0x0a:
                            return;
                        case 0x07:
                            if (dev->cur_reg == 0xf0)
                                val &= 0xbf;
                            break;
                        case 0x0b:
                            if (dev->no_uart3)
                                return;
                            break;

                        default:
                            break;
                    }
                dev->ld_regs[cur_ld][dev->cur_reg] = val;
            }
        } else
            return;
    }

    if (dev->cur_reg < 48) {
        switch (dev->cur_reg) {
            case 0x02:
                if (val & 0x01)
                    ali5123_reset(dev);
                dev->regs[0x02] = 0x00;
                break;
            case 0x22:
                if (valxor & 0x01)
                    ali5123_fdc_handler(dev);
                if (valxor & 0x08)
                    ali5123_lpt_handler(dev);
                for (int uart = 0; uart < 3; uart++) {
                    if (valxor & (1 << (3 + ali5123_uart_chip_no(dev, uart))))
                        ali5123_serial_handler(dev, uart);
                }
                break;
            case 0x2d:
                /* The register blocks belong to the UARTs, so they trade
                   logical device numbers along with them. */
                if ((valxor & 0x20) && !dev->no_uart3) {
                    uint8_t tmp[256];

                    memcpy(tmp, dev->ld_regs[5], 256);
                    memcpy(dev->ld_regs[5], dev->ld_regs[0x0b], 256);
                    memcpy(dev->ld_regs[0x0b], tmp, 256);
                    ali5123_serial_handler(dev, 1);
                    ali5123_serial_handler(dev, 2);
                }
                break;

            default:
                break;
        }

        return;
    }

    cur_ld = dev->regs[7];
    switch (cur_ld) {
        case 0:
            /* FDD */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] &= ~0x01;
                    if (valxor)
                        ali5123_fdc_handler(dev);
                    break;
                case 0xf0:
                    if (valxor & 0x08)
                        fdc_update_enh_mode(dev->fdc, !(val & 0x08));
                    if (valxor & 0x10)
                        fdc_set_swap(dev->fdc, (val & 0x10) >> 4);
                    break;
                case 0xf1:
                    if (valxor & 0xc)
                        fdc_update_densel_force(dev->fdc, (val & 0xc) >> 2);
                    break;
                case 0xf4:
                    if (valxor & 0x08)
                        fdc_update_drvrate(dev->fdc, 0, (val & 0x08) >> 3);
                    break;
                case 0xf5:
                    if (valxor & 0x08)
                        fdc_update_drvrate(dev->fdc, 1, (val & 0x08) >> 3);
                    break;
                case 0xf6:
                    if (valxor & 0x08)
                        fdc_update_drvrate(dev->fdc, 2, (val & 0x08) >> 3);
                    break;
                case 0xf7:
                    if (valxor & 0x08)
                        fdc_update_drvrate(dev->fdc, 3, (val & 0x08) >> 3);
                    break;

                default:
                    break;
            }
            break;
        case 3:
            /* Parallel port */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                case 0x74:
                case 0xf0:
                case 0xf1:
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] &= ~0x08;
                    if (valxor)
                        ali5123_lpt_handler(dev);
                    break;

                default:
                    break;
            }
            break;
        case 4:
        case 5:
        case 0x0b:
            /* Serial ports */
            switch (dev->cur_reg) {
                case 0x30:
                case 0x60:
                case 0x61:
                case 0x70:
                case 0xf0: {
                    int uart = ali5123_ld_uart(dev, cur_ld);

                    if (uart < 0)
                        break;
                    if ((dev->cur_reg == 0x30) && (val & 0x01))
                        dev->regs[0x22] &= ~(1 << (3 + ali5123_uart_chip_no(dev, uart)));
                    if (valxor)
                        ali5123_serial_handler(dev, uart);
                    break;
                }

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

static uint8_t
ali5123_read(uint16_t port, void *priv)
{
    const ali5123_t *dev   = (ali5123_t *) priv;
    uint8_t          index = (port & 1) ? 0 : 1;
    uint8_t          ret   = 0xff;
    uint8_t          cur_ld;

    if (dev->locked) {
        if (index)
            ret = dev->cur_reg;
        else {
            if (dev->cur_reg < 0x30) {
                if (dev->cur_reg == 0x20)
                    ret = dev->chip_id;
                else
                    ret = dev->regs[dev->cur_reg];
            } else {
                cur_ld = dev->regs[7];
                ret    = dev->ld_regs[cur_ld][dev->cur_reg];
            }
        }
    }

    return ret;
}

static void
ali5123_close(void *priv)
{
    ali5123_t *dev = (ali5123_t *) priv;

    free(dev);
}

static void *
ali5123_init(const device_t *info)
{
    ali5123_t *dev = (ali5123_t *) calloc(1, sizeof(ali5123_t));

    dev->fdc = device_add_params(&fdc_at_ali_device, (void *) FDC_FLAG_PNP);

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);
    dev->no_uart3 = !!(info->local & ALI5123_NO_UART3);
    if (!dev->no_uart3)
        dev->uart[2] = device_add_inst(&ns16550_device, 3);
    dev->lpt     = device_add_inst(&lpt_port_device, 1);

    dev->chip_id = info->local & 0xff;

    ali5123_reset(dev);

    if (info->local & ALI5123_370)
        io_sethandler(FDC_SECONDARY_ADDR, 0x0002,
                      ali5123_read, NULL, NULL, ali5123_write, NULL, NULL, dev);
    else
        io_sethandler(FDC_PRIMARY_ADDR, 0x0002,
                      ali5123_read, NULL, NULL, ali5123_write, NULL, NULL, dev);

    device_add_params(&kbc_at_device, (void *) KBC_VEN_ALI);

    return dev;
}

const device_t ali5123_device = {
    .name          = "ALi M5123/M1543C Super I/O",
    .internal_name = "ali5123",
    .flags         = 0,
    .local         = 0x40,
    .init          = ali5123_init,
    .close         = ali5123_close,
    .reset         = ali5123_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
