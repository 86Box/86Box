/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Emulation of the Winbond W837x7F/IF Super I/O Chip.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020-2025 Miran Grca.
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/timer.h>
#include <86box/mem.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/gameport.h>
#include <86box/sio.h>

#define FDDA_TYPE             (dev->regs[7] & 3)
#define FDDB_TYPE             ((dev->regs[7] >> 2) & 3)
#define FDDC_TYPE             ((dev->regs[7] >> 4) & 3)
#define FDDD_TYPE             ((dev->regs[7] >> 6) & 3)

#define FD_BOOT               (dev->regs[8] & 3)
#define SWWP                  ((dev->regs[8] >> 4) & 1)
#define DISFDDWR              ((dev->regs[8] >> 5) & 1)

#define EN3MODE               ((dev->regs[9] >> 5) & 1)

#define DRV2EN_NEG            (dev->regs[0xB] & 1)        /* 0 = drive 2 installed */
#define INVERTZ               ((dev->regs[0xB] >> 1) & 1) /* 0 = invert DENSEL polarity */
#define IDENT                 ((dev->regs[0xB] >> 3) & 1)

#define HEFERE                ((dev->regs[0xC] >> 5) & 1)

typedef struct w837x7_t {
    uint8_t  tries;
    uint8_t  has_ide;
    uint8_t  type;
    uint8_t  hefere;
    uint8_t  max_reg;
    uint8_t  regs[256];
    int      locked;
    int      rw_locked;
    int      cur_reg;
    int      key;
    int      ide_start;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;
    void     *gameport;
} w837x7_t;

#ifdef ENABLE_W837X7_LOG
int w837x7_do_log = ENABLE_W837X7_LOG;

