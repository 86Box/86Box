/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the SMC FDC37C669 Super I/O Chip.
 *
 *
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2024 Miran Grca.
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
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

typedef struct fdc37c669_t {
    uint8_t   id;
    uint8_t   tries;
    uint8_t   regs[42];
    int       locked;
    int       rw_locked;
    int       cur_reg;
    fdc_t    *fdc;
    serial_t *uart[2];
} fdc37c669_t;

static int next_id = 0;

#ifdef ENABLE_FDC37C669_LOG
int fdc37c669_do_log = ENABLE_FDC37C669_LOG;

static void
fdc37c669_log(const char *fmt, ...)
{
    va_list ap;

    if (fdc37c669_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define fdc37c669_log(fmt, ...)
#endif

static void
fdc37c669_fdc_handler(fdc37c669_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0x20] & 0xc0)
        fdc_set_base(dev->fdc, ((uint16_t) dev->regs[0x20]) << 2);
}

static void
fdc37c669_uart_handler(fdc37c669_t *dev, uint8_t uart)
{
    uint8_t uart_reg   = 0x24 + uart;
    uint8_t pwrdn_mask = 0x08 << (uart << 2);
    uint8_t uart_shift = ((uart ^ 1) << 2);

    serial_remove(dev->uart[uart]);
    if ((dev->regs[0x02] & pwrdn_mask) && (dev->regs[uart_reg] & 0xc0))
        serial_setup(dev->uart[0], ((uint16_t) dev->regs[0x24]) << 2,
                     (dev->regs[0x28] >> uart_shift) & 0x0f);
}

static double
fdc37c669_uart_get_clock_src(fdc37c669_t *dev, uint8_t uart)
{
    double clock_srcs[4] = { 24000000.0 / 13.0, 24000000.0 / 12.0, 24000000.0 / 3.0, 24000000.0 / 3.0 };
    double ret;
    uint8_t clock_src_0 = !!(dev->regs[0x04] & (0x10 << uart));
    uint8_t clock_src_1 = !!(dev->regs[0x0c] & (0x40 << uart));
    uint8_t clock_src = clock_src_0 | (clock_src_1 << 1);

    ret = clock_srcs[clock_src];

    return ret;
}

static void
fdc37c669_lpt_handler(fdc37c669_t *dev)
{
    uint8_t mask = ~(dev->regs[0x04] & 0x01);

    lpt_port_remove(dev->id);
    if ((dev->regs[0x01] & 0x04) && (dev->regs[0x23] >= 0x40))
        lpt_port_setup(dev->id, ((uint16_t) (dev->regs[0x23] & mask)) << 2);
}

