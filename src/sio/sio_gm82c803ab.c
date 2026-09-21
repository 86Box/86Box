/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of the GoldStar GM82C803 A andB Super I/O
 *          Chips.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2025 Miran Grca.
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
#include <86box/pci.h>
#include <86box/lpt.h>
#include <86box/serial.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/fdd.h>
#include <86box/fdc.h>
#include <86box/sio.h>

typedef struct gm82c803ab_t {
    uint8_t   type;
    uint8_t   tries;
    uint8_t   has_ide;
    uint8_t   regs[256];
    int       cur_reg;
    int       com3_addr;
    int       com4_addr;
    fdc_t    *fdc;
    serial_t *uart[2];
    lpt_t    *lpt;
} gm82c803ab_t;

#ifdef ENABLE_GM82C803AB_LOG
int gm82c803ab_do_log = ENABLE_GM82C803AB_LOG;

static void
gm82c803ab_log(const char *fmt, ...)
{
    va_list ap;

    if (gm82c803ab_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define gm82c803ab_log(fmt, ...)
#endif

static void
ide_handler(gm82c803ab_t *dev)
{
    if (dev->has_ide > 0) {
        int ide_id = dev->has_ide - 1;

        ide_handlers(ide_id, 0);

        ide_set_base_addr(ide_id, 0, (dev->regs[0xa1] & 0x80) ? 0x0170 : 0x01f0);
        ide_set_base_addr(ide_id, 1, (dev->regs[0xa1] & 0x80) ? 0x0376 : 0x03f6);

        if (dev->regs[0xa0] & 0x20)
            ide_handlers(ide_id, 1);
    }
}

static void
fdc_handler(gm82c803ab_t *dev)
{
    fdc_remove(dev->fdc);
    if (dev->regs[0xa0] & 0x10)
        fdc_set_base(dev->fdc, (dev->regs[0xa1] & 0x40) ? FDC_SECONDARY_ADDR : FDC_PRIMARY_ADDR);
}

static void
set_com34_addr(gm82c803ab_t *dev)
{
    switch (dev->regs[0xa4] & 0xc0) {
        case 0x00:
            dev->com3_addr = COM3_ADDR;
            dev->com4_addr = COM4_ADDR;
            break;
        case 0x40:
            dev->com3_addr = 0x338;
            dev->com4_addr = 0x238;
            break;
        case 0x80:
            dev->com3_addr = COM3_ADDR;
            dev->com4_addr = 0x2e0;
            break;
        case 0xc0:
            dev->com3_addr = 0x220;
            dev->com4_addr = 0x228;
            break;

        default:
            break;
    }
}

static void
set_serial_addr(gm82c803ab_t *dev, int port)
{
    uint8_t shift     = 2 + (port << 1);
    double  clock_src = 24000000.0 / 13.0;

    if (dev->regs[0xa4] & (1 << (4 + port)))
        clock_src = 24000000.0 / 12.0;

    serial_remove(dev->uart[port]);
    if (dev->regs[0xa0] & (0x04 << port)) {
        switch ((dev->regs[0xa1] >> shift) & 0x03) {
            case 0x00:
                serial_setup(dev->uart[port], COM1_ADDR, COM1_IRQ);
                break;
            case 0x01:
                serial_setup(dev->uart[port], COM2_ADDR, COM2_IRQ);
                break;
            case 0x02:
                serial_setup(dev->uart[port], dev->com3_addr, COM3_IRQ);
                break;
            case 0x03:
                serial_setup(dev->uart[port], dev->com4_addr, COM4_IRQ);
                break;

            default:
                break;
        }
    }

    serial_set_clock_src(dev->uart[port], clock_src);
}

static void
lpt_handler(gm82c803ab_t *dev)
{
    uint16_t lpt_port     = 0x0000;
    uint16_t mask         = 0xfffc;
    uint8_t  local_enable = 1;
    uint8_t  lpt_irq      = LPT1_IRQ;
    uint8_t  lpt_mode     = (dev->regs[0xa0] & 0x03);

    switch (lpt_mode) {
        default:
            local_enable = 0;
            break;
        case 0x00:
            lpt_set_epp(dev->lpt, 0);
            lpt_set_ecp(dev->lpt, 0);
            lpt_set_ext(dev->lpt, 1);
            break;
        case 0x01:
            if (dev->type == GM82C803B) {
                lpt_set_epp(dev->lpt, !!(dev->regs[0xa5] & 0x20));
                lpt_set_ecp(dev->lpt, 1);
                lpt_set_ext(dev->lpt, 0);
            } else
                local_enable = 0;
            break;
        case 0x02:
            if (dev->type == GM82C803B) {
                mask = 0xfff8;
                lpt_set_epp(dev->lpt, 1);
                lpt_set_ecp(dev->lpt, !!(dev->regs[0xa5] & 0x20));
                lpt_set_ext(dev->lpt, 0);
            } else
                local_enable = 0;
            break;
    }

    switch (dev->regs[0xa1] & 0x03) {
        default:
            lpt_port     = LPT_MDA_ADDR;
            lpt_irq      = LPT_MDA_IRQ;
            break;
        case 0x00:
            lpt_port     = LPT1_ADDR;
            lpt_irq      = LPT1_IRQ /*LPT2_IRQ*/;
            break;
        case 0x01:
            lpt_port     = LPT2_ADDR;
            lpt_irq      = LPT1_IRQ /*LPT2_IRQ*/;
            break;
    }

    if (lpt_irq > 15)
        lpt_irq = 0xff;

    lpt_port_remove(dev->lpt);
    lpt_set_fifo_threshold(dev->lpt, dev->regs[0x0a] & 0x0f);

    if (local_enable && (lpt_port >= 0x0100) && (lpt_port <= (0x0ffc & mask)))
        lpt_port_setup(dev->lpt, lpt_port);

    lpt_port_irq(dev->lpt, lpt_irq);
}

static void
gm82c803ab_write(uint16_t port, uint8_t val, void *priv)
{
    gm82c803ab_t *dev    = (gm82c803ab_t *) priv;
    uint8_t       valxor = 0;

    if (dev->tries == 2) {
        if (port == 0x0398) {
            if (val == 0xcc)
                dev->tries = 0;
            else
                dev->cur_reg = val;
        } else {
            if ((dev->cur_reg < 0xa0) || (dev->cur_reg > 0xa5))
                return;

            valxor                  = val ^ dev->regs[dev->cur_reg];
            dev->regs[dev->cur_reg] = val;

            switch (dev->cur_reg) {
                default:
                    break;
                case 0xa0: /* Function Selection Register (FSR) */
                    if (valxor & 0x20)
                        ide_handler(dev);
                    if (valxor & 0x10)
                        fdc_handler(dev);
                    if (valxor & 0x08)
                        set_serial_addr(dev, 1);
                    if (valxor & 0x04)
                        set_serial_addr(dev, 0);
                    if (valxor & 0x03)
                        lpt_handler(dev);
                    break;
                case 0xa1: /* Address Selection Register (ASR) */
                    if (valxor & 0x80)
                        ide_handler(dev);
                    if (valxor & 0x40)
                        fdc_handler(dev);
                    if (valxor & 0x30)
                        set_serial_addr(dev, 1);
                    if (valxor & 0x0c)
                        set_serial_addr(dev, 0);
                    if (valxor & 0x03)
                        lpt_handler(dev);
                    break;
                case 0xa4: /* Miscellaneous Function Register */
                    if (valxor & 0xc0) {
                        set_com34_addr(dev);
                        set_serial_addr(dev, 0);
                        set_serial_addr(dev, 1);
                    }
                    if (valxor & 0x20)
                        set_serial_addr(dev, 1);
                    if (valxor & 0x10)
                        set_serial_addr(dev, 0);
                    if (valxor & 0x01)
                        fdc_set_swap(dev->fdc, val & 0x01);
                    break;
                case 0xa5: /* ECP Register */
                    if (valxor & 0x20)
                        lpt_handler(dev);
                    break;
            }
        }
    } else if ((port == 0x0398) && (val == 0x33))
        dev->tries++;
}

static uint8_t
gm82c803ab_read(uint16_t port, void *priv)
{
    const gm82c803ab_t *dev = (gm82c803ab_t *) priv;
    uint8_t             ret = 0xff;

    if (dev->tries == 2) {
        if ((port == 0x0399) && (dev->cur_reg >= 0xa0) && (dev->cur_reg <= 0xa1))
            ret = dev->regs[dev->cur_reg];
    }

    return ret;
}

static void
gm82c803ab_reset(gm82c803ab_t *dev)
{
    dev->com3_addr = 0x338;
    dev->com4_addr = 0x238;

    serial_remove(dev->uart[0]);
    serial_setup(dev->uart[0], COM1_ADDR, COM1_IRQ);

    serial_remove(dev->uart[1]);
    serial_setup(dev->uart[1], COM2_ADDR, COM2_IRQ);

    lpt_port_remove(dev->lpt);
    lpt_port_setup(dev->lpt, LPT1_ADDR);

    fdc_reset(dev->fdc);
    fdc_remove(dev->fdc);

    dev->tries = 0;
    memset(dev->regs, 0, 256);

    dev->regs[0xa0] = 0xff;

    set_serial_addr(dev, 0);
    set_serial_addr(dev, 1);

    lpt_handler(dev);

    fdc_handler(dev);

    if (dev->has_ide)
        ide_handler(dev);
}

static void
gm82c803ab_close(void *priv)
{
    gm82c803ab_t *dev = (gm82c803ab_t *) priv;

    free(dev);
}

static void *
gm82c803ab_init(const device_t *info)
{
    gm82c803ab_t *dev = (gm82c803ab_t *) calloc(1, sizeof(gm82c803ab_t));

    dev->fdc = device_add(&fdc_at_smc_device);

    dev->type    = info->local & 0xff;
    dev->has_ide = (info->local >> 8) & 0xff;

    dev->uart[0] = device_add_inst(&ns16550_device, 1);
    dev->uart[1] = device_add_inst(&ns16550_device, 2);

    dev->lpt = device_add_inst(&lpt_port_device, 1);
    lpt_set_cnfgb_readout(dev->lpt, 0x00);

    io_sethandler(0x0398, 0x0002,
                  gm82c803ab_read, NULL, NULL, gm82c803ab_write, NULL, NULL, dev);

    gm82c803ab_reset(dev);

    return dev;
}

const device_t gm82c803ab_device = {
    .name          = "Goldstar GMC82C803A/B",
    .internal_name = "gm82c803ab",
    .flags         = 0,
    .local         = 0,
    .init          = gm82c803ab_init,
    .close         = gm82c803ab_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