static void
w837x7_log(const char *fmt, ...)
{
    va_list ap;

    if (w837x7_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define w837x7_log(fmt, ...)
#endif

static void
w837x7_serial_handler(w837x7_t *dev, int uart)
{
    int      urs0      = !!(dev->regs[0x01] & (0x01 << uart));
    int      urs1      = !!(dev->regs[0x01] & (0x04 << uart));
    int      urs2      = !!(dev->regs[0x03] & (0x08 >> uart));
    int      urs;
    int      irq       = COM1_IRQ;
    uint16_t addr      = COM1_ADDR;
    uint16_t enable    = 1;
    double   clock_src = 24000000.0 / 13.0;

    if (dev->regs[0x03] & (1 << (1 - uart)))
        clock_src = 24000000.0 / 12.0;

    urs = (urs1 << 1) | urs0;

    if (urs2) {
        addr = uart ? COM1_ADDR : COM2_ADDR;
        irq  = uart ? COM1_IRQ : COM2_IRQ;
    } else {
        switch (urs) {
            case 0x00:
                addr = uart ? COM3_ADDR : COM4_ADDR;
                irq  = uart ? COM3_IRQ : COM4_IRQ;
                break;
            case 0x01:
                addr = uart ? COM4_ADDR : COM3_ADDR;
                irq  = uart ? COM4_IRQ : COM3_IRQ;
                break;
            case 0x02:
                addr = uart ? COM2_ADDR : COM1_ADDR;
                irq  = uart ? COM2_IRQ : COM1_IRQ;
                break;
            case 0x03:
            default:
                enable = 0;
                break;
        }
    }

    if (dev->regs[0x04] & (0x20 >> uart))
        enable = 0;

    serial_remove(dev->uart[uart]);
    if (enable)
        serial_setup(dev->uart[uart], addr, irq);

    serial_set_clock_src(dev->uart[uart], clock_src);
}

static void
w837x7_lpt_handler(w837x7_t *dev)
{
    int      ptras        = (dev->regs[1] >> 4) & 0x03;
    uint16_t lpt_port     = 0x0000;
    uint16_t mask         = 0xfffc;
    uint8_t  local_enable = 1;
    uint8_t  lpt_irq      = LPT1_IRQ;
    uint8_t  lpt_mode     = (dev->regs[0x09] & 0x80) | (dev->regs[0x00] & 0x0c);

    switch (ptras) {
        case 0x00:
            lpt_port     = LPT_MDA_ADDR;
            lpt_irq      = LPT_MDA_IRQ;
            break;
        case 0x01:
            lpt_port     = LPT2_ADDR;
            lpt_irq      = LPT1_IRQ /*LPT2_IRQ*/;
            break;
        case 0x02:
            lpt_port     = LPT1_ADDR;
            lpt_irq      = LPT1_IRQ /*LPT2_IRQ*/;
            break;

        default:
            local_enable = 0;
            break;
    }

    if (dev->regs[0x04] & 0x80)
        local_enable = 0;

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    lpt_port_remove(dev->lpt);
    lpt_set_fifo_threshold(dev->lpt, dev->regs[0x05] & 0x0f);
    switch (lpt_mode) {
        default:
            local_enable = 0;
            break;
        case 0x00:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 1);
            break;
        case 0x84:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x88:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
        case 0x8c:
            mask = 0xfff8;
            lpt_set_epp(dev->lpt, 1);
            lpt_set_ecp(dev->lpt, 1);
            lpt_set_ext(dev->lpt, 0);
            break;
    }

    if (local_enable && (lpt_port >= 0x0100) && (lpt_port <= (0x0ffc & mask)))
        lpt_port_setup(dev->lpt, lpt_port);

    lpt_port_irq(dev->lpt, lpt_irq);
}

static void
w837x7_gameport_handler(w837x7_t *dev)
{
    if (!(dev->regs[3] & 0x40) && !(dev->regs[4] & 0x40))
        gameport_remap(dev->gameport, 0x201);
    else
        gameport_remap(dev->gameport, 0);
}

static void
w837x7_fdc_handler(w837x7_t *dev)
{
    fdc_remove(dev->fdc);
    if (!(dev->regs[0] & 0x20))
        fdc_set_base(dev->fdc, (dev->regs[0] & 0x10) ? FDC_PRIMARY_ADDR : FDC_SECONDARY_ADDR);
    fdc_set_power_down(dev->fdc, !!(dev->regs[6] & 0x08));
}

static void
w837x7_ide_handler(w837x7_t *dev)
{
    if (dev->has_ide > 0) {
        int ide_id = dev->has_ide - 1;

        ide_handlers(ide_id, 0);

        ide_set_base_addr(ide_id, 0, (dev->regs[0x00] & 0x40) ? 0x01f0 : 0x0170);
        ide_set_base_addr(ide_id, 1, (dev->regs[0x00] & 0x40) ? 0x03f6 : 0x0376);

        if (!(dev->regs[0x00] & 0x80))
            ide_handlers(ide_id, 1);
    }
}

static void
w837x7_write(uint16_t port, uint8_t val, void *priv)
{
    w837x7_t *dev    = (w837x7_t *) priv;
    uint8_t    valxor = 0;

    if (port == 0x0250) {
        if (val == dev->key)
            dev->locked = 1;
        else
            dev->locked = 0;
        return;
    } else if (port == 0x0251) {
        dev->cur_reg = val;
        return;
    } else {
        if (dev->locked) {
            if (dev->rw_locked && (dev->cur_reg <= 0x0b))
                return;
            valxor                  = val ^ dev->regs[dev->cur_reg];
            dev->regs[dev->cur_reg] = val;
        } else
            return;
    }

    if (dev->cur_reg <= dev->max_reg)  switch (dev->cur_reg) {
        case 0x00:
            w837x7_log("REG 00: %02X\n", val);
            if (valxor & 0xc0)
                w837x7_ide_handler(dev);
            if (valxor & 0x30)
                w837x7_fdc_handler(dev);
            if (valxor & 0x0c)
                w837x7_lpt_handler(dev);
            break;
        case 0x01:
            if (valxor & 0x80)
                fdc_set_swap(dev->fdc, (dev->regs[1] & 0x80) ? 1 : 0);
            if (valxor & 0x30)
                w837x7_lpt_handler(dev);
            if (valxor & 0x0a)
                w837x7_serial_handler(dev, 1);
            if (valxor & 0x05)
                w837x7_serial_handler(dev, 0);
            break;
        case 0x03:
            if (valxor & 0x80)
                w837x7_lpt_handler(dev);
            if (valxor & 0x40)
                w837x7_gameport_handler(dev);
            if (valxor & 0x0a)
                w837x7_serial_handler(dev, 0);
            if (valxor & 0x05)
                w837x7_serial_handler(dev, 1);
            break;
        case 0x04:
            if (valxor & 0x10)
                w837x7_serial_handler(dev, 1);
            if (valxor & 0x20)
                w837x7_serial_handler(dev, 0);
            if (valxor & 0x80)
                w837x7_lpt_handler(dev);
            if (valxor & 0x40)
                w837x7_gameport_handler(dev);
            break;
        case 0x05:
            if (valxor & 0x0f)
                w837x7_lpt_handler(dev);
            break;
        case 0x06:
            if (valxor & 0x08)
                w837x7_fdc_handler(dev);
            break;
        case 0x07:
            if (valxor & 0x03)
                fdc_update_rwc(dev->fdc, 0, FDDA_TYPE);
            if (valxor & 0x0c)
                fdc_update_rwc(dev->fdc, 1, FDDB_TYPE);
            if (valxor & 0x30)
                fdc_update_rwc(dev->fdc, 2, FDDC_TYPE);
            if (valxor & 0xc0)
                fdc_update_rwc(dev->fdc, 3, FDDD_TYPE);
            break;
        case 0x08:
            if (valxor & 0x03)
                fdc_update_boot_drive(dev->fdc, FD_BOOT);
            if (valxor & 0x10)
                fdc_set_swwp(dev->fdc, SWWP ? 1 : 0);
            if (valxor & 0x20)
                fdc_set_diswr(dev->fdc, DISFDDWR ? 1 : 0);
            break;
        case 0x09:
            if (valxor & 0x20)
                fdc_update_enh_mode(dev->fdc, EN3MODE ? 1 : 0);
            if (valxor & 0x40)
                dev->rw_locked = (val & 0x40) ? 1 : 0;
            if (valxor & 0x80)
                w837x7_lpt_handler(dev);
            break;
        case 0x0b:
            if ((valxor & 0x0c) && (dev->type == W83777F)) {
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
            break;
        case 0x0c:
            if (dev->type == W83787IF)
                dev->key = 0x88 | HEFERE;
            break;

        default:
            break;
    }
}

static uint8_t
w837x7_read(uint16_t port, void *priv)
{
    w837x7_t *dev = (w837x7_t *) priv;
    uint8_t    ret = 0xff;

    if (dev->locked) {
        if (port == 0x0251)
            ret = dev->cur_reg;
        else if (port == 0x0252) {
            if (dev->cur_reg == 7)
                ret = (fdc_get_rwc(dev->fdc, 0) | (fdc_get_rwc(dev->fdc, 1) << 2));
            else if (!dev->rw_locked || (dev->cur_reg > 0x0b))
                ret = dev->regs[dev->cur_reg];
        }
    }

    return ret;
}

static void
w837x7_reset(w837x7_t *dev)
{
    memset(dev->regs, 0x00, dev->max_reg + 1);

    if (dev->has_ide == 0x02)
        dev->regs[0x00] = 0x90;
    else
        dev->regs[0x00] = 0xd0;

    if (dev->ide_start)
        dev->regs[0x00] &= 0x7f;

    dev->regs[0x01] = 0x2c;
    dev->regs[0x03] = 0x30;
    dev->regs[0x09] = dev->type;
    dev->regs[0x0a] = 0x1f;

    if (dev->type == W83787IF) {
        dev->regs[0x0c] = 0x0c | dev->hefere;
        dev->regs[0x0d] = 0x03;
    } else
        dev->regs[0x0c] = dev->hefere;

    dev->key = 0x88 | HEFERE;

    fdc_reset(dev->fdc);
    fdc_clear_flags(dev->fdc, FDC_FLAG_PS2 | FDC_FLAG_PS2_MCA);

    w837x7_fdc_handler(dev);

    w837x7_lpt_handler(dev);
    w837x7_serial_handler(dev, 0);
    w837x7_serial_handler(dev, 1);
    w837x7_gameport_handler(dev);
    w837x7_ide_handler(dev);

    dev->locked    = 0;
    dev->rw_locked = 0;
}

static void
w837x7_close(void *priv)
{
    w837x7_t *dev = (w837x7_t *) priv;

    free(dev);
}

static void *
w837x7_init(const device_t *info)
{
    w837x7_t *dev = (w837x7_t *) calloc(1, sizeof(w837x7_t));

    dev->type      = info->local & 0x0f;
    dev->hefere    = info->local & W837X7_KEY_89;
    dev->max_reg   = (dev->type == W83787IF) ? 0x15 : ((dev->type == W83787F) ? 0x0a : 0x0b);
    dev->has_ide   = (info->local >> 16) & 0xff;
    dev->ide_start = !!(info->local & W837X7_IDE_START);

    dev->fdc       = device_add(&fdc_at_winbond_device);

    dev->uart[0]   = device_add_inst(&ns16550_device, 1);
    dev->uart[1]   = device_add_inst(&ns16550_device, 2);

    dev->lpt       = device_add_inst(&lpt_port_device, 1);
    lpt_set_cnfgb_readout(dev->lpt, 0x3f);

    dev->gameport  = gameport_add(&gameport_sio_1io_device);

    w837x7_reset(dev);

    io_sethandler(0x250, 0x0004,
                  w837x7_read, NULL, NULL, w837x7_write, NULL, NULL, dev);

    return dev;
}

const device_t w837x7_device = {
    .name          = "Winbond W837x7 Super I/O",
    .internal_name = "w837x7",
    .flags         = 0,
    .local         = 0,
    .init          = w837x7_init,
    .close         = w837x7_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