static void
fdc37c669_write(uint16_t port, uint8_t val, void *priv)
{
    fdc37c669_t *dev    = (fdc37c669_t *) priv;
    uint8_t      index  = (port & 1) ? 0 : 1;
    uint8_t      valxor = val ^ dev->regs[dev->cur_reg];

    fdc37c669_log("[%04X:%08X] [W] %04X = %02X (%i, %i)\n", CS, cpu_state.pc, port, val,
                  dev->tries, dev->locked);

    if (index) {
        if ((val == 0x55) && !dev->locked) {
            dev->tries = (dev->tries + 1) & 1;

            if (!dev->tries)
                dev->locked = 1;
        } else {
            if (dev->locked) {
                if (val == 0xaa)
                    dev->locked = 0;
                else
                    dev->cur_reg = val;
            } else
                dev->tries = 0;
        }
    } else if (!dev->rw_locked || (dev->cur_reg > 0x0f))  switch (dev->cur_reg) {
        case 0x00:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x74) | (val & 0x8b);
            if (!dev->id && (valxor & 8))
                fdc_set_power_down(dev->fdc, !(val & 0x08));
            break;
        case 0x01:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x73) | (val & 0x8c);
            if (valxor & 0x04)
                fdc37c669_lpt_handler(dev);
            if (valxor & 0x80)
                dev->rw_locked = !(val & 0x80);
            break;
        case 0x02:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x77) | (val & 0x88);
            if (valxor & 0x08)
                fdc37c669_uart_handler(dev, 0);
            if (valxor & 0x80)
                fdc37c669_uart_handler(dev, 1);
            break;
        case 0x03:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x08) | (val & 0xf7);
            if (!dev->id && (valxor & 0x02))
                fdc_update_enh_mode(dev->fdc, !!(val & 0x02));
            break;
        case 0x04:
            dev->regs[dev->cur_reg] = val;
            if (valxor & 0x03)
                fdc37c669_lpt_handler(dev);
            if (valxor & 0x10)
                serial_set_clock_src(dev->uart[0], fdc37c669_uart_get_clock_src(dev, 0));
            if (valxor & 0x20)
                serial_set_clock_src(dev->uart[1], fdc37c669_uart_get_clock_src(dev, 1));
            break;
        case 0x05:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x83) | (val & 0x7c);
            if (!dev->id && (valxor & 0x18))
                fdc_update_densel_force(dev->fdc, (val & 0x18) >> 3);
            if (!dev->id && (valxor & 0x20))
                fdc_set_swap(dev->fdc, (val & 0x20) >> 5);
            break;
        case 0x06:
            dev->regs[dev->cur_reg] = val;
            break;
        case 0x07:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x06) | (val & 0xf9);
            break;
        case 0x08:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x0f) | (val & 0xf0);
            break;
        case 0x09:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x38) | (val & 0xc7);
            break;
        case 0x0a:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0xf0) | (val & 0x0f);
            break;
        case 0x0b:
            dev->regs[dev->cur_reg] = val;
            if (!dev->id && (valxor & 0x03))
                fdc_update_rwc(dev->fdc, 0, val & 0x03);
            if (!dev->id && (valxor & 0x0c))
                fdc_update_rwc(dev->fdc, 1, (val & 0x0c) >> 2);
            break;
        case 0x0c:
            dev->regs[dev->cur_reg] = val;
            if (valxor & 0x40)
                serial_set_clock_src(dev->uart[0], fdc37c669_uart_get_clock_src(dev, 0));
            if (valxor & 0x80)
                serial_set_clock_src(dev->uart[1], fdc37c669_uart_get_clock_src(dev, 1));
            break;
        case 0x0f:
        case 0x12 ... 0x1f:
            dev->regs[dev->cur_reg] = val;
            break;
        case 0x10:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x07) | (val & 0xf8);
            break;
        case 0x11:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0xfc) | (val & 0x03);
            break;
        case 0x20:
            dev->regs[dev->cur_reg] = val & 0xfc;
            if (!dev->id && (valxor & 0xfc))
                fdc37c669_fdc_handler(dev);
            break;
        case 0x21:
            dev->regs[dev->cur_reg] = val & 0xfc;
            break;
        case 0x22:
            dev->regs[dev->cur_reg] = (dev->regs[dev->cur_reg] & 0x03) | (val & 0xfc);
            break;
        case 0x23:
            dev->regs[dev->cur_reg] = val;
            if (valxor)
                fdc37c669_lpt_handler(dev);
            break;
        case 0x24:
            dev->regs[dev->cur_reg] = val & 0xfe;
            if (valxor & 0xfe)
                fdc37c669_uart_handler(dev, 0);
            break;
        case 0x25:
            dev->regs[dev->cur_reg] = val & 0xfe;
            if (valxor & 0xfe)
                fdc37c669_uart_handler(dev, 1);
            break;
        case 0x26:
            dev->regs[dev->cur_reg] = val;
            if (valxor & 0xf0)
                fdc_set_dma_ch(dev->fdc, val >> 4);
            break;
        case 0x27:
            dev->regs[dev->cur_reg] = val;
            if (valxor & 0xf0)
                fdc_set_irq(dev->fdc, val >> 4);
            if (valxor & 0x0f)
                lpt_port_irq(dev->id, val & 0x0f);
            break;
        case 0x28:
            dev->regs[dev->cur_reg] = val;
            if (valxor & 0xf0)
                fdc37c669_uart_handler(dev, 0);
            if (valxor & 0x0f)
                fdc37c669_uart_handler(dev, 1);
            break;
        case 0x29:
            dev->regs[dev->cur_reg] = val & 0x0f;
            break;
    }
}

static uint8_t
fdc37c669_read(uint16_t port, void *priv)
{
    const fdc37c669_t *dev   = (fdc37c669_t *) priv;
    uint8_t            index = (port & 1) ? 0 : 1;
    uint8_t            ret   = 0xff;

    if (dev->locked) {
        if (index)
            ret = dev->cur_reg;
        else if (!dev->rw_locked || (dev->cur_reg > 0x0f))
            ret = dev->regs[dev->cur_reg];
    }

    fdc37c669_log("[%04X:%08X] [R] %04X = %02X (%i, %i)\n", CS, cpu_state.pc, port, ret,
                  dev->tries, dev->locked);

    return ret;
}

static void
fdc37c669_reset(void *priv)
{
    fdc37c669_t *dev = (fdc37c669_t *) priv;

    memset(dev->regs, 0x00, 42);

    dev->regs[0x00] = 0x28;
    dev->regs[0x01] = 0x9c;
    dev->regs[0x02] = 0x88;
    dev->regs[0x03] = 0x78;
    dev->regs[0x06] = 0xff;
    dev->regs[0x0d] = 0x03;
    dev->regs[0x0e] = 0x02;
    dev->regs[0x1e] = 0x3c;    /* Gameport controller. */
    dev->regs[0x20] = 0x3c;
    dev->regs[0x21] = 0x3c;
    dev->regs[0x22] = 0x3d;

    if (dev->id != 1) {
        fdc_reset(dev->fdc);
        fdc37c669_fdc_handler(dev);
    }

    fdc37c669_uart_handler(dev, 0);
    serial_set_clock_src(dev->uart[0], fdc37c669_uart_get_clock_src(dev, 0));

    fdc37c669_uart_handler(dev, 1);
    serial_set_clock_src(dev->uart[1], fdc37c669_uart_get_clock_src(dev, 1));

    fdc37c669_lpt_handler(dev);

    dev->locked    = 0;
    dev->rw_locked = 0;
}

static void
fdc37c669_close(void *priv)
{
    fdc37c669_t *dev = (fdc37c669_t *) priv;

    next_id = 0;

    free(dev);
}

static void *
fdc37c669_init(const device_t *info)
{
    fdc37c669_t *dev = (fdc37c669_t *) calloc(1, sizeof(fdc37c669_t));

    dev->id = next_id;

    if (next_id != 1)
        dev->fdc = device_add(&fdc_at_smc_device);

    dev->uart[0] = device_add_inst(&ns16550_device, (next_id << 1) + 1);
    dev->uart[1] = device_add_inst(&ns16550_device, (next_id << 1) + 2);

    io_sethandler(info->local ? FDC_SECONDARY_ADDR : (next_id ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR),
                  0x0002, fdc37c669_read, NULL, NULL, fdc37c669_write, NULL, NULL, dev);

    fdc37c669_reset(dev);

    next_id++;

    return dev;
}

const device_t fdc37c669_device = {
    .name          = "SMC FDC37C669 Super I/O",
    .internal_name = "fdc37c669",
    .flags         = 0,
    .local         = 0,
    .init          = fdc37c669_init,
    .close         = fdc37c669_close,
    .reset         = fdc37c669_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};

const device_t fdc37c669_370_device = {
    .name          = "SMC FDC37C669 Super I/O (Port 370h)",
    .internal_name = "fdc37c669_370",
    .flags         = 0,
    .local         = 1,
    .init          = fdc37c669_init,
    .close         = fdc37c669_close,
    .reset         = fdc37c669_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
